#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct RunState {
    std::vector<fs::path>  targets;
    int                    target_idx{0};    // 1-based
    std::optional<fs::path> current_file;
    std::optional<int>     block_ordinal;    // 1-based
    std::optional<int>     block_start_line;
    std::optional<std::string> last_failure_reason;
};

void print_plan(const std::vector<fs::path>& targets);

void report_interrupt(const RunState& state, const std::string& kind);
