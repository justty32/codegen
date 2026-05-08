from __future__ import annotations


def apply_indent(text: str, base_indent: str, auto_indent: bool) -> str:
    """Prepend *base_indent* to every non-empty line in *text* (§5.1).

    If *auto_indent* is False, return *text* unchanged.
    The final newline (if any) is preserved exactly as-is.
    """
    if not auto_indent or not base_indent:
        return text

    ends_with_newline = text.endswith("\n")
    lines = text.splitlines()

    result = []
    for line in lines:
        if line:  # non-empty: add indent
            result.append(base_indent + line)
        else:     # blank: leave empty (no trailing whitespace)
            result.append("")

    joined = "\n".join(result)
    if ends_with_newline:
        joined += "\n"
    return joined
