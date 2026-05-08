from pathlib import Path

import pytest

from codegen.config import Config
from codegen.scanner import collect_files


def _write(path: Path, text: str = "") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def test_file_target_passes_through(tmp_path):
    f = _write(tmp_path / "foo.xyz")
    result = collect_files([f], Config())
    assert f.resolve() in result


def test_collects_by_extension(tmp_path):
    _write(tmp_path / "a.c")
    _write(tmp_path / "b.py")
    _write(tmp_path / "c.txt")
    cfg = Config()
    result = collect_files([tmp_path], cfg)
    names = {p.name for p in result}
    assert "a.c" in names
    assert "b.py" in names
    assert "c.txt" not in names


def test_exclude_glob(tmp_path):
    _write(tmp_path / "build" / "out.c")
    _write(tmp_path / "src" / "main.c")
    cfg = Config(exclude=("build/**",))
    result = collect_files([tmp_path], cfg)
    names = {p.name for p in result}
    assert "main.c" in names
    assert "out.c" not in names


def test_scan_all_includes_unknown_extensions(tmp_path):
    _write(tmp_path / "file.xyz")
    cfg = Config(scan_all=True)
    result = collect_files([tmp_path], cfg)
    assert any(p.name == "file.xyz" for p in result)


def test_include_glob_adds_extra_files(tmp_path):
    _write(tmp_path / "custom.xyz")
    _write(tmp_path / "normal.c")
    cfg = Config(include=("*.xyz",))
    result = collect_files([tmp_path], cfg)
    names = {p.name for p in result}
    assert "custom.xyz" in names
    assert "normal.c" in names


def test_missing_target_raises(tmp_path):
    from codegen.errors import ConfigError
    with pytest.raises(ConfigError, match="not found"):
        collect_files([tmp_path / "nonexistent"], Config())
