#include "Onboarding.h"

namespace chat
{
   // Every text here is an English translation key (resources/lang/*.tsv); %s is the agent's name.
   std::vector<Problem> CheckMachine(const MachineFacts& f)
   {
      std::vector<Problem> out;
      if (f.netChecked && !f.fetchTool)
         out.push_back({ Problem::Kind::NoFetch, "This computer is missing curl, which downloads the setup. Install curl, then press Check again.", true });
      else if (f.netChecked && !f.online)
         out.push_back({ Problem::Kind::Offline, "No internet. Connect to the internet, then press Check again.", true });
      if (f.freeBytes >= 0 && f.freeBytes < kNeedBytes)
         out.push_back({ Problem::Kind::Disk, "Not enough free space. Free up 2 GB, then press Check again.", true });
      if (f.windows && f.scriptsLocked)
         out.push_back({ Problem::Kind::Locked, "This computer's settings block installers. Ask the person who manages it.", true });
      if (!f.browser)
         out.push_back({ Problem::Kind::NoBrowser, "No web browser found. You will sign in by copying an address and pasting a code.", false });
      return out;
   }

   bool HasBrowser(bool linux, const std::string& display, const std::string& wayland, bool xdgOpen)
   {
      if (!linux)
         return true;
      return xdgOpen && (!display.empty() || !wayland.empty());
   }

   std::vector<std::string> NetProbeArgs(bool windows)
   {
      // The same host the setup downloads from; the same tool it downloads with, so a proxy counts the same.
      if (windows)
         return { "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                  "if ($ExecutionContext.SessionState.LanguageMode -ne 'FullLanguage') { 'locked' }; "
                  "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}; "
                  "try { Invoke-WebRequest -UseBasicParsing -Method Head -TimeoutSec 10 https://nodejs.org/dist/index.json | Out-Null; 'online' } "
                  "catch { 'offline' }" };
      return { "/bin/sh", "-c",
               "if command -v curl >/dev/null 2>&1; then curl -fsI --max-time 10 https://nodejs.org/dist/index.json >/dev/null 2>&1 && echo online || echo offline; "
               "elif command -v wget >/dev/null 2>&1; then wget -q --spider -T 10 https://nodejs.org/dist/index.json >/dev/null 2>&1 && echo online || echo offline; "
               "else echo nofetch; fi" };
   }

   NetProbe ReadNetProbe(const std::string& output)
   {
      const std::string t = StripAnsi(output);
      auto line = [&](const char* w)
      {
         size_t at = 0;
         const std::string word = w;
         while ((at = t.find(word, at)) != std::string::npos)
         {
            const bool startOk = at == 0 || t[at - 1] == '\n';
            const size_t end = at + word.size();
            const bool endOk = end == t.size() || t[end] == '\n' || t[end] == ' ';
            if (startOk && endOk)
               return true;
            at = end;
         }
         return false;
      };
      NetProbe p;
      p.locked = line("locked");
      p.fetchTool = !line("nofetch");
      p.online = line("online");
      return p;
   }

   std::string SignUpUrl(const std::string& agentId)
   {
      if (agentId == "codex")
         return "https://chatgpt.com/";
      if (agentId == "antigravity")
         return "https://accounts.google.com/signup";
      return "https://claude.ai/login";
   }

   Friendly SetupProblem(const std::string& reason, const std::string& stepId, const std::string& agentName)
   {
      using K = Friendly::Kind;
      Friendly f;
      (void)agentName;
      if (reason == "offline")
         f = { K::Offline, "No internet. Connect to the internet, then press Try again." };
      else if (reason == "download")
         f = { K::Offline, "A download did not finish. Check the internet, then press Try again." };
      else if (reason == "disk")
         f = { K::Disk, "Not enough free space. Free up 2 GB, then press Try again." };
      else if (reason == "policy")
         f = { K::Policy, "This computer's settings block installers. Ask the person who manages it." };
      else if (reason == "password")
         f = { K::Password, "This step needs your computer password. Press Type password." };
      else if (reason == "unsupported")
         f = { K::Missing, "This kind of computer is not supported yet. Pick another one." };
      else if (reason == "nodeversion")
         f = { K::Missing, "A helper program on this computer is too old. Press Try again to get a new one." };
      else if (reason == "install")
         f = { K::Missing, "%s did not install. Press Try again." };
      else if (reason == "register" || reason == "skills" || reason == "launcher")
         f = { K::Other, "Could not connect %s to Infinite. Press Try again." };
      else if (stepId == "node")
         f = { K::Other, "Getting ready did not finish. Press Try again." };
      else if (stepId == "agent")
         f = { K::Other, "%s did not install. Press Try again." };
      else
         f = { K::Other, "Connecting to Infinite did not finish. Press Try again." };
      return f;
   }

   bool SignsInWithoutBrowser(AgentKind kind)
   {
      return kind != AgentKind::Codex;
   }

   const std::vector<Idea>& FirstIdeas()
   {
      // Labels a beginner can press without knowing what to ask; prompts that end in something to see or hear.
      static const std::vector<Idea> kIdeas = {
         { "Make something that looks like rain",
           "Make something that looks like rain, on the canvas, and show it on the output. Keep it simple. "
           "When it is done, tell me in one short sentence what to look at and one thing I could change." },
         { "Make a calm sound",
           "Make a calm, gentle sound that plays now. Keep it quiet and simple. "
           "When it is done, tell me in one short sentence what I am hearing and one thing I could change." },
         { "Show me what you can do",
           "I have never used Infinite. Make one small thing that moves and makes a sound, and start it. "
           "Then tell me in two short sentences what you made and what else I could ask for. No jargon." },
      };
      return kIdeas;
   }
}
