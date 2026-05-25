#include "comment_syntax.hpp"
#include "utils.hpp"
#include <vector>

CommentSyntax CommentSyntax::from_string(const std::string& spec) {
    std::string s = strip(spec);
    auto sp = s.find(' ');
    if (sp != std::string::npos) {
        return {s.substr(0, sp), strip(s.substr(sp + 1))};
    }
    return {s, {}};
}

static const CommentSyntax BLOCK_C    {"/*",   "*/"};
static const CommentSyntax BLOCK_HTML {"<!--", "-->"};
static const CommentSyntax LINE_HASH  {"#",    {}};
static const CommentSyntax LINE_DASH  {"--",   {}};
static const CommentSyntax LINE_SLASH {"//",   {}};

static const std::map<std::string, CommentSyntax> DEFAULT_TABLE = {
    {".c",        BLOCK_C},
    {".cpp",      BLOCK_C},
    {".cc",       BLOCK_C},
    {".cxx",      BLOCK_C},
    {".h",        BLOCK_C},
    {".hpp",      BLOCK_C},
    {".rs",       BLOCK_C},
    {".go",       BLOCK_C},
    {".js",       BLOCK_C},
    {".mjs",      BLOCK_C},
    {".ts",       BLOCK_C},
    {".tsx",      BLOCK_C},
    {".jsx",      BLOCK_C},
    {".java",     BLOCK_C},
    {".cs",       BLOCK_C},
    {".swift",    BLOCK_C},
    {".kt",       BLOCK_C},
    {".py",       LINE_HASH},
    {".sh",       LINE_HASH},
    {".bash",     LINE_HASH},
    {".rb",       LINE_HASH},
    {".pl",       LINE_HASH},
    {".yaml",     LINE_HASH},
    {".yml",      LINE_HASH},
    {".toml",     LINE_HASH},
    {".makefile", LINE_HASH},
    {".html",     BLOCK_HTML},
    {".xml",      BLOCK_HTML},
    {".svg",      BLOCK_HTML},
    {".md",       BLOCK_HTML},
    {".lua",      LINE_DASH},
    {".sql",      LINE_DASH},
    {".hs",       LINE_DASH},
};

std::optional<CommentSyntax> lookup_syntax(const fs::path& path,
                                           const std::map<std::string, std::string>* overrides) {
    std::string ext = to_lower(path.extension().string());
    if (overrides) {
        auto it = overrides->find(ext);
        if (it != overrides->end())
            return CommentSyntax::from_string(it->second);
    }
    auto it = DEFAULT_TABLE.find(ext);
    if (it != DEFAULT_TABLE.end())
        return it->second;
    return std::nullopt;
}

std::vector<std::string> default_extensions() {
    std::vector<std::string> exts;
    exts.reserve(DEFAULT_TABLE.size());
    for (auto& [ext, _] : DEFAULT_TABLE)
        exts.push_back(ext);
    std::sort(exts.begin(), exts.end());
    return exts;
}

static const std::map<std::string, std::string> INTERP_PREFIX = {
    {"python",  "#"},
    {"python3", "#"},
    {"python2", "#"},
    {"sh",      "#"},
    {"bash",    "#"},
    {"zsh",     "#"},
    {"ruby",    "#"},
    {"perl",    "#"},
    {"node",    "//"},
    {"deno",    "//"},
};

std::string pragma_prefix_for_shebang(const std::string& shebang) {
    if (shebang.empty()) return "#";
    std::string line = lstrip(shebang);
    if (starts_with(line, "#!"))
        line = lstrip(line.substr(2));
    if (line.empty()) return "";
    auto parts = split(line, ' ');
    if (parts.empty()) return "";
    std::string cmd = parts[0];
    // handle `env python3` form
    if (ends_with(cmd, "env") && parts.size() >= 2)
        cmd = parts[1];
    // take basename
    auto slash = cmd.rfind('/');
    if (slash != std::string::npos)
        cmd = cmd.substr(slash + 1);
    auto it = INTERP_PREFIX.find(cmd);
    if (it != INTERP_PREFIX.end()) return it->second;
    return "";
}
