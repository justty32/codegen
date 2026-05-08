from __future__ import annotations

import fnmatch
from pathlib import Path
from typing import Sequence

from codegen.comment_syntax import default_extensions
from codegen.config import Config


def _matches_any(path: Path, patterns: Sequence[str], root: Path) -> bool:
    rel = path.relative_to(root).as_posix()
    for pat in patterns:
        if fnmatch.fnmatch(rel, pat) or fnmatch.fnmatch(path.name, pat):
            return True
    return False


def _collect_dir(
    directory: Path,
    cfg: Config,
    root: Path,
    effective_extensions: tuple[str, ...],
    backup_dir_resolved: Path | None,
) -> list[Path]:
    results: list[Path] = []
    # Hardcoded ignore list for safety
    ignore_dirs = {".git", "__pycache__", ".pytest_cache", ".codegen-backup"}

    for child in sorted(directory.iterdir()):
        if child.is_dir():
            if child.name in ignore_dirs:
                continue
            if backup_dir_resolved is not None and child.resolve() == backup_dir_resolved:
                continue
            results.extend(_collect_dir(child, cfg, root, effective_extensions, backup_dir_resolved))
        elif child.is_file():
            if cfg.exclude and _matches_any(child, cfg.exclude, root):
                continue
            suffix = child.suffix.lower()
            if cfg.scan_all:
                results.append(child)
            elif cfg.include and _matches_any(child, cfg.include, root):
                results.append(child)
            elif suffix in effective_extensions:
                results.append(child)
    return results


def collect_files(targets: Sequence[Path], cfg: Config) -> list[Path]:
    """Expand *targets* to a list of files to process.

    - If a target is a file: include directly, no filtering.
    - If a target is a directory: recurse with extensions/include/exclude filter.
    """
    effective_extensions = cfg.extensions if cfg.extensions else default_extensions()
    backup_dir_resolved = cfg.backup_dir.resolve() if cfg.backup else None

    result: list[Path] = []
    for target in targets:
        target = target.resolve()
        if target.is_file():
            result.append(target)
        elif target.is_dir():
            result.extend(_collect_dir(
                target, cfg,
                root=target,
                effective_extensions=effective_extensions,
                backup_dir_resolved=backup_dir_resolved,
            ))
        else:
            from codegen.errors import ConfigError
            raise ConfigError(f"target not found: {target}")
    return result
