# Backup / CLI / 進度 / Helper / 結構 / 測試（§12–§18）

← [architecture/README](README.md)

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

文檔已重組進 `docs/`、並採分層拆檔（每個大文件 → 資料夾 + README 索引）。倉庫頂層地圖見 [INDEX.md](../../../INDEX.md)；以下聚焦 Python 參考實作的 `src/` 與 `tests/`：

```
codegen/
├── README.md
├── pyproject.toml
├── docs/
│   ├── intro.md                       # 一頁速覽
│   ├── for_agent/                     # 給 AI agent 的使用手冊
│   ├── agent_skills/                  # 新增 / 補全 block 的操作 playbook
│   └── design/
│       ├── DESIGN/                    # 設計文件
│       └── architecture/              # 架構文件（本文件所在）
├── src/
│   ├── codegen/
│   │   ├── __init__.py
│   │   ├── __main__.py                # python -m codegen
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
│   │   └── helper.py                  # 內部別名：from codegen_helper import *
│   └── codegen_helper.py              # 公開 helper（使用者腳本 import codegen_helper）
├── tests/
│   ├── unit/
│   │   ├── test_config.py
│   │   ├── test_parser.py             # 含 pragma / shebang 切分
│   │   ├── test_comment_syntax.py
│   │   ├── test_scanner.py
│   │   ├── test_indent.py
│   │   ├── test_scope.py
│   │   ├── test_expander.py
│   │   ├── test_backup.py
│   │   └── test_cli.py
│   └── integration/
│       └── test_pipeline_basic.py     # 實際 spawn 子程序、跑單檔（fixture 內嵌於測試）
└── cpp/                               # 功能對齊的 C++ 重寫（見 cpp/README.md）
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
> [`cpp/README.md`](../../../cpp/README.md)。
