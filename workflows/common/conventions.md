# conventions — 程式碼慣例 + code_map 維護鏈

← [common/README](README.md)

**只在碰原始碼時適用**（feature-dev / refactor / spec / plan 指向這裡）。always-on 鐵律在 [CLAUDE.md](../../CLAUDE.md)，結構整理原則在 [dev-guide](../../dev-guide.md)。

## 程式碼慣例

- **單檔 ≤ 300 行**：`src/codegen/` 與 `cpp/src/` 單檔超過就照 [dev-guide](../../dev-guide.md) 拆。
- **雙實作一一對應**：Python 模組與 C++ 模組對齊（`parser.py` ↔ `parser.{cpp,hpp}` …，見 [code_map](code-map/code_map.md)）。新增/重命名/移除模組時**兩邊一起處理**；確實只動單邊時，在 commit 訊息或 [session-log](../../session-log.md) 註明「另一邊待同步」。
- **行為對齊**：兩份實作對外行為應一致；改了行為層面的東西要評估另一邊。差異（如 C++ 版的 Windows 支援、超時整樹 kill、降權）屬有意為之，記錄在 [cpp/README.md](../../cpp/README.md)。
- **編碼/平台**：檔案一律 UTF-8（無 BOM）。Python 版 POSIX only；C++ 版額外支援 Windows。
- **文檔語言**：繁體中文（zh-TW），與既有文檔一致。

## code_map 維護鏈

程式碼導航 index 在 [code-map/code_map.md](code-map/code_map.md)。

**觸發**：碰了 `src/codegen/` 或 `cpp/src/`——新增/刪除/重命名模組、改變模組職責、調整模組對應關係。
**動作**：在**同一個 commit**內更新 code_map（與必要時的 [architecture/](../../docs/design/architecture/README.md) §2/§3）。
**例外**：純函式內部實作微調、不影響模組職責的，不必動 code_map。

feature-dev 流程裡，測試迭代期間 code_map/文檔可暫時落後，但 **commit 前必須對齊**。
