// Infinite-Turbo 0.47: in-app Claude chat backend (see ClaudeChat.h).

#include "ClaudeChat.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "json.hpp"

#include <windows.h>

using json = nlohmann::json;

namespace
{
   std::mutex gMutex;
   std::vector<ClaudeChat::Event> gQueue;
   std::atomic<bool> gBusy { false };
   // Owns the CLI's whole process tree. Kept until the next turn starts; on
   // app exit Windows closes it, and KILL_ON_JOB_CLOSE ends the tree.
   HANDLE gJob = nullptr;
   HANDLE gProcessForCancel = nullptr; // fallback when the job could not be used
   std::atomic<bool> gCancelled { false };
   std::atomic<bool> gSawResult { false };
   std::atomic<bool> gSawText { false };
   std::string gLogPath; // %LOCALAPPDATA%\Infinite\chat\last-turn.log, rewritten every turn

   void LogAppend(const std::string& text)
   {
      if (gLogPath.empty())
         return;
      std::ofstream log(std::filesystem::u8path(gLogPath), std::ios::binary | std::ios::app);
      log << text;
   }

   void Push(ClaudeChat::Event::Kind kind, std::string text)
   {
      std::lock_guard<std::mutex> lock(gMutex);
      gQueue.push_back({ kind, std::move(text) });
   }

   std::wstring Wide(const std::string& s)
   {
      if (s.empty())
         return std::wstring();
      const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
      std::wstring w((size_t)n, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
      return w;
   }

   std::string Narrow(const std::wstring& w)
   {
      if (w.empty())
         return std::string();
      const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
      std::string s((size_t)n, '\0');
      WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
      return s;
   }

   std::string Env(const char* name)
   {
      const char* v = std::getenv(name);
      return v != nullptr ? std::string(v) : std::string();
   }

   bool IsFile(const std::string& path)
   {
      std::error_code ec;
      return !path.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
   }

   std::string SearchOnPath(const wchar_t* name, const wchar_t* ext)
   {
      wchar_t buffer[MAX_PATH * 2];
      const DWORD n = SearchPathW(nullptr, name, ext, (DWORD)(sizeof(buffer) / sizeof(buffer[0])), buffer, nullptr);
      if (n == 0 || n >= sizeof(buffer) / sizeof(buffer[0]))
         return std::string();
      return Narrow(std::wstring(buffer, n));
   }

   // One argument quoted for CreateProcess / the C runtime's argv parser.
   std::wstring Quote(const std::wstring& arg)
   {
      if (!arg.empty() && arg.find_first_of(L" \t\"&|<>^") == std::wstring::npos)
         return arg;
      std::wstring out = L"\"";
      size_t backslashes = 0;
      for (wchar_t c : arg)
      {
         if (c == L'\\')
         {
            backslashes++;
            continue;
         }
         if (c == L'"')
            out.append(backslashes * 2 + 1, L'\\');
         else
            out.append(backslashes, L'\\');
         backslashes = 0;
         out.push_back(c);
      }
      out.append(backslashes * 2, L'\\');
      out.push_back(L'"');
      return out;
   }

   std::string Shorten(std::string s, size_t max)
   {
      for (char& c : s)
         if (c == '\n' || c == '\r')
            c = ' ';
      if (s.size() > max)
      {
         size_t cut = max;
         while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80)
            cut--; // keep UTF-8 whole
         s = s.substr(0, cut) + "...";
      }
      return s;
   }

   std::string ToolName(const std::string& name)
   {
      const std::string prefix = "mcp__infinite-turbo__";
      return name.rfind(prefix, 0) == 0 ? name.substr(prefix.size()) : name;
   }

   bool HandleLineUnsafe(const std::string& line);

   // One line of --output-format stream-json. Returns false for non-JSON.
   // Never throws: this runs on the detached reader thread.
   bool HandleLine(const std::string& line)
   {
      try
      {
         return HandleLineUnsafe(line);
      }
      catch (const std::exception&)
      {
         return true; // JSON of an unexpected shape: skip it, it is not stray text
      }
   }

