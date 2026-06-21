#pragma once
#include <map>
#include <string>

// Maps POSIX directory paths to Windows directory paths for shebang resolution.
// Loaded from a JSON file whose path is in CODEGEN_PATH_MAP (env var).
//
// JSON format:
//   { "/usr/bin": "C:\\Python313", "/bin": "C:\\mingw64\\bin" }
//
// Mapping algorithm (§ "parent-only" rule):
//   Given a POSIX absolute interpreter path like /usr/bin/python3:
//     1. parent = /usr/bin
//     2. Look up parent in the table → Windows dir
//     3. Return  Windows_dir\python3.exe
//
// Only absolute POSIX paths (starting with '/') are mapped.
// Relative names and '/usr/bin/env'-style shebangs are left as-is.
class PathMapper {
public:
    // Read CODEGEN_PATH_MAP and load the mapping file.
    // Returns an empty PathMapper if the env var is unset or the file is missing.
    static PathMapper load();

    bool empty() const { return _table.empty(); }

    // Map an absolute POSIX interpreter path to a Windows executable path.
    // Returns empty string if no entry matches the parent directory.
    std::string map(const std::string& posix_path) const;

private:
    std::map<std::string, std::string> _table; // /posix/dir → C:\win\dir
};
