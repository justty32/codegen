# codegen 使用手冊（for AI agent）

這份文件告訴你如何在原始碼檔案中寫 codegen block，以及如何執行 codegen CLI。

---

## codegen 做什麼

你在原始檔裡寫一段「腳本 + 特殊 marker」。執行 `codegen <檔案>` 後，工具會：
1. 找到這段 marker 框住的腳本
2. 執行腳本
3. 把腳本的 **stdout** 寫回原位，**替換掉整個 block（包含 marker 自身）**

執行完之後 marker 就消失了，無法再次執行。這是設計如此，不是 bug。

**只支援 Linux / macOS，不支援 Windows。** 在 Windows 環境下需透過 WSL（Windows Subsystem for Linux）執行；若無法切換，應提示使用者切換到 WSL 或 Linux/macOS 環境，自己不要嘗試執行 codegen。

---

## 如何在檔案裡寫 block

### 規則

1. 用 marker 把腳本包起來
2. 第一行寫 shebang（`#!/usr/bin/env python3` 之類）
3. 腳本的 **stdout** 就是要生成的內容
4. 只輸出你真正想寫進檔案的行，不要多印

### 各語言的正確寫法

**C / C++ / Rust / Go**（用 `/* */`）：
```c
/* CODEGEN_START
#!/usr/bin/env python3
print("int x = 42;")
CODEGEN_END */
```

**Python / Shell / YAML**（用 `#`）：
```python
# CODEGEN_START
# #!/usr/bin/env python3
# print("GENERATED = True")
# CODEGEN_END
```

#### line-style block 裡的空行

在 `#` 系列（Python / Shell / YAML）的 block 內，空行有三種等效寫法：
- `#`（只有井字號）—— 建議，無尾隨空白
- `# `（井字號 + 空格）
- 完全空行（什麼都不寫）

三種都不會讓 parser 誤判成 block 結束，可以安心使用。建議統一用 `#`。

**HTML / Markdown / XML**（用 `<!-- -->`）：
```html
<!-- CODEGEN_START
#!/usr/bin/env python3
print("<li>item</li>")
CODEGEN_END -->
```

### 省略 shebang

如果第一行不是 `#!` 開頭，整個 block 就當 Python 腳本執行：
```c
/* CODEGEN_START
for i in range(3):
    print(f"int x{i};")
CODEGEN_END */
```

### 縮排（auto-indent）

codegen 會偵測 `CODEGEN_START` 那一行前面有幾個空格（稱為「block 縮排」），然後把腳本 stdout 的**每一行**都加上這段縮排，再寫回檔案。

**你的腳本輸出不需要自己加縮排，工具會幫你補。**

#### 範例：block 在函式裡（前面有 4 個空格）

寫入檔案的樣子：
```c
void foo() {
    /* CODEGEN_START
    #!/usr/bin/env python3
    print("int x = 1;")
    print("int y = 2;")
    CODEGEN_END */
}
```

執行 codegen 後：
```c
void foo() {
    int x = 1;
    int y = 2;
}
```

工具把 `int x = 1;` 和 `int y = 2;` 各加上 4 個空格（跟 START marker 同一層）。

**補充：block 裡的腳本行本身有縮排是正常的，不是問題。** 在上面的例子中，`    #!/usr/bin/env python3` 和 `    print(...)` 這些腳本行在檔案裡有 4 格縮排——工具在執行前會自動把腳本的共同縮排移掉（dedent），所以執行的是無縮排的腳本。你唯一需要注意的是：**print 字串的內容**不要重複加 block 的縮排層級。

#### 反例：自己多加縮排，結果縮排兩次

```c
void foo() {
    /* CODEGEN_START
    #!/usr/bin/env python3
    print("    int x = 1;")   # print 裡已有 4 個空格
    CODEGEN_END */
}
```

執行後變成（縮排 8 格，錯誤）：
```c
void foo() {
        int x = 1;
}
```

**結論：你的 print 輸出永遠從第 0 欄開始，不要自己加縮排。**

#### block 在第 0 欄（沒有縮排）

如果 START marker 前面沒有空格，auto-indent 不加任何東西，stdout 原樣寫回：
```c
/* CODEGEN_START
#!/usr/bin/env python3
print("int x = 1;")
CODEGEN_END */
```
執行後：
```c
int x = 1;
```

#### 限制：CODEGEN_START 那行的前面只能有空白字元

`CODEGEN_START` 必須位於**獨立的一行**，前面只能有空格/tab，**不能有其他程式碼**。例如 `if (x) { /* CODEGEN_START` 這種把 block 塞在行尾的寫法，auto-indent 偵測到的縮排會是空字串（`""`），導致縮排失效。應該另起一行放 block。

> 更多 block 格式細節（per-block pragma、自訂 marker 等）：README.md §「Block 格式」

---

## 執行指令

### 最基本的用法

```sh
# 處理單一檔案
codegen path/to/file.c

# 遞迴處理整個目錄
codegen src/
```

**執行前建議確認 git 狀態。** 若工作目錄有未 commit 的修改，萬一需要 rollback 時，`git checkout` 會連帶丟棄手動更動。執行前先知道目前的變更範圍，出問題時才有退路。

