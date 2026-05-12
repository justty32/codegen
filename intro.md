# codegen

跨語言的 in-source 程式碼生成工具。在原始檔裡用特殊註解框住一段腳本，執行後把 stdout 寫回原位，marker 隨即消失。

**平台：POSIX only（Linux / macOS）。**

---

## 安裝

```sh
pip install .
```

---

## 最簡範例

```c
/* CODEGEN_START
#!/usr/bin/env python3
for i in range(3):
    print(f"int x{i};")
CODEGEN_END */
```

```sh
codegen file.c
```

執行後 block 被替換為腳本的 stdout：

```c
int x0;
int x1;
int x2;
```

---

## 各語言寫法

| 語言 | 格式 |
|---|---|
| C / C++ / Rust / Go | `/* CODEGEN_START ... CODEGEN_END */` |
| Python / Shell / YAML | `# CODEGEN_START ... # CODEGEN_END` |
| HTML / XML / Markdown | `<!-- CODEGEN_START ... CODEGEN_END -->` |

省略 shebang 時，整個 block 視為 Python 腳本。

---

## 常用指令

```sh
codegen src/              # 遞迴處理目錄
codegen file.c --dry-run  # 預覽不寫回
codegen file.c --strict   # 任何失敗即中止
codegen rollback file.c   # 回滾到上一次備份
```

---

完整說明請見 [README.md](README.md)。
