// Find on the canvas (Cmd/Ctrl+F). One field over the canvas; matches node title, node type, comment
// text and param names as you type; Up/Down step, Enter jumps to the node, Esc closes.
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/Keymap.h"
#include "app/ui/design/components/LibraryParts.h"
#include "app/AppShared.h"
#include "app/ui/design/components/PanelFrame.h"
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
   if (cmd && !io.KeyShift && !io.KeyAlt && Keymap::KeyPressed(Keymap::Act::Find) &&
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

   const float w = tok::find_overlay_w;
   const ImVec2 pos(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f, gGraphScreenTL.y + tok::space_3_5);
   ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(tok::find_overlay_window_x, 0.0f));
   ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
   PanelFrame::PushFloatingStyle();
   bool stay = true;
   if (ImGui::Begin("##canvasfind", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoCollapse))
   {
      if (sFocusField)
      {
         UiInteract::FocusHere();
         sFocusField = false;
      }
      const bool pickNow = Keymap::Confirm(false);
      LibraryParts::SearchField("canvasfind", T("find nodes, comments, params..."), sBuf, sizeof(sBuf));
      if (!sMatches.empty())
      {
         const int n = (int)sMatches.size();
         if (Keymap::Down(true)) sCurrent = (sCurrent + 1) % n;
         if (Keymap::Up(true)) sCurrent = (sCurrent + n - 1) % n;
         if (pickNow) sJumpTo = sMatches[sCurrent];
      }
      sFocusField = pickNow;   // Enter ends the edit; take the caret straight back
      ImGui::Dummy(ImVec2(0, tok::find_overlay_spacer_h));
      if (sBuf[0] == '\0')
         ImGui::TextDisabled("%s", T("Type to find. Up/Down step, Enter jumps, Esc closes."));
      else if (sMatches.empty())
         ImGui::TextDisabled("%s", T("no matches"));
      else
         ImGui::TextDisabled("%d / %d", sCurrent + 1, (int)sMatches.size());
      if (Keymap::Cancel(true))
         stay = false;
   }
   ImGui::End();
   PanelFrame::PopFloatingStyle();

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
         // Both corners through CanvasToScreen: ed::GetCurrentZoom() is InvScale and must not scale the size.
         const ImVec2 cp = ed::GetNodePosition(gn->NodeId());
         const ImVec2 cs = ed::GetNodeSize(gn->NodeId());
         const ImVec2 p = ed::CanvasToScreen(cp);
         const ImVec2 p2 = ed::CanvasToScreen(ImVec2(cp.x + cs.x, cp.y + cs.y));
         const bool cur = i == sCurrent;
         dl->AddRect(ImVec2(p.x - tok::space_0_75, p.y - tok::space_0_75), ImVec2(p2.x + tok::space_0_75, p2.y + tok::space_0_75),
                     ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, cur ? tok::find_overlay_y_lg : tok::find_overlay_y_sm)), tok::find_overlay_radius, 0, cur ? tok::find_overlay_stroke : tok::find_overlay_outline_stroke);
      }
      dl->PopClipRect();
   }
   else
      Close();
}
}
