#pragma once
#include "parser.hpp"
#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// Execute a single block subprocess (POSIX only).
// Returns (stdout, elapsed_seconds).
// Raises BlockFailure on non-zero exit, timeout, or I/O error.
std::pair<std::string, double> run_block(
    const Block& block,
    const std::map<std::string, std::string>& env,
    const fs::path& cwd,
    double max_pass_time,
    std::vector<std::string>& pass_outputs);
