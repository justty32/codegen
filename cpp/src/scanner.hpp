#pragma once
#include "config.hpp"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// Expand targets to a list of files to process (§9).
std::vector<fs::path> collect_files(const std::vector<fs::path>& targets,
                                     const Config& cfg);
