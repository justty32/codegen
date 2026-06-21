#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---- string helpers ----

inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string lstrip(const std::string& s) {
    auto it = std::find_if(s.begin(), s.end(), [](unsigned char c) {
        return !std::isspace(c);
    });
    return {it, s.end()};
}

inline std::string rstrip(const std::string& s) {
    auto it = std::find_if(s.rbegin(), s.rend(), [](unsigned char c) {
        return !std::isspace(c);
    });
    return {s.begin(), it.base()};
}

inline std::string strip(const std::string& s) { return lstrip(rstrip(s)); }

// Split s on delimiter (single char). keepends: include the delimiter at end of each part.
inline std::vector<std::string> splitlines(const std::string& s, bool keepends = false) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find('\n', start);
        if (pos == std::string::npos) {
            if (start < s.size())
                result.push_back(s.substr(start));
            break;
        }
        if (keepends)
            result.push_back(s.substr(start, pos - start + 1));
        else
            result.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return result;
}

inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim))
        result.push_back(tok);
    return result;
}

// Python textwrap.dedent: strip common leading whitespace from all non-empty lines.
inline std::string dedent(const std::string& text) {
    auto lines = splitlines(text, true);
    std::string prefix;
    bool first = true;
    for (auto& line : lines) {
        std::string stripped = lstrip(line);
        if (stripped.empty() || stripped == "\n") continue;
        size_t indent_len = line.size() - lstrip(line).size();
        std::string cur = line.substr(0, indent_len);
        if (first) {
            prefix = cur;
            first = false;
        } else {
            // find common prefix
            size_t i = 0;
            while (i < prefix.size() && i < cur.size() && prefix[i] == cur[i])
                i++;
            prefix = prefix.substr(0, i);
        }
    }
    if (prefix.empty()) return text;
    std::string result;
    for (auto& line : lines) {
        if (starts_with(line, prefix))
            result += line.substr(prefix.size());
        else
            result += line;
    }
    return result;
}

// ---- file I/O helpers ----

inline std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read: " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline void write_file(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write: " + p.string());
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// Lowercase a string.
inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}
