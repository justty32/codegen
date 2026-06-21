// codegen_helper.hpp — single-header convenience API for C++ block scripts.
//
// This is the C++ counterpart of `codegen_helper.py`. A codegen block whose
// shebang compiles/runs C++ (e.g. via a `g++`-wrapper shebang, cling, or any
// run-from-source tool) can do:
//
//     #include "codegen_helper.hpp"
//     namespace cg = codegen;
//     int main() {
//         cg::file().set("count", 3);
//         long long n = cg::file().get_int("count");
//         cg::global().set("name", "widget");
//         // ... emit generated code on stdout ...
//     }
//
// It reads/writes the very same per-scope JSON files the codegen runner hands
// to block scripts via the CODEGEN_GLOBAL / CODEGEN_FILE / CODEGEN_BLOCK
// environment variables, so values round-trip with Python blocks and the
// runner itself.
//
// Design notes:
//   * Header-only and dependency-free (C++17 standard library only) — drop the
//     file next to your block script, no build-system wiring required.
//   * Values are stored as raw JSON. Typed accessors (get_str/get_int/...) cover
//     the common cases; set_json()/get_json() give raw access for nested data.
//   * The top-level scope file is always a JSON object. Other keys' values are
//     preserved verbatim on write (parsed as opaque raw substrings), so this
//     helper never corrupts data it does not understand.
//
// Thread-safety: none. Like the Python helper, each call does a full read /
// mutate / write of the scope file; concurrent writers race.

#ifndef CODEGEN_HELPER_HPP
#define CODEGEN_HELPER_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace codegen {

// ===================================================================
//  Minimal JSON support
//
//  We only ever need to manipulate the *top-level* object of a scope file:
//  add/replace/remove a key, and interpret a single value. Nested values are
//  carried around as their raw JSON text, so no full document model is needed.
// ===================================================================
namespace detail {

// Append `s` to `out` as a JSON string literal (with surrounding quotes).
inline void append_json_string(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

inline std::string json_string(const std::string& s) {
    std::string o;
    append_json_string(o, s);
    return o;
}

// Encode a Unicode code point as UTF-8 onto `out`.
inline void append_utf8(std::string& out, unsigned long cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
}

inline unsigned hex_val(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
    throw std::runtime_error("codegen_helper: bad \\u escape");
}

// Parse a JSON string literal at s[i]=='"'. Returns the decoded value and
// advances `i` past the closing quote.
inline std::string parse_string(const std::string& s, size_t& i) {
    if (i >= s.size() || s[i] != '"')
        throw std::runtime_error("codegen_helper: expected string");
    ++i;  // opening quote
    std::string out;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return out;
        if (c == '\\') {
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (i + 4 > s.size())
                        throw std::runtime_error("codegen_helper: truncated \\u");
                    unsigned long cp = (hex_val(s[i]) << 12) | (hex_val(s[i+1]) << 8) |
                                       (hex_val(s[i+2]) << 4) | hex_val(s[i+3]);
                    i += 4;
                    // Combine a high/low surrogate pair if present.
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() &&
                        s[i] == '\\' && s[i+1] == 'u') {
                        unsigned long lo = (hex_val(s[i+2]) << 12) | (hex_val(s[i+3]) << 8) |
                                           (hex_val(s[i+4]) << 4) | hex_val(s[i+5]);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            i += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    out += e;  // lenient: keep unknown escape char
            }
        } else {
            out += c;
        }
    }
    throw std::runtime_error("codegen_helper: unterminated string");
}

