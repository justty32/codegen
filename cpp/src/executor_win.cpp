// Windows implementation of run_block (CreateProcess + Job Object tree-kill +
// PathMapper shebang resolution).  The POSIX counterpart lives in
// executor_posix.cpp; both are guarded so exactly one compiles per platform.

#include "executor.hpp"
#include "utils.hpp"

#ifdef _WIN32
// ---- Windows implementation using CreateProcess ----

#include <algorithm>
#include <chrono>
#include <future>
#include <sstream>
#include <windows.h>

#include "path_mapper.hpp"

// Return the cached PathMapper (loaded from CODEGEN_PATH_MAP once at startup).
static const PathMapper& get_path_mapper() {
    static PathMapper m = PathMapper::load();
    return m;
}

// Resolve a shebang line to a Windows-executable string.
//
// Resolution order:
//   1. "/usr/bin/env interpreter" → return "interpreter" (search PATH, no map)
//   2. "/absolute/path/interpreter" → try PathMapper; fall back to basename
//   3. "interpreter" (no slash) → return as-is (search PATH)
static std::string shebang_to_win_cmd(const std::string& shebang) {
    std::string line = lstrip(shebang);
    if (starts_with(line, "#!")) line = lstrip(line.substr(2));
    if (line.empty()) return "python";

    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;

    // Normalise slashes for basename extraction
    auto normalise = [](std::string s) -> std::string {
        for (char& c : s) if (c == '\\') c = '/';
        return s;
    };
    std::string ncmd = normalise(cmd);
    auto slash_pos = ncmd.rfind('/');
    std::string basename = (slash_pos != std::string::npos)
                               ? ncmd.substr(slash_pos + 1)
                               : ncmd;

    // Case 1: /…/env  → treat next token as interpreter name (search PATH)
    if (basename == "env" || basename == "env.exe") {
        std::string next;
        if (ss >> next) {
            // `next` is a plain name like "python3"; no mapping applied
            return normalise(next);
        }
        return "python";
    }

    // Case 2: absolute POSIX path → try PathMapper
    if (!cmd.empty() && cmd[0] == '/') {
        std::string mapped = get_path_mapper().map(cmd);
        if (!mapped.empty()) return mapped;
        // PathMapper had no entry: fall back to basename so it is found via PATH
    }

    // Case 3 (and fallback from case 2): bare name or basename
    return basename;
}

// Map interpreter name to a suitable temp file extension.
// interp may be a bare name ("python3") or a full path ("C:\\Py\\python.exe"),
// so reduce to the basename before matching.
static std::string interp_to_ext(const std::string& interp) {
    std::string n = interp;
    for (char& c : n) if (c == '\\') c = '/';
    auto slash = n.rfind('/');
    if (slash != std::string::npos) n = n.substr(slash + 1);
    if (ends_with(n, ".exe")) n = n.substr(0, n.size() - 4);
    if (starts_with(n, "python")) return ".py";
    if (n == "bash" || n == "sh" || n == "zsh" || n == "ksh") return ".sh";
    if (n == "node" || n == "deno") return ".js";
    if (n == "ruby") return ".rb";
    if (n == "perl") return ".pl";
    if (n == "lua")  return ".lua";
    return ".sh";
}

