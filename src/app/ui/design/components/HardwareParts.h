// HardwareParts: drawn 3D hardware pieces for instrument panels (Drum panel first; any hardware-style
// panel may use them). Same light model as the knobs in ModWidgets: light from above, a soft drop
// shadow below, a lit top edge, a dark lower lip.
//   Key   - a raised key cap; pressed sits lower; lit = a backlit key in `litCol` with a glow.
//   Led   - a lens in a dark bezel; lit glows with a specular dot.
//   Well  - a recess (LCD glass, a tray): inner shadow at the top, a light catch on the bottom edge.
//   Plate - a raised housing panel: drop shadow, sheen on the top, edge highlight.
// Drawing only; the caller owns interaction.
#pragma once
#include <imgui.h>
#include <algorithm>
#include "app/AppShared.h"
#include "app/ui/design/Tokens.gen.h"

namespace HardwareParts
{
   inline ImU32 Ink(float r, float g, float b, float a) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }
   inline ImU32 Black(float a) { return Ink(0.0f, 0.0f, 0.0f, a); }
   inline ImU32 White(float a) { return Ink(1.0f, 1.0f, 1.0f, a); }

   // rgb * k (k > 1 brightens), alpha kept.
   inline ImU32 Shade(ImU32 c, float k)
   {
      ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
      return Ink(std::min(1.0f, v.x * k), std::min(1.0f, v.y * k), std::min(1.0f, v.z * k), v.w);
   }

   inline ImU32 Mix(ImU32 a, ImU32 b, float t)
   {
      const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
      t = std::clamp(t, 0.0f, 1.0f);
      return Ink(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t);
   }

   inline ImU32 KeyBody() { return app::IsThemeLight() ? tok::U32(tok::pal::c_F2F5FAFF) : tok::U32(tok::pal::c_242630FF); }
   inline ImU32 PlateBody() { return app::IsThemeLight() ? tok::U32(tok::pal::c_DCE0EAFF) : tok::U32(tok::pal::c_16171EFF); }
   inline ImU32 WellBody() { return app::IsThemeLight() ? tok::U32(tok::pal::c_DCE1EBFF) : tok::U32(tok::pal::c_0B0C10FF); }

   // Soft vertical fade inside [mn, mx]: `top` colour at the top edge to transparent at the bottom.
   inline void Fade(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 top)
   {
      const ImU32 clear = top & ~IM_COL32_A_MASK;
      dl->AddRectFilledMultiColor(mn, mx, top, top, clear, clear);
   }

   inline void Key(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 body, bool pressed = false, ImU32 litCol = 0, float lit = 0.0f,
                   float r = 3.0f)
   {
      const bool light = app::IsThemeLight();
      const float lip = pressed ? 1.0f : 2.5f;
      const float sink = pressed ? 1.5f : 0.0f;
      lit = std::clamp(lit, 0.0f, 1.0f);
      const ImU32 face = lit > 0.0f ? Mix(body, litCol, lit) : body;
      // Glow spill around a lit key.
      if (lit > 0.02f)
         for (int i = 3; i >= 1; i--)
         {
            const float g = 1.5f * (float)i;
            dl->AddRectFilled(ImVec2(mn.x - g, mn.y - g), ImVec2(mx.x + g, mx.y + g), (litCol & ~IM_COL32_A_MASK) | ((ImU32)(lit * 26.0f / (float)i) << IM_COL32_A_SHIFT), r + g);
         }
      if (!pressed)
         dl->AddRectFilled(ImVec2(mn.x + 0.5f, mn.y + 2.0f), ImVec2(mx.x + 0.5f, mx.y + 2.0f), Black(light ? 0.18f : 0.55f), r);
      dl->AddRectFilled(ImVec2(mn.x, mn.y + sink), mx, Shade(face, light ? 0.72f : 0.55f), r); // lip
      const ImVec2 fmn(mn.x, mn.y + sink), fmx(mx.x, mx.y - lip);
      dl->AddRectFilled(fmn, fmx, face, r);
      const float h = fmx.y - fmn.y;
      if (h > 4.0f)
         Fade(dl, ImVec2(fmn.x + 1.0f, fmn.y + 1.0f), ImVec2(fmx.x - 1.0f, fmn.y + h * 0.55f), White(lit > 0.0f ? 0.30f : (light ? 0.55f : 0.09f)));
      dl->AddLine(ImVec2(fmn.x + r, fmn.y + 0.5f), ImVec2(fmx.x - r, fmn.y + 0.5f), White(lit > 0.0f ? 0.55f : (light ? 0.9f : 0.16f)));
      dl->AddRect(ImVec2(mn.x, mn.y + sink), mx, Black(light ? 0.16f : 0.45f), r);
   }

   inline void Led(ImDrawList* dl, ImVec2 c, float rad, ImU32 col, float lit)
   {
      lit = std::clamp(lit, 0.0f, 1.0f);
      dl->AddCircleFilled(ImVec2(c.x, c.y + 0.5f), rad + 1.8f, Black(app::IsThemeLight() ? 0.35f : 0.7f));
      dl->AddCircle(c, rad + 1.8f, White(0.08f));
      if (lit > 0.02f)
      {
         const ImU32 rgb = col & ~IM_COL32_A_MASK;
         dl->AddCircleFilled(c, rad + 5.0f, rgb | ((ImU32)(lit * 30.0f) << IM_COL32_A_SHIFT));
         dl->AddCircleFilled(c, rad + 2.8f, rgb | ((ImU32)(lit * 70.0f) << IM_COL32_A_SHIFT));
      }
      dl->AddCircleFilled(c, rad, Mix(Shade(col, 0.25f), col, lit));
      dl->AddCircleFilled(ImVec2(c.x - rad * 0.35f, c.y - rad * 0.35f), rad * 0.38f, White(0.20f + 0.45f * lit));
   }

   inline void Well(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 fill, float r = 3.0f)
   {
      const bool light = app::IsThemeLight();
      dl->AddRectFilled(mn, mx, fill, r);
      Fade(dl, ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mn.y + std::min(6.0f, (mx.y - mn.y) * 0.4f)), Black(light ? 0.14f : 0.45f));
      dl->AddLine(ImVec2(mn.x + r, mx.y - 0.5f), ImVec2(mx.x - r, mx.y - 0.5f), White(light ? 0.9f : 0.10f));
      dl->AddRect(mn, mx, Black(light ? 0.18f : 0.55f), r);
   }

   inline void Plate(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 fill, float r = 5.0f)
   {
      const bool light = app::IsThemeLight();
      dl->AddRectFilled(ImVec2(mn.x, mn.y + 2.5f), ImVec2(mx.x, mx.y + 2.5f), Black(light ? 0.12f : 0.45f), r);
      dl->AddRectFilled(mn, mx, fill, r);
      Fade(dl, ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mn.y + (mx.y - mn.y) * 0.35f), White(light ? 0.45f : 0.045f));
      dl->AddLine(ImVec2(mn.x + r, mn.y + 0.5f), ImVec2(mx.x - r, mn.y + 0.5f), White(light ? 0.9f : 0.12f));
      dl->AddRect(mn, mx, Black(light ? 0.14f : 0.5f), r);
   }
}
