from codegen.indent import apply_indent


def test_no_indent_when_disabled():
    assert apply_indent("int x;\n", "    ", False) == "int x;\n"


def test_no_indent_when_empty_base():
    assert apply_indent("int x;\n", "", True) == "int x;\n"


def test_basic_indent():
    assert apply_indent("int x;\n", "    ", True) == "    int x;\n"


def test_multiline_indent():
    result = apply_indent("int x;\nint y;\n", "  ", True)
    assert result == "  int x;\n  int y;\n"


def test_blank_lines_not_indented():
    result = apply_indent("int x;\n\nint y;\n", "  ", True)
    assert result == "  int x;\n\n  int y;\n"


def test_preserves_trailing_newline():
    result = apply_indent("int x;\n", "    ", True)
    assert result.endswith("\n")


def test_no_trailing_newline_preserved():
    result = apply_indent("int x;", "    ", True)
    assert result == "    int x;"
    assert not result.endswith("\n")
