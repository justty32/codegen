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

## 4. Block 執行環境

Block subprocess 啟動時會看到下列幾類環境變數。

### 4.1 Scope dicts（三層共享狀態）

| 環境變數 | 範圍 | 生命週期 |
|---|---|---|
| `$CODEGEN_GLOBAL` | folder 全域 | 一次資料夾處理 |
| `$CODEGEN_FILE` | 檔案級 | 該檔案處理期間 |
| `$CODEGEN_BLOCK` | block 級 | 單一 block 執行 |

每個變數指向一個 JSON 檔路徑。Block 可任意 read/write，工具在前後負責 load/persist。

選擇性提供 `codegen_helper.py`，把三層 dict 包成可直接 `import` 使用的 Python 介面（避免每次自己 read/write JSON）。Shell/其他語言直接用 JSON。

### 4.2 原始狀態指標（唯讀）

工具會額外提供兩個指向「處理開始前快照」的環境變數，方便 block 腳本參考原樣：

| 環境變數 | 內容 | 生命週期 |
|---|---|---|
| `$CODEGEN_ORIGIN_FILE` | 整份檔案的原文（codegen 開始處理該檔前的快照） | 該檔案處理期間 |
| `$CODEGEN_ORIGIN_BLOCK` | 該 block 的原文，僅含 shebang + 腳本本體（不含 marker、不含註解語法） | 單一 block 處理期間 |

兩者皆為暫存檔路徑，內容為純文字（非 JSON），block 腳本只會讀不會寫——即使動了也不影響工具，下一輪展開的依據是原檔案當前內容，不是這份快照。

對展開出來的子 block：`$CODEGEN_ORIGIN_BLOCK` 指向該子 block 在父 block 該輪 stdout 中的原樣內容（同樣是 shebang + 腳本本體，不含 marker 與註解語法）；`$CODEGEN_ORIGIN_FILE` 任何層級都共用同一份——整檔的處理前快照。

### 4.3 路徑與位置指標（唯讀）

讓 block 腳本知道自己被誰、在哪裡呼叫：

| 環境變數 | 內容 |
|---|---|
| `$CODEGEN_INVOKE_CWD` | 使用者執行 `codegen` 時的 cwd（絕對路徑） |
| `$CODEGEN_TARGETS` | 本次執行傳給 codegen 的目標清單，換行分隔（絕對路徑） |
| `$CODEGEN_FILE_PATH` | 當前處理檔案在 disk 上的絕對路徑 |

### 4.4 Working directory 與使用者自訂 env

- **`cwd` 設定**：subprocess 啟動時的 working directory。預設為 `$CODEGEN_INVOKE_CWD`（使用者執行 codegen 的 cwd）。可透過設定鏈覆寫（CLI `--cwd`、folder config `cwd = "..."`、pragma `cwd=...`、per-block pragma 同樣可以蓋）。相對路徑以 §4.3 的 `$CODEGEN_INVOKE_CWD` 為基準。
- **使用者自訂 env**：在 `codegen.toml` 的 `[env]` 表或 CLI `--env KEY=VAL`（可重複）注入，subprocess 啟動時一併帶入。`[env]` 與 `--env` 採合併語意，後者覆寫前者；不支援在 pragma 內注入（pragma 只能調行為設定）。

## 5. 輸出與保留原 source

- **預設行為**：執行成功後，產生的內容**完整取代** source block——**包含 marker 與整個註解區塊一起被覆蓋**。codegen 是**一次性**生成器，跑完一次原 block 就消失，再跑一次找不到 marker、不會重新生成。要重新生成請從版控 revert 後再跑。
- `--keep-as-comment`：把原 source 用該檔案語言的註解語法包起來，貼在輸出上方當就地備份（純參考用，不影響「一次性」性質——被註解掉的 marker 不會再被識別）

```c
// === codegen source (kept) ===
// CODEGEN_START
// #!/usr/bin/env python3
// print("int x;")
// CODEGEN_END
// === end ===
int x;
```

