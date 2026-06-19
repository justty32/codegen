#pragma once
#include "errors.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Config {
    std::vector<std::string>              extensions{};
    std::vector<std::string>              include{};
    std::vector<std::string>              exclude{};
    bool                                  scan_all{false};
    std::map<std::string, std::string>    comment_syntax_overrides{};

    std::pair<std::string, std::string>   markers{"CODEGEN_START", "CODEGEN_END"};

    int    max_passes{1};
    double max_total_time{5.0};
    double max_pass_time{5.0};

    bool keep_as_comment{false};
    bool auto_indent{true};

    bool    backup{true};
    fs::path backup_dir{".codegen-backup"};

    std::string on_error{"continue"};  // continue | abort_file | abort_all

    std::optional<fs::path>            cwd{};
    std::map<std::string, std::string> extra_env{};
    std::optional<std::string>         run_as_user{};

    void validate() const;
};

// Apply a pragma/overrides map (string→string) onto base, returning new Config.
// scan_only_fields are rejected if in_pragma=true.
Config merge_from_strings(const Config& base,
                          const std::map<std::string, std::string>& overrides,
                          bool is_pragma = false);

// Load codegen.toml into an overrides map.
std::map<std::string, std::string> load_toml_strings(const fs::path& toml_path);

// Read CODEGEN_* environment variables as overrides.
std::map<std::string, std::string> config_from_env();

// Find codegen.toml: walk up from start, stop at .git or fs root.
std::optional<fs::path> find_folder_toml(const fs::path& start,
                                         const std::optional<fs::path>& explicit_path = {});

// Find project root (dir containing codegen.toml or .git).
fs::path find_project_root(const fs::path& start);

// Build initial Config from defaults + env + toml + cli_overrides.
Config resolve_initial(const std::map<std::string, std::string>& cli_overrides,
                       const fs::path& start,
                       const std::optional<fs::path>& config_path = {});
