#include "backup.hpp"
#include "config.hpp"
#include "errors.hpp"
#include "pipeline.hpp"
#include "progress.hpp"
#include "rollback.hpp"
#include "scanner.hpp"

#include <CLI/CLI.hpp>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// ---- SIGINT ----

static volatile sig_atomic_t g_cancel = 0;
static RunState* g_run_state = nullptr;

static void sigint_handler(int) {
    if (g_cancel) {
        // Second Ctrl+C: hard exit
        exit(EXIT_SIGINT);
    }
    g_cancel = 1;
    if (g_run_state)
        report_interrupt(*g_run_state, "sigint");
    exit(EXIT_SIGINT);
}

// ---- helpers ----

static std::map<std::string, std::string> parse_env_pairs(
    const std::vector<std::string>& pairs)
{
    std::map<std::string, std::string> out;
    for (auto& pair : pairs) {
        auto eq = pair.find('=');
        if (eq == std::string::npos)
            throw ConfigError("--env: expected KEY=VAL, got: " + pair);
        out[pair.substr(0, eq)] = pair.substr(eq + 1);
    }
    return out;
}

// ---- run subcommand ----

static int cmd_run(
    const std::vector<std::string>& path_strs,
    const std::optional<std::string>& config_path_str,
    const std::optional<std::string>& markers_str,
    bool keep_as_comment,
    std::optional<bool> auto_indent,
    const std::optional<std::string>& backup_dir_str,
    bool no_backup,
    bool scan_all,
    const std::optional<std::string>& ext_str,
    const std::vector<std::string>& include_globs,
    const std::vector<std::string>& exclude_globs,
    std::optional<int> max_passes,
    std::optional<double> max_total_time,
    std::optional<double> max_pass_time,
    const std::optional<std::string>& on_error,
    const std::optional<std::string>& cwd_str,
    const std::vector<std::string>& env_pairs,
    const std::optional<std::string>& run_as_user,
    bool dry_run)
{
    // Build CLI overrides as string map
    std::map<std::string, std::string> cli_over;

    if (markers_str)     cli_over["markers"]      = *markers_str;
    if (keep_as_comment) cli_over["keep_as_comment"] = "true";
    if (auto_indent)     cli_over["auto_indent"]   = *auto_indent ? "true" : "false";
    if (backup_dir_str)  cli_over["backup_dir"]    = *backup_dir_str;
    if (no_backup)       cli_over["backup"]         = "false";
    if (scan_all)        cli_over["scan_all"]       = "true";
    if (ext_str)         cli_over["extensions"]     = *ext_str;
    if (max_passes)      cli_over["max_passes"]     = std::to_string(*max_passes);
    if (max_total_time)  cli_over["max_total_time"] = std::to_string(*max_total_time);
    if (max_pass_time)   cli_over["max_pass_time"]  = std::to_string(*max_pass_time);
    if (on_error)        cli_over["on_error"]       = *on_error;
    if (cwd_str)         cli_over["cwd"]            = *cwd_str;
    if (run_as_user)     cli_over["run_as_user"]    = *run_as_user;

    // include/exclude are cumulative
    if (!include_globs.empty()) {
        std::string s;
        for (size_t i = 0; i < include_globs.size(); i++) {
            if (i) s += ',';
            s += include_globs[i];
        }
        cli_over["include"] = s;
    }
    if (!exclude_globs.empty()) {
        std::string s;
        for (size_t i = 0; i < exclude_globs.size(); i++) {
            if (i) s += ',';
            s += exclude_globs[i];
        }
        cli_over["exclude"] = s;
    }

    std::vector<fs::path> paths;
    if (path_strs.empty()) {
        paths.push_back(fs::current_path());
    } else {
        for (auto& s : path_strs) paths.push_back(s);
    }

    std::optional<fs::path> cfg_path;
    if (config_path_str) cfg_path = *config_path_str;

    Config cfg;
    try {
        cfg = resolve_initial(cli_over, paths[0], cfg_path);

        // Inject extra_env from --env flags
        if (!env_pairs.empty()) {
            auto extra = parse_env_pairs(env_pairs);
            for (auto& [k, v] : extra)
                cfg.extra_env[k] = v;
        }
    } catch (const ConfigError& e) {
        std::cerr << "codegen: 設定錯誤 — " << e.what() << "\n";
        return EXIT_STARTUP;
    }

    std::vector<fs::path> files;
    try {
        files = collect_files(paths, cfg);
    } catch (const ConfigError& e) {
        std::cerr << "codegen: " << e.what() << "\n";
        return EXIT_STARTUP;
    }

    if (files.empty()) return EXIT_OK;

    print_plan(paths);

    RunState state;
    state.targets = paths;
    g_run_state = &state;

    std::string run_id = make_run_id();
    int code;
    try {
        code = run_all(files, cfg, run_id, dry_run, &state);
    } catch (const CodegenError& e) {
        // e.g. a forbidden/invalid pragma discovered while expanding a block.
        // Surface it cleanly instead of letting it abort the process.
        std::cerr << "codegen: 設定錯誤 — " << e.what() << "\n";
        g_run_state = nullptr;
        return EXIT_STARTUP;
    }

    if (code == EXIT_ABORT_ALL)
        report_interrupt(state, "abort_all");

    g_run_state = nullptr;
    return code;
}

// ---- main ----

