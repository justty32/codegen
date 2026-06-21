# 掃描、解析、展開（§5–§7）

← [architecture/README](README.md)

## 5. 掃描器（`scanner.py`）

```python
def collect_files(targets: Sequence[Path], cfg: Config) -> list[Path]: ...
```

行為：

- 若 `target` 是檔案 → 直接加入結果，不套用任何過濾
- 若 `target` 是資料夾 → 遞迴展開，套用：
  1. `extensions` 過濾（`scan_all=true` 時略過此步）
  2. `include` glob 加進來（疊加在 1 之上）
  3. `exclude` glob 扣除
- glob 使用 `pathlib.Path.match` + 自製遞迴匹配（`**` 支援），不 fork 給 shell

回傳順序：對每個 target 內，按字典序遞迴。

## 6. Parser（`parser.py`）

兩個層級：檔案層 / block 層。

### 6.1 檔案層

```python
def parse_file(content: str, comment_syntax: CommentSyntax, markers: tuple[str, str])
    -> tuple[FilePragma | None, list[Block]]: ...
```

- **File pragma**：掃描檔頭第一個註解區塊（block-style 註解 → 第一段 `/* ... */`；line-style → 連續以 `#` / `//` 開頭的最頂端區塊）。內若有以 `codegen:` 開頭的行才算 pragma；否則回傳 `None`。
- **Top-level blocks**：以 marker 字串為主、用 line-by-line 掃描定位 START / END 行，不用單一 regex（避免在 block-style `/* ... */` 內遇到對註解轉義意外）。
- 回傳的 Block 只標出位置與切片，**不**遞迴拆 stdout 中的子 block——子 block 在 expander 拿到輸出後才即時 parse。

### 6.2 Block header 拆解

```python
def parse_block_header(inner_text: str) -> tuple[str | None, dict[str, str], str]: ...
    # → (shebang, pragma, body)
```

規則：

- 第一行若以 `#!` 開頭 → 視為 shebang；否則 shebang = None（呼叫端會視為 `#!/usr/bin/env python3`）
- 接著的行若是註解前綴 + ` codegen: ...` → pragma 行；多行 pragma 不支援，第二行以後不再嘗試解析（§3.5）
- 剩下的所有行 = body
- 注意：在 block-style 註解（`/* ... */`）內，`# codegen:` 的 `#` 是當前 block 腳本語言的註解符（即 shebang 指向的解譯器），不是檔案語言的註解符。pragma 前綴判定看「shebang 對應的腳本語言」對應的註解前綴。
  - Shell 腳本：`# codegen: ...`
  - Python：`# codegen: ...`
  - Node：`// codegen: ...`
  - 若 shebang 解析不出腳本語言 → 預設 `#`
  - 暫定一張 `interpreter → comment_prefix` 小表，未匹配的腳本語言視為不支援 per-block pragma（已生效的 file/folder/CLI 設定仍然作用）

### 6.3 Pragma 解析

```python
def parse_pragma_line(line: str) -> dict[str, str]: ...
```

- 切掉前綴註解 → 抓到 `codegen:` 之後的內容
- `split()` 拆 token，每個 token 必須是 `key=value` 形式
- value 中不能含空格（已被 `split()` 切開）；list 以逗號分隔留給上層 `Config` 反序列化處理（如 `markers=A,B` → `("A", "B")`）
- `true`/`false` 字面值對應 bool 欄位

未知 key、缺等號、value 空字串：拋 `ConfigError`。

## 7. Expander（`expander.py`）— per-block 多輪展開

```python
def process_content(content: str, cfg: Config, scope: ScopeStore, file_path: Path) -> str: ...
def expand_block(block: Block, parent_cfg: Config, scope: ScopeStore) -> ExpandResult: ...
```

`process_content`：

1. parser 找出檔案中所有 top-level block
2. 由上至下逐 block 呼叫 `expand_block`，將返回的字串替換掉 block 在 content 中的對應切片
3. 任一 block 失敗 → 依 `on_error` 拋 / 回報 / 略過

`expand_block`（單一 block 的多輪展開）：

```
expand_block(block, parent_cfg, scope):
    block_cfg = merge_block_pragma(parent_cfg, block.pragma)
    scope.snapshot()                            # §10.5 記下三層 dict 當前狀態

    region = block.raw_block_text               # 起始：整個原 block 文字
    elapsed_total = 0.0
    pass_idx = 0
    pass_outputs: list[str] = []

    try:
        while pass_idx < block_cfg.max_passes:
            inner_blocks = parser.find_blocks_within(region, ...)
            if not inner_blocks:
                break                            # 穩定

            # 一輪內可能有多個新 block，由上至下逐一展開
            for ib in inner_blocks:
                stdout, used_time = executor.run_block(ib, block_cfg, scope, ...)
                pass_outputs.append(stdout)
                elapsed_total += used_time
                if elapsed_total > block_cfg.max_total_time:
                    raise BlockFailure(reason="timeout:total", ...)
                stdout_indented = indent.apply_indent(stdout, ib.indent, block_cfg)
                region = splice(region, ib, stdout_indented)
            pass_idx += 1
        else:
            # 沒 break：迴圈跑完仍非穩定 → 視為達到 max_passes 上限
            warn("max_passes reached")

        scope.commit()                           # §10.5 成功 → 丟掉 snapshot
        return ExpandResult(text=region, ok=True)

    except BlockFailure as e:
        scope.restore()                          # §10.5 失敗 → 還原三層 dict
        raise
```

幾個細節：

- 第 1 輪的「inner_blocks」就是 block 自己（特別處理：第 1 輪是執行原 block 的 shebang，第 2 輪以後才是看 stdout 內有沒有新 block）
- 實作上，把第 1 輪和後續輪用同一段邏輯吃掉：region 初值就是整個 block 原文，`parser.find_blocks_within(region)` 第一次找到的就是 block 本身，第二次找到的是 stdout 中的新 block
- 「子 block 失敗 = 父 block 失敗」（§10.1 嵌套冒泡）由 `executor.run_block` 拋出 `BlockFailure` → expander 不額外處理嵌套，異常自然往上冒
