# codegen 架構文件

對應 `DESIGN.md` 的實作面分解：模組劃分、核心資料結構、執行流程、內部介面。本文件以「正在動手寫」的觀點寫成，所有 §X 章節編號皆指向 `DESIGN.md`。

## 1. 整體流程

從 CLI 進來到結束的鳥瞰圖：

```
cli.main()
 ├─ argparse → 子命令分派（run / rollback）
 │
 ├─ rollback：rollback.run(paths, timestamp, list_only)
 │
 └─ run（預設）：
     1. config.resolve_initial(cli_args, env)        # CLI + env + folder toml + defaults
     2. scanner.collect_files(targets, config)       # 套用 extensions / include / exclude
     3. progress.print_plan(targets)                 # §12.1
     4. 逐檔處理：
        ├─ pipeline.process_file(path, config_chain)
        │   ├─ 讀檔 → file_pragma 解析 → 合併出 file_config
        │   ├─ backup.snapshot_file(path)
        │   ├─ scope.open_file_scope() → 寫入 $CODEGEN_FILE 暫存檔
        │   ├─ expander.process_content(content, file_config)
        │   │   └─ 對每個 top-level block 呼叫 expander.expand_block(...)
        │   └─ 將最終 content 一次寫回 disk
        └─ 視 on_error 決定是否中止後續檔案
     5. exit(code)                                   # §10.6
```

整個流程單執行緒、由上至下，掃到的檔案逐個處理，檔案內 block 由上至下。

## 2. 模組劃分

採平面結構（不分 sub-package），單一 Python package `codegen/`：

| 模組 | 職責 | 主要對外介面 |
|---|---|---|
| `cli.py` | argparse、子命令分派、SIGINT handler 安裝 | `main()` |
| `config.py` | `Config` dataclass + 多層覆寫解析 | `resolve_initial()`, `merge_pragma()` |
| `comment_syntax.py` | 副檔名 → 註解語法表、line/block 兩種風格抽象 | `lookup(ext)`, `CommentSyntax` |
| `scanner.py` | 目標展開、`extensions`/`include`/`exclude` 過濾 | `collect_files()` |
| `parser.py` | Marker/pragma/shebang 切分、找出檔案中所有 top-level block | `parse_file()`, `parse_block_header()` |
| `expander.py` | 單一 block 的多輪展開（含嵌套） | `expand_block()`, `process_content()` |
| `executor.py` | 啟動 subprocess、組 env、timeout、收 stdout/stderr | `run_block()` |
| `env.py` | 組 subprocess 環境變數（origin、scope、path 三類） | `build_env()` |
| `scope.py` | 三層 scope dict 的 JSON 檔生命週期 + 快照／回滾 | `ScopeStore`, `snapshot()`, `restore()` |
| `indent.py` | `auto_indent` 計算與套用 | `apply_indent()` |
| `backup.py` | 執行前備份 | `snapshot_file()` |
| `rollback.py` | `codegen rollback` 子命令 | `run()` |
| `pipeline.py` | 單檔 in-memory 處理流程，串 parser/expander/indent/scope | `process_file()` |
| `progress.py` | 計畫列印、SIGINT 中斷回報、abort_all 中斷回報 | `print_plan()`, `report_interrupt()` |
| `errors.py` | Exit codes 常數、自訂 exception 階層 | `BlockFailure`, `ConfigError`, `EXIT_*` |
| `helper.py` | 對 block 腳本暴露的 Python helper（可被使用者 `import codegen_helper`） | `read_global()`, `write_global()`, ... |

## 3. 核心資料結構

