// Tip: the tooltip that always shows (chunk 3, one tooltip part). Help tips - what a control does, its shortcut - go
// through HelpTip() (opt-in in Settings, after tok::motion_tooltip_delay). Everything else uses Tip: an icon-only
// button's name, a diagnostic (why a cable is refused, an error), a live readout. Shown at once, wrapped at
// tok::tip_wrap_em. Never write ImGui::SetTooltip / BeginTooltip outside the kit (ratchet lint `tooltips`).
#pragma once
#include <cstdarg>
#include "app/ui/design/Tokens.gen.h"
#include "imgui.h"

namespace Tip
{
   namespace detail
   {
      inline bool& Open()
      {
         static bool b = false;
         return b;
      }
   }

   // A tooltip with custom content: Begin, draw, End. Safe to call End even when Begin returned false.
   inline bool Begin()
   {
      detail::Open() = ImGui::BeginTooltip();
      if (detail::Open())
         ImGui::PushTextWrapPos(ImGui::GetFontSize() * tok::tip_wrap_em);
      return detail::Open();
   }
   inline void End()
   {
      if (!detail::Open())
         return;
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
      detail::Open() = false;
   }

   inline void ShowV(const char* fmt, va_list args)
   {
      if (!Begin())
         return;
      ImGui::TextV(fmt, args);
      End();
   }
   inline void Show(const char* fmt, ...) IM_FMTARGS(1);
   inline void Show(const char* fmt, ...)
   {
      va_list args;
      va_start(args, fmt);
      ShowV(fmt, args);
      va_end(args);
   }
}