// Advance `i` past one complete JSON value and return its raw text (verbatim).
inline std::string skip_value(const std::string& s, size_t& i) {
    skip_ws(s, i);
    size_t start = i;
    if (i >= s.size()) throw std::runtime_error("codegen_helper: unexpected end");
    char c = s[i];
    if (c == '"') {
        parse_string(s, i);
    } else if (c == '{' || c == '[') {
        // Walk nested containers, honouring strings so braces inside strings
        // do not throw off the depth count.
        std::vector<char> stack;
        stack.push_back(c);
        ++i;
        while (i < s.size() && !stack.empty()) {
            char d = s[i];
            if (d == '"') {
                parse_string(s, i);
                continue;
            }
            if (d == '{' || d == '[') stack.push_back(d);
            else if (d == '}' || d == ']') stack.pop_back();
            ++i;
        }
        if (!stack.empty())
            throw std::runtime_error("codegen_helper: unbalanced container");
    } else {
        // Scalar: number / true / false / null — read to the next structural char.
        while (i < s.size()) {
            char d = s[i];
            if (d == ',' || d == '}' || d == ']' ||
                d == ' ' || d == '\t' || d == '\n' || d == '\r')
                break;
            ++i;
        }
    }
    return s.substr(start, i - start);
}

// Parse a top-level JSON object into key -> raw-value-text. An empty or
// whitespace-only document yields an empty map (matching the Python helper).
inline std::map<std::string, std::string> parse_object(const std::string& s) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    skip_ws(s, i);
    if (i >= s.size()) return out;
    if (s[i] != '{')
        throw std::runtime_error("codegen_helper: scope file is not a JSON object");
    ++i;
    skip_ws(s, i);
    if (i < s.size() && s[i] == '}') return out;
    while (i < s.size()) {
        skip_ws(s, i);
        std::string key = parse_string(s, i);
        skip_ws(s, i);
        if (i >= s.size() || s[i] != ':')
            throw std::runtime_error("codegen_helper: expected ':'");
        ++i;
        out[key] = skip_value(s, i);
        skip_ws(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') break;
        throw std::runtime_error("codegen_helper: expected ',' or '}'");
    }
    return out;
}

inline std::string serialize_object(const std::map<std::string, std::string>& m) {
    std::string out = "{";
    bool first = true;
    for (auto& [k, v] : m) {
        if (!first) out += ',';
        first = false;
        append_json_string(out, k);
        out += ':';
        out += v;  // raw value text
    }
    out += '}';
    return out;
}

// Strip surrounding quotes + unescape if `raw` is a JSON string; otherwise
// return it verbatim (numbers/bools/null/containers).
inline std::string raw_to_text(const std::string& raw) {
    size_t i = 0;
    detail::skip_ws(raw, i);
    if (i < raw.size() && raw[i] == '"')
        return parse_string(raw, i);
    // trim trailing whitespace for scalars
    std::string t = raw;
    size_t b = t.find_first_not_of(" \t\n\r");
    size_t e = t.find_last_not_of(" \t\n\r");
    if (b == std::string::npos) return "";
    return t.substr(b, e - b + 1);
}

}  // namespace detail

// ===================================================================
//  Scope — read/modify/write a single scope file (global/file/block)
// ===================================================================
class Scope {
public:
    explicit Scope(const char* env_var) : env_var_(env_var) {}

    // --- existence & raw access ---------------------------------------------

    bool has(const std::string& key) const {
        auto m = load();
        return m.find(key) != m.end();
    }

    // Raw JSON text of the value, or nullopt if the key is absent.
    std::optional<std::string> get_json(const std::string& key) const {
        auto m = load();
        auto it = m.find(key);
        if (it == m.end()) return std::nullopt;
        return it->second;
    }

    // Store a value given as raw JSON text (caller guarantees validity).
    void set_json(const std::string& key, const std::string& raw_json) {
        auto m = load();
        m[key] = raw_json;
        store(m);
    }

    void del(const std::string& key) {
        auto m = load();
        if (m.erase(key)) store(m);
    }

    // --- typed getters -------------------------------------------------------

    std::string get_str(const std::string& key, const std::string& def = "") const {
        auto v = get_json(key);
        return v ? detail::raw_to_text(*v) : def;
    }

