// ReorderDrag: the one drag-to-reorder motion every list, card stack, tab strip and rail in the app uses.
//   - The thing itself is the handle: drag a card, row, chip or tile anywhere on it (no grip icon).
//   - While dragging, a ghost of the item (its size, its label) follows the pointer from where it was grabbed,
//     and the item left behind is dimmed in place.
//   - Over a target, an accent insertion bar shows where it will land (before or after), or an outline box when it
//     will go into the target (a group, a clip).
// Call Source right after the item's own button, then Target on the same item. Payload types stay per list.
#pragma once
#include <algorithm>
#include <cstring>
#include "app/AppShared.h"
#include "app/ui/design/Tokens.gen.h"

namespace ReorderDrag
{
   enum class Edge
   {
      Before,
      After,
      Into,
   };

   // The payload being dragged right now, when it is of this type (lets a target pick before/after or refuse).
   template <class T> inline const T* Peek(const char* type)
   {
      const ImGuiPayload* p = ImGui::GetDragDropPayload();
      if (p == nullptr || !p->IsDataType(type) || p->DataSize != (int)sizeof(T))
         return nullptr;
      return static_cast<const T*>(p->Data);
   }

   // True while the pointer is in the last item's lower (vertical) or right half.
   inline bool PointerAfter(bool vertical = true)
   {
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      const ImVec2 m = ImGui::GetIO().MousePos;
      return vertical ? m.y >= (mn.y + mx.y) * 0.5f : m.x >= (mn.x + mx.x) * 0.5f;
   }

   inline ImU32 AccentCol() { return ImGui::GetColorU32(app::AccentEmphasisSelected()); }

   // The insertion bar: accent, 2 pt, round ends, drawn on top of the list.
   inline void Bar(ImVec2 a, ImVec2 b)
   {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      dl->AddLine(a, b, AccentCol(), 2.0f);
      dl->AddCircleFilled(a, 3.0f, AccentCol());
      dl->AddCircleFilled(b, 3.0f, AccentCol());
   }

   // The "goes into this one" outline.
   inline void Box(ImVec2 mn, ImVec2 mx) { ImGui::GetForegroundDrawList()->AddRect(mn, mx, AccentCol(), tok::radius_control, 0, 2.0f); }

   // Makes the last item draggable. `label` is drawn on the ghost (nullptr: an empty ghost, for icon tiles).
   // Returns true while this item is being dragged.
   inline bool Source(const char* type, const void* data, size_t bytes, const char* label, ImGuiDragDropFlags flags = 0)
   {
      if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip | flags))
         return false;
      ImGui::SetDragDropPayload(type, data, bytes, ImGuiCond_Once);
      ImGui::EndDragDropSource();
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      const ImGuiIO& io = ImGui::GetIO();
      // The item left behind: dimmed in place.
      ImGui::GetWindowDrawList()->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.6f), tok::radius_control);
      // The ghost: the item's size, held where it was grabbed.
      const ImVec2 size(std::min(mx.x - mn.x, 320.0f), std::max(mx.y - mn.y, ImGui::GetFrameHeight()));
      ImVec2 grab(io.MouseClickedPos[0].x - mn.x, io.MouseClickedPos[0].y - mn.y);
      grab.x = std::clamp(grab.x, 0.0f, size.x);
      grab.y = std::clamp(grab.y, 0.0f, size.y);
      const ImVec2 g0(io.MousePos.x - grab.x, io.MousePos.y - grab.y), g1(g0.x + size.x, g0.y + size.y);
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      fg->AddRectFilled(ImVec2(g0.x + 2.0f, g0.y + 3.0f), ImVec2(g1.x + 2.0f, g1.y + 3.0f), IM_COL32(0, 0, 0, 60), tok::radius_control);
      fg->AddRectFilled(g0, g1, ImGui::GetColorU32(ImGuiCol_PopupBg, 0.94f), tok::radius_control);
      fg->AddRect(g0, g1, AccentCol(), tok::radius_control, 0, 1.0f);
      if (label != nullptr && label[0] != '\0')
      {
         fg->PushClipRect(g0, g1, true);
         fg->AddText(ImVec2(g0.x + tok::space_2, (g0.y + g1.y - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label);
         fg->PopClipRect();
      }
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      return true;
   }

   // Makes the last item a drop target. Draws the bar on its `edge` side (or the box for Into) while a payload of
   // `type` hovers it; returns the payload on the drop. `span` (min, max) widens the bar to the whole card or row
   // when the item is only part of it.
   inline const ImGuiPayload* Target(const char* type, Edge edge, bool vertical = true, const ImVec2* span = nullptr)
   {
      if (!ImGui::BeginDragDropTarget())
         return nullptr;
      const ImGuiPayload* out = nullptr;
      const ImGuiPayload* peek = ImGui::GetDragDropPayload();
      if (peek != nullptr && peek->IsDataType(type))
      {
         const ImVec2 mn = span != nullptr ? span[0] : ImGui::GetItemRectMin();
         const ImVec2 mx = span != nullptr ? span[1] : ImGui::GetItemRectMax();
         if (edge == Edge::Into)
            Box(mn, mx);
         else if (vertical)
         {
            const float y = edge == Edge::After ? mx.y + 1.0f : mn.y - 1.0f;
            Bar(ImVec2(mn.x + 2.0f, y), ImVec2(mx.x - 2.0f, y));
         }
         else
         {
            const float x = edge == Edge::After ? mx.x + 1.0f : mn.x - 1.0f;
            Bar(ImVec2(x, mn.y + 2.0f), ImVec2(x, mx.y - 2.0f));
         }
         out = ImGui::AcceptDragDropPayload(type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
      }
      ImGui::EndDragDropTarget();
      return out;
   }

   // For "move from slot a to slot b" lists: the bar goes after the target when moving down, before when moving up.
   inline Edge SlotEdge(int from, int to) { return from < to ? Edge::After : Edge::Before; }
}
