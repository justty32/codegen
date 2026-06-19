from __future__ import annotations

import json
import tempfile
from pathlib import Path


class ScopeStore:
    """Manages three JSON files for §4.1 scope dicts and §10.5 snapshot/restore."""

    def __init__(self, tmpdir: Path, *, world_accessible: bool = False) -> None:
        self._tmpdir = tmpdir
        self._world = world_accessible
        self._global_path = tmpdir / "global.json"
        self._file_path: Path | None = None
        self._block_path: Path | None = None
        self._snapshots: list[tuple[bytes, bytes, bytes]] = []

        self._global_path.write_text("{}", encoding="utf-8")
        self._publish(self._global_path)

    def _publish(self, path: Path) -> None:
        """Make *path* read/writable by a dropped-privilege block (run_as_user)."""
        if self._world:
            try:
                path.chmod(0o666)
            except OSError:
                pass

    @property
    def global_path(self) -> Path:
        return self._global_path

    @property
    def file_path(self) -> Path:
        assert self._file_path is not None, "open_file() not called"
        return self._file_path

    @property
    def block_path(self) -> Path:
        assert self._block_path is not None, "open_block() not called"
        return self._block_path

    def open_file(self) -> Path:
        self._file_path = self._tmpdir / "file.json"
        self._file_path.write_text("{}", encoding="utf-8")
        self._publish(self._file_path)
        return self._file_path

    def open_block(self) -> Path:
        self._block_path = self._tmpdir / "block.json"
        self._block_path.write_text("{}", encoding="utf-8")
        self._publish(self._block_path)
        return self._block_path

    def close_block(self) -> None:
        if self._block_path and self._block_path.exists():
            self._block_path.write_text("{}", encoding="utf-8")

    def close_file(self) -> None:
        if self._file_path and self._file_path.exists():
            self._file_path.write_text("{}", encoding="utf-8")
        self._file_path = None

    def snapshot(self) -> None:
        """Push a snapshot of all three JSON files onto the stack (§10.5)."""
        g = self._global_path.read_bytes()
        f = self._file_path.read_bytes() if self._file_path else b"{}"
        b = self._block_path.read_bytes() if self._block_path else b"{}"
        self._snapshots.append((g, f, b))

    def commit(self) -> None:
        """Pop and discard the top snapshot (block succeeded)."""
        if self._snapshots:
            self._snapshots.pop()

    def restore(self) -> None:
        """Pop and restore the top snapshot (block failed, §10.5)."""
        if not self._snapshots:
            return
        g, f, b = self._snapshots.pop()
        self._global_path.write_bytes(g)
        if self._file_path:
            self._file_path.write_bytes(f)
        if self._block_path:
            self._block_path.write_bytes(b)

    @classmethod
    def create(cls, *, world_accessible: bool = False) -> "ScopeStore":
        tmpdir = Path(tempfile.mkdtemp(prefix="codegen_scope_"))
        if world_accessible:
            # mkdtemp is 0700; a dropped-privilege block must be able to traverse
            # the dir to reach the scope JSON files it reads/writes.
            try:
                tmpdir.chmod(0o777)
            except OSError:
                pass
        return cls(tmpdir, world_accessible=world_accessible)

    def cleanup(self) -> None:
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)
