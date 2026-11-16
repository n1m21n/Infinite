// Windows-only startup/crash diagnostics: main.cpp is linked with
// WIN32_EXECUTABLE (CMakeLists.txt), a GUI-subsystem process with no
// console, so fprintf(stderr, ...) - the only thing every failure path in
// this codebase used before this file existed - goes nowhere a user can see
// it, and an unhandled exception unwinds silently: the window just vanishes.
// macOS gets a `.ips` crash report and a visible terminal for free from the
// OS (see Platform.mm's no-op counterparts), so all of this is Windows-only
// real work.
//
// The macOS counterpart is src/platform/Platform.mm; every function here
// must keep the exact signature declared in ../Platform.h.

#include "../Platform.h"
#include "../AppPaths.h"
#include "../PluginCrashGuard.h"
#include "WinCommon.h"

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <intrin.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

namespace
{
   // Appends `text` (already newline-terminated by the caller) to `path`,
   // wide-char throughout per windows-parity's UTF-8-path rule - a
   // std::string account name or app-support path must never reach fopen/
   // ifstream directly. Rotates (truncates) the file first if it has grown
   // past a few MB, so "rolling" doesn't mean "unbounded".
   void AppendToFileW(const std::wstring& path, const std::string& text)
   {
      DWORD createMode = OPEN_ALWAYS;
      WIN32_FILE_ATTRIBUTE_DATA existing{};
      if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &existing))
      {
         const ULONGLONG size = (((ULONGLONG)existing.nFileSizeHigh) << 32) | existing.nFileSizeLow;
         if (size > 4ull * 1024 * 1024)
            createMode = CREATE_ALWAYS;
      }

      // FILE_SHARE_WRITE matters: InstallCrashHandler reopens this same file as
      // the process's stderr via _wfreopen, and the CRT opens it deny-none.
      // Asking for FILE_SHARE_READ alone denies writers, which conflicts with
      // that live handle and fails with ERROR_SHARING_VIOLATION - i.e. every
      // AppendLogLine and the crash handler's own log line would silently
      // write nothing, which is exactly the diagnostic this file exists for.
      HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             createMode, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE)
         return;
      DWORD written = 0;
      WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr);
      CloseHandle(h);
   }

   std::string TimestampPrefix()
   {
      SYSTEMTIME st;
      GetLocalTime(&st);
      char buf[32];
      snprintf(buf, sizeof(buf), "[%04d-%02d-%02d %02d:%02d:%02d] ",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
      return buf;
   }

   // The SetUnhandledExceptionFilter callback. Writes a minidump plus a short
   // text log to AppPaths::AppSupportDir() + "/crash/", then lets the default
   // handler run (EXCEPTION_EXECUTE_HANDLER) so the process still terminates -
   // this is a "leave something behind" hook, not a recovery mechanism.
   // Resolved once in InstallCrashHandler, on a healthy heap. The handler runs
   // after something has already gone wrong - often a corrupted heap, where
   // AppPaths::AppSupportDir()'s std::string work can deadlock or fault before
   // it ever reaches MiniDumpWriteDump - so the crash path only reads these.
   std::string gCrashDir;
   std::string gLogPath;

   // `what` names how the process died; `detail` is optional extra text (an exception's what()).
   void WriteReport(EXCEPTION_POINTERS* info, const char* what, const char* detail)
   {
      if (gCrashDir.empty())
         return;

      const std::string& crashDir = gCrashDir;
      AppPaths::EnsureDir(crashDir);

      SYSTEMTIME st;
      GetLocalTime(&st);
      char stamp[32];
      snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
      const std::string dumpName = std::string("crash-") + stamp + ".dmp";
      const std::wstring dumpPath = WinCommon::Utf8ToWide(crashDir + "/" + dumpName);

      HANDLE dumpFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      bool dumpWritten = false;
      if (dumpFile != INVALID_HANDLE_VALUE)
      {
         MINIDUMP_EXCEPTION_INFORMATION mei{};
         mei.ThreadId = GetCurrentThreadId();
         mei.ExceptionPointers = info;
         mei.ClientPointers = FALSE;
         dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                                         MiniDumpNormal, info ? &mei : nullptr, nullptr,
                                         nullptr) != FALSE;
         CloseHandle(dumpFile);
      }

      const DWORD code = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionCode : 0;
      const void* addr = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionAddress
                                                          : nullptr;
      char msg[512];
      snprintf(msg, sizeof(msg),
               "%s 0x%08lX at %p%s%s - %s%s",
               what, (unsigned long)code, addr, detail != nullptr ? ": " : "", detail != nullptr ? detail : "",
               dumpWritten ? "dump written: " : "dump FAILED to write: ", dumpName.c_str());
      const std::string logLine = TimestampPrefix() + "[CRASH] " + msg + "\r\n";
      AppendToFileW(WinCommon::Utf8ToWide(gLogPath), logLine);
   }

   LONG WINAPI WriteCrashReport(EXCEPTION_POINTERS* info)
   {
      PluginCrashGuard::NoteCrash(); // first: names the plugin this thread was inside, if any
      WriteReport(info, "unhandled exception", nullptr);
      return EXCEPTION_EXECUTE_HANDLER;
   }

   // abort(), std::terminate (an uncaught C++ exception, std::bad_alloc included), a CRT invalid-parameter check and
   // a pure virtual call never reach the filter above: the release CRT ends the process through __fastfail, which
   // skips it, so the window just vanished and left neither a dump nor a log line. Each routes here instead, writes
   // the same report with the current thread's stack, and ends the process the way the CRT would have.
   constexpr DWORD kFatalTerminate = 0xE0000001;
   constexpr DWORD kFatalAbort = 0xE0000002;
   constexpr DWORD kFatalInvalidParameter = 0xE0000003;
   constexpr DWORD kFatalPureCall = 0xE0000004;
   std::atomic<bool> gFatalReported{ false };

   __declspec(noinline) void FatalReport(DWORD code, const char* what, const char* detail)
   {
      if (!gFatalReported.exchange(true))
      {
         PluginCrashGuard::NoteCrash();
         CONTEXT context{};
         RtlCaptureContext(&context);
         EXCEPTION_RECORD record{};
         record.ExceptionCode = code;
         record.ExceptionAddress = _ReturnAddress();
         EXCEPTION_POINTERS pointers{ &record, &context };
         WriteReport(&pointers, what, detail);
      }
      TerminateProcess(GetCurrentProcess(), 3);
   }

   void OnTerminate()
   {
      // The exception's own text says which failure it was ("bad allocation" is memory running out). Copied into a
      // static buffer: by now the heap may be the thing that failed.
      static char detail[160];
      const char* shown = nullptr;
      if (std::exception_ptr current = std::current_exception())
      {
         try
         {
            std::rethrow_exception(current);
         }
         catch (const std::exception& e)
         {
            strncpy_s(detail, sizeof(detail), e.what(), _TRUNCATE);
            shown = detail;
         }
         catch (...)
         {
            shown = "non-std exception";
         }
      }
      FatalReport(kFatalTerminate, "std::terminate", shown);
   }

   void OnAbortSignal(int)
   {
      FatalReport(kFatalAbort, "abort", nullptr);
   }

   void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
   {
      FatalReport(kFatalInvalidParameter, "CRT invalid parameter", nullptr);
   }

   void OnPureCall()
   {
      FatalReport(kFatalPureCall, "pure virtual call", nullptr);
   }
}

