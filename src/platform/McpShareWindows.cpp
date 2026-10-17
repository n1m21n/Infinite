// Infinite-Turbo 0.59: see McpShare.h.

#include "McpShare.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "platform/SettingsPaths.h"

#include <windows.h>

namespace
{
   std::mutex gMutex;
   std::atomic<int> gState { McpShare::kOff };
   std::string gPublicUrl, gError;
   HANDLE gJob = nullptr;
   std::atomic<unsigned> gGeneration { 0 };

   std::wstring Wide(const std::string& s)
   {
      if (s.empty())
         return std::wstring();
      const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
      std::wstring w((size_t)n, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
      return w;
   }

   std::string Env(const char* name)
   {
      const std::filesystem::path p = InfiniteEnvPath(name);
      return p.empty() ? std::string() : p.u8string();
   }

   bool IsFile(const std::string& path)
   {
      std::error_code ec;
      return !path.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
   }

   std::string TokenPath() { return InfiniteSettingsDirectory() + "/mcp_http_token"; }

   std::string MakeToken()
   {
      std::random_device rd;
      static const char* kHex = "0123456789abcdef";
      std::string t;
      for (int i = 0; i < 32; i++)
         t.push_back(kHex[rd() & 15]);
      return t;
   }

   bool Spawn(const std::wstring& commandLine, HANDLE stdErrWrite, PROCESS_INFORMATION& pi)
   {
      STARTUPINFOW si = {};
      si.cb = sizeof(si);
      BOOL inherit = FALSE;
      if (stdErrWrite != nullptr)
      {
         si.dwFlags = STARTF_USESTDHANDLES;
         si.hStdError = stdErrWrite;
         si.hStdOutput = stdErrWrite;
         si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
         inherit = TRUE;
      }
      std::vector<wchar_t> buf(commandLine.begin(), commandLine.end());
      buf.push_back(L'\0');
      if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, inherit, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                          nullptr, nullptr, &si, &pi))
         return false;
      if (gJob != nullptr)
         AssignProcessToJobObject(gJob, pi.hProcess);
      ResumeThread(pi.hThread);
      CloseHandle(pi.hThread);
      return true;
   }

   // cloudflared prints the quick tunnel's address on stderr, in a box:
   //   https://some-words-here.trycloudflare.com
   void TunnelReader(HANDLE readPipe, HANDLE process, unsigned generation, std::string token)
   {
      std::string text;
      char chunk[4096];
      DWORD got = 0;
      bool found = false;
      while (ReadFile(readPipe, chunk, sizeof(chunk), &got, nullptr) && got > 0)
      {
         if (found)
            continue; // keep draining so cloudflared never blocks on a full pipe
         text.append(chunk, got);
         const size_t end = text.find(".trycloudflare.com");
         if (end != std::string::npos)
         {
            const size_t start = text.rfind("https://", end);
            if (start != std::string::npos && gGeneration.load() == generation)
            {
               std::lock_guard<std::mutex> lock(gMutex);
               gPublicUrl = text.substr(start, end + 18 - start) + "/mcp/" + token;
               gState.store(McpShare::kPublic);
               found = true;
            }
         }
         if (text.size() > 65536)
            text.erase(0, text.size() - 4096);
      }
      CloseHandle(readPipe);
      CloseHandle(process);
      if (!found && gGeneration.load() == generation && gState.load() != McpShare::kOff)
      {
         std::lock_guard<std::mutex> lock(gMutex);
         gError = "the Cloudflare tunnel stopped before giving an address (no internet?)";
         gState.store(McpShare::kLocalOnly);
      }
   }
}

namespace McpShare
{
   int Port()
   {
      if (const char* env = std::getenv("INFINITE_MCP_HTTP_PORT"))
         if (int p = std::atoi(env); p > 0)
            return p;
      return 7780;
   }

   std::string Token()
   {
      std::ifstream in(std::filesystem::u8path(TokenPath()));
      std::string t;
      std::getline(in, t);
      if (t.size() >= 16)
         return t;
      t = MakeToken();
      std::ofstream out(std::filesystem::u8path(TokenPath()), std::ios::trunc);
      out << t << "\n";
      return t;
   }

