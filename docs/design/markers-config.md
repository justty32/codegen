# §3. Marker 與選項的設定

← [design/README](README.md)

## 3. Marker 與選項的設定（多層覆寫）

優先順序由高至低：

1. Per-block pragma（block 內的 `# codegen: ...`，見 §3.3）
2. CLI 參數
3. 檔案頂部 pragma
4. 資料夾全域設定檔
5. 環境變數
6. 內建預設

Per-block pragma 是最局部、最精確的意圖宣告（開發者為該 block 親手寫的），所以放在最高優先級——即使 CLI 用 `--max-passes 1` 跑整批，個別 block 仍可用 pragma 把自己拉成 `max_passes=10`。

**命名慣例**：同一設定在 TOML 與 pragma 用 snake_case（如 `max_pass_time`），對應到 CLI 是 kebab-case（如 `--max-pass-time`），名稱一一對應。

### 3.1 全域設定檔

- 檔名：`codegen.toml`
- 位置：從處理目標資料夾向上找，直到檔案系統根或 git root
- 格式：TOML

完整範例：

```toml
# 掃描設定（§9，僅在 toml/CLI/env 生效，不適用 pragma）
extensions = [".c", ".h", ".py", ".rs"]
include = []
exclude = ["**/build/**", "**/.venv/**"]
scan_all = false

# Marker 設定（§3）

# 自訂副檔名 → 註解語法（§3.4）
[comment_syntax]
".pyx" = "#"
".kts" = "/* */"

# 預設展開上限（§6.2）
max_passes = 1
max_total_time = 5.0
max_pass_time = 5.0

# 預設輸出行為（§5）
keep_as_comment = false
auto_indent = true

# 備份（§7）
backup = true
backup_dir = ".codegen-backup"

# 錯誤處理（§10）
on_error = "continue"

# Subprocess 執行環境（§4.4）
cwd = "."

[env]
PROJECT_ROOT = "/path/to/project"
SOME_FLAG = "1"
```

### 3.2 File pragma

檔案最頂部的第一個註解區塊內寫入：

```c
// codegen: markers=<<<,>>>
```

### 3.3 Per-block pragma

Block 內 shebang 之後若有 `# codegen: ...` 行視為該 block 的設定，剩下才是程式內容：

```c
// === codegen source (kept) ===
// /* CODEGEN_START
// #!/usr/bin/env python3
// # codegen: backup=false keep_as_comment=true max_passes=5 max_total_time=1.0 max_pass_time=0.3
// print("int x;")
// CODEGEN_END */
// === end ===
int x;
```

Per-block 可調整：是否備份、是否註解保留、`max_passes`、`max_total_time`、`max_pass_time`、`on_error`、`auto_indent` 等行為設定。

### 3.4 內建預設

- 預設副檔名 ↔ 註解語法：
  - `/* */`：`.c .cpp .h .hpp .rs .go .js .ts .java .cs .swift .kt`
  - `#`：`.py .sh .bash .rb .pl .yaml .yml .toml .makefile`
  - `<!-- -->`：`.html .xml .svg .md`
  - `--`：`.lua .sql .hs`
- 其他副檔名可在設定檔自行擴充

### 3.5 Pragma 共用語法

§3.2 file pragma 與 §3.3 per-block pragma 使用同一套語法：

- 形式：`<comment-prefix> codegen: <key>=<value> <key>=<value> ...`，註解前綴依檔案語言而定（`#`、`//`、`/* */` 等）
- key：snake_case ASCII 識別字（如 `max_pass_time`）
- value：不含空格；list 以逗號分隔（如 `markers=<<<,>>>`）
- 布林值直接寫 `true` / `false`
- 不支援引號、跳脫字元、多行續行

需要塞含空格或特殊字元的值請改用 TOML 設定檔（§3.1）。
