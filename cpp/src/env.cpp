#include "env.hpp"
#include "utils.hpp"

#ifdef _WIN32
extern char** _environ;
#define environ _environ
#else
extern char** environ;
#endif

std::map<std::string, std::string> build_env(
    const std::map<std::string, std::string>& cfg_extra_env,
    const ScopeStore& scope,
    const RunContext& ctx)
{
    std::map<std::string, std::string> env;

    // Start from current process environment
    for (char** ep = environ; ep && *ep; ep++) {
        std::string entry(*ep);
        auto eq = entry.find('=');
        if (eq == std::string::npos) continue;
        env[entry.substr(0, eq)] = entry.substr(eq + 1);
    }

    // cfg.extra_env
    for (auto& [k, v] : cfg_extra_env)
        env[k] = v;

    // Scope paths
    env["CODEGEN_GLOBAL"] = scope.global_path().string();
    env["CODEGEN_FILE"]   = scope.file_path().string();
    env["CODEGEN_BLOCK"]  = scope.block_path().string();

    // Origin snapshots
    env["CODEGEN_ORIGIN_FILE"]  = ctx.origin_file_path.string();
    env["CODEGEN_ORIGIN_BLOCK"] = ctx.origin_block_path.string();

    // Path/location indicators
    env["CODEGEN_INVOKE_CWD"] = ctx.invoke_cwd.string();

    std::string targets_str;
    for (size_t i = 0; i < ctx.targets.size(); i++) {
        if (i > 0) targets_str += '\n';
        targets_str += ctx.targets[i].string();
    }
    env["CODEGEN_TARGETS"]   = targets_str;
    env["CODEGEN_FILE_PATH"] = ctx.file_path.string();

    return env;
}
