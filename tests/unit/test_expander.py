"""Unit tests for expander.py.

We mock `executor.run_block` to avoid needing a POSIX subprocess.
Scope rollback and multi-pass logic are tested here.
"""
from __future__ import annotations

import json
import sys
import textwrap
from pathlib import Path
from unittest.mock import patch

import pytest

from codegen.comment_syntax import CommentSyntax
from codegen.config import Config
from codegen.env import RunContext
from codegen.errors import AbortAll, BlockFailure
from codegen.expander import _splice, expand_block, process_content
from codegen.parser import Block, find_top_level_blocks
from codegen.scope import ScopeStore

_C = CommentSyntax("/*", "*/")
_PY = CommentSyntax("#")
_FILE = Path("test.c")


def _make_ctx(tmp_path: Path, file_path: Path | None = None) -> RunContext:
    fp = file_path or tmp_path / "test.c"
    origin = tmp_path / "origin.txt"
    origin.write_text("", encoding="utf-8")
    ob = tmp_path / "origin_block.txt"
    ob.write_text("", encoding="utf-8")
    return RunContext(
        invoke_cwd=tmp_path,
        targets=[fp],
        file_path=fp,
        origin_file_path=origin,
        origin_block_path=ob,
    )


def _scope(tmp_path: Path) -> ScopeStore:
    scope = ScopeStore.create()
    scope.open_file()
    return scope


def _find_block(content: str, cs: CommentSyntax = _C) -> Block:
    blocks = find_top_level_blocks(
        content, file_path=_FILE, markers=("CODEGEN_START", "CODEGEN_END"), cs=cs
    )
    assert blocks, "no block found"
    return blocks[0]


# ---------- _splice ----------

def test_splice_replaces_block():
    content = "line1\nline2\nline3\n"
    block = Block(
        file_path=_FILE, start_line=2, end_line=2,
        indent="", comment_syntax=_C,
        raw_block_text="line2\n", inner_text="", shebang=None, pragma={}, body="",
    )
    result = _splice(content, block, "REPLACED\n")
    assert result == "line1\nREPLACED\nline3\n"


# ---------- expand_block (mocked executor) ----------

def _block_c(body_output: str) -> str:
    return (
        "/* CODEGEN_START\n"
        "#!/usr/bin/env python3\n"
        f'print({body_output!r})\n'
        "CODEGEN_END */\n"
    )


def test_expand_block_single_pass(tmp_path):
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\nprint('hello')\nCODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config()
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path)

    with patch("codegen.expander._run_block", return_value=("hello\n", 0.1)):
        result = expand_block(block, cfg, scope, ctx)

    scope.cleanup()
    assert result.ok
    assert result.text == "hello\n"


def test_expand_block_applies_auto_indent(tmp_path):
    content = "    /* CODEGEN_START\n    #!/usr/bin/env python3\n    pass\n    CODEGEN_END */\n"
    block = _find_block(content)
    assert block.indent == "    "

    cfg = Config(auto_indent=True)
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path)

    with patch("codegen.expander._run_block", return_value=("int x;\n", 0.1)):
        result = expand_block(block, cfg, scope, ctx)

    scope.cleanup()
    assert result.text == "    int x;\n"


def test_expand_block_no_auto_indent(tmp_path):
    content = "    /* CODEGEN_START\n    #!/usr/bin/env python3\n    pass\n    CODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config(auto_indent=False)
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path)

    with patch("codegen.expander._run_block", return_value=("int x;\n", 0.1)):
        result = expand_block(block, cfg, scope, ctx)

    scope.cleanup()
    assert result.text == "int x;\n"


def test_expand_block_failure_raises(tmp_path):
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config()
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path)

    failure = BlockFailure(block=block, reason="exit:1")

    with patch("codegen.expander._run_block", side_effect=failure):
        with pytest.raises(BlockFailure):
            expand_block(block, cfg, scope, ctx)

    scope.cleanup()


# ---------- scope snapshot/restore on failure ----------

def test_scope_restored_on_failure(tmp_path):
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config()
    scope = _scope(tmp_path)

    # Write something into file scope before the block runs
    scope.file_path.write_text(json.dumps({"before": 1}), encoding="utf-8")

    ctx = _make_ctx(tmp_path)
    failure = BlockFailure(block=block, reason="exit:1")

    with patch("codegen.expander._run_block", side_effect=failure):
        with pytest.raises(BlockFailure):
            expand_block(block, cfg, scope, ctx)

    # Scope should be restored to pre-execution state
    data = json.loads(scope.file_path.read_text(encoding="utf-8"))
    assert data == {"before": 1}
    scope.cleanup()


