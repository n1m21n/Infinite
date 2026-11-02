#include "CliSetup.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"
#include "platform/AppPaths.h"
#include "platform/Platform.h"

namespace CliSetup
{
   Os CurrentOs()
   {
#if defined(_WIN32)
      return Os::kWindows;
#elif defined(__APPLE__)
      return Os::kMac;
#else
      return Os::kLinux;
#endif
   }

   namespace
   {
      std::string Replace(std::string s, const std::string& from, const std::string& to)
      {
         for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
            s.replace(at, from.size(), to);
         return s;
      }

      std::string ShQuote(const std::string& s)
      {
         std::string out = "'";
         for (char c : s)
         {
            if (c == '\'')
               out += "'\\''";
            else
               out += c;
         }
         return out + "'";
      }

      std::string PsQuote(const std::string& s)
      {
         std::string out = "'";
         for (char c : s)
         {
            if (c == '\'')
               out += "''";
            else
               out += c;
         }
         return out + "'";
      }

      const char* const kStepIds[kStepCount] = { "path", "headless", "node", "mcp", "agent" };
      const char* const kStepTitles[kStepCount] = {
         "Put the infinite command on your PATH",
         "Check the headless engine",
         "Check Node.js (runs the MCP server)",
         "Fetch the Infinite MCP server",
         "Register the MCP server with your coding agent",
      };
   }

   const std::vector<Agent>& Agents()
   {
      static const std::vector<Agent> kAgents = {
         { "claude", "Claude Code", "claude",
           "curl -fsSL https://claude.ai/install.sh | bash",
           "irm https://claude.ai/install.ps1 | iex",
           "claude",
           "claude mcp get infinite",
           "claude mcp add --scope user -e INFINITE_BIN={EXE} infinite -- npx -y infinite-engine serve",
           "claude mcp remove infinite --scope user",
           "Claude Code asks you to log in the first time it starts. Infinite never sees or stores the login." },
      };
      return kAgents;
   }

   const Agent* FindAgent(const std::string& id)
   {
      for (const Agent& a : Agents())
         if (id == a.id)
            return &a;
      return nullptr;
   }

   int Status::GreenCount() const
   {
      int n = 0;
      for (const StepStatus& s : steps)
         n += s.Green() ? 1 : 0;
      return n;
   }

   int Status::FailedCount() const
   {
      int n = 0;
      for (const StepStatus& s : steps)
         n += s.state == StepState::kFailed ? 1 : 0;
      return n;
   }

   Status ParseStatus(const std::string& text, long long now)
   {
      Status st;
      for (int i = 0; i < kStepCount; i++)
      {
         StepStatus s;
         s.id = kStepIds[i];
         s.title = kStepTitles[i];
         st.steps.push_back(s);
      }
      if (text.empty())
         return st;
      nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
      if (!j.is_object() || j.value("version", 0) != 1 || !j.contains("steps") || !j["steps"].is_array())
         return st;
      st.present = true;
      st.startedAt = j.value("startedAt", 0LL);
      st.finishedAt = j.value("finishedAt", 0LL);
      const bool running = j.value("running", false);
      st.running = running && st.startedAt > 0 && now - st.startedAt < kStaleSeconds;
      for (const nlohmann::json& e : j["steps"])
      {
         if (!e.is_object())
            continue;
         const std::string id = e.value("id", std::string());
         const std::string state = e.value("state", std::string());
         for (StepStatus& s : st.steps)
         {
            if (id != s.id)
               continue;
            s.detail = e.value("detail", std::string());
            if (state == "ok") s.state = StepState::kOk;
            else if (state == "skipped") s.state = StepState::kSkipped;
            else if (state == "failed") s.state = StepState::kFailed;
            else if (state == "waiting") s.state = StepState::kWaiting;
            else if (state == "running") s.state = running && st.running ? StepState::kRunning : StepState::kPending;
         }
      }
      return st;
   }

   Paths PathsFor(const std::string& appSupportDir, Os os)
   {
      Paths p;
      p.dir = appSupportDir + "/cli-setup";
      p.script = p.dir + (os == Os::kWindows ? "/setup.ps1" : "/setup.sh");
      p.status = p.dir + "/status.json";
      p.config = p.dir + "/config.json";
      return p;
   }