```python
# config.py

@dataclass(frozen=True)
class Config:
    # markers

    # 掃描（§9）
    extensions: tuple[str, ...]
    include: tuple[str, ...]
    exclude: tuple[str, ...]
    scan_all: bool

    # 副檔名 → 註解語法（§3.4）
    comment_syntax_overrides: Mapping[str, str]   # ".pyx" -> "#"

    # 展開（§6.2）
    max_passes: int
    max_total_time: float
    max_pass_time: float

    # 輸出（§5）
    keep_as_comment: bool
    auto_indent: bool

    # 備份（§7）
    backup: bool
    backup_dir: Path

    # 錯誤（§10）
    on_error: str                        # "continue" | "abort_file" | "abort_all"

    # subprocess 環境（§4.4）
    cwd: Path
    extra_env: Mapping[str, str]
    run_as_user: str | None              # 降權執行身份（名稱/uid）；pragma-forbidden


# parser.py

@dataclass
class Block:
    file_path: Path
    start_line: int                      # 1-based，§10.4 要用
    end_line: int                        # 結束 marker 所在行
    indent: str                          # START marker 的前綴空白／tab
    comment_syntax: "CommentSyntax"

    raw_block_text: str                  # 從 start marker 整段到 end marker（含 marker 與註解語法）
    inner_text: str                      # 拆掉 marker 與註解殼之後的內容（shebang + pragma + body）
    shebang: str | None                  # 第一個非空行；無 shebang → None（隱含 python3）
    pragma: Mapping[str, str]            # 已解析的 per-block pragma（§3.3 / §3.5）
    body: str                            # 去掉 shebang 與 pragma 之後的純腳本


# comment_syntax.py

@dataclass(frozen=True)
class CommentSyntax:
    open: str                            # "/*", "<!--", "#", "//", "--"
    close: str | None                    # "*/", "-->" or None（line style）

    @property
    def is_block(self) -> bool: ...      # close is not None


# scope.py

@dataclass
class ScopeStore:
    global_path: Path                    # JSON 檔
    file_path: Path
    block_path: Path
    # 內部維護 in-memory snapshot stack 給 §10.5 用


# errors.py

class CodegenError(Exception): ...
class ConfigError(CodegenError): ...
class BlockFailure(CodegenError):
    block: Block
    reason: str                          # "exit:1" | "timeout:pass" | "timeout:total" | "io" | "user:unknown:X" | "user:denied:X" | ...
    passes_stdout: list[str]
    last_stderr: str


EXIT_OK = 0
EXIT_BLOCK_FAILURE = 1
EXIT_ABORT_ALL = 2
EXIT_STARTUP = 3
EXIT_SIGINT = 130
```

## 4. 設定解析（多層覆寫）

`Config` 是 immutable。整條覆寫鏈用「逐層 merge 出新 Config」實作，不是 mutable bag。

### 4.1 解析時機

| 時機 | 函式 | 涵蓋層級 |
|---|---|---|
| 啟動時 | `resolve_initial(cli_args, env)` | 內建預設 → env → folder toml → CLI |
| 處理某檔開頭 | `merge_file_pragma(base, file_pragma)` | 上面結果 → file pragma |
| 處理某 block | `merge_block_pragma(file_cfg, block_pragma)` | 上面結果 → per-block pragma |
| 子 block 繼承（§6.2） | `merge_block_pragma(parent_cfg, child_pragma)` | 用父 block 已合併過的 cfg 當基底，再被子 pragma 覆寫 |

注意：掃描相關欄位（§9.1）只能在 CLI / folder toml / env / defaults 出現，若 file/per-block pragma 出現會被 parser 直接拒掉並報 `ConfigError`。

### 4.2 命名映射

`Config` 的欄位名以 snake_case 為準。CLI argparse 透過 `dest=` 對齊（如 `--max-pass-time` → `dest="max_pass_time"`）。Pragma key 與 Config 欄位同名。TOML 反序列化也直接對應。

`config.FIELD_NAMES`：所有合法 key 的集合，pragma 解析時拿來驗證未知 key（直接報錯，§3.5 不允許未知 key 偷渡）。

### 4.3 folder config 搜尋

`config.find_folder_toml(start: Path) -> Path | None`：從 `start`（target）往上找 `codegen.toml`，遇到 git root 或 filesystem root 就停。`--config <path>` 會跳過此搜尋直接用指定路徑。

## 5. 掃描器（`scanner.py`）

```python
def collect_files(targets: Sequence[Path], cfg: Config) -> list[Path]: ...
```

行為：

- 若 `target` 是檔案 → 直接加入結果，不套用任何過濾
- 若 `target` 是資料夾 → 遞迴展開，套用：
  1. `extensions` 過濾（`scan_all=true` 時略過此步）
  2. `include` glob 加進來（疊加在 1 之上）
  3. `exclude` glob 扣除
- glob 使用 `pathlib.Path.match` + 自製遞迴匹配（`**` 支援），不 fork 給 shell

回傳順序：對每個 target 內，按字典序遞迴。

## 6. Parser（`parser.py`）

兩個層級：檔案層 / block 層。

