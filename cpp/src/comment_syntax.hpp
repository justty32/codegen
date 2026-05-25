#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace fs = std::filesystem;

struct CommentSyntax {
    std::string open;
    std::string close; // empty = line-style

    bool is_block() const { return !close.empty(); }

    static CommentSyntax from_string(const std::string& spec);
};

// Returns nullopt if extension is not recognised.
std::optional<CommentSyntax> lookup_syntax(const fs::path& path,
                                           const std::map<std::string, std::string>* overrides = nullptr);

std::vector<std::string> default_extensions();

// Returns the pragma comment prefix appropriate for a given shebang line.
// e.g. "#!/usr/bin/env python3" → "#"
// Returns empty string if prefix cannot be determined.
std::string pragma_prefix_for_shebang(const std::string& shebang);
