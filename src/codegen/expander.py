from __future__ import annotations

import tempfile
import warnings
from dataclasses import dataclass, field
from pathlib import Path
from typing import Mapping

from codegen.config import Config, merge_pragma
from codegen.env import RunContext, build_env
from codegen.errors import AbortAll, BlockFailure
from codegen.executor import run_block as _run_block
from codegen.indent import apply_indent
from codegen.parser import Block, find_top_level_blocks
from codegen.scope import ScopeStore


@dataclass
class ExpandResult:
    text: str          # replacement text (no markers)
    ok: bool = True


def _splice(region: str, block: Block, replacement: str) -> str:
    """Replace the lines occupied by *block* in *region* with *replacement*."""
    lines = region.splitlines(keepends=True)
    before = lines[: block.start_line - 1]
    after = lines[block.end_line :]
    return "".join(before) + replacement + "".join(after)


def _write_tmp(content: str) -> Path:
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".txt", delete=False, encoding="utf-8"
    ) as f:
        f.write(content)
        return Path(f.name)


def _keep_as_comment(original_raw: str, block: Block, output: str) -> str:
    """Prepend the original block as a comment above the generated output (§5)."""
    cs = block.comment_syntax
    lines = original_raw.splitlines(keepends=True)
    if cs.is_block:
        commented = (
            f"{block.indent}// === codegen source (kept) ===\n"
            + "".join(f"{block.indent}// {line.rstrip()}\n" for line in lines)
            + f"{block.indent}// === end ===\n"
        )
    else:
        commented = (
            f"{block.indent}{cs.open} === codegen source (kept) ===\n"
            + "".join(f"{block.indent}{cs.open} {line.rstrip()}\n" for line in lines)
            + f"{block.indent}{cs.open} === end ===\n"
        )
    return commented + output


def expand_block(
    block: Block,
    parent_cfg: Config,
    scope: ScopeStore,
    ctx: RunContext,
) -> ExpandResult:
    """Expand a single top-level block through up to max_passes rounds (§6.2).

    Returns ExpandResult with the replacement text (no markers, may contain
    generated code plus any keep_as_comment header).

    Raises BlockFailure on error (caller decides on_error behaviour).
    """
    block_cfg = merge_pragma(parent_cfg, block.pragma, source=f"{block.file_path}:{block.start_line}")

    # Snapshot scope dicts before touching this block (§10.5)
    scope.open_block()
    scope.snapshot()
    snapshot_finalized = False

    # Create origin_block snapshot (§4.2)
    origin_block_path = _write_tmp(block.inner_text)

    try:
        # region starts as the full raw block text (including markers).
        # Each pass replaces block(s) found in region with their stdout.
        region = block.raw_block_text
        elapsed_total = 0.0
        pass_outputs: list[str] = []
        pass_idx = 0
        original_raw = block.raw_block_text

        while pass_idx < block_cfg.max_passes:
            inner_blocks = find_top_level_blocks(
                region,
                file_path=block.file_path,
                markers=(block_cfg.markers[0], block_cfg.markers[1]),
                cs=block.comment_syntax,
            )
            if not inner_blocks:
                break  # stable

            for ib in inner_blocks:
                # For nested sub-blocks, origin_block is the sub-block's inner text
                ib_origin = _write_tmp(ib.inner_text)
                ib_env = build_env(
                    block_cfg.extra_env,
                    scope,
                    RunContext(
                        invoke_cwd=ctx.invoke_cwd,
                        targets=ctx.targets,
                        file_path=ctx.file_path,
                        origin_file_path=ctx.origin_file_path,
                        origin_block_path=ib_origin,
                    ),
                )

                cwd = block_cfg.cwd or ctx.invoke_cwd
                stdout, elapsed = _run_block(
                    ib,
                    env=ib_env,
                    cwd=cwd,
                    max_pass_time=block_cfg.max_pass_time,
                    pass_outputs=pass_outputs,
                )
                try:
                    ib_origin.unlink()
                except OSError:
                    pass

                pass_outputs.append(stdout)
                elapsed_total += elapsed

                if elapsed_total > block_cfg.max_total_time:
                    raise BlockFailure(
                        block=ib,
                        reason="timeout:total",
                        pass_outputs=list(pass_outputs),
                    )

                indented = apply_indent(stdout, ib.indent, block_cfg.auto_indent)
                region = _splice(region, ib, indented)

            pass_idx += 1

        else:
            # Loop exhausted without break — warn only if blocks remain in region
            remaining = find_top_level_blocks(
                region,
                file_path=block.file_path,
                markers=(block_cfg.markers[0], block_cfg.markers[1]),
                cs=block.comment_syntax,
            )
            if remaining:
                warnings.warn(
                    f"{block.file_path}:{block.start_line}: max_passes={block_cfg.max_passes} reached; "
                    "expansion may be incomplete",
                    stacklevel=2,
                )

        scope.commit()
        snapshot_finalized = True

        if block_cfg.keep_as_comment:
            region = _keep_as_comment(original_raw, block, region)

        return ExpandResult(text=region, ok=True)

    except BlockFailure:
        scope.restore()
        snapshot_finalized = True
        raise
    finally:
        if not snapshot_finalized:
            # Non-BlockFailure exception: restore so the snapshot doesn't leak.
            scope.restore()
        try:
            origin_block_path.unlink()
        except OSError:
            pass
        scope.close_block()


