# codegen 架構文件

← [docs/design](../)｜[index](../../../index.md)

對應 [`design/`](../README.md) 的實作面分解：模組劃分、核心資料結構、執行流程、內部介面。本文件以「正在動手寫」的觀點寫成，所有 §X 章節編號皆指向 [`design/`](../README.md)。

本架構文件已按主題拆檔，本檔是索引入口；§1 整體流程、§2 模組劃分留在此處當總覽，其餘章節見下表。

## 章節索引

| § | 主題 | 檔案 |
|---|------|------|
| 1 | 整體流程 | 本檔（下方）|
| 2 | 模組劃分 | 本檔（下方）|
| 3 | 核心資料結構 | [data-config.md](data-config.md) |
| 4 | 設定解析（多層覆寫）| [data-config.md](data-config.md) |
| 5 | 掃描器 | [scan-parse.md](scan-parse.md) |
| 6 | Parser | [scan-parse.md](scan-parse.md) |
| 7 | Expander（per-block 多輪展開）| [scan-parse.md](scan-parse.md) |
| 8 | Executor | [execute.md](execute.md) |
| 9 | Env 組裝 | [execute.md](execute.md) |
| 10 | Scope | [execute.md](execute.md) |
| 11 | Indent | [execute.md](execute.md) |
| 12 | Backup / Rollback | [ops.md](ops.md) |
| 13 | CLI | [ops.md](ops.md) |
| 14 | 進度與中斷 | [ops.md](ops.md) |
| 15 | Helper module | [ops.md](ops.md) |
| 16 | 目錄結構 | [ops.md](ops.md) |
| 17 | 測試策略 | [ops.md](ops.md) |
| 18 | 暫不處理 | [ops.md](ops.md) |

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