   bool HandleLineUnsafe(const std::string& line)
   {
      json j = json::parse(line, nullptr, false);
      if (j.is_discarded() || !j.is_object())
         return false;
      const std::string type = j.value("type", std::string());
      if (type == "system" && j.value("subtype", std::string()) == "init")
      {
         if (j.contains("session_id") && j["session_id"].is_string())
            Push(ClaudeChat::Event::kSession, j["session_id"].get<std::string>());
         if (j.contains("mcp_servers") && j["mcp_servers"].is_array())
            for (const json& s : j["mcp_servers"])
               if (s.is_object() && s.value("name", std::string()) == "infinite-turbo" &&
                   s.value("status", std::string()) != "connected" && s.value("status", std::string()) != "pending")
                  Push(ClaudeChat::Event::kError, "the Infinite-Turbo MCP server did not connect (status: " +
                                                     s.value("status", std::string("?")) + ")");
         if (j.contains("model") && j["model"].is_string())
            Push(ClaudeChat::Event::kInfo, "model: " + j["model"].get<std::string>());
         return true;
      }
      if (type == "assistant" && j.contains("message") && j["message"].contains("content") &&
          j["message"]["content"].is_array())
      {
         for (const json& block : j["message"]["content"])
         {
            const std::string bt = block.value("type", std::string());
            if (bt == "text" && block.contains("text") && block["text"].is_string())
            {
               gSawText.store(true);
               const std::string text = block["text"].get<std::string>();
               if (!text.empty())
                  Push(ClaudeChat::Event::kText, text);
            }
            else if (bt == "tool_use")
            {
               std::string args = block.contains("input") ? block["input"].dump() : std::string();
               if (args == "{}")
                  args.clear();
               Push(ClaudeChat::Event::kTool, ToolName(block.value("name", std::string("tool"))) +
                                                 (args.empty() ? std::string() : " " + Shorten(args, 160)));
            }
         }
         return true;
      }
      if (type == "user" && j.contains("message") && j["message"].contains("content") &&
          j["message"]["content"].is_array())
      {
         for (const json& block : j["message"]["content"])
            if (block.value("type", std::string()) == "tool_result" && block.value("is_error", false))
            {
               std::string text;
               if (block.contains("content"))
               {
                  if (block["content"].is_string())
                     text = block["content"].get<std::string>();
                  else if (block["content"].is_array())
                     for (const json& c : block["content"])
                        if (c.is_object() && c.contains("text") && c["text"].is_string())
                           text += c["text"].get<std::string>();
               }
               Push(ClaudeChat::Event::kToolError, Shorten(text.empty() ? std::string("tool error") : text, 240));
            }
         return true;
      }
      if (type == "result")
      {
         gSawResult.store(true);
         if (j.contains("session_id") && j["session_id"].is_string())
            Push(ClaudeChat::Event::kSession, j["session_id"].get<std::string>());
         const std::string subtype = j.value("subtype", std::string());
         if (j.value("is_error", false) || (subtype != "success" && !subtype.empty()))
         {
            std::string why = j.contains("result") && j["result"].is_string() ? j["result"].get<std::string>() : subtype;
            Push(ClaudeChat::Event::kError, why.empty() ? std::string("the turn failed") : why);
         }
         return true;
      }
      return true; // other event types (stream_event, retries...) are not shown
   }


   void StderrThread(HANDLE readPipe, std::string* out)
   {
      char chunk[4096];
      DWORD got = 0;
      while (ReadFile(readPipe, chunk, sizeof(chunk), &got, nullptr) && got > 0)
         if (out->size() < 6000)
            out->append(chunk, got);
      CloseHandle(readPipe);
   }

   void ReaderThread(HANDLE readPipe, HANDLE errPipe, HANDLE process, HANDLE writePipe, std::string prompt)
   {
      // The prompt is written here, not on the main thread: a big one would
      // block until the CLI reads it, and the CLI may first wait for the MCP
      // server, which the main loop serves.
      std::thread writer([writePipe, prompt]() {
         DWORD written = 0;
         WriteFile(writePipe, prompt.data(), (DWORD)prompt.size(), &written, nullptr);
         CloseHandle(writePipe);
      });
      std::string errText;
      std::thread errReader(StderrThread, errPipe, &errText);

      std::string buffer, stray;
      size_t scanned = 0;
      char chunk[8192];
      DWORD got = 0;
      while (ReadFile(readPipe, chunk, sizeof(chunk), &got, nullptr) && got > 0)
      {
         buffer.append(chunk, got);
         size_t nl;
         while ((nl = buffer.find('\n', scanned)) != std::string::npos)
         {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            scanned = 0;
            if (!line.empty() && line.back() == '\r')
               line.pop_back();
            LogAppend((line.size() > 4000 ? line.substr(0, 4000) + " [...]" : line) + "\n");
            if (!line.empty() && !HandleLine(line) && stray.size() < 4000)
               stray += line + "\n";
         }
         scanned = buffer.size();
      }
      if (!buffer.empty() && !HandleLine(buffer))
         stray += buffer;
      CloseHandle(readPipe);
      WaitForSingleObject(process, INFINITE);
      DWORD code = 0;
      GetExitCodeProcess(process, &code);
      writer.join();
      errReader.join();
      LogAppend("\n--- stderr ---\n" + errText + "\n--- exit code " + std::to_string(code) + " ---\n");
      const bool cancelled = gCancelled.load();
      if (!cancelled && !gSawText.load() && code == 0 && errText.empty() && stray.empty())
         Push(ClaudeChat::Event::kError, "Claude ended without an answer. The full output is in " + gLogPath);
      // stderr (and stray stdout) only matters when the turn went wrong.
      if (!cancelled && (code != 0 || !gSawResult.load()))
      {
         std::string why = stray + errText;
         if (!why.empty())
            Push(ClaudeChat::Event::kError, Shorten(why, 1200));
      }
      std::string done;
      if (!cancelled && code != 0)
         done = "claude exited with code " + std::to_string(code);
      gBusy.store(false);
      Push(ClaudeChat::Event::kDone, done);
   }

