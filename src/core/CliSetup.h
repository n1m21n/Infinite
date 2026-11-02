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
      const char* configFile;   // the agent's own config, relative to HOME; holds the MCP registration
      const char* skillsDir;    // where the agent loads skills from, relative to HOME; "" = it has none
      const char* loginHint;    // shown under the agent row
   };
   const std::vector<Agent>& Agents();
   const Agent* FindAgent(const std::string& id);   // nullptr when unknown

   // The AI skill files Infinite ships (patch authoring, Field language, Expression globals). Setup copies
   // them into the agent's skills folder as <name>/SKILL.md; the launch check compares them byte for byte.
   struct Skill { const char* name; const char* content; };
   const std::vector<Skill>& Skills();

   enum class StepState { kPending, kRunning, kOk, kSkipped, kFailed, kWaiting, kRepair };
   struct StepStatus
   {
      const char* id;
      const char* title;
      StepState state = StepState::kPending;
      std::string detail;
      std::string repairLine;   // only on a kRepair row: the line its Repair button types
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
      std::string dir, script, status, config, record;   // record: the exe path Infinite writes at each start
   };
   Paths PathsFor(const std::string& appSupportDir, Os os);

   // The two scripts: shell-quoted variables first, then the fixed body. This is the exact text that runs.
   // `only` limits the run to those step ids (empty = all); `prior` seeds the other steps' last states so
   // a repair leaves the rest of status.json as it was.
   std::string BuildScript(Os os, const std::string& exe, const Agent& agent, const std::string& statusPath,
                           const std::vector<std::string>& only = {}, const Status* prior = nullptr);
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

   // ---- the silent check at launch (file stats and one small file read; never spawns a process) ----
   enum class Health { kUnknown, kOk, kBroken };
   struct HealthReport
   {
      bool checked = false;
      Health step[kStepCount] = {};
      std::string why[kStepCount];
   };
   struct HealthInput
   {
      Os os = Os::kMac;
      std::string home, binDir, pathEnv, exe, recordFile;
      const Agent* agent = nullptr;
   };
   // Does the recorded (or a usual install) Infinite still exist? This is what the launcher does.
   bool LauncherResolves(Os os, const std::string& recordFile);
   HealthReport CheckHealth(const HealthInput& in);
   // Real HOME/PATH version. At launch: refreshes the recorded exe path when setup was done before, then
   // checks on a worker thread. LatestHealth() returns the last finished report.
   void StartLaunchCheck();
   HealthReport LatestHealth();

   // What the page shows. Everything it draws and every button comes from here.
   enum class Action { kNone, kSetup, kInstallAgent, kStartAgent, kRepairStep };
   struct View
   {
      std::vector<StepStatus> steps;
      std::string setupLabel;       // "Connect terminal & agent" / "Connect again" / "Connecting..."
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
                 const Paths& paths, const HealthReport* health = nullptr);

   // Opens a Terminal tab titled `title` and types `line` into it. The app supplies the real one.
   using Runner = std::function<void(const std::string& title, const std::string& line)>;
   // Runs the action on the fake or real runner. Writes the script first for kSetup. False when nothing ran.
   bool Perform(Action action, Os os, const Agent& agent, const std::string& exe, const Paths& paths, const Runner& run,
                std::string* error, const std::string& step = std::string(), const Status* prior = nullptr);

   // Process-wide state used by the app (Settings page and the Terminal panel).
   Config& Prefs();
   std::string RealExecutable();
}
