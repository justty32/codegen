#include "rollback.hpp"
#include "backup.hpp"
#include "errors.hpp"

#include <algorithm>
#include <iostream>
#include <system_error>

static std::vector<fs::path> all_backed_up(const fs::path& backup_dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(backup_dir, ec)) return files;

    for (auto& entry : fs::recursive_directory_iterator(backup_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        try {
            fs::path rel = fs::relative(entry.path(), backup_dir);
            auto parts = rel;
            // Structure: rel_parent / timestamp / filename
            // Need at least 3 components: <dir>/<ts>/<file>
            std::vector<fs::path> comps;
            for (auto& p : rel) comps.push_back(p);
            if (comps.size() >= 3) {
                fs::path orig = backup_dir.parent_path();
                for (size_t i = 0; i + 2 < comps.size(); i++)
                    orig /= comps[i];
                orig /= comps.back();
                if (std::find(files.begin(), files.end(), orig) == files.end())
                    files.push_back(orig);
            }
        } catch (...) {}
    }
    return files;
}

int run_rollback(const std::vector<fs::path>& paths,
                  const std::optional<std::string>& timestamp,
                  bool list_only,
                  const fs::path& backup_dir)
{
    std::error_code ec;
    if (!fs::exists(backup_dir, ec)) {
        std::cerr << "codegen rollback: backup dir not found: "
                  << backup_dir.string() << "\n";
        return EXIT_STARTUP;
    }

    std::vector<fs::path> targets;
    if (paths.empty()) {
        targets = all_backed_up(backup_dir);
    } else {
        for (auto& p : paths)
            targets.push_back(fs::absolute(p));
    }

    if (list_only) {
        for (auto& t : targets) {
            auto tss = list_timestamps(t, backup_dir);
            if (!tss.empty()) {
                std::cout << t.string() << ":\n";
                for (auto& ts : tss)
                    std::cout << "  " << ts << "\n";
            }
        }
        return EXIT_OK;
    }

    bool had_failure = false;
    for (auto& t : targets) {
        auto tss = list_timestamps(t, backup_dir);
        if (tss.empty()) {
            std::cerr << "codegen rollback: no backup for " << t.string() << "\n";
            had_failure = true;
            continue;
        }

        std::string ts = timestamp ? *timestamp : tss.back();
        if (std::find(tss.begin(), tss.end(), ts) == tss.end()) {
            std::cerr << "codegen rollback: timestamp '" << ts
                      << "' not found for " << t.string() << "\n";
            had_failure = true;
            continue;
        }

        fs::path t_abs     = fs::absolute(t);
        fs::path bdir_abs  = fs::absolute(backup_dir);
        fs::path root      = bdir_abs.parent_path();

        fs::path rel;
        try {
            rel = fs::relative(t_abs, root);
        } catch (...) {
            rel = t_abs.filename();
        }

        fs::path src = bdir_abs / rel.parent_path() / ts / t_abs.filename();
        if (!fs::exists(src, ec)) {
            std::cerr << "codegen rollback: backup file missing: " << src.string() << "\n";
            had_failure = true;
            continue;
        }

        fs::copy_file(src, t, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "codegen rollback: failed to restore " << t.string()
                      << ": " << ec.message() << "\n";
            had_failure = true;
        }
    }

    return had_failure ? EXIT_BLOCK_FAILURE : EXIT_OK;
}
