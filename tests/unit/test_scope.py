import json
from pathlib import Path

from codegen.scope import ScopeStore


def test_initial_state(tmp_path):
    scope = ScopeStore(tmp_path)
    data = json.loads(scope.global_path.read_text())
    assert data == {}


def test_open_file_creates_json(tmp_path):
    scope = ScopeStore(tmp_path)
    fp = scope.open_file()
    assert fp.exists()
    assert json.loads(fp.read_text()) == {}


def test_open_block_creates_json(tmp_path):
    scope = ScopeStore(tmp_path)
    scope.open_file()
    bp = scope.open_block()
    assert bp.exists()
    assert json.loads(bp.read_text()) == {}


def test_snapshot_and_restore(tmp_path):
    scope = ScopeStore(tmp_path)
    scope.open_file()
    scope.open_block()

    scope.global_path.write_text(json.dumps({"g": 1}))
    scope.file_path.write_text(json.dumps({"f": 2}))
    scope.block_path.write_text(json.dumps({"b": 3}))

    scope.snapshot()

    # Mutate after snapshot
    scope.global_path.write_text(json.dumps({"g": 99}))
    scope.file_path.write_text(json.dumps({"f": 99}))
    scope.block_path.write_text(json.dumps({"b": 99}))

    scope.restore()

    assert json.loads(scope.global_path.read_text()) == {"g": 1}
    assert json.loads(scope.file_path.read_text()) == {"f": 2}
    assert json.loads(scope.block_path.read_text()) == {"b": 3}


def test_commit_discards_snapshot(tmp_path):
    scope = ScopeStore(tmp_path)
    scope.open_file()
    scope.global_path.write_text(json.dumps({"g": 1}))

    scope.snapshot()
    scope.global_path.write_text(json.dumps({"g": 99}))
    scope.commit()

    # No snapshot left: calling restore should be a no-op
    scope.restore()
    assert json.loads(scope.global_path.read_text()) == {"g": 99}


def test_nested_snapshot(tmp_path):
    """Stack semantics: outer snapshot is unaffected by inner commit/restore."""
    scope = ScopeStore(tmp_path)
    scope.open_file()

    scope.global_path.write_text(json.dumps({"outer": 1}))
    scope.snapshot()                               # outer snapshot

    scope.global_path.write_text(json.dumps({"inner": 2}))
    scope.snapshot()                               # inner snapshot

    scope.global_path.write_text(json.dumps({"innermost": 3}))
    scope.restore()                                # restore inner → {"inner": 2}
    assert json.loads(scope.global_path.read_text()) == {"inner": 2}

    scope.restore()                                # restore outer → {"outer": 1}
    assert json.loads(scope.global_path.read_text()) == {"outer": 1}


def test_cleanup(tmp_path):
    scope = ScopeStore.create()
    tmpdir = scope._tmpdir
    assert tmpdir.exists()
    scope.cleanup()
    assert not tmpdir.exists()
