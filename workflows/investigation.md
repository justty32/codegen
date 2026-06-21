# 調查（investigation）— 工作流入口

← [index](../index.md)｜[CLAUDE.md](../CLAUDE.md)

**可行性調查 / 研究某個做法**的工作流：在動手前先搞清楚「這樣行不行、有哪些坑、別人怎麼做」。產出是**結論性的調查筆記**，餵給 [spec](spec.md) 或 [idea](idea.md)。

## 流程

```
界定問題（要驗證什麼假設 / 回答什麼問題）
  → 蒐證（讀源碼 / 跑小實驗 spike / 查文件 / 比較方案）
  → 寫成調查筆記（問題 → 證據 → 結論 → 對 codegen 的意涵）
  → 結論去向：可行 → spec；先不做 → idea/roadmap；不可行 → 記下原因封存
```

- 調查常見題材：跨語言/跨平台行為差異（Python vs C++ 版、POSIX vs Windows）、效能取捨、某語言註解語法的邊界情況、第三方依賴評估。
- **小實驗（spike）允許寫拋棄式 code**，但別混進正式 `src/`；驗證完把結論留下、code 丟掉或標記。
- 查外部資料時，**URL / 版本號 / API 事實必須驗證**，不要把未證實的當結論。

## 膨脹時

調查筆記多了 → 升級成資料夾（`investigation/README.md` + `decode/` 或主題子夾 + `gotchas.md`）。檔名建議帶日期 `<主題>-YYYY-MM-DD.md`。
