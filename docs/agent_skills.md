# codegen agent_skills.md

這份文件教你兩件事：
- **技能 A**：在檔案的指定位置新增一個 codegen block
- **技能 B**：找到檔案中已存在的 stub block，並把它補全成可執行的腳本

執行前提：你有辦法讀取和修改檔案內容，且環境是 Linux 或 macOS。

---

## 背景：codegen block 是什麼，怎麼運作

### 用途

codegen block 是一種「in-source 程式碼生成」機制。它讓你在原始碼檔案裡，用特殊 marker 框住一段可執行的腳本，之後執行 `codegen` 工具時，工具會跑這段腳本，並把腳本的輸出（stdout）寫回到原始檔的同一個位置，取代整個 block。

**典型使用情境**：

- 一個 C 頭檔需要列出 32 個 register 的 `#define`，與其手寫 32 行，用 block 寫一個 `for` 迴圈就能生成
- 一個設定檔需要依照某個清單生成對應的 key-value，腳本從清單讀取並 print 出結果
- 某段程式碼的內容有規律性，但人工維護容易出錯，用腳本保證正確性

### 運作原理

1. 你在原始檔裡寫好 block（marker + 腳本）
2. 執行 `codegen <檔案路徑>`
3. 工具掃描檔案，找到所有被 marker 包住的 block
4. 對每個 block，把腳本取出來、建立 subprocess 執行它
5. 收集腳本的 stdout，按照 block 在檔案裡的縮排層級對齊
6. 用這段 stdout **取代掉整個 block**（含 marker 兩行），寫回原始檔

執行完之後，原始檔裡的 marker 就消失了，只留下生成的輸出。**如果需要重新生成，必須先還原檔案（`git checkout` 或 `codegen rollback`），再重新跑一次。**

### block 的結構

```
<comment-open> CODEGEN_START
#!/usr/bin/env python3        ← shebang：指定執行腳本用哪個解譯器
# codegen: key=value ...      ← pragma（選填）：調整這個 block 的設定
<腳本內容>                    ← 這段程式碼會被執行，stdout 就是輸出
CODEGEN_END <comment-close>
```

- shebang 決定用哪個直譯器（Python、Shell 等）；省略時預設為 Python
- pragma 是選填的，用來覆寫設定（例如 `auto_indent=false`）
- 腳本只有 stdout 被採用，stderr 不會進入輸出

### 為什麼 block 要包在原始碼的註解裡

因為 block 留在原始檔裡時，必須對該語言的編譯器/直譯器透明。用該語言的註解語法把 marker 和腳本包起來，檔案在生成之前仍然是合法的語法，不影響正常編譯。執行 codegen 後，整個 block 被替換成純輸出，也不留任何多餘的東西。

> 更詳細的 block 格式說明、執行指令、常見錯誤排查：[for_agent.md](for_agent.md)

---

## 準備工作：根據副檔名選對 block 格式

在動手之前，先看檔案的副檔名，決定要用哪種 block 格式。

| 副檔名 | 格式 |
|---|---|
| `.c` `.cpp` `.h` `.hpp` `.rs` `.go` `.java` `.cs` `.swift` `.kt` | `/* CODEGEN_START … CODEGEN_END */` |
| `.py` `.sh` `.bash` `.zsh` `.yaml` `.yml` `.toml` `.rb` `.pl` | `# CODEGEN_START … # CODEGEN_END` |
| `.html` `.xml` `.md` `.markdown` `.svg` | `<!-- CODEGEN_START … CODEGEN_END -->` |
| `.js` `.ts` `.jsx` `.tsx` `.css` `.scss` | `/* CODEGEN_START … CODEGEN_END */` |

**如果不確定，先查專案根目錄的 `codegen.toml` 有無 `[comment_syntax]` 自訂設定；沒有的話預設用 `/* CODEGEN_START … CODEGEN_END */`。**

**限制：`CODEGEN_START` 那一行的前面只能有空白字元，不能有其他程式碼。** 例如 `if (x) { /* CODEGEN_START` 這種寫法會導致 auto-indent 失效。Block 必須另起一行放置。

每種格式的完整模板：

```
/* CODEGEN_START
#!/usr/bin/env python3
<你的腳本>
CODEGEN_END */
```

```
# CODEGEN_START
# #!/usr/bin/env python3
# <你的腳本>
# CODEGEN_END
```

