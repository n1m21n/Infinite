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

   constexpr ImU32 U32(const Rgba8& c) { return IM_COL32(c.r, c.g, c.b, c.a); }

   constexpr ImVec4 V4(const Rgbaf& c) { return ImVec4(c.r, c.g, c.b, c.a); }

   constexpr ImVec4 V4(const Pairf& p, bool light)
   {
      const Rgbaf& c = light ? p.light : p.dark;
      return ImVec4(c.r, c.g, c.b, c.a);
   }
}
