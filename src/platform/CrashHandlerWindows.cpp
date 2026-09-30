// Turbo (from upstream's CrashHandlerWin.cpp): Infinite-Turbo is a GUI
// process with no console, so an unhandled exception used to just make the
// window vanish. This leaves a minidump plus a log line behind in
// %LOCALAPPDATA%\Infinite\crash\ - enough to see where it died (open the
// .dmp in Visual Studio with the matching .pdb). Not a recovery mechanism:
// the default handler still terminates the process.
#include <cstdio>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#if defined(_MSC_VER)
#pragma comment(lib, "dbghelp.lib")
#endif

#include "Platform.h"
#include "SettingsPaths.h"

namespace
{
   // Resolved once at install, on a healthy heap: the handler itself runs
   // after something already broke (often the heap), so it only reads these.
   wchar_t gCrashDir[MAX_PATH] = {};
   bool gInstalled = false;

   void AppendLine(const wchar_t* path, const char* text)
   {
      HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE)
         return;
      DWORD written = 0;
      WriteFile(h, text, (DWORD)strlen(text), &written, nullptr);
      CloseHandle(h);
   }

   LONG WINAPI WriteCrashReport(EXCEPTION_POINTERS* info)
   {
      if (gCrashDir[0] == 0)
         return EXCEPTION_CONTINUE_SEARCH;
      CreateDirectoryW(gCrashDir, nullptr);

      SYSTEMTIME st;
      GetLocalTime(&st);
      wchar_t dumpPath[MAX_PATH];
      _snwprintf(dumpPath, MAX_PATH, L"%s\\crash-%04d%02d%02d-%02d%02d%02d.dmp", gCrashDir, st.wYear, st.wMonth,
                 st.wDay, st.wHour, st.wMinute, st.wSecond);
      dumpPath[MAX_PATH - 1] = 0;

      bool dumpWritten = false;
      HANDLE dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (dumpFile != INVALID_HANDLE_VALUE)
      {
         MINIDUMP_EXCEPTION_INFORMATION mei {};
         mei.ThreadId = GetCurrentThreadId();
         mei.ExceptionPointers = info;
         mei.ClientPointers = FALSE;
         dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                                         (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                                         info ? &mei : nullptr, nullptr, nullptr) != FALSE;
         CloseHandle(dumpFile);
      }

      const unsigned long code = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionCode : 0;
      const void* addr = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionAddress : nullptr;
      char line[512];
      snprintf(line, sizeof(line), "[%04d-%02d-%02d %02d:%02d:%02d] unhandled exception 0x%08lX at %p - dump %s\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, code, addr,
               dumpWritten ? "written" : "FAILED");
      wchar_t logPath[MAX_PATH];
      _snwprintf(logPath, MAX_PATH, L"%s\\crash.log", gCrashDir);
      logPath[MAX_PATH - 1] = 0;
      AppendLine(logPath, line);
      return EXCEPTION_CONTINUE_SEARCH; // let Windows Error Reporting / the debugger see it too
   }
}

namespace Platform
{
   void InstallCrashHandler()
   {
      if (gInstalled)
         return;
      const std::string dir = InfiniteSettingsDirectory();
      if (dir.empty())
         return;
      const std::string crash = dir + "\\crash";
      const int n = MultiByteToWideChar(CP_UTF8, 0, crash.c_str(), -1, gCrashDir, MAX_PATH);
      if (n <= 0)
      {
         gCrashDir[0] = 0;
         return;
      }
      SetUnhandledExceptionFilter(WriteCrashReport);
      gInstalled = true;
   }

   std::string CrashReportDirectory()
   {
      return InfiniteSettingsDirectory().empty() ? std::string() : InfiniteSettingsDirectory() + "\\crash";
   }
}
