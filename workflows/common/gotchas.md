# gotchas — 跨工作流踩坑

← [common/README](README.md)

開發 codegen 時的行為陷阱與易錯點。遇到新坑就追加（一條一段：現象 → 原因 → 對策）。

---

## 概念性陷阱

- **block 執行後 marker 消失、不可重跑**：codegen 把整個 block（含 marker）替換成腳本 stdout，這是設計如此，不是 bug。要保留可重生性就靠版本控制 + `codegen rollback`（[backup.py](../../src/codegen/backup.py) / `rollback.py`）。
- **雙實作易漂移**：改了 Python 版忘了 C++ 版（或反之）。對策見 [conventions](conventions.md) 的「雙實作一一對應」——改行為層面就評估另一邊，只動單邊要註明待同步。

## 平台

- **Python 版不支援原生 Windows**（依賴 shebang / `chmod +x` / SIGINT）。Windows 上要嘛走 WSL、要嘛用 C++ 版。
- **Windows 上的 C++ 版行為 Claude 驗不了** → 交使用者，記 [WAIT_USER](../../WAIT_USER.md)。

## 建置

- **C++ 首次 configure 需網路**（CMake FetchContent 抓 CLI11 / nlohmann/json 等）；離線機器首次 build 會失敗。
- 改了 `cpp/CMakeLists.txt`（含 install 規則）後，要先重跑 `cmake -S cpp -B cpp/build` 再 build/install，否則沿用舊設定。

---

_（暫無更多踩坑）_
