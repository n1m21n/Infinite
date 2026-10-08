// Arrange panel support: position formatting, typed edit, sliders, media import (moved verbatim from main.cpp).
#include "app/AppShared.h"
#include "app/ui/design/components/FieldWell.h"

namespace app
{
   // A stable per-group colour, so a group reads as one thing on every lane.
   ImU32 ArrangeGroupColor(uint64_t groupId, int alpha)
   {
      uint64_t h = groupId * 0x9E3779B97F4A7C15ull;
      h ^= h >> 29;
      const float hue = (float)(h % 360ull) / 360.0f;
      float r, g, b;
      ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 0.95f, r, g, b);
      return IM_COL32((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f), alpha);
   }


   // Diagonal hatch over a rect - the shared "this clip will not play" mark
   // for disabled and unassigned (offline) clips.
   void DrawArrangeHatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float spacing)
   {
      dl->PushClipRect(a, b, true);
      const float h = b.y - a.y;
      for (float x = a.x - h; x < b.x; x += spacing)
         dl->AddLine(ImVec2(x, b.y), ImVec2(x + h, a.y), col, 1.0f);
      dl->PopClipRect();
   }


   // ---- time formatting (WP6) ----------------------------------------------
   // Positions are 1-indexed bar.beat.sixteenth ("5.1.1" is the downbeat of
   // bar 5), durations 0-indexed bars.beats.sixteenths ("1.0.0" is one bar),
   // both at the transport's live meter. Time is M:SS.cc at the live tempo.
   void ArrangeSplitBBT(Arrange::Tick t, long long& bar, long long& beat, long long& six)
   {
      const Arrange::Tick perBar =
         std::max<Arrange::Tick>(1, Arrange::BeatsToTicks(std::max(1.0, Transport::Instance().BeatsPerBar())));
      t = std::max<Arrange::Tick>(0, t);
      bar = (long long)(t / perBar);
      const Arrange::Tick inBar = t - (Arrange::Tick)bar * perBar;
      beat = (long long)(inBar / Arrange::kPPQ);
      six = (long long)((inBar % Arrange::kPPQ) / (Arrange::kPPQ / 4));
   }

   std::string ArrangeFormatBBT(Arrange::Tick t)
   {
      long long bar, beat, six;
      ArrangeSplitBBT(t, bar, beat, six);
      char buf[48];
      snprintf(buf, sizeof(buf), "%lld.%lld.%lld", bar + 1, beat + 1, six + 1);
      return buf;
   }

   std::string ArrangeFormatBBTLength(Arrange::Tick t)
   {
      long long bar, beat, six;
      ArrangeSplitBBT(t, bar, beat, six);
      char buf[48];
      snprintf(buf, sizeof(buf), "%lld.%lld.%lld", bar, beat, six);
      return buf;
   }

   std::string ArrangeFormatSeconds(double sec, bool centis)
   {
      sec = std::max(0.0, sec);
      if (centis)
         sec = std::round(sec * 100.0) / 100.0; // so 59.999 reads 1:00.00, not 0:60.00
      const int mm = (int)(sec / 60.0);
      const double ss = sec - (double)mm * 60.0;
      char buf[32];
      if (centis)
         snprintf(buf, sizeof(buf), "%d:%05.2f", mm, ss);
      else
         snprintf(buf, sizeof(buf), "%d:%02d", mm, (int)std::floor(ss + 1e-6));
      return buf;
   }

   std::string ArrangeFormatTickSeconds(Arrange::Tick t)
   {
      return ArrangeFormatSeconds(Arrange::TicksToSeconds(t, std::max(1.0, (double)Transport::Instance().Tempo())));
   }

   // A position in the chosen unit (Settings::timeDisplay).
   std::string ArrangeFormatPos(Arrange::Tick t)
   {
      return gArrange.settings.timeDisplay == 1 ? ArrangeFormatTickSeconds(t) : ArrangeFormatBBT(t);
   }

   std::string ArrangeFormatLength(Arrange::Tick t)
   {
      if (gArrange.settings.timeDisplay == 1)
      {
         char buf[32];
         snprintf(buf, sizeof(buf), "%.2fs", Arrange::TicksToSeconds(t, std::max(1.0, (double)Transport::Instance().Tempo())));
         return buf;
      }
      return ArrangeFormatBBTLength(t);
   }

   // Parses a position typed in the chosen unit: Bars takes "5", "5.2" or
   // "5.2.3" (1-indexed); Time takes "M:SS(.cc)" or bare seconds. -1 when
   // it does not parse.
   Arrange::Tick ArrangeParsePos(const char* buf)
   {
      if (gArrange.settings.timeDisplay == 1)
      {
         int mm = 0;
         double ss = 0.0;
         double sec = -1.0;
         if (sscanf(buf, "%d:%lf", &mm, &ss) == 2)
            sec = (double)mm * 60.0 + ss;
         else
         {
            char* end = nullptr;
            const double v = strtod(buf, &end);
            if (end != buf) sec = v;
         }
         if (sec < 0.0) return -1;
         return Arrange::SecondsToTicks(sec, std::max(1.0, (double)Transport::Instance().Tempo()));
      }
      long long bar = 0, beat = 1, six = 1;
      const int n = sscanf(buf, "%lld.%lld.%lld", &bar, &beat, &six);
      if (n < 1 || bar < 1 || beat < 1 || six < 1) return -1;
      const Arrange::Tick perBar =
         std::max<Arrange::Tick>(1, Arrange::BeatsToTicks(std::max(1.0, Transport::Instance().BeatsPerBar())));
      const Arrange::Tick t = (Arrange::Tick)(bar - 1) * perBar + (Arrange::Tick)(beat - 1) * Arrange::kPPQ +
                              (Arrange::Tick)(six - 1) * (Arrange::kPPQ / 4);
      return std::clamp<Arrange::Tick>(t, 0, Arrange::kMaxTick);
   }



   // Parses a LENGTH typed in the chosen unit. Same shape as ArrangeParsePos
   // above but 0-indexed, matching ArrangeFormatBBTLength: a one-bar clip
   // reads and is typed as "1.0.0", where a clip starting at bar one reads
   // and is typed as "1.1.1". -1 when it does not parse.
   Arrange::Tick ArrangeParseLen(const char* buf)
   {
      if (gArrange.settings.timeDisplay == 1)
      {
         char* end = nullptr;
         const double sec = strtod(buf, &end);
         if (end == buf || sec < 0.0) return -1;
         return Arrange::SecondsToTicks(sec, std::max(1.0, (double)Transport::Instance().Tempo()));
      }
      long long bar = 0, beat = 0, six = 0;
      const int n = sscanf(buf, "%lld.%lld.%lld", &bar, &beat, &six);
      if (n < 1 || bar < 0 || beat < 0 || six < 0) return -1;
      const Arrange::Tick perBar =
         std::max<Arrange::Tick>(1, Arrange::BeatsToTicks(std::max(1.0, Transport::Instance().BeatsPerBar())));
      const Arrange::Tick t = (Arrange::Tick)bar * perBar + (Arrange::Tick)beat * Arrange::kPPQ +
                              (Arrange::Tick)six * (Arrange::kPPQ / 4);
      return std::clamp<Arrange::Tick>(t, 0, Arrange::kMaxTick);
   }

   void ArrangeMarkFieldHot() { gArrangeFieldHotFrame = ImGui::GetFrameCount(); }

   bool ArrangeFieldHot() { return (ImGui::GetFrameCount() - gArrangeFieldHotFrame) <= 1; }


   void ArrangeTypedEditOpen(ImGuiID id, const std::string& seed, bool selectAll)
   {
      gArrangeTypedEdit.id = id;
      gArrangeTypedEdit.text = seed;
      gArrangeTypedEdit.justOpened = true;
      gArrangeTypedEdit.pendingInit = true;
      gArrangeTypedEdit.noAutoSelect = !selectAll;
   }

   void ArrangeTypedEditClose() { gArrangeTypedEdit = ArrangeTypedEditState(); }


   // Hover + a digit / '-' / '.' / ':' opens the editor seeded with that one
   // character. ':' is here for the Time display mode, where a position is
   // typed "0:12.80". The character is deliberately NOT consumed from the
   // input queue: the field it opens is not drawn until the next frame (the
   // caller has already committed to drawing the slider this frame), by
   // which point the queue is empty anyway - exactly how the canvas path
   // behaves.
   bool ArrangeTypedEditHoverHotkey(ImGuiID id)
   {
      ImGuiContext& g = *GImGui;
      // Never while another widget holds the keyboard: hovering a slider must
      // not steal keystrokes from the name box being typed into above it.
      if (g.ActiveId != 0)
         return false;
      for (int i = 0; i < g.IO.InputQueueCharacters.Size; ++i)
      {
         const ImWchar ch = g.IO.InputQueueCharacters[i];
         if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == ':')
         {
            ArrangeTypedEditOpen(id, std::string(1, (char)ch), /*selectAll=*/false);
            return true;
         }
      }
      return false;
   }


   // Draws the open editor in place of its value widget. Returns true on the
   // frame the user commits (Enter, or clicking away); `out` then holds the
   // raw text for the caller to parse in whatever unit it speaks.
   bool ArrangeTypedEditDraw(ImGuiID id, const char* strId, float width, const char* label,
                             std::string& out)
   {
      if (gArrangeTypedEdit.id != id)
         return false;
      ArrangeMarkFieldHot();
      ImGui::SetNextItemWidth(width);
      if (gArrangeTypedEdit.justOpened)
      {
         ImGui::SetKeyboardFocusHere();
         gArrangeTypedEdit.justOpened = false;
      }
      char buf[128];
      snprintf(buf, sizeof(buf), "%s", gArrangeTypedEdit.text.c_str());
      const bool entered = ImGui::InputText(strId, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
      gArrangeTypedEdit.text = buf;
      // Selection is driven explicitly rather than by a flag, for the reason
      // spelled out at the canvas equivalent: focus arrives through
      // SetKeyboardFocusHere, so ImGui runs its own select-all the moment the
      // field goes active - possibly a frame later - whatever flags say. That
      // select-all is right for a double-click (replace the whole value) and
      // wrong for hover+type (it would eat the digit just seeded).
      if (gArrangeTypedEdit.pendingInit && ImGui::IsItemActive())
      {
         if (ImGuiInputTextState* st = ImGui::GetInputTextState(ImGui::GetItemID()))
         {
            if (gArrangeTypedEdit.noAutoSelect)
            {
               st->ReloadUserBufAndMoveToEnd();
               st->ClearSelection();
            }
            else
               st->SelectAll();
         }
         gArrangeTypedEdit.pendingInit = false;
      }
      // The caption the slider would have drawn to the right of its frame,
      // so the row does not visibly reflow the moment it becomes editable.
      if (label != nullptr)
      {
         const char* visible = label;
         const char* hash = strstr(label, "##");
         if (hash == label)
            visible = nullptr;
         if (visible != nullptr)
         {
            ImGui::SameLine(0.0f, GImGui->Style.ItemInnerSpacing.x);
            if (hash != nullptr)
               ImGui::TextUnformatted(visible, hash);
            else
               ImGui::TextUnformatted(visible);
         }
      }
      if (entered || ImGui::IsItemDeactivated())
      {
         out = gArrangeTypedEdit.text;
         ArrangeTypedEditClose();
         return true;
      }
      return false;
   }


   bool ArrangeSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
   {
      ImGuiWindow* window = ImGui::GetCurrentWindow();
      if (window->SkipItems)
         return false;

      ImGuiContext& g = *GImGui;
      const ImGuiStyle& style = g.Style;
      const ImGuiID id = window->GetID(label);
      const float w = ImGui::CalcItemWidth();

      // Typed edit open on this field: it replaces the slider entirely for as
      // long as it is open. Parsed with strtof rather than through ImGui's
      // format string, so "0.3" typed over "0.80 " is just 0.3 - the trailing
      // unit in formats like "%.1f dB" / "%.1f st" never has to be retyped.
      if (gArrangeTypedEdit.id == id)
      {
         std::string typed;
         if (!ArrangeTypedEditDraw(id, "##arrtypedval", w, label, typed))
            return false;
         const std::string trimmed = TrimCopy(typed);
         if (trimmed.empty())
            return false;
         char* end = nullptr;
         const float parsed = strtof(trimmed.c_str(), &end);
         if (end == trimmed.c_str())
            return false;
         const float clamped = std::clamp(parsed, std::min(v_min, v_max), std::max(v_min, v_max));
         if (clamped == *v)
            return false;
         *v = clamped;
         return true;
      }

      const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
      const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
      const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

      const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
      ImGui::ItemSize(total_bb, style.FramePadding.y);
      if (!ImGui::ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
         return false;

      if (format == NULL)
         format = "%.3f";

      const bool hovered = ImGui::ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
      if (hovered)
         ArrangeMarkFieldHot();
      {
         ArrangeSliderPendingClick& pending = sArrangeSliderPending[id];

         const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(0);
         const bool freshClick = hovered && !doubleClicked && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, id);

         // Hover + a digit opens the typed editor NEXT frame, seeded with that
         // digit (ArrangeTypedEditHoverHotkey). Double-click and Ctrl+click
         // open it on the current value, selected, because there the intent is
         // "replace this whole number" rather than "start typing a new one".
         bool openTyped = false;
         bool openSelected = false;
         if (hovered && temp_input_allowed)
            openTyped = ArrangeTypedEditHoverHotkey(id);

         bool activateDrag = false;
         if (doubleClicked && temp_input_allowed)
         {
            pending.waiting = false;
            ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
            openTyped = true;
            openSelected = true;
         }
         else if (freshClick && g.IO.KeyCtrl && temp_input_allowed)
         {
            // Ctrl+Click is unambiguous - no need to wait out the double-click window.
            pending.waiting = false;
            ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
            openTyped = true;
            openSelected = true;
         }
         else if (freshClick)
         {
            pending.waiting = true;
            pending.downTime = g.Time;
            pending.downPos = g.IO.MousePos;
         }
         else if (pending.waiting)
         {
            const float dx = g.IO.MousePos.x - pending.downPos.x;
            const float dy = g.IO.MousePos.y - pending.downPos.y;
            const bool movedPastThreshold = (dx * dx + dy * dy) > (g.IO.MouseDragThreshold * g.IO.MouseDragThreshold);
            const bool doubleClickWindowElapsed = (g.Time - pending.downTime) > g.IO.MouseDoubleClickTime;
            if (ImGui::IsMouseDown(0) ? (movedPastThreshold || doubleClickWindowElapsed) : doubleClickWindowElapsed)
            {
               pending.waiting = false;
               ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
               activateDrag = true;
            }
         }
         else if (g.NavActivateId == id)
         {
            activateDrag = true;
         }

         if (openSelected)
         {
            char seed[64];
            ImGui::DataTypeFormatString(seed, IM_ARRAYSIZE(seed), ImGuiDataType_Float, v, format);
            ArrangeTypedEditOpen(id, TrimCopy(seed), /*selectAll=*/true);
         }
         if (openTyped)
         {
            ImGui::ClearActiveID();
            return false; // the editor draws in this field's place next frame
         }

         if (activateDrag)
         {
            ImGui::SetActiveID(id, window);
            ImGui::SetFocusID(id, window);
            ImGui::FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
         }
      }

      // Well + fill: a text-tinted well (same family as ChipButton) with the value drawn as an accent fill from the left.
      ImGui::RenderNavHighlight(frame_bb, id);
      FieldWell::Draw(window->DrawList, frame_bb.Min, frame_bb.Max, hovered, g.ActiveId == id);

      // Slider behavior
      ImRect grab_bb;
      const bool value_changed = ImGui::SliderBehavior(frame_bb, id, ImGuiDataType_Float, v, &v_min, &v_max, format, flags, &grab_bb);
      if (value_changed)
         ImGui::MarkItemEdited(id);

      // Fill up to the grab centre (the grab itself is not drawn)
      FieldWell::Fill(window->DrawList, frame_bb.Min, frame_bb.Max, (grab_bb.Min.x + grab_bb.Max.x) * 0.5f);

      // Display value
      char value_buf[64];
      const char* value_buf_end = value_buf + ImGui::DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), ImGuiDataType_Float, v, format);
      if (g.LogEnabled)
         ImGui::LogSetNextTextDecoration("{", "}");
      ImGui::RenderTextClipped(frame_bb.Min, frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.5f));

      if (label_size.x > 0.0f)
         ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

      return value_changed;
   }


   bool ArrangeTickField(const char* label, Arrange::Tick cur, Arrange::Tick lo, Arrange::Tick hi,
                         ArrangeTickUnit unit, float width, Arrange::Tick* out)
   {
      ImGuiWindow* window = ImGui::GetCurrentWindow();
      if (window->SkipItems)
         return false;
      const ImGuiID id = window->GetID(label);
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      const bool bars = gArrange.settings.timeDisplay == 0 && unit != ArrangeTickUnit::FadeMs;

      auto toTicks = [&](double shown) -> Arrange::Tick
      {
         if (unit == ArrangeTickUnit::FadeMs)
            return Arrange::SecondsToTicks(shown / 1000.0, bpm);
         return bars ? Arrange::BeatsToTicks(shown) : Arrange::SecondsToTicks(shown, bpm);
      };
      auto fromTicks = [&](Arrange::Tick t) -> float
      {
         if (unit == ArrangeTickUnit::FadeMs)
            return (float)(Arrange::TicksToSeconds(t, bpm) * 1000.0);
         return bars ? (float)Arrange::TicksToBeats(t) : (float)Arrange::TicksToSeconds(t, bpm);
      };

      // Typed edit open on this field - it replaces the drag entirely.
      if (gArrangeTypedEdit.id == id)
      {
         std::string typed;
         if (!ArrangeTypedEditDraw(id, "##arrtypedtick", width, label, typed))
            return false;
         const std::string trimmed = TrimCopy(typed);
         if (trimmed.empty())
            return false;
         Arrange::Tick parsed = -1;
         if (unit == ArrangeTickUnit::FadeMs)
         {
            char* end = nullptr;
            const double ms = strtod(trimmed.c_str(), &end);
            if (end != trimmed.c_str() && ms >= 0.0)
               parsed = Arrange::SecondsToTicks(ms / 1000.0, bpm);
         }
         else if (unit == ArrangeTickUnit::Length)
            parsed = ArrangeParseLen(trimmed.c_str());
         else
            parsed = ArrangeParsePos(trimmed.c_str());
         if (parsed < 0)
            return false;
         *out = std::clamp<Arrange::Tick>(parsed, lo, hi);
         return *out != cur;
      }

      const float v0 = fromTicks(cur);
      float v = v0;
      const float vlo = fromTicks(lo);
      const float vhi = hi >= Arrange::kMaxTick ? FLT_MAX : fromTicks(hi);
      const float speed = (unit == ArrangeTickUnit::FadeMs) ? 1.0f : (bars ? 0.0625f : 0.01f);
      // The bar/beat text is a literal rather than a printf of the dragged
      // float, so ImGui's own ctrl+click text entry is switched off here and
      // the typed path above is the only way in - it is the one that knows
      // how to read "9.3.3" back.
      std::string shown;
      if (unit == ArrangeTickUnit::FadeMs)
         shown = "%.0f ms";
      else if (bars)
         shown = (unit == ArrangeTickUnit::Length) ? ArrangeFormatBBTLength(cur) : ArrangeFormatBBT(cur);
      else
         shown = "%.2fs";

      ImGui::SetNextItemWidth(width);
      FieldWell::PushStyle();
      const bool dragged = ImGui::DragFloat(label, &v, speed, vlo, vhi, shown.c_str(),
                                            ImGuiSliderFlags_NoInput);
      FieldWell::PopStyle();

      if (ImGui::IsItemHovered())
      {
         ArrangeMarkFieldHot();
         if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            std::string seed;
            if (unit == ArrangeTickUnit::FadeMs)
            {
               char buf[32];
               snprintf(buf, sizeof(buf), "%.0f", v0);
               seed = buf;
            }
            else if (bars)
               seed = (unit == ArrangeTickUnit::Length) ? ArrangeFormatBBTLength(cur) : ArrangeFormatBBT(cur);
            else
            {
               char buf[32];
               snprintf(buf, sizeof(buf), "%.2f", v0);
               seed = buf;
            }
            ArrangeTypedEditOpen(id, seed, /*selectAll=*/true);
            ImGui::ClearActiveID();
            return false;
         }
         if (ArrangeTypedEditHoverHotkey(id))
         {
            ImGui::ClearActiveID();
            return false;
         }
      }

      if (!dragged)
         return false;
      if (unit == ArrangeTickUnit::FadeMs)
         *out = std::clamp<Arrange::Tick>(toTicks(v), lo, hi);
      else if (bars)
      {
         const Arrange::Tick q = Arrange::kPPQ / 4;
         *out = std::clamp<Arrange::Tick>(Arrange::SnapToGrid(Arrange::BeatsToTicks(v), q), lo, hi);
      }
      else
         *out = std::clamp<Arrange::Tick>(toTicks(v), lo, hi);
      return *out != cur;
   }


   // A plain drag field with the same typing behaviour as the sliders beside
   // it. ImGui::DragFloat's own ctrl+click entry exists but opens on the old
   // text and does not hover-and-type, which is the inconsistency this whole
   // pass is removing - so Sample BPM goes through here rather than being the
   // one field in the inspector that still behaves differently.
   bool ArrangeDragFloat(const char* label, float* v, float speed, float v_min, float v_max,
                         const char* format, float width)
   {
      ImGuiWindow* window = ImGui::GetCurrentWindow();
      if (window->SkipItems)
         return false;
      const ImGuiID id = window->GetID(label);

      if (gArrangeTypedEdit.id == id)
      {
         std::string typed;
         if (!ArrangeTypedEditDraw(id, "##arrtypeddrag", width, label, typed))
            return false;
         const std::string trimmed = TrimCopy(typed);
         if (trimmed.empty())
            return false;
         char* end = nullptr;
         const float parsed = strtof(trimmed.c_str(), &end);
         if (end == trimmed.c_str())
            return false;
         const float clamped = std::clamp(parsed, std::min(v_min, v_max), std::max(v_min, v_max));
         if (clamped == *v)
            return false;
         *v = clamped;
         return true;
      }

      ImGui::SetNextItemWidth(width);
      const bool dragged = ImGui::DragFloat(label, v, speed, v_min, v_max, format,
                                            ImGuiSliderFlags_NoInput);
      if (ImGui::IsItemHovered())
      {
         ArrangeMarkFieldHot();
         if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            char seed[64];
            ImGui::DataTypeFormatString(seed, IM_ARRAYSIZE(seed), ImGuiDataType_Float, v, format);
            ArrangeTypedEditOpen(id, TrimCopy(seed), /*selectAll=*/true);
            ImGui::ClearActiveID();
            return false;
         }
         if (ArrangeTypedEditHoverHotkey(id))
         {
            ImGui::ClearActiveID();
            return false;
         }
      }
      return dragged;
   }


   // ---- render-range and render-target helpers (WP7) ------------------------

   // The end of the arrangement as the *render* sees it: disabled clips and
   // clips whose source is gone contribute nothing to a take, so counting
   // them padded every "Whole arrangement" job with silence and black
   // (WP7 #5). Arrange::ArrangementEnd stays as it is - the End key and the
   // ruler deliberately still jump to the last clip, enabled or not.
   Arrange::Tick ArrangeRenderableEndTick()
   {
      Arrange::Tick end = 0;
      for (const Arrange::Lane& l : gArrange.lanes)
         for (const Arrange::Clip& c : l.clips)
         {
            if (!c.enabled || c.srcUid == 0 || FindNodeByUid(c.srcUid) == nullptr)
               continue;
            end = std::max(end, c.End());
         }
      return end;
   }


   // "Match Clips": the first *renderable* video clip that actually reports a
   // size, else the patch's own Output node, else 1080p. A geometry clip has
   // no texture size of its own, and a disabled or unassigned one isn't in
   // the picture at all - either used to hand the job a 0x0 or a stale size.
   void ArrangeRenderDetectClipSize(int& outW, int& outH)
   {
      outW = 0;
      outH = 0;
      for (const Arrange::Lane& l : gArrange.lanes)
      {
         if (l.type != Arrange::kLaneVideo)
            continue;
         for (const Arrange::Clip& c : l.clips)
         {
            if (!c.enabled || c.srcUid == 0)
               continue;
            GraphNode* gn = FindNodeByUid(c.srcUid);
            if (gn == nullptr || gn->node == nullptr)
               continue;
            if (gn->node->GetOutputWidth() > 0 && gn->node->GetOutputHeight() > 0)
            {
               outW = gn->node->GetOutputWidth();
               outH = gn->node->GetOutputHeight();
               return;
            }
         }
      }
      for (GraphNode& gn : gNodes)
      {
         if (auto* on = dynamic_cast<OutputNode*>(gn.node.get()))
         {
            if (on->GetOutputWidth() > 0 && on->GetOutputHeight() > 0)
            {
               outW = on->GetOutputWidth();
               outH = on->GetOutputHeight();
               return;
            }
         }
      }
      outW = 1920;
      outH = 1080;
   }


   // How many enabled, resolvable video clips a range actually covers. Decides
   // whether a timeline take writes a movie or a WAV, and is the whole of the
   // render dialog's source logic now that the Audio/Video source dropdowns
   // are gone. A free function, not a lambda in the popup, so the self-test
   // below exercises the code that ships rather than a copy of it.
   int ArrangeRenderVideoClipsInRange(Arrange::Tick a, Arrange::Tick b)
   {
      int n = 0;
      for (const Arrange::Lane& l : gArrange.lanes)
      {
         if (l.type != Arrange::kLaneVideo)
            continue;
         for (const Arrange::Clip& c : l.clips)
            if (c.enabled && c.srcUid != 0 && c.End() > a && c.start < b && FindNodeByUid(c.srcUid) != nullptr)
               n++;
      }
      return n;
   }


   // The timeline Render dialog's sources. Audio is always the timeline - the
   // dialog renders the arrangement, and a canvas take is the Output node's
   // own record button. Video follows the range: a movie when there is
   // something to draw, a WAV when there is not.
   int ArrangeRenderEffectiveAudioSource() { return kArrangeAudioTimeline; }

   int ArrangeRenderEffectiveVideoSource(Arrange::Tick a, Arrange::Tick b)
   {
      return ArrangeRenderVideoClipsInRange(a, b) > 0 ? kArrangeVideoTimeline : kArrangeVideoNone;
   }


   // "name.mp4" -> "name (2).mp4", counting up past anything already on disk
   // or already queued (WP7 #8's Auto-rename).
   // Does an unfinished job already own this path? Two queued jobs writing
   // the same file is the same collision as one overwriting an existing file,
   // and is easier to miss - the second one only clobbers the first once the
   // queue gets there.
   bool ArrangeRenderPathQueued(const std::string& path, uint64_t exceptJobId)
   {
      for (const ArrangeRenderJob& j : gArrangeRenderQueue)
      {
         if (j.id == exceptJobId)
            continue;
         if (j.status != kArrangeJobQueued && j.status != kArrangeJobRendering &&
             j.status != kArrangeJobFinalizing)
            continue;
         if (j.path == path)
            return true;
      }
      return false;
   }


   // The tick span a range kind resolves to against the model as it stands
   // right now. Extracted from the render popup so INFINITE_ARRANGERENDERTEST
   // checks the code the popup runs rather than a copy of it. `markerA/B` are
   // indices into gArrange.markers and are ignored by the other kinds.
   void ArrangeRenderResolveRange(int kind, int markerA, int markerB, Arrange::Tick customStart,
                                  Arrange::Tick customEnd, Arrange::Tick& outA, Arrange::Tick& outB)
   {
      switch (kind)
      {
      case kArrangeRangeLoop:
         outA = gArrange.settings.loop.start;
         outB = gArrange.settings.loop.end;
         break;
      case kArrangeRangeMarkers:
      {
         const int n = (int)gArrange.markers.size();
         const int ia = std::clamp(markerA, 0, std::max(0, n - 1));
         const int ib = std::clamp(markerB, 0, std::max(0, n - 1));
         outA = n > 0 ? gArrange.markers[ia].pos : 0;
         outB = n > 0 ? gArrange.markers[ib].pos : 0;
         if (outB < outA)
            std::swap(outA, outB);
         break;
      }
      case kArrangeRangeCustom:
         outA = customStart;
         outB = customEnd;
         break;
      case kArrangeRangeWhole:
      default:
         outA = 0;
         outB = ArrangeRenderableEndTick();
         break;
      }
      if (outB <= outA) // never hand the runner an empty job
         outB = outA + Arrange::kPPQ;
   }


   // Frames a take of `durSec` writes at `fps`. ceil, not round or truncate:
   // a 2.4s range at 30fps is 72 frames, and dropping the partial one would
   // end the file short of the range the user asked for (WP7 #2). The clamp's
   // upper bound is 240 hours at 1fps / 1 hour at 240fps - a guard against a
   // nonsense tick range, not a policy.
   int ArrangeRenderFrameBudget(double durSec, int fps)
   {
      return std::clamp((int)std::ceil(durSec * (double)std::max(1, fps)), 1, 240 * 3600);
   }


   // Sample frames an audio-only take of `durSec` writes at `rate`. Round, not
   // ceil: unlike a video frame a sample is not a container for a slice of
   // time, so the nearest whole sample is the closest the file can get.
   long long ArrangeRenderSampleBudget(double durSec, double rate)
   {
      return std::max<long long>(1, llround(durSec * rate));
   }


   // The sample rate an offline take will actually be written at. Every
   // AudioNode is PrepareToPlay'd at the rate the device negotiated, so the
   // engine's rate is not a preference the render can override - it is the
   // only rate the graph knows how to generate. Before the device has ever
   // opened, the global setting is the best prediction of what it will be
   // (0 there means "device default", and 48k is what every supported
   // backend defaults to).
   double ArrangeRenderActiveSampleRate()
   {
      const double engine = AudioEngine::Instance().SampleRate();
      if (engine > 0.0)
         return engine;
      if (gAudioSampleRate > 0.0)
         return gAudioSampleRate;
      return 48000.0;
   }


   int OfflineAudioBlockFrames()
   {
      if (gHeadlessJob.mode != Headless::Mode::None)
         return std::min(kHeadlessAudioBlockFrames, kAudioMaxBlockFrames);
      int frames = (int)Platform::AudioDeviceBufferFrames(gAudioOutputDeviceId);
      if (frames <= 0)
         frames = gAudioBufferFrames;
      if (frames <= 0)
         frames = 512;
      return std::clamp(frames, 1, kAudioMaxBlockFrames);
   }


   // Classifies a dropped file for Arrange media-drop import (audio sample,
   // video, or image). Video/image share their extension lists with the
   // canvas drop handler and SampleScanner (MediaExtensions.h); audio gets
   // its own short list here matching exactly what AudioFileNode/
   // AudioDecodeCache already link, rather than MediaExtensions growing an
   // audio list of its own for this one caller.
   bool ArrangeMediaKindForPath(const std::string& path, Arrange::ImportMediaKind& outKind)
   {
      static const std::vector<std::string> kAudioExt = {
         "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg"
      };
      if (HasExtension(path, kAudioExt)) { outKind = Arrange::ImportMediaKind::Audio; return true; }
      if (HasExtension(path, MediaExtensions::Video())) { outKind = Arrange::ImportMediaKind::Video; return true; }
      if (HasExtension(path, MediaExtensions::Image())) { outKind = Arrange::ImportMediaKind::Image; return true; }
      return false;
   }


   // Drops `path` onto the Arrange timeline at (laneId, atTick): spawns the
   // matching source node, places a clip referencing it immediately in an
   // `importPending` state (so the timeline shows something the instant the
   // drop lands, per the "loading state clip shown immediately" requirement),
   // and kicks off the file's real decode on a worker thread via
   // Arrange::GetMediaImportManager() (ArrangeMediaImport.h). The clip/node
   // pair is tracked in gArrangePendingImports until ArrangePollMediaImports
   // Multi-strategy sample BPM estimation combining:
   // 1. Filename/path metadata (e.g. "_128bpm", "140BPM", "bpm124")
   // 2. Exact loop duration matching (1, 2, 4, 8, 16, 32 bars)
   // 3. Spectral flux transient onset detection & IOI autocorrelation histogram
   float ArrangeEstimateSampleBpm(const Platform::SampleBuffer* buf, const std::string& path, float fallbackBpm,
                                  bool* detected = nullptr)
   {
      if (detected) *detected = true;
      // Strategy 1: Explicit BPM tags in filename or path
      if (!path.empty())
      {
         std::string lowerPath = path;
         for (char& ch : lowerPath)
            ch = (char)std::tolower((unsigned char)ch);

         size_t pos = 0;
         while ((pos = lowerPath.find("bpm", pos)) != std::string::npos)
         {
            // Backward search for number before "bpm"
            int endDigit = (int)pos - 1;
            while (endDigit >= 0 && (lowerPath[endDigit] == ' ' || lowerPath[endDigit] == '_' || lowerPath[endDigit] == '-'))
               endDigit--;
            if (endDigit >= 0 && std::isdigit((unsigned char)lowerPath[endDigit]))
            {
               int startDigit = endDigit;
               while (startDigit > 0 && (std::isdigit((unsigned char)lowerPath[startDigit - 1]) || lowerPath[startDigit - 1] == '.'))
                  startDigit--;
               std::string numStr = lowerPath.substr(startDigit, endDigit - startDigit + 1);
               try {
                  float val = std::stof(numStr);
                  if (val >= 40.0f && val <= 300.0f)
                     return val;
               } catch (...) {}
            }

            // Forward search for number after "bpm"
            size_t startAfter = pos + 3;
            while (startAfter < lowerPath.size() && (lowerPath[startAfter] == ' ' || lowerPath[startAfter] == '_' || lowerPath[startAfter] == '-'))
               startAfter++;
            if (startAfter < lowerPath.size() && std::isdigit((unsigned char)lowerPath[startAfter]))
            {
               size_t endAfter = startAfter;
               while (endAfter < lowerPath.size() && (std::isdigit((unsigned char)lowerPath[endAfter]) || lowerPath[endAfter] == '.'))
                  endAfter++;
               std::string numStr = lowerPath.substr(startAfter, endAfter - startAfter);
               try {
                  float val = std::stof(numStr);
                  if (val >= 40.0f && val <= 300.0f)
                     return val;
               } catch (...) {}
            }
            pos += 3;
         }
      }

      if (buf == nullptr || buf->numFrames <= 0 || buf->channels <= 0 || buf->channelData.empty())
         { if (detected) *detected = false; return fallbackBpm > 0.0f ? fallbackBpm : 120.0f; }

      const double sr = buf->sampleRate > 0.0 ? buf->sampleRate : 44100.0;
      const int numFrames = buf->numFrames;
      const double durationSec = (double)numFrames / sr;

      // Strategy 2: Exact musical loop duration matching (1, 2, 4, 8, 16 bars)
      float bestLoopBpm = 0.0f;
      float bestLoopDiff = 999.0f;
      for (int bars : { 1, 2, 4, 8, 16, 32 })
      {
         const double candidateBpm = (double)(bars * 240) / durationSec;
         if (candidateBpm >= 60.0 && candidateBpm <= 200.0)
         {
            const float rounded = std::roundf((float)candidateBpm);
            const float diff = std::abs((float)candidateBpm - rounded);
            if (diff < 0.15f && diff < bestLoopDiff)
            {
               bestLoopDiff = diff;
               bestLoopBpm = rounded;
            }
         }
      }

      // Strategy 3: Transient Onset Detection & IOI clustering via SlicerDsp
      const int maxAnalyzeFrames = std::min(numFrames, (int)(sr * 30.0));
      std::vector<float> mono(maxAnalyzeFrames);
      const float* ch0 = buf->channelData.data();
      if (buf->channels == 1)
      {
         std::copy(ch0, ch0 + maxAnalyzeFrames, mono.begin());
      }
      else
      {
         const float* ch1 = ch0 + numFrames;
         for (int i = 0; i < maxAnalyzeFrames; i++)
            mono[i] = 0.5f * (ch0[i] + ch1[i]);
      }

      SlicerDsp::Params params;
      params.sensitivity = 70.0f;
      params.maxSlices = 128;
      std::vector<int> onsets;
      std::vector<float> strengths;
      std::atomic<bool> abortFlag{false};
      SlicerDsp::Detect(mono.data(), maxAnalyzeFrames, sr, params, onsets, strengths, &abortFlag);

      if (onsets.size() >= 4)
      {
         std::vector<double> onsetTimes(onsets.size());
         for (size_t i = 0; i < onsets.size(); i++)
            onsetTimes[i] = (double)onsets[i] / sr;

         float bestScore = -1.0f;
         float bestBpm = 0.0f;
         const double minIoiSec = 0.15; // 400 BPM
         const double maxIoiSec = 2.0;  // 30 BPM

         for (float candidateBpm = 60.0f; candidateBpm <= 200.0f; candidateBpm += 0.5f)
         {
            const double beatPeriod = 60.0 / (double)candidateBpm;
            float score = 0.0f;

            for (size_t i = 0; i < onsets.size(); i++)
            {
               for (size_t j = i + 1; j < onsets.size() && j < i + 16; j++)
               {
                  const double delta = onsetTimes[j] - onsetTimes[i];
                  if (delta < minIoiSec) continue;
                  if (delta > maxIoiSec) break;

                  for (double mult : { 0.25, 0.333333, 0.5, 0.666667, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 })
                  {
                     const double targetDelta = beatPeriod * mult;
                     const double err = std::abs(delta - targetDelta);
                     if (err < 0.025 * mult)
                     {
                        const float weight = (mult == 1.0 || mult == 0.5 || mult == 2.0) ? 2.0f : 1.0f;
                        score += weight * (1.0f - (float)(err / (0.025 * mult)));
                     }
                  }
               }
            }

            if (candidateBpm >= 85.0f && candidateBpm <= 145.0f)
               score *= 1.15f;

            if (score > bestScore)
            {
               bestScore = score;
               bestBpm = candidateBpm;
            }
         }

         if (bestScore > 5.0f && bestBpm > 0.0f)
         {
            if (bestLoopBpm > 0.0f)
            {
               if (std::abs(bestLoopBpm - bestBpm) < 2.0f ||
                   std::abs(bestLoopBpm * 2.0f - bestBpm) < 2.0f ||
                   std::abs(bestLoopBpm * 0.5f - bestBpm) < 2.0f)
               {
                  return bestLoopBpm;
               }
            }
            if (std::abs(bestBpm - std::roundf(bestBpm)) < 0.12f)
               return std::roundf(bestBpm);
            return bestBpm;
         }
      }

      if (bestLoopBpm > 0.0f && bestLoopDiff < 0.1f)
         return bestLoopBpm;

      { if (detected) *detected = false; return fallbackBpm > 0.0f ? fallbackBpm : 120.0f; }
   }


   // Spawn the right node type for dropped media, place a clip in the lane,
   // and dispatch the decode job to the background import thread.
   void ArrangeImportMediaFile(const std::string& path, uint64_t laneId, Arrange::Tick atTick,
                               Arrange::ImportMediaKind kind)
   {
      int laneIdx = -1;
      for (size_t i = 0; i < gArrange.lanes.size(); i++)
      {
         if (gArrange.lanes[i].id == laneId)
         {
            laneIdx = (int)i;
            break;
         }
      }
      if (laneIdx < 0)
         return;

      const bool isAudioKind = (kind == Arrange::ImportMediaKind::Audio);
      const bool isAudioLane = (gArrange.lanes[laneIdx].type == Arrange::kLaneAudio);
      if (isAudioKind != isAudioLane)
         return;

      const char* typeName = isAudioKind ? "Audio File" : (kind == Arrange::ImportMediaKind::Video ? "Video" : "Image Source");
      const char* category = isAudioKind ? "Modulators" : "Source";
      const ImVec2 spawnPos = FindFreeSpawnPosition(gViewCenterCanvas);
      GraphNode* spawned = SpawnNode(typeName, category, spawnPos.x, spawnPos.y);
      if (spawned == nullptr)
         return;

      // A dropped Audio/Video/Image Sample owns this node privately - it is
      // not a modular-canvas object the user patches into other things, just
      // where the decoded buffer and WSOLA/playback state actually live (see
      // AudioFilePlayerAudioNode). hiddenFromCanvas (the same flag Mount/
      // Field encapsulation already uses) keeps it out of the node editor
      // entirely - not drawn, not pickable, not wireable - so "drop a
      // sample" no longer clutters the canvas the way spawning a visible
      // node used to. ArrangePasteAt/ArrangeDuplicateSelection/Split give
      // every clone its own freshly spawned hidden node rather than sharing
      // this one, so no two clips ever contend over one node's single
      // playback cursor/stretcher (see those functions' own comments).
      spawned->hiddenFromCanvas = true;
      if (auto* afn = dynamic_cast<AudioFileNode*>(spawned->node.get()))
         afn->loop = false; // a Sample's box, not the node, decides what plays

      // Placeholder length until the real duration is known: one bar for
      // audio/video, corrected in ArrangePollMediaImports once decode
      // finishes; an image's fixed 5-second length is already final, since
      // an image has no natural duration to later correct to.
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      Arrange::Tick length = Arrange::kTicksPerBar;
      if (kind == Arrange::ImportMediaKind::Image)
         length = std::max<Arrange::Tick>(1, Arrange::SecondsToTicks(5.0, bpm));

      uint64_t clipId = 0;
      ArrangeEdit([&]()
      {
         Arrange::Clip c;
         c.start = atTick;
         c.length = std::max<Arrange::Tick>(1, length);
         c.srcUid = spawned->uid;
         c.name = spawned->typeName;
         c.importPending = true;
         c.sampleDropped = true;
         // Default syncToTempo to false so the dropped sample's original native state
         // is preserved untouched until the user explicitly requests tempo sync.
         c.syncToTempo = false;
         c.sampleBpm = (float)bpm;
         Arrange::PlaceOverwrite(gArrange, laneId, c, &clipId);
      });
      if (clipId == 0)
      {
         RemoveNodeByIndex(spawned->index);
         return;
      }

      ArrangePendingImport pending;
      pending.jobId = Arrange::GetMediaImportManager().StartImport(path, kind);
      pending.clipId = clipId;
      pending.nodeUid = spawned->uid;
      pending.kind = kind;
      gArrangePendingImports.push_back(pending);

      gArrangeSel = { clipId };
      gArrangeSelAnchor = clipId;
      gArrangeFlashClipId = clipId;
      gArrangeFlashStart = ImGui::GetTime();
      gPatchDirty = true;
   }


   // Gives a pasted/duplicated/split Sample clip its own private hidden node
   // and kicks a fresh async decode of the same source file, instead of
   // leaving it pointing at the original clip's node - see
   // ArrangeImportMediaFile's own comment on hiddenFromCanvas for why two
   // clips must never share one node's single playback cursor/stretcher.
   // No-op for anything that isn't a dropped Sample (e.g. an ordinary
   // patched Audio Clip, which is fine to keep sharing srcUid the way
   // paste/duplicate always have).
   void ArrangeRespawnCloneNode(uint64_t clipId)
   {
      Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      if (c == nullptr || !c->sampleDropped)
         return;

      GraphNode* srcNode = FindNodeByUid(c->srcUid);
      if (srcNode == nullptr)
         return;

      std::string path;
      Arrange::ImportMediaKind kind;
      if (auto* audioNode = dynamic_cast<AudioFileNode*>(srcNode->node.get()))
      {
         path = audioNode->FilePath();
         kind = Arrange::ImportMediaKind::Audio;
      }
      else if (auto* videoNode = dynamic_cast<VideoSourceNode*>(srcNode->node.get()))
      {
         path = videoNode->LoadedPath();
         kind = Arrange::ImportMediaKind::Video;
      }
      else if (auto* imageNode = dynamic_cast<ImageSourceNode*>(srcNode->node.get()))
      {
         path = imageNode->LoadedPath();
         kind = Arrange::ImportMediaKind::Image;
      }
      else
         return;
      if (path.empty())
         return;

      const ImVec2 spawnPos = FindFreeSpawnPosition(gViewCenterCanvas);
      const std::string typeName = srcNode->typeName;
      const char* category = (kind == Arrange::ImportMediaKind::Audio) ? "Modulators" : "Source";
      GraphNode* spawned = SpawnNode(typeName.c_str(), category, spawnPos.x, spawnPos.y);
      if (spawned == nullptr)
         return;
      spawned->hiddenFromCanvas = true;

      // Same "loading" placeholder gap ArrangeImportMediaFile leaves a fresh
      // drop in until ArrangePollMediaImports adopts the decode - the clone
      // is silent/blank for a frame or two rather than briefly still
      // pointing at (and contending with) the original's node.
      c->srcUid = spawned->uid;
      c->importPending = true;

      ArrangePendingImport pending;
      pending.jobId = Arrange::GetMediaImportManager().StartImport(path, kind);
      pending.clipId = clipId;
      pending.nodeUid = spawned->uid;
      pending.kind = kind;
      pending.isClone = true;
      gArrangePendingImports.push_back(pending);
   }


   // Hover detail for the shared-source warning. The warning itself is one
   // line by design - the explanation is real but nobody needs it on every
   // frame they have that clip selected, so it lives here, behind a hover,
   // for the moment they want to know why.
   void ArrangeSharedSourceTooltip(const char* body)
   {
      if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         return;
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
      ImGui::TextUnformatted(body);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
   }


   // "Make Unique": gives this clip its own copy of its source node, so two
   // clips sharing one node stop fighting over the node's single playback
   // position (the conflict the inspector warns about).
   //
   // A dropped Sample already has a purpose-built path for this - its node
   // owns a decoded file and a stretcher, which must be re-decoded rather
   // than parameter-copied - so that case defers to ArrangeRespawnCloneNode.
   // Everything else is an ordinary graph node and is cloned the same way
   // Shift+D clones one: same type, same params, and every link *landing on*
   // it re-established onto the copy via the cluster clipboard, so the copy
   // arrives fed by the same upstream and carrying the same modulations
   // rather than as a bare default node.
   //
   // The copy is a normal visible canvas node, not hidden: a hidden node's
   // param pins never re-register for modulation, which would leave the
   // clip's own Modulations list permanently empty - the opposite of what
   // someone pressing this button wants.
   //
   // The clip's bypass list survives untouched: it stores paramIndex values,
   // and the copy is the same node type, so index N is the same parameter on
   // the copy as it was on the original.
   bool ArrangeMakeClipSourceUnique(uint64_t clipId)
   {
      Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
      if (c == nullptr)
         return false;

      // One FULL checkpoint for the whole button press, not ArrangeEdit's
      // arrange-only snapshot: this changes the graph (a node appears) and
      // the timeline (srcUid moves) together, and an arrange-only undo would
      // put srcUid back while leaving the copy stranded on the canvas.
      // Suppressing for the duration also stops SpawnNode pushing a second
      // checkpoint of its own, which would make one button two undos.
      const bool wasSuppressed = gSuppressUndoCheckpoints;
      PushUndoCheckpoint();
      gSuppressUndoCheckpoints = true;
      struct Restore
      {
         bool prev;
         ~Restore() { gSuppressUndoCheckpoints = prev; }
      } restore{ wasSuppressed };

      if (c->sampleDropped)
      {
         ArrangeRespawnCloneNode(clipId);
         ArrangeCommitEdit();
         return true;
      }

      GraphNode* srcNode = FindNodeByUid(c->srcUid);
      if (srcNode == nullptr)
         return false;

      // Resolved before SpawnNode, which can reallocate gNodes and invalidate
      // every GraphNode* taken above.
      const int origIndex = srcNode->index;
      const std::string typeName = srcNode->typeName;
      const std::string category = srcNode->category;
      const bool showParams = srcNode->showParams;
      const bool showMiniViewport = srcNode->showMiniViewport;
      const bool showAdvancedParams = srcNode->showAdvancedParams;
      INode* srcImpl = srcNode->node.get();

      ClusterClipboard cluster;
      CaptureClusterLinks({ origIndex }, cluster);

      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      const ImVec2 origPos = ed::GetNodePosition(srcNode->NodeId());
      ed::SetCurrentEditor(prevEditor);
      const ImVec2 pos = FindFreeSpawnPosition(ImVec2(origPos.x + 60.0f, origPos.y + 60.0f));

      GraphNode* copy = SpawnNode(typeName, category, pos.x, pos.y);
      if (copy == nullptr)
         return false;

      CopyParams(copy->node.get(), srcImpl);
      // Same identity divergence Shift+D applies: a copy that kept the
      // original's seed/ownership map would not be an independent voice, it
      // would be the original running twice.
      if (auto* rn = dynamic_cast<RandomNode*>(copy->node.get()))
         rn->seed = RandomNode::NextSeed();
      if (auto* fgn = dynamic_cast<FieldGraphNode*>(copy->node.get()))
      {
         fgn->SetUid(FieldGraphNode::NewUid());
         fgn->Ownership() = Field::GraphOwnershipMap();
         fgn->ownershipText.clear();
      }
      copy->showParams = showParams;
      copy->showMiniViewport = showMiniViewport;
      copy->showAdvancedParams = showAdvancedParams;

      std::map<int, GraphNode*> newByOrig;
      newByOrig[origIndex] = copy;
      ApplyClusterLinks(newByOrig, cluster);

      // Re-found: ApplyClusterLinks spawns nothing, but FindClip's pointer
      // predates SpawnNode's possible reallocation of unrelated storage, and
      // re-finding is cheaper than reasoning about which containers moved.
      if (Arrange::Clip* live = Arrange::FindClip(gArrange, clipId))
      {
         live->srcUid = copy->uid;
         gArrange.revision++;
      }
      ArrangeCommitEdit();
      return true;
   }


   // Main thread, once per frame (called from DrawArrangePanelContent - the
   // Arrange panel is the only consumer of import results, same as
   // SampleScanner/PluginScanner only ever poll from the panel that shows
   // their state). Adopts any decode that finished since the last poll: the
   // node gets the real buffer/handle/pixels, and syncToTempo clips get
   // their placeholder length corrected to the file's real duration.
   void ArrangePollMediaImports()
   {
      if (gArrangePendingImports.empty())
         return;

      std::vector<Arrange::MediaImportResult> results = Arrange::GetMediaImportManager().PollResults();
      if (results.empty())
         return;

      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());

      for (Arrange::MediaImportResult& r : results)
      {
         const auto it = std::find_if(gArrangePendingImports.begin(), gArrangePendingImports.end(),
            [&](const ArrangePendingImport& p) { return p.jobId == r.jobId; });
         if (it == gArrangePendingImports.end())
         {
            // Nothing is tracking this job anymore (e.g. the clip/node was
            // deleted while the decode was still in flight) - just release
            // whatever it decoded.
            delete r.audioBuffer;
            if (r.videoHandle != nullptr)
               Platform::VideoClose(r.videoHandle);
            continue;
         }

         const ArrangePendingImport pending = *it;
         gArrangePendingImports.erase(it);

         GraphNode* gn = FindNodeByUid(pending.nodeUid);
         if (!r.success || gn == nullptr)
         {
            delete r.audioBuffer;
            if (r.videoHandle != nullptr)
               Platform::VideoClose(r.videoHandle);
            if (!r.success)
               printf("Arrange media import failed for %s: %s\n", r.path.c_str(), r.error.c_str());
            // Leave the clip in place but no longer pending - it just stays
            // silent/blank, same as any other clip whose node never loaded.
            if (Arrange::Clip* c = Arrange::FindClip(gArrange, pending.clipId))
            {
               c->importPending = false;
               gArrange.revision++;
            }
            continue;
         }

         AudioFileNode* audioFileNode = nullptr;
         if (pending.kind == Arrange::ImportMediaKind::Audio)
         {
            audioFileNode = static_cast<AudioFileNode*>(gn->node.get());
            audioFileNode->OpenFromDecoded(r.path, r.audioBuffer);
         }
         else if (pending.kind == Arrange::ImportMediaKind::Video)
            static_cast<VideoSourceNode*>(gn->node.get())->OpenFromHandle(r.path, r.videoHandle, r.audioBuffer);
         else
            static_cast<ImageSourceNode*>(gn->node.get())->LoadFromDecoded(r.imagePixels, r.width, r.height, r.path);

         if (Arrange::Clip* c = Arrange::FindClip(gArrange, pending.clipId))
         {
            c->importPending = false;
            // A clone (paste/duplicate/split) already has a correct
            // sampleBpm/origBpm/length/sourceDurationSeconds copied from the
            // clip it was cloned from - re-estimating here from the fresh
            // decode would just be a second, possibly-different guess at the
            // same file's native tempo, silently changing the clone's length
            // and playback ratio out from under the user right after the
            // paste/duplicate/split that made it. Only a genuinely new drop
            // (ArrangeImportMediaFile) needs the first-time estimate below.
            if (!pending.isClone)
            {
            // Step 3: the persisted natural duration and estimated native
            // tempo a later Sample BPM edit recomputes length from
            // (SampleClipLengthTicks) - captured once here, never touched by
            // a later project-tempo change (see SampleClipLengthTicks's own
            // comment for why that has to be true for both sync states).
            if (pending.kind == Arrange::ImportMediaKind::Audio && r.durationSeconds > 0.0)
            {
               c->sourceDurationSeconds = r.durationSeconds;
               bool detected = false;
               const float estimatedBpm = ArrangeEstimateSampleBpm(audioFileNode ? audioFileNode->Buffer() : nullptr,
                                                                   r.path, (float)bpm, &detected);
               c->sampleBpm = estimatedBpm;
               // origBpm is the analysis result shown in Clip Settings; <= 0
               // means nothing was detected (the Sample BPM field then holds
               // the project tempo as a neutral starting point).
               c->origBpm = detected ? estimatedBpm : 0.0f;
               // A clear beat means a loop the user almost certainly wants on
               // the grid, so it arrives synced (DAW auto-warp); no beat means
               // a one-shot or free-time recording, which arrives at native
               // speed. Either way the box holds the whole file.
               c->syncToTempo = detected;
               c->length = Arrange::SampleClipLengthTicks(
                  c->sourceDurationSeconds,
                  Arrange::SampleSourceBpm(c->syncToTempo, c->sampleBpm, bpm));
               printf("Arrange sample import: %s duration %.3fs, BPM %s %.2f, sync %s\n", r.path.c_str(),
                      (double)r.durationSeconds, detected ? "detected" : "not detected (using project tempo)",
                      (double)estimatedBpm, c->syncToTempo ? "on" : "off");
            }
            // Video/Image have no Sample BPM concept - their length is just
            // the file's natural duration at the current project tempo.
            else if (pending.kind != Arrange::ImportMediaKind::Image && r.durationSeconds > 0.0)
               c->length = std::max<Arrange::Tick>(1, Arrange::SecondsToTicks(r.durationSeconds, bpm));
            }
            // Step 2: the Sample's static waveform, computed once from the
            // fully-decoded source right here (before any BPM warp is ever
            // applied to c->length) - see ArrangeComputeSampleStaticWave's
            // own comment. Only meaningful for a Sample; a manually-patched
            // Audio Clip keeps using the live-fill gArrangeClipWaves cache.
            if (pending.kind == Arrange::ImportMediaKind::Audio && c->sampleDropped && audioFileNode != nullptr)
               ArrangeComputeSampleStaticWave(c->id, c->srcUid, c->srcOutput, c->start, c->length,
                                               ArrangeSampleEffBpm(*c), c->sourceOffsetSeconds,
                                               audioFileNode->Buffer());
            gArrange.revision++;
         }
         AudioTopologyRequest::Request();
      }
   }


   std::string ArrangeRenderUniquePath(const std::string& path)
   {
      const size_t dot = path.rfind('.');
      const size_t slash = path.find_last_of("/\\");
      const bool dotInName = dot != std::string::npos && (slash == std::string::npos || dot > slash);
      const std::string stem = dotInName ? path.substr(0, dot) : path;
      const std::string ext = dotInName ? path.substr(dot) : std::string();
      std::error_code ec;
      for (int n = 2; n < 1000; n++)
      {
         const std::string candidate = stem + " (" + std::to_string(n) + ")" + ext;
         if (!std::filesystem::exists(AppPaths::FsPath(candidate), ec) && !ArrangeRenderPathQueued(candidate, 0))
            return candidate;
      }
      return path;
   }


   // Builds a render job scoped to a lane subset - shared by "Render Track"
   // (a single lane) and "Render Group" (a group's recursive lane subtree),
   // both on the arrange header context menus. A free function, not a lambda
   // local to DrawArrangePanelContent's render-settings popup, so it stays
   // reachable from the (separately scoped) context-menu code below it: uses
   // gArrange.settings/ArrangeRenderableEndTick directly rather than that
   // popup's own rset/rangeA/rangeB locals, and always covers the whole
   // timeline (WP7's per-arrangement custom range only applies to the
   // whole-project "Render" button, not a one-off track/group take).
   // Video/audio sources are always forced to Timeline (a lane subset isn't
   // a Canvas concept), and output goes under a name derived from the
   // track/group, uniquified against anything already there or queued so it
   // never collides with the whole-project take's own path.
   ArrangeRenderJob ArrangeBuildLaneScopedRenderJob(const std::vector<uint64_t>& laneIds, const std::string& baseName)
   {
      ArrangeRenderJob job;
      job.rangeKind = kArrangeRangeWhole;
      job.startTick = 0;
      job.endTick = ArrangeRenderableEndTick();
      job.width = gArrange.settings.renderWidth;
      job.height = gArrange.settings.renderHeight;
      job.fps = gArrange.settings.renderFps;
      job.sampleRate = (int)llround(ArrangeRenderActiveSampleRate());
      job.laneScope = laneIds;
      job.audioSource = kArrangeAudioTimeline;
      job.canvasVideoUid = 0;

      bool hasVideo = false;
      for (uint64_t lid : laneIds)
      {
         const Arrange::Lane* ln = Arrange::FindLane(gArrange, lid);
         if (ln != nullptr && ln->type == Arrange::kLaneVideo) { hasVideo = true; break; }
      }
      job.videoSource = hasVideo ? kArrangeVideoTimeline : kArrangeVideoNone;
      job.format = hasVideo ? (gArrange.settings.renderFormat == 1 ? 1 : 0) : 2;

      std::string folder = gArrange.settings.renderFolder;
      if (folder.empty())
      {
         folder = AppPaths::DesktopDir();
      }
      while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\'))
         folder.pop_back();
      std::string safeName = baseName;
      for (char& ch : safeName)
         if (ch == '/' || ch == '\\') ch = '_';
      const char* ext = hasVideo ? (gArrange.settings.renderFormat == 1 ? ".mov" : ".mp4") : ".wav";
      job.path = ArrangeRenderUniquePath(folder + "/" + safeName + ext);
      return job;
   }


   // Queues a lane-scoped job ahead of anything already parked - same
   // "Render Now" semantics as the main panel's button.
   void ArrangeCommitLaneScopedRenderJob(ArrangeRenderJob job)
   {
      job.id = gArrangeRenderNextJobId++;
      job.status = kArrangeJobQueued;
      gArrangeRenderQueue.insert(gArrangeRenderQueue.begin(), job);
      gArrangeRenderQueueRunning = true;
   }
}
