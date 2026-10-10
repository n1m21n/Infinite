// DropdownField: the closed face of a node dropdown (mode / scale / rate selectors in node bodies): the field well
// (same recess as Settings combos) with the current value; deliberately no chevron (owner decision). Drawing and hit target only; the
// caller opens the option list. Text colour is whatever ImGuiCol_Text is (the modulated amber is pushed by the caller).
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "app/ui/design/components/FieldWell.h"

namespace app { ImVec4 AccentEmphasisSelected(); }

namespace DropdownField
{
   // `caption` uses ImGui's "text##id" form. Returns true when clicked. `size.x` 0 = fit the text.
   // `label` (optional, plain text): the dim parameter name drawn inside the face on the left, value on the right -
   // the same grammar as the slider faces; it yields space to the value, never the other way round.
   inline bool Draw(const char* caption, ImVec2 size, const char* label = nullptr)
   {
      const float h = ImGui::GetFrameHeight();
      const float pad = tok::space_2;
      
      const char* end = ImGui::FindRenderedTextEnd(caption);
      const ImVec2 ts = ImGui::CalcTextSize(caption, end);
      const float w = size.x > 0.0f ? size.x : ts.x + 2.0f * pad;
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 mx(mn.x + w, mn.y + h);
      if (ImGui::ButtonLabelHook != nullptr)
         ImGui::ButtonLabelHook(caption); // headless --describe lists the buttons a node draws, as ImGui::Button does
      const bool clicked = ImGui::InvisibleButton(caption, ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      FieldWell::Draw(dl, mn, mx, hovered, ImGui::IsItemActive());
      const float textRight = mx.x - pad;
      const float labelW = (label != nullptr && label[0] != '\0') ? ImGui::CalcTextSize(label).x : 0.0f;
      const float avail = textRight - (mn.x + pad);
      // A caption that cannot show whole is dropped, never clipped to a sliver ("de", "for").
      const bool hasLabel = labelW > 0.0f && labelW + pad + ts.x <= avail;
      // Value keeps up to everything but a 24px label sliver; right-aligned when a label shares the face.
      float valueX = mn.x + pad;
      if (hasLabel)
      {
         const float valueW = std::min(ts.x, std::max(0.0f, avail - std::min(labelW + pad, 24.0f)));
         valueX = textRight - valueW;
         ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         dim.w *= 0.55f;
         dl->PushClipRect(mn, ImVec2(std::max(mn.x + pad, valueX - pad), mx.y), true);
         dl->AddText(ImVec2(mn.x + pad, mn.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(dim), label);
         dl->PopClipRect();
      }
      dl->PushClipRect(ImVec2(hasLabel ? valueX : mn.x, mn.y), ImVec2(textRight, mx.y), true);
      dl->AddText(ImVec2(valueX, mn.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), caption, end);
      dl->PopClipRect();
      return clicked;
   }

   // Segmented face for a short discrete choice (axis X/Y/Z, off/on): ONE hit item over the whole face, so every
   // hook that keys off the last item (right-click menus, hover-and-type, learn ring, Tab/arrow stepping, tooltip,
   // modulation drop rect) behaves exactly as it does on the dropdown face. The click picks the segment by x.
   // Returns the clicked option index, or -1. `label` is the dim caption inside on the left.
   inline int DrawSegments(const char* id, const std::vector<std::string>& opts, int current, float width, const char* label)
   {
      const float h = ImGui::GetFrameHeight();
      const float pad = tok::space_2;
      const float w = width > 0.0f ? width : 168.0f;
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 mx(mn.x + w, mn.y + h);
      if (ImGui::ButtonLabelHook != nullptr)
         ImGui::ButtonLabelHook(id); // headless --describe lists the buttons a node draws, as ImGui::Button does
      const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      FieldWell::Draw(dl, mn, mx, hovered, ImGui::IsItemActive());
      const bool hasLabel = label != nullptr && label[0] != '\0';
      float segL = mn.x + 2.0f;
      if (hasLabel)
      {
         ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         dim.w *= 0.55f;
         const float lw = ImGui::CalcTextSize(label).x;
         // segments get at least 60% of the face; the label yields
         const float maxLabel = w * 0.4f - pad;
         dl->PushClipRect(mn, ImVec2(mn.x + pad + maxLabel, mx.y), true);
         dl->AddText(ImVec2(mn.x + pad, mn.y + (h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(dim), label);
         dl->PopClipRect();
         segL = mn.x + pad + std::min(lw, maxLabel) + pad;
      }
      const int n = (int)opts.size();
      const float segW = (mx.x - 2.0f - segL) / (float)std::max(1, n);
      int clicked = -1;
      if (pressed && n > 0)
         clicked = std::clamp((int)((ImGui::GetIO().MousePos.x - segL) / segW), 0, n - 1);
      if (current >= 0 && current < n)
      {
         const ImVec4 a = app::AccentEmphasisSelected();
         dl->AddRectFilled(ImVec2(segL + segW * (float)current, mn.y + 2.0f), ImVec2(segL + segW * (float)(current + 1), mx.y - 2.0f),
                           ImGui::GetColorU32(a), tok::radius_pill);
      }
      for (int i = 0; i < n; ++i)
      {
         const ImVec2 ts = ImGui::CalcTextSize(opts[i].c_str());
         dl->AddText(ImVec2(std::round(segL + segW * ((float)i + 0.5f) - ts.x * 0.5f), mn.y + (h - ts.y) * 0.5f),
                     ImGui::GetColorU32(ImGuiCol_Text), opts[i].c_str());
      }
      return clicked;
   }
}
