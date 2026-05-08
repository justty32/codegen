from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class RunState:
    targets: list[Path] = field(default_factory=list)
    target_idx: int = 0          # 1-based
    current_file: Path | None = None
    block_ordinal: int | None = None   # 1-based, top-level block index in file
    block_start_line: int | None = None
    last_failure_reason: str | None = None


_state: RunState = RunState()


def reset(targets: list[Path]) -> RunState:
    global _state
    _state = RunState(targets=targets)
    return _state


def current() -> RunState:
    return _state


def print_plan(targets: list[Path]) -> None:
    n = len(targets)
    print(f"codegen: 將依序處理 {n} 個目標：")
    for i, t in enumerate(targets, 1):
        print(f"  [{i}/{n}] {t}")


def report_interrupt(state: RunState, *, kind: str) -> None:
    n = len(state.targets)
    if kind == "sigint":
        print("\n^C", file=sys.stderr)
        print("codegen: 已中止。當前處理至：", file=sys.stderr)
    else:  # abort_all
        print(f"\ncodegen: 已中止（abort_all 觸發）。當前處理至：", file=sys.stderr)

    if state.target_idx:
        print(f"  目標：[{state.target_idx}/{n}] {state.targets[state.target_idx - 1]}", file=sys.stderr)
    if state.current_file:
        print(f"  檔案：{state.current_file}", file=sys.stderr)
    if state.block_ordinal is not None:
        print(f"  Block：第 {state.block_ordinal} 個（行 {state.block_start_line}）", file=sys.stderr)
    if kind == "abort_all" and state.last_failure_reason:
        print(f"  失敗原因：{state.last_failure_reason}", file=sys.stderr)