   void NewToken()
   {
      std::ofstream out(std::filesystem::u8path(TokenPath()), std::ios::trunc);
      out << MakeToken() << "\n";
   }

   std::string FindCloudflared()
   {
      if (const char* forced = std::getenv("INFINITE_CLOUDFLARED_PATH"))
         if (IsFile(forced))
            return forced;
      wchar_t found[MAX_PATH * 2] = {};
      if (SearchPathW(nullptr, L"cloudflared", L".exe", (DWORD)(sizeof(found) / sizeof(found[0])), found, nullptr) > 0)
         return std::filesystem::path(found).u8string();
      const std::vector<std::pair<const char*, const char*>> places = {
         { "ProgramFiles", "cloudflared\\cloudflared.exe" },
         { "ProgramFiles(x86)", "cloudflared\\cloudflared.exe" },
         { "LOCALAPPDATA", "Microsoft\\WinGet\\Links\\cloudflared.exe" },
      };
      for (const auto& p : places)
      {
         const std::string base = Env(p.first);
         if (!base.empty())
         {
            const std::string path = (std::filesystem::u8path(base) / p.second).u8string();
            if (IsFile(path))
               return path;
         }
      }
      return std::string();
   }

   bool Start(std::string& error)
   {
      Stop();
      const unsigned generation = ++gGeneration;
      {
         std::lock_guard<std::mutex> lock(gMutex);
         gPublicUrl.clear();
         gError.clear();
      }
      gJob = CreateJobObjectW(nullptr, nullptr);
      if (gJob != nullptr)
      {
         JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
         limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
         SetInformationJobObject(gJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
      }
      wchar_t exe[MAX_PATH * 2] = {};
      GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
      const std::string token = Token();
      const std::wstring serverCmd = L"\"" + std::wstring(exe) + L"\" --mcp-http " + std::to_wstring(Port()) + L" " + Wide(token);
      PROCESS_INFORMATION pi = {};
      if (!Spawn(serverCmd, nullptr, pi))
      {
         error = "could not start the MCP HTTP server";
         gState.store(kFailed);
         return false;
      }
      CloseHandle(pi.hProcess);

      const std::string cloudflared = FindCloudflared();
      if (cloudflared.empty())
      {
         gState.store(kLocalOnly);
         std::lock_guard<std::mutex> lock(gMutex);
         gError = "cloudflared is not installed, so there is no public address yet";
         return true;
      }
      SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
      HANDLE readPipe = nullptr, writePipe = nullptr;
      if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
      {
         gState.store(kLocalOnly);
         return true;
      }
      SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
      const std::wstring tunnelCmd = L"\"" + Wide(cloudflared) + L"\" tunnel --no-autoupdate --url http://127.0.0.1:" +
                                     std::to_wstring(Port());
      PROCESS_INFORMATION tpi = {};
      const bool ok = Spawn(tunnelCmd, writePipe, tpi);
      CloseHandle(writePipe);
      if (!ok)
      {
         CloseHandle(readPipe);
         gState.store(kLocalOnly);
         std::lock_guard<std::mutex> lock(gMutex);
         gError = "could not start cloudflared";
         return true;
      }
      gState.store(kStarting);
      std::thread(TunnelReader, readPipe, tpi.hProcess, generation, token).detach();
      return true;
   }

   void Stop()
   {
      ++gGeneration;
      if (gJob != nullptr)
      {
         TerminateJobObject(gJob, 0);
         CloseHandle(gJob);
         gJob = nullptr;
      }
      gState.store(kOff);
      std::lock_guard<std::mutex> lock(gMutex);
      gPublicUrl.clear();
   }

   State Current() { return (State)gState.load(); }

   std::string LocalUrl() { return "http://127.0.0.1:" + std::to_string(Port()) + "/mcp/" + Token(); }

   std::string PublicUrl()
   {
      std::lock_guard<std::mutex> lock(gMutex);
      return gPublicUrl;
   }

   std::string LastError()
   {
      std::lock_guard<std::mutex> lock(gMutex);
      return gError;
   }
}
