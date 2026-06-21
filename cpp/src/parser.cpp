#include "parser.hpp"
#include "utils.hpp"
#include <stdexcept>

std::map<std::string, std::string> parse_pragma_tokens(const std::string& text) {
    std::map<std::string, std::string> result;
    std::istringstream ss(text);
    std::string tok;
    while (ss >> tok) {
        auto eq = tok.find('=');
        if (eq == std::string::npos)
            throw ConfigError("pragma: token " + tok + " missing '='");
        std::string k = tok.substr(0, eq);
        std::string v = tok.substr(eq + 1);
        if (k.empty()) throw ConfigError("pragma: empty key in " + tok);
        if (v.empty()) throw ConfigError("pragma: empty value for key " + k);
        result[k] = v;
    }
    return result;
}

static std::map<std::string, std::string> pragma_from_line(
    const std::string& line, const std::string& prefix)
{
    std::string stripped = strip(line);
    if (!starts_with(stripped, prefix)) return {};
    std::string rest = lstrip(stripped.substr(prefix.size()));
    if (!starts_with(rest, "codegen:")) return {};
    std::string payload = strip(rest.substr(8)); // len("codegen:") == 8
    if (payload.empty()) return {};
    return parse_pragma_tokens(payload);
}

static std::map<std::string, std::string> pragma_from_text(const std::string& text) {
    // Search for "codegen: ..." pattern anywhere in text
    std::string key = "codegen:";
    auto pos = text.find(key);
    if (pos == std::string::npos) return {};
    std::string payload = strip(text.substr(pos + key.size()));
    // strip trailing block-comment closers
    for (auto& closer : std::vector<std::string>{"*/", "-->"}) {
        if (ends_with(payload, closer))
            payload = rstrip(payload.substr(0, payload.size() - closer.size()));
    }
    std::map<std::string, std::string> result;
    std::istringstream ss(payload);
    std::string tok;
    while (ss >> tok) {
        auto eq = tok.find('=');
        if (eq != std::string::npos) {
            std::string k = tok.substr(0, eq);
            std::string v = tok.substr(eq + 1);
            if (!k.empty() && !v.empty())
                result[k] = v;
        }
    }
    return result;
}

BlockHeader parse_block_header(const std::string& inner_text) {
    auto lines = splitlines(inner_text, true);
    int idx = 0;

    std::string shebang;
    if (!lines.empty() && starts_with(lstrip(lines[0]), "#!")) {
        shebang = strip(lines[0]);
        idx = 1;
    }

    std::map<std::string, std::string> pragma;
    if (idx < (int)lines.size()) {
        std::string prefix = pragma_prefix_for_shebang(shebang);
        if (!prefix.empty()) {
            auto parsed = pragma_from_line(lines[idx], prefix);
            if (!parsed.empty()) {
                pragma = std::move(parsed);
                idx++;
            }
        }
    }

    std::string body;
    for (int i = idx; i < (int)lines.size(); i++)
        body += lines[i];

    return {shebang, pragma, body};
}

static std::string detect_indent(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        i++;
    return line.substr(0, i);
}

static std::string extract_inner_block_style(
    const std::vector<std::string>& lines, int start_idx, int end_idx)
{
    std::string inner;
    for (int i = start_idx + 1; i < end_idx; i++)
        inner += lines[i];
    return inner;
}

static std::string extract_inner_line_style(
    const std::vector<std::string>& lines, int start_idx, int end_idx,
    const CommentSyntax& cs)
{
    std::string inner;
    const std::string& prefix = cs.open;
    for (int i = start_idx + 1; i < end_idx; i++) {
        std::string line = lines[i];
        std::string stripped = lstrip(line);
        std::string with_space = prefix + " ";
        if (starts_with(stripped, with_space))
            inner += stripped.substr(with_space.size());
        else if (starts_with(stripped, prefix))
            inner += stripped.substr(prefix.size());
        else
            inner += line;
    }
    return inner;
}

std::vector<Block> find_top_level_blocks(
    const std::string& content,
    const fs::path& file_path,
    const std::pair<std::string, std::string>& markers,
    const CommentSyntax& cs)
{
    const std::string& start_marker = markers.first;
    const std::string& end_marker   = markers.second;

    auto lines = splitlines(content, true);
    std::vector<Block> blocks;
    int depth     = 0;
    int start_idx = -1;

    for (int i = 0; i < (int)lines.size(); i++) {
        const std::string& line = lines[i];
        bool has_start = line.find(start_marker) != std::string::npos;
        bool has_end   = line.find(end_marker)   != std::string::npos;

        if (has_start && depth == 0) {
            start_idx = i;
            depth = 1;
        } else if (has_start) {
            depth++;
        }

        if (has_end && depth > 0) {
            depth--;
            if (depth == 0) {
                int end_idx = i;
                std::string raw;
                for (int j = start_idx; j <= end_idx; j++)
                    raw += lines[j];

                std::string indent = detect_indent(lines[start_idx]);

                std::string inner;
                if (cs.is_block())
                    inner = extract_inner_block_style(lines, start_idx, end_idx);
                else
                    inner = extract_inner_line_style(lines, start_idx, end_idx, cs);

                inner = dedent(inner);
                auto [shebang, pragma, body] = parse_block_header(inner);

                blocks.push_back(Block{
                    file_path,
                    start_idx + 1,  // 1-based
                    end_idx + 1,
                    indent,
                    cs,
                    raw,
                    inner,
                    shebang,
                    pragma,
                    body,
                });
                start_idx = -1;
            }
        }
    }
    return blocks;
}

std::map<std::string, std::string> parse_file_pragma(
    const std::string& content,
    const CommentSyntax& cs,
    const std::pair<std::string, std::string>& markers)
{
    const std::string& start_marker = markers.first;
    auto lines = splitlines(content, true);

    if (cs.is_block()) {
        bool in_comment = false;
        std::string buf;
        for (auto& line : lines) {
            std::string stripped = strip(line);
            if (!in_comment) {
                if (stripped.empty()) continue;
                if (starts_with(stripped, cs.open)) {
                    if (stripped.find(start_marker) != std::string::npos) break;
                    in_comment = true;
                    buf += stripped.substr(cs.open.size());
                    if (!cs.close.empty() && stripped.find(cs.close) != std::string::npos)
                        break;
                } else {
                    break;
                }
            } else {
                if (!cs.close.empty() && stripped.find(cs.close) != std::string::npos) {
                    auto p = stripped.find(cs.close);
                    buf += " " + stripped.substr(0, p);
                    break;
                }
                buf += " " + line;
            }
        }
        return pragma_from_text(buf);
    } else {
        for (auto& line : lines) {
            std::string stripped = strip(line);
            if (stripped.empty()) continue;
            if (starts_with(stripped, cs.open)) {
                if (stripped.find(start_marker) != std::string::npos) break;
                auto p = pragma_from_line(stripped, cs.open);
                if (!p.empty()) return p;
            } else {
                break;
            }
        }
        return {};
    }
}
