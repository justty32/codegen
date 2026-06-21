# §4. Block 執行環境 ｜ §5. 輸出與保留原 source ｜ §6. 執行模型與嵌套

← [DESIGN/README](README.md)

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
