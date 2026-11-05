#pragma once
// The agent behind the chat (docs/plans/agent-collab/README.md C5): one headless turn per message,
// Claude Code `claude -p --output-format stream-json`, Codex `codex exec --json` or Antigravity
// `agy --output-format stream-json -p`, read line by line into events the chat draws. The same
// session id resumes in the terminal.
#include <string>
#include <vector>

namespace chat
{
   enum class AgentKind { Claude, Codex, Antigravity };

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
      std::string mReply;  // Antigravity: the reply step's text so far, sent whole when the step is done
   };

   // What the user chose for a turn: a model ("" = the agent's own), and whether it may also read and edit
   // files in its folder (else it only runs `infinite` and reads what the user attached).
   struct TurnOptions
   {
      std::string model;
      bool folderAccess = false;
      std::vector<std::string> folders;   // more folders from Settings, read (and edited with folderAccess)
      std::vector<std::string> commands;  // commands allowed besides `infinite`
   };

   // What would have let a denied step run: a command to allow ("ffprobe"), or a folder to open to agents.
   // `denied` is a "Not allowed" line (a Bash command, or "Read /path"). None for commands too wide to allow
   // with one click (a shell, sudo, rm).
   struct DeniedFix
   {
      enum class Kind { None, Command, Folder } kind = Kind::None;
      std::string value;
   };
   DeniedFix FixFor(const std::string& denied);

   // argv for one turn. The prompt goes as an argument: a PTY line holds about 1 KB.
   // `dirs`: folders of attached files the turn may read (ChatFiles FileDirs).
   std::vector<std::string> AgentArgs(AgentKind kind, const std::string& prompt, const std::string& session, bool resume,
                                      const std::vector<std::string>& dirs = {}, const TurnOptions& options = TurnOptions());
   // The line the terminal face runs to open the same conversation.
   std::string TerminalResumeLine(AgentKind kind, const std::string& session);
}
