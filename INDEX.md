# INDEX — codegen 專案地圖

整個專案的頂層導航。codegen = **跨語言 in-source 程式碼生成工具**。CLAUDE.md 只放路由 + 鐵律 + 指向本檔；細節從這裡分流出去。

程式碼導航另見 [CODE_MAP](workflows/common/code-map/CODE_MAP.md)，在 repo 怎麼工作見 [DEV-GUIDE](DEV-GUIDE.md)。

---

## Repo 佈局

| 路徑 | 內容 |
|------|------|
| `src/codegen/` | Python 參考實作（CLI + parser/scanner/expander/executor/pipeline…）。導航見 [CODE_MAP](workflows/common/code-map/CODE_MAP.md) |
| `src/codegen_helper.py` | block 腳本可 import 的 Python 便利 API |
| `cpp/` | 功能對齊的 **C++ 重寫**（單一原生執行檔，額外支援 Windows / 超時整樹 kill / 降權）。入口 [cpp/README.md](cpp/README.md) |
| `tests/` | pytest（`tests/unit/` + `tests/integration/`）|
| `docs/` | 使用手冊 + 設計文件（見下）|
| `pyproject.toml` | Python 打包（hatchling）+ pytest 設定；`codegen` console script |

## 開發工作流

工作流的**選擇與入口**見 **[WORKFLOWS.md](WORKFLOWS.md)**——依「你想做什麼」派發到 feature-dev / refactor / spec / plan / idea / roadmap / investigation / testing / dev-env / tooling。每個工作流目前多為**單檔**（`workflows/*.md`），按 [DEV-GUIDE](DEV-GUIDE.md) 的四級成長軌跡膨脹後再升級成資料夾。

[DEV-GUIDE](DEV-GUIDE.md) 是**被動的結構整理參考**——只在要重構/整理結構時取用。always-on 的**鐵律**在 [CLAUDE.md](CLAUDE.md)；碰原始碼的**程式碼慣例 + CODE_MAP 維護鏈**在 [common/conventions](workflows/common/conventions.md)。

## 通用（跨工作流共享）

| 路徑 | 內容 |
|------|------|
| [common/README](workflows/common/README.md) | 跨工作流共通：[conventions](workflows/common/conventions.md) 程式碼慣例 + [gotchas](workflows/common/gotchas.md) 踩坑 + [code-map/](workflows/common/code-map/CODE_MAP.md) 程式碼導航 |

## docs/ — 使用手冊與設計文件

| 路徑 | 內容 |
|------|------|
| [docs/intro.md](docs/intro.md) | 最簡上手（快速範例）|
| [docs/for_agent.md](docs/for_agent.md) | 給 AI agent 的使用手冊（怎麼寫 block、怎麼跑 CLI）|
| [docs/agent_skills.md](docs/agent_skills.md) | agent 技能：新增 block / 補全 stub block |
| [docs/design/DESIGN.md](docs/design/DESIGN.md) | 設計文件（block 格式、執行模型、設定、回滾…）|
| [docs/design/architecture.md](docs/design/architecture.md) | 架構文件（模組劃分、資料結構、執行流程；對應 DESIGN §X）|
| [README.md](README.md) | 對外產品 README（安裝、CLI 參考、完整功能說明）|

## 活狀態（只列還沒完成的）

| 檔案 | 用途 |
|------|------|
| [SESSION-LOG](SESSION-LOG.md) | 進度 hub（repo 根）：open / in-flight |
| [WAIT_USER](WAIT_USER.md) | 待**你**親自做/驗證的（跨平台實測 / 外部環境 / 權限）|
