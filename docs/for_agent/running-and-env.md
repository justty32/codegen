# 執行指令與環境變數

← [for_agent/README](README.md)

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
