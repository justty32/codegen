# workflows — 工作流派發器

← [CLAUDE.md](CLAUDE.md)｜專案地圖 [index.md](index.md)

你（使用者）說要做某件事 → **從這張表選對應工作流 → 讀它的「入口檔」→ 就知道要做什麼**。每個工作流的細節都在它自己的入口檔，不在這裡。

## 你想做什麼 → 用哪個工作流

| 觸發（你說…）| 工作流 | 入口檔（先讀這個）|
|--------------|--------|-------------------|
| 「我想開發 / 修改某個 feature」 | **feature-dev** | [workflows/feature-dev.md](workflows/feature-dev.md) |
| 「重構 / 拆檔 / 整理結構」 | **refactor** | [workflows/refactor.md](workflows/refactor.md) |
| 「把一個 idea 討論成設計方案」 | **spec** | [workflows/spec.md](workflows/spec.md) |
| 「把設計方案展開成動工計畫」 | **plan** | [workflows/plan.md](workflows/plan.md) |
| 「記一個奇思妙想」（不確定要不要做）| **idea** | [workflows/idea.md](workflows/idea.md) |
| 「記一件確定會做、不確定何時的事」 | **roadmap** | [workflows/roadmap.md](workflows/roadmap.md) |
| 「可行性調查 / 研究某個做法」 | **investigation** | [workflows/investigation.md](workflows/investigation.md) |
| 「跑測試」 | **testing** | [workflows/testing.md](workflows/testing.md) |
| 「設定 / 了解開發環境」「fresh clone 後要做什麼」 | **dev-env** | [workflows/dev-env.md](workflows/dev-env.md) |
| 「用 / 設定某個外部工具」「查 env var / 依賴」 | **tooling** | [workflows/tooling.md](workflows/tooling.md) |

**規劃管線**（一個想法的成熟過程）：idea（要不要做？）→ roadmap（會做，何時？）→ spec（討論後方案）→ plan（動工前詳規）→ build（feature-dev）。

## 工作流的統一形式（規範）

所有工作流照同一套形式（細則見 [dev-guide](dev-guide.md)）：

**檔名規範**：
- **README** = 初入一個資料夾**先讀的入口／導引**（這資料夾在幹嘛、怎麼用）。
- **index** = **描述該資料夾頂層結構**的索引（有哪些子項、各放什麼）。
- 小資料夾兩者可合一（README 兼述結構）；大到結構複雜時才分出獨立 index。

形式（依[四級成長軌跡](dev-guide.md)按需長出）：
- **單檔工作流**（目前多數，如 testing / dev-env / feature-dev…）：一個 `.md` 同時是入口與內容。撐大了才升級成資料夾型。
- **資料夾型工作流**（如 common）：一個入口 README + 視需要的 `archive/`（封存過時文檔）、`gotchas.md`、`session-log.md` 等。
- 入口檔本身膨脹 → 照[結構整理原則](dev-guide.md)拆。

## 跨工作流的活狀態（repo 根）

- **進度**（還沒完成的 in-flight / open）→ [session-log.md](session-log.md)
- **待你親自做 / 驗證的**（實機環境 / 外部權限 / 跨平台驗證）→ [wait_user.md](wait_user.md)
