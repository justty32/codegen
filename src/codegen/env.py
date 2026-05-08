from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

from codegen.scope import ScopeStore


@dataclass
class RunContext:
    invoke_cwd: Path
    targets: Sequence[Path]
    file_path: Path
    origin_file_path: Path        # temp file path containing pre-processing snapshot
    origin_block_path: Path       # temp file path containing original block content


def build_env(
    cfg_extra_env: Mapping[str, str],
    scope: ScopeStore,
    ctx: RunContext,
) -> dict[str, str]:
    """Assemble the subprocess environment for a block execution (§4).

    Order: os.environ → cfg.extra_env → runtime codegen vars.
    Later entries win.
    """
    env = dict(os.environ)

    for k, v in cfg_extra_env.items():
        env[k] = v

    env["CODEGEN_GLOBAL"] = str(scope.global_path)
    env["CODEGEN_FILE"] = str(scope.file_path)
    env["CODEGEN_BLOCK"] = str(scope.block_path)

    env["CODEGEN_ORIGIN_FILE"] = str(ctx.origin_file_path)
    env["CODEGEN_ORIGIN_BLOCK"] = str(ctx.origin_block_path)

    env["CODEGEN_INVOKE_CWD"] = str(ctx.invoke_cwd)
    env["CODEGEN_TARGETS"] = "\n".join(str(p) for p in ctx.targets)
    env["CODEGEN_FILE_PATH"] = str(ctx.file_path)

    return env
