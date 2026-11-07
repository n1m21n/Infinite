#pragma once
// The Chat panel's first run, for someone who has never used a terminal (docs/CHAT.md, First run).
// Pure functions: facts about the machine in, plain sentences out, so a test checks every case with strings.
#include "AgentAuth.h"
#include "core/CliSetup.h"

#include <string>
#include <vector>

namespace chat
{
   // ---- 0. this computer ----
   struct MachineFacts
   {
      bool onLinux = false, windows = false;
      bool netChecked = false, online = false;
      bool fetchTool = true;          // curl or wget (Linux can have neither)
      bool scriptsLocked = false;     // Windows PowerShell outside FullLanguage: installers cannot run
      long long freeBytes = -1;       // in the home folder; -1 = unknown
      bool browser = true;
   };
   constexpr long long kNeedBytes = 1500LL * 1024 * 1024;  // the helper, one agent, its updates
   struct Problem
   {
      enum class Kind { Offline, Disk, NoFetch, Locked, NoBrowser };
      Kind kind;
      std::string text;   // what is wrong and the one next thing, in one or two short sentences
      bool blocks = true; // false: setup goes on (no browser: sign in by pasting)
   };
   std::vector<Problem> CheckMachine(const MachineFacts& f);

   // Can the agents open a web page here? macOS and Windows always can; Linux needs a desktop session
   // (DISPLAY or WAYLAND_DISPLAY) and xdg-open, and an SSH login without a forwarded display has neither.
   bool HasBrowser(bool isLinux, const std::string& display, const std::string& wayland, bool xdgOpen);

   // The network probe the app runs before setup: argv for the platform, and how to read what it printed.
   std::vector<std::string> NetProbeArgs(bool windows);
   struct NetProbe
   {
      bool online = false, fetchTool = true, locked = false;
   };
   NetProbe ReadNetProbe(const std::string& output);

   // ---- 1-2. who to talk to, and an account ----
   // The provider's own sign-up page. Infinite never sees what is typed there.
   std::string SignUpUrl(const std::string& agentId);
   // Plain words for each check-in, shown before anything is installed or opened (never run on its own).
   // They come from the agent's own row (CliSetup::Agent), so each agent and OS says its own paths.
   //  InstallNote: the agent is missing: what is downloaded, where it goes, what else is added.
   //  ConnectNote: the agent is installed: nothing is downloaded; what is written to connect it to Infinite.
   //  SetUpNote: picks between the two from `installed`.
   //  SignInNote: what the sign-in opens and what the account needs (Claude Code is not in Claude's free plan).
   std::string InstallNote(const CliSetup::Agent& agent, bool windows);
   std::string ConnectNote(const CliSetup::Agent& agent, bool windows);
   std::string SetUpNote(const CliSetup::Agent& agent, bool windows, bool installed);
   std::string SignInNote(const CliSetup::Agent& agent);
   // Two or three words on the agent's pick row, so what the account needs is known before anything starts.
   std::string PlanTag(const std::string& agentId);

   // A sign-in is given this long when the person is making a new account first.
   constexpr double kSignUpSeconds = 900.0;

   // ---- 3. setup failures ----
   // status.json names why a step failed (the setup script's `reason`); this says it for a beginner.
   Friendly SetupProblem(const std::string& reason, const std::string& stepId, const std::string& agentName);

   // ---- 4. sign-in without a browser ----
   // False when this agent's sign-in cannot finish without a browser on this computer (ChatGPT's page
   // returns to a local address); Claude and Gemini show a code to paste back.
   bool SignsInWithoutBrowser(AgentKind kind);

   // ---- 6. first ideas ----
   struct Idea
   {
      const char* label;   // what the button says
      const char* prompt;  // what is sent
   };
   const std::vector<Idea>& FirstIdeas();
}
