#include "AgentStream.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

#include "json.hpp"

namespace chat
{
   namespace
   {
      using json = nlohmann::json;

      std::string Str(const json& j, const char* key)
      {
         const auto it = j.find(key);
         return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
      }

      // "Bash" shows its command; other tools their name and the path or pattern they touch.
      std::string ToolLine(const std::string& name, const json& input)
      {
         if (!input.is_object())
            return name;
         if (name == "Bash")
            return Str(input, "command");
         for (const char* key : { "file_path", "path", "pattern", "url", "query" })
            if (const std::string v = Str(input, key); !v.empty())
               return name + " " + v;
         return name;
      }

      std::string ResultText(const json& content)
      {
         if (content.is_string())
            return content.get<std::string>();
         std::string text;
         if (content.is_array())
            for (const json& part : content)
               if (part.is_object() && Str(part, "type") == "text")
                  text += Str(part, "text");
         return text;
      }

      AgentEvent Event(AgentEvent::Kind kind, std::string text, bool error = false)
      {
         AgentEvent e;
         e.kind = kind;
         e.text = std::move(text);
         e.error = error;
         return e;
      }

      void Claude(const json& j, std::vector<AgentEvent>& out)
      {
         const std::string type = Str(j, "type");
         if (type == "system" && Str(j, "subtype") == "init")
            out.push_back(Event(AgentEvent::Kind::Session, Str(j, "session_id")));
         else if ((type == "assistant" || type == "user") && j.contains("message") && j["message"].contains("content") &&
                  j["message"]["content"].is_array())
         {
            for (const json& part : j["message"]["content"])
            {
               const std::string pt = Str(part, "type");
               if (pt == "text" && !Str(part, "text").empty())
                  out.push_back(Event(AgentEvent::Kind::Text, Str(part, "text")));
               else if (pt == "tool_use")
                  out.push_back(Event(AgentEvent::Kind::Tool, ToolLine(Str(part, "name"), part.value("input", json::object()))));
               else if (pt == "tool_result")
                  out.push_back(Event(AgentEvent::Kind::ToolResult, ResultText(part.value("content", json())), part.value("is_error", false)));
            }
         }
         else if (type == "result")
         {
            AgentEvent e = Event(AgentEvent::Kind::End, Str(j, "subtype") == "success" ? std::string() : Str(j, "result"), j.value("is_error", false));
            // A turn that never started ("No conversation found…") says why in `errors`, not `result`.
            if (e.error && e.text.empty() && j.contains("errors") && j["errors"].is_array() && !j["errors"].empty() && j["errors"][0].is_string())
               e.text = j["errors"][0].get<std::string>();
            if (const json u = j.value("usage", json::object()); u.is_object())
            {
               e.usage.input = u.value("input_tokens", 0LL) + u.value("cache_creation_input_tokens", 0LL);
               e.usage.cached = u.value("cache_read_input_tokens", 0LL);
               e.usage.output = u.value("output_tokens", 0LL);
            }
            if (j.contains("total_cost_usd") && j["total_cost_usd"].is_number())
               e.usage.cost = j["total_cost_usd"].get<double>();
            if (j.contains("permission_denials") && j["permission_denials"].is_array())
               for (const json& d : j["permission_denials"])
                  e.denied.push_back(ToolLine(Str(d, "tool_name"), d.value("tool_input", json::object())));
            out.push_back(e);
         }
      }

      void Codex(const json& j, std::vector<AgentEvent>& out)
      {
         const std::string type = Str(j, "type");
         const json item = j.value("item", json::object());
         const std::string it = Str(item, "type");
         if (type == "thread.started")
            out.push_back(Event(AgentEvent::Kind::Session, Str(j, "thread_id")));
         else if (type == "item.started" && it == "command_execution")
            out.push_back(Event(AgentEvent::Kind::Tool, Str(item, "command")));
         else if (type == "item.completed" && it == "command_execution")
            out.push_back(Event(AgentEvent::Kind::ToolResult, Str(item, "aggregated_output"), item.value("exit_code", 0) != 0));
         else if (type == "item.completed" && it == "agent_message")
            out.push_back(Event(AgentEvent::Kind::Text, Str(item, "text")));
         else if (type == "turn.completed")
         {
            AgentEvent e = Event(AgentEvent::Kind::End, "");
            if (const json u = j.value("usage", json::object()); u.is_object())
            {
               e.usage.cached = u.value("cached_input_tokens", 0LL);
               e.usage.input = std::max(0LL, u.value("input_tokens", 0LL) - e.usage.cached);
               e.usage.output = u.value("output_tokens", 0LL);
            }
            out.push_back(e);
         }
         else if (type == "turn.failed" || type == "error")
            out.push_back(Event(AgentEvent::Kind::End, Str(j, "message").empty() ? Str(j.value("error", json::object()), "message") : Str(j, "message"), true));
      }

