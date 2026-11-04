#include "CliSetup.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <sstream>

#include "json.hpp"
#include "core/AISkillContent.h"
#include "core/AgentDocs.h"
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

      // {EXE} -> how the launcher is spelled in this shell; {TOOL} -> the quoted path of the JSON-registration helper.
      std::string Sub(const std::string& cmd, const std::string& exe, const std::string& tool)
      {
         return Replace(Replace(cmd, "{EXE}", exe), "{TOOL}", tool);
      }

      std::string ShQuote(const std::string& s);
      std::string PsQuote(const std::string& s);

      // The helper script path, quoted for the shell it is typed into.
      std::string ToolRef(Os os, const std::string& dir)
      {
         const std::string path = dir + "/mcp_json.js";
         return os == Os::kWindows ? PsQuote(path) : ShQuote(path);
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
         "Connect your coding agent (MCP server and AI skills)",
      };
   }

   const std::vector<Agent>& Agents()
   {
      static const std::vector<Agent> kAgents = {
         { "claude", "Claude", "claude",
           "curl -fsSL https://claude.ai/install.sh | bash",
           "irm https://claude.ai/install.ps1 | iex",
           "claude",
           "claude mcp get infinite",
           "claude mcp add --scope user infinite -e INFINITE_BIN={EXE} -- npx -y infinite-engine serve",
           "claude mcp remove infinite --scope user",
           ".claude.json",
           ".claude/skills",
           "Claude asks you to log in the first time it starts. Infinite never sees or stores the login." },
         { "codex", "ChatGPT", "codex",
           "npm install -g @openai/codex",
           "npm install -g @openai/codex",
           "codex",
           "codex mcp get infinite",
           "codex mcp add infinite --env INFINITE_BIN={EXE} -- npx -y infinite-engine serve",
           "codex mcp remove infinite",
           ".codex/config.toml",
           ".codex/skills",
           "ChatGPT asks you to sign in with ChatGPT or an API key the first time it starts. Infinite never sees or stores the login." },
         // Antigravity has no `mcp add` command: the helper edits its JSON config (mcp_json.js, written next to the setup script).
         { "antigravity", "Gemini", "agy",
           "curl -fsSL https://antigravity.google/cli/install.sh | bash",
           "irm https://antigravity.google/cli/install.ps1 | iex",
           "agy",
           "node {TOOL} check ~/.gemini/config/mcp_config.json infinite",
           "node {TOOL} add ~/.gemini/config/mcp_config.json infinite {EXE}",
           "node {TOOL} remove ~/.gemini/config/mcp_config.json infinite",
           ".gemini/config/mcp_config.json",
           ".gemini/skills",
           "Gemini asks you to sign in with Google the first time it starts. Infinite never sees or stores the login." },
      };
      return kAgents;
   }

   const std::vector<Skill>& Skills()
   {
      static const std::vector<Skill> kSkills = {
         { "infinite-patch-authoring", AISkillContent::kPatchAuthoringMarkdown },
         { "infinite-field-language", AISkillContent::kFieldLanguageMarkdown },
         { "infinite-expression-globals", AISkillContent::kExpressionGlobalsMarkdown },
         { "infinite-live-session", AISkillContent::kLiveSessionMarkdown },
         { "infinite-map", AgentDocs::kSkillMap },
         { "infinite-visual-craft", AgentDocs::kSkillVisualCraft },
         { "infinite-sound-craft", AgentDocs::kSkillSoundCraft },
         { "infinite-film-craft", AgentDocs::kSkillFilmCraft },
      };
      return kSkills;
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
      p.record = p.dir + "/exe";
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
for s in $STEPS; do eval ": \"\${ST_$s:=pending}\"; : \"\${DT_$s:=}\""; done
FAILED=0
BIN="$HOME/.local/bin"; APPBIN="$BIN/infinite-app"
RECORD="$(dirname "$STATUS")/exe"
want() { [ -z "$ONLY" ] && return 0; case " $ONLY " in *" $1 "*) return 0 ;; esac; return 1; }

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
node_hint() { # one next step for this machine
  if command -v node >/dev/null 2>&1; then
    printf 'Node.js %s is too old; 18 or newer is needed. Update it, then press Connect again' "$(node -v)"
  elif [ "$(uname -s)" = Darwin ]; then
    printf 'Node.js is needed. Run the macOS installer from https://nodejs.org/en/download, then press Connect again'
  else
    printf 'Node.js 18+ is needed. Install it with your package manager (zypper install nodejs, apk add nodejs npm) or from https://nodejs.org/en/download, then press Connect again'
  fi
}
node_ok() {
  command -v node >/dev/null 2>&1 && command -v npx >/dev/null 2>&1 &&
    [ "$(node -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0)" -ge 18 ]
}
# The two launchers never name this copy of Infinite. They read the path Infinite records each time it
# starts, then fall back to the usual install folders, so moving or updating the app does not break them.
launcher() { # $1 = "--cli " for the infinite command, empty for infinite-app
  printf '#!/bin/sh\nP=$(cat "%s" 2>/dev/null)\n[ -x "$P" ] || for P in /Applications/Infinite.app/Contents/MacOS/Infinite "$HOME/Applications/Infinite.app/Contents/MacOS/Infinite" /opt/Infinite/Infinite; do [ -x "$P" ] && break; done\n[ -x "$P" ] || { echo "infinite: Infinite is not installed or was moved. Open it once, then try again." >&2; exit 127; }\nexec "$P" %s"$@"\n' "$RECORD" "$1"
}
mkdir -p "$(dirname "$STATUS")"
write_status true
printf 'Infinite: command line and agent setup. Safe to run again.\n'

