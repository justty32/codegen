from pathlib import Path

import pytest

from codegen.comment_syntax import (
    CommentSyntax,
    default_extensions,
    lookup,
    pragma_prefix_for_shebang,
)


def test_lookup_c():
    cs = lookup(Path("foo.c"))
    assert cs == CommentSyntax("/*", "*/")
    assert cs.is_block


def test_lookup_python():
    cs = lookup(Path("foo.py"))
    assert cs == CommentSyntax("#")
    assert not cs.is_block


def test_lookup_html():
    cs = lookup(Path("foo.html"))
    assert cs == CommentSyntax("<!--", "-->")


def test_lookup_unknown():
    assert lookup(Path("foo.xyz")) is None


def test_lookup_override():
    cs = lookup(Path("foo.pyx"), overrides={".pyx": "#"})
    assert cs == CommentSyntax("#")


def test_lookup_override_block():
    cs = lookup(Path("foo.kts"), overrides={".kts": "/* */"})
    assert cs == CommentSyntax("/*", "*/")


def test_from_string_line():
    assert CommentSyntax.from_string("#") == CommentSyntax("#")


def test_from_string_block():
    assert CommentSyntax.from_string("/* */") == CommentSyntax("/*", "*/")
    assert CommentSyntax.from_string("<!-- -->") == CommentSyntax("<!--", "-->")


def test_default_extensions_include_c():
    exts = default_extensions()
    assert ".c" in exts
    assert ".py" in exts
    assert ".html" in exts


@pytest.mark.parametrize(
    "shebang, expected",
    [
        ("#!/usr/bin/env python3", "#"),
        ("#!/usr/bin/env python", "#"),
        ("#!/bin/sh", "#"),
        ("#!/usr/bin/env node", "//"),
        ("#!/usr/bin/env deno", "//"),
        (None, "#"),
        ("#!/usr/bin/env unknown_lang", None),
    ],
)
def test_pragma_prefix(shebang, expected):
    assert pragma_prefix_for_shebang(shebang) == expected
