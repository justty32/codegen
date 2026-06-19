// Privilege-drop (setgroups/getgrouplist) and setsid are glibc/BSD extensions
// that the project-wide _POSIX_C_SOURCE would otherwise hide.  Enable the full
// feature set for this translation unit before any system header is pulled in.
#ifndef _WIN32
#define _GNU_SOURCE 1
#endif

#include "executor.hpp"
#include "utils.hpp"

#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* FALLBACK_SHEBANG = "#!/usr/bin/env python3";

// Resolved identity for run_as_user.
struct ResolvedUser {
    uid_t uid;
    gid_t gid;
    std::vector<gid_t> groups;  // supplementary groups (incl. primary)
};

// Resolve a username or numeric uid string to its identity.
// Returns false when the user does not exist.
static bool resolve_user(const std::string& spec, ResolvedUser& out) {
    struct passwd* pw = nullptr;
    bool all_digits = !spec.empty() &&
        std::all_of(spec.begin(), spec.end(),
                    [](unsigned char c) { return std::isdigit(c) != 0; });
    if (all_digits)
        pw = getpwuid(static_cast<uid_t>(std::stoul(spec)));
    else
        pw = getpwnam(spec.c_str());
    if (!pw) return false;

    out.uid = pw->pw_uid;
    out.gid = pw->pw_gid;

    int ngroups = 32;
    std::vector<gid_t> groups(static_cast<size_t>(ngroups));
    if (getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &ngroups) < 0) {
        // groups buffer too small; ngroups now holds the required size.
        groups.resize(static_cast<size_t>(ngroups));
        getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &ngroups);
    }
    groups.resize(static_cast<size_t>(ngroups));
    out.groups = std::move(groups);
    return true;
}

// Write script to a temp file, make it executable, return the path.
// execve relies on the shebang line, so the file MUST have the execute bit set.
// When world_accessible is set, a dropped-privilege child (a *different* user)
// must additionally be able to read+exec it, so use 0755 instead of 0700.
static std::string write_script(const std::string& shebang, const std::string& body,
                                bool world_accessible) {
    std::string content = (shebang.empty() ? FALLBACK_SHEBANG : shebang) + "\n" + body;

    char tmpl[] = "/tmp/codegen_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) throw std::runtime_error(std::string("mkstemp: ") + strerror(errno));

    // mkstemp creates the file 0600; add the execute bits so execve works.
    mode_t mode = world_accessible ? mode_t(0755) : S_IRWXU;
    if (fchmod(fd, mode) != 0) {
        int e = errno;
        close(fd);
        unlink(tmpl);
        throw std::runtime_error(std::string("fchmod: ") + strerror(e));
    }

    const char* p = content.data();
    size_t remaining = content.size();
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmpl);
            throw std::runtime_error("write script");
        }
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    close(fd);
    return std::string(tmpl);
}

static void read_until_closed(int fd, std::string& out) {
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
}

// SIGKILL the whole process group led by *pid*.  The child calls setsid(), so
// it leads its own group (pgid == pid); killing the group reaps any background
// children the script spawned which a plain kill(pid) would orphan.
static void kill_process_group(pid_t pid) {
    kill(-pid, SIGKILL);  // whole group
    kill(pid, SIGKILL);   // fallback: direct child, in case setsid hasn't run
}

std::pair<std::string, double> run_block(
    const Block& block,
    const std::map<std::string, std::string>& env,
    const fs::path& cwd,
    double max_pass_time,
    std::vector<std::string>& pass_outputs,
    const std::optional<std::string>& run_as_user)
{
    const bool drop = run_as_user.has_value();

    // Resolve the target identity up front so an unknown user fails cleanly,
    // before we fork or write any temp files.
    ResolvedUser ruser{};
    if (drop) {
        if (!resolve_user(*run_as_user, ruser))
            throw BlockFailure(block, "user:unknown:" + *run_as_user, pass_outputs);
    }

    std::string script_path;
    try {
        script_path = write_script(block.shebang, block.body, /*world_accessible=*/drop);
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

    // Pipes for stdout and stderr, plus a CLOEXEC "exec error report" pipe the
    // child uses to signal a privilege-drop failure: on success exec closes the
    // write end (CLOEXEC) and the parent reads EOF; on failure the child writes
    // one byte before _exit, and the parent maps it to user:denied.
    int out_pipe[2], err_pipe[2], rpt_pipe[2];
    if (pipe(out_pipe) || pipe(err_pipe) || pipe(rpt_pipe)) {
        unlink(script_path.c_str());
        throw BlockFailure(block, std::string("io:pipe: ") + strerror(errno), pass_outputs);
    }
    fcntl(rpt_pipe[1], F_SETFD, fcntl(rpt_pipe[1], F_GETFD) | FD_CLOEXEC);

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
        close(rpt_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        // Lead our own session/process group so a timeout can SIGKILL the
        // whole tree, not just this process.
        setsid();

        if (!cwd.empty()) {
            if (chdir(cwd.c_str()) != 0) _exit(127);
        }

        // Drop privilege (must precede execve, order: groups -> gid -> uid).
        if (drop) {
            if (setgroups(ruser.groups.size(), ruser.groups.data()) != 0 ||
                setgid(ruser.gid) != 0 ||
                setuid(ruser.uid) != 0) {
                char c = 'U';
                ssize_t w = write(rpt_pipe[1], &c, 1);
                (void)w;
                _exit(126);
            }
        }
        close(rpt_pipe[1]);  // belt-and-suspenders; CLOEXEC also closes on exec

        const char* argv[] = {script_path.c_str(), nullptr};
        execve(script_path.c_str(),
               const_cast<char* const*>(argv),
               envp.data());
        _exit(127);
    }

    // Parent
    close(out_pipe[1]);
    close(err_pipe[1]);
    close(rpt_pipe[1]);
    // NOTE: do not unlink the script here — the child may not have execve'd it
    // yet, which would cause ENOENT. We delete it after the child exits.

    // Read the exec-error report. A single byte means privilege drop failed;
    // EOF (read==0) means the child exec'd (or proceeded) successfully.
    {
        char c;
        ssize_t r;
        do { r = read(rpt_pipe[0], &c, 1); } while (r < 0 && errno == EINTR);
        close(rpt_pipe[0]);
        if (r == 1) {
            waitpid(pid, nullptr, 0);
            close(out_pipe[0]);
            close(err_pipe[0]);
            unlink(script_path.c_str());
            throw BlockFailure(block, "user:denied:" + *run_as_user, pass_outputs);
        }
    }

    // Read stdout and stderr with timeout using poll
    std::string stdout_data, stderr_data;

    auto deadline = start + std::chrono::duration<double>(max_pass_time);

    struct pollfd fds[2];
    bool out_done = false, err_done = false;

    while (!out_done || !err_done) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            kill_process_group(pid);
            waitpid(pid, nullptr, 0);
            close(out_pipe[0]);
            close(err_pipe[0]);
            unlink(script_path.c_str()); // safe: child has been reaped
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
    unlink(script_path.c_str()); // safe: child has exited

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
// Windows stub — the tool targets POSIX only (per DESIGN.md §1).
#include <stdexcept>
std::pair<std::string, double> run_block(
    const Block&,
    const std::map<std::string, std::string>&,
    const fs::path&,
    double,
    std::vector<std::string>&,
    const std::optional<std::string>&)
{
    throw std::runtime_error("codegen: subprocess execution is not supported on Windows");
}
#endif
