# codegen agent_skills.md

← [docs](../) ｜ [index](../../index.md)

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

> 更詳細的 block 格式說明、執行指令、常見錯誤排查：[for_agent](../for_agent/README.md)

---

## 章節 → 檔案

| 章節 | 檔案 |
|---|---|
| 準備工作：根據副檔名選對 block 格式 | [setup.md](setup.md) |
| 技能 A：在指定行號新增 block／技能 B：補全已存在的 stub block | [skills.md](skills.md) |
| 寫腳本的規則（規則 1–8） | [script-rules.md](script-rules.md) |
| 跨檔案生成的執行順序／完整範例／出錯時怎麼辦 | [examples-troubleshooting.md](examples-troubleshooting.md) |
