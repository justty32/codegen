# for_agent.md — codegen 的 AI Agent 指南

這份文件服務**兩種** AI agent，請依你的任務看對應的部分：

- **Part 一 · 使用 codegen** — 你要在某個專案裡**寫 / 跑** codegen block（不改工具本身）。
- **Part 二 · 開發 codegen** — 你要**修改 codegen 這個工具的原始碼**（新增功能、修 bug）。

相關文件：
- `README.md` — 完整使用手冊與設定欄位（對外行為的權威來源，等同 spec）
- `agent_skills.md` — 操作手冊：在指定位置新增 block、把 stub block 補全成可執行腳本
- `intro.md` — 一頁速覽
- `DESIGN.md` — 設計規格（§1~§13）
- `architecture.md` — 實作面分解（模組劃分、資料結構、流程；部分已漂移，見 Part 二 §8）

---
---

# Part 一 · 使用 codegen（for agents *using* the tool）

> 對象：要在某個專案裡寫 / 執行 codegen block 的 agent。只想知道「怎麼用」看這部分就夠了。

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

---
---

# Part 二 · 開發 codegen（for agents *modifying* the tool）

> 對象：要來修改 / 擴充 codegen 這個工具本身原始碼的 agent。這是「動手改 code 之前該先知道的事」。
> 想知道「工具對外怎麼用」請看上面的 Part 一 與 `README.md`。

## 0. TL;DR（30 秒版）

- **這是什麼**：跨語言的 in-source 程式碼生成工具。在原始檔裡用註解區塊（`/* CODEGEN_START ... CODEGEN_END */`）包一段腳本，`codegen` 跑它、把 stdout 寫回原位。**POSIX only**（依賴 shebang / `chmod +x` / SIGINT）。
- **語言/版本**：純 Python，`requires-python >= 3.11`（本機 venv 是 3.14）。**執行期零第三方依賴**，dev 只用 `pytest`。
- **跑測試**：在 repo 根目錄執行 `.venv/bin/python -m pytest -q`（目前 **113 passed**，~0.25s）。
- **跑 CLI（未安裝時）**：`PYTHONPATH=src .venv/bin/python -m codegen --help`。
- **改動前必讀**：本部分 §5「不可破壞的不變量」與 §8「雷區」。

---

## 1. 文件地圖與「真實來源」優先序

這個 repo 的說明文件職責不同，**可信度也不同**：

| 文件 | 角色 | 對 agent 的意義 |
|---|---|---|
| `README.md` | 使用者手冊（CLI、設定、block 格式、helper API） | 想知道「對外行為應該長怎樣」時看這裡，等同 spec |
| `DESIGN.md` | 設計規格，章節以 §1~§13 編號 | 設計意圖與邊界規則的權威來源；`architecture.md` 的 §X 都指向它 |
| `architecture.md` | 實作面分解（模組劃分、資料結構、流程） | 理解內部結構的最佳起點，**但部分已漂移**，見 §8 |
| `for_agent.md`（本檔） | Part 一＝給「使用者 agent」的手冊；Part 二＝本開發指南 | 上手與紀律 |
| `agent_skills.md` | 給「使用者 agent」的操作 playbook（新增 / 補全 block） | 偏實作步驟，不是工具開發文件 |
| `intro.md` | 一頁速覽 | 快速理解工具定位 |

> **黃金規則**：當文件與程式碼衝突時，**以程式碼為準**，並順手修正文件（或在 PR 說明裡指出漂移）。`architecture.md` 的測試/目錄章節（§16、§17）已經和實際不符，別照抄。

---

## 2. 環境與常用指令

本機已有 venv 在 `.venv/`（Python 3.14）。套件**沒有**安裝進 venv；測試能跑是因為 `pyproject.toml` 設了 `pythonpath = ["src"]`。

```sh
# 跑全部測試（最常用）
.venv/bin/python -m pytest -q

# 跑單一檔 / 單一測試
.venv/bin/python -m pytest tests/unit/test_parser.py -q
.venv/bin/python -m pytest tests/integration/test_pipeline_basic.py -q -k single_block

# 不靠安裝直接跑 CLI
PYTHONPATH=src .venv/bin/python -m codegen run <path> [options]
PYTHONPATH=src .venv/bin/python -m codegen rollback <path> --list

# 要驗證「使用者真的 pip install 後」的行為（含 codegen_helper 能被腳本 import）
.venv/bin/pip install -e .
```

