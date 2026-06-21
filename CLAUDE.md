# codegen — Claude Code 專案備忘

codegen = **跨語言 in-source 程式碼生成工具**：在原始檔裡用註解 marker 框住一段腳本，執行後把 stdout 寫回原位、marker 隨即消失。Python 參考實作在 `src/codegen/`，功能對齊的 C++ 重寫在 `cpp/`。本檔是**最頂層路由器**：只指向下一層，**durable 細節一律不寫這裡**。

## 先讀哪裡

- **使用者要你動手做某件事** → **[workflows.md](workflows.md)**：依使用者意圖派發到對應工作流，再讀該工作流入口。
- **想看專案長怎樣** → **[index.md](index.md)**：repo 頂層結構地圖。

## 分層思想（本專案的組織原則）

整個 repo 是一棵**分層樹**，每一層**只指向下一層、不存下層的細節**：

```
CLAUDE.md（本檔，最頂）→ workflows.md / index.md → 各工作流入口 → 工作流內容 → 子工作流…
```

- **README**＝初入一個資料夾**先讀的入口／導引**；**index**＝**描述該資料夾頂層結構**的索引。小資料夾兩者合一，大了才分出獨立 index。
- **durable 知識歸到它所屬的那一層／那個工作流**，絕不往上堆——所以 CLAUDE.md 才這麼薄。要某主題的細節，順著上面的樹往下走，不在本檔找。

## 鐵律（always-on，任何工作流任何時候都遵守）

1. **重構/整理必須行為不變**：改完跑測試（Python `pytest`、C++ 視改動範圍重新 build／跑 example）。測試指令與分類見 [workflows/testing.md](workflows/testing.md)。
2. **未經確認不 push、不開新工作**：commit 到本地 `main` 是慣例，**push 前先確認**。
3. **雙實作對齊**：`src/codegen/` 與 `cpp/` 功能對齊——改了行為層面的東西，要評估另一邊是否需要同步（見 [common/conventions](workflows/common/conventions.md)）。
4. **各工作流的具體流程在它自己的入口檔**，不在頂層。

## 被動參考（按需取用，非 always-on）

- **[dev-guide.md](dev-guide.md)**：結構整理原則 + 四級成長軌跡——**只在要重構/整理結構時**才取用，不貫穿日常每個動作。
- 碰原始碼的**程式碼慣例 + code_map 維護鏈**在 [common/conventions](workflows/common/conventions.md)。
- Codex / 其他 agent 的平行備忘在 [AGENTS.md](AGENTS.md)（測試指令 + 程式碼慣例的精簡版）。

## 活狀態（只列還沒完成的）

事情告一段落、因應需求結束、或臨時中止時 → 把**還沒完成**的活狀態記到進度；需要**使用者親自做/驗證**的（實機跑某環境、外部權限）→ 記到待測。兩者都**只列 open**，完成即移除、不留已完成清單。

- **進度** → [session-log.md](session-log.md)
- **待測** → [wait_user.md](wait_user.md)