### 6.1 檔案層

```python
def parse_file(content: str, comment_syntax: CommentSyntax, markers: tuple[str, str])
    -> tuple[FilePragma | None, list[Block]]: ...
```

- **File pragma**：掃描檔頭第一個註解區塊（block-style 註解 → 第一段 `/* ... */`；line-style → 連續以 `#` / `//` 開頭的最頂端區塊）。內若有以 `codegen:` 開頭的行才算 pragma；否則回傳 `None`。
- **Top-level blocks**：以 marker 字串為主、用 line-by-line 掃描定位 START / END 行，不用單一 regex（避免在 block-style `/* ... */` 內遇到對註解轉義意外）。
- 回傳的 Block 只標出位置與切片，**不**遞迴拆 stdout 中的子 block——子 block 在 expander 拿到輸出後才即時 parse。

### 6.2 Block header 拆解

```python
def parse_block_header(inner_text: str) -> tuple[str | None, dict[str, str], str]: ...
    # → (shebang, pragma, body)
```

規則：

- 第一行若以 `#!` 開頭 → 視為 shebang；否則 shebang = None（呼叫端會視為 `#!/usr/bin/env python3`）
- 接著的行若是註解前綴 + ` codegen: ...` → pragma 行；多行 pragma 不支援，第二行以後不再嘗試解析（§3.5）
- 剩下的所有行 = body
- 注意：在 block-style 註解（`/* ... */`）內，`# codegen:` 的 `#` 是當前 block 腳本語言的註解符（即 shebang 指向的解譯器），不是檔案語言的註解符。pragma 前綴判定看「shebang 對應的腳本語言」對應的註解前綴。
  - Shell 腳本：`# codegen: ...`
  - Python：`# codegen: ...`
  - Node：`// codegen: ...`
  - 若 shebang 解析不出腳本語言 → 預設 `#`
  - 暫定一張 `interpreter → comment_prefix` 小表，未匹配的腳本語言視為不支援 per-block pragma（已生效的 file/folder/CLI 設定仍然作用）

### 6.3 Pragma 解析

```python
def parse_pragma_line(line: str) -> dict[str, str]: ...
```

- 切掉前綴註解 → 抓到 `codegen:` 之後的內容
- `split()` 拆 token，每個 token 必須是 `key=value` 形式
- value 中不能含空格（已被 `split()` 切開）；list 以逗號分隔留給上層 `Config` 反序列化處理（如 `markers=A,B` → `("A", "B")`）
- `true`/`false` 字面值對應 bool 欄位

未知 key、缺等號、value 空字串：拋 `ConfigError`。

## 7. Expander（`expander.py`）— per-block 多輪展開

```python
def process_content(content: str, cfg: Config, scope: ScopeStore, file_path: Path) -> str: ...
def expand_block(block: Block, parent_cfg: Config, scope: ScopeStore) -> ExpandResult: ...
```

`process_content`：

1. parser 找出檔案中所有 top-level block
2. 由上至下逐 block 呼叫 `expand_block`，將返回的字串替換掉 block 在 content 中的對應切片
3. 任一 block 失敗 → 依 `on_error` 拋 / 回報 / 略過

`expand_block`（單一 block 的多輪展開）：

```
expand_block(block, parent_cfg, scope):
    block_cfg = merge_block_pragma(parent_cfg, block.pragma)
    scope.snapshot()                            # §10.5 記下三層 dict 當前狀態

    region = block.raw_block_text               # 起始：整個原 block 文字
    elapsed_total = 0.0
    pass_idx = 0
    pass_outputs: list[str] = []

    try:
        while pass_idx < block_cfg.max_passes:
            inner_blocks = parser.find_blocks_within(region, ...)
            if not inner_blocks:
                break                            # 穩定

            # 一輪內可能有多個新 block，由上至下逐一展開
            for ib in inner_blocks:
                stdout, used_time = executor.run_block(ib, block_cfg, scope, ...)
                pass_outputs.append(stdout)
                elapsed_total += used_time
                if elapsed_total > block_cfg.max_total_time:
                    raise BlockFailure(reason="timeout:total", ...)
                stdout_indented = indent.apply_indent(stdout, ib.indent, block_cfg)
                region = splice(region, ib, stdout_indented)
            pass_idx += 1
        else:
            # 沒 break：迴圈跑完仍非穩定 → 視為達到 max_passes 上限
            warn("max_passes reached")

        scope.commit()                           # §10.5 成功 → 丟掉 snapshot
        return ExpandResult(text=region, ok=True)

    except BlockFailure as e:
        scope.restore()                          # §10.5 失敗 → 還原三層 dict
        raise
```

