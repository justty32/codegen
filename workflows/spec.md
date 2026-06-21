# 設計方案（spec）— 工作流入口

← [INDEX](../INDEX.md)｜[CLAUDE.md](../CLAUDE.md)

把一個 idea / 需求**討論成設計方案**的工作流。產出是「要做什麼、為什麼這樣做」的設計文檔，**還不是動工步驟**（那是 [plan](plan.md)）。

## 流程

```
釐清問題與目標 → 探索選項 + 取捨 → 收斂成方案
  → 寫成 spec 文檔（決策 + 理由 + 影響面 + 對兩份實作的意涵）
  → 進 plan（展開動工步驟）或回 roadmap（暫不做）
```

- **設計要顧雙實作**：codegen 同時有 Python（`src/codegen/`）與 C++（`cpp/`）兩份對齊實作，設計方案要說明對兩邊各自的意涵。
- 全工具層級的權威設計在 [docs/design/DESIGN/](../docs/design/DESIGN/README.md) 與 [architecture/](../docs/design/architecture/README.md)；spec 工作流產出的是**新功能/變更的提案**，定案後該把影響回寫進這些設計文件。
- 規劃管線：idea → roadmap → **spec（本工作流）** → plan → build。

## 膨脹時

spec 多了 → 升級成資料夾（`specs/README.md` + 各 `*-design.md` + `archive/`：**已完成/被取代的設計一律移 archive 凍結**，不留現役夾）。檔名建議 `YYYY-MM-DD-<主題>-design.md`。