> **注意 `codegen_helper`**：`src/codegen/env.py` 的 `build_env()` **不會**自動把 `src/` 注入 subprocess 的 `PYTHONPATH`。所以 block 腳本裡的 `import codegen_helper` 只有在「codegen 已被 pip install」時才一定可用。寫整合測試若要測 helper，請改用 `pip install -e .`，或自行在 env 注入 `PYTHONPATH`，別假設它天生可用。

---

## 3. 程式碼地圖（以實際檔案為準）

單一扁平 package `src/codegen/`，外加一個 top-level 的 `src/codegen_helper.py`。

| 檔案 | 行數量級 | 職責 | 主要對外介面 |
|---|---|---|---|
| `cli.py` | ~195 | argparse、子命令分派、SIGINT handler | `main(argv)` |
| `config.py` | ~300 | `Config` (frozen dataclass) + 多層覆寫解析 | `resolve_initial()`, `merge_*_pragma()`, `FIELD_NAMES` |
| `comment_syntax.py` | ~108 | 副檔名 → 註解語法表、line/block 風格 | `lookup(ext)`, `CommentSyntax` |
| `scanner.py` | ~74 | 目標展開、extensions/include/exclude 過濾 | `collect_files()` |
| `parser.py` | ~258 | marker/pragma/shebang 切分、找 top-level blocks | `parse_file()`, `parse_block_header()` |
| `expander.py` | ~275 | 單一 block 多輪展開（含嵌套） | `process_content()`, `expand_block()` |
| `executor.py` | ~91 | spawn subprocess、組 env、timeout、收 stdout/stderr | `run_block()` |
| `env.py` | ~46 | 組 subprocess 環境變數 | `build_env()`, `RunContext` |
| `scope.py` | ~83 | 三層 scope JSON 的生命週期 + snapshot/restore | `ScopeStore` |
| `indent.py` | ~26 | `auto_indent` 計算與套用 | `apply_indent()` |
| `backup.py` | ~55 | 執行前備份 | `snapshot_file()` |
| `rollback.py` | ~89 | `codegen rollback` 子命令 | `run()` |
| `pipeline.py` | ~154 | 單檔 in-memory 處理流程，串起各模組 | `process_file()` |
| `progress.py` | ~53 | 計畫列印、SIGINT/abort_all 中斷回報 | `print_plan()`, `report_interrupt()`, `RunState` |
| `errors.py` | ~48 | Exit code 常數、exception 階層 | `CodegenError`, `ConfigError`, `BlockFailure`, `EXIT_*` |
| `helper.py` | 8 | **內部別名**，`from codegen_helper import *` | — |
| `__main__.py` | 3 | `python -m codegen` 入口 | — |
| `../codegen_helper.py` | ~122 | **使用者腳本用的公開 helper**（`import codegen_helper as cg`） | `global_*`, `file_*`, `block_*`, `origin_*`, `targets`, `invoke_cwd`, `file_path` |

> **helper 的雙檔結構（容易搞混）**：公開 API 的本體在 **`src/codegen_helper.py`**（top-level 模組，pyproject 透過 `force-include` 把它裝成頂層可 import）。`src/codegen/helper.py` 只是 `from codegen_helper import *` 的轉出別名。**要改 helper 行為，改 `src/codegen_helper.py`。**

---

## 4. 執行流程（鳥瞰）

單執行緒、由上至下。掃到的檔案逐個處理，檔案內 block 由上至下。

```
cli.main()
 ├─ rollback → rollback.run(...)
 └─ run（預設子命令）
     1. config.resolve_initial(cli_args, env)   # defaults → env → folder toml → CLI
     2. scanner.collect_files(targets, cfg)      # extensions / include / exclude / scan_all
     3. progress.print_plan(targets)
     4. 逐檔 pipeline.process_file():
        ├─ 讀檔 → file pragma → merge 出 file_config
        ├─ backup.snapshot_file()                # backup=true 時
        ├─ scope.open_file() → 寫 $CODEGEN_FILE
        ├─ expander.process_content():
        │    每個 top-level block → expand_block()
        │      └─ 多輪：executor.run_block() → indent.apply_indent() → 替換切片
        └─ 全部成功後「一次性」把最終內容寫回 disk
        └─ 依 on_error 決定是否中止後續
     5. 依結果 exit（見 §6 exit codes）
```

更細的流程與虛擬碼在 `architecture.md` §1、§7；設計理由在 `DESIGN.md` §6。

---

## 5. 不可破壞的不變量（改動前務必確認）

這些是設計保證，破壞它們等於改壞工具。動到相關模組時要特別小心：

