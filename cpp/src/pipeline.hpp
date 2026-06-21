#pragma once
#include "config.hpp"
#include "progress.hpp"
#include "scope.hpp"
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// Process a single file in-memory and optionally write back to disk.
// Returns (final_content, had_failure).
// Raises AbortAll when on_error=abort_all is triggered.
std::pair<std::string, bool> process_file(
    const fs::path& path,
    const Config& base_cfg,
    ScopeStore& scope,
    const std::vector<fs::path>& all_targets,
    const fs::path& invoke_cwd,
    const std::string& run_id,
    bool dry_run,
    RunState* state = nullptr);

// Process all files. Returns exit code.
int run_all(const std::vector<fs::path>& targets,
            const Config& cfg,
            const std::string& run_id,
            bool dry_run,
            RunState* state = nullptr);
