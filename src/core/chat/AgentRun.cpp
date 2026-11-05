#include "AgentRun.h"
#include "core/CliSetup.h"

#include "core/term/PtyHost.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
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
      std::string log;
      std::atomic<unsigned long long> total{ 0 };

      void AppendLog(const std::string& bytes)
      {
         std::lock_guard<std::mutex> g(lock);
         total += bytes.size();
         log += bytes;
         if (log.size() > 16384)
            log.erase(0, log.size() - 16384);
      }

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
         spawn.shell = "cmd.exe";
         spawn.rawArgs = WindowsCmdArgs(o.argv);
#else
         // The login shell reads the user's profile (PATH), then becomes the agent with argv untouched.
         const char* shell = std::getenv("SHELL");
         spawn.shell = shell != nullptr && shell[0] != '\0' ? shell : "/bin/sh";
         spawn.args = { "-l", "-c", "exec \"$@\"", "infinite-chat" };
         spawn.args.insert(spawn.args.end(), o.argv.begin(), o.argv.end());
#endif
         spawn.cwd = o.cwd;
         spawn.env = o.env;
#if defined(_WIN32)
         // A program installed since the app started (npm's %APPDATA%\\npm) is not on the PATH the app inherited.
         if (!o.argv.empty() && o.argv[0].find_first_of("/\\\\") == std::string::npos)
         {
            const char* home = std::getenv("USERPROFILE");
            const char* path = std::getenv("PATH");
            const std::string dir = CliSetup::BinaryDir(CliSetup::Os::kWindows, o.argv[0], home != nullptr ? home : "", path != nullptr ? path : "");
            if (!dir.empty())
            {
               bool set = false;
               for (auto& kv : spawn.env)
                  if (kv.first == "PATH")
                     kv.second = dir + ";" + kv.second, set = true;
               if (!set)
                  spawn.env.push_back({ "PATH", dir + ";" + (path != nullptr ? path : "") });
            }
         }
#else
         // The login shell reads .zprofile but not .zshrc, so a program that only .zshrc's PATH reaches (codex in a
         // Homebrew keg) would be "command not found" here although the user can run it. Put its folder, and so the
         // `node` beside it, on the PATH the agent starts with.
         if (!o.argv.empty() && o.argv[0].find('/') == std::string::npos)
         {
            const char* home = std::getenv("HOME");
            const char* path = std::getenv("PATH");
            std::string current = path != nullptr ? path : "";
            for (auto& kv : spawn.env)
               if (kv.first == "PATH")
                  current = kv.second;
            const std::string dir = CliSetup::BinaryDir(CliSetup::CurrentOs(), o.argv[0], home != nullptr ? home : "", current);
            if (!dir.empty() && (":" + current + ":").find(":" + dir + ":") == std::string::npos)
            {
               bool set = false;
               for (auto& kv : spawn.env)
                  if (kv.first == "PATH")
                     kv.second = dir + ":" + kv.second, set = true;
               if (!set)
                  spawn.env.push_back({ "PATH", dir + ":" + current });
            }
         }
#endif
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
            AppendLog(bytes);
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
         if (o.raw)
         {
            AgentEvent e;
            e.kind = AgentEvent::Kind::End;
            e.error = !stopped && code != 0;
            e.text = stopped ? "stopped" : tail;
            events.push_back(e);
            Push(events);
            finished = true;
            return;
         }
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

   std::string WindowsCmdArgs(const std::vector<std::string>& argv)
   {
      auto meta = [](char c) { return c != '\0' && std::strchr("&|<>^%!()\"", c) != nullptr; };
      // What the shim's second read would act on outside quotes; ( ) and ! do nothing there.
      auto shimMeta = [](char c) { return c != '\0' && std::strchr("&|<>^%", c) != nullptr; };
      std::string line;
      for (std::string a : argv)
      {
         for (char& c : a)
            if (c == '\n' || c == '\r')
               c = ' ';
         bool risky = false;
         for (char c : a)
            risky = risky || shimMeta(c);
         if (risky && a.find('"') != std::string::npos)
         {
            std::string curly;
            for (char c : a)
               curly += c == '"' ? std::string("\xE2\x80\x9D") : std::string(1, c);
            a = curly;
         }
         // The C runtime's rules: backslashes double only before a quote.
         std::string q = "\"";
         size_t slashes = 0;
         for (char c : a)
         {
            if (c == '\\')
            {
               slashes++;
               continue;
            }
            q.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
            slashes = 0;
            q += c;
         }
         q.append(slashes * 2, '\\');
         q += '"';
         line += (line.empty() ? "" : " ") + q;
      }
      std::string escaped;
      for (char c : line)
      {
         if (meta(c))
            escaped += '^';
         escaped += c;
      }
      // /s: cmd drops only the outer pair of quotes and keeps the rest exactly.
      return "/d /s /c \"" + escaped + "\"";
   }

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

   std::string AgentRun::Log() const
   {
      std::lock_guard<std::mutex> g(mImpl->lock);
      return mImpl->log;
   }

   unsigned long long AgentRun::Bytes() const { return mImpl->total.load(); }

   bool AgentRun::Write(const std::string& text)
   {
      if (mImpl->finished)
         return false;
      std::lock_guard<std::mutex> g(mImpl->lock);
      return mImpl->pty != nullptr && mImpl->pty->Write(text.data(), (int)text.size());
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
