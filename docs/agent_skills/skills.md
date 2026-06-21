# 技能 A／B：新增 block 與補全 stub block

← [agent_skills/README](README.md)

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