# 1. the infinite command on PATH
if want path; then
begin 1 path "Put the infinite command on your PATH"
printf '%s\n' "$EXE" > "$RECORD" 2>/dev/null
SHIM_NEW=0; SHIM_ERR=0
for pair in "infinite:--cli " "infinite-app:"; do
  f="$BIN/${pair%%:*}"; text=$(launcher "${pair#*:}")
  if ! { [ -x "$f" ] && [ "$(cat "$f")" = "$text" ]; }; then
    SHIM_NEW=1
    { mkdir -p "$BIN" && printf '%s\n' "$text" > "$f" && chmod 755 "$f"; } 2>/dev/null || SHIM_ERR=1
  fi
done
case "$SHELL" in
  */zsh) RC="$HOME/.zshrc" ;;
  */bash) if [ "$OS" = Darwin ]; then RC="$HOME/.bash_profile"; else RC="$HOME/.bashrc"; fi ;;
  *) RC="$HOME/.profile" ;;
esac
ON_PATH=0; case ":$PATH:" in *":$BIN:"*) ON_PATH=1 ;; esac
IN_RC=0; grep -qs '# >>> infinite cli >>>' "$RC" && IN_RC=1
if [ $SHIM_ERR = 1 ]; then
  finish failed "could not write ~/.local/bin/infinite"
elif [ $ON_PATH = 1 ] || [ $IN_RC = 1 ]; then
  if [ $SHIM_NEW = 1 ]; then finish ok "updated ~/.local/bin/infinite"; else finish skipped "~/.local/bin/infinite already set up"; fi
else
  if printf '\n# >>> infinite cli >>>\nexport PATH="$HOME/.local/bin:$PATH"\n# <<< infinite cli <<<\n' >> "$RC" 2>/dev/null; then
    finish ok "added ~/.local/bin/infinite and a PATH line in $(basename "$RC"). Open a new terminal tab to use it"
  else
    finish failed "could not write $(basename "$RC")"
  fi
fi
fi

# 2. the headless engine
if want headless; then
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
fi

# 3. Node.js
if want node; then
begin 3 node "Check Node.js (runs the MCP server)"
if node_ok; then
  finish skipped "node $(node -v)"
elif pkg_install "nodejs npm" "nodejs npm" "nodejs npm" node && node_ok; then
  finish ok "installed node $(node -v)"
else
  finish failed "$(node_hint)"
fi
fi

# 4. the MCP server
if want mcp; then
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
fi

