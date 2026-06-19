#include "expander.hpp"
#include "executor.hpp"
#include "indent.hpp"
#include "utils.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
static fs::path write_tmp_file(const std::string& content, bool /*world_readable*/ = false) {
    char tmp[MAX_PATH];
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    GetTempFileNameA(dir, "cg", 0, tmp);
    write_file(fs::path(tmp), content);
    return tmp;
}
#else
#include <sys/stat.h>
#include <unistd.h>
// world_readable: a dropped-privilege block (run_as_user) reads these via
// CODEGEN_ORIGIN_* and would otherwise be denied by the 0600 default.
static fs::path write_tmp_file(const std::string& content, bool world_readable = false) {
    char tmpl[] = "/tmp/codegen_tmp_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) throw std::runtime_error("mkstemp failed");
    if (world_readable) fchmod(fd, 0644);
    if (write(fd, content.data(), content.size()) < 0) {
        close(fd);
        throw std::runtime_error("write tmp failed");
    }
    close(fd);
    return tmpl;
}
#endif

// Replace lines [start_line-1 .. end_line-1] (0-indexed) with replacement.
static std::string splice(const std::string& region,
                           int start_line, int end_line,
                           const std::string& replacement) {
    auto lines = splitlines(region, true);
    std::string result;
    for (int i = 0; i < (int)lines.size(); i++) {
        if (i < start_line - 1 || i >= end_line)
            result += lines[i];
        else if (i == start_line - 1)
            result += replacement;
    }
    return result;
}

static std::string keep_as_comment(const std::string& original_raw,
                                    const Block& block,
                                    const std::string& output) {
    const CommentSyntax& cs = block.comment_syntax;
    auto lines = splitlines(original_raw, false);
    std::string commented;
    if (cs.is_block()) {
        commented += block.indent + "// === codegen source (kept) ===\n";
        for (auto& l : lines)
            commented += block.indent + "// " + l + "\n";
        commented += block.indent + "// === end ===\n";
    } else {
        commented += block.indent + cs.open + " === codegen source (kept) ===\n";
        for (auto& l : lines)
            commented += block.indent + cs.open + " " + l + "\n";
        commented += block.indent + cs.open + " === end ===\n";
    }
    return commented + output;
}

std::string expand_block(
    const Block& block,
    const Config& parent_cfg,
    ScopeStore& scope,
    const RunContext& ctx)
{
    Config block_cfg = merge_from_strings(
        parent_cfg, block.pragma, /*is_pragma=*/true);

    scope.open_block();
    scope.snapshot();
    bool snapshot_done = false;

    const bool world = block_cfg.run_as_user.has_value();
    fs::path origin_block_path = write_tmp_file(block.inner_text, world);

    try {
        std::string region = block.raw_block_text;
        double elapsed_total = 0.0;
        std::vector<std::string> pass_outputs;
        int pass_idx = 0;
        std::string original_raw = block.raw_block_text;

        while (pass_idx < block_cfg.max_passes) {
            auto inner_blocks = find_top_level_blocks(
                region, block.file_path,
                {block_cfg.markers.first, block_cfg.markers.second},
                block.comment_syntax);

            if (inner_blocks.empty()) break; // stable

            for (auto& ib : inner_blocks) {
                fs::path ib_origin = write_tmp_file(ib.inner_text, world);

                RunContext ib_ctx{
                    ctx.invoke_cwd,
                    ctx.targets,
                    ctx.file_path,
                    ctx.origin_file_path,
                    ib_origin,
                };

                auto ib_env = build_env(block_cfg.extra_env, scope, ib_ctx);
                fs::path eff_cwd = block_cfg.cwd ? *block_cfg.cwd : ctx.invoke_cwd;

                auto [stdout_str, elapsed] = run_block(
                    ib, ib_env, eff_cwd,
                    block_cfg.max_pass_time,
                    pass_outputs,
                    block_cfg.run_as_user);

                std::error_code ec;
                fs::remove(ib_origin, ec);

                pass_outputs.push_back(stdout_str);
                elapsed_total += elapsed;

                if (elapsed_total > block_cfg.max_total_time) {
                    throw BlockFailure(ib, "timeout:total", pass_outputs);
                }

                std::string indented = apply_indent(
                    stdout_str, ib.indent, block_cfg.auto_indent);
                region = splice(region,
                                ib.start_line, ib.end_line,
                                indented);
            }
            pass_idx++;
        }

        // Check for remaining unexpanded blocks (max_passes exceeded)
        if (pass_idx == block_cfg.max_passes) {
            auto remaining = find_top_level_blocks(
                region, block.file_path,
                {block_cfg.markers.first, block_cfg.markers.second},
                block.comment_syntax);
            if (!remaining.empty()) {
                std::cerr << block.file_path.string() << ":" << block.start_line
                          << ": warning: max_passes=" << block_cfg.max_passes
                          << " reached; expansion may be incomplete\n";
            }
        }

        scope.commit();
        snapshot_done = true;

        if (block_cfg.keep_as_comment)
            region = keep_as_comment(original_raw, block, region);

        std::error_code ec;
        fs::remove(origin_block_path, ec);
        scope.close_block();
        return region;

    } catch (BlockFailure&) {
        scope.restore();
        snapshot_done = true;
        std::error_code ec;
        fs::remove(origin_block_path, ec);
        scope.close_block();
        throw;
    } catch (...) {
        if (!snapshot_done) scope.restore();
        std::error_code ec;
        fs::remove(origin_block_path, ec);
        scope.close_block();
        throw;
    }
}

