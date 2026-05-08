from __future__ import annotations

import shutil
from datetime import datetime, timezone
from pathlib import Path


def make_run_id() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def snapshot_file(source: Path, backup_dir: Path, run_id: str) -> Path | None:
    """Copy *source* into *backup_dir/<rel-path>/<run_id>/<filename>*.

    Returns the backup path, or None if backup is disabled.
    Uses os.replace for atomic writes where possible.
    """
    source = source.resolve()
    backup_dir = backup_dir.resolve()
    root = backup_dir.parent

    try:
        rel = source.relative_to(root)
    except ValueError:
        # Outside root? Keep as-is at backup root
        rel = Path(source.name)

    dest_dir = backup_dir / rel.parent / run_id
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / source.name

    shutil.copy2(source, dest)
    return dest


def list_timestamps(source: Path, backup_dir: Path) -> list[str]:
    """Return sorted list of available backup timestamps for *source*."""
    source = source.resolve()
    backup_dir = backup_dir.resolve()
    root = backup_dir.parent

    try:
        rel = source.relative_to(root)
    except ValueError:
        rel = Path(source.name)

    ts_dir = backup_dir / rel.parent
    if not ts_dir.exists():
        return []
    timestamps = [
        d.name
        for d in sorted(ts_dir.iterdir())
        if d.is_dir() and (d / source.name).exists()
    ]
    return timestamps
