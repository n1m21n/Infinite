// PtyHost on Windows: ConPTY. Its calls are looked up at run time so the app still starts on
// Windows older than 10 1809, where the terminal only reports that it is unavailable.
#include "PtyHost.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>

namespace term {

namespace {

constexpr DWORD_PTR kAttributePseudoConsole = 0x00020016;  // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
constexpr DWORD kExtendedStartup = 0x00080000;             // EXTENDED_STARTUPINFO_PRESENT
constexpr DWORD kUnicodeEnvironment = 0x00000400;          // CREATE_UNICODE_ENVIRONMENT

using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, void**);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(void*, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(void*);

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n);
    return out;
}

// PowerShell 7 when it is on the PATH, Windows PowerShell otherwise.
std::wstring DefaultShell() {
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"pwsh.exe", nullptr, MAX_PATH, found, nullptr) > 0) return found;
    if (SearchPathW(nullptr, L"powershell.exe", nullptr, MAX_PATH, found, nullptr) > 0) return found;
    return L"cmd.exe";
}

std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    int slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append((size_t)(c == L'"' ? slashes * 2 + 1 : slashes), L'\\');
        slashes = 0;
        out += c;
    }
    out.append((size_t)slashes * 2, L'\\');
    return out + L"\"";
}

// The app's environment with the session's values on top, in the sorted block CreateProcess wants.
std::wstring EnvironmentBlock(const std::vector<std::pair<std::string, std::string>>& extra) {
    struct NoCase {
        bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
    };
    std::map<std::wstring, std::wstring, NoCase> vars;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p != L'\0'; p += wcslen(p) + 1) {
            const std::wstring entry = p;
            const size_t eq = entry.find(L'=', 1);  // names like =C: start with '='
            if (eq != std::wstring::npos) vars[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& kv : extra) vars[Wide(kv.first)] = Wide(kv.second);
    std::wstring out;
    for (const auto& kv : vars) {
        out += kv.first + L"=" + kv.second;
        out += L'\0';
    }
    out += L'\0';
    return out;
}

class PtyHostWin final : public PtyHost {
public:
    ~PtyHostWin() override {
        ClosePty();
        if (input_) CloseHandle(input_);
        if (output_) CloseHandle(output_);
        if (process_) CloseHandle(process_);
    }

    bool Start(const PtySpawn& spawn, std::string* error) override {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        auto create = reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(kernel, "CreatePseudoConsole"));
        resize_ = reinterpret_cast<ResizePseudoConsoleFn>(GetProcAddress(kernel, "ResizePseudoConsole"));
        close_ = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
        if (create == nullptr || resize_ == nullptr || close_ == nullptr) {
            if (error) *error = "The terminal needs Windows 10 version 1809 or newer.";
            return false;
        }

        HANDLE childInput = nullptr, childOutput = nullptr;
        if (!CreatePipe(&childInput, &input_, nullptr, 0) || !CreatePipe(&output_, &childOutput, nullptr, 0)) {
            if (error) *error = "Could not create the terminal's pipes.";
            return false;
        }
        const COORD size{(SHORT)spawn.cols, (SHORT)spawn.rows};
        const HRESULT made = create(size, childInput, childOutput, 0, &pty_);
        CloseHandle(childInput);
        CloseHandle(childOutput);
        if (FAILED(made)) {
            if (error) *error = "Could not create the pseudo console.";
            return false;
        }

        SIZE_T attributeSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
        std::vector<char> attributes(attributeSize);
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof startup;
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize) ||
            !UpdateProcThreadAttribute(startup.lpAttributeList, 0, kAttributePseudoConsole, pty_, sizeof(void*), nullptr, nullptr)) {
            if (error) *error = "Could not attach the pseudo console.";
            return false;
        }

        std::wstring commandLine = QuoteArg(spawn.shell.empty() ? DefaultShell() : Wide(spawn.shell));
        for (const std::string& arg : spawn.args) commandLine += L" " + QuoteArg(Wide(arg));
        std::wstring cwd = Wide(spawn.cwd);
        if (cwd.empty()) {
            if (const wchar_t* home = _wgetenv(L"USERPROFILE")) cwd = home;
        }
        std::wstring environment = EnvironmentBlock(spawn.env);

        PROCESS_INFORMATION info{};
        const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, kExtendedStartup | kUnicodeEnvironment,
                                            environment.data(), cwd.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &info);
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (!started) {
            if (error) *error = "Could not start the shell.";
            return false;
        }
        CloseHandle(info.hThread);
        process_ = info.hProcess;
        pid_ = info.dwProcessId;
        return true;
    }

    int Read(char* buffer, int size) override {
        // A pipe ReadFile cannot be woken, and ConPTY keeps the pipe open after the child exits. Peek instead.
        while (!stopped_.load()) {
            DWORD available = 0;
            if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr)) return 0;
            if (available > 0) {
                DWORD got = 0;
                if (!ReadFile(output_, buffer, (DWORD)std::min<DWORD>((DWORD)size, available), &got, nullptr)) return 0;
                return (int)got;
            }
            if (WaitForSingleObject(process_, 4) == WAIT_OBJECT_0) {
                PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr);
                if (available == 0) return 0;
            }
        }
        return 0;
    }

    bool Write(const char* data, int size) override {
        while (size > 0) {
            DWORD wrote = 0;
            if (!WriteFile(input_, data, (DWORD)size, &wrote, nullptr)) return false;
            data += wrote;
            size -= (int)wrote;
        }
        return true;
    }

    void Resize(int cols, int rows) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (pty_ != nullptr) resize_(pty_, COORD{(SHORT)cols, (SHORT)rows});
    }

    void Kill(bool force) override {
        // Closing the console sends the shell its close event; the reader keeps draining meanwhile.
        ClosePty();
        if (force) {
            if (process_) TerminateProcess(process_, 1);
            stopped_.store(true);
        }
    }

    bool Busy() override {
        // No foreground process group here: anything the shell started counts.
        if (process_ == nullptr || WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) return false;
        HANDLE all = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (all == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof entry;
        bool busy = false;
        for (BOOL more = Process32FirstW(all, &entry); more && !busy; more = Process32NextW(all, &entry))
            busy = entry.th32ParentProcessID == pid_ && _wcsicmp(entry.szExeFile, L"conhost.exe") != 0;
        CloseHandle(all);
        return busy;
    }

    int ExitCode() override {
        if (process_ == nullptr) return -1;
        WaitForSingleObject(process_, INFINITE);
        DWORD code = 0;
        return GetExitCodeProcess(process_, &code) ? (int)code : -1;
    }

private:
    void ClosePty() {
        void* pty = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            pty = pty_;
            pty_ = nullptr;
        }
        if (pty != nullptr) close_(pty);
    }

    ResizePseudoConsoleFn resize_ = nullptr;
    ClosePseudoConsoleFn close_ = nullptr;
    std::mutex mutex_;
    void* pty_ = nullptr;
    HANDLE input_ = nullptr;
    HANDLE output_ = nullptr;
    HANDLE process_ = nullptr;
    DWORD pid_ = 0;
    std::atomic<bool> stopped_{false};
};

}  // namespace

std::unique_ptr<PtyHost> PtyHost::Create() { return std::make_unique<PtyHostWin>(); }

}  // namespace term
