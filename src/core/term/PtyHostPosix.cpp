// POSIX side of PtyHost. fork() is followed only by async-signal-safe calls, so
// it is safe from a thread of this multi-threaded process; it runs on the
// session thread, never on main.
#include "PtyHost.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

extern char** environ;

namespace term {
namespace {

std::string LoginShell() {
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_shell && pw->pw_shell[0]) return pw->pw_shell;
    if (const char* shell = getenv("SHELL"); shell && shell[0]) return shell;
    return "/bin/sh";
}

std::string HomeDir() {
    if (const char* home = getenv("HOME"); home && home[0]) return home;
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) return pw->pw_dir;
    return "/";
}

class PtyPosix final : public PtyHost {
public:
    ~PtyPosix() override {
        if (master_ >= 0) close(master_);
    }

    bool Start(const PtySpawn& spawn, std::string* error) override {
        winsize size{};
        size.ws_col = (unsigned short)spawn.cols;
        size.ws_row = (unsigned short)spawn.rows;
        int slave = -1;
        if (openpty(&master_, &slave, nullptr, nullptr, &size) != 0) {
            if (error) *error = std::string("openpty failed: ") + strerror(errno);
            return false;
        }
        fcntl(master_, F_SETFD, FD_CLOEXEC);
        fcntl(master_, F_SETFL, fcntl(master_, F_GETFL) | O_NONBLOCK);

        // Everything the child needs is built before fork.
        const std::string shell = spawn.shell.empty() ? LoginShell() : spawn.shell;
        std::vector<std::string> argvText{shell};
        if (spawn.args.empty() && spawn.shell.empty()) argvText.push_back("-l");
        argvText.insert(argvText.end(), spawn.args.begin(), spawn.args.end());
        std::vector<char*> argv;
        for (auto& a : argvText) argv.push_back(a.data());
        argv.push_back(nullptr);

        std::vector<std::string> envText;
        const auto overridden = [&](const char* entry) {
            const char* eq = strchr(entry, '=');
            const size_t n = eq ? (size_t)(eq - entry) : strlen(entry);
            for (const auto& kv : spawn.env)
                if (kv.first.size() == n && !strncmp(kv.first.c_str(), entry, n)) return true;
            return false;
        };
        for (char** e = environ; e && *e; ++e)
            if (!overridden(*e)) envText.push_back(*e);
        for (const auto& kv : spawn.env) envText.push_back(kv.first + "=" + kv.second);
        std::vector<char*> envp;
        for (auto& e : envText) envp.push_back(e.data());
        envp.push_back(nullptr);

        const std::string cwd = spawn.cwd.empty() ? HomeDir() : spawn.cwd;
        rlimit limit{};
        const int maxFd = getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < 65536 ? (int)limit.rlim_cur : 65536;

        const pid_t pid = fork();
        if (pid < 0) {
            if (error) *error = std::string("fork failed: ") + strerror(errno);
            close(slave);
            return false;
        }
        if (pid == 0) {
            setsid();
            ioctl(slave, TIOCSCTTY, 0);
            dup2(slave, 0);
            dup2(slave, 1);
            dup2(slave, 2);
            for (int fd = 3; fd < maxFd; ++fd) close(fd);  // D7: nothing of the app leaks into the shell
            for (int sig = 1; sig < NSIG; ++sig) signal(sig, SIG_DFL);
            sigset_t none;
            sigemptyset(&none);
            sigprocmask(SIG_SETMASK, &none, nullptr);
            if (chdir(cwd.c_str()) != 0) (void)!chdir("/");
            execve(argv[0], argv.data(), envp.data());
            _exit(127);
        }
        close(slave);
        pid_.store(pid);
        return true;
    }

    int Read(char* buffer, int size) override {
        for (;;) {
            if (killed_.load()) return 0;
            pollfd pfd{master_, POLLIN, 0};
            const int ready = poll(&pfd, 1, 200);
            if (ready < 0 && errno != EINTR) return 0;
            if (ready > 0) {
                const ssize_t n = read(master_, buffer, (size_t)size);
                if (n > 0) return (int)n;
                if (n == 0 || (errno != EAGAIN && errno != EINTR)) return 0;  // EIO = last slave closed
                continue;
            }
            // A daemon can keep the slave open after the shell has gone.
            if (ready == 0 && Reap(false)) return 0;
        }
    }

    bool Write(const char* data, int size) override {
        while (size > 0) {
            if (killed_.load() || master_ < 0) return false;
            const ssize_t n = write(master_, data, (size_t)size);
            if (n > 0) {
                data += n;
                size -= (int)n;
            } else if (errno == EAGAIN) {
                pollfd pfd{master_, POLLOUT, 0};
                poll(&pfd, 1, 200);
            } else if (errno != EINTR) {
                return false;
            }
        }
        return true;
    }

    void Resize(int cols, int rows) override {
        winsize size{};
        size.ws_col = (unsigned short)cols;
        size.ws_row = (unsigned short)rows;
        ioctl(master_, TIOCSWINSZ, &size);
    }

    void Kill(bool force) override {
        const pid_t pid = pid_.load();
        if (force) killed_.store(true);
        if (pid > 0 && !reaped_.load()) {
            kill(-pid, force ? SIGKILL : SIGHUP);
            kill(pid, force ? SIGKILL : SIGHUP);
        }
    }

    int ExitCode() override {
        Reap(true);
        return exitCode_.load();
    }

private:
    bool Reap(bool wait) {
        if (reaped_.load()) return true;
        const pid_t pid = pid_.load();
        if (pid <= 0) return true;
        int status = 0;
        const pid_t got = waitpid(pid, &status, wait ? 0 : WNOHANG);
        if (got == 0) return false;
        if (got == pid) {
            if (WIFEXITED(status)) exitCode_.store(WEXITSTATUS(status));
            else if (WIFSIGNALED(status)) exitCode_.store(128 + WTERMSIG(status));
        }
        reaped_.store(true);
        return true;
    }

    int master_ = -1;
    std::atomic<pid_t> pid_{-1};
    std::atomic<bool> killed_{false};
    std::atomic<bool> reaped_{false};
    std::atomic<int> exitCode_{-1};
};

}  // namespace

std::unique_ptr<PtyHost> PtyHost::Create() { return std::make_unique<PtyPosix>(); }

}  // namespace term
