# 常見錯誤與完整設定方式

← [for_agent/README](README.md)

## 常見錯誤與修正方式

### 執行完找不到 block、什麼都沒發生

**原因**：block 已經執行過一次，marker 已消失。  
**修正**：先還原檔案（`git checkout file.c` 或 `codegen rollback file.c`），再重新執行。

### block 沒有輸出、檔案變成空的

**原因**：腳本沒有任何 print。  
**修正**：確認你的腳本有 `print(...)` 輸出。腳本的 stdout 是輸出，不是腳本本身的程式碼。

### 縮排跑掉（縮排層數比預期多）

**原因**：腳本的 print 輸出裡已有縮排，工具的 auto-indent 又再加一層，導致雙重縮排。  
**修正**：print 的字串從第 0 欄開始，不要自己加縮排。工具會根據 START marker 的位置自動補齊。  
**如果你確定不要自動縮排**，在 block 裡加 pragma：`# codegen: auto_indent=false`。

### 腳本 import 了非標準庫，執行時 ModuleNotFoundError

**規則**：block 腳本只能使用 **Python 標準庫** 和 **`codegen_helper`**。不要在腳本裡 import 第三方套件（例如 `requests`、`pandas`、`pyyaml`）——不保證目標環境有安裝。  
**修正**：若任務確實需要某套件，向使用者回報需求並請使用者確認安裝後再執行，不要自行執行 `pip install`。

### 腳本的輸出包含敏感資訊（API Key、密碼）

block 腳本的 **stdout 會直接寫進原始碼**。絕對不能讓腳本把 secret print 出來。  
**正確做法**：透過 `--env KEY=VAL` 由外部注入 secret，在腳本裡用 `os.environ["KEY"]` 讀取，不要 print 出來：
```python
import os
api_key = os.environ["MY_API_KEY"]   # 讀取，但不要 print(api_key)
result = do_something_with(api_key)
print(result)                         # 只 print 真正要寫進檔案的內容
```
如果任務需要 secret，告訴使用者需要用 `--env MY_API_KEY=xxx` 帶入，再由使用者執行。不要去讀取 `.env` 檔。

### codegen 執行完 exit 0，但檔案裡還有 CODEGEN_START 殘留

**原因**：block 的 `max_passes` 達到上限，工具印 warning 後繼續，不報錯、不改 exit code。  
**修正**：執行後**必須讀取檔案**，確認沒有殘留的 `CODEGEN_START` marker。不能只靠 exit code 判斷生成是否完成。

### 腳本執行失敗、看不到錯誤訊息

**修正**：加 `--strict` 讓錯誤更明顯：
```sh
codegen file.c --strict
```

### 部分 block 失敗，其餘的卻已被替換（混合狀態）

**原因**：預設 `on_error=continue` 是逐 block 處理——前面成功的 block 已替換（marker 消失），失敗的 block 保留原 marker，最後**整個檔案仍然被寫回**。結果是「成功 block 已替換、失敗 block 仍是 marker」的混合狀態。  
**策略**：
- 若需要「全部成功才寫回」：用 `--strict`（abort_all）或 per-file pragma `on_error=abort_file`
- 若已部分替換：先 rollback 再修正後重跑：`codegen rollback file.c`
- **不要只修正失敗的 block 然後重跑**——成功的 block 已無 marker，無法再次執行；只有殘留 marker 的 block 才會被執行

### 生成後 Lint 工具報告換行符錯誤（CRLF / LF 混用）

**原因**：codegen 在 POSIX（WSL）下**不做換行符正規化**。若原始檔為 CRLF（例如來自 Windows 磁碟），但腳本 stdout 輸出 LF，寫回後會產生混合換行符的檔案。  
**修正**：這是 git 設定問題，應在專案設定 `.gitattributes`（例如 `* text=auto`）由 git 統一管理換行符。腳本裡**不需要**自行偵測或轉換換行符。

### WSL 下的 chmod 疑慮（/mnt/c 路徑）

**實際情況**：codegen 執行腳本前，會把腳本寫入系統暫存目錄（`/tmp`）並賦予執行權限。暫存檔位於 WSL 自己的 Linux filesystem，不在 `/mnt/c`，chmod 完全有效。原始碼檔位於 `/mnt/c` 也沒有影響——對原始碼檔只做讀取和寫入，不需要 chmod。

---

## 完整設定方式（需要時再看）

你可以在專案根目錄放 `codegen.toml` 來設定預設行為：

```toml
extensions = [".c", ".h"]
on_error = "abort_all"
max_passes = 1
backup = true
```

若遇到表以外的副檔名（例如 `.vue`、`.svelte`），可以在 `codegen.toml` 加 `[comment_syntax]` 表自訂對應：

```toml
[comment_syntax]
".vue" = "/* */"
".svelte" = "/* */"
```

若沒有設定，預設用 `/* */`。處理未知副檔名前，先確認 `codegen.toml` 有無設定，避免用錯格式破壞檔案語法。

也可以在檔案最頂端寫 file pragma（針對單一檔案）：
```c
/* codegen: on_error=abort_all */
```

也可以在 block 裡面寫 per-block pragma（只針對這個 block）：
```c
/* CODEGEN_START
#!/usr/bin/env python3
# codegen: max_passes=3 on_error=abort_all
print("...")
CODEGEN_END */
```

pragma 的語法規則：
- `key=value` 以空格分隔，多個設定並排，例如：`max_passes=3 on_error=abort_all`
- `=` 前後**不能有空格**（`key = value` 無效，會報錯）
- 布林值寫 `true` 或 `false`，例如：`auto_indent=false`
- list 值以逗號分隔，不加空格，例如：`markers=<<<,>>>`
- **值（value）不能包含空格**，因為空格是 token 分隔符。如果需要傳入含空格的值（例如有空格的路徑），改用 `--env KEY=VAL` 把值注入成環境變數，再在腳本裡用 `os.environ["KEY"]` 讀取。

> 所有可設定欄位的完整說明、預設值、環境變數對照：README.md §「設定方式（多層覆寫）」
