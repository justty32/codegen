#include "path_mapper.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

PathMapper PathMapper::load() {
    PathMapper m;
    const char* env = std::getenv("CODEGEN_PATH_MAP");
    if (!env || !*env) return m;

    std::ifstream f(env);
    if (!f) return m; // silently ignore missing/unreadable file

    try {
        auto j = nlohmann::json::parse(f, nullptr, /*exceptions=*/true,
                                       /*ignore_comments=*/true);
        for (auto& [k, v] : j.items()) {
            if (v.is_string())
                m._table[std::string(k)] = v.get<std::string>();
        }
    } catch (...) {
        // Malformed JSON: ignore (caller will fall back to PATH)
    }
    return m;
}

std::string PathMapper::map(const std::string& posix_path) const {
    // Only handle absolute POSIX paths
    if (posix_path.empty() || posix_path[0] != '/') return "";

    auto slash = posix_path.rfind('/');
    // parent: everything up to the last slash (or "/" when slash == 0)
    std::string parent   = (slash > 0) ? posix_path.substr(0, slash) : "/";
    std::string basename = posix_path.substr(slash + 1);
    if (basename.empty()) return "";

    auto it = _table.find(parent);
    if (it == _table.end()) return "";

    // Compose: Windows_dir\basename.exe
    return (fs::path(it->second) / (basename + ".exe")).string();
}
