// Bridges generated tokens (Tokens.gen.h) to ImGui types. Hand-written; no values live here.
#pragma once
#include "app/ui/design/Tokens.gen.h"
#include "imgui.h"

namespace tok
{
   constexpr ImU32 U32(const Pair8& p, bool light)
   {
      const Rgba8& c = light ? p.light : p.dark;
      return IM_COL32(c.r, c.g, c.b, c.a);
   }

   // A paired role at another opacity.
   constexpr ImU32 U32(const Pair8& p, bool light, int alpha)
   {
      const Rgba8& c = light ? p.light : p.dark;
      return IM_COL32(c.r, c.g, c.b, alpha);
   }

   constexpr ImU32 U32(const Rgba8& c) { return IM_COL32(c.r, c.g, c.b, c.a); }

   // A fixed role at another opacity (an outline under on-canvas ink, a faint grid).
   constexpr ImU32 U32(const Rgba8& c, int alpha) { return IM_COL32(c.r, c.g, c.b, alpha); }

   constexpr ImVec4 V4(const Rgbaf& c) { return ImVec4(c.r, c.g, c.b, c.a); }

   constexpr ImVec4 V4(const Rgba8& c) { return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f); }

   constexpr ImVec4 V4(const Pair8& p, bool light) { return V4(light ? p.light : p.dark); }

   // An edge drawn as ink over the surface: black on light themes, white on dark, at the given opacities.
   constexpr ImVec4 InkEdge(bool light, float aLight, float aDark)
   {
      return light ? ImVec4(0, 0, 0, aLight) : ImVec4(1, 1, 1, aDark);
   }

   // The 1 pt hairline round cards, wells and banners.
   constexpr ImVec4 Hairline(bool light) { return InkEdge(light, alpha_hairline_light, alpha_hairline_dark); }

   constexpr ImVec4 V4(const Pairf& p, bool light)
   {
      const Rgbaf& c = light ? p.light : p.dark;
      return ImVec4(c.r, c.g, c.b, c.a);
   }

   // The one alpha helpers (chunk 3: one definition each). WithAlpha sets the alpha (0..1); ScaleAlpha multiplies
   // the alpha the colour already has; the ImVec4 form goes through ImGui, so the style's disabled dimming applies.
   inline ImU32 WithAlpha(ImU32 c, float a)
   {
      const float k = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
      return (c & ~IM_COL32_A_MASK) | ((ImU32)(k * 255.0f) << IM_COL32_A_SHIFT);
   }
   inline ImU32 WithAlpha(ImVec4 c, float a)
   {
      c.w = a;
      return ImGui::GetColorU32(c);
   }
   inline ImU32 ScaleAlpha(ImU32 c, float k)
   {
      const ImU32 base = (c >> IM_COL32_A_SHIFT) & 0xFF;
      return (c & ~IM_COL32_A_MASK) | ((ImU32)(k * (float)base) << IM_COL32_A_SHIFT);
   }
   // The theme's text colour at alpha a (faint fills and rules drawn in ink).
   inline ImU32 TextTint(float a)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      return ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, a));
   }
}