   // cmd.exe re-parses a .cmd shim's command line: no quotes, % or newlines.
   std::wstring CmdSafe(std::wstring s)
   {
      for (wchar_t& c : s)
         if (c == L'"' || c == L'%' || c == L'\n' || c == L'\r' || c == L'^' || c == L'!')
            c = L' ';
      return s;
   }
}

namespace ClaudeChat
{
   std::string FindClaude()
   {
      const std::string forced = Env("INFINITE_CLAUDE_PATH");
      if (IsFile(forced))
         return forced;
      const std::string profile = Env("USERPROFILE");
      if (!profile.empty())
      {
         const std::string native = (std::filesystem::u8path(profile) / ".local" / "bin" / "claude.exe").u8string();
         if (IsFile(native))
            return native;
      }
      std::string found = SearchOnPath(L"claude", L".exe");
      if (found.empty())
         found = SearchOnPath(L"claude", L".cmd");
      if (!found.empty())
         return found;
      const std::string appData = Env("APPDATA");
      if (!appData.empty())
      {
         const std::string npm = (std::filesystem::u8path(appData) / "npm" / "claude.cmd").u8string();
         if (IsFile(npm))
            return npm;
      }
      return std::string();
   }

   bool Busy() { return gBusy.load(); }

   void Cancel()
   {
      if (!gBusy.load())
         return;
      gCancelled.store(true);
      if (gJob != nullptr)
         TerminateJobObject(gJob, 1);
      else if (gProcessForCancel != nullptr)
         TerminateProcess(gProcessForCancel, 1);
   }

   void Discard()
   {
      std::lock_guard<std::mutex> lock(gMutex);
      gQueue.clear();
   }

   void Drain(std::vector<Event>& out)
   {
      std::lock_guard<std::mutex> lock(gMutex);
      for (Event& e : gQueue)
         out.push_back(std::move(e));
      gQueue.clear();
   }