```
<!-- CODEGEN_START
#!/usr/bin/env python3
<你的腳本>
CODEGEN_END -->
```

---

## 技能 A：在指定行號新增 block

### 你會收到什麼

- 檔案路徑（例如 `src/uart.c`）
- 目標行號（例如「第 12 行之後」）
- 要生成什麼的描述（例如「生成 baud rate 的列舉值，包含 9600、19200、115200」）

### 步驟

**步驟 1：讀取檔案，確認目標行號存在。**

讀取整個檔案，確認指定的行號有內容。

**步驟 2：根據副檔名決定 block 格式。**

參考上方的表格。

**步驟 3：寫出完整的 block 文字。**

根據格式模板，把腳本填好。注意：
- shebang 寫 `#!/usr/bin/env python3`（除非你要用別的語言）
- 腳本只用 `print(...)` 輸出，輸出的內容就是最終寫進檔案的文字
- **不要在 print 的字串裡加額外縮排**，工具會自動對齊

範例——在 `src/uart.c` 第 5 行之後插入，生成 baud rate 列舉值：

```c
/* CODEGEN_START
#!/usr/bin/env python3
baud_rates = [9600, 19200, 38400, 57600, 115200]
for b in baud_rates:
    print(f"BAUD_{b},")
CODEGEN_END */
```

**步驟 4：把 block 插入到目標行號之後。**

在第 N 行之後插入，就是把 block 的每一行放在第 N 行後面，原本第 N+1 行開始的內容往後移。

插入時在 block 前後各留一個空白行，讓原始碼可讀性好一點：

```
（第 N 行：原有內容）
（空行）
/* CODEGEN_START
#!/usr/bin/env python3
…
CODEGEN_END */
（空行）
（第 N+1 行：原有內容）
```

**步驟 5：確認寫入完成後，執行 codegen。**

執行前先確認 git 狀態（`git status`）。若工作目錄有未 commit 的修改，提醒使用者——萬一需要 rollback，`git checkout` 會連帶丟棄手動更動。

```sh
codegen src/uart.c
```

如果你不確定輸出是否正確，先用 `--dry-run` 預覽：

```sh
codegen src/uart.c --dry-run
```

**步驟 6：讀取修改後的檔案，確認生成結果正確。**

codegen 執行後，原本的 block 已被輸出取代。重新讀取檔案，確認：
- 生成的內容符合預期（行數、格式、值）
- 縮排層級正確
- 沒有多印或少印
- 沒有 `CODEGEN_START` marker 殘留（exit 0 不代表所有 block 都已展開）

如果使用者的任務描述指定了 formatter（例如 `ruff`、`clang-format`），確認後可執行格式化。未指定時，提醒使用者自行執行 lint 檢查。

---

## 技能 B：補全已存在的 stub block

### 你會收到什麼

情況一：「去第 N 行的 block，把它補全」  
情況二：「去這個檔案的第 X 個 block，把它補全」

### 步驟

**步驟 1：讀取整個檔案。**

**步驟 2：找到目標 block。**

情況一（給了行號）：找到行號正好等於指定行號的 `CODEGEN_START`。如果找不到，往後幾行找最近的一個。不要往前找。  
情況二（給了序號）：從上往下數，找第 X 個 `CODEGEN_START`（從 1 開始數）。

**步驟 3：讀取 block 的現有內容，理解它想生成什麼。**

stub block 通常長這樣——裡面只有一段說明，或是殘缺的腳本：

```c
/* CODEGEN_START
#!/usr/bin/env python3
# TODO: 生成 REG_0 到 REG_7 的 #define
CODEGEN_END */
```

或是完全沒有 shebang，只有文字描述：

```c
/* CODEGEN_START
生成 REG_0 到 REG_7 的 #define，值為 0x00 到 0x07
CODEGEN_END */
```

把這段說明當作你的任務說明，理解需要輸出什麼。

**步驟 4：寫出完整的替換 block。**

保留原有的 marker（`CODEGEN_START` / `CODEGEN_END`），把中間的內容換成真正可以執行的腳本。

以上面的例子為例，替換後：

```c
/* CODEGEN_START
#!/usr/bin/env python3
for i in range(8):
    print(f"#define REG_{i} 0x{i:02X}")
CODEGEN_END */
```

**步驟 5：把原始 block 整段（從 START 那行到 END 那行，含這兩行）替換成新的 block。**

