#pragma once
#include "scope.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct RunContext {
    fs::path              invoke_cwd;
    std::vector<fs::path> targets;
    fs::path              file_path;
    fs::path              origin_file_path;
    fs::path              origin_block_path;
};

// Assemble subprocess environment (§4).
// Order: current process env → cfg.extra_env → codegen runtime vars.
std::map<std::string, std::string> build_env(
    const std::map<std::string, std::string>& cfg_extra_env,
    const ScopeStore& scope,
    const RunContext& ctx);
