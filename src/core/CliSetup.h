#pragma once

#include <functional>
#include <string>
#include <vector>

// "Command line & agents" setup (docs/plans/terminal/cli-setup.md). Pure logic: no ImGui, no process
// spawn. The app types the lines built here into a Terminal tab; the script it runs writes status.json;
// the Settings page reads only that file (Describe()). A test replaces the runner and the files.
namespace CliSetup
{
   enum class Os { kMac, kLinux, kWindows };
   Os CurrentOs();

   // One row per coding agent. Commands are trusted constants; {EXE} stands for the Infinite executable.
   struct Agent
   {
      const char* id;
      const char* name;
      const char* binary;       // file name looked for (without .exe/.cmd)
      const char* installUnix;
      const char* installWin;
      const char* launch;
      const char* mcpCheck;     // prints the registration; exit code nonzero when missing
      const char* mcpAdd;
      const char* mcpRemove;
      const char* loginHint;    // shown under the agent row
   };
   const std::vector<Agent>& Agents();
   const Agent* FindAgent(const std::string& id);   // nullptr when unknown

   enum class StepState { kPending, kRunning, kOk, kSkipped, kFailed, kWaiting };
   struct StepStatus
   {
      const char* id;
      const char* title;
      StepState state = StepState::kPending;
      std::string detail;
      bool Green() const { return state == StepState::kOk || state == StepState::kSkipped; }
   };
   constexpr int kStepCount = 5;

   struct Status
   {
      bool present = false;     // a usable status.json was read
      bool running = false;     // false once the file is older than kStaleSeconds
      long long startedAt = 0, finishedAt = 0;
      std::vector<StepStatus> steps;   // always kStepCount entries
      int GreenCount() const;
      int FailedCount() const;
   };
   constexpr long long kStaleSeconds = 15 * 60;
   // Never throws; garbage, an unknown version or an empty string give present=false with pending steps.
   Status ParseStatus(const std::string& json, long long now);

   struct Paths
   {
      std::string dir, script, status, config;
   };
   Paths PathsFor(const std::string& appSupportDir, Os os);

   // The two scripts: shell-quoted variables first, then the fixed body. This is the exact text that runs.
   std::string BuildScript(Os os, const std::string& exe, const Agent& agent, const std::string& statusPath);
   // The one line typed into the Terminal tab.
   std::string SetupLine(Os os, const std::string& scriptPath);
   std::string InstallAgentLine(Os os, const Agent& agent);
   // Registers MCP when missing, then starts the agent.
   std::string StartAgentLine(Os os, const std::string& exe, const Agent& agent);

   // File probe: is the agent's binary in PATH or a usual install folder? `home` and `pathEnv` are injected.
   bool AgentInstalled(Os os, const Agent& agent, const std::string& home, const std::string& pathEnv);

   bool AgentInstalledHere(const Agent& agent);   // the real HOME and PATH

   struct Config
   {
      std::string agent = "claude";
      bool autoStart = false;
   };
   Config LoadConfig(const std::string& file);
   void SaveConfig(const std::string& file, const Config& config);

   // What the page shows. Everything it draws and every button comes from here.
   enum class Action { kNone, kSetup, kInstallAgent, kStartAgent };
   struct View
   {
      std::vector<StepStatus> steps;
      std::string setupLabel;       // "Set up everything" / "Set up again" / "Setting up..."
      bool setupEnabled = true;
      std::string summary;          // "3 of 5 done", "1 step needs attention", ...
      std::string agentName;
      bool agentInstalled = false;
      bool showInstall = false;     // agent missing
      bool startEnabled = false;
      std::string setupLine, installLine, startLine;   // the exact text each button types
      std::string loginHint;
      bool supported = true;        // false when there is no script for this OS
      std::string unsupported;
   };
   View Describe(Os os, const Status& status, const Agent& agent, bool agentInstalled, const std::string& exe,
                 const Paths& paths);

   // Opens a Terminal tab titled `title` and types `line` into it. The app supplies the real one.
   using Runner = std::function<void(const std::string& title, const std::string& line)>;
   // Runs the action on the fake or real runner. Writes the script first for kSetup. False when nothing ran.
   bool Perform(Action action, Os os, const Agent& agent, const std::string& exe, const Paths& paths, const Runner& run,
                std::string* error);

   // Process-wide state used by the app (Settings page and the Terminal panel).
   Config& Prefs();
   std::string RealExecutable();
}
