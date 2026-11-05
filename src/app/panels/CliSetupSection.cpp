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
#include "core/chat/ChatThread.h"
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

      // Every row the same grid: the label column, the value after it, and one button column on the right edge whose
      // buttons all share one width and the row's height.
      const float rowH = ImGui::GetFrameHeight();
      float bw = 84.0f;
      for (const char* l : { "Sign in", "Sign out", "Set up", "Cancel", "Repair", "Choose…", "Clear all", "Add folder…", "Remove" })
         bw = std::max(bw, ImGui::CalcTextSize(T(l)).x + 2.0f * tok::space_3);
      // Label first, then this: the value text, cut to what fits before the button column.
      auto value = [&](const std::string& text, bool dim)
      {
         const float room = std::max(0.0f, ImGui::GetContentRegionAvail().x - bw - tok::space_3);
         std::string shown = text;
         if (ImGui::CalcTextSize(shown.c_str()).x > room)
         {
            while (!shown.empty() && ImGui::CalcTextSize(("…" + shown).c_str()).x > room)
               shown.erase(0, 1);
            shown = "…" + shown;
         }
         ImGui::AlignTextToFramePadding();
         if (dim)
            ImGui::TextDisabled("%s", shown.c_str());
         else
            ImGui::TextUnformatted(shown.c_str());
         if (shown != text && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text.c_str());
      };
      // The row's button, in the right column.
      auto button = [&](const char* label, bool primary)
      {
         // Measured without the "###id" a translated label carries, so every button is the column's width and
         // none reaches past the card (that overflow let the page scroll sideways).
         const float w = std::max(bw, ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * tok::space_3);
         FormParts::Inline(0.0f, 0.0f);
         ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - w);
         return ChipButton::Draw(label, primary, rowH, w);
      };

      // One row per agent, all the same: its mark, its name, where it stands, and the one button that moves it on.
      for (const Agent& a : Agents())
      {
         const ChatAgentInfo info = ChatAgentStatus(a.id);
         ImGui::PushID(a.id);
         FormParts::Label(L(a.name));
         const float rowW = ImGui::GetContentRegionAvail().x;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         ChatDrawAvatar(ImGui::GetWindowDrawList(), ImVec2(at.x + rowH * 0.5f, at.y + rowH * 0.5f), rowH * 0.42f, a.id);
         ImGui::Dummy(ImVec2(rowH, rowH));
         FormParts::Inline(0.0f, tok::space_2);
         value(info.status, !info.ready);
         if (!info.action.empty())  // Cancel shows while a sign-in waits
            if (button(info.action.c_str(), !info.ready))
               ChatAgentAct(a.id, false);
         ChatAgentCodeEntry(a.id, rowW);
         if (!info.problem.empty())
         {
            FormParts::Label("");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tok::U32(tok::action_record, CategoryColors::IsThemeLight())));
            ImGui::TextWrapped("%s", info.problem.c_str());
            ImGui::PopStyleColor();
         }
         ImGui::PopID();
      }

      // The model each connected agent answers with; the chat's picker sets the same thing. An agent that is not
      // installed and signed in has no model to pick yet.
      for (const Agent& a : Agents())
      {
         if (!ChatAgentStatus(a.id).ready)
            continue;
         const auto models = ChatAgentModels(a.id);
         if (models.empty())
            continue;
         const std::string face = ChatAgentModelFace(a.id);
         std::string items;
         int cur = 0;
         for (size_t i = 0; i < models.size(); i++)
         {
            items += models[i].second + '\0';
            if (models[i].second == face)
               cur = (int)i;
         }
         items.push_back('\0');
         const std::string label = std::string(a.name) + " " + T("model");
         ImGui::PushID(a.id);
         if (FormParts::Combo(label.c_str(), &cur, items.c_str()))
         {
            prefs.models[a.id] = models[(size_t)cur].first;
            SaveConfig(paths.config, prefs);
         }
         ImGui::PopID();
      }

      // What each agent used through the chat, so a long chat's cost is never a surprise. Cached input is read
      // again on every step of a turn and costs about a tenth of fresh input.
      {
         bool any = false;
         for (const Agent& a : Agents())
         {
            const auto it = prefs.usage.find(a.id);
            if (it == prefs.usage.end() || it->second.turns == 0)
               continue;
            const Config::Usage& u = it->second;
            const long long total = u.input + u.cached + u.output;
            std::string line = std::to_string(u.turns) + " " + T(u.turns == 1 ? "turn" : "turns") + " · " + chat::TokenCount(total) + " " + T("tokens");
            if (total > 0)
               line += " · " + std::to_string((int)(100.0 * (double)u.cached / (double)total)) + "% " + T("cached");
            if (u.cost > 0.0)
            {
               char c[32];
               std::snprintf(c, sizeof c, " · $%.2f", u.cost);
               line += c;
            }
            const std::string label = std::string(a.name) + " " + T("usage");
            ImGui::PushID(a.id);
            FormParts::Label(label.c_str());
            value(line, false);
            const bool reset = !any && button(L("Reset"), false);
            ImGui::PopID();
            any = true;
            if (reset)
            {
               prefs.usage.clear();
               SaveConfig(paths.config, prefs);
               break;
            }
         }
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
            value(broken, true);
            if (button(L("Repair"), true))
               ChatAgentAct(agent->id, true);
         }
      }

      {
         const Paths wpaths = PathsFor(AppPaths::AppSupportDir(), CurrentOs());
         FormParts::Label(L("Agent folder"));
         value(prefs.workspace.empty() ? std::string(T("The open patch's folder")) : HomeTilde(prefs.workspace), prefs.workspace.empty());
         if (button(L("Choose…"), false))
         {
            const std::string dir = Platform::OpenFolderDialog("Folder for agents", prefs.workspace);
            if (!dir.empty())
            {
               prefs.workspace = dir;
               SaveConfig(wpaths.config, prefs);
            }
         }
         // More folders agents may open, and the commands they may run besides `infinite`: what an Allow under a
         // "Not allowed" note in the chat adds, and where it is taken back.
         for (size_t i = 0; i < prefs.folders.size(); i++)
         {
            ImGui::PushID((int)i);
            FormParts::Label(i == 0 ? L("More folders") : "");
            value(HomeTilde(prefs.folders[i]), false);
            const bool remove = button(L("Remove"), false);
            ImGui::PopID();
            if (remove)
            {
               prefs.folders.erase(prefs.folders.begin() + (long)i);
               SaveConfig(wpaths.config, prefs);
               break;
            }
         }
         FormParts::Label(prefs.folders.empty() ? L("More folders") : "");
         value(prefs.folders.empty() ? std::string(T("None")) : std::string(), true);
         if (button(L("Add folder…"), false))
         {
            const std::string dir = Platform::OpenFolderDialog("Folder for agents", prefs.workspace);
            if (!dir.empty() && std::find(prefs.folders.begin(), prefs.folders.end(), dir) == prefs.folders.end())
            {
               prefs.folders.push_back(dir);
               SaveConfig(wpaths.config, prefs);
            }
         }
         FormParts::Label(L("Edit files in them"));
         {
            const float pad = (rowH - Switch::kH) * 0.5f;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
            if (Switch::Box("##agentaccess", &prefs.folderAccess))
               SaveConfig(wpaths.config, prefs);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
         }
         for (size_t i = 0; i < prefs.commands.size(); i++)
         {
            ImGui::PushID(1000 + (int)i);
            FormParts::Label(i == 0 ? L("Allowed commands") : "");
            value(prefs.commands[i], false);
            const bool remove = button(L("Remove"), false);
            ImGui::PopID();
            if (remove)
            {
               prefs.commands.erase(prefs.commands.begin() + (long)i);
               SaveConfig(wpaths.config, prefs);
               break;
            }
         }
      }

      {
         const int count = ChatSavedCount();
         const std::string label = std::string(T("Saved chats")) + " (" + std::to_string(count) + ")";
         FormParts::Label(label.c_str());
         ImGui::Dummy(ImVec2(0.0f, rowH));
         ImGui::BeginDisabled(count == 0 || ChatBusy());
         if (button(L("Clear all"), false))
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
}
