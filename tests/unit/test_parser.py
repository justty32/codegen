from pathlib import Path

import pytest

from codegen.comment_syntax import CommentSyntax
from codegen.errors import ConfigError
from codegen.parser import (
    Block,
    find_top_level_blocks,
    parse_block_header,
    parse_file_pragma,
    parse_pragma_tokens,
)

_FILE = Path("test.c")
_C = CommentSyntax("/*", "*/")
_PY = CommentSyntax("#")


# ---------- pragma token parsing ----------


def test_parse_pragma_tokens_basic():
    d = parse_pragma_tokens("max_passes=5 on_error=abort_all")
    assert d == {"max_passes": "5", "on_error": "abort_all"}


def test_parse_pragma_tokens_empty():
    assert parse_pragma_tokens("") == {}


def test_parse_pragma_tokens_missing_equals():
    with pytest.raises(ConfigError, match="missing '='"):
        parse_pragma_tokens("max_passes")


def test_parse_pragma_tokens_empty_value():
    with pytest.raises(ConfigError, match="empty value"):
        parse_pragma_tokens("max_passes=")


def test_parse_pragma_tokens_empty_key():
    with pytest.raises(ConfigError, match="empty key"):
        parse_pragma_tokens("=5")


# ---------- block header parsing ----------


def test_parse_block_header_full():
    inner = "#!/usr/bin/env python3\n# codegen: max_passes=3\nprint('hi')\n"
    shebang, pragma, body = parse_block_header(inner)
    assert shebang == "#!/usr/bin/env python3"
    assert pragma == {"max_passes": "3"}
    assert body == "print('hi')\n"


def test_parse_block_header_no_shebang():
    inner = "print('hi')\n"
    shebang, pragma, body = parse_block_header(inner)
    assert shebang is None
    assert pragma == {}
    assert body == "print('hi')\n"


def test_parse_block_header_no_pragma():
    inner = "#!/usr/bin/env python3\nprint('hi')\n"
    shebang, pragma, body = parse_block_header(inner)
    assert shebang == "#!/usr/bin/env python3"
    assert pragma == {}
    assert body == "print('hi')\n"


def test_parse_block_header_shebang_only():
    inner = "#!/bin/sh\n"
    shebang, pragma, body = parse_block_header(inner)
    assert shebang == "#!/bin/sh"
    assert body == ""


# ---------- find_top_level_blocks: block-style comments ----------


C_SINGLE = """\
/* CODEGEN_START
#!/usr/bin/env python3
print("int x;")
CODEGEN_END */
"""


def test_find_blocks_c_single():
    blocks = find_top_level_blocks(C_SINGLE, file_path=_FILE, markers=("CODEGEN_START", "CODEGEN_END"), cs=_C)
    assert len(blocks) == 1
    b = blocks[0]
    assert b.start_line == 1
    assert b.end_line == 4
    assert b.indent == ""
    assert b.shebang == "#!/usr/bin/env python3"
    assert 'print("int x;")' in b.body


def test_find_blocks_c_indented():
    content = '    /* CODEGEN_START\n    #!/usr/bin/env python3\n    print("x")\n    CODEGEN_END */\n'
    blocks = find_top_level_blocks(content, file_path=_FILE, markers=("CODEGEN_START", "CODEGEN_END"), cs=_C)
    assert len(blocks) == 1
    assert blocks[0].indent == "    "


def test_find_blocks_c_two():
    content = (
        "/* CODEGEN_START\n#!/usr/bin/env python3\nprint(1)\nCODEGEN_END */\n"
        "some_code();\n"
        "/* CODEGEN_START\n#!/usr/bin/env python3\nprint(2)\nCODEGEN_END */\n"
    )
    blocks = find_top_level_blocks(content, file_path=_FILE, markers=("CODEGEN_START", "CODEGEN_END"), cs=_C)
    assert len(blocks) == 2
    assert blocks[0].start_line == 1
    assert blocks[1].start_line == 6


def test_find_blocks_c_pragma():
    content = (
        "/* CODEGEN_START\n"
        "#!/usr/bin/env python3\n"
        "# codegen: max_passes=5 on_error=abort_all\n"
        'print("x")\n'
        "CODEGEN_END */\n"
    )
    blocks = find_top_level_blocks(content, file_path=_FILE, markers=("CODEGEN_START", "CODEGEN_END"), cs=_C)
    assert blocks[0].pragma == {"max_passes": "5", "on_error": "abort_all"}
    assert 'print("x")' in blocks[0].body


# ---------- find_top_level_blocks: line-style comments ----------


PY_SINGLE = """\
# CODEGEN_START
# #!/usr/bin/env python3
# print("x")
# CODEGEN_END
"""


def test_find_blocks_py_single():
    blocks = find_top_level_blocks(
        PY_SINGLE, file_path=Path("test.py"), markers=("CODEGEN_START", "CODEGEN_END"), cs=_PY
    )
    assert len(blocks) == 1
    b = blocks[0]
    assert b.shebang == "#!/usr/bin/env python3"
    assert 'print("x")' in b.body


def test_find_blocks_none():
    blocks = find_top_level_blocks("int main() {}\n", file_path=_FILE, markers=("CODEGEN_START", "CODEGEN_END"), cs=_C)
    assert blocks == []


# ---------- find_top_level_blocks: custom markers ----------


def test_find_blocks_custom_markers():
    content = "/* <<<\n#!/usr/bin/env python3\nprint(1)\n>>> */\n"
    blocks = find_top_level_blocks(content, file_path=_FILE, markers=("<<<", ">>>"), cs=_C)
    assert len(blocks) == 1
    assert blocks[0].shebang == "#!/usr/bin/env python3"


# ---------- file pragma ----------


def test_file_pragma_block_style():
    content = "/* codegen: markers=<<<,>>> */\nsome code;\n"
    p = parse_file_pragma(content, _C, ("CODEGEN_START", "CODEGEN_END"))
    assert p == {"markers": "<<<,>>>"}


def test_file_pragma_line_style():
    content = "# codegen: max_passes=2\n# some other comment\ndef foo(): pass\n"
    p = parse_file_pragma(content, _PY, ("CODEGEN_START", "CODEGEN_END"))
    assert p == {"max_passes": "2"}


def test_file_pragma_none_when_codegen_block_first():
    p = parse_file_pragma(C_SINGLE, _C, ("CODEGEN_START", "CODEGEN_END"))
    assert p == {}


def test_file_pragma_none_when_no_pragma():
    content = "/* Copyright 2024 */\nsome code;\n"
    p = parse_file_pragma(content, _C, ("CODEGEN_START", "CODEGEN_END"))
    assert p == {}