**執行後必須驗證結果。** codegen 達到 `max_passes` 上限時只會印 warning，**exit code 仍為 0**。不能僅憑 exit code 判斷生成是否完成——執行後應讀取檔案，確認 `CODEGEN_START` marker 沒有殘留。

### 常用選項

| 選項 | 說明 |
|---|---|
| `--dry-run` | 執行腳本、把結果印到 stdout，**不寫回原始檔**；scope 暫存檔（CODEGEN_FILE 等）仍正常讀寫，跨 block 資料交換不受影響 |
| `--strict` | 任何 block 失敗就立刻停止整批 |
| `--no-backup` | 不做備份（預設會備份） |
| `--ext .c,.h` | 只處理這些副檔名 |
| `--env KEY=VAL` | 注入環境變數給腳本 |

### 回滾

```sh
codegen rollback path/to/file.c          # 回到最近一次備份
codegen rollback src/                    # 回滾整個目錄
codegen rollback file.c --list           # 列出所有可用的備份時間點
codegen rollback file.c -t 20260508T075750Z  # 回到指定時間點
```

`--list` 的輸出是純文字，格式為：
```
/absolute/path/to/file.c:
  20260508T075750Z
  20260508T084201Z
```
時間戳格式：`YYYYMMDDTHHMMSSsZ`（UTC，無微秒）。沒有 `--json` 選項。

**重要：不要自行決定執行 rollback。** rollback 可能覆蓋使用者的手動修改。遇到需要 rollback 的情況，必須先告知使用者並取得明確授權再執行。

> 完整 CLI 選項表與更多範例：README.md §「CLI 參考」  
> 備份目錄結構與回滾細節：README.md §「備份與回滾」

---

## 腳本裡可以用的環境變數

執行腳本時，codegen 透過環境變數把一些資訊傳給腳本。在腳本裡用 `os.environ["變數名"]` 就能取得。

| 環境變數 | 值的型態 | 說明 |
|---|---|---|
| `CODEGEN_FILE_PATH` | 字串（路徑） | 正在被處理的原始檔的絕對路徑 |
| `CODEGEN_INVOKE_CWD` | 字串（路徑） | 執行 codegen 時的工作目錄 |
| `CODEGEN_FILE` | 字串（路徑） | 一個暫存 JSON 檔案的路徑（見下方說明） |
| `CODEGEN_GLOBAL` | 字串（路徑） | 另一個暫存 JSON 檔案的路徑（見下方說明） |
| `CODEGEN_BLOCK` | 字串（路徑） | 同一個 block 的多次 pass 間共用的暫存 JSON 路徑（見下方說明） |

### CODEGEN_FILE、CODEGEN_GLOBAL、CODEGEN_BLOCK 是什麼

這三個環境變數的值都是**暫存 JSON 檔案的路徑**。你可以在腳本裡用普通的檔案讀寫（`open()`）存取這些檔案，讀出或寫入任何 JSON 資料。

它們的用途是讓不同 block 之間交換資料，差別在共用範圍（scope）：

- **`CODEGEN_BLOCK`**：只在**同一個 block 的多次 pass** 之間共用，其他 block 看不到。
- **`CODEGEN_FILE`**：同一個原始檔裡的**所有 block** 共用。先執行的 block 寫入，後執行的可以讀到。
- **`CODEGEN_GLOBAL`**：這次執行涉及的**所有檔案**都共用，可以跨檔案傳遞資料。

這是比較進階的功能，一般任務用不到，了解概念就好。如果需要便利的讀寫 API，可以在腳本裡 `import codegen_helper`（它是隨工具安裝的頂層模組，提供 `file_get/set`、`global_get/set` 等函式）。

> 完整的跨 block 資料交換說明與程式碼範例：README.md §「Scope dicts：跨 block 共享狀態」

### 腳本的執行環境

codegen 啟動的子程序**完整繼承**當前 process 的環境變數，包含 `VIRTUAL_ENV`、`PATH` 等 venv 相關設定。若你在 venv 中執行 `codegen`，子程序也在同一個 venv 下執行，`python3` 和 `codegen_helper` 都可以正常使用。

### 在腳本裡讀取專案資源（路徑選擇）

若腳本需要讀取同一個專案下的外部資源（例如 schema JSON 或設定檔），有兩種基準路徑：

- **以專案根目錄為基準**（最常見）：使用 `CODEGEN_INVOKE_CWD`，這是執行 `codegen` 指令時的工作目錄，通常就是專案根目錄：
  ```python
  import os, json
  from pathlib import Path
  root = Path(os.environ["CODEGEN_INVOKE_CWD"])
  data = json.loads((root / "docs/schema.json").read_text())
  ```

- **以當前原始檔為基準**（與本檔案同目錄的資源）：用 `CODEGEN_FILE_PATH` 的父目錄：
  ```python
  import os
  from pathlib import Path
  file_dir = Path(os.environ["CODEGEN_FILE_PATH"]).parent
  data = (file_dir / "config.json").read_text()
  ```

**約定**：專案根目錄的資源用 `CODEGEN_INVOKE_CWD`，與本檔案同目錄的資源用 `CODEGEN_FILE_PATH` 的父目錄。

---

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