1. **單執行緒、由上至下**。檔案依字典序、block 依出現順序。**不要引入平行化**（`DESIGN.md` §6.1 明確保證；後面 block 依賴前面 block 寫入 scope 的值）。
2. **一次性寫回**。單檔的所有 block 在記憶體內處理完才一次寫回 disk（`pipeline.process_file`）。不要邊跑邊寫。
3. **一次性消耗**。跑完 marker 就消失，無法重跑生成；要重來靠 `git checkout`。輸出本身不應再含原 marker（除非 `keep_as_comment`）。
4. **`Config` 是 frozen dataclass**。覆寫鏈是「逐層 merge 出新 Config」，不是 mutable bag。新增設定欄位時：欄位加進 `Config` + 補進 `FIELD_NAMES` + 對齊 CLI/env/toml/pragma 的命名（見 §7）。
5. **stdout 結尾換行原樣保留**，不 strip 也不補（`DESIGN.md` §2）。
6. **stderr 成功時吞掉、失敗時才印**（`executor.run_block` 收集，由頂層 §10.4 診斷輸出統一印）。
7. **scope snapshot/restore 用 stack 配對**。每個 block 進入 `snapshot()`、成功 `commit()`、失敗 `restore()`。巢狀展開要正確 push/pop（`scope.py`）。block 失敗 → 三層 scope 必須回滾到該 block 執行前。
8. **子 block 失敗 = 父 block 失敗**（嵌套冒泡）。`executor` 拋 `BlockFailure`，expander 讓它自然往上冒，不要吞掉。
9. **掃描相關設定（`extensions`/`include`/`exclude`/`scan_all`）只能來自 CLI/toml/env/defaults**。出現在 file/per-block pragma 要報 `ConfigError`。
10. **POSIX only**。可以用 shebang、`chmod +x`、SIGINT、`os.replace`；不要為 Windows 加相容碼。測試對 spawn 子程序的部分用 `@pytest.mark.skipif(sys.platform == "win32")`。

---

## 6. Exit codes（`errors.py`）

| Code | 常數 | 意義 |
|---|---|---|
| 0 | `EXIT_OK` | 全部成功 |
| 1 | `EXIT_BLOCK_FAILURE` | 至少一個 block 失敗（`on_error=continue` 跑完） |
| 2 | `EXIT_ABORT_ALL` | `abort_all` 觸發的整批中止 |
| 3 | `EXIT_STARTUP` | 啟動前設定/參數錯誤（`ConfigError`） |
| 130 | `EXIT_SIGINT` | SIGINT (Ctrl+C) |

改錯誤處理流程時，務必讓這張表跟 `README.md` 的「Exit codes」一致。注意 Part 一提到的「達到 `max_passes` 上限只印 warning、exit 仍為 0」也是刻意行為，別改成報錯。

---

## 7. 設定系統（最容易改錯的地方）

同一個設定在不同層用不同命名風格，**必須一一對應**：

| 層 | 命名風格 | 範例 |
|---|---|---|
| `Config` 欄位 / TOML key / pragma key | `snake_case` | `max_pass_time` |
| CLI flag | `kebab-case`（argparse `dest=` 對齊回 snake_case） | `--max-pass-time` |
| 環境變數 | `CODEGEN_` + 大寫 | `CODEGEN_MAX_PASS_TIME` |

覆寫優先序（高 → 低）：**per-block pragma > CLI > file pragma > folder `codegen.toml` > 環境變數 > 內建預設**。

**新增一個設定欄位的清單**（缺一就會行為不一致）：
1. `config.py`：`Config` 加欄位、預設值、加進 `FIELD_NAMES`、處理 env/toml/pragma 解析與型別轉換。
2. `cli.py`：加 argparse flag（`dest` 對齊 snake_case）。
3. 實際讀取它的模組（如 expander/executor/indent…）。
4. `README.md`：CLI 表、`codegen.toml` 表、環境變數表、pragma key 清單都要補。
5. 測試：`tests/unit/test_config.py` 加覆寫優先序的 case。

pragma 語法限制（`parser.parse_pragma_line`）：`key=value` 以空格分隔、**value 不能含空格、不能有引號**、bool 寫 `true`/`false`、list 用逗號（如 `markers=A,B`）。未知 key / 缺等號 / 空 value → `ConfigError`。

---

## 8. 已知的文件漂移 & 雷區

動手前先知道這些，省得照著過時文件改錯：

