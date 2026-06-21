#include "config.hpp"
#include "comment_syntax.hpp"
#include "utils.hpp"

#include <toml++/toml.hpp>

#include <cstdlib>
#include <set>
#include <sstream>

#ifndef _WIN32
#include <unistd.h>   // for environ on POSIX
#endif

static const std::set<std::string> SCAN_ONLY_FIELDS = {
    "extensions", "include", "exclude", "scan_all", "comment_syntax_overrides"
};
// run_as_user is security-sensitive (privilege selection) and must never be
// settable from within a processed file.
static const std::set<std::string> PRAGMA_FORBIDDEN = {
    "extensions", "include", "exclude", "scan_all", "comment_syntax_overrides",
    "extra_env", "run_as_user"
};
static const std::set<std::string> ALL_FIELDS = {
    "extensions", "include", "exclude", "scan_all", "comment_syntax_overrides",
    "markers", "max_passes", "max_total_time", "max_pass_time",
    "keep_as_comment", "auto_indent", "backup", "backup_dir",
    "on_error", "cwd", "extra_env", "run_as_user"
};

void Config::validate() const {
    if (markers.first.empty() || markers.second.empty())
        throw ConfigError("markers: both start and end must be non-empty");
    if (on_error != "continue" && on_error != "abort_file" && on_error != "abort_all")
        throw ConfigError("on_error: must be continue|abort_file|abort_all, got: " + on_error);
    if (max_passes < 1)
        throw ConfigError("max_passes must be >= 1");
    if (max_total_time <= 0 || max_pass_time <= 0)
        throw ConfigError("max_total_time and max_pass_time must be > 0");
}

static bool parse_bool(const std::string& name, const std::string& val) {
    std::string v = to_lower(strip(val));
    if (v == "true" || v == "1" || v == "yes" || v == "on")  return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    throw ConfigError(name + ": cannot interpret '" + val + "' as bool");
}

