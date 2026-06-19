"""Executor tests — real POSIX subprocesses (project is POSIX-only).

Covers timeout process-group teardown and run_as_user resolution failures.
"""
from __future__ import annotations

import os
import time
from pathlib import Path

import pytest

from codegen.comment_syntax import lookup
from codegen.errors import BlockFailure
from codegen.executor import run_block
from codegen.parser import Block


def _block(body: str, shebang: str = "#!/bin/sh") -> Block:
    return Block(
        file_path=Path("x.sh"),
        start_line=1,
        end_line=2,
        indent="",
        comment_syntax=lookup(Path("x.py")),
        raw_block_text="",
        inner_text=body,
        shebang=shebang,
        pragma={},
        body=body,
    )


def _run(block, *, max_pass_time, env=None, run_as_user=None):
    return run_block(
        block,
        env=env or dict(os.environ),
        cwd=Path.cwd(),
        max_pass_time=max_pass_time,
        pass_outputs=[],
        run_as_user=run_as_user,
    )


def test_happy_path_returns_stdout():
    stdout, elapsed = _run(_block("echo hello"), max_pass_time=5.0)
    assert stdout == "hello\n"
    assert elapsed >= 0.0


def test_nonzero_exit_raises_blockfailure():
    with pytest.raises(BlockFailure) as exc:
        _run(_block("exit 3"), max_pass_time=5.0)
    assert exc.value.reason == "exit:3"


def test_timeout_raises_blockfailure():
    with pytest.raises(BlockFailure) as exc:
        _run(_block("sleep 5"), max_pass_time=0.3)
    assert exc.value.reason == "timeout:pass"


def test_timeout_kills_background_process_group(tmp_path):
    """A backgrounded child must die with the timed-out block, not orphan.

    The block spawns a child that would touch *sentinel* after 1.5s, while the
    block itself sleeps 4s.  With max_pass_time=0.4 the whole process group is
    SIGKILLed; if teardown only killed the direct child, the sentinel would
    still appear.
    """
    sentinel = tmp_path / "leaked"
    body = '( sleep 1.5; touch "$SENTINEL" ) &\nsleep 4\n'
    env = {**os.environ, "SENTINEL": str(sentinel)}

    with pytest.raises(BlockFailure) as exc:
        _run(_block(body), max_pass_time=0.4, env=env)
    assert exc.value.reason == "timeout:pass"

    # Wait past the child's would-be touch time; it must never fire.
    time.sleep(2.0)
    assert not sentinel.exists(), "background child survived the timeout kill"


def test_run_as_user_unknown_user_raises():
    with pytest.raises(BlockFailure) as exc:
        _run(_block("echo hi"), max_pass_time=5.0, run_as_user="codegen_no_such_user_xyz")
    assert exc.value.reason.startswith("user:unknown:")


@pytest.mark.skipif(os.geteuid() == 0, reason="needs an unprivileged euid")
def test_run_as_user_requires_privilege():
    """Dropping to *any* identity needs setuid/setgroups privilege (i.e. root).

    Even re-selecting the current user trips setgroups, so an unprivileged
    codegen must surface a clean user:denied failure rather than crashing.
    """
    me = os.environ.get("USER") or pwd_getlogin()
    with pytest.raises(BlockFailure) as exc:
        _run(_block("echo ok"), max_pass_time=5.0, run_as_user=me)
    assert exc.value.reason.startswith("user:denied:")


def pwd_getlogin() -> str:
    import pwd

    return pwd.getpwuid(os.getuid()).pw_name


@pytest.mark.skipif(os.geteuid() != 0, reason="needs root to actually drop privilege")
def test_run_as_user_drops_privilege_when_root():
    """As root, selecting a user actually runs the block under that uid."""
    import pwd

    target = pwd.getpwuid(os.getuid()).pw_name  # root, but exercises the path
    stdout, _ = _run(_block("id -u"), max_pass_time=5.0, run_as_user=target)
    assert stdout.strip().isdigit()
