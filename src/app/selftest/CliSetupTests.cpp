// INFINITE_CLISETUPTEST: walks the "Command line & agents" section through every state with fake
// status.json files (docs/plans/terminal/cli-setup.md, Tests). No window, no shell, no installer:
// the runner is a fake that records the line it was given. Early exit before glfwInit().
#include "app/AppShared.h"
#include "core/CliSetup.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace app
{
   namespace
   {
      std::string StatusJson(int version, bool running, long long started, const char* states[5], const char* extra = "")
      {
         static const char* const ids[5] = { "path", "headless", "node", "mcp", "agent" };
         std::string s = "{\"version\":" + std::to_string(version) + ",\"os\":\"macos\",\"agent\":\"claude\",\"running\":" +
                         (running ? "true" : "false") + ",\"startedAt\":" + std::to_string(started) + ",\"finishedAt\":0,\"steps\":[";
         for (int i = 0; i < 5; i++)
            s += std::string(i ? "," : "") + "{\"id\":\"" + ids[i] + "\",\"state\":\"" + states[i] + "\",\"detail\":\"d" + std::to_string(i) + "\"}";
         return s + "]" + extra + "}";
      }
   }

   bool RunCliSetupTest()
   {
      setvbuf(stdout, nullptr, _IONBF, 0);
      bool ok = true;
      auto check = [&](bool c, const char* what)
      {
         std::printf("CLISETUPTEST %s  %s\n", what, c ? "OK" : "FAIL");
         ok = ok && c;
      };
      using namespace CliSetup;
      const Agent& claude = *FindAgent("claude");
      const long long now = 1800000000;
      const std::string exe = "/Applications/Infinite's Dir/Infinite.app/Contents/MacOS/Infinite";
      const Paths paths = PathsFor("/tmp/fake support", Os::kMac);
      auto describe = [&](const std::string& json, bool installed, Os os = Os::kMac)
      {
         return Describe(os, ParseStatus(json, now), claude, installed, exe, PathsFor("/tmp/fake support", os));
      };

      // 1. no file: everything grey, the first-run button.
      {
         View v = describe("", false);
         check(v.steps.size() == 5 && v.steps[0].state == StepState::kPending, "no file: five pending steps");
         check(v.setupLabel == "Set up everything" && v.setupEnabled && v.summary == "Not set up yet.", "no file: first-run button");
         check(v.showInstall && !v.startEnabled, "agent missing: Install shown, Start disabled");
      }
      // 2. garbage, wrong version, wrong shape: the same as no file.
      {
         const char* st[5] = { "ok", "ok", "ok", "ok", "ok" };
         check(!ParseStatus("not json {", now).present, "garbage reads as not set up");
         check(!ParseStatus(StatusJson(2, false, now, st), now).present, "unknown version reads as not set up");
         check(!ParseStatus("{\"version\":1,\"steps\":5}", now).present, "wrong shape reads as not set up");
      }
      // 3. running, two of five done.
      {
         const char* st[5] = { "ok", "skipped", "running", "pending", "pending" };
         View v = describe(StatusJson(1, true, now - 5, st), false);
         check(v.setupLabel == "Setting up..." && !v.setupEnabled, "running: button disabled");
         check(v.steps[0].Green() && v.steps[1].Green() && v.steps[2].state == StepState::kRunning, "running: ticks and the live step");
         check(v.summary == "2 of 5 done so far.", "running: summary");
      }
      // 4. a run that never finished and is older than 15 minutes is dead.
      {
         const char* st[5] = { "ok", "ok", "running", "pending", "pending" };
         View v = describe(StatusJson(1, true, now - kStaleSeconds - 1, st), false);
         check(v.setupEnabled && v.steps[2].state == StepState::kPending, "stale run: button back, step no longer 'running'");
      }
      // 5. all green with the agent installed.
      {
         const char* st[5] = { "ok", "skipped", "skipped", "ok", "ok" };
         View v = describe(StatusJson(1, false, now - 90, st), true);
         check(v.setupLabel == "Set up again" && v.summary == "Everything is set up.", "all green: summary");
         check(!v.showInstall && v.startEnabled, "agent present: Start enabled, no Install");
         int green = 0;
         for (const StepStatus& s : v.steps) green += s.Green();
         check(green == 5, "all green: five ticks");
      }
      // 6. one failed, one waiting.
      {
         const char* st[5] = { "ok", "ok", "failed", "failed", "waiting" };
         Status s = ParseStatus(StatusJson(1, false, now - 30, st), now);
         check(s.FailedCount() == 2 && s.GreenCount() == 2, "failed and waiting are not green");
         View v = Describe(Os::kMac, s, claude, false, exe, paths);
         check(v.summary == "2 steps need attention.", "failed: summary");
         check(v.steps[2].detail == "d2", "failed: the detail is shown");
      }
      // 7. everything but the agent: the summary says what to do next.
      {
         const char* st[5] = { "ok", "ok", "ok", "ok", "waiting" };
         View v = describe(StatusJson(1, false, now - 30, st), false);
         check(v.summary.find("after installing Claude Code") != std::string::npos, "waiting agent: summary names the next step");
      }

      // 8. the buttons hand the exact line to the runner and write the script first.
      {
         const std::filesystem::path dir = std::filesystem::temp_directory_path() / "infinite_clisetup_test";
         std::filesystem::remove_all(dir);
         const Paths real = PathsFor(dir.string(), Os::kMac);
         std::string title, line, err;
         auto runner = [&](const std::string& t, const std::string& l) { title = t; line = l; };
         check(Perform(Action::kSetup, Os::kMac, claude, exe, real, runner, &err), "setup action runs");
         check(line == "sh '" + real.script + "'" && std::filesystem::exists(real.script), "setup types one sh line; script written");
         std::ifstream f(real.script);
         std::string script((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
         check(script == BuildScript(Os::kMac, exe, claude, real.status), "the file is the exact script the page shows");
         check(script.find("EXE='/Applications/Infinite'\\''s Dir/Infinite.app/Contents/MacOS/Infinite'") != std::string::npos, "single quote in the exe path is escaped");
         for (const char* step : { "begin 1 path", "begin 2 headless", "begin 3 node", "begin 4 mcp", "begin 5 agent" })
            check(script.find(step) != std::string::npos, step);
         check(script.find("sudo") != std::string::npos && script.find("brew install") != std::string::npos, "installs are visible in the script");
         check(Perform(Action::kInstallAgent, Os::kMac, claude, exe, real, runner, &err) && line == "curl -fsSL https://claude.ai/install.sh | bash", "install types the vendor's command");
         check(Perform(Action::kStartAgent, Os::kMac, claude, exe, real, runner, &err), "start action runs");
         check(line.find("claude mcp get infinite >/dev/null 2>&1 ||") != std::string::npos && line.find("claude mcp add --scope user -e INFINITE_BIN='/Applications/Infinite'\\''s Dir") != std::string::npos &&
               line.size() > 6 && line.compare(line.size() - 6, 6, "claude") == 0,
               "start registers MCP when missing, then launches");
         check(!Perform(Action::kSetup, Os::kMac, claude, "", real, runner, &err) && !err.empty(), "no executable: refused with a reason");
         check(!Perform(Action::kNone, Os::kMac, claude, exe, real, runner, &err), "no action does nothing");
         std::filesystem::remove_all(dir);
      }

      // 9. Windows variant, text only.
      {
         View v = describe("", false, Os::kWindows);
         check(v.setupLine.find("powershell -NoProfile -ExecutionPolicy Bypass -File") == 0, "windows: powershell line");
         check(v.installLine.find("irm ") == 0, "windows: install is the PowerShell installer");
         const std::string ps = BuildScript(Os::kWindows, "C:\\Program Files\\Infinite\\Infinite.exe", claude, "C:\\s\\status.json");
         check(ps.find("$Exe = 'C:\\Program Files\\Infinite\\Infinite.exe'") != std::string::npos && ps.find("Step ${n}/5") != std::string::npos, "windows: script carries the exe and the steps");
      }

      // 10. the agent probe and the config file.
      {
         const std::filesystem::path dir = std::filesystem::temp_directory_path() / "infinite_clisetup_probe";
         std::filesystem::remove_all(dir);
         std::filesystem::create_directories(dir / ".local" / "bin");
         check(!AgentInstalled(Os::kLinux, claude, dir.string(), ""), "probe: not found");
         std::ofstream((dir / ".local" / "bin" / "claude").string()) << "x";
         check(AgentInstalled(Os::kLinux, claude, dir.string(), ""), "probe: found in ~/.local/bin");
         const std::string cfg = (dir / "config.json").string();
         check(LoadConfig(cfg).agent == "claude" && !LoadConfig(cfg).autoStart, "config: defaults (auto-start off)");
         Config c; c.autoStart = true; SaveConfig(cfg, c);
         check(LoadConfig(cfg).autoStart, "config: auto-start round trip");
         std::ofstream(cfg) << "{\"agent\":\"nope\",\"autoStart\":true}";
         check(LoadConfig(cfg).agent == "claude", "config: unknown agent falls back");
         std::filesystem::remove_all(dir);
      }

      std::printf("CLISETUPTEST %s\n", ok ? "PASS" : "FAIL");
      return ok;
   }
}
