// Piano Roll panel (docs/plans/piano-roll/README.md §3, §4, §9): docked bottom or top like the Timeline, one page per
// MIDI lane. A page edits the lane's clips and notes through the Arrange note ops; the linked Piano Roll node plays
// them. Edits are timeline undo entries; page add and delete touch a node too, so they are patch undo steps.
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/DropdownField.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "arrange/ArrangeModel.h"
#include "core/Transport.h"
#include "nodes/PianoRollNode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace app
{

namespace
{
   constexpr float kMinHeight = 240.0f;
   constexpr float kIconW = 30.0f;

   struct State
   {
      uint64_t lane = 0;         // the page on show
      uint64_t renameLane = 0;   // a tab being renamed
      char renameBuf[64] = {};
      bool renameFocus = false;
      bool pickOnCanvas = false; // the link chip's "Pick on canvas"
      uint64_t lastSelectedNode = 0;
   };

   State& S()
   {
      static State s;
      return s;
   }

   std::vector<Arrange::Lane*> Pages()
   {
      std::vector<Arrange::Lane*> out;
      for (Arrange::Lane& l : gArrange.lanes)
         if (l.type == Arrange::kLaneMidi)
            out.push_back(&l);
      return out;
   }

   Arrange::Lane* Page()
   {
      Arrange::Lane* l = S().lane != 0 ? Arrange::FindLane(gArrange, S().lane) : nullptr;
      return l != nullptr && l->type == Arrange::kLaneMidi ? l : nullptr;
   }

   GraphNode* LinkedNode(const Arrange::Lane& l)
   {
      return l.noteNodeUid != 0 ? FindNodeByUid(l.noteNodeUid) : nullptr;
   }

   std::string NodeLabel(const GraphNode& gn)
   {
      auto* n = dynamic_cast<PianoRollNode*>(gn.node.get());
      if (n != nullptr && !n->laneName.empty())
         return n->laneName + "##" + std::to_string(gn.uid);
      return std::string(T("Piano Roll")) + " " + std::to_string(gn.index) + "##" + std::to_string(gn.uid);
   }

   // A new page: the lane plus a linked node at the canvas view's centre, one patch undo step.
   void AddPage()
   {
      PushUndoCheckpoint("Add Piano Roll Page");
      GraphNode* fresh = SpawnNode("Piano Roll", "Notes", gViewCenterCanvas.x, gViewCenterCanvas.y);
      if (fresh == nullptr)
         return;
      const uint64_t lane = Arrange::AddLane(gArrange, Arrange::kLaneMidi);
      if (Arrange::Lane* l = Arrange::FindLane(gArrange, lane))
         l->name = Arrange::UniqueLaneName(gArrange, "Part", Arrange::kLaneMidi);
      Arrange::LinkLane(gArrange, lane, fresh->uid);
      ArrangeCommitEdit();
      S().lane = lane;
   }

   void DeletePage(uint64_t lane)
   {
      PushUndoCheckpoint("Delete Piano Roll Page");
      Arrange::RemoveLane(gArrange, lane);
      ArrangeCommitEdit();
      if (S().lane == lane)
         S().lane = 0;
   }

   void DrawTabs()
   {
      State& s = S();
      const std::vector<Arrange::Lane*> pages = Pages();
      if (s.lane == 0 && !pages.empty())
         s.lane = pages.front()->id;
      for (size_t i = 0; i < pages.size(); i++)
      {
         Arrange::Lane* l = pages[i];
         const uint64_t id = l->id;
         ImGui::PushID((int)id);
         if (i > 0)
            FormParts::Inline(0.0f, tok::space_1);
         if (s.renameLane == id)
         {
            if (s.renameFocus)
               ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(120.0f);
            const bool done = FieldWell::InputText("##tabrename", s.renameBuf, sizeof s.renameBuf,
                                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool lost = !s.renameFocus && !ImGui::IsItemActive();
            s.renameFocus = false;
            if (done || lost)
            {
               const std::string name = s.renameBuf;
               s.renameLane = 0;
               if (!name.empty() && name != l->name)
                  ArrangeEdit([&] {
                     l->name = name;
                     gArrange.revision++;
                  });
            }
            ImGui::PopID();
            continue;
         }
         const std::string label = l->name + "##tab";
         if (ChipButton::Draw(label.c_str(), s.lane == id, tok::tile))
            s.lane = id;
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            s.renameLane = id;
            s.renameFocus = true;
            std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", l->name.c_str());
         }
         // Drag a tab past its neighbour: the lane order is the tab order.
         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, tok::space_2))
         {
            const float mx = ImGui::GetIO().MousePos.x;
            const float x0 = ImGui::GetItemRectMin().x, x1 = ImGui::GetItemRectMax().x;
            const int dir = mx > x1 ? 1 : mx < x0 ? -1 : 0;
            const int to = (int)i + dir;
            if (dir != 0 && to >= 0 && to < (int)pages.size())
            {
               int index = 0;
               for (int k = 0; k < (int)gArrange.lanes.size(); k++)
                  if (gArrange.lanes[(size_t)k].id == pages[(size_t)to]->id)
                     index = k;
               ArrangeEdit([&] { Arrange::ReorderLane(gArrange, id, index); });
               ImGui::PopID();
               break; // the lane pointers moved
            }
         }
         if (ImGui::BeginPopupContextItem("##tabmenu"))
         {
            if (MenuParts::Item(L("Rename")))
            {
               s.renameLane = id;
               s.renameFocus = true;
               std::snprintf(s.renameBuf, sizeof s.renameBuf, "%s", l->name.c_str());
            }
            if (MenuParts::Item(L("Delete Page")))
            {
               DeletePage(id);
               ImGui::EndPopup();
               ImGui::PopID();
               break;
            }
            ImGui::EndPopup();
         }
         ImGui::PopID();
      }
      if (!pages.empty())
         FormParts::Inline(0.0f, tok::space_1);
      if (ActionButton::Draw("##pageAdd", ImVec2(kIconW, tok::tile)))
         AddPage();
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Add a page and a Piano Roll node"));
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tok::icon_md,
                  ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::Plus);
   }

   void DrawLinkChip()
   {
      State& s = S();
      Arrange::Lane* page = Page();
      if (page == nullptr)
         return;
      const GraphNode* linked = LinkedNode(*page);
      std::string face;
      if (s.pickOnCanvas)
         face = T("Click a Piano Roll node");
      else if (linked != nullptr)
         face = NodeLabel(*linked);
      else
         face = T("Link a Piano Roll node");
      const float w = 190.0f;
      ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - w));
      const std::string caption = face.substr(0, face.find("##")) + "##pageLink";
      if (DropdownField::Draw(caption.c_str(), ImVec2(w, 0.0f)))
         ImGui::OpenPopup("##pageLinkMenu");
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("The Piano Roll node that plays this page"));
      if (!MenuParts::BeginPopup("##pageLinkMenu"))
         return;
      for (GraphNode& gn : gNodes)
      {
         if (dynamic_cast<PianoRollNode*>(gn.node.get()) == nullptr || gn.uid == page->noteNodeUid)
            continue;
         bool elsewhere = false;
         for (const Arrange::Lane& l : gArrange.lanes)
            elsewhere = elsewhere || (l.type == Arrange::kLaneMidi && l.noteNodeUid == gn.uid);
         std::string label = NodeLabel(gn);
         if (elsewhere)
            label = std::string(T("Move the link here:")) + " " + label;
         if (MenuParts::Item(label.c_str()))
         {
            const uint64_t uid = gn.uid, lane = page->id;
            ArrangeEdit([&] { Arrange::LinkLane(gArrange, lane, uid); });
         }
      }
      if (MenuParts::Item(L("Pick on canvas")))
         s.pickOnCanvas = true;
      if (linked != nullptr && MenuParts::Item(L("Unlink")))
      {
         const uint64_t lane = page->id;
         ArrangeEdit([&] { Arrange::LinkLane(gArrange, lane, 0); });
      }
      MenuParts::EndPopup();
   }

   // Selecting a Piano Roll node jumps to its page, or finishes "Pick on canvas".
   void FollowCanvasSelection()
   {
      State& s = S();
      uint64_t uid = 0;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<PianoRollNode*>(gn.node.get()) != nullptr && ed::IsNodeSelected(gn.NodeId()))
         {
            uid = gn.uid;
            break;
         }
      if (uid == s.lastSelectedNode)
         return;
      s.lastSelectedNode = uid;
      if (uid == 0)
         return;
      if (s.pickOnCanvas)
      {
         s.pickOnCanvas = false;
         if (Page() != nullptr)
         {
            const uint64_t lane = s.lane;
            ArrangeEdit([&] { Arrange::LinkLane(gArrange, lane, uid); });
         }
         return;
      }
      for (const Arrange::Lane& l : gArrange.lanes)
         if (l.type == Arrange::kLaneMidi && l.noteNodeUid == uid)
            s.lane = l.id;
   }

   void DrawDockMenu()
   {
      const ImVec2 mn = ImGui::GetWindowPos(), mx(mn.x + ImGui::GetWindowSize().x, mn.y + 2.0f * tok::tile + tok::space_3);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(mn, mx) && !ImGui::IsAnyItemHovered() &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
         ImGui::OpenPopup("##pianoDockMenu");
      if (MenuParts::BeginPopup("##pianoDockMenu"))
      {
         if (MenuParts::SubMenu(L("Dock Position")))
         {
            const bool top = gPianoRollDock == 3;
            if (MenuParts::Item(L("Piano Roll at Bottom"), nullptr, !top) && top)
               gPianoRollDock = 0;
            if (MenuParts::Item(L("Piano Roll at Top"), nullptr, top) && !top)
               gPianoRollDock = 3;
            ImGui::EndMenu();
         }
         ImGui::Dummy(ImVec2(0.0f, tok::space_1));
         if (MenuParts::Item(L("Close Piano Roll")))
            gPianoRollOpen = false;
         MenuParts::EndPopup();
      }
   }

   void DrawPanel()
   {
      FollowCanvasSelection();
      DrawTabs();
      DrawLinkChip();
      DrawDockMenu();
      Arrange::Lane* page = Page();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      if (page == nullptr || page->clips.empty())
      {
         const char* text = page == nullptr ? T("Add a page to start writing notes") : T("Draw a clip, drop a .mid, or record");
         const ImVec2 ts = ImGui::CalcTextSize(text);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (avail.x - ts.x) * 0.5f, p.y + (avail.y - ts.y) * 0.5f),
                                             ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
      }
   }
}

void PianoRollOpenPage(uint64_t laneId)
{
   gPianoRollOpen = true;
   S().lane = laneId;
}

void DrawPianoRollDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool gripFirst = gPianoRollDock != 3; // bottom: the grip sits on top
   ImGui::BeginChild(id, size, false);
   gPianoRollRectMin = ImGui::GetWindowPos();
   gPianoRollRectMax = ImVec2(gPianoRollRectMin.x + ImGui::GetWindowSize().x, gPianoRollRectMin.y + ImGui::GetWindowSize().y);
   gPianoRollClaimedKeys = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##pianogrip", ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(false, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         gPianoRollHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
         gPianoRollHeight = std::max(kMinHeight, gPianoRollHeight);
      }
   };
   if (gripFirst)
      grip();
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
   PanelFrame::Insets in;
   if (gripFirst)
      in.t = 0.0f;
   else
      in.b = 0.0f;
   PanelFrame::BeginCard("##pianopanelcontent", cardSize, in);
   DrawPanel();
   PanelFrame::EndCard();
   if (!gripFirst)
      grip();
   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}
}
