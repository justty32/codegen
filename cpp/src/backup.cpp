#include "backup.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>

std::string make_run_id() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_utc, "%Y%m%dT%H%M%SZ");
    return ss.str();
}

std::optional<fs::path> snapshot_file(const fs::path& source,
                                        const fs::path& backup_dir,
                                        const std::string& run_id) {
    std::error_code ec;
    fs::path src_abs  = fs::absolute(source);
    fs::path bdir_abs = fs::absolute(backup_dir);
    fs::path root     = bdir_abs.parent_path();

    fs::path rel;
    try {
        rel = fs::relative(src_abs, root);
    } catch (...) {
        rel = src_abs.filename();
    }

    fs::path dest_dir = bdir_abs / rel.parent_path() / run_id;
    fs::create_directories(dest_dir, ec);
    if (ec) return std::nullopt;

    fs::path dest = dest_dir / src_abs.filename();
    fs::copy_file(src_abs, dest,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) return std::nullopt;
    return dest;
}

std::vector<std::string> list_timestamps(const fs::path& source,
                                          const fs::path& backup_dir) {
    std::error_code ec;
    fs::path src_abs  = fs::absolute(source);
    fs::path bdir_abs = fs::absolute(backup_dir);
    fs::path root     = bdir_abs.parent_path();

    fs::path rel;
    try {
        rel = fs::relative(src_abs, root);
    } catch (...) {
        rel = src_abs.filename();
    }

    fs::path ts_dir = bdir_abs / rel.parent_path();
    if (!fs::exists(ts_dir, ec)) return {};

    std::vector<std::string> result;
    for (auto& entry : fs::directory_iterator(ts_dir, ec)) {
        if (!entry.is_directory()) continue;
        if (fs::exists(entry.path() / src_abs.filename()))
            result.push_back(entry.path().filename().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}
