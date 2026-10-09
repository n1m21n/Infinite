// History: the undo and redo stacks as a list of named edits, docked on the right of the canvas.
// Newest on top. Rows above the highlighted one are undone (dimmed) and stay until a new edit; rows below it are
// the past. Clicking a row moves the document to the state right after that edit, by repeating Undo or Redo.
// The labels live on UndoEntry and are never written into a patch.
#include "app/AppShared.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/PanelFrame.h"

namespace app
{
namespace
{
   constexpr float kMinWidth = 220.0f;

   // The top row's label is derived from the live graph, so it is refreshed on a short timer, not every frame.
   const std::string& TopLabel()
   {
      static std::string sLabel;
      static double sAt = -1.0;
      static size_t sSize = (size_t)-1;
      const double now = ImGui::GetTime();
      if (sSize != gUndoStack.size() || now - sAt > 0.4)
      {
         sLabel = UndoLabelAt(0);
         sAt = now;
         sSize = gUndoStack.size();
      }
      return sLabel;
   }

   // One row: returns true when clicked. `dim` for undone entries, `current` for the state the document is in.
   bool Row(const char* id, const std::string& text, bool current, bool dim, float rowH)
   {
      const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(id);
      const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, rowH));
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (current)
         dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_Header, 0.30f), tok::radius_field);
      else if (hovered)
         dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_FrameBgHovered), tok::radius_field);
      const ImU32 col = ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
      dl->PushClipRect(p, ImVec2(p.x + w, p.y + rowH), true);
      dl->AddText(ImVec2(p.x + tok::space_2, p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), col, text.c_str());
      dl->PopClipRect();
      if (hovered)
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      return clicked;
   }

   void DrawHistoryList()
   {
      const size_t nUndo = gUndoStack.size();
      const size_t nRedo = gRedoStack.size();
      ImGui::TextUnformatted(T("History"));
      ImGui::PushTextWrapPos(0.0f);
      if (nUndo >= kMaxUndoDepth)
         ImGui::TextDisabled(T("Showing the last %d edits; older ones are dropped."), (int)kMaxUndoDepth);
      else
         ImGui::TextDisabled("%s", T("Click an edit to go back or forward to it."));
      ImGui::PopTextWrapPos();
      ImGui::Spacing();

      if (nUndo == 0 && nRedo == 0)
      {
         EmptyState::DrawInWindow(T("No edits yet"), T("Changes you make will be listed here"));
         return;
      }

      ImGui::BeginChild("##historyrows", ImVec2(0, 0), false);
      const float rowH = ImGui::GetTextLineHeight() + tok::space_2 * 1.5f;
      // Rows top to bottom: undone (farthest first), then the current edit, the past, and the start.
      const size_t total = nRedo + nUndo + 1;
      ImGuiListClipper clip;
      clip.Begin((int)total, rowH);
      while (clip.Step())
      {
         for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++)
         {
            const size_t row = (size_t)r;
            if (row < nRedo)
            {
               const size_t j = nRedo - 1 - row; // 0 = nearest to the present
               if (Row(("r" + std::to_string(row)).c_str(), RedoLabelAt(j), false, true, rowH))
                  JumpInHistory(0, (int)j + 1);
            }
            else if (row < nRedo + nUndo)
            {
               const size_t i = row - nRedo; // 0 = the edit the document is in
               const std::string text = (i == 0) ? TopLabel() : UndoLabelAt(i);
               if (Row(("u" + std::to_string(i)).c_str(), text, i == 0, false, rowH) && i > 0)
                  JumpInHistory((int)i, 0);
            }
            else
            {
               const bool here = (nUndo == 0);
               if (Row("start", nUndo >= kMaxUndoDepth ? T("Earliest kept state") : T("Start"), here, false, rowH) && !here)
                  JumpInHistory((int)nUndo, 0);
            }
         }
      }
      ImGui::EndChild();
   }
}

void DrawHistoryDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   // Sit beside whatever was drawn last, like the other right-docked panels.
   {
      const ImVec2 prevMax = ImGui::GetItemRectMax();
      ImGui::SetCursorScreenPos(ImVec2(prevMax.x, ImGui::GetItemRectMin().y));
   }
   ImGui::BeginChild(id, size, false);
   const ImVec2 inner = ImGui::GetContentRegionAvail();

   ImGui::InvisibleButton("##historygrip", ImVec2(kGrip, std::max(1.0f, inner.y)));
   DrawPanelSeam(true, true);
   if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
   if (ImGui::IsItemActive())
      gHistoryWidth = std::max(kMinWidth, gHistoryWidth - ImGui::GetIO().MouseDelta.x);
   {
      const ImVec2 gripMax = ImGui::GetItemRectMax();
      ImGui::SetCursorScreenPos(ImVec2(gripMax.x + ImGui::GetStyle().ItemSpacing.x, ImGui::GetItemRectMin().y));
   }

   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize(std::max(1.0f, inner.x - kGrip - gap.x), inner.y);
   PanelFrame::Insets in;
   in.l = 0.0f;
   PushDockedPanelStyle(/*isChild=*/true);
   PanelFrame::BeginCard("##historypanelcontent", cardSize, in);
   PopDockedPanelStyle();
   DrawHistoryList();
   PanelFrame::EndCard();

   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}
}
