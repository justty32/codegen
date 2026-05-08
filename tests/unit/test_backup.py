from pathlib import Path

from codegen.backup import list_timestamps, snapshot_file


def test_snapshot_creates_backup(tmp_path):
    src = tmp_path / "src" / "foo.c"
    src.parent.mkdir()
    src.write_text("original")
    backup_dir = tmp_path / ".codegen-backup"

    dest = snapshot_file(src, backup_dir, "20240101T000000Z")
    assert dest is not None
    assert dest.read_text() == "original"


def test_list_timestamps(tmp_path):
    src = tmp_path / "src" / "foo.c"
    src.parent.mkdir()
    src.write_text("v1")
    backup_dir = tmp_path / ".codegen-backup"

    snapshot_file(src, backup_dir, "20240101T000000Z")
    snapshot_file(src, backup_dir, "20240102T000000Z")

    tss = list_timestamps(src, backup_dir)
    assert "20240101T000000Z" in tss
    assert "20240102T000000Z" in tss
    assert len(tss) == 2


def test_list_timestamps_empty(tmp_path):
    src = tmp_path / "no_backup.c"
    src.write_text("")
    backup_dir = tmp_path / ".codegen-backup"
    backup_dir.mkdir()
    assert list_timestamps(src, backup_dir) == []
