#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int run_rollback(const std::vector<fs::path>& paths,
                  const std::optional<std::string>& timestamp,
                  bool list_only,
                  const fs::path& backup_dir);
