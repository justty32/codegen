# 工具與依賴（tooling）— 工作流入口

← [INDEX](../INDEX.md)｜[CLAUDE.md](../CLAUDE.md)

用 / 設定外部工具、查依賴與 env var 的入口。codegen 本體刻意零執行期依賴（Python 版 `dependencies = []`），所以這裡很薄。

## 設定（config）

- 設定檔名：**`codegen.toml`**（多層覆寫，權威說明見 [README.md](../README.md) 的「設定方式」段與 `src/codegen/config.py`）。

## 環境變數（env var）

- codegen 在執行 block 腳本時會**注入一批 `CODEGEN_*` 環境變數**供腳本讀取（檔案路徑、block 內容、scope/global、targets、origin、invoke cwd 等）。權威清單與語意在 `src/codegen/env.py`（C++ 版對應 `cpp/src/env.cpp`）。
- 行為性開關（如最大展開遍數）走 `codegen.toml` / 對應 env var，定義在 `src/codegen/config.py`。
- **不要在文檔裡硬記變數清單**（易過時）——以 `env.py` / `config.py` 為準，本檔只給指標。

## C++ 版第三方依賴

- 透過 CMake `FetchContent` 在 configure 時抓取（CLI11、nlohmann/json 等）；首次 configure 需網路。版本與清單見 `cpp/CMakeLists.txt`。

## 膨脹時

依賴 / env / 外部工具變多 → 升級成資料夾（`tooling/README.md` + `env-vars.md` + `binaries.md` 等）。