   // ------------------------------------------------------------------------------------------------
   // Scripts. The body is fixed text; only the variable block at the top changes.

   namespace
   {
      const char* const kShBody = R"SH(
# ---- fixed part -------------------------------------------------------------------------------
OS=$(uname -s)
STARTED=$(date +%s)
STEPS="path headless node mcp agent"
for s in $STEPS; do eval "ST_$s=pending; DT_$s="; done
FAILED=0

clean() { printf '%s' "$1" | tr -d '"\\\n\r' | cut -c1-200; }
write_status() {
  fin=0; [ "$1" = false ] && fin=$(date +%s)
  {
    printf '{"version":1,"os":"%s","agent":"%s","exe":"%s","running":%s,"startedAt":%s,"finishedAt":%s,"steps":[' \
      "$(clean "$OS")" "$(clean "$AGENT_ID")" "$(clean "$EXE")" "$1" "$STARTED" "$fin"
    sep=""
    for s in $STEPS; do
      eval "st=\$ST_$s; dt=\$DT_$s"
      printf '%s{"id":"%s","state":"%s","detail":"%s"}' "$sep" "$s" "$st" "$(clean "$dt")"
      sep=","
    done
    printf ']}\n'
  } > "$STATUS.tmp" && mv "$STATUS.tmp" "$STATUS"
}
begin() { ID=$2; printf '\nStep %s/5: %s\n' "$1" "$3"; eval "ST_$ID=running"; write_status true; }
finish() {
  eval "ST_$ID=\$1; DT_$ID=\$2"
  printf '   %s: %s\n' "$1" "$2"
  [ "$1" = failed ] && FAILED=$((FAILED + 1))
  write_status true
}
pkg_install() { # apt names, dnf names, pacman names, brew name
  case "$OS" in
    Darwin) command -v brew >/dev/null 2>&1 || return 1; printf '   running: brew install %s\n' "$4"; brew install $4 ;;
    Linux)
      if command -v apt-get >/dev/null 2>&1; then printf '   running: sudo apt-get install -y %s\n' "$1"; sudo apt-get install -y $1
      elif command -v dnf >/dev/null 2>&1; then printf '   running: sudo dnf install -y %s\n' "$2"; sudo dnf install -y $2
      elif command -v pacman >/dev/null 2>&1; then printf '   running: sudo pacman -S --noconfirm %s\n' "$3"; sudo pacman -S --noconfirm $3
      else return 1; fi ;;
    *) return 1 ;;
  esac
}
node_ok() {
  command -v node >/dev/null 2>&1 && command -v npx >/dev/null 2>&1 &&
    [ "$(node -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0)" -ge 18 ]
}
mkdir -p "$(dirname "$STATUS")"
write_status true
printf 'Infinite: command line and agent setup. Safe to run again.\n'

# 1. the infinite command on PATH
begin 1 path "Put the infinite command on your PATH"
BIN="$HOME/.local/bin"; SHIM="$BIN/infinite"
WANT=$(printf '#!/bin/sh\nexec "%s" --cli "$@"\n' "$EXE")
SHIM_NEW=0
if ! { [ -x "$SHIM" ] && [ "$(cat "$SHIM")" = "$WANT" ]; }; then
  SHIM_NEW=1
  { mkdir -p "$BIN" && printf '%s\n' "$WANT" > "$SHIM" && chmod 755 "$SHIM"; } 2>/dev/null || SHIM_NEW=err
fi
case "$SHELL" in
  */zsh) RC="$HOME/.zshrc" ;;
  */bash) if [ "$OS" = Darwin ]; then RC="$HOME/.bash_profile"; else RC="$HOME/.bashrc"; fi ;;
  *) RC="$HOME/.profile" ;;
esac
ON_PATH=0; case ":$PATH:" in *":$BIN:"*) ON_PATH=1 ;; esac
IN_RC=0; grep -qs '# >>> infinite cli >>>' "$RC" && IN_RC=1
if [ "$SHIM_NEW" = err ]; then
  finish failed "could not write $SHIM"
elif [ $ON_PATH = 1 ] || [ $IN_RC = 1 ]; then
  if [ "$SHIM_NEW" = 1 ]; then finish ok "updated $SHIM"; else finish skipped "$SHIM already set up"; fi
