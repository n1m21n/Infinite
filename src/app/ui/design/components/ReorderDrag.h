// ReorderDrag: the one drag-to-reorder motion every list, card stack, tab strip and rail in the app uses.
//   - The thing itself is the handle: drag a card, row, chip or tile anywhere on it (no grip icon).
//   - Flat lists (cards, chips, tiles, strips) shift live, as Figma's layers, macOS toolbars and every sortable
//     list do: while you drag, the others slide aside (a short glide, not a jump) to open a gap where the item will
//     land; the ghost follows the pointer from where it was grabbed; release drops it into the gap, Esc cancels.
//     Nothing in the data moves until the drop, so one drag is one undo step.
//   - Icon grids (a tool palette) swap instead: dropping on a cell trades places with the icon there, so only those
//     two move (a grid that reflows sends every icon in between round the row ends, which reads as jitter).
//   - Trees (layer groups, outliner parents) keep the insertion bar / "into" box (Source + Target), because there a
//     drop can also mean "inside this one".
// Flat list use:
//     for (int i : ReorderDrag::Order("TYPE", n, vertical)) { ReorderDrag::Flow(i); <draw item i>; ReorderDrag::Item(i, ...); }
//     if (const ReorderDrag::Move m = ReorderDrag::End()) <move m.from to m.to>;
// Flow is for items laid out by the cursor; an item placed at an explicit position uses Glide on that position.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "imgui_internal.h"
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

   namespace detail
   {
      // A copy of a delivered payload that outlives ImGui's ClearDragDrop.
      inline const ImGuiPayload* Keep(const ImGuiPayload& p)
      {
         static ImGuiPayload copy;
         static unsigned char bytes[64];
         copy = p;
         const int n = std::min(p.DataSize, (int)sizeof(bytes));
         std::memcpy(bytes, p.Data, (size_t)n);
         copy.Data = bytes;
         copy.DataSize = n;
         return &copy;
      }
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
         // EndDragDropTarget clears the payload right after delivery (Data becomes null), so the drop is copied out first.
         if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect); p != nullptr && p->Data != nullptr)
            out = detail::Keep(*p);
      }
      ImGui::EndDragDropTarget();
      return out;
   }

   // For "move from slot a to slot b" lists: the bar goes after the target when moving down, before when moving up.
   inline Edge SlotEdge(int from, int to) { return from < to ? Edge::After : Edge::Before; }

   // ---- flat lists: live shift ---------------------------------------------------------------------------------
   struct Move
   {
      int from = -1, to = -1;
      bool swap = false;   // trade places (icon grids) rather than take `from` out and insert it at `to`
      explicit operator bool() const { return from >= 0 && to >= 0 && from != to; }
   };

   namespace detail
   {
      struct List
      {
         std::string type;
         bool vertical = true;
         bool swap = false;
         int n = 0;
         int from = -1, to = -1;   // dragging: data index held, slot it would land in
         int frame = -1;           // last frame the held item was dragged
         double settle = 0.0;      // glides run until this time (a little after a drop)
         ImVec2 grab{ 0, 0 };
         std::vector<int> order;
         std::vector<ImRect> rects;   // this frame's other items, natural (un-glided) rects, drawn order
         std::vector<ImRect> slots;   // every item's natural rect in drawn order: the slots (a grid's cells do not move)
         std::unordered_map<int, ImVec2> last, off;   // per data index: natural pos last frame, glide offset
         float lastT = 0.0f;
         ImVec2 applied{ 0, 0 };      // Flow: the glide the last item was drawn with
         float lineY = 0.0f;          // Flow: the glide of the first item on the current line (ImGui's line top)
         int glideFrame = -1;
      };
      inline std::unordered_map<ImGuiID, List>& Lists()
      {
         static std::unordered_map<ImGuiID, List> m;
         return m;
      }
      inline std::vector<ImGuiID>& Stack()
      {
         static std::vector<ImGuiID> s;
         return s;
      }
      inline List& Top() { return Lists()[Stack().back()]; }
      inline bool Dragging(const List& l) { return l.from >= 0 && l.from < l.n; }
      // Data indices in drawn order once `from` lands on `to`.
      inline std::vector<int> Perm(int n, int from, int to, bool swap)
      {
         std::vector<int> o;
         for (int i = 0; i < n; i++)
            o.push_back(i);
         if (from < 0 || from >= n)
            return o;
         to = std::clamp(to, 0, n - 1);
         if (swap)
            std::swap(o[from], o[to]);
         else
         {
            o.erase(o.begin() + from);
            o.insert(o.begin() + to, from);
         }
         return o;
      }

      // The glide: an item whose natural place moved keeps where it was drawn and eases into the new place
      // (about 90 ms). Only while a drag runs or settles, so scrolling or a relayout never animates.
      inline ImVec2 Glide(List& l, int index, ImVec2 natural)
      {
         const double now = ImGui::GetTime();
         const bool live = Dragging(l) || now < l.settle;
         auto it = l.last.find(index);
         ImVec2& o = l.off[index];
         if (!live)
            o = ImVec2(0, 0);
         else if (it != l.last.end())
         {
            o.x += it->second.x - natural.x;
            o.y += it->second.y - natural.y;
            // Time constant: half the focus motion token (an item settles in about the on-motion time).
            const float k = std::exp(-ImGui::GetIO().DeltaTime / (tok::motion_focus * 0.5f / 1000.0f));
            o.x *= k;
            o.y *= k;
            if (std::fabs(o.x) < 0.5f)
               o.x = 0.0f;
            if (std::fabs(o.y) < 0.5f)
               o.y = 0.0f;
         }
         l.last[index] = natural;
         return o;
      }
   }

   // Starts a flat list of `n` items (call where the list begins, in the same ID scope as End). Returns the order to
   // draw the items in: the data order, or while one is held, the order it would have if dropped now.
   // `swap`: an icon grid, where a drop trades places (see the top).
   inline const std::vector<int>& Order(const char* type, int n, bool vertical = true, bool swap = false)
   {
      const ImGuiID id = ImGui::GetID(type);
      detail::Stack().push_back(id);
      detail::List& l = detail::Lists()[id];
      l.type = type;
      l.vertical = vertical;
      l.swap = swap;
      l.n = n;
      l.rects.clear();
      l.slots.clear();
      l.applied = ImVec2(0, 0);
      l.lineY = 0.0f;
      if (!detail::Dragging(l))
         l.from = l.to = -1;
      l.order = detail::Perm(n, l.from, l.to, swap);
      return l.order;
   }

   // Before an item laid out by the cursor: moves the cursor by the item's glide.
   inline void Flow(int index)
   {
      detail::List& l = detail::Top();
      // ImGui placed the cursor after items that were drawn glided: take that back out to find the natural place.
      // On the same line, x follows the last item and y the line's top (its first item); on a new line, y follows
      // the last line's top. So a wrapped grid row or the next card never inherits a neighbour's motion.
      const bool sameLine = ImGui::GetCurrentWindowRead()->DC.IsSameLine;
      const ImVec2 cur = ImGui::GetCursorPos();
      const ImVec2 natural(cur.x - (sameLine ? l.applied.x : 0.0f), cur.y - l.lineY);
      ImVec2 o = detail::Glide(l, index, natural);
      if (l.vertical)
         o.x = 0.0f;
      if (o.x != cur.x - natural.x || o.y != cur.y - natural.y)
         ImGui::SetCursorPos(ImVec2(natural.x + o.x, natural.y + o.y));
      l.applied = o;
      if (!sameLine)
         l.lineY = o.y;
   }

   // An item placed at an explicit position (a rail tile at y): where to draw it this frame.
   inline ImVec2 Glide(int index, ImVec2 natural)
   {
      const ImVec2 o = detail::Glide(detail::Top(), index, natural);
      return ImVec2(natural.x + o.x, natural.y + o.y);
   }

   // Call right after the item (its last widget is the handle). `data` is the drag payload other targets see
   // (nullptr: the index). `span` (min, max) is the whole card when the handle is only part of it.
   // Returns true while this item is held.
   inline bool Item(int index, const char* label, const void* data = nullptr, size_t bytes = 0, const ImVec2* span = nullptr,
                    ImGuiDragDropFlags flags = 0)
   {
      detail::List& l = detail::Top();
      ImVec2 mn = span != nullptr ? span[0] : ImGui::GetItemRectMin();
      ImVec2 mx = span != nullptr ? span[1] : ImGui::GetItemRectMax();
      bool held = false;
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip | flags))
      {
         if (data == nullptr)
         {
            data = &index;
            bytes = sizeof(int);
         }
         ImGui::SetDragDropPayload(l.type.c_str(), data, bytes, ImGuiCond_Always);
         ImGui::EndDragDropSource();
         if (!detail::Dragging(l))
         {
            l.from = index;
            int slot = 0;
            for (int i : l.order)
            {
               if (i == index)
                  break;
               slot++;
            }
            l.to = slot;
            const ImVec2 c = ImGui::GetIO().MouseClickedPos[0];
            l.grab = ImVec2(std::clamp(c.x - mn.x, 0.0f, mx.x - mn.x), std::clamp(c.y - mn.y, 0.0f, mx.y - mn.y));
         }
         l.frame = ImGui::GetFrameCount();
         held = index == l.from;
      }
      {
         const auto it = l.off.find(index);
         const ImVec2 o = it != l.off.end() ? it->second : ImVec2(0, 0);
         l.slots.push_back(ImRect(ImVec2(mn.x - o.x, mn.y - o.y), ImVec2(mx.x - o.x, mx.y - o.y)));
      }
      if (detail::Dragging(l) && index == l.from)
      {
         // The gap it will land in: the item, veiled, with a faint accent edge.
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.82f), tok::radius_control);
         dl->AddRect(mn, mx, (AccentCol() & 0x00FFFFFFu) | 0x60000000u, tok::radius_control, 0, 1.0f);
         // The ghost: the item's size, held where it was grabbed.
         const ImGuiIO& io = ImGui::GetIO();
         const ImVec2 size(std::max(mx.x - mn.x, 1.0f), std::max(mx.y - mn.y, 1.0f));
         const ImVec2 g0(io.MousePos.x - std::min(l.grab.x, size.x), io.MousePos.y - std::min(l.grab.y, size.y));
         const ImVec2 g1(g0.x + size.x, g0.y + size.y);
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
      }
      else
      {
         // Natural rect (the glide taken out), so the slot it picks does not chase the animation.
         const auto it = l.off.find(index);
         const ImVec2 o = it != l.off.end() ? it->second : ImVec2(0, 0);
         l.rects.push_back(ImRect(ImVec2(mn.x - o.x, mn.y - o.y), ImVec2(mx.x - o.x, mx.y - o.y)));
      }
      return held;
   }

   // Ends the list started by Order. Returns the move on the frame it is dropped.
   inline Move End()
   {
      detail::List& l = detail::Top();
      Move m;
      if (l.lineY != 0.0f)
      {
         // What follows the list starts where it would with nothing moving (no Dummy: that would add a row gap).
         ImGuiWindow* win = ImGui::GetCurrentWindow();
         win->DC.CursorPos.y -= l.lineY;
         win->DC.CursorPosPrevLine.y -= l.lineY;
         l.lineY = 0.0f;
      }
      if (detail::Dragging(l))
      {
         const ImGuiPayload* p = ImGui::GetDragDropPayload();
         const bool ours = p != nullptr && p->IsDataType(l.type.c_str());
         const bool fresh = l.frame >= ImGui::GetFrameCount() - 1;
         if (!ours || !fresh)
            l.from = l.to = -1;   // cancelled (Esc), or the held item went away
         else
         {
            // The slot: how many of the other items sit before the pointer (rows: past the middle; strips and grids:
            // a row above, or past the middle in the pointer's row).
            const ImVec2 pt = ImGui::GetIO().MousePos;
            if (l.vertical)
            {
               int slot = 0;
               for (const ImRect& r : l.rects)
                  slot += pt.y > r.GetCenter().y ? 1 : 0;
               l.to = slot;
            }
            else if (!l.slots.empty())
            {
               // Strips and grids: the cell under the pointer. Cells stay put while items glide between them, so the
               // slot never chases the animation (no flicker at a boundary). Between cells it keeps the last slot;
               // past the last row or column it is the end, before the first the start.
               const float hx = ImGui::GetStyle().ItemSpacing.x * 0.5f + 1.0f, hy = ImGui::GetStyle().ItemSpacing.y * 0.5f + 1.0f;
               int hit = -1;
               ImRect all = l.slots[0];
               for (int j = 0; j < (int)l.slots.size(); j++)
               {
                  ImRect r = l.slots[j];
                  all.Add(r);
                  r.Expand(ImVec2(hx, hy));
                  if (hit < 0 && r.Contains(pt))
                     hit = j;
               }
               if (hit < 0 && (pt.y > all.Max.y || (pt.x > all.Max.x && pt.y > l.slots.back().Min.y)))
                  hit = (int)l.slots.size() - 1;
               else if (hit < 0 && (pt.y < all.Min.y || (pt.x < all.Min.x && pt.y < l.slots[0].Max.y)))
                  hit = 0;
               if (hit >= 0)
                  l.to = hit;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               m.from = l.from;
               m.to = l.to;
               m.swap = l.swap;
               l.from = l.to = -1;
               // The data is about to move: carry each item's glide state to its new index.
               const std::vector<int> perm = detail::Perm(l.n, m.from, m.to, m.swap);
               std::unordered_map<int, ImVec2> last, off;
               for (int j = 0; j < (int)perm.size(); j++)
               {
                  if (auto it = l.last.find(perm[j]); it != l.last.end())
                     last[j] = it->second;
                  if (auto it = l.off.find(perm[j]); it != l.off.end())
                     off[j] = it->second;
               }
               l.last.swap(last);
               l.off.swap(off);
               l.settle = ImGui::GetTime() + tok::motion_accent_fade / 1000.0;
            }
         }
      }
      detail::Stack().pop_back();
      return m;
   }
}
