# 功能開發（feature-dev）— 工作流入口

← [INDEX](../INDEX.md)｜[CLAUDE.md](../CLAUDE.md)

新增 / 修改 codegen 功能的工作流。always-on 鐵律見 [CLAUDE.md](../CLAUDE.md)；要整理結構時參考 [DEV-GUIDE](../DEV-GUIDE.md)（被動）；**程式碼慣例 + CODE_MAP 維護鏈**見 [common/conventions](common/conventions.md)。

## 流程

```
修改程式碼（增量）
  → 跑測試綠燈（Python pytest；C++ 視改動 build + 跑 example）
  → 評估雙實作是否要同步（src/codegen ↔ cpp）
  → 全數通過後：補齊 CODE_MAP → 補文檔 → commit
```

- **測試是你（Claude）自己跑**的把關（鐵律：改完跑測試）；指令見 [testing](testing.md)、跨環境差異見 [dev-env](dev-env.md)。
- **雙實作對齊**：codegen 有兩份功能對齊的實作（`src/codegen/` Python、`cpp/` C++）。改了行為層面的東西，預設要評估另一邊是否同步；只改單邊時，在 commit 訊息或 [SESSION-LOG](../SESSION-LOG.md) 註明「另一邊待同步」。
- 需使用者親自驗證的（如 Windows 上的 C++ 行為）記到 [WAIT_USER](../WAIT_USER.md)。
- **commit 前**：CODE_MAP + 相關文檔（DESIGN/architecture/README）必須對齊。

## 規劃管線銜接

一個還沒成形的需求別直接動 code：idea（要不要做？[idea](idea.md)）→ roadmap（會做，何時？[roadmap](roadmap.md)）→ spec（設計方案 [spec](spec.md)）→ plan（動工詳規 [plan](plan.md)）→ **本工作流（build）**。已經想清楚的小改動可直接進本工作流。

## 膨脹時

本檔超過 8192 bytes，或已落地功能多到需要編目 → 照 [DEV-GUIDE「結構整理原則」](../DEV-GUIDE.md) 升級成資料夾（`feature-dev/README.md` + `landed/` 功能目錄 + `gotchas.md` + `archive/`）。