替換範圍：
- 開始：含 `CODEGEN_START` 的那行
- 結束：含 `CODEGEN_END` 的那行
- 這兩行之間的所有內容，全部替換

**步驟 6：確認寫入完成後，執行 codegen。**

執行前先確認 git 狀態（`git status`）。若工作目錄有未 commit 的修改，提醒使用者——萬一需要 rollback，`git checkout` 會連帶丟棄手動更動。

```sh
codegen <檔案路徑>
```

**步驟 7：讀取修改後的檔案，確認生成結果正確。**

重新讀取檔案，確認：
- 生成的內容符合預期
- 縮排層級正確
- 沒有多印或少印
- 沒有 `CODEGEN_START` marker 殘留

如果使用者的任務描述指定了 formatter（例如 `ruff`、`clang-format`），確認後可執行格式化。未指定時，提醒使用者自行執行 lint 檢查。

---

## 寫腳本的規則

這些規則不管是技能 A 還是技能 B 都適用。

### 規則 1：腳本的 stdout 就是輸出內容

只有 `print(...)` 的輸出會寫回檔案。腳本本身的程式碼不會出現在結果裡。

```python
# 正確：print 出你想要的內容
print("int x = 42;")

# 錯誤：你寫的程式碼本身不會出現
int x = 42;  # 這不是 Python，也不會被當輸出
```

### 規則 2：print 字串不要重複加 block 的縮排層級

auto-indent 會自動把 block 本身的縮排補給每一行輸出。所以你的 print 字串裡**不要再加一次 block 的縮排**，否則會縮排兩次。

```python
# 情境：block 在 4 格縮排的函式裡

# 正確——print 字串無縮排，工具自動補 4 格
print("int x = 1;")        # 最終輸出：    int x = 1;

# 錯誤——字串裡已有 4 格，加上工具的 4 格，變成 8 格
print("    int x = 1;")    # 最終輸出：        int x = 1;
```

**但是，輸出內容本身的結構縮排仍需自己加。** 例如生成一個 Python list 時，list item 前面的縮排是內容結構的一部分，你需要自己在 print 字串裡加：

```python
# 情境：block 在第 0 欄（無 block 縮排），生成 Python list

print("SUPPORTED_LANGS = [")
for lang in langs:
    print(f'    "{lang}",')   # 這個 4 格是 list 元素的結構縮排，需要自己加
print("]")
```

判斷標準：**這個縮排是「block 放在哪裡」的縮排嗎？** 是的話不要加（工具補）；不是的話（是輸出內容的層次結構），你要自己加。

### 規則 3：shebang 決定用哪個解譯器

- Python：`#!/usr/bin/env python3`
- Shell：`#!/bin/sh` 或 `#!/usr/bin/env bash`
- 省略 shebang：預設視為 Python

### 規則 4：line-style 格式（`#`）的腳本每行要加 `#` 前綴

`.py`、`.sh` 等檔案用 `#` 格式，block 裡每行（包含腳本）都要以 `# ` 開頭：

```python
# CODEGEN_START
# #!/usr/bin/env python3
# for i in range(3):
#     print(f"x{i} = {i}")
# CODEGEN_END
```

執行時工具會自動把每行的 `# ` 前綴去掉，再執行腳本。

腳本裡的空行可以寫成 `#`（只有井字號，建議寫法）、`# `（加空格），或完全空行——三種都不會讓 parser 誤判成 block 結束。

### 規則 5：CODEGEN_START 必須獨占一行，前面只能有空白

`CODEGEN_START` 那一行前面只能有空格/tab，不能有其他程式碼。把 block 放在程式碼的行尾（例如 `if (x) { /* CODEGEN_START`）會導致 auto-indent 失效。Block 必須另起一行放置。

### 規則 6：不要在 block 裡面再出現 marker 字串

如果你的腳本需要輸出包含 `CODEGEN_START` 的文字，請改用變數或字串拼接，避免 parser 混淆：

```python
# 不好：直接寫 marker
print("/* CODEGEN_START")

# 好：用變數避免直接出現
start = "CODEGEN" + "_START"
print(f"/* {start}")
```

### 規則 7：腳本只能用 Python 標準庫和 codegen_helper

不要在 block 腳本裡 import 第三方套件（例如 `requests`、`pandas`、`yaml`）。只有 Python 標準庫和 `codegen_helper` 是保證可用的。若任務需要第三方套件，向使用者回報，請使用者確認安裝後再執行，**不要自行執行 `pip install`**。

