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
#include "app/ui/design/components/Switch.h"
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
      ChatAgentsTick();

      FormParts::Section(T("Agents"));

      // One row per agent, all the same: its mark, its name, where it stands, and the one button that moves it on.
      for (const Agent& a : Agents())
      {
         const ChatAgentInfo info = ChatAgentStatus(a.id);
         ImGui::PushID(a.id);
         FormParts::Label(L(a.name));
         const float h = ImGui::GetFrameHeight();
         const float rowStart = ImGui::GetCursorPosX();
         const float rowW = ImGui::GetContentRegionAvail().x;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ChatDrawAvatar(ImGui::GetWindowDrawList(), ImVec2(at.x + h * 0.5f, at.y + h * 0.5f), h * 0.42f, a.id);
         ImGui::Dummy(ImVec2(h, h));
         FormParts::Inline();
         ImGui::AlignTextToFramePadding();
         if (info.ready)
            ImGui::TextUnformatted(info.status.c_str());
         else
            ImGui::TextDisabled("%s", info.status.c_str());
         if (!info.action.empty())  // Cancel shows while a sign-in waits
         {
            const float bw = std::max(84.0f, ImGui::CalcTextSize(info.action.c_str()).x + 2.0f * tok::space_3);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(rowStart + rowW - bw);
            if (ChipButton::Draw(info.action.c_str(), !info.ready, h, bw))
               ChatAgentAct(a.id, false);
         }
         ChatAgentCodeEntry(a.id, rowW);
         ImGui::PopID();
      }

      // The model each agent starts with; the chat's picker changes it for a moment, this is where it is kept.
      for (const Agent& a : Agents())
      {
         const auto models = ChatAgentModels(a.id);
         std::string items = std::string(T("Default")) + '\0';
         int cur = 0;
         const std::string saved = prefs.models.count(a.id) ? prefs.models[a.id] : std::string();
         for (size_t i = 0; i < models.size(); i++)
         {
            items += models[i].second + '\0';
            if (models[i].first == saved)
               cur = (int)i + 1;
         }
         items.push_back('\0');
         const std::string label = std::string(a.name) + " " + T("model");
         ImGui::PushID(a.id);
         if (FormParts::Combo(label.c_str(), &cur, items.c_str()))
         {
            if (cur == 0)
               prefs.models.erase(a.id);
            else
               prefs.models[a.id] = models[(size_t)cur - 1].first;
            SaveConfig(paths.config, prefs);
         }
         ImGui::PopID();
      }

      // Only a fault in the plumbing (the infinite command, the MCP link, the skills) shows here, with its fix.
      {
         const std::string exe = RealExecutable();
         const Agent* agent = FindAgent(prefs.agent);
         if (agent == nullptr)
            agent = &Agents()[0];
         const HealthReport health = LatestHealth();
         const View v = Describe(os, ParseStatus("", 0), *agent, AgentInstalledHere(*agent), exe, paths, &health);
         std::string broken;
         for (const StepStatus& st : v.steps)
            if (st.state == StepState::kRepair || st.state == StepState::kFailed)
               broken += (broken.empty() ? "" : ", ") + std::string(T(st.title));
         const ChatAgentInfo cur = ChatAgentStatus(agent->id);
         if (!broken.empty() && cur.ready && !cur.busy)
         {
            FormParts::Label(L("Connection"));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", broken.c_str());
            FormParts::Inline();
            if (FormParts::Button(L("Repair")))
               ChatAgentAct(agent->id, true);
         }
      }

      {
         const Paths wpaths = PathsFor(AppPaths::AppSupportDir(), CurrentOs());
         FormParts::Label(L("Agent folder"));
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted(prefs.workspace.empty() ? T("The open patch's folder") : prefs.workspace.c_str());
         FormParts::Inline();
         if (FormParts::Button(L("Choose…")))
         {
            const std::string dir = Platform::OpenFolderDialog("Folder for agents", prefs.workspace);
            if (!dir.empty())
            {
               prefs.workspace = dir;
               SaveConfig(wpaths.config, prefs);
            }
         }
         FormParts::Label(L("Edit files in it"));
         if (Switch::Box("##agentaccess", &prefs.folderAccess))
            SaveConfig(wpaths.config, prefs);
      }

      {
         const int count = ChatSavedCount();
         const std::string label = std::string(T("Saved chats")) + " (" + std::to_string(count) + ")";
         FormParts::Label(label.c_str());
         ImGui::BeginDisabled(count == 0 || ChatBusy());
         if (FormParts::Button(L("Clear all")))
            ImGui::OpenPopup("##clearchats");
         ImGui::EndDisabled();
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_3, tok::space_3));
         if (ImGui::BeginPopup("##clearchats"))
         {
            ImGui::TextUnformatted(T("Delete every saved chat?"));
            ImGui::Dummy(ImVec2(0.0f, tok::space_2));
            const float cancelW = std::max(64.0f, ImGui::CalcTextSize(T("Cancel")).x + 2.0f * tok::space_2);
            const float deleteW = std::max(64.0f, ImGui::CalcTextSize(T("Delete")).x + 2.0f * tok::space_2);
            const float total = cancelW + tok::space_2 + deleteW;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > total)
               ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - total);
            if (ChipButton::Draw(L("Cancel"), false, 24.0f, cancelW))
               ImGui::CloseCurrentPopup();
            FormParts::Inline(0.0f, tok::space_2);
            if (ChipButton::Draw(L("Delete"), true, 24.0f, deleteW))
            {
               ChatClearHistory();
               ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
         }
         ImGui::PopStyleVar();
      }

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

      ImGui::TextWrapped("%s", T("Set up and Repair run this line in the background, with no terminal. It runs the script below, which checks each step first and skips what is already done."));
      CodeBlock::Draw(v.setupLine.c_str(), "cb_cli_setup");
      if (v.showInstall)
      {
         ImGui::TextWrapped("%s", T("The agent's own installer:"));
         CodeBlock::Draw(v.installLine.c_str(), "cb_cli_install");
      }
      ImGui::TextWrapped("%s", T("The command the chat starts for each message begins with:"));
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