# 5. the coding agent: the MCP registration names the launcher, not this copy of the app; the AI skill
#    files are copied into the agent's skills folder (a changed file is replaced, an equal one is left).
skills_sync() {
  SK_N=0
  [ -n "$SKILLS_DIR" ] || return 0
  for d in "$SKILLS_SRC"/*/; do
    [ -f "$d/SKILL.md" ] || continue
    n=$(basename "$d")
    if ! cmp -s "$d/SKILL.md" "$SKILLS_DIR/$n/SKILL.md"; then
      mkdir -p "$SKILLS_DIR/$n" && cp "$d/SKILL.md" "$SKILLS_DIR/$n/SKILL.md" || return 1
      SK_N=$((SK_N + 1))
    fi
  done
}
if want agent; then
begin 5 agent "Connect $AGENT_NAME (MCP server and AI skills)"
export PATH="$HOME/.local/bin:$HOME/.claude/local:/opt/homebrew/bin:/usr/local/bin:$PATH"
if ! command -v "$AGENT_BIN" >/dev/null 2>&1 && [ -n "$AGENT_INSTALL" ]; then
  printf '   running: %s\n' "$AGENT_INSTALL"
  sh -c "$AGENT_INSTALL"
  hash -r 2>/dev/null
fi
if ! command -v "$AGENT_BIN" >/dev/null 2>&1; then
  if [ -n "$AGENT_INSTALL" ]; then finish failed "$AGENT_NAME did not install. The lines above say why"
  else finish waiting "$AGENT_NAME is not installed. Press Install in Settings, then connect again"; fi
elif [ ! -x "$APPBIN" ]; then
  finish failed "run step 1 first: $APPBIN is missing"
else
  REG=skipped
  if mcp_check_out=$(mcp_check 2>/dev/null) && printf '%s' "$mcp_check_out" | grep -qF "$APPBIN"; then :
  else
    mcp_remove >/dev/null 2>&1
    if mcp_add; then REG=ok; else REG=failed; fi
  fi
  if ! skills_sync; then
    finish failed "could not write the AI skills to $SKILLS_DIR"
  elif [ "$REG" = failed ]; then
    finish failed "$AGENT_NAME refused the registration"
  elif [ "$REG" = ok ] || [ "$SK_N" -gt 0 ]; then
    finish ok "registered with $AGENT_NAME; $SK_N AI skill file(s) installed"
  else
    finish skipped "already registered; AI skills up to date"
  fi
fi
fi

write_status false
printf '\nDone. %s step(s) need attention.\n' "$FAILED"
[ "$FAILED" = 0 ]
)SH";

      const char* const kPsBody = R"PS(
# ---- fixed part -------------------------------------------------------------------------------
$Started = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$Steps = [ordered]@{ path = @('pending', ''); headless = @('pending', ''); node = @('pending', ''); mcp = @('pending', ''); agent = @('pending', '') }
foreach ($k in @($Steps.Keys)) { if ($Prior.ContainsKey($k)) { $Steps[$k] = @($Prior[$k][0], $Prior[$k][1]) } }
$Failed = 0
$Id = ''
$Bin = Join-Path $env:LOCALAPPDATA 'Infinite\bin'
$AppBin = Join-Path $Bin 'infinite-app.cmd'
$Record = Join-Path (Split-Path $Status) 'exe'
function Want([string]$id) { return ($Only.Count -eq 0) -or ($Only -contains $id) }
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
# The launchers never name this copy of Infinite: they read the path Infinite records at each start.
function Launcher([string]$arg) { return "@echo off`r`nset /p P=<`"$Record`"`r`nif not exist `"%P%`" set P=C:\Program Files\Infinite\Infinite.exe`r`nif not exist `"%P%`" (echo infinite: Infinite is not installed or was moved. Open it once, then try again. 1>&2 & exit /b 127)`r`n`"%P%`" $arg%*`r`n" }
Save $true
Write-Host 'Infinite: command line and agent setup. Safe to run again.'

# 1. the infinite command on PATH
if (Want 'path') {
Begin 1 'path' 'Put the infinite command on your PATH'
try {
  New-Item -ItemType Directory -Force $Bin, (Split-Path $Record) | Out-Null
  $Front = [IO.Path]::ChangeExtension($Exe, '.com'); if (-not (Test-Path $Front)) { $Front = $Exe }
  [IO.File]::WriteAllText($Record, "$Front`r`n")
  $new = $false
  foreach ($pair in @(@('infinite.cmd', '--cli '), @('infinite-app.cmd', ''))) {
    $f = Join-Path $Bin $pair[0]; $text = Launcher $pair[1]
    if (-not ((Test-Path $f) -and ([IO.File]::ReadAllText($f) -eq $text))) { [IO.File]::WriteAllText($f, $text); $new = $true }
  }
  $user = [Environment]::GetEnvironmentVariable('Path', 'User'); if (-not $user) { $user = '' }
  if (($user -split ';') -contains $Bin) { if ($new) { Finish 'ok' "updated $Bin" } else { Finish 'skipped' "$Bin already set up" } }
  else { [Environment]::SetEnvironmentVariable('Path', ($user.TrimEnd(';') + ';' + $Bin), 'User'); Finish 'ok' "added $Bin to your PATH. Open a new terminal tab to use it" }
} catch { Finish 'failed' "$_" }
}

# 2. the headless engine
if (Want 'headless') {
Begin 2 'headless' 'Check the headless engine'
try { $ver = (& $Exe --version 2>&1 | Select-Object -First 1); if ($LASTEXITCODE -eq 0) { Finish 'skipped' "$ver" } else { Finish 'failed' "$Exe --version did not run" } } catch { Finish 'failed' "$_" }
}

# 3. Node.js
if (Want 'node') {
Begin 3 'node' 'Check Node.js (runs the MCP server)'
if (NodeOk) { Finish 'skipped' "node $(& node -v)" }
else {
  if (Get-Command winget -ErrorAction SilentlyContinue) { Write-Host '   running: winget install OpenJS.NodeJS.LTS'; winget install --id OpenJS.NodeJS.LTS -e --accept-source-agreements --accept-package-agreements; RefreshPath }
  if (NodeOk) { Finish 'ok' "installed node $(& node -v)" }
  elseif (Get-Command node -ErrorAction SilentlyContinue) { Finish 'failed' "Node.js $(& node -v) is too old; 18 or newer is needed. Update it, then press Connect again" }
  else { Finish 'failed' 'Node.js is needed. Run the Windows installer (.msi) from https://nodejs.org/en/download, then press Connect again' }
}
}

# 4. the MCP server
if (Want 'mcp') {
Begin 4 'mcp' 'Fetch the Infinite MCP server'
if (-not (NodeOk)) { Finish 'failed' 'needs Node.js (step 3)' }
else {
  $env:INFINITE_BIN = $Exe
  $out = (& npx -y infinite-engine status 2>&1 | Out-String)
  if (($out -match 'infinite program: ') -and ($out -notmatch 'not found')) { Finish 'ok' 'infinite-engine finds this app' }
  else { Finish 'failed' (($out.Trim() -split "`n")[-1]) }
}
}

# 5. the coding agent: the MCP registration names the launcher; the AI skill files are copied into the agent's skills folder.
function SkillsSync {
  $script:SkN = 0
  if (-not $SkillsDir) { return $true }
  foreach ($d in (Get-ChildItem -Directory $SkillsSrc -ErrorAction SilentlyContinue)) {
    $src = Join-Path $d.FullName 'SKILL.md'; if (-not (Test-Path $src)) { continue }
    $dst = Join-Path (Join-Path $SkillsDir $d.Name) 'SKILL.md'
    if (-not ((Test-Path $dst) -and ((Get-FileHash $src).Hash -eq (Get-FileHash $dst).Hash))) {
      New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null; Copy-Item -Force $src $dst; $script:SkN++
    }
  }
  return $true
}
if (Want 'agent') {
Begin 5 'agent' "Connect $AgentName (MCP server and AI skills)"
$env:Path = "$env:USERPROFILE\.local\bin;$env:Path"
if (-not (Get-Command $AgentBin -ErrorAction SilentlyContinue) -and $AgentInstall) { Write-Host "   running: $AgentInstall"; Invoke-Expression $AgentInstall; RefreshPath; $env:Path = "$env:USERPROFILE\.local\bin;$env:Path" }
if (-not (Get-Command $AgentBin -ErrorAction SilentlyContinue)) { if ($AgentInstall) { Finish 'failed' "$AgentName did not install. The lines above say why" } else { Finish 'waiting' "$AgentName is not installed. Press Install in Settings, then connect again" } }
elseif (-not (Test-Path $AppBin)) { Finish 'failed' "run step 1 first: $AppBin is missing" }
else {
  $reg = 'skipped'
  $cur = (McpCheck 2>$null | Out-String)
  if (-not $cur.Contains($AppBin)) { McpRemove 2>$null | Out-Null; McpAdd; if ($LASTEXITCODE -eq 0) { $reg = 'ok' } else { $reg = 'failed' } }
  try { [void](SkillsSync); $skOk = $true } catch { $skOk = $false }
  if (-not $skOk) { Finish 'failed' "could not write the AI skills to $SkillsDir" }
  elseif ($reg -eq 'failed') { Finish 'failed' "$AgentName refused the registration" }
  elseif ($reg -eq 'ok' -or $SkN -gt 0) { Finish 'ok' "registered with $AgentName; $SkN AI skill file(s) installed" }
  else { Finish 'skipped' 'already registered; AI skills up to date' }
}
}

Save $false
Write-Host ""; Write-Host "Done. $Failed step(s) need attention."
if ($Failed -gt 0) { exit 1 }
)PS";
   }

   std::string BuildScript(Os os, const std::string& exe, const Agent& agent, const std::string& statusPath,
                           const std::vector<std::string>& only, const Status* prior, bool installAgent)
   {
      std::ostringstream o;
      const std::string tool = ToolRef(os, statusPath.substr(0, statusPath.find_last_of("/\\") == std::string::npos ? 0 : statusPath.find_last_of("/\\")));
      if (os == Os::kWindows)
      {
         o << "# Infinite: command line and agent setup. Written by Infinite; safe to run again.\n";
         o << "$Exe = " << PsQuote(exe) << "\n$Status = " << PsQuote(statusPath) << "\n";
         o << "$AgentId = " << PsQuote(agent.id) << "\n$AgentName = " << PsQuote(agent.name) << "\n$AgentBin = " << PsQuote(agent.binary) << "\n";
         o << "$AgentInstall = " << PsQuote(installAgent ? agent.installWin : "") << "\n";
         o << "$Only = @(";
         for (size_t i = 0; i < only.size(); i++)
            o << (i ? ", " : "") << PsQuote(only[i]);
         o << ")\n$Prior = @{";
         if (prior != nullptr && !only.empty())
            for (const StepStatus& st : prior->steps)
            {
               const char* name = st.state == StepState::kOk ? "ok" : st.state == StepState::kSkipped ? "skipped" : st.state == StepState::kFailed ? "failed" : st.state == StepState::kWaiting ? "waiting" : "pending";
               o << " " << PsQuote(st.id) << " = @(" << PsQuote(name) << ", " << PsQuote(st.detail) << ");";
            }
         o << " }\n";
         o << "$AppBin = Join-Path (Join-Path $env:LOCALAPPDATA 'Infinite\\bin') 'infinite-app.cmd'\n";
         o << "$SkillsSrc = Join-Path (Split-Path $Status) 'skills'\n$SkillsDir = "
           << (agent.skillsDir[0] != '\0' ? "Join-Path $env:USERPROFILE " + PsQuote(Replace(agent.skillsDir, "/", "\\")) : std::string("''")) << "\n";
         o << "function McpCheck { " << Sub(agent.mcpCheck, "$AppBin", tool) << " }\n";
         o << "function McpAdd { " << Sub(agent.mcpAdd, "$AppBin", tool) << " }\n";
         o << "function McpRemove { " << Sub(agent.mcpRemove, "$AppBin", tool) << " }\n";
         o << kPsBody;
         return o.str();
      }
      o << "#!/bin/sh\n# Infinite: command line and agent setup. Written by Infinite; safe to run again.\n";
      o << "EXE=" << ShQuote(exe) << "\nSTATUS=" << ShQuote(statusPath) << "\n";
      o << "AGENT_ID=" << ShQuote(agent.id) << "\nAGENT_NAME=" << ShQuote(agent.name) << "\nAGENT_BIN=" << ShQuote(agent.binary) << "\n";
      o << "AGENT_INSTALL=" << ShQuote(installAgent ? agent.installUnix : "") << "\n";
      o << "ONLY='";
      for (size_t i = 0; i < only.size(); i++)
         o << (i ? " " : "") << only[i];
      o << "'\n";
      if (prior != nullptr && !only.empty())
         for (const StepStatus& st : prior->steps)
         {
            const char* name = st.state == StepState::kOk ? "ok" : st.state == StepState::kSkipped ? "skipped" : st.state == StepState::kFailed ? "failed" : st.state == StepState::kWaiting ? "waiting" : "pending";
            o << "ST_" << st.id << "=" << name << "; DT_" << st.id << "=" << ShQuote(st.detail) << "\n";
         }
      o << "APPBIN=\"$HOME/.local/bin/infinite-app\"\nSKILLS_SRC=\"$(dirname \"$STATUS\")/skills\"\n";
      o << "SKILLS_DIR=" << (agent.skillsDir[0] != '\0' ? "\"$HOME/" + std::string(agent.skillsDir) + "\"" : std::string("''")) << "\n";
      o << "mcp_check() { " << Sub(agent.mcpCheck, "\"$APPBIN\"", tool) << "; }\n";
      o << "mcp_add() { " << Sub(agent.mcpAdd, "\"$APPBIN\"", tool) << "; }\n";
      o << "mcp_remove() { " << Sub(agent.mcpRemove, "\"$APPBIN\"", tool) << "; }\n";
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
      const std::string tool = ToolRef(os, PathsFor(AppPaths::AppSupportDir(), os).dir);
      if (os == Os::kWindows)
         return "$env:Path = \"$env:USERPROFILE\\.local\\bin;$env:Path\"; $A = Join-Path $env:LOCALAPPDATA 'Infinite\\bin\\infinite-app.cmd'; if (-not (Test-Path $A)) { $A = " +
                PsQuote(exe) + " }; if (-not ((" + Sub(agent.mcpCheck, "$A", tool) + " 2>$null | Out-String).Contains($A))) { " +
                Sub(agent.mcpRemove, "$A", tool) + " 2>$null | Out-Null; " + Sub(agent.mcpAdd, "$A", tool) + " }; " + agent.launch;
      return std::string("export PATH=\"$HOME/.local/bin:$HOME/.claude/local:/opt/homebrew/bin:/usr/local/bin:$PATH\"; A=\"$HOME/.local/bin/infinite-app\"; [ -x \"$A\" ] || A=") +
             ShQuote(exe) + "; " + Sub(agent.mcpCheck, "\"$A\"", tool) + " 2>/dev/null | grep -qF \"$A\" || { " + Sub(agent.mcpRemove, "\"$A\"", tool) +
             " >/dev/null 2>&1; " + Sub(agent.mcpAdd, "\"$A\"", tool) + "; }; " + agent.launch;
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

   bool AgentInstalledHere(const Agent& agent)
   {
      const Os os = CurrentOs();
      const char* home = getenv(os == Os::kWindows ? "USERPROFILE" : "HOME");
      const char* path = getenv("PATH");
      return AgentInstalled(os, agent, home != nullptr ? home : "", path != nullptr ? path : "");
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
      c.welcomed = j.value("welcomed", false);
      if (const std::string face = j.value("face", std::string()); face == "chat" || face == "terminal")
         c.face = face;
      c.workspace = j.value("workspace", std::string());
      c.folderAccess = j.value("folderAccess", false);
      c.introduced = j.value("introduced", false);
      if (j.contains("models") && j["models"].is_object())
         for (auto it = j["models"].begin(); it != j["models"].end(); ++it)
            if (it.value().is_string() && FindAgent(it.key()) != nullptr)
               c.models[it.key()] = it.value().get<std::string>();
      return c;
   }

   void SaveConfig(const std::string& file, const Config& c)
   {
      std::error_code ec;
      std::filesystem::create_directories(AppPaths::FsPath(file).parent_path(), ec);
      std::ofstream(AppPaths::FsPath(file)) << nlohmann::json{ { "agent", c.agent }, { "autoStart", c.autoStart }, { "welcomed", c.welcomed }, { "face", c.face },
                                                                { "workspace", c.workspace }, { "folderAccess", c.folderAccess }, { "introduced", c.introduced },
                                                                { "models", c.models } }.dump(2) << "\n";
   }

   namespace
   {
      bool IsFile(const std::string& p)
      {
         std::error_code ec;
         return !p.empty() && std::filesystem::is_regular_file(AppPaths::FsPath(p), ec);
      }

      std::string ReadAll(const std::string& p)
      {
         std::ifstream f(AppPaths::FsPath(p), std::ios::binary);
         std::stringstream ss;
         ss << f.rdbuf();
         return ss.str();
      }

      std::string FirstLine(std::string t)
      {
         t = t.substr(0, t.find('\n'));
         while (!t.empty() && (t.back() == '\r' || t.back() == ' '))
            t.pop_back();
         return t;
      }

      bool OnPath(Os os, const std::vector<std::string>& names, const std::string& home, const std::string& pathEnv)
      {
         std::vector<std::string> dirs;
         std::stringstream ss(pathEnv);
         for (std::string d; std::getline(ss, d, os == Os::kWindows ? ';' : ':');)
            if (!d.empty())
               dirs.push_back(d);
         if (!home.empty())
            dirs.push_back(home + "/.local/bin");
         if (os == Os::kMac)
         {
            dirs.push_back("/opt/homebrew/bin");
            dirs.push_back("/usr/local/bin");
         }
         if (os == Os::kLinux)
            dirs.push_back("/usr/bin");
         for (const std::string& d : dirs)
            for (const std::string& n : names)
               if (IsFile(d + "/" + n))
                  return true;
         return false;
      }
   }

   bool LauncherResolves(Os os, const std::string& recordFile)
   {
      if (IsFile(FirstLine(ReadAll(recordFile))))
         return true;
      // The same fallbacks the launcher script tries.
      if (os == Os::kMac)
      {
         const char* home = getenv("HOME");
         return IsFile("/Applications/Infinite.app/Contents/MacOS/Infinite") ||
                (home != nullptr && IsFile(std::string(home) + "/Applications/Infinite.app/Contents/MacOS/Infinite"));
      }
      if (os == Os::kLinux)
         return IsFile("/opt/Infinite/Infinite");
      return IsFile("C:/Program Files/Infinite/Infinite.exe");
   }

   HealthReport CheckHealth(const HealthInput& in)
   {
      HealthReport r;
      r.checked = true;
      auto set = [&](int i, Health h, const char* why = "") { r.step[i] = h; r.why[i] = why; };
      const bool win = in.os == Os::kWindows;

      // 0 path: both launchers exist, the recorded app still exists, and the folder is on PATH
      const std::string shim = in.binDir + (win ? "/infinite.cmd" : "/infinite");
      if (!IsFile(shim) || !IsFile(in.binDir + (win ? "/infinite-app.cmd" : "/infinite-app")))
         set(0, Health::kBroken, "the infinite command is missing");
      else if (!LauncherResolves(in.os, in.recordFile))
         set(0, Health::kBroken, "Infinite was moved. Open it from its new place");
      else if (!win)
      {
         bool onPath = false;
         std::stringstream ss(in.pathEnv);
         for (std::string d; std::getline(ss, d, ':');)
            onPath = onPath || d == in.binDir;
         bool inRc = false;
         // A Finder-launched app has a bare PATH, so the shell's own profile is the truth: our marker, or the
         // user's own line that already puts ~/.local/bin on PATH (setup then had nothing to add).
         for (const char* rc : { "/.zshrc", "/.zprofile", "/.zshenv", "/.bash_profile", "/.bashrc", "/.profile" })
         {
            const std::string text = ReadAll(in.home + rc);
            inRc = inRc || text.find("# >>> infinite cli >>>") != std::string::npos || text.find(".local/bin") != std::string::npos;
         }
         if (onPath || inRc)
            set(0, Health::kOk);
         else
            set(0, Health::kBroken, "the PATH line is gone from your shell profile");
      }
      else
         set(0, Health::kOk);

      // 1 headless: the app that is running
      set(1, IsFile(in.exe) ? Health::kOk : Health::kUnknown);

      // 2 and 3 node and the MCP server it fetches
      const bool node = OnPath(in.os, win ? std::vector<std::string>{ "node.exe" } : std::vector<std::string>{ "node" }, in.home, in.pathEnv) &&
                        OnPath(in.os, win ? std::vector<std::string>{ "npx.cmd" } : std::vector<std::string>{ "npx" }, in.home, in.pathEnv);
      set(2, node ? Health::kOk : Health::kBroken, "Node.js was removed");
      set(3, node ? Health::kOk : Health::kUnknown);

      // 4 the agent: installed, and its own config still names the launcher
      if (in.agent == nullptr)
         set(4, Health::kUnknown);
      else if (!OnPath(in.os, win ? std::vector<std::string>{ std::string(in.agent->binary) + ".exe", std::string(in.agent->binary) + ".cmd" } : std::vector<std::string>{ in.agent->binary }, in.home, in.pathEnv) &&
               !IsFile(in.home + "/.claude/local/" + in.agent->binary))
         set(4, Health::kBroken, "the coding agent was uninstalled");
      else if (ReadAll(in.home + "/" + in.agent->configFile).find("infinite-app") == std::string::npos)
         set(4, Health::kBroken, "the MCP registration is gone from the agent's config");
      else
      {
         set(4, Health::kOk);
         if (in.agent->skillsDir[0] != '\0')
            for (const Skill& k : Skills())
               if (ReadAll(in.home + "/" + in.agent->skillsDir + "/" + k.name + "/SKILL.md") != k.content)
                  set(4, Health::kBroken, "the AI skill files are missing or out of date");
      }
      return r;
   }

   namespace
   {
      std::mutex gHealthMutex;
      HealthReport gHealth;
   }

   void StartLaunchCheck()
   {
      const Os os = CurrentOs();
      const Paths paths = PathsFor(AppPaths::AppSupportDir(), os);
      const std::string exe = RealExecutable();
      const char* home = getenv(os == Os::kWindows ? "USERPROFILE" : "HOME");
      const char* path = getenv("PATH");
      const char* local = getenv("LOCALAPPDATA");
      HealthInput in;
      in.os = os;
      in.home = home != nullptr ? home : "";
      in.pathEnv = path != nullptr ? path : "";
      in.exe = exe;
      in.recordFile = paths.record;
      in.binDir = os == Os::kWindows ? std::string(local != nullptr ? local : "") + "/Infinite/bin" : in.home + "/.local/bin";
      in.agent = FindAgent(Prefs().agent);
      std::thread([in, paths, exe]()
      {
         // Setup was done before: keep the launchers pointed at the app that is running now.
         if (IsFile(paths.status) && !exe.empty() && FirstLine(ReadAll(paths.record)) != exe)
         {
            std::ofstream(AppPaths::FsPath(paths.record), std::ios::binary) << exe << "\n";
         }
         HealthReport r = CheckHealth(in);
         std::lock_guard<std::mutex> lock(gHealthMutex);
         gHealth = r;
      }).detach();
   }

   Readiness AgentReadiness(const Agent& agent)
   {
      const Os os = CurrentOs();
      const char* homeEnv = getenv(os == Os::kWindows ? "USERPROFILE" : "HOME");
      const char* pathEnv = getenv("PATH");
      const char* local = getenv("LOCALAPPDATA");
      HealthInput in;
      in.os = os;
      in.home = homeEnv != nullptr ? homeEnv : "";
      in.pathEnv = pathEnv != nullptr ? pathEnv : "";
      in.exe = RealExecutable();
      in.recordFile = PathsFor(AppPaths::AppSupportDir(), os).record;
      in.binDir = os == Os::kWindows ? std::string(local != nullptr ? local : "") + "/Infinite/bin" : in.home + "/.local/bin";
      in.agent = &agent;
      Readiness r;
      r.installed = AgentInstalled(os, agent, in.home, in.pathEnv);
      const HealthReport h = CheckHealth(in);
      r.connected = r.installed && h.step[0] == Health::kOk && h.step[2] == Health::kOk && h.step[4] == Health::kOk;
      if (!r.installed)
         r.why = std::string(agent.name) + " is not installed.";
      else
         for (int i : { 0, 2, 4 })
            if (r.why.empty() && h.step[i] != Health::kOk)
               r.why = !h.why[i].empty() ? h.why[i] : std::string("a setup step did not finish");
      // Each agent keeps its login in its own file; an API key in the environment works as well.
      const std::string id = agent.id;
      auto env = [](const char* name) { const char* v = getenv(name); return v != nullptr && v[0] != '\0'; };
      if (id == "claude")
         r.signedIn = ReadAll(in.home + "/.claude.json").find("\"oauthAccount\"") != std::string::npos || env("ANTHROPIC_API_KEY");
      else if (id == "codex")
         r.signedIn = IsFile(in.home + "/.codex/auth.json") || env("OPENAI_API_KEY");
      else if (id == "antigravity")
         r.signedIn = IsFile(in.home + "/.gemini/jetski-standalone-oauth-token") || env("GEMINI_API_KEY");
      return r;
   }

   HealthReport LatestHealth()
   {
      std::lock_guard<std::mutex> lock(gHealthMutex);
      return gHealth;
   }

   View Describe(Os os, const Status& status, const Agent& agent, bool agentInstalled, const std::string& exe, const Paths& paths, const HealthReport* health)
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
      int repairs = 0;
      if (health != nullptr && health->checked && !status.running)
         for (int i = 0; i < kStepCount && i < (int)v.steps.size(); i++)
            if (v.steps[(size_t)i].Green() && health->step[i] == Health::kBroken)
            {
               v.steps[(size_t)i].state = StepState::kRepair;
               v.steps[(size_t)i].detail = health->why[i];
               v.steps[(size_t)i].repairLine = SetupLine(os, paths.script);
               repairs++;
            }
      v.setupEnabled = !status.running;
      if (status.running)
         v.setupLabel = "Connecting...";
      else
         v.setupLabel = status.present ? "Connect again" : "Connect terminal & agent";
      const int green = status.GreenCount() - repairs, failed = status.FailedCount();
      if (!status.present)
         v.summary = "Not set up yet.";
      else if (status.running)
         v.summary = std::to_string(green) + " of " + std::to_string(kStepCount) + " done so far.";
      else if (failed > 0)
         v.summary = std::to_string(failed) + (failed == 1 ? " step needs attention." : " steps need attention.");
      else if (repairs > 0)
         v.summary = std::to_string(repairs) + (repairs == 1 ? " step needs repair." : " steps need repair.");
      else if (green == kStepCount)
         v.summary = "Everything is set up.";
      else
         v.summary = std::to_string(green) + " of " + std::to_string(kStepCount) + " done. Press Connect again after installing " + agent.name + ".";
      return v;
   }

   namespace
   {
      // The one place Antigravity's JSON config is edited: add / remove / check one stdio server by name.
      // Plain Node, no packages (Node is already a setup step), the same file on every OS.
      const char* const kMcpJsonTool = R"JS(const fs = require("fs"), os = require("os"), path = require("path");
const [cmd, rawFile, name, exe] = process.argv.slice(2);
const file = rawFile.startsWith("~") ? path.join(os.homedir(), rawFile.slice(1)) : rawFile;
let doc = {};
try { doc = JSON.parse(fs.readFileSync(file, "utf8")); } catch (e) { if (e.code !== "ENOENT") { console.error("cannot read " + file + ": " + e.message); process.exit(2); } }
if (typeof doc !== "object" || doc === null || Array.isArray(doc)) { console.error(file + " is not a JSON object"); process.exit(2); }
doc.mcpServers = doc.mcpServers || {};
if (cmd === "check") { const e = doc.mcpServers[name]; if (!e) process.exit(1); console.log(JSON.stringify(e)); process.exit(0); }
if (cmd === "add") doc.mcpServers[name] = { command: "npx", args: ["-y", "infinite-engine", "serve"], env: { INFINITE_BIN: exe } };
else if (cmd === "remove") delete doc.mcpServers[name];
else { console.error("usage: add|remove|check FILE NAME [EXE]"); process.exit(2); }
fs.mkdirSync(path.dirname(file), { recursive: true });
fs.writeFileSync(file, JSON.stringify(doc, null, 2) + "\n");
)JS";

      void StageTool(const Paths& paths)
      {
         std::error_code ec;
         std::filesystem::create_directories(AppPaths::FsPath(paths.dir), ec);
         std::ofstream(AppPaths::FsPath(paths.dir + "/mcp_json.js"), std::ios::binary) << kMcpJsonTool;
      }

      // Writes the shipped skill files where the script copies them from.
      void StageSkills(const Paths& paths)
      {
         for (const Skill& k : Skills())
         {
            const std::string dir = paths.dir + "/skills/" + k.name;
            std::error_code ec;
            std::filesystem::create_directories(AppPaths::FsPath(dir), ec);
            std::ofstream(AppPaths::FsPath(dir + "/SKILL.md"), std::ios::binary) << k.content;
         }
      }
   }

   bool Perform(Action action, Os os, const Agent& agent, const std::string& exe, const Paths& paths, const Runner& run, std::string* error, const std::string& step, const Status* prior)
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
         StageSkills(paths);
         StageTool(paths);
         std::ofstream out(AppPaths::FsPath(paths.script), std::ios::binary);
         out << BuildScript(os, exe, agent, paths.status);
         out.close();
         if (!out.good() && !std::filesystem::exists(AppPaths::FsPath(paths.script)))
            return fail("could not write " + paths.script);
         run("Infinite setup", SetupLine(os, paths.script));
         return true;
      }
      case Action::kSetupAgent:
      case Action::kConnectAll:
      {
         // One tab, start to finish: install the agent if it is missing, run the setup script, and only when
         // that succeeds hand the tab to the agent. A failed step stops the chain and stays on screen.
         if (exe.empty())
            return fail("Infinite could not find its own executable");
         std::error_code ec;
         std::filesystem::create_directories(AppPaths::FsPath(paths.dir), ec);
         StageSkills(paths);
         StageTool(paths);
         std::ofstream out(AppPaths::FsPath(paths.script), std::ios::binary);
         // The script installs a missing agent at step 5, after step 3 has Node: Codex installs with npm.
         out << BuildScript(os, exe, agent, paths.status, {}, nullptr, true);
         out.close();
         if (!out.good() && !std::filesystem::exists(AppPaths::FsPath(paths.script)))
            return fail("could not write " + paths.script);
         if (action == Action::kSetupAgent)
         {
            run(agent.name, SetupLine(os, paths.script));
            return true;
         }
         const bool win = os == Os::kWindows;
         run(agent.name, SetupLine(os, paths.script) + (win ? "; if ($LASTEXITCODE -eq 0) { " : " && ") + StartAgentLine(os, exe, agent) + (win ? " }" : ""));
         return true;
      }
      case Action::kRepairStep:
      {
         if (exe.empty())
            return fail("Infinite could not find its own executable");
         bool known = false;
         for (const char* id : kStepIds)
            known = known || step == id;
         if (!known)
            return fail("unknown step");
         std::error_code ec;
         std::filesystem::create_directories(AppPaths::FsPath(paths.dir), ec);
         StageSkills(paths);
         StageTool(paths);
         std::ofstream out(AppPaths::FsPath(paths.script), std::ios::binary);
         out << BuildScript(os, exe, agent, paths.status, { step }, prior);
         out.close();
         if (!out.good() && !std::filesystem::exists(AppPaths::FsPath(paths.script)))
            return fail("could not write " + paths.script);
         run("Infinite repair", SetupLine(os, paths.script));
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
