from __future__ import annotations

import argparse
import signal
import sys
from pathlib import Path
from typing import Sequence

from codegen.backup import make_run_id
from codegen.config import Config, ConfigError, resolve_initial
from codegen.errors import EXIT_SIGINT, EXIT_STARTUP
from codegen.progress import RunState, print_plan, report_interrupt, reset
from codegen.scanner import collect_files


# ---------- SIGINT handling ----------

_cancel = False
_run_state: RunState | None = None


def _sigint_handler(sig, frame):
    global _cancel
    if _cancel:
        raise KeyboardInterrupt
    _cancel = True
    if _run_state:
        report_interrupt(_run_state, kind="sigint")
    sys.exit(EXIT_SIGINT)


# ---------- CLI parsers ----------

def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="codegen",
        description="Cross-language in-source code generation tool.",
    )
    sub = parser.add_subparsers(dest="command")

    # run (default)
    run_p = sub.add_parser("run", help="Run codegen blocks (default command)")
    _add_run_args(run_p)

    # rollback
    rb_p = sub.add_parser("rollback", help="Roll back files to a previous backup")
    rb_p.add_argument("paths", nargs="*", type=Path)
    rb_p.add_argument("--timestamp", "-t", help="Backup timestamp to restore")
    rb_p.add_argument("--list", "-l", action="store_true", dest="list_only", help="List available timestamps")
    rb_p.add_argument("--backup-dir", type=Path, dest="backup_dir")

    return parser


def _add_run_args(p: argparse.ArgumentParser) -> None:
    p.add_argument("paths", nargs="*", type=Path, help="Files or directories to process")
    p.add_argument("--config", type=Path, dest="config_path", help="Explicit codegen.toml path")
    p.add_argument("--markers", help="Override markers (start,end)")
    p.add_argument("--keep-as-comment", action="store_true", dest="keep_as_comment")
    p.add_argument("--auto-indent", action="store_true", default=None, dest="auto_indent")
    p.add_argument("--no-auto-indent", action="store_false", dest="auto_indent")
    p.add_argument("--backup-dir", type=Path, dest="backup_dir")
    p.add_argument("--no-backup", action="store_true", dest="no_backup")
    p.add_argument("--all", action="store_true", dest="scan_all")
    p.add_argument("--ext", dest="extensions", help="Override extensions (comma-separated)")
    p.add_argument("--include", action="append", dest="include", default=[])
    p.add_argument("--exclude", action="append", dest="exclude", default=[])
    p.add_argument("--max-passes", type=int, dest="max_passes")
    p.add_argument("--max-total-time", type=float, dest="max_total_time")
    p.add_argument("--max-pass-time", type=float, dest="max_pass_time")
    p.add_argument("--on-error", choices=["continue", "abort_file", "abort_all"], dest="on_error")
    p.add_argument("--strict", action="store_const", const="abort_all", dest="on_error")
    p.add_argument("--cwd", type=Path, dest="cwd")
    p.add_argument("--env", action="append", dest="env_pairs", default=[], metavar="KEY=VAL")
    p.add_argument("--dry-run", action="store_true", dest="dry_run")


def _cli_overrides(args: argparse.Namespace) -> dict:
    overrides: dict = {}
    if args.markers:
        overrides["markers"] = args.markers
    if getattr(args, "keep_as_comment", False):
        overrides["keep_as_comment"] = True
    if getattr(args, "auto_indent", None) is not None:
        overrides["auto_indent"] = args.auto_indent
    if getattr(args, "backup_dir", None):
        overrides["backup_dir"] = args.backup_dir
    if getattr(args, "no_backup", False):
        overrides["backup"] = False
    if getattr(args, "scan_all", False):
        overrides["scan_all"] = True
    if getattr(args, "extensions", None):
        overrides["extensions"] = args.extensions
    if getattr(args, "include", []):
        overrides["include"] = args.include
    if getattr(args, "exclude", []):
        overrides["exclude"] = args.exclude
    if getattr(args, "max_passes", None) is not None:
        overrides["max_passes"] = args.max_passes
    if getattr(args, "max_total_time", None) is not None:
        overrides["max_total_time"] = args.max_total_time
    if getattr(args, "max_pass_time", None) is not None:
        overrides["max_pass_time"] = args.max_pass_time
    if getattr(args, "on_error", None):
        overrides["on_error"] = args.on_error
    if getattr(args, "cwd", None):
        overrides["cwd"] = args.cwd
    if getattr(args, "env_pairs", []):
        env_dict = {}
        for pair in args.env_pairs:
            if "=" not in pair:
                raise ConfigError(f"--env: expected KEY=VAL, got {pair!r}")
            k, v = pair.split("=", 1)
            env_dict[k] = v
        overrides["extra_env"] = env_dict
    return overrides


# ---------- entry point ----------

def main(argv: Sequence[str] | None = None) -> int:
    signal.signal(signal.SIGINT, _sigint_handler)

    parser = _build_parser()

    # If no subcommand given, treat the whole invocation as `run`
    raw = list(argv) if argv is not None else sys.argv[1:]
    if not raw or raw[0] not in ("run", "rollback", "-h", "--help"):
        raw = ["run"] + raw

    try:
        args = parser.parse_args(raw)
    except SystemExit as e:
        return int(e.code) if e.code is not None else 0

    if args.command == "rollback":
        return _run_rollback(args)

    return _run_run(args)


def _run_run(args: argparse.Namespace) -> int:
    global _run_state
    from codegen.errors import EXIT_ABORT_ALL
    from codegen.pipeline import run_all

    paths: list[Path] = args.paths or [Path(".")]
    config_path: Path | None = getattr(args, "config_path", None)

    try:
        overrides = _cli_overrides(args)
        cfg = resolve_initial(
            overrides,
            start=paths[0],
            config_path=config_path,
        )
    except ConfigError as e:
        print(f"codegen: 設定錯誤 — {e}", file=sys.stderr)
        return EXIT_STARTUP

    try:
        files = collect_files(paths, cfg)
    except ConfigError as e:
        print(f"codegen: {e}", file=sys.stderr)
        return EXIT_STARTUP

    if not files:
        return 0

    print_plan(paths)

    state = reset(paths)
    _run_state = state

    run_id = make_run_id()
    dry_run = getattr(args, "dry_run", False)

    code = run_all(files, cfg, run_id=run_id, dry_run=dry_run, state=state)

    if code == EXIT_ABORT_ALL:
        report_interrupt(state, kind="abort_all")

    return code


def _run_rollback(args: argparse.Namespace) -> int:
    from codegen.rollback import run as rollback_run

    backup_dir = getattr(args, "backup_dir", None) or Path(".codegen-backup")
    return rollback_run(
        paths=args.paths or [],
        timestamp=args.timestamp,
        list_only=args.list_only,
        backup_dir=backup_dir,
    )
