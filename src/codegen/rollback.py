from __future__ import annotations

import shutil
import sys
from pathlib import Path
from typing import Sequence

from codegen.backup import list_timestamps
from codegen.errors import EXIT_BLOCK_FAILURE, EXIT_OK, EXIT_STARTUP


def run(
    paths: Sequence[Path],
    *,
    timestamp: str | None,
    list_only: bool,
    backup_dir: Path,
) -> int:
    if not backup_dir.exists():
        print(f"codegen rollback: backup dir not found: {backup_dir}", file=sys.stderr)
        return EXIT_STARTUP

    targets = [p.resolve() for p in paths] if paths else _all_backed_up(backup_dir)

    if list_only:
        for t in targets:
            tss = list_timestamps(t, backup_dir)
            if tss:
                print(f"{t}:")
                for ts in tss:
                    print(f"  {ts}")
        return EXIT_OK

    had_failure = False
    for t in targets:
        tss = list_timestamps(t, backup_dir)
        if not tss:
            print(f"codegen rollback: no backup for {t}", file=sys.stderr)
            had_failure = True
            continue

        ts = timestamp or tss[-1]
        if ts not in tss:
            print(f"codegen rollback: timestamp {ts!r} not found for {t}", file=sys.stderr)
            had_failure = True
            continue

        t_abs = t.resolve()
        backup_dir_abs = backup_dir.resolve()
        root = backup_dir_abs.parent

        try:
            rel = t_abs.relative_to(root)
        except ValueError:
            rel = Path(t_abs.name)

        src = backup_dir_abs / rel.parent / ts / t_abs.name
        if not src.exists():
            print(f"codegen rollback: backup file missing: {src}", file=sys.stderr)
            had_failure = True
            continue

        try:
            shutil.copy2(src, t)
        except OSError as e:
            print(f"codegen rollback: failed to restore {t}: {e}", file=sys.stderr)
            had_failure = True

    return EXIT_BLOCK_FAILURE if had_failure else EXIT_OK


def _all_backed_up(backup_dir: Path) -> list[Path]:
    files: list[Path] = []
    if not backup_dir.exists():
        return files
    for child in backup_dir.rglob("*"):
        if child.is_file():
            # Structure: backup_dir / rel_parent / timestamp / filename
            # Original path: backup_dir.parent / rel_parent / filename
            try:
                rel = child.relative_to(backup_dir)
                parts = rel.parts
                if len(parts) >= 3:
                    orig = backup_dir.parent / Path(*parts[:-2]) / parts[-1]
                    if orig not in files:
                        files.append(orig)
            except ValueError:
                pass
    return files