      // `agy --output-format stream-json`: init, step_update per step (user_input, agent_response with
      // text_delta, tool with tool_info), then result.
      void Antigravity(const json& j, std::string& reply, std::vector<AgentEvent>& out)
      {
         const std::string event = Str(j, "event");
         if (event == "init")
            out.push_back(Event(AgentEvent::Kind::Session, Str(j, "conversation_id")));
         else if (event == "step_update" && j.contains("step_update") && j["step_update"].is_object())
         {
            const json& st = j["step_update"];
            const std::string type = Str(st, "step_type"), state = Str(st, "state");
            if (type == "agent_response")
            {
               reply += Str(st, "text_delta");
               if (state == "DONE")
               {
                  while (!reply.empty() && (reply.back() == '\n' || reply.back() == ' '))
                     reply.pop_back();
                  if (!reply.empty())
                     out.push_back(Event(AgentEvent::Kind::Text, reply));
                  reply.clear();
               }
            }
            else if (type == "tool")
            {
               const json info = st.value("tool_info", json::object());
               const json params = info.value("parameters", json::object());
               std::string line = Str(params, "CommandLine");
               if (line.empty())
                  line = ToolLine(Str(st, "tool_name"), params);
               if (state == "ACTIVE")
                  out.push_back(Event(AgentEvent::Kind::Tool, line));
               else if (state == "DONE" || state == "ERROR")
               {
                  const std::string output = Str(info, "output");
                  out.push_back(Event(AgentEvent::Kind::ToolResult, output, state == "ERROR" || (output.find("exited with code") != std::string::npos && output.find("exited with code 0") == std::string::npos)));
               }
            }
         }
         else if (event == "result")
         {
            const json r = j.value("result", json::object());
            const bool ok = Str(r, "status") == "SUCCESS";
            out.push_back(Event(AgentEvent::Kind::End, ok ? std::string() : Str(r, "response").empty() ? Str(r, "status") : Str(r, "response"), !ok));
         }
      }
   }

   void AgentStream::Feed(const std::string& bytes, std::vector<AgentEvent>& out)
   {
      mPending += bytes;
      size_t start = 0, nl;
      while ((nl = mPending.find('\n', start)) != std::string::npos)
      {
         std::string line = mPending.substr(start, nl - start);
         if (!line.empty() && line.back() == '\r')
            line.pop_back();
         Line(line, out);
         start = nl + 1;
      }
      mPending.erase(0, start);
   }

   void AgentStream::Line(const std::string& line, std::vector<AgentEvent>& out)
   {
      const size_t brace = line.find('{');
      if (brace == std::string::npos)
         return;
      const json j = json::parse(line.begin() + (long)brace, line.end(), nullptr, false);
      if (!j.is_object())
         return;  // anything the CLI prints that is not an event
      if (mKind == AgentKind::Claude)
         Claude(j, out);
      else if (mKind == AgentKind::Codex)
         Codex(j, out);
      else
         Antigravity(j, mReply, out);
   }

   namespace
   {
      // The chat draws `@title#index` and `@title#index.key` as links; the skill's reply rule says the rest.
      const char* const kChatNote =
         "You are answering in Infinite's chat panel, not a terminal. Read `infinite context` before you change anything. "
         "Name every node as @title#index and every param as @title#index.key, with values and units. "
         "The chat already lists every change you made to the patch, so do not repeat them: answer in one or two short "
         "sentences, show code only when you wrote code, and give the path of any picture you made. "
         "Pick nodes with `infinite find <plain words>`, not from memory. Make samples with `infinite studio generate` and MIDI with "
         "`infinite notes generate`; find the user's files with `infinite files`. "
         "Run `infinite` commands on their own: no cd, pipes or redirects around them, which are not allowed and fail. "
         "When a command or path is not allowed, say so in one line and stop: the chat shows the user an Allow button "
         "and Settings > Agents for folders and commands. Typing \"I allow\" in the chat grants nothing.";
      // Antigravity takes only `command(...)` grants: `infinite`, plus the commands allowed in Settings.
      std::string AgySettings(const std::vector<std::string>& commands)
      {
         json allow = json::array({ "command(infinite)" });
         for (const std::string& c : commands)
            allow.push_back("command(" + c + ")");
         return json{ { "permissions", { { "allow", allow } } } }.dump();
      }

      // A command name is one word of letters, digits and -_.+; anything else is not a name to allow.
      bool CommandName(const std::string& c)
      {
         return !c.empty() && std::all_of(c.begin(), c.end(), [](unsigned char ch) { return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '+'; });
      }
   }