### 5.1 縮排處理

| 設定 | 預設 | 說明 |
|---|---|---|
| `auto_indent` | true | 把 stdout 重新對齊到原 block 在檔案中的縮排層級（block 整體前綴的空白／tab）。關閉時 stdout 原樣寫回。 |

關閉 `auto_indent` 的場景：之後會跑 formatter，或腳本自己處理縮排。`auto_indent` 套用 §3 的多層覆寫鏈。

## 6. 執行模型與嵌套

### 6.1 執行順序（單執行緒、由上至下）

整個處理過程都是單執行緒，順序固定：

- 資料夾掃描：逐一檔案處理（不平行）
- 單一檔案內：block 從頂至底依序處理
- 每處理完一個 block 才會接到下一個 block

這保證 file-local JSON 的讀寫順序是可預測的——較後面的 block 一定能看到較前面 block 寫入的內容。

**檔案處理是 in-memory**：開始處理一個檔案時，先把整檔讀進記憶體，所有 block 在記憶體中依序展開／替換完，最後才一次性寫回 disk。block 執行期間 disk 上的檔案內容**不會變動**（仍是處理前的原樣），block 想讀取「同檔案前面 block 的結果」要透過 `$CODEGEN_FILE`（JSON 共享）而不是去讀 disk。`$CODEGEN_ORIGIN_FILE` 也指向處理前的快照。

### 6.2 單一 block 的展開（per-block 多輪）

嵌套是**以 block 為單位**處理，不是整檔重掃。處理一個 block 的流程（全程在記憶體內）：

1. 執行該 block 的腳本，取得 stdout
2. 把 stdout 替換掉該 block 在記憶體中的位置
3. 若 stdout 中又有未展開的 CODEGEN block，就針對新出現的 block 繼續展開（同一 pass 內若有多個新 block，由上至下逐一處理）
4. 重複直到此 block 區域內沒有新 block（**穩定**），或達到上限為止
5. 達到穩定後，這塊區域才算處理完成，繼續處理檔案中下一個 top-level block

新展開出來的子 block 視為**與父 block 屬於同一檔案**：共用 `$CODEGEN_FILE`、可讀寫同檔內前面 block 的狀態，**預設繼承父 block 的 pragma 設定**。子 block 若自己也寫了 per-block pragma，依 §3 規則由 per-block pragma 覆寫（仍是最高優先級）。繼承是逐層的——孫 block 沿用子 block 已被覆寫過的 pragma，孫的 pragma 再覆寫一次，以此類推。

期間 block 可任意讀寫 file-local JSON（`$CODEGEN_FILE`），看到的就是同檔內前面 block 已寫入的狀態。

支援三種上限（任一達到即停止該 block 的展開）：

| 設定 | 預設 | 說明 |
|---|---|---|
| `max_passes` | 1 | 該 block 最多展開幾輪（預設 1 = 不嵌套，需要嵌套就調高） |
| `max_total_time` | 5.0 秒 | 該 block 整體展開的累積時間上限（包含所有子 block 的執行時間；從第一輪開始累計到展開穩定為止；各 top-level block 的計時器獨立、互不相干） |
| `max_pass_time` | 5.0 秒 | 單一 pass（一次 subprocess 執行）的時間上限；超過即殺掉該 subprocess |

`max_total_time` 是「該 block 從頭到尾」的累積額度，`max_pass_time` 則是「每一輪 subprocess 自己」能跑多久——前者預防整個 block 卡在無限嵌套，後者預防單次腳本本身寫掛。兩者獨立計算。

三個設定都遵循 §3 的多層覆寫順序：per-block pragma > CLI > file pragma > folder config > 環境變數 > 內建預設。

達到上限時印警告、保留目前展開結果、繼續處理下一個 block。

### 6.3 同檔案內 block 互相引用

透過 file-local JSON（`$CODEGEN_FILE`）共享資料。因為 block 是依序執行的，後面的 block 可直接讀到前面寫入的鍵值。

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
