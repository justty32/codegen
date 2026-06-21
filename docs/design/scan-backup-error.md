# §7. 備份 ｜ §8. 回滾 ｜ §9. 檔案掃描 ｜ §10. 錯誤處理

← [design/README](README.md)

## 7. 備份

預設啟用，存放規則：

- 預設位置：`<backup_dir>/<相對路徑>/<timestamp>/`
- `<backup_dir>` 預設為 `<cwd>/.codegen-backup/`，可用 `--backup-dir <path>` 指定
  - 若指定為與輸入資料夾相同 → 等於就地備份
- `<相對路徑>` 是處理目標相對於 `<backup_dir>` 父資料夾的相對路徑（預設下等同相對 cwd）
- `--no-backup`：關閉備份

備份內容：執行前的整份原始檔（不只是 block）。

## 8. 回滾

簡化設計（搭配 git 使用，所以工具本身不做歷史導覽）：

- `codegen rollback [paths...]`：把指定路徑（檔案或資料夾）回復到**最近一次執行前**的狀態，從 backup 目錄讀
- `codegen rollback --timestamp <ts> [paths...]`：回復到指定 timestamp 那次
- `codegen rollback --list [paths...]`：列出可用的備份 timestamp

不做：分支式版本樹、跨 timestamp 合併、互動式選擇。需要更複雜的歷史管理就用 git。

## 9. 檔案掃描

掃描相關設定：

| 設定 | 預設 | 說明 |
|---|---|---|
| `extensions` | 內建副檔名清單（見 §3.4） | 要掃描的副檔名清單 |
| `scan_all` | false | 忽略副檔名清單，全掃（看內容是否有 marker） |
| `include` | `[]` | 額外要納入的 glob（疊加在 `extensions` 之上） |
| `exclude` | `[]` | 要排除的 glob（如 `**/node_modules/**`） |

行為：

- 預設：依 `extensions` 過濾，再扣掉 `exclude`
- `scan_all=true` 或 `--all`：忽略 `extensions`，仍套用 `exclude`
- CLI 直接給個別檔案路徑：照給定路徑處理，不套用任何過濾

### 9.1 套用層級（檔案掃描專屬）

掃描設定的覆寫鏈和其他設定不同——因為決定「要不要掃這個檔」必須在打開檔案之前，所以**不適用 file pragma 與 per-block pragma**。實際的覆寫順序由高至低：

1. CLI 參數（`--all`、`--ext`、`--include`、`--exclude` 等）
2. 資料夾全域設定檔（`codegen.toml`）
3. 環境變數
4. 內建預設

`codegen.toml` 範例：

```toml
extensions = [".c", ".h", ".py"]
exclude = ["**/build/**", "**/.venv/**"]
scan_all = false
```

## 10. 錯誤處理

block 失敗的觸發條件：subprocess 非 0、timeout、超過 `max_total_time` 仍未穩定、無法寫回檔案等。

### 10.1 `on_error` 設定

| 值 | 行為 |
|---|---|
| `continue`（預設） | 印 stderr、跳過該 block、繼續處理同檔案後面的 block 與其他檔案 |
| `abort_file` | 印 stderr、停止處理當前檔案剩下的 block，但繼續處理下一個檔案 |
| `abort_all` | 印 stderr、立即中止整個批次 |

不論選哪一種，只要過程中曾發生失敗，CLI 最終 exit code 都是非 0。

**嵌套情境下的失敗傳遞**：子 block 失敗即視為父 block 失敗，逐層往上冒泡。所以 `abort_file` 在子/孫 block 失敗時同樣會中止整個檔案；`abort_all` 同理會中止整個批次。

### 10.2 覆寫層級

`on_error` 套用完整的多層覆寫鏈（同 §3）：per-block pragma > CLI > file pragma > folder config > 環境變數 > 內建預設。

典型使用情境：

- 在 `codegen.toml` 裡寫 `on_error = "abort_all"` → 該資料夾任何 block 出錯就立即中止
- 在某個 block 的 pragma 裡寫 `# codegen: on_error=abort_all` → 只有這個關鍵 block 出錯時才整個中止，其他 block 仍走預設

### 10.3 `--strict` 旗標

`--strict` 等同 `--on-error abort_all`，保留作便利寫法。

### 10.4 錯誤時的診斷輸出

不論 `on_error` 是哪種模式，只要 block 失敗，工具都會把以下三項印到 stderr 協助除錯，再依 `on_error` 決定後續流程（行號為起始 marker 所在行，1-based）：

| 項目 | 內容 |
|---|---|
| 原始 block | 展開前的 shebang + 腳本本體（即 `$CODEGEN_ORIGIN_BLOCK` 的內容） |
| 每一輪 pass 的 stdout | 第 1 輪、第 2 輪……依序列出每一輪 subprocess 的 stdout |
| 失敗原因 | 失敗 pass 的退出碼、stderr，是否 timeout（`max_pass_time` 或 `max_total_time` 觸發）等 |

範例：

```
codegen: block 失敗 — lib/foo.c 行 42
原始 block:
  #!/usr/bin/env python3
  print("...")
pass 1 stdout:
  ...
pass 2 stdout:
  ...
pass 3 失敗（max_pass_time 0.5s 超時）:
  stderr: ...
```

寫回檔案之前的原始整檔內容透過 `$CODEGEN_ORIGIN_FILE` 取得（見 §4.2），需要回到完整原檔則用 §8 rollback。

### 10.5 Scope dict 的快照與回滾

每個 block 執行**前**，工具會把 `$CODEGEN_GLOBAL`、`$CODEGEN_FILE`、`$CODEGEN_BLOCK` 三個 JSON 各自快照一份（in-memory）。block 執行失敗（subprocess 非 0、timeout、寫回失敗等）時，自動把這三個 JSON 回滾到該 block 執行**前**的狀態，避免半寫的鍵值污染後續 block。

回滾只動 scope dict——已成功寫回記憶體（或 disk）的 block 結果不會被回滾，要回到原檔請用 §8 rollback。

### 10.6 Exit codes

| Code | 意義 |
|---|---|
| 0 | 全部成功 |
| 1 | 至少一個 block 失敗（`continue` 或 `abort_file` 模式下繼續跑完仍計為失敗） |
| 2 | `abort_all` 觸發的整批中止 |
| 3 | 啟動前的設定／參數錯誤（找不到目標、TOML 解析失敗、CLI 參數無效等） |
| 130 | SIGINT（Ctrl+C，見 §12.2） |

`--dry-run` 模式採同一組 codes（block 失敗仍回 1），只是不寫回 disk。

`codegen rollback` 子命令也共用此表：

| Code | 意義 |
|---|---|
| 0 | 全部路徑回滾成功 |
| 1 | 至少一個路徑回滾失敗（備份檔損毀、目的地無法寫入等） |
| 3 | 啟動錯誤（找不到備份目錄、`--timestamp` 不存在、目標路徑無效等） |
| 130 | SIGINT |
