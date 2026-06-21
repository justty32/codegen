# codegen — Codex 專案備忘

codegen = **跨語言 in-source 程式碼生成工具**。Python 參考實作在 `src/codegen/`，功能對齊的 C++ 重寫在 `cpp/`。
分層工作流、結構整理原則與 always-on 鐵律詳見 [CLAUDE.md](CLAUDE.md) / [workflows.md](workflows.md) / [dev-guide.md](dev-guide.md)；本檔是精簡平行備忘。

## 開發環境

- **平台**：POSIX（Linux / macOS）。Python 版依賴 shebang / `chmod +x` / SIGINT，不支援 Windows（須走 WSL）。**C++ 版**額外支援 Windows。
- **Python 測試**：`pytest`（`pyproject.toml` 已設 `testpaths=tests`、`pythonpath=src`；`tests/unit/` + `tests/integration/`）。需 `pip install -e ".[dev]"` 或環境內有 pytest。
- **C++ 建置**：
  ```
  cmake -S cpp -B cpp/build
  cmake --build cpp/build      # 產物 cpp/build/codegen
  ```
  首次 configure 需網路（FetchContent 抓 CLI11、nlohmann/json 等）。改了行為後跑 `cpp/examples/codegen_helper/demo.sh` 驗證。
- 測試指令與分類的權威說明見 [workflows/testing.md](workflows/testing.md)，跨環境差異見 [workflows/dev-env.md](workflows/dev-env.md)。

## 鐵律

- **重構必須行為不變**：改完跑測試。
- **未經確認不 push、不開新工作**（commit 到本地 `main` 是慣例，push 先確認）。
- **雙實作對齊**：`src/codegen/` 與 `cpp/` 功能對齊——改了行為層面的東西，評估另一邊是否要同步。

## 程式碼慣例

- 單檔維持在 **300 行以下**（超過照 [dev-guide](dev-guide.md) 拆）。
- Python 與 C++ 模組**一一對應**（`parser.py` ↔ `parser.{cpp,hpp}` 等）；新增/重命名模組時兩邊一起處理，並更新 [code_map](workflows/common/code-map/code_map.md)。
- 文檔一律 **繁體中文（zh-TW）**，與既有文檔一致。

## code_map 維護鏈

程式碼導航 index 在 [workflows/common/code-map/code_map.md](workflows/common/code-map/code_map.md)。**碰了 `src/codegen/` 或 `cpp/src/`**（新增/刪除/重命名模組、改變模組職責）→ 同一個 commit 內更新 code_map。詳見 [common/conventions.md](workflows/common/conventions.md)。
