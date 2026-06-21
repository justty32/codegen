# 測試（testing）— 工作流入口

← [INDEX](../INDEX.md)｜[CLAUDE.md](../CLAUDE.md)

跑測試的權威指令與分類。鐵律：**改完程式碼就跑測試**（[CLAUDE.md](../CLAUDE.md)）。

## Python 參考實作

```
pytest                       # 全部（pyproject 已設 testpaths=tests, pythonpath=src）
pytest tests/unit            # 只跑單元
pytest tests/integration     # 只跑整合
pytest -k <關鍵字>           # 篩選
```

- 需環境內有 pytest：`pip install -e ".[dev]"`（dev extra 帶 `pytest>=8.0`）。
- 測試在 `tests/unit/` 與 `tests/integration/`。

## C++ 重寫

```
cmake -S cpp -B cpp/build    # 首次 configure 需網路（FetchContent 抓依賴）
cmake --build cpp/build      # 產物 cpp/build/codegen
cpp/examples/codegen_helper/demo.sh    # 跑 example 驗證行為
```

- 目前 cpp 無獨立 ctest 套件；以「build 成功 + example 跑通」與 Python 版行為對照為把關。新增正式 C++ 測試時更新本檔。

## 把關範圍

- **Claude 自己跑**：Python pytest（必跑）、C++ build + example（碰 cpp 時）。
- **交給使用者**：需特定平台/環境才能驗的（如 Windows 上的 C++ 行為）→ 記到 [WAIT_USER](../WAIT_USER.md)。
