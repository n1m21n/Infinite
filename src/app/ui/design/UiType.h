// The only way UI code picks a text size or weight (docs/plans/ui-system/README.md, L1).
// Sizes come from tokens.json (type.caption/body/display, in ImGui points); weights are separate
// fonts registered by Startup. ImGui 1.92 rasterizes each (font, size) on demand, so any size is sharp.
#pragma once
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/UiDensity.h"
#include "imgui.h"

namespace UiType
{
   enum class Weight { Regular, Medium, Semibold, Count };
   enum class Size { Caption, Body, Display };

   // Token value for a role, in points (the UI scale is applied by the framebuffer scale, not here).
   inline float Px(Size s)
   {
      switch (s)
      {
      case Size::Caption: return UiDensity::IsCompact() ? UiDensity::M().text - 1.0f : tok::type_caption;
      case Size::Body: return UiDensity::M().text;
      case Size::Display: return tok::type_display;
      }
      return tok::type_body;
   }

   void Reset();                         // Startup, before re-registering after a font rebuild
   void Register(Weight w, ImFont* f);
   ImFont* Font(Weight w);               // falls back to Regular, then the current font
   int Registered();                     // how many weights are real fonts (not fallbacks)

   // Push a size and weight for the next widgets; always pair with Pop(). Prefer Scope.
   void Push(Size s, Weight w = Weight::Regular);
   void Pop();

   struct Scope
   {
      Scope(Size s, Weight w = Weight::Regular) { Push(s, w); }
      ~Scope() { Pop(); }
      Scope(const Scope&) = delete;
      Scope& operator=(const Scope&) = delete;
   };
}
