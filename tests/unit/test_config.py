import os
import textwrap
from pathlib import Path

import pytest

from codegen.config import (
    Config,
    ConfigError,
    from_env,
    load_toml,
    merge,
    merge_pragma,
    resolve_initial,
)


def test_defaults():
    cfg = Config()
    assert cfg.markers == ("CODEGEN_START", "CODEGEN_END")
    assert cfg.max_passes == 1
    assert cfg.auto_indent is True
    assert cfg.on_error == "continue"
    assert cfg.backup is True


def test_merge_basic():
    cfg = Config()
    cfg2 = merge(cfg, max_passes=5)
    assert cfg2.max_passes == 5
    assert cfg.max_passes == 1  # original unchanged (frozen)


def test_merge_markers_string():
    cfg = merge(Config(), markers="<<<,>>>")
    assert cfg.markers == ("<<<", ">>>")


def test_merge_markers_list():
    cfg = merge(Config(), markers=["<<<", ">>>"])
    assert cfg.markers == ("<<<", ">>>")


def test_merge_bool_string():
    cfg = merge(Config(), auto_indent="false")
    assert cfg.auto_indent is False


def test_merge_unknown_key():
    with pytest.raises(ConfigError, match="unknown setting"):
        merge(Config(), nonexistent_key="x")


def test_merge_bad_on_error():
    with pytest.raises(ConfigError, match="on_error"):
        merge(Config(), on_error="explode")


def test_merge_pragma_rejects_scan_fields():
    with pytest.raises(ConfigError, match="scan"):
        merge_pragma(Config(), {"extensions": ".c,.h"}, source="test")


def test_merge_pragma_applies():
    cfg = merge_pragma(Config(), {"max_passes": "3", "on_error": "abort_all"}, source="block")
    assert cfg.max_passes == 3
    assert cfg.on_error == "abort_all"


def test_from_env_basic():
    fake_env = {"CODEGEN_MAX_PASSES": "7", "CODEGEN_AUTO_INDENT": "false"}
    overrides = from_env(fake_env)
    assert overrides["max_passes"] == "7"
    assert overrides["auto_indent"] == "false"


def test_from_env_ignores_runtime_vars():
    fake_env = {
        "CODEGEN_GLOBAL": "/tmp/g.json",
        "CODEGEN_FILE": "/tmp/f.json",
        "CODEGEN_MAX_PASSES": "2",
    }
    overrides = from_env(fake_env)
    assert "max_passes" in overrides
    assert "global" not in overrides
    assert "file" not in overrides


def test_load_toml(tmp_path):
    (tmp_path / "codegen.toml").write_text(
        textwrap.dedent(
            """
            max_passes = 3
            on_error = "abort_all"
            [env]
            MY_KEY = "hello"
            [comment_syntax]
            ".pyx" = "#"
            """
        )
    )
    data = load_toml(tmp_path / "codegen.toml")
    assert data["max_passes"] == 3
    assert data["on_error"] == "abort_all"
    assert data["extra_env"] == {"MY_KEY": "hello"}
    assert data["comment_syntax_overrides"] == {".pyx": "#"}


def test_load_toml_unknown_key(tmp_path):
    (tmp_path / "codegen.toml").write_text("foobar = 1\n")
    with pytest.raises(ConfigError, match="unknown"):
        load_toml(tmp_path / "codegen.toml")


def test_config_invalid_markers():
    with pytest.raises(ConfigError):
        Config(markers=("ONLY_ONE",))  # type: ignore[arg-type]


def test_config_invalid_max_passes():
    with pytest.raises(ConfigError):
        Config(max_passes=0)


def test_resolve_initial_uses_env(tmp_path):
    cfg = resolve_initial(
        {},
        start=tmp_path,
        env={"CODEGEN_MAX_PASSES": "9"},
    )
    assert cfg.max_passes == 9


def test_resolve_initial_cli_overrides_toml(tmp_path):
    (tmp_path / "codegen.toml").write_text("max_passes = 5\n")
    cfg = resolve_initial(
        {"max_passes": 10},
        start=tmp_path,
    )
    assert cfg.max_passes == 10


def test_resolve_initial_toml_overrides_env(tmp_path):
    (tmp_path / "codegen.toml").write_text("max_passes = 5\n")
    cfg = resolve_initial(
        {},
        start=tmp_path,
        env={"CODEGEN_MAX_PASSES": "3"},
    )
    assert cfg.max_passes == 5
