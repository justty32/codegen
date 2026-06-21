# codegen（C++ 版）

`codegen` 的 C++ 重寫，與 `src/codegen/` 的 Python 參考實作功能對齊，並額外提供跨平台
（Windows）與執行隔離（超時整樹 kill、降權執行）等強化。產物是**單一原生執行檔**，
不需要安裝 Python runtime。

> 工具本身的概念、block 語法、`codegen.toml` 設定項說明，請看專案根目錄的
> [`README.md`](../README.md)、[`intro.md`](../intro.md) 與 [`architecture.md`](../architecture.md)。
> 本文件只說明 C++ 版特有的建置、使用與差異。

---

## 建置

需求：

- CMake ≥ 3.20
- 支援 C++17 的編譯器（GCC / Clang / MSVC / MinGW）
- 首次 configure 需要網路：相依函式庫透過 CMake `FetchContent` 抓取
  （[CLI11](https://github.com/CLIUtils/CLI11)、[nlohmann/json](https://github.com/nlohmann/json)、
  [tomlplusplus](https://github.com/marzer/tomlplusplus)）

```sh
cd cpp
cmake -S . -B build
cmake --build build
```

產物：`cpp/build/codegen`。

---

## 使用

```sh
# 展開檔案或目錄裡的 codegen block（run 是預設子指令，可省略）
codegen run path/to/file.c
codegen path/to/dir/

# 預演：印出處理後內容，不寫回磁碟
codegen run --dry-run file.c

# 還原到先前的備份
codegen rollback --list
codegen rollback -t <timestamp> file.c
```

### `run` 主要選項

| 選項 | 說明 |
| --- | --- |
| `--config <path>` | 指定 `codegen.toml` 路徑 |
| `--markers <start,end>` | 覆寫 block 標記（預設 `CODEGEN_START,CODEGEN_END`） |
| `--ext <list>` / `--all` | 覆寫副檔名白名單 / 忽略白名單 |
| `--include` / `--exclude <glob>` | 額外納入 / 排除 glob（可重複） |
| `--keep-as-comment` | 展開後保留原始 block 為註解 |
| `--auto-indent` / `--no-auto-indent` | 自動縮排（預設開） |
| `--backup-dir <dir>` / `--no-backup` | 備份目錄（預設 `.codegen-backup`）/ 關閉備份 |
| `--max-passes <n>` | 每個 block 的最大展開回合（預設 1） |
| `--max-total-time <s>` / `--max-pass-time <s>` | block 總時限 / 單一子程序時限（預設皆 5.0 秒） |
| `--on-error <continue\|abort_file\|abort_all>` / `--strict` | 錯誤處理模式（`--strict` 等同 `abort_all`） |
| `--cwd <dir>` | 覆寫子程序工作目錄 |
| `--env KEY=VAL` | 注入環境變數（可重複） |
| `--run-as-user <name\|uid>` | 降權執行 block（需 root，僅 POSIX） |
| `--dry-run` | 只印出，不寫回 |

`codegen.toml` 的設定鍵（`markers`、`max_passes`、`max_total_time`、`max_pass_time`、
`keep_as_comment`、`auto_indent`、`backup`、`backup_dir`、`on_error`、`cwd`、`extensions`、
`include`、`exclude`、`scan_all`、`comment_syntax_overrides`、`extra_env`、`run_as_user`）
與 Python 版相同，且同樣支援子目錄層級的設定覆寫。詳見根目錄 README。

---

## 與 Python 版的差異

兩者 block 語法、`codegen.toml` schema、三層 scope（global/file/block）行為一致。
C++ 版額外提供：

| 功能 | C++ 版 | Python 版 |
| --- | :---: | :---: |
| Windows 子程序支援（`CreateProcess`） | ✅ | ❌ |
| POSIX→Windows 直譯器路徑映射（`PathMapper`，讀 `CODEGEN_PATH_MAP`） | ✅ | ❌ |
| 超時時 kill 整個 process group（POSIX）/ Job Object（Windows） | ✅ | ❌ |
| `--run-as-user` 降權執行（setgroups→setgid→setuid，POSIX） | ✅ | ❌ |
| 免 Python runtime，單一原生執行檔 | ✅ | ❌ |

預設直譯器：block 未寫 shebang 時，POSIX 與 Windows 端的 fallback 皆為
`#!/usr/bin/env python3`（走 `PATH` 尋找 `python3`，**並非**硬寫死 `/usr/bin/python`）。

---

## 平台支援

- **POSIX（Linux/macOS）**：`fork`/`execve`/`poll` 執行 block；子程序 `setsid()` 自成一個
  process group，超時以 `kill(-pid, SIGKILL)` 收掉整棵子樹。
- **Windows**：`CreateProcessA` 搭配 Job Object（`KILL_ON_JOB_CLOSE`）執行，超時以
  `TerminateJobObject` 收掉整樹。block 的 shebang 會被轉成 Windows 可執行命令：
  - `#!/usr/bin/env <interp>` → 取 `<interp>`，走 `PATH` 尋找；
  - `#!/絕對/路徑/<interp>` → 先查 `PathMapper`（環境變數 `CODEGEN_PATH_MAP`，格式
    `posix路徑=windows路徑`，多筆以 `;` 分隔），查不到則退回 basename 走 `PATH`；
  - `#!<interp>`（無斜線）→ 原樣走 `PATH`。

    暫存腳本副檔名依直譯器推斷（python→`.py`、bash/sh→`.sh`、node→`.js`、ruby→`.rb`、
    perl→`.pl`、lua→`.lua`，其餘 `.sh`）。
  - `--run-as-user` 在 Windows 沒有 `setuid` 對應，會明確以 `user:unsupported` 失敗，
    而非默默忽略。

---

## C++ block helper

若要用 **C++ 撰寫 block 腳本**，可使用單檔、零依賴（僅 C++17 標準庫）的便利 API：

- 標頭：[`include/codegen_helper.hpp`](include/codegen_helper.hpp)
- 說明與可執行 demo：[`examples/codegen_helper/`](examples/codegen_helper/)

它提供 `codegen::global()/file()/block()` 三層 scope（`set`/`get_str`/`get_int`/
`get_double`/`get_bool`/`set_json`/`get_json`/`has`/`del`）與唯讀 context
（`origin_file`/`origin_block`/`targets`/`invoke_cwd`/`file_path`），讀寫的是與 Python
block、runner 共用的同一份 scope JSON，值能跨語言互通。

---

## 原始碼結構

`src/` 與 Python 版逐一對應（`scanner`、`parser`、`expander`、`executor`、`pipeline`、
`scope`、`backup`、`rollback`、`config`、`indent`、`env`、`comment_syntax`、`progress`、
`errors`）。C++ 版另有：

- `executor.cpp` — POSIX `fork/exec` 與 Windows `CreateProcess` 兩套實作（以 `_WIN32` 分支）。
- `path_mapper.{hpp,cpp}` — Windows 的 POSIX→Windows 直譯器路徑映射。
- `utils.hpp` — 字串小工具（`lstrip`/`starts_with`/`ends_with` 等）。

依賴管理由 `CMakeLists.txt` 的 `FetchContent` 處理。