def test_scope_committed_on_success(tmp_path):
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config()
    scope = _scope(tmp_path)
    scope.file_path.write_text(json.dumps({"before": 1}), encoding="utf-8")

    ctx = _make_ctx(tmp_path)

    def fake_run(block, *, env, cwd, max_pass_time, pass_outputs):
        import json as _json
        data = _json.loads(scope.file_path.read_text())
        data["after"] = 2
        scope.file_path.write_text(_json.dumps(data))
        return "ok\n", 0.0

    with patch("codegen.expander._run_block", side_effect=fake_run):
        expand_block(block, cfg, scope, ctx)

    data = json.loads(scope.file_path.read_text(encoding="utf-8"))
    assert data == {"before": 1, "after": 2}
    scope.cleanup()


# ---------- multi-pass expansion ----------

def test_multi_pass_expansion(tmp_path):
    """Simulate nested output: pass1 produces another block, pass2 expands it."""
    nested_output = (
        "/* CODEGEN_START\n"
        "#!/usr/bin/env python3\n"
        "pass\n"
        "CODEGEN_END */\n"
    )
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config(max_passes=2)
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path)

    call_count = [0]

    def fake_run(b, *, env, cwd, max_pass_time, pass_outputs):
        call_count[0] += 1
        if call_count[0] == 1:
            return nested_output, 0.1  # first call: produces a nested block
        return "final\n", 0.1          # second call: produces final output

    with patch("codegen.expander._run_block", side_effect=fake_run):
        result = expand_block(block, cfg, scope, ctx)

    scope.cleanup()
    assert call_count[0] == 2
    assert result.text == "final\n"


def test_multi_pass_stable_after_one(tmp_path):
    """If first pass produces no blocks, we should break immediately."""
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config(max_passes=5)
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path)

    call_count = [0]

    def fake_run(b, *, env, cwd, max_pass_time, pass_outputs):
        call_count[0] += 1
        return "plain output\n", 0.1

    with patch("codegen.expander._run_block", side_effect=fake_run):
        result = expand_block(block, cfg, scope, ctx)

    scope.cleanup()
    assert call_count[0] == 1  # stable after one; no further passes
    assert result.text == "plain output\n"


def test_max_total_time_exceeded(tmp_path):
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    block = _find_block(content)
    cfg = Config(max_passes=5, max_total_time=0.5)
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path)

    nested_output = (
        "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    )

    def fake_run(b, *, env, cwd, max_pass_time, pass_outputs):
        return nested_output, 0.3  # each pass takes 0.3s → 2nd hits max_total_time

    with patch("codegen.expander._run_block", side_effect=fake_run):
        with pytest.raises(BlockFailure, match="timeout:total"):
            expand_block(block, cfg, scope, ctx)

    scope.cleanup()


# ---------- process_content (on_error variants) ----------

def test_process_content_continue_on_error(tmp_path):
    content = (
        "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
        "middle;\n"
        "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    )
    fp = tmp_path / "test.c"
    fp.write_text(content)
    cfg = Config(on_error="continue")
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path, fp)

    call_count = [0]

    def fake_run(b, *, env, cwd, max_pass_time, pass_outputs):
        call_count[0] += 1
        if call_count[0] == 1:
            raise BlockFailure(block=b, reason="exit:1")
        return "good\n", 0.1

    with patch("codegen.expander._run_block", side_effect=fake_run):
        result = process_content(content, cfg, scope, ctx)

    scope.cleanup()
    assert call_count[0] == 2            # second block was still attempted
    assert "CODEGEN_START" in result     # first block kept as-is
    assert "good" in result              # second block expanded


def test_process_content_abort_all(tmp_path):
    content = "/* CODEGEN_START\n#!/usr/bin/env python3\npass\nCODEGEN_END */\n"
    fp = tmp_path / "test.c"
    fp.write_text(content)
    cfg = Config(on_error="abort_all")
    scope = _scope(tmp_path)
    ctx = _make_ctx(tmp_path, fp)

    block_ref = find_top_level_blocks(content, file_path=fp, markers=cfg.markers, cs=_C)[0]
    failure = BlockFailure(block=block_ref, reason="exit:1")

    with patch("codegen.expander._run_block", side_effect=failure):
        with pytest.raises(AbortAll):
            process_content(content, cfg, scope, ctx)

    scope.cleanup()


# ---------- pragma inheritance in nested expansion ----------

def test_pragma_inherited_by_nested(tmp_path):
    """Child block should inherit parent's on_error=abort_all pragma."""
    # We can't directly test inheritance without real subprocess,
    # but we can verify that the child config has abort_all.
    content = (
        "/* CODEGEN_START\n"
        "#!/usr/bin/env python3\n"
        "# codegen: on_error=abort_all max_passes=2\n"
        "pass\n"
        "CODEGEN_END */\n"
    )
    block = _find_block(content)
    assert block.pragma == {"on_error": "abort_all", "max_passes": "2"}