void emit_failure(const BlockFailure& exc) {
    const Block& b = exc.block;
    std::cerr << "codegen: block 失敗 — " << b.file_path.string()
              << " 行 " << b.start_line << "\n";
    std::cerr << "原始 block:\n";
    for (auto& line : splitlines(b.inner_text))
        std::cerr << "  " << line << "\n";
    for (size_t i = 0; i < exc.pass_outputs.size(); i++) {
        std::cerr << "pass " << (i + 1) << " stdout:\n";
        for (auto& line : splitlines(exc.pass_outputs[i]))
            std::cerr << "  " << line << "\n";
    }
    std::cerr << "失敗原因：" << exc.reason << "\n";
    if (!exc.last_stderr.empty()) {
        std::cerr << "stderr:\n";
        for (auto& line : splitlines(exc.last_stderr))
            std::cerr << "  " << line << "\n";
    }
}

std::pair<std::string, bool> process_content(
    const std::string& content,
    const Config& cfg,
    ScopeStore& scope,
    const RunContext& ctx)
{
    auto cs_opt = lookup_syntax(ctx.file_path, &cfg.comment_syntax_overrides);
    if (!cs_opt) return {content, false};

    auto blocks = find_top_level_blocks(
        content, ctx.file_path, cfg.markers, *cs_opt);
    if (blocks.empty()) return {content, false};

    std::string result = content;
    int line_offset = 0;
    bool had_failure = false;

    for (auto& block : blocks) {
        Block adjusted = block;
        adjusted.start_line += line_offset;
        adjusted.end_line   += line_offset;

        std::string replacement;
        try {
            replacement = expand_block(adjusted, cfg, scope, ctx);
        } catch (BlockFailure& exc) {
            emit_failure(exc);
            had_failure = true;
            if (cfg.on_error == "abort_all")
                throw AbortAll(std::move(exc));
            if (cfg.on_error == "abort_file")
                throw;
            // continue: leave original block text
            continue;
        }

        // Preserve trailing newline of original block
        if (ends_with(adjusted.raw_block_text, "\n") &&
            !replacement.empty() && !ends_with(replacement, "\n"))
            replacement += "\n";

        int old_lines = (int)splitlines(adjusted.raw_block_text).size();
        int new_lines = (int)splitlines(replacement).size();
        line_offset += new_lines - old_lines;

        result = splice(result,
                        adjusted.start_line, adjusted.end_line,
                        replacement);
    }

    return {result, had_failure};
}