def process_content(
    content: str,
    cfg: Config,
    scope: ScopeStore,
    ctx: RunContext,
) -> tuple[str, bool]:
    """Process all top-level blocks in *content*.

    Returns (updated_text, had_failure). Each block is expanded in place;
    on_error decides whether to keep going, abort the file, or raise AbortAll.
    Per §10.4, the failure diagnostic is always emitted to stderr regardless
    of which on_error mode is active.
    """
    from codegen.comment_syntax import lookup

    cs = lookup(ctx.file_path, overrides=dict(cfg.comment_syntax_overrides))
    if cs is None:
        return content, False  # unknown extension: nothing to do

    blocks = find_top_level_blocks(
        content,
        file_path=ctx.file_path,
        markers=cfg.markers,
        cs=cs,
    )
    if not blocks:
        return content, False

    # Work with a mutable list of lines; offsets shift as we replace blocks.
    # Easiest: rebuild content string after each block (blocks are sequential).
    result = content
    line_offset = 0  # track how many lines the content has shifted so far
    had_failure = False

    for block in blocks:
        # Adjust block position by accumulated line offset
        adjusted = Block(
            file_path=block.file_path,
            start_line=block.start_line + line_offset,
            end_line=block.end_line + line_offset,
            indent=block.indent,
            comment_syntax=block.comment_syntax,
            raw_block_text=block.raw_block_text,
            inner_text=block.inner_text,
            shebang=block.shebang,
            pragma=block.pragma,
            body=block.body,
        )

        try:
            expand_res = expand_block(adjusted, cfg, scope, ctx)
        except BlockFailure as exc:
            _emit_failure(exc)  # §10.4: always emit diagnostic
            had_failure = True
            if cfg.on_error == "abort_all":
                raise AbortAll(exc) from exc
            if cfg.on_error == "abort_file":
                raise
            # continue: keep original block text, move on
            continue

        replacement = expand_res.text
        # If the original block was line-terminated, the replacement must be too —
        # otherwise the line that follows the block would be merged into the last
        # line of the replacement, corrupting the file and shifting later blocks.
        if adjusted.raw_block_text.endswith("\n") and replacement and not replacement.endswith("\n"):
            replacement += "\n"

        old_line_count = len(adjusted.raw_block_text.splitlines())
        new_line_count = len(replacement.splitlines())
        line_offset += new_line_count - old_line_count

        result = _splice(result, adjusted, replacement)

    return result, had_failure


def _emit_failure(exc: BlockFailure) -> None:
    import sys

    b = exc.block
    print(f"codegen: block 失敗 — {b.file_path} 行 {b.start_line}", file=sys.stderr)
    print("原始 block:", file=sys.stderr)
    for line in b.inner_text.splitlines():
        print(f"  {line}", file=sys.stderr)
    for i, out in enumerate(exc.pass_outputs, 1):
        print(f"pass {i} stdout:", file=sys.stderr)
        for line in out.splitlines():
            print(f"  {line}", file=sys.stderr)
    print(f"失敗原因：{exc.reason}", file=sys.stderr)
    if exc.last_stderr:
        print("stderr:", file=sys.stderr)
        for line in exc.last_stderr.splitlines():
            print(f"  {line}", file=sys.stderr)