幾個細節：

- 第 1 輪的「inner_blocks」就是 block 自己（特別處理：第 1 輪是執行原 block 的 shebang，第 2 輪以後才是看 stdout 內有沒有新 block）
- 實作上，把第 1 輪和後續輪用同一段邏輯吃掉：region 初值就是整個 block 原文，`parser.find_blocks_within(region)` 第一次找到的就是 block 本身，第二次找到的是 stdout 中的新 block
- 「子 block 失敗 = 父 block 失敗」（§10.1 嵌套冒泡）由 `executor.run_block` 拋出 `BlockFailure` → expander 不額外處理嵌套，異常自然往上冒

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

## 12. Backup / Rollback（`backup.py`, `rollback.py`）

### 12.1 Backup

```python
def snapshot_file(path: Path, cfg: Config, run_id: str) -> Path: ...
```

- 路徑：`<backup_dir>/<相對路徑>/<timestamp>/<filename>`
- `run_id` = 整次執行的 timestamp（ISO8601 緊湊格式 `YYYYMMDDTHHMMSSZ`），同一次 run 共用
- 寫入前用 `os.replace` 確保 atomic

### 12.2 Rollback

```python
def run(paths: Sequence[Path], timestamp: str | None, list_only: bool, cfg: Config) -> int: ...
```

- `--list`：列出 `<backup_dir>` 下所有 timestamp（按時間排序）
- 預設 timestamp = 最新一次
- 把對應路徑下的備份檔複製回原位（覆蓋），exit code 依 §10.6 rollback 表

## 13. CLI（`cli.py`）

```python
def main(argv: list[str] | None = None) -> int: ...
```

- argparse 用 subparsers：`run`（預設）/ `rollback`
- 安裝 SIGINT handler：第一次 Ctrl+C → 標記 cancel flag，等當前 subprocess 收 SIGTERM 退出後 by `progress.report_interrupt()` 印報告並 exit 130；第二次 Ctrl+C → 直接 raise（讓 Python 預設行為接手）
- 頂層捕捉 `ConfigError` → exit 3，`KeyboardInterrupt` → exit 130
- abort_all 觸發時走 `progress.report_interrupt(reason="abort_all", ...)` → exit 2

## 14. 進度與中斷（`progress.py`）

```python
def print_plan(targets: list[Path]) -> None: ...                 # §12.1
def report_interrupt(state: RunState, *, kind: str, exit_code: int) -> None: ...
```

`RunState` 是 mutable singleton，pipeline / expander 在處理每個檔案 / block 時更新：

```python
@dataclass
class RunState:
    targets: list[Path]
    target_idx: int            # 1-based
    current_file: Path | None
    block_ordinal: int | None  # 第幾個 top-level block
    block_start_line: int | None
    last_failure_reason: str | None  # abort_all 時要印
```

`kind` ∈ `{"sigint", "abort_all"}`，影響抬頭文字（§12.2 / §12.3）。

## 15. Helper module（公開本體 `src/codegen_helper.py`；`codegen/helper.py` 為別名）

對 block 腳本暴露的便利 API。**實作位置**：公開模組是 top-level 的 `src/codegen_helper.py`（pyproject 用 `force-include` 裝成頂層可 import）；package 內的 `codegen/helper.py` 只是 `from codegen_helper import *` 的轉出別名。要改 helper 行為改前者。

```python
import codegen_helper as cg

cg.global_get("key")          # 讀 $CODEGEN_GLOBAL
cg.global_set("key", value)
cg.file_get("key") / cg.file_set(...)
cg.block_get(...) / cg.block_set(...)

cg.origin_file()              # 讀 $CODEGEN_ORIGIN_FILE → str
cg.origin_block()             # 讀 $CODEGEN_ORIGIN_BLOCK → str

cg.targets() -> list[str]     # 拆 $CODEGEN_TARGETS
cg.invoke_cwd() -> str
cg.file_path() -> str
```

實作只是讀環境變數 + JSON load/save。每次 set 都立即 flush（避免腳本崩潰時遺失資料；雖然失敗會被 §10.5 回滾，但成功路徑要保證寫入持久化）。