namespace Platform
{
   void InstallCrashHandler()
   {
      // Resolve the report paths before arming the filter - see gCrashDir.
      {
         const std::string dir = AppPaths::AppSupportDir();
         if (!dir.empty())
         {
            gCrashDir = dir + "/crash";
            gLogPath = dir + "/log.txt";
         }
      }
      SetUnhandledExceptionFilter(WriteCrashReport);
      std::set_terminate(OnTerminate);
      signal(SIGABRT, OnAbortSignal);
      _set_invalid_parameter_handler(OnInvalidParameter);
      _set_purecall_handler(OnPureCall);

      // Redirects the process's actual stderr stream into the rolling log,
      // rather than routing individual call sites through AppendLogLine -
      // this way every existing (and future) fprintf(stderr, ...) in the
      // app, from shader compile logs to audio-device errors, ends up
      // somewhere a user can find and attach to a report. WIN32_EXECUTABLE
      // means stderr previously had no destination at all on Windows, so
      // this only adds a sink, it never removes one.
      const std::string appDir = AppPaths::AppSupportDir();
      if (appDir.empty())
         return;
      const std::wstring logPath = WinCommon::Utf8ToWide(appDir + "/log.txt");
      const wchar_t* mode = L"a";
      WIN32_FILE_ATTRIBUTE_DATA existing{};
      if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &existing))
      {
         const ULONGLONG size = (((ULONGLONG)existing.nFileSizeHigh) << 32) | existing.nFileSizeLow;
         if (size > 4ull * 1024 * 1024)
            mode = L"w";
      }
      if (_wfreopen(logPath.c_str(), mode, stderr) != nullptr)
         setvbuf(stderr, nullptr, _IONBF, 0); // flush every line - a crash must not lose the last one
   }

   void AppendLogLine(const std::string& line)
   {
      std::string logPath = gLogPath;
      if (logPath.empty())
      {
         // Called before InstallCrashHandler (or with no app-support dir at
         // all) - resolve on the spot rather than dropping the line.
         const std::string appDir = AppPaths::AppSupportDir();
         if (appDir.empty())
            return;
         logPath = appDir + "/log.txt";
      }
      AppendToFileW(WinCommon::Utf8ToWide(logPath), TimestampPrefix() + line + "\r\n");
   }

   void RevealLogs()
   {
      const std::string appDir = AppPaths::AppSupportDir();
      if (appDir.empty())
         return;
      const std::string file = appDir + "/log.txt";
      // Created empty first: Explorer's /select, on a missing file opens the wrong folder.
      if (FILE* f = _wfopen(WinCommon::Utf8ToWide(file).c_str(), L"ab"))
         fclose(f);
      RevealInFileManager(file);
   }

   void ShowFatalError(const std::string& title, const std::string& message)
   {
      // stderr first, unconditionally - matches every existing fatal-path
      // call site this replaces, and InstallCrashHandler has already
      // redirected stderr into the rolling log, so this alone covers both
      // "print it" and "log it" without writing the line twice.
      fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
      MessageBoxW(nullptr, WinCommon::Utf8ToWide(message).c_str(),
                  WinCommon::Utf8ToWide(title).c_str(), MB_OK | MB_ICONERROR);
   }

   bool LocalTime(std::time_t time, std::tm* outTm)
   {
      if (outTm == nullptr)
         return false;
      return localtime_s(outTm, &time) == 0;
   }
}