// Write body to a temp file with the given extension.  Returns the path.
static std::string write_script_win(const std::string& body, const std::string& ext) {
    char dir[MAX_PATH], base_path[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    GetTempFileNameA(dir, "cg", 0, base_path); // creates a placeholder file
    DeleteFileA(base_path);

    std::string path = std::string(base_path) + ext;
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        throw std::runtime_error("CreateFile failed: " + path);
    DWORD written;
    WriteFile(hFile, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    CloseHandle(hFile);
    return path;
}

// Read all bytes from a Windows HANDLE until EOF.
static std::string read_handle(HANDLE h) {
    std::string out;
    char buf[4096];
    DWORD n;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
        out.append(buf, n);
    return out;
}

// Build a Windows environment block: KEY=VAL\0...\0\0
static std::string build_env_block(const std::map<std::string, std::string>& env) {
    std::string block;
    for (auto& [k, v] : env) {
        block += k + "=" + v;
        block += '\0';
    }
    block += '\0';
    return block;
}

std::pair<std::string, double> run_block(
    const Block& block,
    const std::map<std::string, std::string>& env,
    const fs::path& cwd,
    double max_pass_time,
    std::vector<std::string>& pass_outputs,
    const std::optional<std::string>& run_as_user)
{
    // Privilege drop (run_as_user) is a POSIX setuid/setgid concept with no
    // CreateProcess equivalent. Refuse rather than silently ignore, so a caller
    // expecting isolation is not misled into thinking it took effect.
    if (run_as_user.has_value())
        throw BlockFailure(block, "user:unsupported:" + *run_as_user, pass_outputs);

    std::string shebang = block.shebang.empty() ? "#!/usr/bin/env python3" : block.shebang;
    std::string interp  = shebang_to_win_cmd(shebang);
    std::string ext     = interp_to_ext(interp);

    std::string script_path;
    try {
        script_path = write_script_win(block.body, ext);
    } catch (const std::exception& e) {
        throw BlockFailure(block, std::string("io:") + e.what(), pass_outputs);
    }

    // Command line: "interpreter" "script"
    std::string cmdline = "\"" + interp + "\" \"" + script_path + "\"";
    std::string env_block = build_env_block(env);

    // Inheritable pipe handles for stdout and stderr
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hOutR, hOutW, hErrR, hErrW;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0) ||
        !CreatePipe(&hErrR, &hErrW, &sa, 0)) {
        DeleteFileA(script_path.c_str());
        throw BlockFailure(block, "io:CreatePipe failed", pass_outputs);
    }
    // Parent read-ends must NOT be inherited by the child
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb           = sizeof(si);
    si.hStdOutput   = hOutW;
    si.hStdError    = hErrW;
    si.dwFlags      = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    std::string cwd_str = cwd.empty() ? "" : cwd.string();
    const char* pcwd = cwd_str.empty() ? nullptr : cwd_str.c_str();

    // Put the child (and everything it spawns) in a Job Object so a timeout can
    // kill the whole tree, not just the direct child. KILL_ON_JOB_CLOSE also
    // reaps the tree if we bail out while the job handle is still open.
    HANDLE hJob = CreateJobObjectA(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                &jeli, sizeof(jeli));
    }

    auto start = std::chrono::steady_clock::now();

    // Create suspended so the child can be assigned to the job before it runs
    // any code (and thus before it can spawn children outside the job).
    BOOL ok = CreateProcessA(
        nullptr, &cmdline[0], nullptr, nullptr,
        TRUE,                 // inherit handles
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        env_block.data(),
        pcwd,
        &si, &pi);

    // Close child-side write ends immediately (parent must not hold them open)
    CloseHandle(hOutW);
    CloseHandle(hErrW);
    // NOTE: do NOT delete script_path yet — the child process needs to read it.
    // On Windows, unlink-while-open semantics don't apply; we delete after exit.

    if (!ok) {
        DeleteFileA(script_path.c_str());
        CloseHandle(hOutR); CloseHandle(hErrR);
        if (hJob) CloseHandle(hJob);
        throw BlockFailure(block,
            "io:CreateProcess error " + std::to_string(GetLastError()),
            pass_outputs);
    }

    // Assign to the job, then let the child run.
    if (hJob) AssignProcessToJobObject(hJob, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // Kill the whole tree (job) on timeout; fall back to the direct child if the
    // job object could not be created/assigned.
    auto kill_tree = [&]() {
        if (hJob) TerminateJobObject(hJob, 1);
        else      TerminateProcess(pi.hProcess, 1);
    };

    // Drain pipes in background threads to avoid deadlock on large output
    auto out_fut = std::async(std::launch::async, read_handle, hOutR);
    auto err_fut = std::async(std::launch::async, read_handle, hErrR);

    // Wait for process with timeout
    auto elapsed0 = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    DWORD timeout_ms = static_cast<DWORD>(
        std::max(0.0, (max_pass_time - elapsed0) * 1000.0));

    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);

    if (wait_result == WAIT_TIMEOUT) {
        kill_tree();
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        std::string stderr_data;
        try { stderr_data = err_fut.get(); } catch (...) {}
        try { out_fut.get(); } catch (...) {}
        CloseHandle(hOutR); CloseHandle(hErrR);
        if (hJob) CloseHandle(hJob);
        DeleteFileA(script_path.c_str()); // safe: process has exited
        throw BlockFailure(block, "timeout:pass",
                           std::vector<std::string>(pass_outputs),
                           stderr_data);
    }

    std::string stdout_data = out_fut.get();
    std::string stderr_data = err_fut.get();
    CloseHandle(hOutR); CloseHandle(hErrR);

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    if (hJob) CloseHandle(hJob);
    DeleteFileA(script_path.c_str()); // safe: process has exited

    if (exit_code != 0) {
        throw BlockFailure(block,
                           "exit:" + std::to_string(exit_code),
                           std::vector<std::string>(pass_outputs),
                           stderr_data,
                           static_cast<int>(exit_code));
    }
    return {stdout_data, elapsed};
}
#endif
