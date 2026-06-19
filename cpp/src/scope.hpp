#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Manages three JSON files for §4.1 scope dicts and §10.5 snapshot/restore.
class ScopeStore {
public:
    static ScopeStore create(bool world_accessible = false);
    explicit ScopeStore(fs::path tmpdir, bool world_accessible = false);
    ~ScopeStore();

    ScopeStore(const ScopeStore&) = delete;
    ScopeStore& operator=(const ScopeStore&) = delete;
    ScopeStore(ScopeStore&&) = default;

    const fs::path& global_path() const { return _global_path; }
    const fs::path& file_path()   const { return _file_path; }
    const fs::path& block_path()  const { return _block_path; }

    void open_file();
    void open_block();
    void close_block();
    void close_file();

    void snapshot();
    void commit();
    void restore();

    void cleanup();

private:
    void publish(const fs::path& path) const;  // chmod 0666 when world-accessible

    fs::path _tmpdir;
    bool     _world{false};
    fs::path _global_path;
    fs::path _file_path;
    fs::path _block_path;

    // stack of (global_bytes, file_bytes, block_bytes)
    struct Snap { std::string g, f, b; };
    std::vector<Snap> _snapshots;
};
