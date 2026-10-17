// Infinite-Turbo 0.47: in-app Claude chat backend (see ClaudeChat.h).

#include "ClaudeChat.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "json.hpp"
#include "stb_image_write.h"

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
   // Turbo 0.59: which CLI runs the turn (ClaudeChat::Backend), read by the
   // reader thread to pick the event parser.
   std::atomic<int> gBackend { 0 };
   const char* BackendName() { return gBackend.load() == 1 ? "Codex" : "Claude"; }

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

   // Turbo 0.59: one line of `codex exec --json` (JSON Lines events).
   bool HandleCodexLine(const json& j)
   {
      const std::string type = j.value("type", std::string());
      if (type == "thread.started")
      {
         if (j.contains("thread_id") && j["thread_id"].is_string())
            Push(ClaudeChat::Event::kSession, j["thread_id"].get<std::string>());
         return true;
      }
      if ((type == "item.started" || type == "item.completed") && j.contains("item") && j["item"].is_object())
      {
         const json& item = j["item"];
         const std::string it = item.value("type", std::string());
         if (it == "agent_message" && type == "item.completed" && item.contains("text") && item["text"].is_string())
         {
            gSawText.store(true);
            const std::string text = item["text"].get<std::string>();
            if (!text.empty())
               Push(ClaudeChat::Event::kText, text);
         }
         else if (it == "mcp_tool_call" && type == "item.started")
         {
            std::string args = item.contains("arguments") && !item["arguments"].is_null() ? item["arguments"].dump() : std::string();
            if (args == "{}")
               args.clear();
            Push(ClaudeChat::Event::kTool, item.value("tool", std::string("tool")) +
                                              (args.empty() ? std::string() : " " + Shorten(args, 160)));
         }
         else if (it == "mcp_tool_call" && type == "item.completed" && item.value("status", std::string()) == "failed")
         {
            std::string why = "tool error";
            if (item.contains("error") && item["error"].is_object() && item["error"].contains("message") &&
                item["error"]["message"].is_string())
               why = item["error"]["message"].get<std::string>();
            Push(ClaudeChat::Event::kToolError, Shorten(why, 240));
         }
         else if (it == "command_execution" && type == "item.started" && item.contains("command") && item["command"].is_string())
            Push(ClaudeChat::Event::kTool, "shell " + Shorten(item["command"].get<std::string>(), 160));
         return true;
      }
      if (type == "turn.completed")
      {
         gSawResult.store(true);
         return true;
      }
      if (type == "turn.failed")
      {
         gSawResult.store(true);
         std::string why = "the turn failed";
         if (j.contains("error") && j["error"].is_object() && j["error"].contains("message") && j["error"]["message"].is_string())
            why = j["error"]["message"].get<std::string>();
         Push(ClaudeChat::Event::kError, why);
         return true;
      }
      if (type == "error")
      {
         const std::string msg = j.value("message", std::string());
         // "Reconnecting... 1/5" and the like arrive here too and are not fatal.
         if (!msg.empty() && msg.find("Reconnecting") == std::string::npos)
            Push(ClaudeChat::Event::kError, Shorten(msg, 600));
         return true;
      }
      return true;
   }

   bool HandleLineUnsafe(const std::string& line)
   {
      json j = json::parse(line, nullptr, false);
      if (j.is_discarded() || !j.is_object())
         return false;
      if (gBackend.load() == 1)
         return HandleCodexLine(j);
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
         Push(ClaudeChat::Event::kError, std::string(BackendName()) + " ended without an answer. The full output is in " + gLogPath);
      // stderr (and stray stdout) only matters when the turn went wrong.
      if (!cancelled && (code != 0 || !gSawResult.load()))
      {
         std::string why = stray + errText;
         if (!why.empty())
            Push(ClaudeChat::Event::kError, Shorten(why, 1200));
      }
      std::string done;
      if (!cancelled && code != 0)
         done = std::string(gBackend.load() == 1 ? "codex" : "claude") + " exited with code " + std::to_string(code);
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

   std::string FindCodex()
   {
      const std::string forced = Env("INFINITE_CODEX_PATH");
      if (IsFile(forced))
         return forced;
      std::string found = SearchOnPath(L"codex", L".exe");
      if (found.empty())
         found = SearchOnPath(L"codex", L".cmd");
      if (!found.empty())
         return found;
      const std::string appData = Env("APPDATA");
      if (!appData.empty())
      {
         const std::string npm = (std::filesystem::u8path(appData) / "npm" / "codex.cmd").u8string();
         if (IsFile(npm))
            return npm;
      }
      const std::string local = Env("LOCALAPPDATA");
      if (!local.empty())
      {
         const std::string prog = (std::filesystem::u8path(local) / "Programs" / "codex" / "codex.exe").u8string();
         if (IsFile(prog))
            return prog;
      }
      return std::string();
   }

   std::string FindCli(Backend backend) { return backend == kCodex ? FindCodex() : FindClaude(); }

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

      gBackend.store(options.backend == kCodex ? 1 : 0);
      const std::string claude = options.claudePath.empty() ? FindCli(options.backend) : options.claudePath;
      if (claude.empty())
      {
         error = options.backend == kCodex ? "the Codex CLI is not installed" : "Claude Code is not installed";
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

      std::vector<std::wstring> args;
      if (options.backend == kCodex)
      {
         // Turbo 0.59: OpenAI's Codex CLI, logged in with the user's ChatGPT
         // account. Read-only sandbox, no approval prompts (there is no one
         // to answer them), this exe's --mcp as an MCP server, the prompt on
         // stdin ("-"). TOML literal strings ('...') survive a .cmd shim.
         // With approval_policy never, Codex refuses every MCP tool call that
         // would need approval ("MCP tool call requires approval, but approval
         // policy is never"): our own server's tools are pre-approved
         // (default_tools_approval_mode, Codex 0.16x: auto/prompt/writes/approve).
         args = { L"exec", L"--json", L"--skip-git-repo-check", L"--sandbox", L"read-only",
                  L"-c", L"approval_policy='never'",
                  L"-c", L"mcp_servers.infinite-turbo.command='" + Wide(options.exePath) + L"'",
                  L"-c", L"mcp_servers.infinite-turbo.args=['--mcp']",
                  L"-c", L"mcp_servers.infinite-turbo.default_tools_approval_mode='approve'",
                  L"-c", L"mcp_servers.infinite-turbo.startup_timeout_sec=30",
                  L"-c", L"mcp_servers.infinite-turbo.tool_timeout_sec=120" };
         if (!options.model.empty())
         {
            args.push_back(L"-m");
            args.push_back(Wide(options.model));
         }
         for (const std::string& img : options.images)
         {
            args.push_back(L"-i");
            args.push_back(Wide(img));
         }
         if (!options.sessionId.empty())
         {
            args.push_back(L"resume");
            args.push_back(Wide(options.sessionId));
         }
         args.push_back(L"-");
      }
      else
      args = {
         L"-p", L"--output-format", L"stream-json", L"--verbose",
         L"--mcp-config", configPath.wstring(), L"--strict-mcp-config",
         L"--allowedTools", L"mcp__infinite-turbo", L"mcp__infinite-turbo__*",
         // Turbo 0.59: attachments (reference images, patches, text) are
         // copied into the work dir and read with the Read tool.
         L"Read",
      };
      if (options.backend != kCodex && !options.systemPrompt.empty())
      {
         args.push_back(L"--append-system-prompt");
         args.push_back(Wide(options.systemPrompt));
      }
      if (options.backend != kCodex && !options.sessionId.empty())
      {
         args.push_back(L"--resume");
         args.push_back(Wide(options.sessionId));
      }
      if (options.backend != kCodex && !options.model.empty())
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

   bool LooksLoggedIn(Backend backend)
   {
      const std::string profile = Env("USERPROFILE");
      if (backend == kCodex)
      {
         if (!Env("OPENAI_API_KEY").empty() || !Env("CODEX_API_KEY").empty())
            return true;
         const std::string codexHome = Env("CODEX_HOME");
         const std::filesystem::path dir = !codexHome.empty() ? std::filesystem::u8path(codexHome)
                                         : profile.empty() ? std::filesystem::path() : std::filesystem::u8path(profile) / ".codex";
         return dir.empty() || IsFile((dir / "auth.json").u8string());
      }
      if (!Env("ANTHROPIC_API_KEY").empty() || !Env("CLAUDE_CODE_OAUTH_TOKEN").empty())
         return true;
      if (profile.empty())
         return true; // cannot tell: do not nag
      const std::filesystem::path home = std::filesystem::u8path(profile) / ".claude";
      return IsFile((home / ".credentials.json").u8string()) || IsFile((std::filesystem::u8path(profile) / ".claude.json").u8string());
   }

   bool OpenTerminal(const std::string& commandLine)
   {
      std::wstring cmd = L"cmd.exe /k " + Wide(commandLine);
      STARTUPINFOW si = {};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi = {};
      std::vector<wchar_t> buf(cmd.begin(), cmd.end());
      buf.push_back(L'\0');
      if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi))
         return false;
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return true;
   }

   bool SaveClipboardImage(const std::string& pngPath, std::string& error)
   {
      if (!IsClipboardFormatAvailable(CF_DIB))
      {
         error = "no image on the clipboard";
         return false;
      }
      if (!OpenClipboard(nullptr))
      {
         error = "the clipboard is busy";
         return false;
      }
      bool ok = false;
      HANDLE h = GetClipboardData(CF_DIB);
      const unsigned char* base = h ? (const unsigned char*)GlobalLock(h) : nullptr;
      if (base != nullptr)
      {
         const SIZE_T size = GlobalSize(h);
         const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)base;
         const int w = bih->biWidth;
         const bool topDown = bih->biHeight < 0;
         const int hgt = topDown ? -bih->biHeight : bih->biHeight;
         const int bpp = bih->biBitCount;
         size_t offset = bih->biSize;
         if (bih->biCompression == BI_BITFIELDS && bih->biSize == sizeof(BITMAPINFOHEADER))
            offset += 12; // three DWORD masks follow the header
         offset += (size_t)bih->biClrUsed * 4;
         const size_t stride = (((size_t)w * (size_t)bpp + 31) / 32) * 4;
         if (w > 0 && hgt > 0 && (bpp == 24 || bpp == 32) && offset + stride * (size_t)hgt <= size)
         {
            std::vector<unsigned char> rgba((size_t)w * (size_t)hgt * 4);
            bool anyAlpha = false;
            for (int y = 0; y < hgt; y++)
            {
               const unsigned char* row = base + offset + stride * (size_t)(topDown ? y : hgt - 1 - y);
               for (int x = 0; x < w; x++)
               {
                  const unsigned char* px = row + (size_t)x * (size_t)(bpp / 8);
                  unsigned char* o = &rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
                  o[0] = px[2]; o[1] = px[1]; o[2] = px[0];
                  o[3] = bpp == 32 ? px[3] : 255;
                  anyAlpha = anyAlpha || (bpp == 32 && px[3] != 0);
               }
            }
            if (bpp == 32 && !anyAlpha) // many apps leave the alpha byte at 0
               for (size_t i = 3; i < rgba.size(); i += 4)
                  rgba[i] = 255;
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::u8path(pngPath).parent_path(), ec);
            ok = stbi_write_png(pngPath.c_str(), w, hgt, 4, rgba.data(), w * 4) != 0;
            if (!ok)
               error = "could not write " + pngPath;
         }
         else
            error = "unsupported clipboard image format";
         GlobalUnlock(h);
      }
      else
         error = "could not read the clipboard image";
      CloseClipboard();
      return ok;
   }
}