### 規則 8：絕不把敏感資訊輸出到 stdout

腳本的 stdout 會直接寫進原始碼。如果任務需要 API Key 或密碼等敏感資訊，必須透過 `--env KEY=VAL` 由外部注入，在腳本裡用 `os.environ["KEY"]` 讀取，**絕對不能 print 出來**。  
如果需要 secret，告訴使用者需要用 `--env` 帶入哪個變數名稱，再由使用者執行。不要去讀取 `.env` 檔。

---

## 跨檔案生成的執行順序

如果多個檔案的生成有依賴關係（例如 Block A 寫入 CODEGEN_GLOBAL，Block B 讀取），**不要用 `codegen src/` 一次處理整個目錄**——處理順序是字典序，不保證符合你的依賴關係。

應按依賴順序，逐檔執行：

```sh
codegen src/constants.h   # 先執行：寫入 CODEGEN_GLOBAL
codegen src/main.c        # 再執行：讀取 CODEGEN_GLOBAL
```

---

## 完整範例

### 範例 A：在 C 檔案第 10 行後新增 block

任務：「在 `src/regs.h` 第 10 行之後，新增一個 block，生成 8 個 register 的 `#define`」

讀取檔案後確認第 10 行，選擇 `/* */` 格式（`.h` 檔案），寫入：

```c
/* CODEGEN_START
#!/usr/bin/env python3
for i in range(8):
    print(f"#define REG_{i}  0x{i:02X}")
CODEGEN_END */
```

插入第 10 行後，存檔，執行：

```sh
codegen src/regs.h
```

---

### 範例 B：補全 Python 檔案的第 2 個 stub block

任務：「去 `config.py` 的第 2 個 block，補全它——那個 block 說要生成所有支援的語言代碼清單」

讀取 `config.py`，從上往下找到第 2 個 `# CODEGEN_START`，看到 stub：

```python
# CODEGEN_START
# 生成支援語言的清單：zh-TW, en-US, ja-JP, ko-KR
# CODEGEN_END
```

替換成完整腳本（注意每行加 `# ` 前綴）：

```python
# CODEGEN_START
# #!/usr/bin/env python3
# langs = ["zh-TW", "en-US", "ja-JP", "ko-KR"]
# print("SUPPORTED_LANGS = [")
# for lang in langs:
#     print(f'    "{lang}",')
# print("]")
# CODEGEN_END
```

存檔，執行：

```sh
codegen config.py
```

---

## 出錯時怎麼辦

### 執行後什麼都沒生成（block 消失但沒有輸出）

腳本沒有任何 print。回去看腳本，確認有 `print(...)` 輸出。

### 執行後提示「沒有找到 block」

Block 可能已經被執行過一次（marker 消失了）。

**重要：不要自行決定執行 rollback。** rollback 可能覆蓋使用者的手動修改，必須先向使用者說明情況並取得明確授權再執行：
```sh
codegen rollback <檔案路徑>
```
或請使用者自行執行：
```sh
git checkout <檔案路徑>
```

### Block marker 語法有誤（例如缺少 `*/`）

若 stub block 的 marker 明顯有格式錯誤，可嘗試推斷正確格式並修正，但**修正後必須向使用者確認再繼續執行 codegen**，不得自行假設修正正確。

### 腳本語法錯誤

加 `--strict` 看完整錯誤訊息：
```sh
codegen <檔案路徑> --strict
```

### 部分 block 成功、部分失敗（混合狀態）

預設行為（`on_error=continue`）：前面成功的 block 已替換（marker 消失），失敗的 block 保留原 marker，**檔案仍然被寫回**，造成混合狀態。

處理方式：
1. 先 rollback（需向使用者取得授權）：`codegen rollback <檔案路徑>`
2. 修正失敗的 block 腳本後重新執行

若需要「全部成功才寫回」，下次可加 `--strict` 或在 pragma 設定 `on_error=abort_file`。  
注意：**不要只修正失敗的 block 然後重跑**——成功的 block 已無 marker，不會再被執行；只有殘留 marker 的 block 才會被執行。

### 縮排錯亂

確認你的 print 輸出沒有多餘縮排。如果你真的不需要自動縮排，在 block 加 pragma：

```c
/* CODEGEN_START
#!/usr/bin/env python3
# codegen: auto_indent=false
print("    already_indented_content")
CODEGEN_END */
```
