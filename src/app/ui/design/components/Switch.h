// Switch: on/off with immediate effect (C5), used in Settings and panels only; node bodies keep CheckBox. A 30x16 pill
// track in the field-well tint (accent when on) with a round thumb that eases between the ends. Drawing and hit target
// only, the same contract as CheckBox.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"

namespace Switch
{
   constexpr float kW = 30.0f;
   constexpr float kH = 16.0f;

   inline bool Box(const char* id, bool* v)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(kW, kH));
      if (clicked)
         *v = !*v;
      const ImGuiID aid = ImGui::GetItemID();
      const float hv = UiAnim::Hover(aid, ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const float onv = UiAnim::Value(aid + 1, *v ? 1.0f : 0.0f, *v ? tok::motion_on : tok::motion_off);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec2 q(p.x + kW, p.y + kH);
      dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.10f + 0.05f * hv)), kH * 0.5f);
      if (onv > 0.001f)
      {
         ImVec4 a = app::AccentEmphasisSelected();
         a.w *= onv;
         dl->AddRectFilled(p, q, ImGui::GetColorU32(a), kH * 0.5f);
      }
      const float r = kH * 0.5f - 3.0f;
      const float x = p.x + kH * 0.5f + onv * (kW - kH);
      const ImVec4 thumbOff(t.x, t.y, t.z, 0.65f);
      const ImVec4 thumb(thumbOff.x + (1.0f - thumbOff.x) * onv, thumbOff.y + (1.0f - thumbOff.y) * onv,
                         thumbOff.z + (1.0f - thumbOff.z) * onv, thumbOff.w + (1.0f - thumbOff.w) * onv);
      dl->AddCircleFilled(ImVec2(x, p.y + kH * 0.5f), r, ImGui::GetColorU32(thumb), 16);
      return clicked;
   }
}
