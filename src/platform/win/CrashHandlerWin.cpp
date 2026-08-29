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
#include "WinCommon.h"

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <cstdio>
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

      HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
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
   LONG WINAPI WriteCrashReport(EXCEPTION_POINTERS* info)
   {
      const std::string appDir = AppPaths::AppSupportDir();
      if (appDir.empty())
         return EXCEPTION_EXECUTE_HANDLER;

      const std::string crashDir = appDir + "/crash";
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
      char msg[256];
      snprintf(msg, sizeof(msg),
               "unhandled exception 0x%08lX at %p - %s%s",
               (unsigned long)code, addr,
               dumpWritten ? "dump written: " : "dump FAILED to write: ", dumpName.c_str());
      const std::string logLine = TimestampPrefix() + "[CRASH] " + msg + "\r\n";
      AppendToFileW(WinCommon::Utf8ToWide(appDir + "/log.txt"), logLine);

      return EXCEPTION_EXECUTE_HANDLER;
   }
}

namespace Platform
{
   void InstallCrashHandler()
   {
      SetUnhandledExceptionFilter(WriteCrashReport);

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
      const std::string appDir = AppPaths::AppSupportDir();
      if (appDir.empty())
         return;
      AppendToFileW(WinCommon::Utf8ToWide(appDir + "/log.txt"), TimestampPrefix() + line + "\r\n");
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
}
