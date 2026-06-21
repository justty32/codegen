# §11. CLI 介面草案 ｜ §12. 進度輸出與中斷處理 ｜ §13. 仍待決定 / 之後再說

← [DESIGN/README](README.md)

## 11. CLI 介面草案

```
codegen [run] [paths...] [options]
codegen rollback [paths...] [--timestamp <ts>] [--list]

Options:
  --config <path>          顯式指定 codegen.toml 路徑（跳過向上搜尋）
  --markers <start,end>    覆寫 marker
  --keep-as-comment        保留 source 為註解
  --auto-indent            開啟自動對齊（預設）
  --no-auto-indent         關閉自動對齊
  --backup-dir <path>      指定備份根目錄
  --no-backup              關閉備份
  --all                    忽略副檔名清單
  --ext <list>             覆寫副檔名清單（逗號分隔，如 .c,.h,.py）
  --include <glob>         額外納入的 glob（可重複）
  --exclude <glob>         排除的 glob（可重複）
  --max-passes <n>         單一 block 最大展開輪數（預設 1）
  --max-total-time <sec>   單一 block 最大展開累積時間（預設 5.0）
  --max-pass-time <sec>    單一 pass（subprocess）最大執行時間（預設 5.0）
  --on-error <mode>        錯誤處理模式：continue|abort_file|abort_all
  --strict                 等同 --on-error abort_all
  --cwd <path>             覆寫 subprocess 的 working directory（預設為 codegen 執行時的 cwd）
  --env KEY=VAL            注入給 subprocess 的環境變數（可重複）
  --dry-run                逐檔執行 block 腳本，但結果不寫回 disk，改印出處理後的整檔內容到 stdout（副作用如檔案 I/O、網路由腳本自負）
```

## 12. 進度輸出與中斷處理

### 12.1 開始時列舉處理順序

執行 `codegen` 一開始（解析完設定、確認要處理的目標後），印出將處理的資料夾／路徑依序列表，例如：

```
codegen: 將依序處理 3 個目標：
  [1/3] src/
  [2/3] lib/
  [3/3] tools/extra.c
```

讓使用者在開始前就清楚整個批次的範圍。

### 12.2 Ctrl+C 中斷時的回報

捕捉 SIGINT（Ctrl+C），中斷時印出當下處理位置再退出：

```
^C
codegen: 已中止。當前處理至：
  目標：[2/3] lib/
  檔案：lib/foo.c
  Block：第 3 個（行 87）
```

行為細節：

- 已寫回的 block 結果**保留**（已完成的工作不回滾，要回滾用 §8 rollback）
- 當前正在執行中的 subprocess 收 SIGTERM，未完成的 block 視為失敗（依 `on_error` 規則處理為「中止」）
- exit code 設定為 130（SIGINT 慣例）

### 12.3 `abort_all` 觸發中止時的回報

`on_error=abort_all` 觸發時，輸出格式同 §12.2，只是抬頭與原因不同（exit code 為 §10.6 的 2）：

```
codegen: 已中止（abort_all 觸發）。當前處理至：
  目標：[2/3] lib/
  檔案：lib/foo.c
  Block：第 3 個（行 87）
  失敗原因：subprocess 退出碼 1
```

## 13. 仍待決定 / 之後再說

- 是否提供 watch 模式（監看檔案變動自動重跑）
- 設定檔是否支援以 glob 對特定路徑套用不同 markers
- Python helper module 的具體 API
- `--verbose` / `--quiet` / log levels（目前只有單一輸出層級）
