#pragma once

// Shared plumbing for the src/platform/win implementations. Not installed
// on any include path on purpose - only the Windows platform TUs include it.

#ifndef WIN32_LEAN_AND_MEAN
   #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
   #define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace WinCommon
{
   // UTF-8 <-> UTF-16. The app speaks UTF-8 everywhere (ImGui, JSON patch
   // files); the Win32 layer is wide-char.
   inline std::wstring Utf8ToWide(const std::string& utf8)
   {
      if (utf8.empty())
         return {};
      const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
      std::wstring wide(len, L'\0');
      if (len > 0)
         MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), wide.data(), len);
      return wide;
   }

   inline std::string WideToUtf8(const std::wstring& wide)
   {
      if (wide.empty())
         return {};
      const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                          nullptr, 0, nullptr, nullptr);
      std::string utf8(len, '\0');
      if (len > 0)
         WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), utf8.data(), len,
                             nullptr, nullptr);
      return utf8;
   }

   // Formats an HRESULT into "0x%08X: message" for outError strings.
   inline std::string HrToString(const char* what, HRESULT hr)
   {
      char buf[256];
      LPWSTR msg = nullptr;
      FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                     (LPWSTR)&msg, 0, nullptr);
      std::string msgUtf8 = msg ? WideToUtf8(msg) : std::string();
      if (msg)
         LocalFree(msg);
      // Trim the trailing newline FormatMessage loves to append.
      while (!msgUtf8.empty() && (msgUtf8.back() == '\n' || msgUtf8.back() == '\r' ||
                                  msgUtf8.back() == ' '))
         msgUtf8.pop_back();
      snprintf(buf, sizeof(buf), "%s failed (0x%08lX)%s%s", what, (unsigned long)hr,
               msgUtf8.empty() ? "" : ": ", msgUtf8.c_str());
      return buf;
   }
}
