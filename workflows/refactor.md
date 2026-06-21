# 重構（refactor）— 工作流入口

← [INDEX](../INDEX.md)｜[CLAUDE.md](../CLAUDE.md)

重構 / 拆檔 / 整理結構的工作流。**核心鐵律：行為不變（behavior-preserving）。**

## 流程

```
先跑測試確認綠燈（建立基準）
  → 一次只動一個面向（一種重構，不混搭新功能）
  → 改完再跑測試，必須與基準一致
  → 雙實作對齊：結構性重構若只動單邊，記下另一邊待同步
  → 更新 CODE_MAP / 受影響文檔 → commit
```

- **行為不變**：重構不得改變對外行為，測試是唯一裁判（[testing](testing.md)）。若測試覆蓋不足以保證行為不變，先補測試再重構。
- **結構整理**走 [DEV-GUIDE](../DEV-GUIDE.md)：膨脹即拆（src/cpp 單檔 > 300 行、workflows 文檔 > 8192 bytes）、雜亂即分類。拆檔後更新對應 index / CODE_MAP。
- **不混搭**：重構 commit 與功能 commit 分開，方便 review 與回滾。
- 程式碼慣例見 [common/conventions](common/conventions.md)。

## 膨脹時

本檔膨脹或重構案例需歸檔 → 升級成資料夾（`refactor/README.md` + `archive/`（已完成重構案例封存）+ `session-log.md`）。
