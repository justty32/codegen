# 準備工作：根據副檔名選對 block 格式

← [agent_skills/README](README.md)

## 準備工作：根據副檔名選對 block 格式

在動手之前，先看檔案的副檔名，決定要用哪種 block 格式。

| 副檔名 | 格式 |
|---|---|
| `.c` `.cpp` `.h` `.hpp` `.rs` `.go` `.java` `.cs` `.swift` `.kt` | `/* CODEGEN_START … CODEGEN_END */` |
| `.py` `.sh` `.bash` `.zsh` `.yaml` `.yml` `.toml` `.rb` `.pl` | `# CODEGEN_START … # CODEGEN_END` |
| `.html` `.xml` `.md` `.markdown` `.svg` | `<!-- CODEGEN_START … CODEGEN_END -->` |
| `.js` `.ts` `.jsx` `.tsx` `.css` `.scss` | `/* CODEGEN_START … CODEGEN_END */` |

**如果不確定，先查專案根目錄的 `codegen.toml` 有無 `[comment_syntax]` 自訂設定；沒有的話預設用 `/* CODEGEN_START … CODEGEN_END */`。**

**限制：`CODEGEN_START` 那一行的前面只能有空白字元，不能有其他程式碼。** 例如 `if (x) { /* CODEGEN_START` 這種寫法會導致 auto-indent 失效。Block 必須另起一行放置。

每種格式的完整模板：

```
/* CODEGEN_START
#!/usr/bin/env python3
<你的腳本>
CODEGEN_END */
```

```
# CODEGEN_START
# #!/usr/bin/env python3
# <你的腳本>
# CODEGEN_END
```

```
<!-- CODEGEN_START
#!/usr/bin/env python3
<你的腳本>
CODEGEN_END -->
```
