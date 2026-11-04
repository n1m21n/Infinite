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
      void Stop();                              // ends the turn; an End event follows
      std::string Log() const;                  // everything the child printed (last ~16 KB), for a Details fold

      struct Impl;

   private:
      std::shared_ptr<Impl> mImpl;
   };
}