static std::vector<std::string> parse_str_list(const std::string& /*name*/,
                                                const std::string& val) {
    auto parts = split(val, ',');
    std::vector<std::string> out;
    for (auto& p : parts) {
        std::string s = strip(p);
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

static std::pair<std::string, std::string> parse_markers(const std::string& name,
                                                          const std::string& val) {
    auto parts = parse_str_list(name, val);
    if (parts.size() != 2)
        throw ConfigError(name + ": must have exactly 2 items, got: " + val);
    return {parts[0], parts[1]};
}

static void apply_one(Config& cfg, const std::string& key, const std::string& val) {
    if (key == "extensions")               cfg.extensions = parse_str_list(key, val);
    else if (key == "include")             cfg.include    = parse_str_list(key, val);
    else if (key == "exclude")             cfg.exclude    = parse_str_list(key, val);
    else if (key == "scan_all")            cfg.scan_all   = parse_bool(key, val);
    else if (key == "markers")             cfg.markers    = parse_markers(key, val);
    else if (key == "max_passes")          cfg.max_passes = std::stoi(val);
    else if (key == "max_total_time")      cfg.max_total_time = std::stod(val);
    else if (key == "max_pass_time")       cfg.max_pass_time  = std::stod(val);
    else if (key == "keep_as_comment")     cfg.keep_as_comment = parse_bool(key, val);
    else if (key == "auto_indent")         cfg.auto_indent     = parse_bool(key, val);
    else if (key == "backup")              cfg.backup          = parse_bool(key, val);
    else if (key == "backup_dir")          cfg.backup_dir      = fs::path(val);
    else if (key == "on_error") {
        if (val != "continue" && val != "abort_file" && val != "abort_all")
            throw ConfigError("on_error: invalid value: " + val);
        cfg.on_error = val;
    }
    else if (key == "cwd")                 cfg.cwd = fs::path(val);
    else if (key == "run_as_user") {
        std::string v = strip(val);
        if (v.empty()) cfg.run_as_user = std::nullopt;
        else           cfg.run_as_user = v;
    }
    else
        throw ConfigError("unknown setting: " + key);
}

Config merge_from_strings(const Config& base,
                           const std::map<std::string, std::string>& overrides,
                           bool is_pragma)
{
    if (is_pragma) {
        for (auto& [k, _] : overrides) {
            if (PRAGMA_FORBIDDEN.count(k))
                throw ConfigError("cannot set '" + k + "' via pragma");
        }
    }
    Config cfg = base;
    for (auto& [k, v] : overrides)
        apply_one(cfg, k, v);
    return cfg;
}

// ---------- TOML loading ----------

static void apply_toml_node(Config& cfg, const std::string& key, const toml::node& node) {
    if (key == "extensions" || key == "include" || key == "exclude") {
        std::vector<std::string>& target =
            (key == "extensions") ? cfg.extensions :
            (key == "include")    ? cfg.include    : cfg.exclude;
        target.clear();
        if (auto* arr = node.as_array()) {
            for (auto& el : *arr)
                if (auto* sv = el.as_string())
                    target.push_back(sv->get());
        }
    } else if (key == "scan_all") {
        if (auto* b = node.as_boolean()) cfg.scan_all = b->get();
    } else if (key == "markers") {
        if (auto* arr = node.as_array(); arr && arr->size() == 2) {
            cfg.markers = {(*arr)[0].value_or<std::string>(""),
                           (*arr)[1].value_or<std::string>("")};
        } else if (auto* sv = node.as_string()) {
            cfg.markers = parse_markers("markers", sv->get());
        }
    } else if (key == "max_passes") {
        if (auto* iv = node.as_integer()) cfg.max_passes = (int)iv->get();
    } else if (key == "max_total_time") {
        if (auto* fv = node.as_floating_point()) cfg.max_total_time = fv->get();
        else if (auto* iv = node.as_integer())   cfg.max_total_time = (double)iv->get();
    } else if (key == "max_pass_time") {
        if (auto* fv = node.as_floating_point()) cfg.max_pass_time = fv->get();
        else if (auto* iv = node.as_integer())   cfg.max_pass_time = (double)iv->get();
    } else if (key == "keep_as_comment") {
        if (auto* b = node.as_boolean()) cfg.keep_as_comment = b->get();
    } else if (key == "auto_indent") {
        if (auto* b = node.as_boolean()) cfg.auto_indent = b->get();
    } else if (key == "backup") {
        if (auto* b = node.as_boolean()) cfg.backup = b->get();
    } else if (key == "backup_dir") {
        if (auto* sv = node.as_string()) cfg.backup_dir = fs::path(sv->get());
    } else if (key == "on_error") {
        if (auto* sv = node.as_string()) {
            std::string v = sv->get();
            if (v != "continue" && v != "abort_file" && v != "abort_all")
                throw ConfigError("on_error: invalid value: " + v);
            cfg.on_error = v;
        }
    } else if (key == "cwd") {
        if (auto* sv = node.as_string()) cfg.cwd = fs::path(sv->get());
    } else if (key == "run_as_user") {
        if (auto* sv = node.as_string()) {
            std::string v = strip(sv->get());
            if (v.empty()) cfg.run_as_user = std::nullopt;
            else           cfg.run_as_user = v;
        }
    }
}

std::map<std::string, std::string> load_toml_strings(const fs::path& toml_path) {
    // We don't use string-map here; apply directly in resolve_initial.
    // This function exists for the env-var layer which uses strings.
    // Actually, TOML has typed values, so we return an empty string-map and
    // handle TOML loading separately. Keep this for future use.
    (void)toml_path;
    return {};
}

static Config apply_toml_file(const Config& base, const fs::path& toml_path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(toml_path.string());
    } catch (const toml::parse_error& e) {
        throw ConfigError("invalid TOML in " + toml_path.string() + ": " + e.what());
    }

    Config cfg = base;

    // Collect keys to skip after special-section extraction
    std::set<std::string> skip_keys;

    // [env] table → extra_env
    if (auto env_node = tbl["env"]; env_node.as_table()) {
        for (auto& [k, v] : *env_node.as_table()) {
            if (auto* sv = v.as_string())
                cfg.extra_env[std::string(k.str())] = sv->get();
        }
        skip_keys.insert("env");
    }

    // [comment_syntax] table
    if (auto cs_node = tbl["comment_syntax"]; cs_node.as_table()) {
        for (auto& [k, v] : *cs_node.as_table()) {
            if (auto* sv = v.as_string())
                cfg.comment_syntax_overrides[std::string(k.str())] = sv->get();
        }
        skip_keys.insert("comment_syntax");
    }

    for (auto& [k, v] : tbl) {
        std::string key = std::string(k.str());
        if (skip_keys.count(key)) continue;
        try {
            apply_toml_node(cfg, key, v);
        } catch (const std::exception& e) {
            throw ConfigError(toml_path.string() + ": " + e.what());
        }
    }
    return cfg;
}

std::map<std::string, std::string> config_from_env() {
    static const std::set<std::string> RESERVED = {
        "CODEGEN_GLOBAL", "CODEGEN_FILE", "CODEGEN_BLOCK",
        "CODEGEN_ORIGIN_FILE", "CODEGEN_ORIGIN_BLOCK",
        "CODEGEN_INVOKE_CWD", "CODEGEN_TARGETS", "CODEGEN_FILE_PATH"
    };
    std::map<std::string, std::string> out;
    extern char** environ;
    for (char** ep = environ; ep && *ep; ep++) {
        std::string entry(*ep);
        if (!starts_with(entry, "CODEGEN_")) continue;
        auto eq = entry.find('=');
        if (eq == std::string::npos) continue;
        std::string key = entry.substr(0, eq);
        if (RESERVED.count(key)) continue;
        std::string field = to_lower(key.substr(8)); // strip "CODEGEN_"
        if (ALL_FIELDS.count(field))
            out[field] = entry.substr(eq + 1);
    }
    return out;
}

std::optional<fs::path> find_folder_toml(const fs::path& start,
                                          const std::optional<fs::path>& explicit_path) {
    if (explicit_path) {
        if (!fs::exists(*explicit_path))
            throw ConfigError("--config path not found: " + explicit_path->string());
        return explicit_path;
    }
    fs::path cur = fs::absolute(start);
    if (fs::is_regular_file(cur)) cur = cur.parent_path();
    while (true) {
        auto candidate = cur / "codegen.toml";
        if (fs::is_regular_file(candidate)) return candidate;
        if (fs::exists(cur / ".git"))       return std::nullopt;
        auto parent = cur.parent_path();
        if (parent == cur) return std::nullopt;
        cur = parent;
    }
}

fs::path find_project_root(const fs::path& start) {
    fs::path cur = fs::absolute(start);
    if (fs::is_regular_file(cur)) cur = cur.parent_path();
    while (true) {
        if (fs::exists(cur / "codegen.toml") || fs::exists(cur / ".git"))
            return cur;
        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return fs::current_path();
}

Config resolve_initial(const std::map<std::string, std::string>& cli_overrides,
                        const fs::path& start,
                        const std::optional<fs::path>& config_path) {
    Config cfg;

    // Layer 1: env vars
    auto env_over = config_from_env();
    if (!env_over.empty())
        cfg = merge_from_strings(cfg, env_over);

    // Layer 2: toml file
    auto toml_path = find_folder_toml(start, config_path);
    if (toml_path)
        cfg = apply_toml_file(cfg, *toml_path);

    // Layer 3: CLI overrides
    if (!cli_overrides.empty())
        cfg = merge_from_strings(cfg, cli_overrides);

    // Ensure backup_dir is absolute (weakly_canonical: path need not exist yet)
    if (!cfg.backup_dir.is_absolute()) {
        fs::path root = find_project_root(start);
        cfg.backup_dir = fs::weakly_canonical(root / cfg.backup_dir);
    }

    cfg.validate();
    return cfg;
}
