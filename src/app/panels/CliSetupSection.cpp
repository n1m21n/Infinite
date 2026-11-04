// Settings > Agents & Terminal > "Agents" (docs/plans/terminal/cli-setup.md). Draws what
// CliSetup::Describe() says; reads only status.json, never terminal text, and spawns nothing itself:
// the buttons hand one line to a new Terminal tab.
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/CodeBlock.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/PillGroup.h"
#include "app/ui/design/components/TextButton.h"
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

      FormParts::Section(T("Agents"));

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
         if (FormParts::Combo(L("Default agent"), &cur, items.c_str()))
         {
            prefs.agent = Agents()[(size_t)cur].id;
            SaveConfig(paths.config, prefs);
         }
      }

      {
         // What the Terminal panel opens to.
         int face = prefs.face == "terminal" ? 1 : 0;
         std::string items = std::string(T("Chat")) + '\0' + T("Terminal") + '\0' + '\0';
         if (FormParts::Combo(L("Agent interface"), &face, items.c_str()))
         {
            prefs.face = face == 1 ? "terminal" : "chat";
            SaveConfig(paths.config, prefs);
            gTerminalFace = face;
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

      // Every agent at a glance: the chat sends to any that is ready, by @name.
      {
         static std::vector<Readiness> sReady;
         static double sReadyAt = -10.0;
         if (now - sReadyAt > 1.0 || sReady.size() != Agents().size())
         {
            sReadyAt = now;
            sReady.clear();
            for (const Agent& a : Agents())
               sReady.push_back(AgentReadiness(a));
         }
         FormParts::Label(L("Agents"));
         const float h = ImGui::GetFrameHeight();
         for (size_t i = 0; i < Agents().size(); i++)
         {
            const Agent& a = Agents()[i];
            const Readiness& r = sReady[i];
            if (i > 0)
               FormParts::Label("");
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ChatDrawAvatar(ImGui::GetWindowDrawList(), ImVec2(at.x + h * 0.5f, at.y + h * 0.5f), h * 0.36f, a.id);
            ImGui::Dummy(ImVec2(h, h));
            FormParts::Inline();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(a.name);
            FormParts::Inline();
            ImGui::AlignTextToFramePadding();
            const char* state = r.Ready() ? T("Ready") : !r.installed ? T("Not installed") : !r.connected ? T("Not connected") : T("Sign in");
            if (r.Ready())
               ImGui::TextUnformatted(state);
            else
               ImGui::TextDisabled("%s", state);
         }
      }

      {
         FormParts::Label(L("Saved chats"));
         const int count = ChatSavedCount();
         ImGui::AlignTextToFramePadding();
         ImGui::Text("%d", count);
         FormParts::Inline();
         ImGui::BeginDisabled(count == 0 || ChatBusy());
         if (FormParts::Button(L("Clear all")))
            ImGui::OpenPopup("##clearchats");
         ImGui::EndDisabled();
         if (ImGui::BeginPopup("##clearchats"))
         {
            ImGui::TextUnformatted(T("Delete every saved chat?"));
            if (FormParts::Button(L("Delete")))
            {
               ChatClearHistory();
               ImGui::CloseCurrentPopup();
            }
            FormParts::Inline();
            if (FormParts::Button(L("Cancel")))
               ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
         }
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
      // A clipped or collapsing panel submits no items; moving the cursor then would trip ImGui's boundary check.
      if (ImGui::GetCurrentWindow()->SkipItems)
         return true;
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

      // One centred column, never wider than reads well; every control is a design component.
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float availW = ImGui::GetContentRegionAvail().x;
      const float colW = std::min(460.0f, std::max(160.0f, availW - 2.0f * tok::space_4));
      const float x = origin.x + (availW - colW) * 0.5f;
      const float rowH = ImGui::GetFrameHeight() + tok::space_1;
      float y = origin.y + tok::space_5;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const float cx = x + colW * 0.5f;
      auto centred = [&](UiType::Size size, UiType::Weight weight, ImU32 col, const char* text)
      {
         UiType::Scope scope(size, weight);
         y = EmptyState::DrawWrapped(dl, cx, y, colW, col, text);
      };

      centred(UiType::Size::Title, UiType::Weight::Semibold, ImGui::GetColorU32(ImGuiCol_Text), T("Welcome to the Terminal"));
      y += tok::space_2;
      centred(UiType::Size::Body, UiType::Weight::Regular, dim, T("Connect a coding agent to build with Infinite."));
      y += tok::space_5;

      {
         const auto& all = Agents();
         std::vector<PillGroup::Segment> segs;
         for (const Agent& a : all)
            segs.push_back({ a.id, a.name });
         int cur = 0;
         for (size_t i = 0; i < all.size(); i++)
            if (&all[i] == agent)
               cur = (int)i;
         const int hit = PillGroup::Draw("welcome.agent", UiLayout::Rect{ x, y, colW, rowH }, segs.data(), (int)segs.size(), cur);
         if (hit >= 0 && hit != cur)
         {
            prefs.agent = all[(size_t)hit].id;
            SaveConfig(paths.config, prefs);
         }
         y += rowH + tok::space_3;
      }
      y += tok::space_1;

      static std::string sError;
      auto perform = [&](Action a)
      {
         sError.clear();
         Perform(a, os, *agent, exe, paths, [](const std::string&, const std::string& line) { TerminalRunInNewTab(line); }, &sError, std::string(), nullptr);
         finish();
      };
      const std::string connect = std::string(T("Connect")) + " " + agent->name;
      if (TextButton::Draw("welcome.connect", UiLayout::Rect{ x, y, colW, rowH }, connect.c_str(), TextButton::Kind::Primary))
         perform(Action::kConnectAll);
      y += rowH + tok::space_2;
      if (TextButton::Draw("welcome.skip", UiLayout::Rect{ x, y, colW, rowH }, T("Skip, just a terminal"), TextButton::Kind::Quiet))
         finish();
      y += rowH;
      if (!sError.empty())
      {
         y += tok::space_2;
         centred(UiType::Size::Caption, UiType::Weight::Regular, tok::U32(tok::action_record, CategoryColors::IsThemeLight()), sError.c_str());
      }
      ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
      ImGui::Dummy(ImVec2(availW, tok::space_4));
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
