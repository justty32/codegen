# 寫腳本的規則

← [agent_skills/README](README.md)

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
