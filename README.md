# codegen

跨語言的 in-source 程式碼生成工具。在原始檔中以特殊註解區塊定義生成腳本，執行後把腳本的 stdout 寫回檔案原位。

**平台：POSIX only（Linux / macOS）。不支援 Windows。**

---

## 目錄

1. [安裝](#安裝)
2. [快速上手](#快速上手)
3. [Block 格式](#block-格式)
4. [設定方式（多層覆寫）](#設定方式多層覆寫)
5. [CLI 參考](#cli-參考)
6. [Scope dicts：跨 block 共享狀態](#scope-dicts跨-block-共享狀態)
7. [嵌套展開（Nested Expansion）](#嵌套展開nested-expansion)
8. [備份與回滾](#備份與回滾)
9. [codegen_helper：Python 腳本便利 API](#codegen_helperPython-腳本便利-api)
10. [錯誤處理](#錯誤處理)

---

## 安裝

```sh
pip install .
```

安裝後 `codegen` 指令即可使用：

```sh
codegen --help
```

---

## 快速上手

在 C 檔案裡，用 `/* CODEGEN_START ... CODEGEN_END */` 包住一段生成腳本：

```c
// uart.c

/* CODEGEN_START
#!/usr/bin/env python3
baud_rates = [9600, 19200, 38400, 57600, 115200]
for b in baud_rates:
    print(f"    BAUD_{b},")
CODEGEN_END */

int main(void) { return 0; }
```

執行：

```sh
codegen uart.c
```

執行後，`uart.c` 變成：

```c
// uart.c

    BAUD_9600,
    BAUD_19200,
    BAUD_38400,
    BAUD_57600,
    BAUD_115200,

int main(void) { return 0; }
```

**注意**：codegen 是一次性的——跑完後 marker 就消失了，再跑一次找不到 block、不會重新生成。要重新生成請先 `git checkout` 還原再跑。

---

## Block 格式

### 基本格式

```
<comment-open> CODEGEN_START
#!/usr/bin/env <interpreter>
<腳本內容>
CODEGEN_END <comment-close>
```

Block 的第一行是 shebang，指定解譯器。任何可以用 shebang 啟動的程式都能用（Python、Shell、Ruby、Node…）。

### 各語言對應的寫法

**C / C++ / Rust / Go（`/* */`）：**

```c
/* CODEGEN_START
#!/usr/bin/env python3
for i in range(4):
    print(f"    REG_{i} = {i},")
CODEGEN_END */
```

**Python / Shell / YAML（`#`）：**

```python
# CODEGEN_START
# #!/usr/bin/env python3
# print("GENERATED = True")
# CODEGEN_END
```

**HTML / XML / Markdown（`<!-- -->`）：**

```html
<!-- CODEGEN_START
#!/usr/bin/env python3
print("<li>generated</li>")
CODEGEN_END -->
```

### 省略 shebang

若第一行不是 `#!` 開頭，整個 block 視為 Python 腳本（隱含 `#!/usr/bin/env python3`）：

```c
/* CODEGEN_START
for i in range(3):
    print(f"int x{i};")
CODEGEN_END */
```

### Per-block pragma

在 shebang 後面加 `# codegen: key=value ...` 可針對這個 block 調整行為：

```c
/* CODEGEN_START
#!/usr/bin/env python3
# codegen: max_passes=3 on_error=abort_all
# ... 需要嵌套展開的腳本 ...
CODEGEN_END */
```

---

## 設定方式（多層覆寫）

優先順序由高至低：

| 層級 | 形式 |
|---|---|
| Per-block pragma | block 內的 `# codegen: ...` |
| CLI 參數 | `--max-passes 3` |
| File pragma | 檔案頂部的 `/* codegen: ... */` |
| Folder config | `codegen.toml` |
| 環境變數 | `CODEGEN_MAX_PASSES=3` |
| 內建預設 | — |

### codegen.toml

從目標目錄往上找（到 git root 為止）。完整範例：

```toml
# Marker（預設 CODEGEN_START / CODEGEN_END）
markers = ["CODEGEN_START", "CODEGEN_END"]  # 識別 block 起止的標記字串，可自訂以避免與現有程式碼字串衝突

# 掃描設定
extensions = [".c", ".h", ".cpp"]           # 掃描目錄時只處理這些副檔名；CLI 直接指定檔案路徑時不套用此過濾
include = ["tools/**/*.gen"]                # 額外納入的 glob，疊加在 extensions 之上（副檔名不在清單也會被掃到）
exclude = ["**/build/**", "**/.venv/**"]    # 排除的 glob，優先於 include 與 extensions
scan_all = false                            # true 時忽略 extensions、掃描目錄下所有檔案（仍套用 exclude）

# 自訂副檔名 → 註解語法
[comment_syntax]
".pyx" = "#"     # Cython：用 # 行註解包住 marker
".kts" = "/* */" # Kotlin Script：用 /* */ 區塊註解包住 marker

# 展開上限
max_passes = 1         # 單一 block 最多展開幾輪；預設 1 表示不允許嵌套，需要嵌套時調高
max_total_time = 5.0   # 單一 block 從第一輪到穩定的累積時間上限（秒），防止無限嵌套
max_pass_time = 5.0    # 單次 subprocess 執行時間上限（秒），超過即強制終止整個進程組
                       #（含腳本自己背景化的子進程，不會留下孤兒進程）

# 輸出
keep_as_comment = false  # true 時把原始 block 以該語言的行註解保留在輸出上方（僅供參考，不會再被識別執行）
auto_indent = true       # true 時自動把 stdout 每行對齊到原 block 在檔案中的縮排層級

# 備份
backup = true                   # true 時執行前自動備份所有被處理的檔案
backup_dir = ".codegen-backup"  # 備份根目錄路徑（建議加入 .gitignore）

# 錯誤處理
# continue：跳過失敗 block，繼續後面的 block 與其他檔案
# abort_file：停止當前檔案剩餘 block，繼續下一個檔案
# abort_all：立即中止整個批次（等同 CLI --strict）
on_error = "continue"

# Subprocess 的 working directory（預設為執行 codegen 時的 cwd）
cwd = "."  # 相對路徑以執行 codegen 時的 cwd 為基準；可讓腳本以固定路徑存取專案資源

# 降權執行：以指定使用者身份跑 codegen 片段（名稱或 uid）
# 需要 codegen 本身有權限（通常以 root 啟動再降權）。屬安全防呆而非完整沙箱。
# run_as_user = "nobody"

# 注入給腳本的環境變數
[env]
PROJECT_ROOT = "/path/to/project"  # 傳給所有 subprocess 的自訂環境變數，可在腳本內透過 os.environ 取用
```

### 欄位說明

| 欄位 | 型別 | 預設值 | 說明 |
|---|---|---|---|
| `markers` | `[string, string]` | `["CODEGEN_START", "CODEGEN_END"]` | Block 起止 marker |
| `extensions` | `[string, ...]` | 內建清單 | 掃描目錄時的副檔名白名單 |
| `include` | `[string, ...]` | — | 額外納入的 glob（優先於 extensions） |
| `exclude` | `[string, ...]` | — | 排除的 glob |
| `scan_all` | `bool` | `false` | 忽略副檔名，掃描所有檔案 |
| `[comment_syntax]` | table | — | 自訂副檔名 → 註解語法（`"#"` / `"/* */"` / `"<!-- -->"` / `"// "` 等） |
| `max_passes` | `int` | `1` | 單一 block 最大展開輪數 |
| `max_total_time` | `float` | `5.0` | Block 整體展開時間上限（秒） |
| `max_pass_time` | `float` | `5.0` | 單次 subprocess 執行時間上限（秒） |
| `keep_as_comment` | `bool` | `false` | 將原始 block 保留為上方註解 |
| `auto_indent` | `bool` | `true` | 自動對齊輸出縮排 |
| `backup` | `bool` | `true` | 執行前備份處理到的檔案 |
| `backup_dir` | `string` | `".codegen-backup"` | 備份根目錄路徑 |
| `on_error` | `string` | `"continue"` | 錯誤處理模式：`continue` / `abort_file` / `abort_all` |
| `cwd` | `string` | — | subprocess 工作目錄（預設為執行 codegen 時的 cwd） |
| `run_as_user` | `string` | — | 以指定使用者（名稱或 uid）降權執行 block；需 root 權限。**不可**透過 pragma 設定 |
| `[env]` | table | — | 注入給所有 subprocess 的環境變數 |

### File pragma

在**檔案最頂部的第一個普通註解**（非 CODEGEN block，不含 marker）中寫入 `codegen: ...`：

- **block-style 語言**（`/* */`、`<!-- -->`）：檔案最頂部的第一段區塊註解
- **line-style 語言**（`#`、`//`）：檔案第一行起連續的行註解區塊

只要該註解內有 `codegen:` 行就會被識別為 file pragma，否則視為一般註解忽略。

```c
/* codegen: markers=<<<,>>> */
```

```python
# codegen: max_passes=2
```

### Per-block pragma

```c
/* CODEGEN_START
#!/usr/bin/env python3
# codegen: backup=false keep_as_comment=true max_passes=5 on_error=abort_all
print("generated")
CODEGEN_END */
```

可設定的 key：`markers`、`max_passes`、`max_total_time`、`max_pass_time`、`keep_as_comment`、`auto_indent`、`backup`、`on_error`、`cwd`。

**語法規則**：
- `key=value` 以空格分隔，不能有引號
- `=` 前後**不可有空格**（`key = value` 無效，會報錯）
- 布林值寫 `true` / `false`
- list 以逗號分隔（如 `markers=<<<,>>>`）

### 環境變數

慣例：`CODEGEN_<設定名大寫>=值`。值格式與 pragma 相同（逗號分隔 list、`true`/`false` 布林）。

這些環境變數在 block 腳本執行時也可透過 `os.environ`（或各語言等效方式）讀取，因為 subprocess 繼承了啟動 codegen 時的完整環境。

| 環境變數 | 說明 | 預設值 |
|---|---|---|
| `CODEGEN_MARKERS` | 自訂 marker，逗號分隔 | `CODEGEN_START,CODEGEN_END` |
| `CODEGEN_EXTENSIONS` | 副檔名白名單，逗號分隔 | 內建清單 |
| `CODEGEN_INCLUDE` | 額外納入的 glob，逗號分隔 | — |
| `CODEGEN_EXCLUDE` | 排除的 glob，逗號分隔 | — |
| `CODEGEN_SCAN_ALL` | 掃描所有檔案 | `false` |
| `CODEGEN_MAX_PASSES` | 最大展開輪數 | `1` |
| `CODEGEN_MAX_TOTAL_TIME` | 整體展開時間上限（秒） | `5.0` |
| `CODEGEN_MAX_PASS_TIME` | 單次 subprocess 時間上限（秒） | `5.0` |
| `CODEGEN_KEEP_AS_COMMENT` | 保留原始 block 為上方註解 | `false` |
| `CODEGEN_AUTO_INDENT` | 自動對齊輸出縮排 | `true` |
| `CODEGEN_BACKUP` | 執行前備份 | `true` |
| `CODEGEN_BACKUP_DIR` | 備份根目錄路徑 | `.codegen-backup` |
| `CODEGEN_ON_ERROR` | 錯誤處理模式（`continue` / `abort_file` / `abort_all`） | `continue` |
| `CODEGEN_CWD` | subprocess 工作目錄 | — |

---

## CLI 參考

```
codegen [run] [paths...] [options]
codegen rollback [paths...] [options]
```

`run` 是預設子指令，可省略。

### codegen run

**常用選項**

| 選項 | 說明 |
|---|---|
| `paths...` | 要處理的檔案或目錄（可多個，預設為 `.`） |
| `--dry-run` | 執行腳本但結果不寫回 disk，改印到 stdout |
| `--strict` | 任何 block 失敗即中止整批（等同 `--on-error abort_all`） |
| `--no-backup` | 關閉備份 |
| `--ext <list>` | 覆寫副檔名白名單（逗號分隔，如 `.c,.h,.cpp`） |
| `--exclude <glob>` | 排除的 glob，可重複使用 |
| `--env KEY=VAL` | 注入給 subprocess 的環境變數，可重複使用 |

**進階選項**

| 選項 | 說明 |
|---|---|
| `--config <path>` | 顯式指定 codegen.toml，跳過自動向上搜尋 |
| `--markers <start,end>` | 覆寫 marker（例：`--markers <<<,>>>`） |
| `--keep-as-comment` | 把原始 block 保留為上方註解 |
| `--auto-indent` | 開啟自動對齊（預設已開） |
| `--no-auto-indent` | 關閉自動對齊 |
| `--backup-dir <path>` | 指定備份根目錄 |
| `--all` | 忽略副檔名清單，掃描所有檔案 |
| `--include <glob>` | 額外納入的 glob，可重複使用 |
| `--on-error <mode>` | `continue` \| `abort_file` \| `abort_all` |
| `--max-passes <n>` | 單一 block 最大展開輪數（預設 `1`） |
| `--max-total-time <sec>` | Block 整體展開時間上限（預設 `5.0`） |
| `--max-pass-time <sec>` | 單次 subprocess 執行時間上限（預設 `5.0`） |
| `--cwd <path>` | 覆寫 subprocess 的 working directory |
| `--run-as-user <user>` | 以指定使用者（名稱或 uid）降權執行 block；需 root 權限 |

### codegen rollback

| 選項 | 說明 |
|---|---|
| `paths...` | 要回滾的檔案或目錄（可多個） |
| `--timestamp <ts>`, `-t` | 回滾到指定 timestamp（例：`20240315T143022Z`） |
| `--list`, `-l` | 列出所有可用的 timestamp，不執行回滾 |
| `--backup-dir <path>` | 指定備份根目錄（預設 `.codegen-backup`） |

### 範例

```sh
# 處理單一檔案
codegen src/uart.c

# 處理整個目錄（遞迴）
codegen src/

# 處理多個目標
codegen src/ lib/ tools/extra.c

# 只掃 .c 和 .h
codegen src/ --ext .c,.h

# 失敗時立即中止
codegen src/ --strict

# 預覽輸出但不寫回
codegen src/uart.c --dry-run

# 注入環境變數給腳本
codegen src/ --env PROJECT_VER=2.1.0

# 指定自訂 codegen.toml
codegen src/ --config /path/to/my-codegen.toml
```

---

## Scope dicts：跨 block 共享狀態

每次執行 codegen，工具會準備三個 JSON 檔案路徑，透過環境變數傳給 subprocess：

| 環境變數 | 範圍 | 說明 |
|---|---|---|
| `$CODEGEN_GLOBAL` | 整個資料夾批次 | 所有檔案的 block 都可讀寫 |
| `$CODEGEN_FILE` | 同一個檔案 | 同檔案的 block 按順序共享 |
| `$CODEGEN_BLOCK` | 單一 block | 每次 block 執行前重置 |

**Python（原生 JSON）：**

```python
#!/usr/bin/env python3
import json, os

# 讀
with open(os.environ["CODEGEN_FILE"]) as f:
    state = json.load(f)

# 寫
state["counter"] = state.get("counter", 0) + 1
with open(os.environ["CODEGEN_FILE"], "w") as f:
    json.dump(state, f)

print(f"// counter = {state['counter']}")
```

**Shell（jq）：**

```sh
#!/bin/sh
val=$(jq -r '.counter // 0' "$CODEGEN_FILE")
jq ".counter = $((val + 1))" "$CODEGEN_FILE" > /tmp/tmp.json && mv /tmp/tmp.json "$CODEGEN_FILE"
echo "// counter = $((val + 1))"
```

因為 block 在同一個檔案內是由上至下依序執行的，後面的 block 一定能看到前面 block 寫入的值。

---

## 嵌套展開（Nested Expansion）

預設 `max_passes=1`，腳本輸出中若包含新的 codegen block，**不會**自動展開。

若要允許嵌套，把 `max_passes` 調高：

```c
/* CODEGEN_START
#!/usr/bin/env python3
# codegen: max_passes=2
# 這個腳本的輸出中包含另一個 codegen block，需要第二輪展開
print("""/* CODEGEN_START
#!/bin/sh
echo "nested output"
CODEGEN_END */""")
CODEGEN_END */
```

限制說明：
- `max_passes`：最多展開幾輪（1 = 不嵌套）
- `max_total_time`：從第一輪到穩定的累積時間上限（包含所有子 block）
- `max_pass_time`：單次 subprocess 執行時間上限

三個限制任一觸發即停止，保留目前已展開的結果，繼續處理下一個 block。

---

## 備份與回滾

### 備份

預設每次執行前，自動備份所有處理到的檔案至 `.codegen-backup/`：

```
.codegen-backup/
  src/
    uart.c/
      20240315T143022Z/   ← timestamp（UTC）
        uart.c
      20240316T090100Z/
        uart.c
```

關閉備份：`--no-backup` 或 `codegen.toml` 裡 `backup = false`。

### 回滾

```sh
# 回滾到最近一次備份
codegen rollback src/uart.c

# 回滾到指定 timestamp
codegen rollback src/uart.c --timestamp 20240315T143022Z

# 回滾整個目錄
codegen rollback src/

# 列出所有可用的 timestamp
codegen rollback src/uart.c --list
```

---

## codegen_helper：Python 腳本便利 API

安裝 codegen 後，`codegen_helper` 可在腳本內直接 `import`：

```python
#!/usr/bin/env python3
import codegen_helper as cg

# 讀寫 file scope
counter = cg.file_get("counter", 0)
cg.file_set("counter", counter + 1)

# 讀寫 global scope（跨檔案）
cg.global_set("build_id", "v2.1.0")

# 取得當前檔案路徑
print(f"// generated from {cg.file_path()}")

# 取得原始 block 內容
original = cg.origin_block()
```

### API 一覽

| 函式 | 說明 |
|---|---|
| `global_get(key, default)` | 讀 global scope |
| `global_set(key, value)` | 寫 global scope |
| `global_del(key)` | 刪 global scope |
| `file_get(key, default)` | 讀 file scope |
| `file_set(key, value)` | 寫 file scope |
| `file_del(key)` | 刪 file scope |
| `block_get(key, default)` | 讀 block scope |
| `block_set(key, value)` | 寫 block scope |
| `block_del(key)` | 刪 block scope |
| `origin_file()` | 整檔的處理前快照（字串） |
| `origin_block()` | 本 block 的原文（shebang + body） |
| `targets()` | 本次執行傳入的目標清單（list[str]） |
| `invoke_cwd()` | 執行 codegen 時的工作目錄 |
| `file_path()` | 當前處理檔案的絕對路徑 |

---

## 錯誤處理

### on_error 模式

| 模式 | 行為 |
|---|---|
| `continue`（預設） | 印 stderr、跳過失敗的 block、繼續處理後面的 block 和其他檔案 |
| `abort_file` | 印 stderr、停止處理當前檔案剩下的 block，繼續下一個檔案 |
| `abort_all` | 印 stderr、立即中止整個批次 |

```sh
# 任何 block 失敗就中止
codegen src/ --strict

# 失敗時繼續跑完所有 block（CI 用，收集所有錯誤）
codegen src/ --on-error continue
```

### 失敗時的診斷輸出

```
codegen: block 失敗 — src/uart.c 行 42
原始 block:
  #!/usr/bin/env python3
  raise ValueError("something wrong")
pass 1 stdout:
  （無輸出）
失敗原因：exit:1
stderr:
  Traceback (most recent call last):
    ...
  ValueError: something wrong
```

### Exit codes

| Code | 意義 |
|---|---|
| 0 | 全部成功 |
| 1 | 至少一個 block 失敗 |
| 2 | `abort_all` 觸發的整批中止 |
| 3 | 啟動前的設定或參數錯誤 |
| 130 | SIGINT（Ctrl+C） |

---

## 實用技巧

### 把 block 保留為註解（just-in-case）

`keep_as_comment=true` 會把原始 block 以該語言的註解語法保留在輸出上方：

```c
/* CODEGEN_START
#!/usr/bin/env python3
# codegen: keep_as_comment=true
print("int x = 42;")
CODEGEN_END */
```

輸出：

```c
// === codegen source (kept) ===
// /* CODEGEN_START
// #!/usr/bin/env python3
// # codegen: keep_as_comment=true
// print("int x = 42;")
// CODEGEN_END */
// === end ===
int x = 42;
```

### 自訂 Marker

避免和現有字串衝突時可換 marker：

```c
/* codegen: markers=<<<,>>> */

/* <<<
#!/usr/bin/env python3
print("int x;")
>>> */
```

### 搭配 git 使用

建議把 `.codegen-backup/` 加到 `.gitignore`。codegen 本身不做版本管理——要回到生成前請用 `git checkout`；備份機制是給工具出問題時的緊急還原用的。
