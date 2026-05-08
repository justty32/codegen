"""End-to-end tests that spawn real subprocesses.  POSIX-only."""
import sys
import textwrap
from pathlib import Path

import pytest

from codegen.comment_syntax import lookup
from codegen.config import Config
from codegen.env import RunContext
from codegen.expander import process_content
from codegen.scope import ScopeStore

POSIX = pytest.mark.skipif(sys.platform == "win32", reason="POSIX-only")


def _ctx(tmp_path: Path, file_path: Path) -> RunContext:
    origin = tmp_path / "origin.txt"
    origin.write_text("")
    origin_block = tmp_path / "origin_block.txt"
    origin_block.write_text("")
    return RunContext(
        invoke_cwd=tmp_path,
        targets=[file_path],
        file_path=file_path,
        origin_file_path=origin,
        origin_block_path=origin_block,
    )


# ---------- single block, C file ----------

C_SINGLE = textwrap.dedent(
    """\
    #include <stdio.h>

    /* CODEGEN_START
    #!/usr/bin/env python3
    print("int generated_var = 42;")
    CODEGEN_END */

    int main(void) { return 0; }
    """
)


@POSIX
def test_single_block_c(tmp_path):
    fp = tmp_path / "foo.c"
    fp.write_text(C_SINGLE)
    cfg = Config()
    scope = ScopeStore.create()
    try:
        scope.open_file()
        result = process_content(C_SINGLE, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()

    assert "int generated_var = 42;" in result
    assert "CODEGEN_START" not in result
    assert "CODEGEN_END" not in result
    assert "#include <stdio.h>" in result


@POSIX
def test_single_block_indented(tmp_path):
    content = textwrap.dedent(
        """\
        void foo() {
            /* CODEGEN_START
            #!/usr/bin/env python3
            print("int x = 1;")
            CODEGEN_END */
        }
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config()
    scope = ScopeStore.create()
    try:
        scope.open_file()
        result = process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()

    assert "    int x = 1;" in result


@POSIX
def test_two_blocks(tmp_path):
    content = textwrap.dedent(
        """\
        /* CODEGEN_START
        #!/usr/bin/env python3
        print("int a = 1;")
        CODEGEN_END */

        /* CODEGEN_START
        #!/usr/bin/env python3
        print("int b = 2;")
        CODEGEN_END */
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config()
    scope = ScopeStore.create()
    try:
        scope.open_file()
        result = process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()

    assert "int a = 1;" in result
    assert "int b = 2;" in result
    assert result.count("CODEGEN_START") == 0


# ---------- keep_as_comment ----------

@POSIX
def test_keep_as_comment(tmp_path):
    content = textwrap.dedent(
        """\
        /* CODEGEN_START
        #!/usr/bin/env python3
        # codegen: keep_as_comment=true
        print("int x = 0;")
        CODEGEN_END */
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config(keep_as_comment=True)
    scope = ScopeStore.create()
    try:
        scope.open_file()
        result = process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()

    assert "int x = 0;" in result
    assert "codegen source (kept)" in result


# ---------- block failure / on_error ----------

@POSIX
def test_block_failure_continue(tmp_path):
    content = textwrap.dedent(
        """\
        /* CODEGEN_START
        #!/usr/bin/env python3
        raise SystemExit(1)
        CODEGEN_END */
        int after;
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config(on_error="continue")
    scope = ScopeStore.create()
    try:
        scope.open_file()
        result = process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()

    # Original block preserved, code after block also preserved
    assert "CODEGEN_START" in result
    assert "int after;" in result


@POSIX
def test_block_failure_abort_file(tmp_path):
    from codegen.errors import BlockFailure

    content = textwrap.dedent(
        """\
        /* CODEGEN_START
        #!/usr/bin/env python3
        raise SystemExit(1)
        CODEGEN_END */
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config(on_error="abort_file")
    scope = ScopeStore.create()
    try:
        scope.open_file()
        with pytest.raises(BlockFailure):
            process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()


@POSIX
def test_block_failure_abort_all(tmp_path):
    from codegen.errors import AbortAll

    content = textwrap.dedent(
        """\
        /* CODEGEN_START
        #!/usr/bin/env python3
        raise SystemExit(1)
        CODEGEN_END */
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config(on_error="abort_all")
    scope = ScopeStore.create()
    try:
        scope.open_file()
        with pytest.raises(AbortAll):
            process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()


# ---------- scope dict shared between blocks ----------

@POSIX
def test_scope_file_shared_across_blocks(tmp_path):
    content = textwrap.dedent(
        """\
        /* CODEGEN_START
        #!/usr/bin/env python3
        import json, os
        d = {}
        d["msg"] = "hello"
        with open(os.environ["CODEGEN_FILE"], "w") as f:
            json.dump(d, f)
        print("")
        CODEGEN_END */

        /* CODEGEN_START
        #!/usr/bin/env python3
        import json, os
        with open(os.environ["CODEGEN_FILE"]) as f:
            d = json.load(f)
        print(f'// {d["msg"]}')
        CODEGEN_END */
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config()
    scope = ScopeStore.create()
    try:
        scope.open_file()
        result = process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()

    assert "// hello" in result


# ---------- Python block, no shebang (implicit python3) ----------

@POSIX
def test_implicit_python3(tmp_path):
    content = textwrap.dedent(
        """\
        /* CODEGEN_START
        print("int implicit = 1;")
        CODEGEN_END */
        """
    )
    fp = tmp_path / "foo.c"
    fp.write_text(content)
    cfg = Config()
    scope = ScopeStore.create()
    try:
        scope.open_file()
        result = process_content(content, cfg, scope, _ctx(tmp_path, fp))
    finally:
        scope.cleanup()

    assert "int implicit = 1;" in result