int main(int argc, char* argv[]) {
    std::signal(SIGINT, sigint_handler);

    // Mirror the documented CLI (DESIGN §11): `run` is the default command and
    // may be omitted. If the first argument is not a known subcommand or a help
    // flag, treat the whole invocation as an implicit `run`.
    std::vector<std::string> raw;
    for (int i = 1; i < argc; i++) raw.emplace_back(argv[i]);
    bool implicit_run = raw.empty() ||
        (raw[0] != "run" && raw[0] != "rollback" &&
         raw[0] != "-h"  && raw[0] != "--help");
    if (implicit_run)
        raw.insert(raw.begin(), "run");

    std::string prog = (argc > 0) ? argv[0] : "codegen";
    std::vector<char*> new_argv;
    new_argv.push_back(prog.data());
    for (auto& s : raw) new_argv.push_back(s.data());
    int   new_argc = static_cast<int>(new_argv.size());
    char** argv_p  = new_argv.data();

    CLI::App app{"Cross-language in-source code generation tool."};
    app.name("codegen");

    // ---- run subcommand ----
    auto* run_cmd = app.add_subcommand("run", "Run codegen blocks (default command)");
    run_cmd->fallthrough(false);

    std::vector<std::string> run_paths;
    run_cmd->add_option("paths", run_paths, "Files or directories to process");

    std::optional<std::string> config_path_str;
    run_cmd->add_option("--config", config_path_str, "Explicit codegen.toml path");

    std::optional<std::string> markers_str;
    run_cmd->add_option("--markers", markers_str, "Override markers (start,end)");

    bool keep_as_comment = false;
    run_cmd->add_flag("--keep-as-comment", keep_as_comment, "Keep source as comment");

    std::optional<bool> auto_indent_val;
    bool auto_indent_on = false, auto_indent_off = false;
    run_cmd->add_flag("--auto-indent",    auto_indent_on,  "Enable auto-indent (default)");
    run_cmd->add_flag("--no-auto-indent", auto_indent_off, "Disable auto-indent");

    std::optional<std::string> backup_dir_str;
    run_cmd->add_option("--backup-dir", backup_dir_str, "Backup root directory");

    bool no_backup = false;
    run_cmd->add_flag("--no-backup", no_backup, "Disable backup");

    bool scan_all = false;
    run_cmd->add_flag("--all", scan_all, "Ignore extension list");

    std::optional<std::string> ext_str;
    run_cmd->add_option("--ext", ext_str, "Override extensions (comma-separated)");

    std::vector<std::string> include_globs, exclude_globs;
    run_cmd->add_option("--include", include_globs, "Extra include glob")->allow_extra_args(false);
    run_cmd->add_option("--exclude", exclude_globs, "Exclude glob")->allow_extra_args(false);

    std::optional<int>    max_passes_val;
    std::optional<double> max_total_time_val, max_pass_time_val;
    run_cmd->add_option("--max-passes",     max_passes_val,     "Max expansion rounds per block");
    run_cmd->add_option("--max-total-time", max_total_time_val, "Max total time per block (sec)");
    run_cmd->add_option("--max-pass-time",  max_pass_time_val,  "Max time per subprocess (sec)");

    std::optional<std::string> on_error_val;
    run_cmd->add_option("--on-error", on_error_val, "Error mode: continue|abort_file|abort_all")
        ->check(CLI::IsMember({"continue", "abort_file", "abort_all"}));
    bool strict_flag = false;
    run_cmd->add_flag("--strict", strict_flag, "Equivalent to --on-error abort_all");

    std::optional<std::string> cwd_str;
    run_cmd->add_option("--cwd", cwd_str, "Override subprocess working directory");

    std::vector<std::string> env_pairs;
    run_cmd->add_option("--env", env_pairs, "Inject env var KEY=VAL (repeatable)");

    std::optional<std::string> run_as_user_val;
    run_cmd->add_option("--run-as-user", run_as_user_val,
                        "Drop privilege: run blocks as this user (name or uid). Requires root.");

    bool dry_run = false;
    run_cmd->add_flag("--dry-run", dry_run, "Print processed content; don't write to disk");

    // ---- rollback subcommand ----
    auto* rb_cmd = app.add_subcommand("rollback", "Roll back files to a previous backup");

    std::vector<std::string> rb_paths;
    rb_cmd->add_option("paths", rb_paths, "Files or directories to roll back");

    std::optional<std::string> rb_timestamp;
    rb_cmd->add_option("--timestamp,-t", rb_timestamp, "Backup timestamp to restore");

    bool rb_list = false;
    rb_cmd->add_flag("--list,-l", rb_list, "List available timestamps");

    std::optional<std::string> rb_backup_dir;
    rb_cmd->add_option("--backup-dir", rb_backup_dir, "Backup root directory");

    // Exactly one subcommand is expected (implicit `run` was injected above).
    app.require_subcommand(1, 1);

    CLI11_PARSE(app, new_argc, argv_p);

    if (rb_cmd->parsed()) {
        std::vector<fs::path> rb_path_list;
        for (auto& s : rb_paths) rb_path_list.push_back(s);
        fs::path bdir = rb_backup_dir ? fs::path(*rb_backup_dir) : fs::path(".codegen-backup");
        return run_rollback(rb_path_list, rb_timestamp, rb_list, bdir);
    }

    // resolve auto_indent override
    std::optional<bool> ai;
    if (auto_indent_on && !auto_indent_off)  ai = true;
    if (auto_indent_off && !auto_indent_on)  ai = false;

    // --strict overrides --on-error
    std::optional<std::string> oe = on_error_val;
    if (strict_flag) oe = "abort_all";

    return cmd_run(run_paths,
                   config_path_str,
                   markers_str,
                   keep_as_comment,
                   ai,
                   backup_dir_str,
                   no_backup,
                   scan_all,
                   ext_str,
                   include_globs,
                   exclude_globs,
                   max_passes_val,
                   max_total_time_val,
                   max_pass_time_val,
                   oe,
                   cwd_str,
                   env_pairs,
                   run_as_user_val,
                   dry_run);
}
