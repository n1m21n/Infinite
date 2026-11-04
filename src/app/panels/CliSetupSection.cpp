// Settings > Terminal > "Command line & agents" (docs/plans/terminal/cli-setup.md). Draws what
// CliSetup::Describe() says; reads only status.json, never terminal text, and spawns nothing itself:
// the buttons hand one line to a new Terminal tab.
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/CodeBlock.h"
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
      const ImVec2 wide(FormParts::ControlW(), 0.0f);   // frame height, the same as the dropdown above it
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
         const float icon = ImGui::GetTextLineHeight();
         ImGui::AlignTextToFramePadding();
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
         HelpTip("%s", T("Opens a Terminal tab and runs the script shown under Reference, Setup commands. Each step checks first and skips what is already done, so run it again any time to repair."));

      FormParts::Label(L("Agent"));
      if (v.showInstall)
      {
         if (button((std::string(T("Install")) + "###cli_install").c_str(), Kind::Primary))
            perform(Action::kInstallAgent);
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Opens a Terminal tab and runs the agent's own installer, shown under Reference, Setup commands."));
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

      SectionCard::End();
   }

   // First open of the Terminal: what it is, which agent to connect, one button to set it up. Returns true
   // while the welcome is showing (the panel draws nothing else then). "Skip" or any setup ends it for good.
   bool DrawTerminalWelcome()
   {
      using namespace CliSetup;
      Config& prefs = Prefs();
      if (prefs.welcomed)
         return false;
      const Os os = CurrentOs();
      const Paths paths = PathsFor(AppPaths::AppSupportDir(), os);
      const Agent* agent = FindAgent(prefs.agent);
      if (agent == nullptr)
         agent = &Agents()[0];
      const std::string exe = RealExecutable();
      const View v = Describe(os, ParseStatus(ReadFile(paths.status), (long long)std::time(nullptr)), *agent, AgentInstalledHere(*agent), exe, paths, nullptr);

      auto finish = [&]()
      {
         prefs.welcomed = true;
         SaveConfig(paths.config, prefs);
      };

      ImGui::Dummy(ImVec2(0.0f, tok::space_4));
      ImGui::Indent(tok::space_4);
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(240.0f, ImGui::GetContentRegionAvail().x - tok::space_4));
      ImGui::TextUnformatted(T("Welcome to the Terminal"));
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      ImGui::TextDisabled("%s", T("A real shell inside Infinite. Run any command, and connect a coding agent that builds patches, renders and edits meshes with you."));
      ImGui::Dummy(ImVec2(0.0f, tok::space_3));

      ImGui::TextUnformatted(T("Choose your coding agent"));
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      const auto& all = Agents();
      const float colW = ImGui::GetContentRegionAvail().x;
      const float bw = ActionButton::RowWidth((int)all.size(), colW + ActionButton::ColumnIndent());
      for (size_t i = 0; i < all.size(); i++)
      {
         if (i > 0)
            ActionButton::RowNext();
         const bool on = &all[i] == agent;
         if (ActionButton::Draw((std::string(all[i].name) + "###welcome_agent" + std::to_string(i)).c_str(), ImVec2(bw, 0.0f), on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
         {
            prefs.agent = all[i].id;
            SaveConfig(paths.config, prefs);
         }
      }
      ImGui::Dummy(ImVec2(0.0f, tok::space_3));
      ImGui::TextDisabled("%s", T("Connecting installs the agent if needed, links it to Infinite and adds the Infinite skills. You can change this later in Settings, Terminal."));
      ImGui::Dummy(ImVec2(0.0f, tok::space_3));

      static std::string sError;
      auto perform = [&](Action a)
      {
         sError.clear();
         Perform(a, os, *agent, exe, paths, [](const std::string&, const std::string& line) { TerminalRunInNewTab(line); }, &sError, std::string(), nullptr);
         finish();
      };
      const ImVec2 wide(std::min(colW, 260.0f), 0.0f);
      if (ActionButton::Draw((std::string(T("Connect")) + " " + agent->name + "###welcome_connect").c_str(), wide, ActionButton::Kind::Primary))
         perform(v.showInstall ? Action::kInstallAgent : Action::kSetup);
      if (ActionButton::Draw((std::string(T("Skip, just a terminal")) + "###welcome_skip").c_str(), wide, ActionButton::Kind::Plain))
         finish();
      if (!sError.empty())
         ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tok::U32(tok::action_record, CategoryColors::IsThemeLight())), "%s", sError.c_str());
      ImGui::PopTextWrapPos();
      ImGui::Unindent(tok::space_4);
      return true;
   }

   // The exact commands and script behind the buttons, for the Reference well on the same page.
   void DrawCliSetupCommands()
   {
      using namespace CliSetup;
      const Os os = CurrentOs();
      const Paths paths = PathsFor(AppPaths::AppSupportDir(), os);
      const Agent* agent = FindAgent(Prefs().agent);
      if (agent == nullptr)
         agent = &Agents()[0];
      const std::string exe = RealExecutable();
      const View v = Describe(os, ParseStatus("", 0), *agent, AgentInstalledHere(*agent), exe, paths, nullptr);

      ImGui::TextWrapped("%s", T("Connect runs this line in a Terminal tab. It runs the script below, which checks each step first and skips what is already done."));
      CodeBlock::Draw(v.setupLine.c_str(), "cb_cli_setup");
      if (v.showInstall)
      {
         ImGui::TextWrapped("%s", T("Install runs the agent's own installer:"));
         CodeBlock::Draw(v.installLine.c_str(), "cb_cli_install");
      }
      ImGui::TextWrapped("%s", T("Start agent runs:"));
      CodeBlock::Draw(v.startLine.c_str(), "cb_cli_start");
      ImGui::TextWrapped("%s", T("The script, exactly as it runs:"));
      static std::string sScript;
      static std::string sScriptFor;
      const std::string key = exe + "|" + agent->id;
      if (sScriptFor != key)
      {
         sScriptFor = key;
         sScript = BuildScript(os, exe, *agent, paths.status);
      }
      CodeBlock::Draw(sScript.c_str(), "cb_cli_script");
   }
}
