# 動工計畫（plan）— 工作流入口

← [INDEX](../INDEX.md)｜[CLAUDE.md](../CLAUDE.md)

把一個**已定案的設計方案**（[spec](spec.md)）展開成**動工前的詳細步驟**。產出是「照著做就能落地」的施工序，交給 [feature-dev](feature-dev.md) 執行。

## 流程

```
取定案 spec → 拆成有序步驟（每步可獨立測試）
  → 標出要改的檔案（src/codegen + 對應 cpp/src）、測試點、風險
  → 進 feature-dev 逐步落地
```

- 每個步驟盡量**可獨立跑測試**、可獨立 commit，方便增量驗證與回滾。
- 計畫要明列**雙實作**各自要改哪些檔（Python ↔ C++ 模組一一對應）。
- 規劃管線：idea → roadmap → spec → **plan（本工作流）** → build。

## 膨脹時

plan 多了 → 升級成資料夾（`plans/README.md` + 各計畫檔 + `archive/`：**已完成的計畫移 archive 凍結**）。檔名建議 `YYYY-MM-DD-<主題>.md`，與對應 spec 同名呼應。