else
  if printf '\n# >>> infinite cli >>>\nexport PATH="$HOME/.local/bin:$PATH"\n# <<< infinite cli <<<\n' >> "$RC" 2>/dev/null; then
    finish ok "added $SHIM and a PATH line in $RC. Open a new terminal tab to use it"
  else
    finish failed "could not write $RC"
  fi
fi

# 2. the headless engine
begin 2 headless "Check the headless engine"
if ! VER=$("$EXE" --version 2>&1); then
  finish failed "$EXE --version did not run"
else
  VER=$(printf '%s' "$VER" | head -n 1)
  if [ "$OS" = Linux ] && [ -z "${DISPLAY:-}" ] && ! command -v xvfb-run >/dev/null 2>&1; then
    if pkg_install xvfb xorg-x11-server-Xvfb xorg-server-xvfb xvfb && command -v xvfb-run >/dev/null 2>&1; then
      finish ok "$VER; installed xvfb for renders without a display"
    else
      finish failed "renders without a display need xvfb-run. Install the xvfb package"
    fi
  else
    finish skipped "$VER"
  fi
fi

# 3. Node.js
begin 3 node "Check Node.js (runs the MCP server)"
if node_ok; then
  finish skipped "node $(node -v)"
elif pkg_install "nodejs npm" "nodejs npm" "nodejs npm" node && node_ok; then
  finish ok "installed node $(node -v)"
else
  finish failed "Node.js 18 or newer is needed. Install it from https://nodejs.org, then run this again"
fi

# 4. the MCP server
begin 4 mcp "Fetch the Infinite MCP server"
if ! node_ok; then
  finish failed "needs Node.js (step 3)"
else
  OUT=$(INFINITE_BIN="$EXE" npx -y infinite-engine status 2>&1)
  if printf '%s' "$OUT" | grep -q 'infinite program: ' && ! printf '%s' "$OUT" | grep -q 'not found'; then
    finish ok "infinite-engine finds this app"
  else
    finish failed "$(printf '%s' "$OUT" | tail -n 1)"
  fi
fi

# 5. the coding agent
begin 5 agent "Register the MCP server with $AGENT_NAME"
export PATH="$HOME/.local/bin:$HOME/.claude/local:/opt/homebrew/bin:/usr/local/bin:$PATH"
if ! command -v "$AGENT_BIN" >/dev/null 2>&1; then
  finish waiting "$AGENT_NAME is not installed. Press Install in Settings, then set up again"
elif mcp_check_out=$(mcp_check 2>/dev/null) && printf '%s' "$mcp_check_out" | grep -qF "$EXE"; then
  finish skipped "already registered"
else
  mcp_remove >/dev/null 2>&1
  if mcp_add; then finish ok "registered with $AGENT_NAME"; else finish failed "$AGENT_NAME refused the registration"; fi
fi

write_status false
printf '\nDone. %s step(s) need attention.\n' "$FAILED"
[ "$FAILED" = 0 ]
)SH";

      const char* const kPsBody = R"PS(
