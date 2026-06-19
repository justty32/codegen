#include "scope.hpp"
#include "utils.hpp"

#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static fs::path make_tmpdir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetTempPathA(MAX_PATH, buf);
    std::string base = std::string(buf) + "codegen_scope_";
    // crude: use PID
    base += std::to_string(GetCurrentProcessId());
    fs::create_directories(base);
    return base;
#else
    char tmpl[] = "/tmp/codegen_scope_XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) throw std::runtime_error("mkdtemp failed");
    return dir;
#endif
}

ScopeStore ScopeStore::create(bool world_accessible) {
    return ScopeStore(make_tmpdir(), world_accessible);
}

ScopeStore::ScopeStore(fs::path tmpdir, bool world_accessible)
    : _tmpdir(std::move(tmpdir)), _world(world_accessible) {
    _global_path = _tmpdir / "global.json";
    _file_path   = _tmpdir / "file.json";
    _block_path  = _tmpdir / "block.json";
#ifndef _WIN32
    if (_world) {
        // mkdtemp is 0700; a dropped-privilege block must be able to traverse
        // the dir to reach the scope JSON files it reads/writes.
        ::chmod(_tmpdir.c_str(), 0777);
    }
#endif
    write_file(_global_path, "{}");
    publish(_global_path);
}

ScopeStore::~ScopeStore() {
    // Don't cleanup automatically — caller decides (ScopeStore::cleanup).
}

void ScopeStore::publish(const fs::path& path) const {
#ifndef _WIN32
    // Make path read/writable by a dropped-privilege block (run_as_user).
    if (_world) ::chmod(path.c_str(), 0666);
#else
    (void)path;
#endif
}

void ScopeStore::open_file() {
    write_file(_file_path, "{}");
    publish(_file_path);
}

void ScopeStore::open_block() {
    write_file(_block_path, "{}");
    publish(_block_path);
}

void ScopeStore::close_block() {
    if (fs::exists(_block_path))
        write_file(_block_path, "{}");
}

void ScopeStore::close_file() {
    if (fs::exists(_file_path))
        write_file(_file_path, "{}");
}

void ScopeStore::snapshot() {
    Snap s;
    s.g = read_file(_global_path);
    s.f = fs::exists(_file_path)  ? read_file(_file_path)  : "{}";
    s.b = fs::exists(_block_path) ? read_file(_block_path) : "{}";
    _snapshots.push_back(std::move(s));
}

void ScopeStore::commit() {
    if (!_snapshots.empty())
        _snapshots.pop_back();
}

void ScopeStore::restore() {
    if (_snapshots.empty()) return;
    auto& s = _snapshots.back();
    write_file(_global_path, s.g);
    if (fs::exists(_file_path))  write_file(_file_path,  s.f);
    if (fs::exists(_block_path)) write_file(_block_path, s.b);
    _snapshots.pop_back();
}

void ScopeStore::cleanup() {
    std::error_code ec;
    fs::remove_all(_tmpdir, ec);
}
