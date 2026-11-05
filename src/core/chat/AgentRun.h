#pragma once
// One agent turn as a child process (docs/plans/agent-collab/README.md C5). The child runs through the
// user's login shell so their PATH finds `claude`, `codex` or `agy`; a reader thread parses its stream-json
// and the main thread takes the events each frame. Never blocks the caller.
#include "AgentStream.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chat
{
   // The text after `cmd.exe` that runs `argv` with every character reaching the program as typed:
   // `/d /s /c "..."`, each argument quoted the way the C runtime reads it, then every cmd metacharacter
   // escaped with ^ so cmd never runs `&`, `|`, `<`, `>` or expands `%var%`. Line breaks become spaces (cmd
   // ends a command at one). npm installs the agents as .cmd shims, which read their arguments a second time
   // without the carets; an argument that has both a `"` and a metacharacter gets its quotes as ” so that
   // second read stays inside quotes.
   std::string WindowsCmdArgs(const std::vector<std::string>& argv);

   class AgentRun
   {
   public:
      struct Options
      {
         AgentKind kind = AgentKind::Claude;
         std::vector<std::string> argv;  // AgentArgs()
         std::string cwd;
         std::vector<std::pair<std::string, std::string>> env;
         bool raw = false;  // not a stream-json turn (a sign-in, a setup): End carries the exit status, Log() the text
      };
      static std::shared_ptr<AgentRun> Start(const Options& options);
      ~AgentRun();

      void Poll(std::vector<AgentEvent>& out);  // events since the last call
      bool Done() const;                        // the child is gone and every event was taken
      bool Write(const std::string& text);      // types into the child (a pasted sign-in code); false once it is gone
      void Stop();                              // ends the turn; an End event follows
      std::string Log() const;                  // everything the child printed (last ~16 KB), for a Details fold
      unsigned long long Bytes() const;         // how much the child printed so far, all of it (Log() keeps the end)

      struct Impl;

   private:
      std::shared_ptr<Impl> mImpl;
   };
}
