// Infinite.com: the console-subsystem front door to Infinite.exe (R489).
//
// Infinite.exe is a GUI-subsystem binary, so cmd.exe and PowerShell do not wait
// for it: a script never sees its exit code and, run bare, no stdout. cmd and
// PowerShell resolve `Infinite` to Infinite.com before Infinite.exe (PATHEXT),
// so `Infinite --frame p.inf 0 out.png` lands here. This shim runs the real exe
// from the same folder with the same command line, waits, and returns its exit
// code. Same trick as devenv.com.
//
// Output: a redirected stdout/stderr (file or pipe) is handed to the child. A
// real console is not inherited (a GUI process is not attached to it); the
// child attaches to this process's console itself, see
// Platform::AttachConsoleForHeadless.

#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>

namespace
{
bool IsConsoleHandle(HANDLE h)
{
   DWORD mode = 0;
   return h != nullptr && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) != 0;
}

bool IsUsable(HANDLE h)
{
   return h != nullptr && h != INVALID_HANDLE_VALUE;
}
} // namespace

int main()
{
   wchar_t self[32768];
   const DWORD n = GetModuleFileNameW(nullptr, self, (DWORD)(sizeof(self) / sizeof(self[0])));
   if (n == 0 || n >= sizeof(self) / sizeof(self[0]))
   {
      std::fputs("Infinite.com: cannot find its own path\n", stderr);
      return 127;
   }
   std::wstring dir(self, n);
   const size_t slash = dir.find_last_of(L"\\/");
   dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
   const std::wstring target = dir + L"Infinite.exe";

   // Everything after argv[0] in the raw command line, untouched, so quoting
   // reaches the child exactly as the caller wrote it.
   const wchar_t* rest = GetCommandLineW();
   if (*rest == L'"')
   {
      ++rest;
      while (*rest != L'\0' && *rest != L'"')
         ++rest;
      if (*rest == L'"')
         ++rest;
   }
   else
   {
      while (*rest != L'\0' && *rest != L' ' && *rest != L'\t')
         ++rest;
   }
   while (*rest == L' ' || *rest == L'\t')
      ++rest;

   std::wstring cmd = L"\"" + target + L"\"";
   if (*rest != L'\0')
      cmd += std::wstring(L" ") + rest;
   std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
   cmdBuf.push_back(L'\0');

   const HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
   const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
   const HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);

   STARTUPINFOW si = {};
   si.cb = sizeof(si);
   BOOL inherit = FALSE;
   if (!(IsConsoleHandle(hOut) && IsConsoleHandle(hErr)))
   {
      // At least one stream is redirected: pass all three through.
      for (HANDLE h : { hIn, hOut, hErr })
         if (IsUsable(h))
            SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdInput = hIn;
      si.hStdOutput = hOut;
      si.hStdError = hErr;
      inherit = TRUE;
   }

   PROCESS_INFORMATION pi = {};
   if (!CreateProcessW(target.c_str(), cmdBuf.data(), nullptr, nullptr, inherit, 0, nullptr, nullptr, &si, &pi))
   {
      std::fprintf(stderr, "Infinite.com: cannot start Infinite.exe next to it (error %lu)\n", GetLastError());
      return 127;
   }
   CloseHandle(pi.hThread);
   WaitForSingleObject(pi.hProcess, INFINITE);
   DWORD code = 1;
   GetExitCodeProcess(pi.hProcess, &code);
   CloseHandle(pi.hProcess);
   return (int)code;
}
