#include "AgentRun.h"

#include "core/term/PtyHost.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace chat
{
   struct AgentRun::Impl
   {
      std::mutex lock;
      std::vector<AgentEvent> queue;
      std::unique_ptr<term::PtyHost> pty;
      std::atomic<bool> finished{ false };
      std::atomic<bool> stopped{ false };

      void Push(std::vector<AgentEvent>& events)
      {
         std::lock_guard<std::mutex> g(lock);
         queue.insert(queue.end(), events.begin(), events.end());
         events.clear();
      }

      void Run(const Options& o)
      {
         term::PtySpawn spawn;
#if defined(_WIN32)
         std::string line;
         for (const std::string& a : o.argv)
         {
            std::string q = "\"";
            for (char c : a)
               q += c == '"' ? std::string("\\\"") : std::string(1, c);
            line += (line.empty() ? "" : " ") + q + "\"";
         }
         spawn.shell = "cmd.exe";
         spawn.args = { "/c", line };
#else
         // The login shell reads the user's profile (PATH), then becomes the agent with argv untouched.
         const char* shell = std::getenv("SHELL");
         spawn.shell = shell != nullptr && shell[0] != '\0' ? shell : "/bin/sh";
         spawn.args = { "-l", "-c", "exec \"$@\"", "infinite-chat" };
         spawn.args.insert(spawn.args.end(), o.argv.begin(), o.argv.end());
#endif
         spawn.cwd = o.cwd;
         spawn.env = o.env;
         spawn.env.push_back({ "TERM", "dumb" });
         spawn.env.push_back({ "NO_COLOR", "1" });
         spawn.cols = 400;  // long lines are not wrapped by the PTY, but some CLIs size their output to it
         spawn.rows = 50;

         AgentStream stream(o.kind);
         std::vector<AgentEvent> events;
         std::string error, tail;
         bool ended = false;
         {
            std::lock_guard<std::mutex> g(lock);
            pty = term::PtyHost::Create();
         }
         if (pty == nullptr || !pty->Start(spawn, &error))
         {
            AgentEvent e;
            e.kind = AgentEvent::Kind::End;
            e.error = true;
            e.text = error.empty() ? "the agent did not start" : error;
            events.push_back(e);
            Push(events);
            finished = true;
            return;
         }
         if (stopped)
            pty->Kill(true);
         char buffer[8192];
         for (int n; (n = pty->Read(buffer, sizeof buffer)) > 0;)
         {
            const std::string bytes(buffer, (size_t)n);
            stream.Feed(bytes, events);
            // What is not an event (a missing command, a login prompt) explains a turn that ends with none.
            tail = (tail + bytes).substr(tail.size() + bytes.size() > 600 ? tail.size() + bytes.size() - 600 : 0);
            for (const AgentEvent& e : events)
               ended = ended || e.kind == AgentEvent::Kind::End;
            Push(events);
         }
         stream.Feed("\n", events);
         for (const AgentEvent& e : events)
            ended = ended || e.kind == AgentEvent::Kind::End;
         const int code = pty->ExitCode();
         if (!ended)
         {
            AgentEvent e;
            e.kind = AgentEvent::Kind::End;
            e.error = !stopped;
            e.text = stopped ? "stopped" : tail.find('{') == std::string::npos && !tail.empty() ? tail : "the agent ended (exit " + std::to_string(code) + ")";
            events.push_back(e);
         }
         Push(events);
         finished = true;
      }
   };

   std::shared_ptr<AgentRun> AgentRun::Start(const Options& options)
   {
      std::shared_ptr<AgentRun> run(new AgentRun());
      run->mImpl = std::make_shared<Impl>();
      std::thread([impl = run->mImpl, options] { impl->Run(options); }).detach();
      return run;
   }

   AgentRun::~AgentRun() { Stop(); }

   void AgentRun::Poll(std::vector<AgentEvent>& out)
   {
      std::lock_guard<std::mutex> g(mImpl->lock);
      out.insert(out.end(), mImpl->queue.begin(), mImpl->queue.end());
      mImpl->queue.clear();
   }

   bool AgentRun::Done() const
   {
      std::lock_guard<std::mutex> g(mImpl->lock);
      return mImpl->finished && mImpl->queue.empty();
   }

   void AgentRun::Stop()
   {
      if (mImpl->finished || mImpl->stopped.exchange(true))
         return;
      std::lock_guard<std::mutex> g(mImpl->lock);
      if (mImpl->pty != nullptr)
         mImpl->pty->Kill(true);
   }
}
