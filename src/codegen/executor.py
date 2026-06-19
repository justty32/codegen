from __future__ import annotations

import os
import pwd
import signal
import stat
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Mapping

from codegen.errors import BlockFailure
from codegen.parser import Block

_FALLBACK_SHEBANG = "#!/usr/bin/env python3"


def _resolve_user(spec: str) -> tuple[int, int, list[int]]:
    """Resolve a username or numeric uid string to (uid, gid, supplementary_gids).

    Raises KeyError/ValueError when the user does not exist.
    """
    if spec.isdigit():
        pw = pwd.getpwuid(int(spec))
    else:
        pw = pwd.getpwnam(spec)
    groups = os.getgrouplist(pw.pw_name, pw.pw_gid)
    return pw.pw_uid, pw.pw_gid, groups


def _kill_process_group(proc: subprocess.Popen) -> None:
    """SIGKILL the whole process group started for *proc*.

    The child is launched with ``start_new_session=True`` so it leads its own
    session/process group; killing the group reaps any background children the
    block script spawned (``&``, ``Popen``, forked daemons) which a plain
    ``proc.kill()`` would orphan.
    """
    try:
        pgid = os.getpgid(proc.pid)
    except OSError:
        # Child already gone; nothing grouped to kill.
        try:
            proc.kill()
        except OSError:
            pass
        return
    try:
        os.killpg(pgid, signal.SIGKILL)
    except OSError:
        try:
            proc.kill()
        except OSError:
            pass


def run_block(
    block: Block,
    *,
    env: Mapping[str, str],
    cwd: Path,
    max_pass_time: float,
    pass_outputs: list[str],
    run_as_user: str | None = None,
) -> tuple[str, float]:
    """Execute a single block subprocess.  Returns (stdout, elapsed_seconds).

    *pass_outputs* is the running list of prior stdout strings for this block
    (used to populate BlockFailure for diagnostics).

    When *run_as_user* is set, the block runs with that user's identity
    (setuid/setgid); this requires the codegen process itself to be privileged.

    Raises BlockFailure on non-zero exit, timeout, unknown/denied user, or
    I/O error.  On timeout the entire process group is killed.
    """
    shebang = block.shebang or _FALLBACK_SHEBANG
    script_content = shebang + "\n" + block.body

    popen_extra: dict[str, object] = {}
    # NamedTemporaryFile is created 0600 (owner-only).  A dropped-privilege child
    # is a *different* user, so it must additionally be able to read+exec it.
    script_mode = stat.S_IRWXU  # 0o700
    if run_as_user is not None:
        try:
            uid, gid, groups = _resolve_user(run_as_user)
        except (KeyError, ValueError) as exc:
            raise BlockFailure(
                block=block,
                reason=f"user:unknown:{run_as_user}",
                pass_outputs=list(pass_outputs),
                last_stderr=str(exc),
            ) from exc
        popen_extra["user"] = uid
        popen_extra["group"] = gid
        popen_extra["extra_groups"] = groups
        script_mode = 0o755

    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".sh",
        delete=False,
        encoding="utf-8",
    ) as tmp:
        tmp.write(script_content)
        tmp_path = Path(tmp.name)

    try:
        tmp_path.chmod(script_mode)

        start = time.monotonic()
        try:
            proc = subprocess.Popen(
                [str(tmp_path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                cwd=cwd,
                env=dict(env),
                start_new_session=True,  # own process group -> killable as a unit
                **popen_extra,
            )
        except PermissionError as exc:
            # setuid/setgid refused: codegen is not privileged enough.
            raise BlockFailure(
                block=block,
                reason=f"user:denied:{run_as_user}",
                pass_outputs=list(pass_outputs),
                last_stderr=str(exc),
            ) from exc
        except OSError as exc:
            raise BlockFailure(
                block=block,
                reason=f"io:{exc}",
                pass_outputs=list(pass_outputs),
                last_stderr=str(exc),
            ) from exc

        try:
            stdout, stderr = proc.communicate(timeout=max_pass_time)
        except subprocess.TimeoutExpired as exc:
            _kill_process_group(proc)
            # Reap the killed group and drain whatever it managed to emit.
            try:
                stdout, stderr = proc.communicate()
            except (OSError, ValueError):
                stderr = (
                    exc.stderr.decode("utf-8", errors="replace")
                    if isinstance(exc.stderr, bytes)
                    else (exc.stderr or "")
                )
            raise BlockFailure(
                block=block,
                reason="timeout:pass",
                pass_outputs=list(pass_outputs),
                last_stderr=stderr or "",
            ) from exc

        elapsed = time.monotonic() - start

        if proc.returncode != 0:
            raise BlockFailure(
                block=block,
                reason=f"exit:{proc.returncode}",
                pass_outputs=list(pass_outputs),
                last_stderr=stderr,
                exit_code=proc.returncode,
            )

        return stdout, elapsed

    finally:
        try:
            tmp_path.unlink()
        except OSError:
            pass
