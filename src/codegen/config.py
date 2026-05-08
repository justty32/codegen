from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, field, fields, replace
from pathlib import Path
from typing import Any, Mapping

from codegen.errors import ConfigError


VALID_ON_ERROR = ("continue", "abort_file", "abort_all")

# Fields that may NOT appear in file pragma or per-block pragma (§9.1).
SCAN_ONLY_FIELDS = frozenset(
    {"extensions", "include", "exclude", "scan_all", "comment_syntax_overrides"}
)
# Fields that may NOT appear in any pragma (file or per-block).
PRAGMA_FORBIDDEN_FIELDS = SCAN_ONLY_FIELDS | frozenset({"extra_env"})


@dataclass(frozen=True)
class Config:

    extensions: tuple[str, ...] = ()
    include: tuple[str, ...] = ()
    exclude: tuple[str, ...] = ()
    scan_all: bool = False
    comment_syntax_overrides: Mapping[str, str] = field(default_factory=dict)

    markers: tuple[str, str] = ("CODEGEN_START", "CODEGEN_END")

    max_passes: int = 1
    max_total_time: float = 5.0
    max_pass_time: float = 5.0

    keep_as_comment: bool = False
    auto_indent: bool = True

    backup: bool = True
    backup_dir: Path = Path(".codegen-backup")

    on_error: str = "continue"

    cwd: Path | None = None
    extra_env: Mapping[str, str] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if len(self.markers) != 2 or not all(self.markers):
            raise ConfigError(f"markers must be a 2-tuple of non-empty strings, got {self.markers!r}")
        if self.on_error not in VALID_ON_ERROR:
            raise ConfigError(
                f"on_error must be one of {VALID_ON_ERROR}, got {self.on_error!r}"
            )
        if self.max_passes < 1:
            raise ConfigError(f"max_passes must be >= 1, got {self.max_passes}")
        if self.max_total_time <= 0 or self.max_pass_time <= 0:
            raise ConfigError("max_total_time and max_pass_time must be > 0")


FIELD_TYPES: dict[str, type] = {f.name: f.type for f in fields(Config)}
FIELD_NAMES: frozenset[str] = frozenset(FIELD_TYPES.keys())


_BOOL_TRUE = {"true", "1", "yes", "on"}
_BOOL_FALSE = {"false", "0", "no", "off"}


