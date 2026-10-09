// Find on the canvas (Cmd/Ctrl+F). One field over the canvas; matches node title, node type, comment
// text and param names as you type; Up/Down step, Enter jumps to the node, Esc closes.
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/LibraryParts.h"
#include "app/AppShared.h"
#include "nodes/UtilityNodes.h"

namespace app
{
namespace
{
   struct NameCollector : ParamVisitor
   {
      std::string names;
      void Add(const char* n) { names += n; names += ' '; }
      void Float(const char* n, float&) override { Add(n); }
      void Int(const char* n, int&) override { Add(n); }
      void Bool(const char* n, bool&) override { Add(n); }
      void Text(const char* n, std::string&) override { Add(n); }
      void Color(const char* n, float*) override { Add(n); }
   };

   bool sOpen = false;
   bool sFocusField = false;
   char sBuf[128] = {};
   std::string sLastQuery;
   size_t sLastNodeCount = (size_t)-1;
   std::vector<int> sMatches;   // GraphNode::index, best field first
   int sCurrent = 0;
   int sJumpTo = -1;            // GraphNode::index to select + centre this frame

   std::string Lower(std::string s)
   {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
   }

   // 0 title, 1 type, 2 comment text, 3 param name; -1 no match.
   int Rank(const GraphNode& gn, const std::string& q)
   {
      if (NodeSearchMatches(NodeTitle(gn), q)) return 0;
      if (NodeSearchMatches(DisplayName(gn.typeName) + " " + gn.typeName, q)) return 1;
      if (auto* c = dynamic_cast<CommentNode*>(gn.node.get()))
         if (NodeSearchMatches(c->text, q)) return 2;
      NameCollector nc;
      gn.node->VisitParams(nc);
      if (NodeSearchMatches(nc.names, q)) return 3;
      return -1;
   }

   void Rebuild()
   {
      const std::string q = Lower(sBuf);
      sMatches.clear();
      if (!q.empty())
      {
         std::vector<std::pair<int, int>> ranked;   // rank, node index
         for (const GraphNode& gn : gNodes)
         {
            const int r = Rank(gn, q);
            if (r >= 0) ranked.emplace_back(r, gn.index);
         }
         std::stable_sort(ranked.begin(), ranked.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
         for (const auto& r : ranked) sMatches.push_back(r.second);
      }
      sLastQuery = q;
      sLastNodeCount = gNodes.size();
      sCurrent = sMatches.empty() ? 0 : std::min(sCurrent, (int)sMatches.size() - 1);
   }

   void Close()
   {
      sOpen = false;
      sMatches.clear();
      sLastQuery.clear();
      sLastNodeCount = (size_t)-1;
   }
}

bool FindIsOpen() { return sOpen; }
int FindMatchCount() { return (int)sMatches.size(); }
int FindMatchNodeIndex(int i) { return i >= 0 && i < (int)sMatches.size() ? sMatches[i] : -1; }
int FindCurrentMatch() { return sCurrent; }
bool FindIsMatch(int nodeIndex)
{
   return sOpen && std::find(sMatches.begin(), sMatches.end(), nodeIndex) != sMatches.end();
}

// Drawn inside the ed::Suspend() block next to the minimap, so ed:: selection and navigation calls work.
void DrawFind()
{
   ImGuiIO& io = ImGui::GetIO();
   const bool cmd = io.KeyCtrl || io.KeySuper;
   if (cmd && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
       (sOpen || !io.WantTextInput) && !gNavOwnsKeys && !gArrangeFocused && !gNodes.empty())
   {
      if (!sOpen)
      {
         sBuf[0] = '\0';
         sCurrent = 0;
      }
      sOpen = true;
      sFocusField = true;
   }
   // Review hook: INFINITE_FINDDEMO=<query> opens the field with that text for screenshots.
   static const char* sDemo = getenv("INFINITE_FINDDEMO");
   if (sDemo != nullptr && ImGui::GetFrameCount() > 20 && !gNodes.empty())
   {
      snprintf(sBuf, sizeof(sBuf), "%s", sDemo);
      sOpen = true;
      sDemo = nullptr;
   }
   if (!sOpen)
      return;
   if (gNodes.empty())
   {
      Close();
      return;
   }
   if (Lower(sBuf) != sLastQuery || gNodes.size() != sLastNodeCount)
      Rebuild();

   const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
   const float w = 340.0f;
   const ImVec2 pos(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f, gGraphScreenTL.y + 14.0f);
   ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
   ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
   ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.panelBg.r, t.panelBg.g, t.panelBg.b, 1.0f));
   ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(t.border.r, t.border.g, t.border.b, 0.9f));
   bool stay = true;
   if (ImGui::Begin("##canvasfind", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoCollapse))
   {
      if (sFocusField)
      {
         ImGui::SetKeyboardFocusHere();
         sFocusField = false;
      }
      const bool pickNow = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
      LibraryParts::SearchField("canvasfind", T("find nodes, comments, params..."), sBuf, sizeof(sBuf));
      if (!sMatches.empty())
      {
         const int n = (int)sMatches.size();
         if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) sCurrent = (sCurrent + 1) % n;
         if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) sCurrent = (sCurrent + n - 1) % n;
         if (pickNow) sJumpTo = sMatches[sCurrent];
      }
      sFocusField = pickNow;   // Enter ends the edit; take the caret straight back
      ImGui::Dummy(ImVec2(0, 2));
      if (sBuf[0] == '\0')
         ImGui::TextDisabled("%s", T("Type to find. Up/Down step, Enter jumps, Esc closes."));
      else if (sMatches.empty())
         ImGui::TextDisabled("%s", T("no matches"));
      else
         ImGui::TextDisabled("%d / %d", sCurrent + 1, (int)sMatches.size());
      if (ImGui::IsKeyPressed(ImGuiKey_Escape))
         stay = false;
   }
   ImGui::End();
   ImGui::PopStyleColor(2);
   ImGui::PopStyleVar(3);

   if (sJumpTo >= 0)
   {
      if (GraphNode* gn = FindNodeByIndex(sJumpTo))
      {
         ed::ClearSelection();
         ed::SelectNode(gn->NodeId(), false);
         // Keep the zoom unless the node would be unreadable at it.
         ed::NavigateToSelection(ed::GetCurrentZoom() < 0.5f, 0.2f);
      }
      sJumpTo = -1;
   }

   // Ring every match on the canvas, the current one heavier, so 400 nodes still read.
   if (stay)
   {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      dl->PushClipRect(gGraphScreenTL, ImVec2(gGraphScreenTL.x + gGraphScreenSize.x,
                                              gGraphScreenTL.y + gGraphScreenSize.y), true);
      const ImVec4 a = AccentEmphasisSelected();
      for (int i = 0; i < (int)sMatches.size(); ++i)
      {
         const GraphNode* gn = FindNodeByIndex(sMatches[i]);
         if (gn == nullptr) continue;
         const ImVec2 p = ed::CanvasToScreen(ed::GetNodePosition(gn->NodeId()));
         const ImVec2 s = ed::GetNodeSize(gn->NodeId());
         const float z = ed::GetCurrentZoom();
         const bool cur = i == sCurrent;
         dl->AddRect(ImVec2(p.x - 3, p.y - 3), ImVec2(p.x + s.x * z + 3, p.y + s.y * z + 3),
                     ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, cur ? 1.0f : 0.6f)), 6.0f, 0, cur ? 3.0f : 1.5f);
      }
      dl->PopClipRect();
   }
   else
      Close();
}
}