# ---- fixed part -------------------------------------------------------------------------------
$Started = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$Steps = [ordered]@{ path = @('pending', ''); headless = @('pending', ''); node = @('pending', ''); mcp = @('pending', ''); agent = @('pending', '') }
$Failed = 0
$Id = ''
function Save([bool]$Running) {
  $fin = 0; if (-not $Running) { $fin = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
  $list = @(); foreach ($k in $Steps.Keys) { $d = [string]$Steps[$k][1]; if ($d.Length -gt 200) { $d = $d.Substring(0, 200) }; $list += [ordered]@{ id = $k; state = $Steps[$k][0]; detail = $d } }
  $obj = [ordered]@{ version = 1; os = 'windows'; agent = $AgentId; exe = $Exe; running = $Running; startedAt = $Started; finishedAt = $fin; steps = $list }
  $dir = Split-Path $Status; if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
  ($obj | ConvertTo-Json -Depth 4) | Set-Content -Encoding UTF8 "$Status.tmp"; Move-Item -Force "$Status.tmp" $Status
}
function Begin([int]$n, [string]$id, [string]$title) { $script:Id = $id; Write-Host ""; Write-Host "Step ${n}/5: $title"; $Steps[$id][0] = 'running'; Save $true }
function Finish([string]$state, [string]$detail) {
  $Steps[$Id][0] = $state; $Steps[$Id][1] = $detail; Write-Host "   ${state}: $detail"
  if ($state -eq 'failed') { $script:Failed++ }
  Save $true
}
function NodeOk { $n = Get-Command node -ErrorAction SilentlyContinue; $x = Get-Command npx -ErrorAction SilentlyContinue; if (-not $n -or -not $x) { return $false }; return ([int](& node -p 'process.versions.node.split(".")[0]')) -ge 18 }
function RefreshPath { $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User') }
Save $true
Write-Host 'Infinite: command line and agent setup. Safe to run again.'

# 1. the infinite command on PATH
Begin 1 'path' 'Put the infinite command on your PATH'
$Bin = Join-Path $env:LOCALAPPDATA 'Infinite\bin'
$Front = [IO.Path]::ChangeExtension($Exe, '.com'); if (-not (Test-Path $Front)) { $Front = $Exe }
$Want = "@`"$Front`" --cli %*`r`n"
$Shim = Join-Path $Bin 'infinite.cmd'
try {
  $new = $true; if ((Test-Path $Shim) -and ([IO.File]::ReadAllText($Shim) -eq $Want)) { $new = $false }
  if ($new) { New-Item -ItemType Directory -Force $Bin | Out-Null; [IO.File]::WriteAllText($Shim, $Want) }
  $user = [Environment]::GetEnvironmentVariable('Path', 'User'); if (-not $user) { $user = '' }
  if (($user -split ';') -contains $Bin) { if ($new) { Finish 'ok' "updated $Shim" } else { Finish 'skipped' "$Shim already set up" } }
  else { [Environment]::SetEnvironmentVariable('Path', ($user.TrimEnd(';') + ';' + $Bin), 'User'); Finish 'ok' "added $Bin to your PATH. Open a new terminal tab to use it" }
} catch { Finish 'failed' "$_" }

# 2. the headless engine
Begin 2 'headless' 'Check the headless engine'
try { $ver = (& $Exe --version 2>&1 | Select-Object -First 1); if ($LASTEXITCODE -eq 0) { Finish 'skipped' "$ver" } else { Finish 'failed' "$Exe --version did not run" } } catch { Finish 'failed' "$_" }

# 3. Node.js
Begin 3 'node' 'Check Node.js (runs the MCP server)'
if (NodeOk) { Finish 'skipped' "node $(& node -v)" }
else {
  if (Get-Command winget -ErrorAction SilentlyContinue) { Write-Host '   running: winget install OpenJS.NodeJS.LTS'; winget install --id OpenJS.NodeJS.LTS -e --accept-source-agreements --accept-package-agreements; RefreshPath }
  if (NodeOk) { Finish 'ok' "installed node $(& node -v)" } else { Finish 'failed' 'Node.js 18 or newer is needed. Install it from https://nodejs.org, then run this again' }
}

# 4. the MCP server
Begin 4 'mcp' 'Fetch the Infinite MCP server'
if (-not (NodeOk)) { Finish 'failed' 'needs Node.js (step 3)' }
else {
  $env:INFINITE_BIN = $Exe
  $out = (& npx -y infinite-engine status 2>&1 | Out-String)
  if (($out -match 'infinite program: ') -and ($out -notmatch 'not found')) { Finish 'ok' 'infinite-engine finds this app' }
  else { Finish 'failed' (($out.Trim() -split "`n")[-1]) }
}

# 5. the coding agent
Begin 5 'agent' "Register the MCP server with $AgentName"
$env:Path = "$env:USERPROFILE\.local\bin;$env:Path"
if (-not (Get-Command $AgentBin -ErrorAction SilentlyContinue)) { Finish 'waiting' "$AgentName is not installed. Press Install in Settings, then set up again" }
else {
  $cur = (McpCheck 2>$null | Out-String)
  if ($cur.Contains($Exe)) { Finish 'skipped' 'already registered' }
  else { McpRemove 2>$null | Out-Null; McpAdd; if ($LASTEXITCODE -eq 0) { Finish 'ok' "registered with $AgentName" } else { Finish 'failed' "$AgentName refused the registration" } }
}

Save $false
Write-Host ""; Write-Host "Done. $Failed step(s) need attention."
if ($Failed -gt 0) { exit 1 }
)PS";
   }

   std::string BuildScript(Os os, const std::string& exe, const Agent& agent, const std::string& statusPath)
   {
      std::ostringstream o;
      if (os == Os::kWindows)
      {
         o << "# Infinite: command line and agent setup. Written by Infinite; safe to run again.\n";
         o << "$Exe = " << PsQuote(exe) << "\n$Status = " << PsQuote(statusPath) << "\n";
         o << "$AgentId = " << PsQuote(agent.id) << "\n$AgentName = " << PsQuote(agent.name) << "\n$AgentBin = " << PsQuote(agent.binary) << "\n";
         o << "function McpCheck { " << Replace(agent.mcpCheck, "{EXE}", "$Exe") << " }\n";
         o << "function McpAdd { " << Replace(agent.mcpAdd, "{EXE}", "$Exe") << " }\n";
         o << "function McpRemove { " << Replace(agent.mcpRemove, "{EXE}", "$Exe") << " }\n";
         o << kPsBody;
         return o.str();
      }
      o << "#!/bin/sh\n# Infinite: command line and agent setup. Written by Infinite; safe to run again.\n";
      o << "EXE=" << ShQuote(exe) << "\nSTATUS=" << ShQuote(statusPath) << "\n";
      o << "AGENT_ID=" << ShQuote(agent.id) << "\nAGENT_NAME=" << ShQuote(agent.name) << "\nAGENT_BIN=" << ShQuote(agent.binary) << "\n";
      o << "mcp_check() { " << Replace(agent.mcpCheck, "{EXE}", "\"$EXE\"") << "; }\n";
      o << "mcp_add() { " << Replace(agent.mcpAdd, "{EXE}", "\"$EXE\"") << "; }\n";
      o << "mcp_remove() { " << Replace(agent.mcpRemove, "{EXE}", "\"$EXE\"") << "; }\n";
      o << kShBody;
      return o.str();
   }

   std::string SetupLine(Os os, const std::string& scriptPath)
   {
      if (os == Os::kWindows)
         return "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + scriptPath + "\"";
      return "sh " + ShQuote(scriptPath);
   }

   std::string InstallAgentLine(Os os, const Agent& agent)
   {
      return os == Os::kWindows ? agent.installWin : agent.installUnix;
   }

   std::string StartAgentLine(Os os, const std::string& exe, const Agent& agent)
   {
      if (os == Os::kWindows)
         return "$env:Path = \"$env:USERPROFILE\\.local\\bin;$env:Path\"; $Exe = " + PsQuote(exe) + "; if (-not ((" +
                Replace(agent.mcpCheck, "{EXE}", "$Exe") + " 2>$null | Out-String).Contains($Exe))) { " +
                Replace(agent.mcpRemove, "{EXE}", "$Exe") + " 2>$null | Out-Null; " + Replace(agent.mcpAdd, "{EXE}", "$Exe") +
                " }; " + agent.launch;
      return std::string("export PATH=\"$HOME/.local/bin:$HOME/.claude/local:/opt/homebrew/bin:/usr/local/bin:$PATH\"; ") +
             agent.mcpCheck + " >/dev/null 2>&1 || { " + Replace(agent.mcpRemove, "{EXE}", ShQuote(exe)) + " >/dev/null 2>&1; " +
             Replace(agent.mcpAdd, "{EXE}", ShQuote(exe)) + "; }; " + agent.launch;
   }

   bool AgentInstalled(Os os, const Agent& agent, const std::string& home, const std::string& pathEnv)
   {
      std::vector<std::string> dirs;
      const char sep = os == Os::kWindows ? ';' : ':';
      std::stringstream ss(pathEnv);
      for (std::string d; std::getline(ss, d, sep);)
         if (!d.empty())
            dirs.push_back(d);
      if (!home.empty())
      {
         dirs.push_back(home + "/.local/bin");
         dirs.push_back(home + "/.claude/local");
      }
      if (os == Os::kMac)
      {
         dirs.push_back("/opt/homebrew/bin");
         dirs.push_back("/usr/local/bin");
      }
      std::vector<std::string> names = { agent.binary };
      if (os == Os::kWindows)
         names = { std::string(agent.binary) + ".exe", std::string(agent.binary) + ".cmd" };
      for (const std::string& d : dirs)
         for (const std::string& n : names)
         {
            std::error_code ec;
            if (std::filesystem::is_regular_file(AppPaths::FsPath(d + "/" + n), ec))
               return true;
         }
      return false;
   }

   Config LoadConfig(const std::string& file)
   {
      Config c;
      std::ifstream f(AppPaths::FsPath(file));
      if (!f.good())
         return c;
      nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
      if (!j.is_object())
         return c;
      const std::string agent = j.value("agent", std::string());
      if (FindAgent(agent) != nullptr)
         c.agent = agent;
      c.autoStart = j.value("autoStart", false);
      return c;
   }

   void SaveConfig(const std::string& file, const Config& c)
   {
      std::error_code ec;
      std::filesystem::create_directories(AppPaths::FsPath(file).parent_path(), ec);
      std::ofstream(AppPaths::FsPath(file)) << nlohmann::json{ { "agent", c.agent }, { "autoStart", c.autoStart } }.dump(2) << "\n";
   }

   View Describe(Os os, const Status& status, const Agent& agent, bool agentInstalled, const std::string& exe, const Paths& paths)
   {
      View v;
      v.steps = status.steps;
      v.agentName = agent.name;
      v.agentInstalled = agentInstalled;
      v.showInstall = !agentInstalled;
      v.startEnabled = agentInstalled;
      v.loginHint = agent.loginHint;
      v.setupLine = SetupLine(os, paths.script);
      v.installLine = InstallAgentLine(os, agent);
      v.startLine = StartAgentLine(os, exe, agent);
      v.setupEnabled = !status.running;
      if (status.running)
         v.setupLabel = "Setting up...";
      else
         v.setupLabel = status.present ? "Set up again" : "Set up everything";
      const int green = status.GreenCount(), failed = status.FailedCount();
      if (!status.present)
         v.summary = "Not set up yet.";
      else if (status.running)
         v.summary = std::to_string(green) + " of " + std::to_string(kStepCount) + " done so far.";
      else if (failed > 0)
         v.summary = std::to_string(failed) + (failed == 1 ? " step needs attention." : " steps need attention.");
      else if (green == kStepCount)
         v.summary = "Everything is set up.";
      else
         v.summary = std::to_string(green) + " of " + std::to_string(kStepCount) + " done. Press Set up again after installing " + agent.name + ".";
      return v;
   }

   bool Perform(Action action, Os os, const Agent& agent, const std::string& exe, const Paths& paths, const Runner& run, std::string* error)
   {
      auto fail = [&](const std::string& why) { if (error != nullptr) *error = why; return false; };
      if (!run)
         return fail("no terminal");
      switch (action)
      {
      case Action::kSetup:
      {
         if (exe.empty())
            return fail("Infinite could not find its own executable");
         std::error_code ec;
         std::filesystem::create_directories(AppPaths::FsPath(paths.dir), ec);
         std::ofstream out(AppPaths::FsPath(paths.script), std::ios::binary);
         out << BuildScript(os, exe, agent, paths.status);
         out.close();
         if (!out.good() && !std::filesystem::exists(AppPaths::FsPath(paths.script)))
            return fail("could not write " + paths.script);
         run("Infinite setup", SetupLine(os, paths.script));
         return true;
      }
      case Action::kInstallAgent:
         run(std::string("Install ") + agent.name, InstallAgentLine(os, agent));
         return true;
      case Action::kStartAgent:
         if (exe.empty())
            return fail("Infinite could not find its own executable");
         run(agent.name, StartAgentLine(os, exe, agent));
         return true;
      default:
         return false;
      }
   }

   Config& Prefs()
   {
      static Config c = LoadConfig(PathsFor(AppPaths::AppSupportDir(), CurrentOs()).config);
      return c;
   }

   std::string RealExecutable()
   {
      return Platform::ExecutablePath();
   }
}
