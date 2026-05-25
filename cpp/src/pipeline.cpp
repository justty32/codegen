#include "pipeline.hpp"
#include "backup.hpp"
#include "comment_syntax.hpp"
#include "env.hpp"
#include "errors.hpp"
#include "expander.hpp"
#include "parser.hpp"
#include "utils.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
static fs::path write_tmp(const std::string& content) {
    char dir[MAX_PATH], tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    GetTempFileNameA(dir, "cg", 0, tmp);
    write_file(fs::path(tmp), content);
    return tmp;
}
#else
#include <unistd.h>
static fs::path write_tmp(const std::string& content) {
    char tmpl[] = "/tmp/codegen_origin_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) throw std::runtime_error("mkstemp failed");
    if (write(fd, content.data(), content.size()) < 0) {
        close(fd);
        throw std::runtime_error("write tmp failed");
    }
    close(fd);
    return tmpl;
}
#endif

std::pair<std::string, bool> process_file(
    const fs::path& path,
    const Config& base_cfg,
    ScopeStore& scope,
    const std::vector<fs::path>& all_targets,
    const fs::path& invoke_cwd,
    const std::string& run_id,
    bool dry_run,
    RunState* state)
{
    auto cs_opt = lookup_syntax(path, &base_cfg.comment_syntax_overrides);
    if (!cs_opt) return {"", false};

    std::string content = read_file(path);

    // File-level pragma
    auto file_pragma = parse_file_pragma(content, *cs_opt, base_cfg.markers);
    Config file_cfg = file_pragma.empty()
        ? base_cfg
        : merge_from_strings(base_cfg, file_pragma, /*is_pragma=*/true);

    // Write CODEGEN_ORIGIN_FILE snapshot
    fs::path origin_file  = write_tmp(content);
    fs::path origin_block = write_tmp(""); // placeholder; overridden per-block inside expander

    RunContext run_ctx{
        invoke_cwd,
        all_targets,
        path,
        origin_file,
        origin_block,
    };

    scope.open_file();
    bool had_failure = false;
    std::string result = content;

    try {
        // Backup before any mutation
        if (base_cfg.backup) {
            fs::path bdir = file_cfg.backup_dir;
            snapshot_file(path, bdir, run_id);
        }

        auto [final_content, failed] = process_content(content, file_cfg, scope, run_ctx);
        result = final_content;
        had_failure = failed;

        if (dry_run) {
            std::cout << result;
        } else {
            write_file(path, result);
        }

    } catch (AbortAll&) {
        scope.close_file();
        std::error_code ec;
        fs::remove(origin_file, ec);
        fs::remove(origin_block, ec);
        throw;
    } catch (BlockFailure&) {
        had_failure = true;
        // abort_file path
    }

    scope.close_file();
    std::error_code ec;
    fs::remove(origin_file, ec);
    fs::remove(origin_block, ec);

    return {result, had_failure};
}

int run_all(const std::vector<fs::path>& targets,
             const Config& cfg,
             const std::string& run_id,
             bool dry_run,
             RunState* state)
{
    ScopeStore scope = ScopeStore::create();
    fs::path invoke_cwd = fs::current_path();
    bool had_any_failure = false;

    try {
        for (int i = 0; i < (int)targets.size(); i++) {
            const fs::path& path = targets[i];
            if (state) {
                state->target_idx    = i + 1;
                state->current_file  = path;
                state->block_ordinal = std::nullopt;
            }

            try {
                auto [_content, failed] = process_file(
                    path, cfg, scope, targets,
                    invoke_cwd, run_id, dry_run, state);
                if (failed) had_any_failure = true;
            } catch (AbortAll& exc) {
                if (state)
                    state->last_failure_reason = exc.failure.reason;
                scope.cleanup();
                return EXIT_ABORT_ALL;
            } catch (BlockFailure&) {
                had_any_failure = true;
                // abort_file: continue to next file
            }
        }
    } catch (...) {
        scope.cleanup();
        throw;
    }

    scope.cleanup();
    return had_any_failure ? EXIT_BLOCK_FAILURE : EXIT_OK;
}
