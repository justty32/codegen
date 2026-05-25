#include "scope.hpp"
#include "utils.hpp"

#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static fs::path make_tmpdir() {
#ifdef _WIN32
    char tmp_base[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_base);
    // Use PID + tick count to get a unique directory name
    DWORD pid  = GetCurrentProcessId();
    ULONGLONG tick = GetTickCount64();
    char path[MAX_PATH];
    // Try up to 32 candidates in case of collision
    for (int i = 0; i < 32; i++) {
        snprintf(path, MAX_PATH, "%scodegen_scope_%lu_%llu_%d",
                 tmp_base, static_cast<unsigned long>(pid), tick, i);
        if (CreateDirectoryA(path, nullptr))
            return path;
    }
    throw std::runtime_error("cannot create temp scope directory");
#else
    char tmpl[] = "/tmp/codegen_scope_XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) throw std::runtime_error("mkdtemp failed");
    return dir;
#endif
}

ScopeStore ScopeStore::create() {
    return ScopeStore(make_tmpdir());
}

ScopeStore::ScopeStore(fs::path tmpdir)
    : _tmpdir(std::move(tmpdir)) {
    _global_path = _tmpdir / "global.json";
    _file_path   = _tmpdir / "file.json";
    _block_path  = _tmpdir / "block.json";
    write_file(_global_path, "{}");
}

ScopeStore::~ScopeStore() {
    // Don't cleanup automatically — caller decides (ScopeStore::cleanup).
}

void ScopeStore::open_file() {
    write_file(_file_path, "{}");
}

void ScopeStore::open_block() {
    write_file(_block_path, "{}");
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