    long long get_int(const std::string& key, long long def = 0) const {
        auto v = get_json(key);
        if (!v) return def;
        try { return std::stoll(detail::raw_to_text(*v)); }
        catch (...) { return def; }
    }

    double get_double(const std::string& key, double def = 0.0) const {
        auto v = get_json(key);
        if (!v) return def;
        try { return std::stod(detail::raw_to_text(*v)); }
        catch (...) { return def; }
    }

    bool get_bool(const std::string& key, bool def = false) const {
        auto v = get_json(key);
        if (!v) return def;
        std::string t = detail::raw_to_text(*v);
        if (t == "true"  || t == "1") return true;
        if (t == "false" || t == "0") return false;
        return def;
    }

    // --- typed setters -------------------------------------------------------

    void set(const std::string& key, const std::string& value) {
        set_json(key, detail::json_string(value));
    }
    void set(const std::string& key, const char* value) {
        set(key, std::string(value));
    }
    void set(const std::string& key, bool value) {
        set_json(key, value ? "true" : "false");
    }
    void set(const std::string& key, long long value) {
        set_json(key, std::to_string(value));
    }
    void set(const std::string& key, int value) {
        set(key, static_cast<long long>(value));
    }
    void set(const std::string& key, double value) {
        std::ostringstream ss;
        ss.precision(17);
        ss << value;
        set_json(key, ss.str());
    }

private:
    const char* env_var_;

    std::string path() const {
        const char* p = std::getenv(env_var_);
        if (!p || !*p)
            throw std::runtime_error(std::string("$") + env_var_ +
                                     " not set — are you running inside codegen?");
        return p;
    }

    std::map<std::string, std::string> load() const {
        std::ifstream f(path(), std::ios::binary);
        if (!f) return {};
        std::ostringstream ss;
        ss << f.rdbuf();
        return detail::parse_object(ss.str());
    }

    void store(const std::map<std::string, std::string>& m) const {
        std::string text = detail::serialize_object(m);
        std::ofstream f(path(), std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("codegen_helper: cannot write scope file");
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
};

// ===================================================================
//  Scope accessors (mirror the Python helper's three scopes)
// ===================================================================

inline Scope& global() {
    static Scope s("CODEGEN_GLOBAL");
    return s;
}
inline Scope& file() {
    static Scope s("CODEGEN_FILE");
    return s;
}
inline Scope& block() {
    static Scope s("CODEGEN_BLOCK");
    return s;
}

// ===================================================================
//  Read-only invocation context
// ===================================================================
namespace detail {
inline std::string read_path_from_env(const char* env_var) {
    const char* p = std::getenv(env_var);
    if (!p || !*p)
        throw std::runtime_error(std::string("$") + env_var +
                                 " not set — are you running inside codegen?");
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
inline std::string getenv_str(const char* env_var) {
    const char* p = std::getenv(env_var);
    return p ? std::string(p) : std::string();
}
}  // namespace detail

// Original file content (before codegen began processing it).
inline std::string origin_file() {
    return detail::read_path_from_env("CODEGEN_ORIGIN_FILE");
}

// Original block content (shebang + body, no markers).
inline std::string origin_block() {
    return detail::read_path_from_env("CODEGEN_ORIGIN_BLOCK");
}

// Targets passed to codegen on this invocation (one per line, blanks dropped).
inline std::vector<std::string> targets() {
    std::vector<std::string> out;
    std::istringstream ss(detail::getenv_str("CODEGEN_TARGETS"));
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

// The cwd from which codegen was invoked.
inline std::string invoke_cwd() {
    return detail::getenv_str("CODEGEN_INVOKE_CWD");
}

// Absolute path of the file currently being processed.
inline std::string file_path() {
    return detail::getenv_str("CODEGEN_FILE_PATH");
}

}  // namespace codegen

#endif  // CODEGEN_HELPER_HPP
