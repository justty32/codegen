#include "scanner.hpp"
#include "comment_syntax.hpp"
#include "utils.hpp"

#include <algorithm>
#include <functional>
#include <set>

// Glob match matching Python's fnmatch.fnmatch behaviour:
// '*' matches any sequence of characters (including '/').
// '?' matches exactly one character.
static bool glob_match(const std::string& pattern, const std::string& path) {
    size_t plen = pattern.size(), slen = path.size();

    std::function<bool(size_t, size_t)> match = [&](size_t p, size_t s) -> bool {
        while (p < plen) {
            if (pattern[p] == '*') {
                p++;
                for (size_t i = s; i <= slen; i++)
                    if (match(p, i)) return true;
                return false;
            } else if (pattern[p] == '?') {
                if (s >= slen) return false;
                p++; s++;
            } else {
                if (s >= slen || pattern[p] != path[s]) return false;
                p++; s++;
            }
        }
        return s == slen;
    };
    return match(0, 0);
}

static bool matches_any(const fs::path& file_path,
                         const std::vector<std::string>& patterns,
                         const fs::path& root) {
    std::error_code ec;
    fs::path rel = fs::relative(file_path, root, ec);
    if (ec) return false;
    // Convert to forward-slash string for pattern matching
    std::string rel_str;
    for (auto& part : rel) {
        if (!rel_str.empty()) rel_str += '/';
        rel_str += part.string();
    }
    std::string name = file_path.filename().string();
    for (auto& pat : patterns) {
        if (glob_match(pat, rel_str) || glob_match(pat, name))
            return true;
    }
    return false;
}

static const std::set<std::string> IGNORE_DIRS = {
    ".git", "__pycache__", ".pytest_cache", ".codegen-backup"
};

static void collect_dir(const fs::path& directory,
                         const Config& cfg,
                         const fs::path& root,
                         const std::vector<std::string>& effective_extensions,
                         const std::optional<fs::path>& backup_dir_abs,
                         std::vector<fs::path>& results)
{
    std::error_code ec;
    std::vector<fs::directory_entry> children;
    for (auto& entry : fs::directory_iterator(directory, ec))
        children.push_back(entry);
    std::sort(children.begin(), children.end(),
              [](const auto& a, const auto& b) {
                  return a.path().filename() < b.path().filename();
              });

    for (auto& entry : children) {
        const fs::path& child = entry.path();
        if (entry.is_directory()) {
            std::string name = child.filename().string();
            if (IGNORE_DIRS.count(name)) continue;
            if (backup_dir_abs) {
                std::error_code ec2;
                if (fs::equivalent(child, *backup_dir_abs, ec2) && !ec2) continue;
            }
            collect_dir(child, cfg, root, effective_extensions, backup_dir_abs, results);
        } else if (entry.is_regular_file()) {
            if (!cfg.exclude.empty() && matches_any(child, cfg.exclude, root))
                continue;
            std::string ext = to_lower(child.extension().string());
            if (cfg.scan_all) {
                results.push_back(child);
            } else if (!cfg.include.empty() && matches_any(child, cfg.include, root)) {
                results.push_back(child);
            } else {
                auto it = std::find(effective_extensions.begin(),
                                    effective_extensions.end(), ext);
                if (it != effective_extensions.end())
                    results.push_back(child);
            }
        }
    }
}

std::vector<fs::path> collect_files(const std::vector<fs::path>& targets,
                                     const Config& cfg) {
    std::vector<std::string> effective_extensions =
        cfg.extensions.empty() ? default_extensions() : cfg.extensions;

    std::optional<fs::path> backup_dir_abs;
    if (cfg.backup) {
        std::error_code ec;
        backup_dir_abs = fs::absolute(cfg.backup_dir);
    }

    std::vector<fs::path> result;
    for (auto& target : targets) {
        fs::path t = fs::absolute(target);
        std::error_code ec;
        if (fs::is_regular_file(t, ec)) {
            result.push_back(t);
        } else if (fs::is_directory(t, ec)) {
            collect_dir(t, cfg, t, effective_extensions, backup_dir_abs, result);
        } else {
            throw ConfigError("target not found: " + t.string());
        }
    }
    return result;
}
