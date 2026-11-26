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
#include "app/ui/design/components/StatusDot.h"
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

   // A row's right-click menu. Delete: the row's state goes (and every later one, unless history is non-linear).
   // With a 2D document open: History Source (the History Brush paints from the row) and New Document.
   struct RowMenu
   {
      bool canDelete = false, del = false;
      bool doc = false, source = false, newDoc = false;
      bool isSource = false;
   };

   // One row: returns true when clicked. `dim` for undone entries, `current` for the state the document is in.
   bool Row(const char* id, const std::string& text, bool current, bool dim, float rowH, RowMenu* menu = nullptr)
   {
      const float w = std::max(1.0f, Ui::AvailW());
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(id);
      const bool clicked = UiInteract::Hit("##row", ImVec2(w, rowH));
      const bool hovered = ImGui::IsItemHovered();
      if (menu != nullptr && (menu->canDelete || menu->doc) && MenuParts::BeginContextItem("##rowmenu"))
      {
         if (menu->doc)
         {
            if (MenuParts::Item(T("History Source"), nullptr, menu->isSource))
               menu->source = true;
            if (MenuParts::Item(T("New Document")))
               menu->newDoc = true;
         }
         if (menu->doc && menu->canDelete)
            MenuParts::Separator();
         if (menu->canDelete && MenuParts::DangerItem(T("Delete")))
            menu->del = true;
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
      float x = p.x + tok::space_2;
      if (menu != nullptr && menu->isSource)
      {
         // The History Brush's source, marked as Photoshop's brush column does.
         const float r = tok::drag_bar_dot;
         StatusDot::Marker(dl, ImVec2(x + r, p.y + rowH * tok::opacity_50), r, col);
         x += r + r + tok::space_2;
      }
      dl->AddText(ImVec2(x, p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), col, text.c_str());
      dl->PopClipRect();
      if (hovered)
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      return clicked;
   }

   void ActOn(const RowMenu& m, int undos, int redos, const std::string& label)
   {
      if (m.source)
         SetHistorySourceFromState(undos, redos);
      if (m.newDoc)
         NewDocumentFromState(undos, redos, label);
      if (m.del)
         DeleteHistoryState(undos, redos);
   }

   RowMenu MenuFor(int undos, int redos, bool doc)
   {
      RowMenu m;
      m.doc = doc;
      m.isSource = doc && HistorySourceIsState(undos, redos);
      m.canDelete = true;   // the start row clears it
      return m;
   }

   // History Options, as Photoshop's flyout has them, and Purge.
   void DrawOptionsMenu()
   {
      if (!MenuParts::BeginPopup("##historyoptions"))
         return;
      bool changed = false;
      changed |= MenuParts::Check(T("Automatically Create First Snapshot"), &gHistoryOptions.firstSnapshot);
      changed |= MenuParts::Check(T("Automatically Create New Snapshot When Saving"), &gHistoryOptions.snapshotOnSave);
      changed |= MenuParts::Check(T("Allow Non-Linear History"), &gHistoryOptions.nonLinear);
      changed |= MenuParts::Check(T("Show New Snapshot Dialog by Default"), &gHistoryOptions.newSnapshotDialog);
      changed |= MenuParts::Check(T("Make Layer Visibility Changes Undoable"), &gHistoryOptions.visibilityUndoable);
      MenuParts::Separator();
      if (MenuParts::DangerItem(T("Purge Histories")))
         PurgeHistories();
      if (changed)
         SaveWorkspaceSettings();
      MenuParts::EndPopup();
   }

   void DrawHistoryList()
   {
      const size_t nUndo = gUndoStack.size();
      const size_t nRedo = gRedoStack.size();
      ImGui::TextUnformatted(T("History"));
      // Clear History: one step that sets the rest aside (undo it to get them back). Hidden when it is all there is.
      {
         const bool canClear = nRedo > 0 || nUndo > 1 || (nUndo == 1 && !gUndoStack.back().cleared);
         const char* clearText = L("Clear History");
         const char* optText = L("Options");
         const float pad = tok::space_2 + tok::space_2;
         const float wOpt = ImGui::CalcTextSize(optText).x + pad;
         const float wClear = canClear ? ImGui::CalcTextSize(clearText).x + pad + ImGui::GetStyle().ItemSpacing.x : 0.0f;
         Ui::SameRow();
         Ui::ColumnAt(ImGui::GetCursorPosX() + std::max(0.0f, Ui::AvailW() - wOpt - wClear));
         if (canClear)
         {
            if (ChipButton::Draw(clearText, false, 0.0f, wClear - ImGui::GetStyle().ItemSpacing.x, true))
               ClearHistory();
            Ui::SameRow();
         }
         if (ChipButton::Draw(optText, false, 0.0f, wOpt, true))
            ImGui::OpenPopup("##historyoptions");
         DrawOptionsMenu();
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

      const bool doc = View2DHasDocument();
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
               RowMenu m = MenuFor(0, (int)j + 1, doc);
               const std::string text = RedoLabelAt(j);
               if (Row(("r" + std::to_string(row)).c_str(), text, false, true, rowH, &m))
                  JumpInHistory(0, (int)j + 1);
               ActOn(m, 0, (int)j + 1, text);
            }
            else if (row < nRedo + nUndo)
            {
               const size_t i = row - nRedo; // 0 = the edit the document is in
               const std::string text = (i == 0) ? TopLabel() : UndoLabelAt(i);
               RowMenu m = MenuFor((int)i, 0, doc);
               if (Row(("u" + std::to_string(i)).c_str(), text, i == 0, false, rowH, &m) && i > 0)
                  JumpInHistory((int)i, 0);
               ActOn(m, (int)i, 0, text);
            }
            else
            {
               const bool here = (nUndo == 0);
               RowMenu m = MenuFor((int)nUndo, 0, doc);
               m.canDelete = false;   // the oldest state stays
               const std::string text = gHistoryDropped > 0 ? std::string(T("Earliest kept state")) : std::string(T(gHistoryStartLabel.c_str()));
               if (Row("start", text, here, false, rowH, &m) && !here)
                  JumpInHistory((int)nUndo, 0);
               ActOn(m, (int)nUndo, 0, text);
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
