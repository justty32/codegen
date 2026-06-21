#pragma once
#include "comment_syntax.hpp"
#include "errors.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Block {
    fs::path  file_path;
    int       start_line;   // 1-based, line of the START marker
    int       end_line;     // 1-based, line of the END marker
    std::string indent;     // leading whitespace on the START marker line
    CommentSyntax comment_syntax;

    std::string raw_block_text; // full text start..end inclusive
    std::string inner_text;     // shebang + pragma + body (comment wrapper stripped)
    std::string shebang;        // may be empty (means use default python3)
    std::map<std::string, std::string> pragma;
    std::string body;
};

struct BlockFailure : CodegenError {
    Block block;
    std::string reason;
    std::vector<std::string> pass_outputs;
    std::string last_stderr;
    std::optional<int> exit_code;

    BlockFailure(Block b, std::string r,
                 std::vector<std::string> outputs = {},
                 std::string stderr_str = {},
                 std::optional<int> code = std::nullopt)
        : CodegenError("block failed: " + r),
          block(std::move(b)),
          reason(std::move(r)),
          pass_outputs(std::move(outputs)),
          last_stderr(std::move(stderr_str)),
          exit_code(code)
    {}
};

struct AbortAll : CodegenError {
    BlockFailure failure;
    explicit AbortAll(BlockFailure f)
        : CodegenError(std::string(f.what())), failure(std::move(f)) {}
};

// Parse `k=v k=v ...` tokens after `codegen:`.
std::map<std::string, std::string> parse_pragma_tokens(const std::string& text);

// Split inner_text into (shebang, pragma, body).
struct BlockHeader {
    std::string shebang;
    std::map<std::string, std::string> pragma;
    std::string body;
};
BlockHeader parse_block_header(const std::string& inner_text);

// Find all top-level codegen blocks in content.
std::vector<Block> find_top_level_blocks(
    const std::string& content,
    const fs::path& file_path,
    const std::pair<std::string, std::string>& markers,
    const CommentSyntax& cs);

// Find the file-level `codegen:` pragma (first comment block at top of file).
std::map<std::string, std::string> parse_file_pragma(
    const std::string& content,
    const CommentSyntax& cs,
    const std::pair<std::string, std::string>& markers);
