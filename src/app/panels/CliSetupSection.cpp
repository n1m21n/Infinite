// Settings > Terminal > "Command line & agents" (docs/plans/terminal/cli-setup.md). Draws what
// CliSetup::Describe() says; reads only status.json, never terminal text, and spawns nothing itself:
// the buttons hand one line to a new Terminal tab.
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/components/ActionButton.h"
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
         // Semantic roles, the same everywhere: go = green, learn = amber, record = red. A glyph or label
         // always rides with the colour.
         if (s.Green())
         {
            const ImU32 col = tok::U32(tok::action_go, light);
            dl->AddPolyline(std::array<ImVec2, 3>{ ImVec2(c.x - r, c.y), ImVec2(c.x - r * 0.25f, c.y + r * 0.8f), ImVec2(c.x + r, c.y - r * 0.7f) }.data(), 3, col, 0, 2.0f);
         }
         else if (s.state == S::kFailed)
            glyph::Draw(dl, c, size, tok::U32(tok::action_record, light), IconsInfinite::Error);
         else if (s.state == S::kRepair)
            glyph::Draw(dl, c, size, tok::U32(tok::action_learn, light), IconsInfinite::Warning);
         else if (s.state == S::kRunning)
            dl->AddCircleFilled(c, r * 0.7f, tok::U32(tok::action_learn, light));
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

      FormParts::Section(T("Terminal & coding agent"));
      ImGui::TextDisabled("%s", T("Connect your coding agent to Infinite. It runs in a Terminal tab you can watch."));
      ImGui::Dummy(ImVec2(0.0f, tok::space_2));

      static std::string sError;
      auto perform = [&](Action a, const std::string& step = std::string())
      {
         sError.clear();
         Perform(a, os, *agent, exe, paths, [](const std::string&, const std::string& line) { TerminalRunInNewTab(line); }, &sError, step, &sStatus);
         sLastRead = -10.0;
      };
      using Kind = ActionButton::Kind;
      const ImVec2 wide(FormParts::ControlW(), FormParts::kRowH);
      auto button = [&](const char* label, Kind kind) { return ActionButton::Draw(label, wide, kind); };

      bool allGreen = sStatus.present && !sStatus.running;
      bool anyFailed = false;
      for (const StepStatus& st : v.steps)
      {
         allGreen = allGreen && st.Green();
         anyFailed = anyFailed || st.state == StepState::kFailed;
      }

      // One status line: a glyph, then the sentence.
      FormParts::Label(L("Status"));
      {
         StepStatus mark;
         mark.state = allGreen ? StepState::kOk : anyFailed ? StepState::kFailed : sStatus.running ? StepState::kRunning : sStatus.present ? StepState::kRepair : StepState::kPending;
         const float icon = ImGui::GetFrameHeight();
         DrawStepIcon(mark, icon);
         FormParts::Inline();
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted(T(v.summary.c_str()));
      }

      {
         std::string items;
         int cur = 0;
         for (size_t i = 0; i < Agents().size(); i++)
         {
            items += Agents()[i].name;
            items.push_back('\0');
            if (&Agents()[i] == agent)
               cur = (int)i;
         }
         items.push_back('\0');
         if (FormParts::Combo(L("Coding agent"), &cur, items.c_str()))
         {
            prefs.agent = Agents()[(size_t)cur].id;
            SaveConfig(paths.config, prefs);
         }
      }

      FormParts::Label(L("Setup"));
      ImGui::BeginDisabled(!v.setupEnabled);
      if (button((std::string(T(v.setupLabel.c_str())) + "###cli_setup").c_str(), allGreen ? Kind::Plain : Kind::Primary))
         perform(Action::kSetup);
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", T("Opens a Terminal tab and runs the script shown under Show commands. Each step checks first and skips what is already done, so run it again any time to repair."));

      FormParts::Label(L("Agent"));
      if (v.showInstall)
      {
         if (button((std::string(T("Install")) + "###cli_install").c_str(), Kind::Primary))
            perform(Action::kInstallAgent);
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Opens a Terminal tab and runs the agent's own installer, shown under Show commands."));
      }
      else
      {
         ImGui::BeginDisabled(!v.startEnabled);
         if (button((std::string(T("Start agent")) + "###cli_start").c_str(), Kind::Plain))
            perform(Action::kStartAgent);
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            HelpTip("%s", T(v.loginHint.c_str()));
      }

      {
         bool autoStart = prefs.autoStart;
         ImGui::BeginDisabled(!v.startEnabled);
         if (FormParts::Switch(L("Start with Terminal"), &autoStart))
         {
            prefs.autoStart = autoStart;
            SaveConfig(paths.config, prefs);
         }
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            HelpTip("%s", T("Starts the agent in the first Terminal tab each time the Terminal opens."));
      }

      FormParts::Label(L("Terminal panel"));
      if (button(gTerminalOpen ? "Close Terminal###cli_term" : "Open Terminal###cli_term", Kind::Plain))
      {
         gTerminalOpen = !gTerminalOpen;
         gTerminalFocus = gTerminalOpen;
      }

      // Only what is wrong is listed; when everything holds the steps stay out of the way.
      if (!allGreen)
      {
         ImGui::Dummy(ImVec2(0.0f, tok::space_2));
         const float icon = ImGui::GetTextLineHeight();
         for (const StepStatus& s : v.steps)
         {
            if (s.Green())
               continue;
            ImGui::PushID(s.id);
            DrawStepIcon(s, icon);
            FormParts::Inline();
            ImGui::TextUnformatted(T(s.title));
            if (!s.detail.empty())
            {
               FormParts::Inline();
               ImGui::TextDisabled("%s", s.detail.c_str());
            }
            if (s.state == StepState::kRepair)
            {
               FormParts::Inline();
               if (FormParts::Button(L("Repair")))
                  perform(Action::kRepairStep, s.id);
               if (ImGui::IsItemHovered())
                  HelpTip("%s", T("Opens a Terminal tab and reruns only this step."));
            }
            ImGui::PopID();
         }
      }
      if (!sError.empty())
         ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tok::U32(tok::action_record, CategoryColors::IsThemeLight())), "%s", sError.c_str());
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));

      if (ImGui::TreeNode(T("Show commands")))
      {
         ImGui::TextDisabled("%s", T("Connect terminal & agent types this line, and runs the script below it:"));
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

      SectionCard::End();
   }
}
