#pragma once
// The agent behind the chat (docs/plans/agent-collab/README.md C5): one headless turn per message,
// Claude Code `claude -p --output-format stream-json` or Codex `codex exec --json`, read line by
// line into events the chat draws. The same session id resumes in the terminal face.
#include <string>
#include <vector>

namespace chat
{
   enum class AgentKind { Claude, Codex };

   struct AgentEvent
   {
      enum class Kind { Session, Text, Tool, ToolResult, End };
      Kind kind = Kind::Text;
      std::string text;  // Session: the id; Text: the reply; Tool: what it ran; ToolResult: its output
      bool error = false;               // ToolResult, End
      std::vector<std::string> denied;  // End: tools the permission mode refused ("Write /x")
   };

   class AgentStream
   {
   public:
      explicit AgentStream(AgentKind kind) : mKind(kind) {}
      // Bytes from the process (PTY output, so \r\n); whole lines become events, the rest waits.
      void Feed(const std::string& bytes, std::vector<AgentEvent>& out);

   private:
      void Line(const std::string& line, std::vector<AgentEvent>& out);
      AgentKind mKind;
      std::string mPending;
   };

   // argv for one turn. The prompt goes as an argument: a PTY line holds about 1 KB.
   std::vector<std::string> AgentArgs(AgentKind kind, const std::string& prompt, const std::string& session, bool resume);
   // The line the terminal face runs to open the same conversation.
   std::string TerminalResumeLine(AgentKind kind, const std::string& session);
}
