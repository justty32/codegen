# 核心資料結構與設定解析（§3–§4）

← [architecture/README](README.md)

## 3. 核心資料結構

```python
# config.py

@dataclass(frozen=True)
class Config:
    # markers

    # 掃描（§9）
    extensions: tuple[str, ...]
    include: tuple[str, ...]
    exclude: tuple[str, ...]
    scan_all: bool

    # 副檔名 → 註解語法（§3.4）
    comment_syntax_overrides: Mapping[str, str]   # ".pyx" -> "#"

    # 展開（§6.2）
    max_passes: int
    max_total_time: float
    max_pass_time: float

    # 輸出（§5）
    keep_as_comment: bool
    auto_indent: bool

    # 備份（§7）
    backup: bool
    backup_dir: Path

    # 錯誤（§10）
    on_error: str                        # "continue" | "abort_file" | "abort_all"

    # subprocess 環境（§4.4）
    cwd: Path
    extra_env: Mapping[str, str]
    run_as_user: str | None              # 降權執行身份（名稱/uid）；pragma-forbidden


# parser.py

@dataclass
class Block:
    file_path: Path
    start_line: int                      # 1-based，§10.4 要用
    end_line: int                        # 結束 marker 所在行
    indent: str                          # START marker 的前綴空白／tab
    comment_syntax: "CommentSyntax"

    raw_block_text: str                  # 從 start marker 整段到 end marker（含 marker 與註解語法）
    inner_text: str                      # 拆掉 marker 與註解殼之後的內容（shebang + pragma + body）
    shebang: str | None                  # 第一個非空行；無 shebang → None（隱含 python3）
    pragma: Mapping[str, str]            # 已解析的 per-block pragma（§3.3 / §3.5）
    body: str                            # 去掉 shebang 與 pragma 之後的純腳本


# comment_syntax.py

@dataclass(frozen=True)
class CommentSyntax:
    open: str                            # "/*", "<!--", "#", "//", "--"
    close: str | None                    # "*/", "-->" or None（line style）

    @property
    def is_block(self) -> bool: ...      # close is not None


# scope.py

@dataclass
class ScopeStore:
    global_path: Path                    # JSON 檔
    file_path: Path
    block_path: Path
    # 內部維護 in-memory snapshot stack 給 §10.5 用


# errors.py

class CodegenError(Exception): ...
class ConfigError(CodegenError): ...
class BlockFailure(CodegenError):
    block: Block
    reason: str                          # "exit:1" | "timeout:pass" | "timeout:total" | "io" | "user:unknown:X" | "user:denied:X" | ...
    passes_stdout: list[str]
    last_stderr: str


EXIT_OK = 0
EXIT_BLOCK_FAILURE = 1
EXIT_ABORT_ALL = 2
EXIT_STARTUP = 3
EXIT_SIGINT = 130
```

## 4. 設定解析（多層覆寫）

`Config` 是 immutable。整條覆寫鏈用「逐層 merge 出新 Config」實作，不是 mutable bag。

### 4.1 解析時機

| 時機 | 函式 | 涵蓋層級 |
|---|---|---|
| 啟動時 | `resolve_initial(cli_args, env)` | 內建預設 → env → folder toml → CLI |
| 處理某檔開頭 | `merge_file_pragma(base, file_pragma)` | 上面結果 → file pragma |
| 處理某 block | `merge_block_pragma(file_cfg, block_pragma)` | 上面結果 → per-block pragma |
| 子 block 繼承（§6.2） | `merge_block_pragma(parent_cfg, child_pragma)` | 用父 block 已合併過的 cfg 當基底，再被子 pragma 覆寫 |

注意：掃描相關欄位（§9.1）只能在 CLI / folder toml / env / defaults 出現，若 file/per-block pragma 出現會被 parser 直接拒掉並報 `ConfigError`。

### 4.2 命名映射

`Config` 的欄位名以 snake_case 為準。CLI argparse 透過 `dest=` 對齊（如 `--max-pass-time` → `dest="max_pass_time"`）。Pragma key 與 Config 欄位同名。TOML 反序列化也直接對應。

`config.FIELD_NAMES`：所有合法 key 的集合，pragma 解析時拿來驗證未知 key（直接報錯，§3.5 不允許未知 key 偷渡）。

### 4.3 folder config 搜尋

`config.find_folder_toml(start: Path) -> Path | None`：從 `start`（target）往上找 `codegen.toml`，遇到 git root 或 filesystem root 就停。`--config <path>` 會跳過此搜尋直接用指定路徑。
