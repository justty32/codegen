← [docs/design](../) ｜ [INDEX](../../../INDEX.md)

# codegen 設計文件

一個跨語言的 in-source 程式碼生成工具：在原始檔中以註解區塊定義生成腳本，執行後把腳本 stdout 寫回檔案。

## 1. 工具實作

- 語言：Python
- CLI 名稱（暫定）：`codegen`
- 平台：POSIX（Linux / macOS）。**不支援 Windows**——shebang、`chmod +x`、SIGINT 行為都依賴 POSIX。
- 檔案編碼：一律 UTF-8（無 BOM）；非 UTF-8 檔案不在支援範圍。

## 2. Block 格式（shebang 風格）

Block 由開頭/結尾 marker 包住，內容第一行為 shebang，剩下是該直譯器的程式內容。任何能用 shebang 啟動的程式都能用。

```c
int x;
```

執行流程：解析出 block → 寫入暫存檔並設為可執行 → spawn subprocess → 取 stdout → 替換掉該 block 在記憶體內的對應位置（最後再一次性寫回 disk，見 §6.1）。

Subprocess stderr：成功時由工具吞掉，不轉發、不存檔；失敗時才依 §10.4 印出。

幾個邊界規則：

- **第一行非 shebang**：視為 Python 原始碼處理（隱含 `#!/usr/bin/env python3`）。
- **shebang 找不到 interpreter**：直接報錯（依 §10 `on_error` 處理）。
- **stdout 結尾換行**：原樣保留，不額外 strip 也不額外加。後續格式化交給 formatter。

## 章節索引

| 章節 | 檔案 |
|---|---|
| §1 工具實作、§2 Block 格式 | 本檔（總覽，見上方） |
| §3 Marker 與選項的設定（3.1–3.5） | [markers-config.md](markers-config.md) |
| §4 Block 執行環境、§5 輸出與保留原 source、§6 執行模型與嵌套 | [execution.md](execution.md) |
| §7 備份、§8 回滾、§9 檔案掃描、§10 錯誤處理（10.1–10.6） | [scan-backup-error.md](scan-backup-error.md) |
| §11 CLI 介面草案、§12 進度輸出與中斷處理、§13 仍待決定 / 之後再說 | [cli-progress.md](cli-progress.md) |
