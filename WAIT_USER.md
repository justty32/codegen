# WAIT_USER — 待你親自做 / 驗證

← [CLAUDE.md](CLAUDE.md)｜[INDEX.md](INDEX.md)

這裡列**需要使用者親自處理**、Claude 起不了或無權做的事：跨平台實測（如 Windows 上的 C++ 版行為）、外部環境/權限、需本機特定設定才能跑的驗證。

**規範**：**只列 open**，完成即移除、不留已完成清單。一般開發進度（Claude 自己能推進的）記到 [SESSION-LOG.md](SESSION-LOG.md)，不在這裡。

---

## Open

- **Windows 上驗證 executor 拆檔**：`cpp/src/executor.cpp` 已按平台拆成 `executor_posix.cpp` / `executor_win.cpp`（純搬移、`#ifdef` 守衛）。Linux 端已 build + 跑 `demo.sh` 通過；Windows 半邊（`executor_win.cpp`）只有在 Windows 上 build 才會編到，需在 Windows 上 `cmake -S cpp -B cpp/build && cmake --build cpp/build` 並跑 example 確認行為不變。
