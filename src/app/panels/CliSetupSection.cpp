// Settings > Terminal > "Command line & agents" (docs/plans/terminal/cli-setup.md). Draws what
// CliSetup::Describe() says; reads only status.json, never terminal text, and spawns nothing itself:
// the buttons hand one line to a new Terminal tab.
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/FormParts.h"
#include "core/CliSetup.h"
#include "platform/AppPaths.h"

#include <ctime>
#include <fstream>
#include <sstream>

namespace app
{
   namespace
   {
      std::string ReadFile(const std::string& path)
      {
         std::ifstream f(AppPaths::FsPath(path), std::ios::binary);
         std::stringstream ss;
         ss << f.rdbuf();
         return ss.str();
      }

      void DrawStepIcon(const CliSetup::StepStatus& s, float size)
      {
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool light = CategoryColors::IsThemeLight();
         const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
         const float r = size * 0.28f;
         using S = CliSetup::StepState;
         if (s.Green())
         {
            const ImU32 col = tok::U32(tok::terminal_ansi2, light);
            dl->AddPolyline(std::array<ImVec2, 3>{ ImVec2(c.x - r, c.y), ImVec2(c.x - r * 0.25f, c.y + r * 0.8f), ImVec2(c.x + r, c.y - r * 0.7f) }.data(), 3, col, 0, 2.0f);
         }
         else if (s.state == S::kFailed)
         {
            const ImU32 col = tok::U32(tok::terminal_ansi1, light);
            dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 2.0f);
            dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), col, 2.0f);
         }
         else if (s.state == S::kRepair)
         {
            const ImU32 col = tok::U32(tok::terminal_ansi3, light);
            dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r * 0.3f), col, 2.0f);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.85f), 1.6f, col);
         }
         else if (s.state == S::kRunning)
            dl->AddCircleFilled(c, r * 0.7f, tok::U32(tok::terminal_ansi3, light));
         else
            dl->AddLine(ImVec2(c.x - r * 0.7f, c.y), ImVec2(c.x + r * 0.7f, c.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), 2.0f);
         ImGui::Dummy(ImVec2(size, size));
      }
   }

   void DrawCliSetupSection()
   {
      using namespace CliSetup;
      const Os os = CurrentOs();
      const Paths paths = PathsFor(AppPaths::AppSupportDir(), os);
      Config& prefs = Prefs();
      const Agent* agent = FindAgent(prefs.agent);
      if (agent == nullptr)
         agent = &Agents()[0];
      const std::string exe = RealExecutable();

      // The file is small; read it about once a second, and only while this section is on screen.
      static double sLastRead = -10.0;
      static Status sStatus = ParseStatus("", 0);
      const double now = ImGui::GetTime();
      if (now - sLastRead > 1.0)
      {
         sLastRead = now;
         const bool wasRunning = sStatus.running;
         sStatus = ParseStatus(ReadFile(paths.status), (long long)std::time(nullptr));
         if (wasRunning && !sStatus.running)
            StartLaunchCheck();   // a setup or repair just finished: look again, quietly
      }
      const bool installed = AgentInstalledHere(*agent);
      const HealthReport health = LatestHealth();
      const View v = Describe(os, sStatus, *agent, installed, exe, paths, &health);

      FormParts::Section(T("Command line & agents"));
      ImGui::TextWrapped("%s", T("Sets up the infinite command in every terminal, checks the headless engine, and connects your coding agent to Infinite through MCP. It runs in a Terminal tab you can watch, so a password prompt works. Nothing is installed until you press a button."));

      static std::string sError;
      auto perform = [&](Action a, const std::string& step = std::string())
      {
         sError.clear();
         Perform(a, os, *agent, exe, paths, [](const std::string&, const std::string& line) { TerminalRunInNewTab(line); }, &sError, step, &sStatus);
         sLastRead = -10.0;
      };

      ImGui::BeginDisabled(!v.setupEnabled);
      if (FormParts::Button((std::string(T(v.setupLabel.c_str())) + "###cli_setup").c_str()))
         perform(Action::kSetup);
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", T("Opens a Terminal tab and runs the script shown under Show the commands. Each step checks first and skips what is already done, so run it again any time to repair."));
      ImGui::SameLine();
      ImGui::TextDisabled("%s", T(v.summary.c_str()));

      const float icon = ImGui::GetTextLineHeight();
      for (const StepStatus& s : v.steps)
      {
         ImGui::PushID(s.id);
         DrawStepIcon(s, icon);
         ImGui::SameLine();
         ImGui::TextUnformatted(T(s.title));
         if (!s.detail.empty())
         {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", s.detail.c_str());
         }
         if (s.state == StepState::kRepair)
         {
            ImGui::SameLine();
            if (FormParts::Button(L("Repair")))
               perform(Action::kRepairStep, s.id);
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Opens a Terminal tab and reruns only this step."));
         }
         ImGui::PopID();
      }

      if (ImGui::TreeNode(T("Show the commands")))
      {
         ImGui::TextDisabled("%s", T("Get set up types this line, and runs the script below it:"));
         ImGui::TextUnformatted(v.setupLine.c_str());
         if (v.showInstall)
         {
            ImGui::TextDisabled("%s", T("Install types:"));
            ImGui::TextUnformatted(v.installLine.c_str());
         }
         ImGui::TextDisabled("%s", T("Start agent types:"));
         ImGui::TextWrapped("%s", v.startLine.c_str());
         ImGui::TextDisabled("%s", T("The script (the exact text that runs):"));
         static std::string sScript;
         static std::string sScriptFor;
         const std::string key = exe + "|" + agent->id;
         if (sScriptFor != key)
         {
            sScriptFor = key;
            sScript = BuildScript(os, exe, *agent, paths.status);
         }
         ImGui::InputTextMultiline("##cli_script", sScript.data(), sScript.size() + 1, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 14), ImGuiInputTextFlags_ReadOnly);
         ImGui::TreePop();
      }

      ImGui::Spacing();
      ImGui::TextUnformatted(T("Coding agent"));
      {
         std::vector<const char*> names;
         int cur = 0;
         for (size_t i = 0; i < Agents().size(); i++)
         {
            names.push_back(Agents()[i].name);
            if (&Agents()[i] == agent)
               cur = (int)i;
         }
         ImGui::SameLine();
         if (FormParts::BareCombo("##cli_agent", &cur, names.data(), (int)names.size(), 180.0f))
         {
            prefs.agent = Agents()[(size_t)cur].id;
            SaveConfig(paths.config, prefs);
         }
      }
      ImGui::SameLine();
      if (v.showInstall)
      {
         if (FormParts::Button(L("Install")))
            perform(Action::kInstallAgent);
         ImGui::SameLine();
      }
      ImGui::BeginDisabled(!v.startEnabled);
      if (FormParts::Button(L("Start agent")))
         perform(Action::kStartAgent);
      ImGui::EndDisabled();
      ImGui::TextDisabled("%s", v.agentInstalled ? T("Installed.") : T("Not installed yet. Install runs the command shown above in a Terminal tab."));
      if (v.startEnabled)
      {
         bool autoStart = prefs.autoStart;
         if (FormParts::Checkbox(T("Start the agent when the Terminal opens"), &autoStart))
         {
            prefs.autoStart = autoStart;
            SaveConfig(paths.config, prefs);
         }
      }
      ImGui::TextDisabled("%s", T(v.loginHint.c_str()));
      if (!sError.empty())
         ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tok::U32(tok::terminal_ansi1, CategoryColors::IsThemeLight())), "%s", sError.c_str());
      SectionCard::End();
   }
}
