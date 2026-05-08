from __future__ import annotations

import re
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

from codegen.comment_syntax import CommentSyntax, pragma_prefix_for_shebang
from codegen.errors import ConfigError


@dataclass
class Block:
    file_path: Path
    start_line: int      # 1-based, line of the START marker
    end_line: int        # 1-based, line of the END marker
    indent: str          # leading whitespace on the START marker line
    comment_syntax: CommentSyntax

    raw_block_text: str  # full text from START line through END line (inclusive)
    inner_text: str      # shebang + pragma + body (comment wrapper stripped)
    shebang: str | None
    pragma: Mapping[str, str]
    body: str


def parse_pragma_tokens(text: str) -> dict[str, str]:
    """Parse `k=v k=v ...` tokens (the part after `codegen:`)."""
    result: dict[str, str] = {}
    for token in text.split():
        if "=" not in token:
            raise ConfigError(f"pragma: token {token!r} missing '='")
        k, v = token.split("=", 1)
        if not k:
            raise ConfigError(f"pragma: empty key in {token!r}")
        if not v:
            raise ConfigError(f"pragma: empty value for key {k!r}")
        result[k] = v
    return result


def _pragma_from_line(line: str, prefix: str) -> dict[str, str]:
    """Try to extract a `<prefix> codegen: k=v ...` directive from a single line."""
    stripped = line.strip()
    if not stripped.startswith(prefix):
        return {}
    rest = stripped[len(prefix):].lstrip()
    if not rest.startswith("codegen:"):
        return {}
    payload = rest[len("codegen:"):].strip()
    if not payload:
        return {}
    return parse_pragma_tokens(payload)


def _pragma_from_text(text: str) -> dict[str, str]:
    """Find a `codegen: k=v ...` directive anywhere in a text fragment."""
    match = re.search(r"codegen:\s*(.+)", text)
    if not match:
        return {}
    payload = match.group(1).strip()
    # strip trailing comment-close markers
    for close in ("*/", "-->", "*/"):
        if payload.endswith(close):
            payload = payload[: -len(close)].rstrip()
    result: dict[str, str] = {}
    for token in payload.split():
        if "=" in token:
            k, v = token.split("=", 1)
            if k and v:
                result[k] = v
    return result


def parse_block_header(inner_text: str) -> tuple[str | None, dict[str, str], str]:
    """Split inner_text → (shebang, pragma_dict, body).

    Rules:
    - Line 0 starting with `#!` → shebang.
    - Next line starting with the appropriate comment prefix + `codegen:` → pragma.
    - Everything else → body.
    """
    lines = inner_text.splitlines(keepends=True)
    idx = 0

    shebang: str | None = None
    if lines and lines[0].lstrip().startswith("#!"):
        shebang = lines[0].strip()
        idx = 1

    pragma: dict[str, str] = {}
    if idx < len(lines):
        prefix = pragma_prefix_for_shebang(shebang)
        if prefix is not None:
            candidate = lines[idx]
            parsed = _pragma_from_line(candidate, prefix)
            if parsed:
                pragma = parsed
                idx += 1

    body = "".join(lines[idx:])
    return shebang, pragma, body


def _detect_indent(line: str) -> str:
    return line[: len(line) - len(line.lstrip())]


def _extract_inner_block_style(lines: list[str], start_idx: int, end_idx: int) -> str:
    """Extract content between block-style comment markers.

    The START marker is on the same line as the comment open:
        /* CODEGEN_START
        #!/usr/bin/env python3
        ...
        CODEGEN_END */

    We skip the first (start-marker) and last (end-marker) lines.
    """
    inner = []
    for i in range(start_idx + 1, end_idx):
        inner.append(lines[i])
    return "".join(inner)


def _extract_inner_line_style(
    lines: list[str], start_idx: int, end_idx: int, cs: CommentSyntax
) -> str:
    """Extract content from line-style comment blocks, stripping the comment prefix.

    # CODEGEN_START
    # #!/usr/bin/env python3
    # print("x")
    # CODEGEN_END

    Each interior line has the comment prefix (e.g. `# `) stripped once.
    """
    inner = []
    prefix = cs.open
    for i in range(start_idx + 1, end_idx):
        line = lines[i]
        stripped = line.lstrip()
        if stripped.startswith(prefix + " "):
            inner.append(stripped[len(prefix) + 1 :])
        elif stripped.startswith(prefix):
            inner.append(stripped[len(prefix) :])
        else:
            inner.append(line)
    return "".join(inner)


def find_top_level_blocks(
    content: str,
    *,
    file_path: Path,
    markers: tuple[str, str],
    cs: CommentSyntax,
) -> list[Block]:
    """Return all top-level codegen blocks found in *content*.

    Depth tracking handles accidentally nested markers in a script body, but
    the expander's recursive expansion is the authoritative nesting mechanism.
    """
    start_marker, end_marker = markers
    lines = content.splitlines(keepends=True)
    blocks: list[Block] = []
    depth = 0
    start_idx = -1

    for i, line in enumerate(lines):
        has_start = start_marker in line
        has_end = end_marker in line

        if has_start and depth == 0:
            start_idx = i
            depth = 1
        elif has_start:
            depth += 1

        if has_end and depth > 0:
            depth -= 1
            if depth == 0:
                end_idx = i
                raw = "".join(lines[start_idx : end_idx + 1])
                indent = _detect_indent(lines[start_idx])

                if cs.is_block:
                    inner = _extract_inner_block_style(lines, start_idx, end_idx)
                else:
                    inner = _extract_inner_line_style(lines, start_idx, end_idx, cs)

                inner = textwrap.dedent(inner)
                shebang, pragma, body = parse_block_header(inner)
                blocks.append(
                    Block(
                        file_path=file_path,
                        start_line=start_idx + 1,
                        end_line=end_idx + 1,
                        indent=indent,
                        comment_syntax=cs,
                        raw_block_text=raw,
                        inner_text=inner,
                        shebang=shebang,
                        pragma=pragma,
                        body=body,
                    )
                )

    return blocks


def parse_file_pragma(content: str, cs: CommentSyntax, markers: tuple[str, str]) -> dict[str, str]:
    """Find the file-level `codegen:` pragma in the first comment block at the top."""
    start_marker = markers[0]
    lines = content.splitlines(keepends=True)

    if cs.is_block:
        # Find the first block-style comment, abort if it's a codegen block
        in_comment = False
        buf: list[str] = []
        for line in lines:
            stripped = line.strip()
            if not in_comment:
                if stripped == "":
                    continue
                if stripped.startswith(cs.open):
                    if start_marker in stripped:
                        break
                    in_comment = True
                    buf.append(stripped[len(cs.open) :])
                    if cs.close and cs.close in stripped:
                        break  # single-line block comment
                    continue
                else:
                    break  # hit non-comment code
            else:
                if cs.close and cs.close in stripped:
                    buf.append(stripped[: stripped.index(cs.close)])
                    break
                buf.append(line)
        return _pragma_from_text(" ".join(buf))

    else:
        # line-style: scan consecutive comment lines at top
        for line in lines:
            stripped = line.strip()
            if stripped == "":
                continue
            if stripped.startswith(cs.open):
                if start_marker in stripped:
                    break
                p = _pragma_from_line(stripped, cs.open)
                if p:
                    return p
            else:
                break
        return {}
