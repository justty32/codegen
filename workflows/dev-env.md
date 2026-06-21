# 開發環境（dev-env）— 工作流入口

← [INDEX](../INDEX.md)｜[CLAUDE.md](../CLAUDE.md)

設定 / 了解開發環境，以及 fresh clone 後要做什麼。

## Fresh clone 後

**Python 版**：
```
pip install -e ".[dev]"     # 可編輯安裝 + pytest
pytest                       # 確認綠燈
```
- `codegen` console script 由 `pyproject.toml` 的 `[project.scripts]` 提供。
- 需要 Python ≥ 3.11。

**C++ 版**：
```
cmake -S cpp -B cpp/build    # 首次需網路（FetchContent 抓 CLI11 / nlohmann/json 等）
cmake --build cpp/build      # 產物 cpp/build/codegen
```
- 需 CMake ≥ 3.20 + 支援 C++17 的編譯器（GCC / Clang / MSVC / MinGW）。
- 安裝規則用 `GNUInstallDirs`：`cmake --install cpp/build --prefix ~/.local`（細節見 [cpp/README.md](../cpp/README.md)）。

## 平台能力

| 環境 | Python 版 | C++ 版 |
|------|-----------|--------|
| Linux / macOS | ✅ 完整 | ✅ 完整 |
| Windows | ❌（須走 WSL）| ✅（C++ 版額外支援）|

- Python 版依賴 POSIX（shebang / `chmod +x` / SIGINT），不支援原生 Windows。
- **Claude 起不了原生 Windows 環境**：Windows 上的 C++ 版行為需使用者親驗 → [WAIT_USER](../WAIT_USER.md)。

## 相關

- 測試指令 → [testing](testing.md)
- 外部依賴 / env var / 設定 → [tooling](tooling.md)
