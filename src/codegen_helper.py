"""codegen_helper — convenience API for block scripts.

Block scripts can do:
    import codegen_helper as cg
    cg.file_set("key", value)
    result = cg.file_get("key")

This module is installed as a top-level package so it is importable from
any shebang script without needing to know the codegen package internals.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any


def _json_path(env_var: str) -> Path:
    val = os.environ.get(env_var)
    if not val:
        raise RuntimeError(f"${env_var} not set — are you running inside codegen?")
    return Path(val)


def _read(env_var: str) -> dict[str, Any]:
    p = _json_path(env_var)
    text = p.read_text(encoding="utf-8")
    return json.loads(text) if text.strip() else {}


def _write(env_var: str, data: dict[str, Any]) -> None:
    p = _json_path(env_var)
    p.write_text(json.dumps(data), encoding="utf-8")


# ---------- global scope ----------

def global_get(key: str, default: Any = None) -> Any:
    """Read a value from the folder-global scope dict."""
    return _read("CODEGEN_GLOBAL").get(key, default)


def global_set(key: str, value: Any) -> None:
    """Write a value to the folder-global scope dict."""
    d = _read("CODEGEN_GLOBAL")
    d[key] = value
    _write("CODEGEN_GLOBAL", d)


def global_del(key: str) -> None:
    d = _read("CODEGEN_GLOBAL")
    d.pop(key, None)
    _write("CODEGEN_GLOBAL", d)


# ---------- file scope ----------

def file_get(key: str, default: Any = None) -> Any:
    """Read a value from the file-level scope dict."""
    return _read("CODEGEN_FILE").get(key, default)


def file_set(key: str, value: Any) -> None:
    """Write a value to the file-level scope dict."""
    d = _read("CODEGEN_FILE")
    d[key] = value
    _write("CODEGEN_FILE", d)


def file_del(key: str) -> None:
    d = _read("CODEGEN_FILE")
    d.pop(key, None)
    _write("CODEGEN_FILE", d)


# ---------- block scope ----------

def block_get(key: str, default: Any = None) -> Any:
    """Read a value from the block-local scope dict."""
    return _read("CODEGEN_BLOCK").get(key, default)


def block_set(key: str, value: Any) -> None:
    """Write a value to the block-local scope dict."""
    d = _read("CODEGEN_BLOCK")
    d[key] = value
    _write("CODEGEN_BLOCK", d)


def block_del(key: str) -> None:
    d = _read("CODEGEN_BLOCK")
    d.pop(key, None)
    _write("CODEGEN_BLOCK", d)


# ---------- read-only context ----------

def origin_file() -> str:
    """Return the original file content (before codegen started processing it)."""
    return _json_path("CODEGEN_ORIGIN_FILE").read_text(encoding="utf-8")


def origin_block() -> str:
    """Return the original block content (shebang + body, no markers)."""
    return _json_path("CODEGEN_ORIGIN_BLOCK").read_text(encoding="utf-8")


def targets() -> list[str]:
    """Return the list of targets passed to codegen on this invocation."""
    val = os.environ.get("CODEGEN_TARGETS", "")
    return [t for t in val.splitlines() if t]


def invoke_cwd() -> str:
    """Return the cwd from which codegen was invoked."""
    return os.environ.get("CODEGEN_INVOKE_CWD", "")


def file_path() -> str:
    """Return the absolute path of the file currently being processed."""
    return os.environ.get("CODEGEN_FILE_PATH", "")