非 Python 的 block 不用 helper，直接 `jq` / 各語言原生 JSON lib。

## 16. 目錄結構

```
codegen/
├── DESIGN.md
├── architecture.md            # 本文件
├── for_agent.md               # 給 AI agent 的使用手冊
├── agent_skills.md            # 新增 / 補全 block 的操作 playbook
├── intro.md                   # 一頁速覽
├── pyproject.toml
├── README.md
├── src/
│   ├── codegen/
│   │   ├── __init__.py
│   │   ├── __main__.py        # python -m codegen
│   │   ├── cli.py
│   │   ├── config.py
│   │   ├── comment_syntax.py
│   │   ├── scanner.py
│   │   ├── parser.py
│   │   ├── expander.py
│   │   ├── executor.py
│   │   ├── env.py
│   │   ├── scope.py
│   │   ├── indent.py
│   │   ├── backup.py
│   │   ├── rollback.py
│   │   ├── pipeline.py
│   │   ├── progress.py
│   │   ├── errors.py
│   │   └── helper.py          # 內部別名：from codegen_helper import *
│   └── codegen_helper.py      # 公開 helper（使用者腳本 import codegen_helper）
└── tests/
    ├── unit/
    │   ├── test_config.py
    │   ├── test_parser.py        # 含 pragma / shebang 切分
    │   ├── test_comment_syntax.py
    │   ├── test_scanner.py
    │   ├── test_indent.py
    │   ├── test_scope.py
    │   ├── test_expander.py
    │   ├── test_backup.py
    │   └── test_cli.py
    └── integration/
        └── test_pipeline_basic.py  # 實際 spawn 子程序、跑單檔（fixture 內嵌於測試）
```

`codegen_helper` 透過 pyproject 的 `[tool.hatch.build.targets.wheel.force-include]`（把 `src/codegen_helper.py` 裝成頂層模組）讓使用者腳本能 `import codegen_helper`；`codegen/helper.py` 則以 `from codegen_helper import *` 轉出同一套 API 作為內部別名。

## 17. 測試策略

| 層級 | 範圍 | 工具 |
|---|---|---|
| Unit | parser / pragma / config merge / indent / scope snapshot | pytest，純函式 |
| Integration | pipeline 端對端跑單檔，spawn 真子程序，斷言檔案最終內容 | pytest + tmp_path fixture |
| CLI smoke | 用 `tmp_path` 寫小範例跑 `python -m codegen run <file>` | pytest + capsys |
| Rollback | snapshot → 跑 → rollback → 比對檔案內容 | pytest |

涵蓋的關鍵情境：

- 單 block 一輪展開 / 多輪嵌套展開（§6.2）
- 子 block 失敗導致父 block 失敗 + scope dict 還原（§10.1, §10.5）
- `max_passes` / `max_total_time` / `max_pass_time` 三種上限分別觸發
- File pragma + per-block pragma 同時存在時的優先級
- `auto_indent` on / off
- `keep_as_comment` on / off
- POSIX-only 行為（用 `pytest.mark.skipif(sys.platform == 'win32')` 全跳）
- 掃描過濾（extensions / include / exclude / scan_all）
- Backup + rollback 來回
- SIGINT 中斷時當前 block 視為失敗、已完成 block 結果保留
- `abort_all` 觸發時的 exit code 與輸出格式

## 18. 暫不處理（之後再說）

對應 DESIGN §13 + 一些實作面的判斷延後：

- Watch 模式
- glob 對特定路徑套用不同 markers
- `--verbose` / `--quiet` / structured logging
- Helper 之外的多語言便利庫（Ruby/JS 版 codegen_helper；C++ 版已由 cpp/ 重寫提供，見下方備註）
- Parallelism（明確不做：§6.1 單執行緒是設計保證）
- Windows 支援（Python 版明確不做：§1；C++ 重寫已支援，見下方備註）

> **備註：C++ 重寫。** 本文件描述的是 `src/codegen/` 的 Python 參考實作。專案另有一份功能對齊的
> C++ 重寫位於 `cpp/`，並額外提供 Windows 支援、超時整樹 kill、`--run-as-user` 降權，以及
> C++ block 用的 `cpp/include/codegen_helper.hpp`。建置與差異說明見
> [`cpp/README.md`](../../cpp/README.md)。
