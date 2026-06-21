# 如何在檔案裡寫 block

← [for_agent/README](README.md)

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
