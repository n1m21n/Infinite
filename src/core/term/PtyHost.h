// PtyHost: one child process on a pseudo terminal. POSIX openpty on macOS and
// Linux, ConPTY on Windows (docs/plans/terminal/README.md D7, D8).
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace term {

struct PtySpawn {
    std::string shell;                                     // empty = the user's login shell
    std::vector<std::string> args;                         // after argv[0]; empty = login-shell flags
    std::string cwd;                                       // empty = home
    std::vector<std::pair<std::string, std::string>> env;  // on top of the app's environment
    int cols = 80;
    int rows = 24;
};

class PtyHost {
public:
    static std::unique_ptr<PtyHost> Create();
    virtual ~PtyHost() = default;

    virtual bool Start(const PtySpawn& spawn, std::string* error) = 0;
    // Blocks until output arrives. 0 = the child is gone or Kill(true) was called.
    virtual int Read(char* buffer, int size) = 0;
    // Blocks until written; false once the child is gone. One thread only.
    virtual bool Write(const char* data, int size) = 0;
    virtual void Resize(int cols, int rows) = 0;
    // false: ask politely (hang up). true: force, and Read returns 0 from now on.
    virtual void Kill(bool force) = 0;
    // True while a program other than the shell has the terminal.
    virtual bool Busy() = 0;
    // Waits for the child, which must be gone or killed. -1 = unknown.
    virtual int ExitCode() = 0;
};

}  // namespace term
