from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from codegen.parser import Block


EXIT_OK = 0
EXIT_BLOCK_FAILURE = 1
EXIT_ABORT_ALL = 2
EXIT_STARTUP = 3
EXIT_SIGINT = 130


class CodegenError(Exception):
    pass


class ConfigError(CodegenError):
    pass


class ParseError(CodegenError):
    def __init__(self, message: str, *, file_path: Path | None = None, line: int | None = None) -> None:
        super().__init__(message)
        self.file_path = file_path
        self.line = line


@dataclass
class BlockFailure(CodegenError):
    block: "Block"
    reason: str
    pass_outputs: list[str] = field(default_factory=list)
    last_stderr: str = ""
    exit_code: int | None = None

    def __str__(self) -> str:
        return f"block failed at {self.block.file_path}:{self.block.start_line} ({self.reason})"


class AbortAll(CodegenError):
    def __init__(self, failure: BlockFailure) -> None:
        super().__init__(str(failure))
        self.failure = failure