- **`architecture.md` §16/§17 的測試與目錄結構已過時**。它列了 `test_pragma.py`、`test_rollback.py`、`fixtures/` 等，但**實際不存在**。實際測試見 §9。
- **`architecture.md` §15/§16 說 helper 是 `helper.py`**；實際公開本體是 top-level 的 `src/codegen_helper.py`，`codegen/helper.py` 只是別名（見 §3）。
- **`codegen_helper` 在 subprocess 不保證可 import**（`build_env` 不注入 `PYTHONPATH`，見 §2）。要靠 `pip install` 才穩。
- **pragma 在 block-style 註解內的前綴判定**看「shebang 指向的腳本語言」的註解符，不是檔案語言的（`architecture.md` §6.2）。改 parser 時別搞混兩種註解語境。
- **第 1 輪展開的特例**：expander 把「執行原 block」和「展開 stdout 內的新 block」用同一段迴圈邏輯處理（region 初值=整個 block 原文）。改 `expand_block` 時讀懂 `architecture.md` §7 再動。

---

## 9. 測試慣例

```
tests/
├── unit/                         # 純函式，快
│   ├── test_config.py            # 多層覆寫優先序
│   ├── test_parser.py            # marker/pragma/shebang 切分
│   ├── test_comment_syntax.py
│   ├── test_scanner.py           # extensions/include/exclude/scan_all
│   ├── test_indent.py
│   ├── test_scope.py             # snapshot/restore 配對
│   ├── test_expander.py          # 多輪 / 嵌套 / 三種 timeout
│   ├── test_backup.py
│   └── test_cli.py
└── integration/
    └── test_pipeline_basic.py    # 端對端 spawn 真子程序，斷言檔案最終內容（POSIX-only）
```

慣例：
- 沒有 `conftest.py`；靠 `pyproject.toml` 的 `pythonpath = ["src"]` 解 import。
- 整合測試會真的 spawn subprocess，請維持 `@pytest.mark.skipif(sys.platform == "win32")`（檔案頂部已有 `POSIX` marker 可重用）。
- 用 `tmp_path` fixture 建臨時檔，**不要**在 repo 內留產物或 `.codegen-backup/`。
- **改任何行為都要附測試**。先確認 `113 passed` 的基線沒被你弄回歸。

關鍵情境（新功能盡量涵蓋）：單/多輪展開、子 block 失敗→父失敗+scope 回滾、三種 timeout、file+per-block pragma 同時存在的優先級、`auto_indent` on/off、`keep_as_comment` on/off、掃描過濾、backup↔rollback 來回、SIGINT 當前 block 視為失敗但已完成的保留、`abort_all` 的 exit code 與輸出。

---

## 10. 改動紀律（PR 前檢查清單）

1. `.venv/bin/python -m pytest -q` 全綠（基線 113 passed）。
2. 改了對外行為 → 同步更新 `README.md`（CLI/設定/exit code 三處表格容易漏）與本檔 Part 一（若影響使用者 agent 的用法）。
3. 改了內部結構 → 順手修正 `architecture.md`（特別是已漂移的章節），別讓漂移擴大。
4. 沒有破壞 §5 的任何不變量。
5. 維持風格：純標準庫、type hints（`from __future__ import annotations`）、frozen dataclass、扁平 package、不引入第三方執行期依賴。
6. 沒留下臨時檔 / `.codegen-backup/`（已在 `.gitignore`）。
7. commit message 用祈使句、聚焦單一改動（參考 `git log`：`Fix ...` / `Propagate ...` / `Exclude ...`）。

---

## 11. 明確不做的事（`DESIGN.md` §13 / `architecture.md` §18）

除非使用者明確要求，**不要**自作主張加上：

- Watch 模式
- 平行化（與 §5.1 的單執行緒保證衝突，明確不做）
- Windows 支援（明確不做）
- 結構化 logging / `--verbose` / `--quiet`
- 非 Python 語言版的 helper（Ruby/JS 版 codegen_helper）
- 對特定 glob 套用不同 markers

要做這些之前先跟使用者確認，並更新 `DESIGN.md`。

---

## 12. 一句話定位每個模組（給快速導航）

> 想找「**X 在哪做**」時用：

- marker/註解語法怎麼切 → `parser.py` + `comment_syntax.py`
- 設定怎麼合併、優先序 → `config.py`
- 哪些檔會被處理 → `scanner.py`
- 怎麼跑腳本、timeout、收輸出 → `executor.py`（環境變數在 `env.py`）
- 多輪/嵌套展開邏輯 → `expander.py`
- 跨 block 共享狀態、失敗回滾 → `scope.py`
- 輸出縮排 → `indent.py`
- 備份/回滾 → `backup.py` / `rollback.py`
- 串整個單檔流程 → `pipeline.py`
- CLI/參數/SIGINT → `cli.py`
- 進度與中斷報告 → `progress.py`
- 給使用者腳本的 API → `src/codegen_helper.py`