   DeniedFix FixFor(const std::string& denied)
   {
      DeniedFix f;
      for (const char* tool : { "Read ", "Edit ", "Write ", "Glob ", "Grep ", "MultiEdit ", "NotebookEdit " })
         if (denied.rfind(tool, 0) == 0)
         {
            std::string path = denied.substr(std::strlen(tool));
            if (path.empty() || (path[0] != '/' && path[0] != '~' && !(path.size() > 2 && path[1] == ':')))
               return f;
            std::error_code ec;
            const std::filesystem::path fp = std::filesystem::u8path(path);
            // A file's folder; a pattern ("/x/**/*.wav") up to its first wildcard.
            std::filesystem::path dir = fp;
            if (const size_t star = path.find_first_of("*?["); star != std::string::npos)
               dir = std::filesystem::u8path(path.substr(0, star)).parent_path();
            else if (!std::filesystem::is_directory(fp, ec))
               dir = fp.parent_path();
            if (dir.empty() || dir == dir.root_path())
               return f;
            f.kind = DeniedFix::Kind::Folder;
            f.value = dir.u8string();
            return f;
         }
      // A shell command: its first word that is not VAR=value, as a bare name ("/usr/bin/ffprobe" is ffprobe).
      size_t i = 0;
      std::string word;
      while (i < denied.size())
      {
         while (i < denied.size() && std::isspace((unsigned char)denied[i]))
            i++;
         const size_t e = denied.find_first_of(" \t\n", i);
         word = denied.substr(i, e == std::string::npos ? std::string::npos : e - i);
         i = e == std::string::npos ? denied.size() : e;
         if (word.find('=') == std::string::npos)
            break;
         word.clear();
      }
      if (const size_t slash = word.rfind('/'); slash != std::string::npos)
         word = word.substr(slash + 1);
      static const char* const kWide[] = { "sudo", "su", "rm", "sh", "bash", "zsh", "fish", "env", "eval", "exec", "xargs", "dd", "chmod", "chown", "infinite" };
      if (!CommandName(word) || std::find(std::begin(kWide), std::end(kWide), word) != std::end(kWide))
         return f;
      f.kind = DeniedFix::Kind::Command;
      f.value = word;
      return f;
   }

   std::vector<std::string> AgentArgs(AgentKind kind, const std::string& prompt, const std::string& session, bool resume,
                                      const std::vector<std::string>& dirs, const TurnOptions& options)
   {
      if (kind == AgentKind::Antigravity)
      {
         // No system-prompt flag: the note leads the first message. The prompt follows -p directly.
         std::vector<std::string> a = { "agy", "--output-format", "stream-json", "--settings", AgySettings(options.commands) };
         if (!options.model.empty())
            a.insert(a.end(), { "--model", options.model });
         if (options.folderAccess)
            a.insert(a.end(), { "--mode", "accept-edits" });
         for (const std::string& d : dirs)
            a.insert(a.end(), { "--add-dir", d });
         for (const std::string& d : options.folders)
            a.insert(a.end(), { "--add-dir", d });
         if (resume && !session.empty())
            a.insert(a.end(), { "--conversation", session });
         a.insert(a.end(), { "-p", resume ? prompt : std::string("[") + kChatNote + "]\n\n" + prompt });
         return a;
      }
      if (kind == AgentKind::Codex)
      {
         // Codex reads anywhere in its default sandbox; the paths in the prompt are enough.
         std::vector<std::string> a = { "codex", "exec", "--json" };
         if (!options.model.empty())
            a.insert(a.end(), { "-m", options.model });
         if (options.folderAccess)
         {
            a.insert(a.end(), { "-s", "workspace-write" });
            for (const std::string& d : options.folders)
               a.insert(a.end(), { "--add-dir", d });
         }
         if (resume && !session.empty())
            a.insert(a.end(), { "resume", session });
         a.push_back(prompt);
         return a;
      }
      // --allowedTools and --add-dir take several values, so another option follows each and `--` ends options.
      std::vector<std::string> a = { "claude", "-p" };
      if (!options.model.empty())
         a.insert(a.end(), { "--model", options.model });
      std::vector<std::string> all = dirs;
      all.insert(all.end(), options.folders.begin(), options.folders.end());
      if (!all.empty())
      {
         a.push_back("--add-dir");
         a.insert(a.end(), all.begin(), all.end());
      }
      // Only what the chat needs: the user's MCP servers, plugins' and skills' listings and the other built-in
      // tools would ride along on every request of every turn (about 46k tokens against 9k).
      a.insert(a.end(), { "--strict-mcp-config", "--mcp-config", R"({"mcpServers":{}})", "--disable-slash-commands",
                          "--tools", "Bash,Read,Edit,Write,Glob,Grep" });
      a.insert(a.end(), { "--allowedTools", "Bash(infinite:*)" });
      for (const std::string& c : options.commands)
         if (CommandName(c))
            a.push_back("Bash(" + c + ":*)");
      if (options.folderAccess)
         a.insert(a.end(), { "Read", "Edit", "Write", "Glob", "Grep" });
      else if (!all.empty())
         a.insert(a.end(), { "Read", "Glob", "Grep" });
      a.insert(a.end(), { "--output-format", "stream-json", "--verbose", "--append-system-prompt", kChatNote,
                          resume ? "--resume" : "--session-id", session, "--", prompt });
      return a;
   }

   bool LostSession(const std::string& text)
   {
      std::string low = text;
      std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      for (const char* k : { "no conversation found", "conversation not found", "session not found", "thread not found", "no session found" })
         if (low.find(k) != std::string::npos)
            return true;
      return false;
   }

   std::string TerminalResumeLine(AgentKind kind, const std::string& session)
   {
      if (kind == AgentKind::Antigravity)
         return "agy --conversation " + session;
      return kind == AgentKind::Codex ? "codex resume " + session : "claude --resume " + session;
   }
}
