// History: the undo and redo stacks as a list of named edits, docked on the right of the canvas.
// Newest on top. Rows above the highlighted one are undone (dimmed) and stay until a new edit; rows below it are
// the past. Clicking a row moves the document to the state right after that edit, by repeating Undo or Redo.
// The labels live on UndoEntry and are never written into a patch.
#include "app/AppShared.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/UiScope.h"

namespace app
{
namespace
{
   constexpr float kMinWidth = tok::history_min_width;

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
   // `del`, when given, gets a right-click Delete: the row's state and every later one go.
   bool Row(const char* id, const std::string& text, bool current, bool dim, float rowH, bool* del = nullptr)
   {
      const float w = std::max(1.0f, Ui::AvailW());
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(id);
      const bool clicked = UiInteract::Hit("##row", ImVec2(w, rowH));
      const bool hovered = ImGui::IsItemHovered();
      if (del != nullptr && MenuParts::BeginContextItem("##rowmenu"))
      {
         if (MenuParts::DangerItem(T("Delete")))
            *del = true;
         MenuParts::EndPopup();
      }
      ImGui::PopID();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (current)
         dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_Header, tok::history_color_alpha_mul), tok::radius_control);
      else if (hovered)
         dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_FrameBgHovered), tok::radius_control);
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
      // Clear History: one step that sets the rest aside (undo it to get them back). Hidden when it is all there is.
      if (nRedo > 0 || nUndo > 1 || (nUndo == 1 && !gUndoStack.back().cleared))
      {
         const char* clearText = L("Clear History");
         const float w = ImGui::CalcTextSize(clearText).x + tok::space_2 + tok::space_2;
         Ui::SameRow();
         Ui::ColumnAt(ImGui::GetCursorPosX() + std::max(0.0f, Ui::AvailW() - w));
         if (ChipButton::Draw(clearText, false, 0.0f, w, true))
            ClearHistory();
      }
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
      // Same 24 pt pitch as every menu and dropdown list (MenuParts::kRow); rows butt against each other.
      const float pitch = MenuParts::kRow;
      const float rowH = pitch - ImGui::GetStyle().ItemSpacing.y;
      // Rows top to bottom: undone (farthest first), then the current edit, the past, and the start.
      const size_t total = nRedo + nUndo + 1;
      ImGuiListClipper clip;
      clip.Begin((int)total, pitch);
      while (clip.Step())
      {
         for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++)
         {
            const size_t row = (size_t)r;
            if (row < nRedo)
            {
               const size_t j = nRedo - 1 - row; // 0 = nearest to the present
               bool del = false;
               if (Row(("r" + std::to_string(row)).c_str(), RedoLabelAt(j), false, true, rowH, &del))
                  JumpInHistory(0, (int)j + 1);
               if (del)
                  DeleteHistoryState(0, (int)j + 1);
            }
            else if (row < nRedo + nUndo)
            {
               const size_t i = row - nRedo; // 0 = the edit the document is in
               const std::string text = (i == 0) ? TopLabel() : UndoLabelAt(i);
               bool del = false;
               if (Row(("u" + std::to_string(i)).c_str(), text, i == 0, false, rowH, &del) && i > 0)
                  JumpInHistory((int)i, 0);
               if (del)
                  DeleteHistoryState((int)i, 0);
            }
            else
            {
               const bool here = (nUndo == 0);
               if (Row("start", nUndo >= kMaxUndoDepth ? T("Earliest kept state") : T("Start"), here, false, rowH) && !here)
                  JumpInHistory((int)nUndo, 0);
            }
         }
      }
      EndChildSafe();
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
   const ImVec2 inner = Ui::Avail();

   UiInteract::Hit("##historygrip", ImVec2(kGrip, std::max(1.0f, inner.y)));
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
   EndChildSafe();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}
}
