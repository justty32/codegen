#pragma once
#include <string>

// Apply base_indent to every non-empty line in text (§5.1).
// If auto_indent is false or base_indent is empty, returns text unchanged.
std::string apply_indent(const std::string& text,
                         const std::string& base_indent,
                         bool auto_indent);
