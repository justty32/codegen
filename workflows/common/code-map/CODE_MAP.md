# CODE_MAP — 程式碼導航

← [common/README](../README.md)｜[INDEX](../../../INDEX.md)

程式碼導航 index。codegen 有**兩份功能對齊的實作**，模組**一一對應**：Python 在 `src/codegen/`，C++ 在 `cpp/src/`。權威的模組職責與資料結構說明在 [docs/design/architecture.md](../../../docs/design/architecture.md)（§2 模組劃分、§3 資料結構），本檔是快速指路。

**維護鏈**：碰了 `src/codegen/` 或 `cpp/src/`（新增/刪除/重命名模組、改變模組職責）→ 同一個 commit 內更新本檔。詳見 [common/conventions](../conventions.md)。

## 模組對照（Python ↔ C++）

| 模組 | 職責 | Python | C++ |
|------|------|--------|-----|
| cli | argparse / CLI11 進入點，子命令分派（run / rollback）| `cli.py` | `cli.cpp` |
| config | `Config` + 多層覆寫解析（defaults→env→folder toml→CLI/pragma）| `config.py` | `config.{cpp,hpp}` |
| comment_syntax | 副檔名 → 註解語法表（line/block 風格）| `comment_syntax.py` | `comment_syntax.{cpp,hpp}` |
| scanner | 目標展開 + extensions/include/exclude 過濾 | `scanner.py` | `scanner.{cpp,hpp}` |
| parser | marker/pragma/shebang 切分，找 top-level block | `parser.py` | `parser.{cpp,hpp}` |
| expander | 單一 block 的多輪展開（含嵌套）| `expander.py` | `expander.{cpp,hpp}` |
| executor | 啟動 subprocess、組 env、timeout、收 stdout/stderr | `executor.py` | `executor.{cpp,hpp}` |
| env | 組 subprocess 的 `CODEGEN_*` 環境變數 | `env.py` | `env.{cpp,hpp}` |
| scope | 三層 scope dict 的 JSON 檔生命週期 + 快照/回滾 | `scope.py` | `scope.{cpp,hpp}` |
| indent | `auto_indent` 計算與套用 | `indent.py` | `indent.{cpp,hpp}` |
| backup | 執行前備份 | `backup.py` | `backup.{cpp,hpp}` |
| rollback | `codegen rollback` 子命令 | `rollback.py` | `rollback.{cpp,hpp}` |
| pipeline | 單檔 in-memory 處理流程（串 parser/expander/indent/scope）| `pipeline.py` | `pipeline.{cpp,hpp}` |
| progress | 計畫列印、SIGINT/abort 中斷回報 | `progress.py` | `progress.{cpp,hpp}` |
| errors | exit code 常數 + 自訂 exception 階層 | `errors.py` | `errors.hpp` |

## 僅單邊存在

| 項目 | 位置 | 說明 |
|------|------|------|
| codegen_helper | `src/codegen_helper.py`（Py）/ `cpp/include/codegen_helper.hpp`（C++）| block 腳本可 import/include 的便利 API |
| path_mapper | `cpp/src/path_mapper.{cpp,hpp}` | C++ 版特有：路徑映射（跨平台相關）|
| utils | `cpp/src/utils.hpp` | C++ 版共用工具 |

## 膨脹時

模組多到單表難導航 → 照 [DEV-GUIDE](../../../DEV-GUIDE.md) 按領域拆成多份子 index（`CODE_MAP.<領域>.md`），本檔退回頂層導航。
