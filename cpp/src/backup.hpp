#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Generate a run ID (UTC timestamp).
std::string make_run_id();

// Copy source into backup_dir/<rel-path>/<run_id>/<filename>.
// Returns backup path, or nullopt if something prevents it.
std::optional<fs::path> snapshot_file(const fs::path& source,
                                       const fs::path& backup_dir,
                                       const std::string& run_id);

// List available backup timestamps for source under backup_dir.
std::vector<std::string> list_timestamps(const fs::path& source,
                                          const fs::path& backup_dir);
