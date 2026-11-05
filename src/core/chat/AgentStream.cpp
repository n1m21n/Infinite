#include "AgentStream.h"

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
            out.push_back(Event(AgentEvent::Kind::End, ""));
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
         "You are answering in Infinite's chat panel, not a terminal. Follow the infinite-live-session reply rule: "
         "name every node as @title#index and every param as @title#index.key, with values and units. "
         "The chat already lists every change you made to the patch, so do not repeat them: answer in one or two short "
         "sentences, show code only when you wrote code, and give the path of any picture you made. "
         "Pick nodes with `infinite find <plain words>`, not from memory. Make samples with `infinite studio generate` and MIDI with "
         "`infinite notes generate`; find the user's files with `infinite files`. "
         "Run `infinite` commands on their own: no cd, pipes or redirects around them, which are not allowed and fail.";
      // Antigravity takes only `command(...)` grants; this one lets the turn run `infinite` and nothing else unasked.
      const char* const kAgySettings = "{\"permissions\":{\"allow\":[\"command(infinite)\"]}}";
   }

   std::vector<std::string> AgentArgs(AgentKind kind, const std::string& prompt, const std::string& session, bool resume,
                                      const std::vector<std::string>& dirs, const TurnOptions& options)
   {
      if (kind == AgentKind::Antigravity)
      {
         // No system-prompt flag: the note leads the first message. The prompt follows -p directly.
         std::vector<std::string> a = { "agy", "--output-format", "stream-json", "--settings", kAgySettings };
         if (!options.model.empty())
            a.insert(a.end(), { "--model", options.model });
         if (options.folderAccess)
            a.insert(a.end(), { "--mode", "accept-edits" });
         for (const std::string& d : dirs)
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
            a.insert(a.end(), { "-s", "workspace-write" });
         if (resume && !session.empty())
            a.insert(a.end(), { "resume", session });
         a.push_back(prompt);
         return a;
      }
      // --allowedTools and --add-dir take several values, so another option follows each and `--` ends options.
      std::vector<std::string> a = { "claude", "-p" };
      if (!options.model.empty())
         a.insert(a.end(), { "--model", options.model });
      if (!dirs.empty())
      {
         a.push_back("--add-dir");
         a.insert(a.end(), dirs.begin(), dirs.end());
      }
      a.insert(a.end(), { "--allowedTools", "Bash(infinite:*)" });
      if (options.folderAccess)
         a.insert(a.end(), { "Read", "Edit", "Write", "Glob", "Grep" });
      else if (!dirs.empty())
         a.push_back("Read");
      a.insert(a.end(), { "--output-format", "stream-json", "--verbose", "--append-system-prompt", kChatNote,
                          resume ? "--resume" : "--session-id", session, "--", prompt });
      return a;
   }

   std::string TerminalResumeLine(AgentKind kind, const std::string& session)
   {
      if (kind == AgentKind::Antigravity)
         return "agy --conversation " + session;
      return kind == AgentKind::Codex ? "codex resume " + session : "claude --resume " + session;
   }
}