def _coerce_bool(name: str, value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        v = value.strip().lower()
        if v in _BOOL_TRUE:
            return True
        if v in _BOOL_FALSE:
            return False
    raise ConfigError(f"{name}: cannot interpret {value!r} as bool")


def _coerce_markers(name: str, value: Any) -> tuple[str, str]:
    if isinstance(value, str):
        parts = value.split(",")
    elif isinstance(value, (list, tuple)):
        parts = list(value)
    else:
        raise ConfigError(f"{name}: must be a list or comma-separated string")
    if len(parts) != 2:
        raise ConfigError(f"{name}: must have exactly 2 items, got {value!r}")
    return (str(parts[0]).strip(), str(parts[1]).strip())


def _coerce_str_tuple(name: str, value: Any) -> tuple[str, ...]:
    if isinstance(value, str):
        return tuple(p.strip() for p in value.split(",") if p.strip())
    if isinstance(value, (list, tuple)):
        return tuple(str(v) for v in value)
    raise ConfigError(f"{name}: must be a list or comma-separated string")


def _coerce_int(name: str, value: Any) -> int:
    try:
        return int(value)
    except (TypeError, ValueError) as e:
        raise ConfigError(f"{name}: cannot interpret {value!r} as int") from e


def _coerce_float(name: str, value: Any) -> float:
    try:
        return float(value)
    except (TypeError, ValueError) as e:
        raise ConfigError(f"{name}: cannot interpret {value!r} as float") from e


def _coerce_path(name: str, value: Any) -> Path:
    if isinstance(value, Path):
        return value
    if isinstance(value, str):
        return Path(value)
    raise ConfigError(f"{name}: cannot interpret {value!r} as path")


def _coerce_str_mapping(name: str, value: Any) -> Mapping[str, str]:
    if isinstance(value, Mapping):
        return {str(k): str(v) for k, v in value.items()}
    raise ConfigError(f"{name}: must be a table/mapping")


def _coerce_field(name: str, value: Any) -> Any:
    if name == "markers":
        return _coerce_markers(name, value)
    if name in ("extensions", "include", "exclude"):
        return _coerce_str_tuple(name, value)
    if name == "scan_all":
        return _coerce_bool(name, value)
    if name == "comment_syntax_overrides":
        return _coerce_str_mapping(name, value)
    if name == "max_passes":
        return _coerce_int(name, value)
    if name in ("max_total_time", "max_pass_time"):
        return _coerce_float(name, value)
    if name in ("keep_as_comment", "auto_indent", "backup"):
        return _coerce_bool(name, value)
    if name == "backup_dir":
        return _coerce_path(name, value)
    if name == "on_error":
        v = str(value)
        if v not in VALID_ON_ERROR:
            raise ConfigError(f"on_error: must be one of {VALID_ON_ERROR}, got {v!r}")
        return v
    if name == "cwd":
        return _coerce_path(name, value)
    if name == "extra_env":
        return _coerce_str_mapping(name, value)
    raise ConfigError(f"unknown setting: {name}")


def _normalize_overrides(items: Mapping[str, Any]) -> dict[str, Any]:
    """Coerce raw mapping (e.g. parsed TOML / pragma) into Config-typed values."""
    out: dict[str, Any] = {}
    for k, v in items.items():
        if k not in FIELD_NAMES:
            raise ConfigError(f"unknown setting: {k}")
        out[k] = _coerce_field(k, v)
    return out


def merge(base: Config, **overrides: Any) -> Config:
    coerced = _normalize_overrides(overrides)
    return replace(base, **coerced)


def merge_pragma(base: Config, pragma: Mapping[str, str], *, source: str) -> Config:
    """Merge a parsed pragma mapping. Rejects scan-only fields (§9.1)."""
    forbidden = set(pragma.keys()) & PRAGMA_FORBIDDEN_FIELDS
    if forbidden:
        raise ConfigError(
            f"{source}: cannot set scan/env settings via pragma: {sorted(forbidden)}"
        )
    return merge(base, **dict(pragma))


# ---------- Sources ----------


def from_env(env: Mapping[str, str] | None = None) -> dict[str, Any]:
    """Read CODEGEN_* environment variables and turn them into config overrides.

    Convention: CODEGEN_<UPPER_FIELD>=value.  e.g. CODEGEN_MAX_PASSES=3.
    Unknown CODEGEN_* env vars are silently ignored (we don't want to claim
    every CODEGEN_* prefix; some are reserved for runtime, e.g. CODEGEN_GLOBAL).
    """
    env = env if env is not None else os.environ
    reserved = {
        "CODEGEN_GLOBAL",
        "CODEGEN_FILE",
        "CODEGEN_BLOCK",
        "CODEGEN_ORIGIN_FILE",
        "CODEGEN_ORIGIN_BLOCK",
        "CODEGEN_INVOKE_CWD",
        "CODEGEN_TARGETS",
        "CODEGEN_FILE_PATH",
    }
    out: dict[str, Any] = {}
    for key, value in env.items():
        if not key.startswith("CODEGEN_") or key in reserved:
            continue
        field_name = key[len("CODEGEN_"):].lower()
        if field_name in FIELD_NAMES:
            out[field_name] = value
    return out


def load_toml(path: Path) -> dict[str, Any]:
    """Load codegen.toml.  [env] and [comment_syntax] are mapped to extra_env / comment_syntax_overrides."""
    try:
        with path.open("rb") as f:
            data = tomllib.load(f)
    except OSError as e:
        raise ConfigError(f"cannot read {path}: {e}") from e
    except tomllib.TOMLDecodeError as e:
        raise ConfigError(f"invalid TOML in {path}: {e}") from e

    env = data.pop("env", None)
    cs = data.pop("comment_syntax", None)

    out: dict[str, Any] = {}
    for k, v in data.items():
        if k not in FIELD_NAMES:
            raise ConfigError(f"{path}: unknown setting {k!r}")
        out[k] = v
    if env is not None:
        out["extra_env"] = env
    if cs is not None:
        out["comment_syntax_overrides"] = cs
    return out


def find_folder_toml(start: Path, *, explicit: Path | None = None) -> Path | None:
    """Walk up from `start` looking for codegen.toml. Stop at filesystem root or .git."""
    if explicit is not None:
        if not explicit.exists():
            raise ConfigError(f"--config path not found: {explicit}")
        return explicit
    cur = start.resolve()
    if cur.is_file():
        cur = cur.parent
    while True:
        candidate = cur / "codegen.toml"
        if candidate.is_file():
            return candidate
        if (cur / ".git").exists():
            return None
        if cur.parent == cur:
            return None
        cur = cur.parent


def find_project_root(start: Path) -> Path:
    """Find the project root by looking for codegen.toml or .git. Fallback to cwd."""
    cur = start.resolve()
    if cur.is_file():
        cur = cur.parent
    while True:
        if (cur / "codegen.toml").exists() or (cur / ".git").exists():
            return cur
        if cur.parent == cur:
            break
        cur = cur.parent
    return Path.cwd()


def resolve_initial(
    cli_overrides: Mapping[str, Any],
    *,
    start: Path,
    env: Mapping[str, str] | None = None,
    config_path: Path | None = None,
) -> Config:
    """Build the initial Config from defaults + env + folder toml + CLI."""
    cfg = Config()

    env_over = from_env(env)
    if env_over:
        cfg = merge(cfg, **env_over)

    toml_path = find_folder_toml(start, explicit=config_path)
    if toml_path is not None:
        toml_over = load_toml(toml_path)
        if toml_over:
            cfg = merge(cfg, **toml_over)

    root = find_project_root(start)

    if cli_overrides:
        cfg = merge(cfg, **dict(cli_overrides))

    # Ensure backup_dir is absolute for robust relative_to calculations
    if not cfg.backup_dir.is_absolute():
        cfg = replace(cfg, backup_dir=(root / cfg.backup_dir).resolve())

    return cfg
