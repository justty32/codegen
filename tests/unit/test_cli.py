"""CLI smoke tests (no subprocess execution needed)."""
from pathlib import Path

import pytest

from codegen.cli import _build_parser, _cli_overrides


def test_default_command_is_run():
    parser = _build_parser()
    # No subcommand → needs prepended 'run'; CLI main() handles this
    args = parser.parse_args(["run", "src/"])
    assert args.command == "run"
    assert args.paths == [Path("src/")]


def test_run_flags():
    parser = _build_parser()
    args = parser.parse_args([
        "run",
        "--max-passes", "5",
        "--on-error", "abort_all",
        "--no-backup",
        "--dry-run",
    ])
    overrides = _cli_overrides(args)
    assert overrides["max_passes"] == 5
    assert overrides["on_error"] == "abort_all"
    assert overrides["backup"] is False


def test_strict_sets_on_error():
    parser = _build_parser()
    args = parser.parse_args(["run", "--strict"])
    overrides = _cli_overrides(args)
    assert overrides["on_error"] == "abort_all"


def test_markers_override():
    parser = _build_parser()
    args = parser.parse_args(["run", "--markers", "<<<,>>>"])
    overrides = _cli_overrides(args)
    assert overrides["markers"] == "<<<,>>>"


def test_rollback_subcommand():
    parser = _build_parser()
    args = parser.parse_args(["rollback", "--list", "src/foo.c"])
    assert args.command == "rollback"
    assert args.list_only is True
    assert args.paths == [Path("src/foo.c")]


def test_env_pairs():
    parser = _build_parser()
    args = parser.parse_args(["run", "--env", "FOO=bar", "--env", "BAZ=qux"])
    overrides = _cli_overrides(args)
    assert overrides["extra_env"] == {"FOO": "bar", "BAZ": "qux"}


def test_main_no_files_returns_zero(tmp_path):
    """Empty directory with no matching files → exit 0."""
    from codegen.cli import main
    code = main(["run", str(tmp_path)])
    assert code == 0
