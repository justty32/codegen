from __future__ import annotations

import stat
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Mapping

from codegen.errors import BlockFailure
from codegen.parser import Block

_FALLBACK_SHEBANG = "#!/usr/bin/env python3"


def run_block(
    block: Block,
    *,
    env: Mapping[str, str],
    cwd: Path,
    max_pass_time: float,
    pass_outputs: list[str],
) -> tuple[str, float]:
    """Execute a single block subprocess.  Returns (stdout, elapsed_seconds).

    *pass_outputs* is the running list of prior stdout strings for this block
    (used to populate BlockFailure for diagnostics).

    Raises BlockFailure on non-zero exit, timeout, or I/O error.
    """
    shebang = block.shebang or _FALLBACK_SHEBANG
    script_content = shebang + "\n" + block.body

    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".sh",
        delete=False,
        encoding="utf-8",
    ) as tmp:
        tmp.write(script_content)
        tmp_path = Path(tmp.name)

    try:
        tmp_path.chmod(tmp_path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        start = time.monotonic()
        try:
            proc = subprocess.run(
                [str(tmp_path)],
                capture_output=True,
                text=True,
                encoding="utf-8",
                cwd=cwd,
                env=dict(env),
                timeout=max_pass_time,
            )
        except subprocess.TimeoutExpired as exc:
            elapsed = time.monotonic() - start
            stderr = (exc.stderr or b"").decode("utf-8", errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
            raise BlockFailure(
                block=block,
                reason="timeout:pass",
                pass_outputs=list(pass_outputs),
                last_stderr=stderr,
            ) from exc
        except OSError as exc:
            raise BlockFailure(
                block=block,
                reason=f"io:{exc}",
                pass_outputs=list(pass_outputs),
                last_stderr=str(exc),
            ) from exc

        elapsed = time.monotonic() - start

        if proc.returncode != 0:
            raise BlockFailure(
                block=block,
                reason=f"exit:{proc.returncode}",
                pass_outputs=list(pass_outputs),
                last_stderr=proc.stderr,
                exit_code=proc.returncode,
            )

        return proc.stdout, elapsed

    finally:
        try:
            tmp_path.unlink()
        except OSError:
            pass
