#pragma once
#include "parser.hpp"
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// Execute a single block subprocess (POSIX only).
// Returns (stdout, elapsed_seconds).
//
// The child leads its own session/process group (setsid), so on timeout the
// whole group is SIGKILL'd — background children the script spawned (`&`,
// forked daemons) are reaped instead of orphaned.
//
// When run_as_user is set, the child drops to that user's identity
// (setgroups/setgid/setuid) before exec; this requires codegen itself to be
// privileged (root).
//
// Raises BlockFailure on non-zero exit, timeout, unknown/denied user, or
// I/O error.
std::pair<std::string, double> run_block(
    const Block& block,
    const std::map<std::string, std::string>& env,
    const fs::path& cwd,
    double max_pass_time,
    std::vector<std::string>& pass_outputs,
    const std::optional<std::string>& run_as_user = std::nullopt);
