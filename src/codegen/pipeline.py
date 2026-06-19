from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from codegen.backup import snapshot_file
from codegen.comment_syntax import lookup
from codegen.config import Config, merge_pragma
from codegen.env import RunContext
from codegen.errors import AbortAll, BlockFailure, ConfigError, EXIT_BLOCK_FAILURE, EXIT_OK
from codegen.expander import process_content
from codegen.parser import parse_file_pragma
from codegen.progress import RunState
from codegen.scope import ScopeStore


def process_file(
    path: Path,
    base_cfg: Config,
    scope: ScopeStore,
    ctx: RunContext,
    *,
    backup_dir: Path | None,
    run_id: str,
    dry_run: bool = False,
    state: RunState | None = None,
) -> tuple[str, bool]:
    """Process a single file in-memory and (optionally) write back to disk.

    Returns (final_content, had_failure).
    Raises AbortAll when on_error=abort_all is triggered.
    """
    cs = lookup(path, overrides=dict(base_cfg.comment_syntax_overrides))
    if cs is None:
        return "", False  # unknown extension: skip

    content = path.read_text(encoding="utf-8")

    # Resolve file-level config
    file_pragma = parse_file_pragma(content, cs, base_cfg.markers)
    if file_pragma:
        file_cfg = merge_pragma(base_cfg, file_pragma, source=str(path))
    else:
        file_cfg = base_cfg

    # Write CODEGEN_ORIGIN_FILE snapshot
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".txt", delete=False, encoding="utf-8"
    ) as f:
        f.write(content)
        origin_file_path = Path(f.name)

    # Placeholder for origin_block (set per-block inside expander)
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".txt", delete=False, encoding="utf-8"
    ) as f:
        f.write("")
        origin_block_path = Path(f.name)

    # When running blocks as another user, the read-only origin snapshots they
    # consume (CODEGEN_ORIGIN_FILE/BLOCK) must be readable by that user.
    if file_cfg.run_as_user is not None:
        for p in (origin_file_path, origin_block_path):
            try:
                p.chmod(0o644)
            except OSError:
                pass

    run_ctx = RunContext(
        invoke_cwd=ctx.invoke_cwd,
        targets=ctx.targets,
        file_path=path,
        origin_file_path=origin_file_path,
        origin_block_path=origin_block_path,
    )

    scope.open_file()
    had_failure = False
    result = content

    try:
        # Backup before any mutation
        if backup_dir is not None and file_cfg.backup:
            snapshot_file(path, backup_dir, run_id)

        result, had_failure = process_content(content, file_cfg, scope, run_ctx)

        if dry_run:
            print(result, end="")
        else:
            path.write_text(result, encoding="utf-8")

    except AbortAll:
        raise
    except BlockFailure:
        # abort_file path: process_content already emitted the diagnostic.
        had_failure = True
    finally:
        scope.close_file()
        try:
            origin_file_path.unlink()
            origin_block_path.unlink()
        except OSError:
            pass

    return result, had_failure


def run_all(
    targets: list[Path],
    cfg: Config,
    *,
    run_id: str,
    dry_run: bool = False,
    state: RunState | None = None,
) -> int:
    """Process all files.  Returns exit code."""
    from codegen.errors import EXIT_ABORT_ALL

    scope = ScopeStore.create(world_accessible=cfg.run_as_user is not None)
    backup_dir = (cfg.backup_dir if cfg.backup else None)
    had_any_failure = False
    invoke_cwd = Path.cwd()

    try:
        for target_idx, path in enumerate(targets, 1):
            if state:
                state.target_idx = target_idx
                state.current_file = path
                state.block_ordinal = None

            try:
                _result, failed = process_file(
                    path,
                    cfg,
                    scope,
                    RunContext(
                        invoke_cwd=invoke_cwd,
                        targets=targets,
                        file_path=path,
                        origin_file_path=path,   # placeholder; overridden inside
                        origin_block_path=path,
                    ),
                    backup_dir=backup_dir,
                    run_id=run_id,
                    dry_run=dry_run,
                    state=state,
                )
                if failed:
                    had_any_failure = True
            except AbortAll as exc:
                if state:
                    state.last_failure_reason = exc.failure.reason
                return EXIT_ABORT_ALL
            except BlockFailure:
                had_any_failure = True
                # abort_file: continue to next file

    finally:
        scope.cleanup()

    return EXIT_BLOCK_FAILURE if had_any_failure else EXIT_OK