   bool Send(const std::string& prompt, const Options& options, std::string& error)
   {
      if (gBusy.load())
      {
         error = "Claude is still answering";
         return false;
      }
      if (gJob != nullptr) // the previous turn has finished (gBusy is false)
      {
         CloseHandle(gJob);
         gJob = nullptr;
      }
      if (gProcessForCancel != nullptr)
      {
         CloseHandle(gProcessForCancel);
         gProcessForCancel = nullptr;
      }
      gCancelled.store(false);
      gSawResult.store(false);
      gSawText.store(false);

      const std::string claude = options.claudePath.empty() ? FindClaude() : options.claudePath;
      if (claude.empty())
      {
         error = "Claude Code is not installed";
         return false;
      }

      // The MCP config: this exe's --mcp, nothing else.
      std::error_code ec;
      std::filesystem::create_directories(std::filesystem::u8path(options.workDir), ec);
      const std::filesystem::path configPath = std::filesystem::u8path(options.workDir) / "mcp-config.json";
      {
         json config = { { "mcpServers", { { "infinite-turbo", { { "command", options.exePath },
                                                                 { "args", json::array({ "--mcp" }) } } } } } };
         std::ofstream file(configPath, std::ios::binary | std::ios::trunc);
         file << config.dump(2);
         if (!file.good())
         {
            error = "could not write " + configPath.u8string();
            return false;
         }
      }

      std::vector<std::wstring> args = {
         L"-p", L"--output-format", L"stream-json", L"--verbose",
         L"--mcp-config", configPath.wstring(), L"--strict-mcp-config",
         L"--allowedTools", L"mcp__infinite-turbo", L"mcp__infinite-turbo__*",
      };
      if (!options.systemPrompt.empty())
      {
         args.push_back(L"--append-system-prompt");
         args.push_back(Wide(options.systemPrompt));
      }
      if (!options.sessionId.empty())
      {
         args.push_back(L"--resume");
         args.push_back(Wide(options.sessionId));
      }
      if (!options.model.empty())
      {
         args.push_back(L"--model");
         args.push_back(Wide(options.model));
      }

      std::wstring command;
      const std::wstring claudeW = Wide(claude);
      const bool isCmd = claude.size() > 4 && _stricmp(claude.c_str() + claude.size() - 4, ".cmd") == 0;
      std::wstring inner = Quote(claudeW);
      for (const std::wstring& a : args)
         inner += L" " + Quote(isCmd ? CmdSafe(a) : a);
      if (isCmd)
         command = L"cmd.exe /d /s /c \"" + inner + L"\"";
      else
         command = inner;

      gLogPath = (std::filesystem::u8path(options.workDir) / "last-turn.log").u8string();
      {
         std::ofstream log(std::filesystem::u8path(gLogPath), std::ios::binary | std::ios::trunc);
         log << "command: " << Narrow(command) << "\n--- stdout ---\n";
      }

      // Pipes are created non-inheritable; only the child's three ends are
      // made inheritable, and only for the CreateProcess call, which also gets
      // an explicit handle list, so neither this child nor one another thread
      // starts meanwhile (the VST3 scanner) picks up the other's handles.
      HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr, inRead = nullptr,
             inWrite = nullptr;
      if (!CreatePipe(&outRead, &outWrite, nullptr, 1 << 16) || !CreatePipe(&errRead, &errWrite, nullptr, 0) ||
          !CreatePipe(&inRead, &inWrite, nullptr, 1 << 16))
      {
         for (HANDLE h : { outRead, outWrite, errRead, errWrite, inRead, inWrite })
            if (h != nullptr)
               CloseHandle(h);
         error = "could not create pipes";
         return false;
      }
      HANDLE inherit[3] = { inRead, outWrite, errWrite };
      for (HANDLE h : inherit)
         SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

      SIZE_T attrSize = 0;
      InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
      std::vector<unsigned char> attrBuffer(attrSize);
      auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
      bool haveList = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                      UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit),
                                                nullptr, nullptr);

      STARTUPINFOEXW si = {};
      si.StartupInfo.cb = sizeof(si);
      si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
      si.StartupInfo.hStdInput = inRead;
      si.StartupInfo.hStdOutput = outWrite;
      si.StartupInfo.hStdError = errWrite;
      si.lpAttributeList = haveList ? attrs : nullptr;
      PROCESS_INFORMATION pi = {};
      std::wstring cmdLine = command; // CreateProcessW may modify it
      const std::wstring cwd = std::filesystem::u8path(options.workDir).wstring();
      const BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                                        (haveList ? EXTENDED_STARTUPINFO_PRESENT : 0),
                                     nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
      const DWORD createError = GetLastError();
      if (haveList)
         DeleteProcThreadAttributeList(attrs);
      CloseHandle(outWrite);
      CloseHandle(errWrite);
      CloseHandle(inRead);
      if (!ok)
      {
         CloseHandle(outRead);
         CloseHandle(errRead);
         CloseHandle(inWrite);
         error = "could not start " + claude + " (error " + std::to_string(createError) + ")";
         return false;
      }

      // A job, so Stop (and closing the app) also ends node.exe and the MCP
      // bridge the CLI started, not just the first process.
      gJob = CreateJobObjectW(nullptr, nullptr);
      if (gJob != nullptr)
      {
         JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
         limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
         SetInformationJobObject(gJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
         if (!AssignProcessToJobObject(gJob, pi.hProcess))
         {
            CloseHandle(gJob);
            gJob = nullptr;
         }
      }
      if (gJob == nullptr)
         DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &gProcessForCancel, 0, FALSE,
                         DUPLICATE_SAME_ACCESS);
      ResumeThread(pi.hThread);
      CloseHandle(pi.hThread);

      gBusy.store(true);
      // Detached: it ends by itself when the CLI exits (or is killed), and
      // must not hold up the app's exit. It writes the prompt to stdin too.
      std::thread(ReaderThread, outRead, errRead, pi.hProcess, inWrite, prompt).detach();
      return true;
   }
}
