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
      else
         Codex(j, out);
   }

   std::vector<std::string> AgentArgs(AgentKind kind, const std::string& prompt, const std::string& session, bool resume)
   {
      if (kind == AgentKind::Codex)
      {
         std::vector<std::string> a = { "codex", "exec", "--json" };
         if (resume && !session.empty())
            a.insert(a.end(), { "resume", session });
         a.push_back(prompt);
         return a;
      }
      // --allowedTools takes several values, so another option follows it and `--` ends options.
      return { "claude", "-p", "--allowedTools", "Bash(infinite:*)", "--output-format", "stream-json", "--verbose",
               resume ? "--resume" : "--session-id", session, "--", prompt };
   }

   std::string TerminalResumeLine(AgentKind kind, const std::string& session)
   {
      return kind == AgentKind::Codex ? "codex resume " + session : "claude --resume " + session;
   }
}
