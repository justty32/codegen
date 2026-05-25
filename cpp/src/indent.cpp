#include "indent.hpp"
#include "utils.hpp"

std::string apply_indent(const std::string& text,
                         const std::string& base_indent,
                         bool auto_indent) {
    if (!auto_indent || base_indent.empty())
        return text;

    bool ends_nl = ends_with(text, "\n");
    auto lines = splitlines(text, false);

    std::string result;
    result.reserve(text.size() + lines.size() * base_indent.size());
    for (size_t i = 0; i < lines.size(); i++) {
        if (!lines[i].empty())
            result += base_indent + lines[i];
        // blank lines get no trailing whitespace
        if (i + 1 < lines.size())
            result += '\n';
    }
    if (ends_nl)
        result += '\n';
    return result;
}
