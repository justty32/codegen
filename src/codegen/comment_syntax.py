from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


@dataclass(frozen=True)
class CommentSyntax:
    open: str
    close: str | None = None

    @property
    def is_block(self) -> bool:
        return self.close is not None

    @classmethod
    def from_string(cls, spec: str) -> "CommentSyntax":
        spec = spec.strip()
        if " " in spec:
            o, c = spec.split(None, 1)
            return cls(o.strip(), c.strip())
        return cls(spec)


_BLOCK_C = CommentSyntax("/*", "*/")
_BLOCK_HTML = CommentSyntax("<!--", "-->")
_LINE_HASH = CommentSyntax("#")
_LINE_DASH = CommentSyntax("--")
_LINE_SLASH = CommentSyntax("//")


_DEFAULT_TABLE: dict[str, CommentSyntax] = {
    ".c": _BLOCK_C,
    ".cpp": _BLOCK_C,
    ".cc": _BLOCK_C,
    ".cxx": _BLOCK_C,
    ".h": _BLOCK_C,
    ".hpp": _BLOCK_C,
    ".rs": _BLOCK_C,
    ".go": _BLOCK_C,
    ".js": _BLOCK_C,
    ".mjs": _BLOCK_C,
    ".ts": _BLOCK_C,
    ".tsx": _BLOCK_C,
    ".jsx": _BLOCK_C,
    ".java": _BLOCK_C,
    ".cs": _BLOCK_C,
    ".swift": _BLOCK_C,
    ".kt": _BLOCK_C,
    ".py": _LINE_HASH,
    ".sh": _LINE_HASH,
    ".bash": _LINE_HASH,
    ".rb": _LINE_HASH,
    ".pl": _LINE_HASH,
    ".yaml": _LINE_HASH,
    ".yml": _LINE_HASH,
    ".toml": _LINE_HASH,
    ".makefile": _LINE_HASH,
    ".html": _BLOCK_HTML,
    ".xml": _BLOCK_HTML,
    ".svg": _BLOCK_HTML,
    ".md": _BLOCK_HTML,
    ".lua": _LINE_DASH,
    ".sql": _LINE_DASH,
    ".hs": _LINE_DASH,
}


def lookup(path: Path, overrides: Mapping[str, str] | None = None) -> CommentSyntax | None:
    ext = path.suffix.lower()
    if overrides and ext in overrides:
        return CommentSyntax.from_string(overrides[ext])
    return _DEFAULT_TABLE.get(ext)


def default_extensions() -> tuple[str, ...]:
    return tuple(sorted(_DEFAULT_TABLE.keys()))


_INTERPRETER_PREFIX: dict[str, str] = {
    "python": "#",
    "python3": "#",
    "python2": "#",
    "sh": "#",
    "bash": "#",
    "zsh": "#",
    "ruby": "#",
    "perl": "#",
    "node": "//",
    "deno": "//",
}


def pragma_prefix_for_shebang(shebang: str | None) -> str | None:
    if shebang is None:
        return "#"
    line = shebang.lstrip()
    if line.startswith("#!"):
        line = line[2:].strip()
    if not line:
        return None
    parts = line.split()
    cmd = parts[0]
    if cmd.endswith("env") and len(parts) >= 2:
        cmd = parts[1]
    name = cmd.rsplit("/", 1)[-1]
    return _INTERPRETER_PREFIX.get(name)
