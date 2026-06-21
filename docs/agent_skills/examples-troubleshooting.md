# 執行順序、完整範例與出錯排查

← [agent_skills/README](README.md)

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
