# 執行：Executor / Env / Scope / Indent（§8–§11）

← [architecture/README](README.md)

## 8. Executor（`executor.py`）

```python
def run_block(block: Block, cfg: Config, scope: ScopeStore, file_path: Path) -> tuple[str, float]: ...
    # → (stdout, elapsed_seconds)
```

職責：

1. 把 `block.inner_text`（不含 marker 殼）寫到暫存檔，加 `chmod +x`
   - 若 `block.shebang is None` → 在第一行補 `#!/usr/bin/env python3` 後再寫入
   - 若設了 `run_as_user` → 暫存檔改 `0o755`，讓降權後的使用者讀得到
2. 用 `env.build_env(...)` 組環境變數
3. `subprocess.Popen([tmp_path], cwd=cfg.cwd, env=..., start_new_session=True)`
   - `start_new_session=True` 讓 block 自成 process group
   - 設 `run_as_user` 時，解析成 uid/gid/supplementary groups 傳給 `user=`/`group=`/`extra_groups=`（需 root；解析不到 → `user:unknown`，權限不足 → `user:denied`）
4. `proc.communicate(timeout=cfg.max_pass_time)` 計時；逾時 → `os.killpg(SIGKILL)` 整個 process group（連 block 自己背景化的子進程一起收掉），再 reap
5. 收 stdout 全文（保留結尾換行，§2）；stderr buffer 起來但不轉發
6. 若退出碼 0 → 回 `(stdout, elapsed)`
7. 若失敗 → 拋 `BlockFailure(block, reason, ...)`，由 §10.4 統一在頂層印 stderr

stderr 在成功時直接丟棄；失敗時保留給 §10.4 印出。

## 9. Env 組裝（`env.py`）

```python
def build_env(block: Block, cfg: Config, scope: ScopeStore, ctx: RunContext) -> dict[str, str]: ...
```

`ctx` 包含 `invoke_cwd`, `targets`, `origin_file_snapshot_path`, `origin_block_snapshot_path`。

內容：

- 繼承 `os.environ`（不洩漏 codegen 自己的 internal env）
- 套上 `cfg.extra_env`（folder toml `[env]` + `--env`）
- `CODEGEN_GLOBAL` / `CODEGEN_FILE` / `CODEGEN_BLOCK` ← scope JSON 路徑（§4.1）
- `CODEGEN_ORIGIN_FILE` ← 整檔處理前快照路徑（§4.2）
- `CODEGEN_ORIGIN_BLOCK` ← 該 block 原文快照路徑（§4.2）
  - 對展開出來的子 block：指向「該子 block 在父 block 該輪 stdout 中的原樣內容」的暫存檔；expander 在每次找到 inner_blocks 時就為各個 inner block 各自寫一份快照
- `CODEGEN_INVOKE_CWD` / `CODEGEN_TARGETS` / `CODEGEN_FILE_PATH`（§4.3）

> **`run_as_user` 的檔案權限**：降權後 block 由另一個 uid 執行，因此它要存取的
> scope JSON（讀寫，`0o666`，所在暫存目錄 `0o777`）與 origin 快照（唯讀，`0o644`）
> 都會在建立時放寬權限。預設（未設 `run_as_user`）一律維持 `0o600`/`0o700` 不變。

## 10. Scope（`scope.py`）

```python
class ScopeStore:
    def __init__(self, global_path: Path):
        self.global_path = global_path           # 跨檔共享，初始為 {}
    def open_file(self) -> Path: ...             # 建檔案級暫存 JSON，§4.1
    def open_block(self) -> Path: ...            # 建 block 級暫存 JSON
    def snapshot(self) -> None: ...              # in-memory 快照三層 JSON 內容
    def commit(self) -> None: ...                # 丟掉最新 snapshot
    def restore(self) -> None: ...               # 把三層 JSON 寫回 snapshot 內容
```

實作要點：

- 三個 JSON 檔放在 `tempfile.mkdtemp()` 下，process 結束時清理
- snapshot 是讀整個檔案的 bytes 推到 stack；restore 把 bytes 寫回
- 巢狀 `snapshot()` / `restore()` 必須能正確配對（每個 block 進入時 push、結束時 pop），用 stack 而非單一 slot

## 11. Indent（`indent.py`）

```python
def apply_indent(stdout: str, base_indent: str, cfg: Config) -> str: ...
```

- `cfg.auto_indent=False` → 原樣回傳
- 否則：取 `block.indent`（即 START marker 那行的 leading whitespace），對 stdout 每一**非空**行前綴此字串。空行不加（避免行尾空白）。
- 結尾換行保留
