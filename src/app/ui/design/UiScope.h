// UiScope: layout as named parts, not cursor arithmetic (ui-formalisation Block B, owner sign-off 2026-11-24).
// A panel says what it wants - a gap from the space ladder, the next control on the same row, a form row, a part
// that claims its own drawn size - and never places the cursor or picks a gap number itself. Every gap is a
// `Ui::Space` step (tokens.json base.space), so no call site can pass a float. IDs are untouched: these parts only
// move the cursor, they push no ID.
//
// Pixel rule: each part does exactly what the raw ImGui call it replaces did (same call, same value), so a
// migration is visually a no-op; tools/design/layout_migrate.py only rewrites a call when the value matches a step.
//
//   Ui::Spacer(Ui::Space::S2);              // vertical gap            (was Dummy(ImVec2(0, tok::space_2)))
//   Ui::SameRow();                          // next item on this row, the style's gap   (was SameLine())
//   Ui::SameRow(Ui::Space::S1);             // ... with a ladder gap    (was SameLine(0, tok::space_1))
//   { Ui::Row row(Ui::Space::S1); A(); row.Next(); B(); }   // a row of items
//   { Ui::Stack stack(Ui::Space::S1); ... } // rows below each other with a ladder gap (ItemSpacing.y)
//   Ui::FillNext();                         // next control fills the row  (was SetNextItemWidth(avail.x))
//   Ui::Claim(size);                        // a custom-drawn part claims its rect (was Dummy(size))
//   Ui::AlignRight(w); { Ui::Aside a(top, w); ... }   // pinned right column / right block beside a left block
//   { Ui::FormGrid grid; Ui::FormRow(L("Gain")); control(); }   // label column + control (in FormParts.h)
#pragma once
#include "imgui.h"
#include "app/ui/design/Tokens.gen.h"
#include <cstdint>

namespace Ui
{
   // The space ladder, by name: S2 is tok::space_2 (8 px), S0_5 is tok::space_0_5. One step per tokens.json entry;
   // the static_asserts below keep the two in lockstep.
   enum class Space : uint8_t { None, S0_25, S0_5, S0_75, S1, S1_25, S1_5, S2, S2_5, S3, S3_5, S4, S5 };

   constexpr float Px(Space s)
   {
      switch (s)
      {
      case Space::None: return 0.0f;
      case Space::S0_25: return tok::space_0_25;
      case Space::S0_5: return tok::space_0_5;
      case Space::S0_75: return tok::space_0_75;
      case Space::S1: return tok::space_1;
      case Space::S1_25: return tok::space_1_25;
      case Space::S1_5: return tok::space_1_5;
      case Space::S2: return tok::space_2;
      case Space::S2_5: return tok::space_2_5;
      case Space::S3: return tok::space_3;
      case Space::S3_5: return tok::space_3_5;
      case Space::S4: return tok::space_4;
      case Space::S5: return tok::space_5;
      }
      return 0.0f;
   }
   static_assert(Px(Space::S0_25) < Px(Space::S0_5) && Px(Space::S0_5) < Px(Space::S0_75) && Px(Space::S0_75) < Px(Space::S1) &&
                 Px(Space::S1) < Px(Space::S1_25) && Px(Space::S1_25) < Px(Space::S1_5) && Px(Space::S1_5) < Px(Space::S2) &&
                 Px(Space::S2) < Px(Space::S2_5) && Px(Space::S2_5) < Px(Space::S3) && Px(Space::S3) < Px(Space::S3_5) &&
                 Px(Space::S3_5) < Px(Space::S4) && Px(Space::S4) < Px(Space::S5),
                 "the space ladder is ordered; a new step goes into Space in order");

   // A vertical gap of one ladder step below the last item.
   inline void Spacer(Space s) { ImGui::Dummy(ImVec2(0.0f, Px(s))); }

   // The next item goes on the same row: after the style's item gap, or after a ladder step.
   inline void SameRow() { ImGui::SameLine(); }
   inline void SameRow(Space gap) { ImGui::SameLine(0.0f, Px(gap)); }

   // A row of items: Next() before every item after the first.
   struct Row
   {
      explicit Row(Space gap) : gap(gap), styled(false) {}
      Row() : gap(Space::None), styled(true) {}
      void Next() const { styled ? SameRow() : SameRow(gap); }
      Space gap;
      bool styled;
   };

   // Items below each other with a ladder gap between rows (the style's ItemSpacing.y while the scope lives).
   struct Stack
   {
      explicit Stack(Space gap)
      {
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, Px(gap)));
      }
      ~Stack() { ImGui::PopStyleVar(); }
      Stack(const Stack&) = delete;
      Stack& operator=(const Stack&) = delete;
   };

   // Width queries for parts that size themselves to the row (no placement).
   inline float AvailW() { return ImGui::GetContentRegionAvail().x; }
   inline float AvailH() { return ImGui::GetContentRegionAvail().y; }
   inline ImVec2 Avail() { return ImGui::GetContentRegionAvail(); }

   // The next control fills what is left of the row.
   inline void FillNext() { ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); }

   // The next control fills the row but leaves `trailingW` for the parts drawn after it (their own sizes).
   inline void FillNextLeaving(float trailingW) { ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - trailingW); }

   // The next item on this line sits so that an item `itemW` wide ends at the content edge (a right-aligned number,
   // a trailing button). A pinned column does not move when the text before it changes width.
   inline void AlignRight(float itemW) { ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - itemW); }

   // A block `w` wide at the content's right edge, top-aligned with `top` (a GetCursorPosY() taken before the left
   // block was drawn). When the scope ends the cursor continues below whichever block is taller.
   struct Aside
   {
      Aside(float top, float w) : leftEnd(ImGui::GetCursorPosY())
      {
         ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - w, top));
      }
      ~Aside() { ImGui::SetCursorPosY(leftEnd > ImGui::GetCursorPosY() ? leftEnd : ImGui::GetCursorPosY()); }
      Aside(const Aside&) = delete;
      Aside& operator=(const Aside&) = delete;
      float leftEnd;
   };

   // A part drawn by hand (draw list) claims its rect so layout flows past it. Its size is the part's own, not a gap.
   inline void Claim(const ImVec2& size) { ImGui::Dummy(size); }

}
