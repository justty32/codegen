← [docs](../) ｜ [index](../../index.md)

# codegen 使用手冊（for AI agent）

這份文件告訴你如何在原始碼檔案中寫 codegen block，以及如何執行 codegen CLI。

---

## codegen 做什麼

你在原始檔裡寫一段「腳本 + 特殊 marker」。執行 `codegen <檔案>` 後，工具會：
1. 找到這段 marker 框住的腳本
2. 執行腳本
3. 把腳本的 **stdout** 寫回原位，**替換掉整個 block（包含 marker 自身）**

執行完之後 marker 就消失了，無法再次執行。這是設計如此，不是 bug。

**只支援 Linux / macOS，不支援 Windows。** 在 Windows 環境下需透過 WSL（Windows Subsystem for Linux）執行；若無法切換，應提示使用者切換到 WSL 或 Linux/macOS 環境，自己不要嘗試執行 codegen。

---

## 章節 → 檔案

| 章節 | 檔案 |
|---|---|
| 如何在檔案裡寫 block（規則 / 各語言寫法 / 省略 shebang / 縮排 auto-indent） | [writing-blocks.md](writing-blocks.md) |
| 執行指令 + 腳本裡可以用的環境變數 | [running-and-env.md](running-and-env.md) |
| 常見錯誤與修正方式 + 完整設定方式 | [troubleshooting-config.md](troubleshooting-config.md) |
