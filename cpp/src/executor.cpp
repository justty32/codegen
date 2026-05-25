#include "executor.hpp"
#include "utils.hpp"

#ifndef _WIN32

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* FALLBACK_SHEBANG = "#!/usr/bin/env python3";

// Write script to a temp file, chmod it, return the path.
static std::string write_script(const std::string& shebang, const std::string& body) {
    std::string content = (shebang.empty() ? FALLBACK_SHEBANG : shebang) + "\n" + body;

    char tmpl[] = "/tmp/codegen_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) throw std::runtime_error(std::string("mkstemp: ") + strerror(errno));
    // Rename to add .sh suffix (not strictly needed for exec, but keeps things clean)
    std::string path = std::string(tmpl) + ".sh";
    rename(tmpl, path.c_str());
    fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) throw std::runtime_error(std::string("open script: ") + strerror(errno));
    if (write(fd, content.data(), content.size()) < 0) {
        close(fd);
        throw std::runtime_error("write script");
    }
    close(fd);
    return path;
}

static void read_until_closed(int fd, std::string& out) {
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
}

std::pair<std::string, double> run_block(
    const Block& block,
    const std::map<std::string, std::string>& env,
    const fs::path& cwd,
    double max_pass_time,
    std::vector<std::string>& pass_outputs)
{
    std::string script_path;
    try {
        script_path = write_script(block.shebang, block.body);
    } catch (const std::exception& e) {
        throw BlockFailure(block, std::string("io:") + e.what(), pass_outputs);
    }

    // Build envp
    std::vector<std::string> env_strings;
    env_strings.reserve(env.size());
    for (auto& [k, v] : env)
        env_strings.push_back(k + "=" + v);
    std::vector<char*> envp;
    envp.reserve(env_strings.size() + 1);
    for (auto& s : env_strings)
        envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);

    // Pipes for stdout and stderr
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) || pipe(err_pipe)) {
        unlink(script_path.c_str());
        throw BlockFailure(block, std::string("io:pipe: ") + strerror(errno), pass_outputs);
    }

    auto start = std::chrono::steady_clock::now();

    pid_t pid = fork();
    if (pid < 0) {
        unlink(script_path.c_str());
        throw BlockFailure(block, std::string("io:fork: ") + strerror(errno), pass_outputs);
    }

    if (pid == 0) {
        // Child
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        if (!cwd.empty()) {
            if (chdir(cwd.c_str()) != 0) _exit(127);
        }

        const char* argv[] = {script_path.c_str(), nullptr};
        execve(script_path.c_str(),
               const_cast<char* const*>(argv),
               envp.data());
        _exit(127);
    }

    // Parent
    close(out_pipe[1]);
    close(err_pipe[1]);
    unlink(script_path.c_str());

    // Read stdout and stderr with timeout using poll
    std::string stdout_data, stderr_data;

    auto deadline = start + std::chrono::duration<double>(max_pass_time);

    struct pollfd fds[2];
    bool out_done = false, err_done = false;

    while (!out_done || !err_done) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            close(out_pipe[0]);
            close(err_pipe[0]);
            throw BlockFailure(block, "timeout:pass",
                               std::vector<std::string>(pass_outputs),
                               stderr_data);
        }

        int nfds = 0;
        int idx_out = -1, idx_err = -1;
        if (!out_done) { idx_out = nfds; fds[nfds++] = {out_pipe[0], POLLIN, 0}; }
        if (!err_done) { idx_err = nfds; fds[nfds++] = {err_pipe[0], POLLIN, 0}; }

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        int timeout_ms = (remaining > 0) ? (int)remaining : 0;

        int ret = poll(fds, nfds, timeout_ms);
        if (ret < 0) break; // EINTR or error

        auto read_fd = [&](int fd, std::string& buf, bool& done) {
            char tmp[4096];
            ssize_t n = read(fd, tmp, sizeof(tmp));
            if (n > 0)      buf.append(tmp, static_cast<size_t>(n));
            else if (n <= 0) done = true;
        };

        if (idx_out >= 0) {
            if (fds[idx_out].revents & (POLLIN | POLLHUP | POLLERR))
                read_fd(out_pipe[0], stdout_data, out_done);
        }
        if (idx_err >= 0) {
            if (fds[idx_err].revents & (POLLIN | POLLHUP | POLLERR))
                read_fd(err_pipe[0], stderr_data, err_done);
        }
    }

    // Drain any remaining data
    if (!out_done) read_until_closed(out_pipe[0], stdout_data);
    if (!err_done) read_until_closed(err_pipe[0], stderr_data);

    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exit_code != 0) {
        throw BlockFailure(block,
                           "exit:" + std::to_string(exit_code),
                           std::vector<std::string>(pass_outputs),
                           stderr_data,
                           exit_code);
    }

    return {stdout_data, elapsed};
}

#else
// ---- Windows implementation using CreateProcess ----

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
static std::string interp_to_ext(const std::string& interp) {
    std::string n = interp;
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
    std::vector<std::string>& pass_outputs)
{
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

    auto start = std::chrono::steady_clock::now();

    BOOL ok = CreateProcessA(
        nullptr, &cmdline[0], nullptr, nullptr,
        TRUE,                 // inherit handles
        CREATE_NO_WINDOW,
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
        throw BlockFailure(block,
            "io:CreateProcess error " + std::to_string(GetLastError()),
            pass_outputs);
    }
    CloseHandle(pi.hThread);

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
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        std::string stderr_data;
        try { stderr_data = err_fut.get(); } catch (...) {}
        try { out_fut.get(); } catch (...) {}
        CloseHandle(hOutR); CloseHandle(hErrR);
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
