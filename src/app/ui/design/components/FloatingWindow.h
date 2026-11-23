// FloatingWindow: the one recipe every floating window in the app is made with (Settings, Template library, Help,
// Shortcuts, the Field editors, a popped-out panel, a settings window from a gear or a More chip):
//   - the elevated panel surface, a title bar with a close button, no collapse arrow;
//   - the form's 16 pt window pad (a popped-out panel draws its own inset, so it can turn the pad off);
//   - a first place (centred, or at a point with a pivot) and a first size; after that it stays where it was moved;
//   - Esc closes it while it has focus and nothing is being edited (a workspace window - a popped-out panel, the
//     terminal - keeps Esc for its own keys).
// Begin always pairs with End, whether or not Begin returned true.
#pragma once
#include <cfloat>
#include <vector>
#include "app/AppShared.h"
#include "app/ui/design/components/FormParts.h"

namespace FloatingWindow
{
   struct Spec
   {
      ImVec2 size{ 0.0f, 0.0f };          // first size; 0 = fit or ImGui's default
      bool place = true;                  // false: the caller (or ImGui) places it
      bool center = true;                 // first place: the middle of the screen
      ImVec2 pos{ 0.0f, 0.0f }, pivot{ 0.0f, 0.0f };   // else: this point, at this corner of the window
      ImGuiCond placeCond = ImGuiCond_FirstUseEver;
      ImVec2 minSize{ 0.0f, 0.0f }, maxSize{ FLT_MAX, FLT_MAX };
      ImGuiWindowFlags flags = 0;
      bool pad = true;                    // the form's window pad
      float titleInset = 0.0f;            // title text moved right (a window whose content sits inside a nav well)
      bool escCloses = true;
   };

   namespace detail
   {
      struct Frame
      {
         bool* open;
         bool esc;
      };
      inline std::vector<Frame>& Stack()
      {
         static std::vector<Frame> s;
         return s;
      }
   }

   inline bool Begin(const char* title, bool* open, const Spec& s = Spec())
   {
      if (s.place && s.center)
         ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), s.placeCond, ImVec2(0.5f, 0.5f));
      else if (s.place)
         ImGui::SetNextWindowPos(s.pos, s.placeCond, s.pivot);
      if (s.size.x > 0.0f && s.size.y > 0.0f)
         ImGui::SetNextWindowSize(s.size, ImGuiCond_FirstUseEver);
      if (s.minSize.x > 0.0f || s.minSize.y > 0.0f || s.maxSize.x < FLT_MAX || s.maxSize.y < FLT_MAX)
         ImGui::SetNextWindowSizeConstraints(s.minSize, s.maxSize);
      app::PushElevatedPanelStyle(/*isChild=*/false);
      if (s.pad)
         FormParts::PushWindowPad(s.titleInset);
      const bool visible = ImGui::Begin(title, open, ImGuiWindowFlags_NoCollapse | s.flags);
      if (s.pad)
         FormParts::PopWindowPad();
      detail::Stack().push_back({ open, s.escCloses });
      return visible;
   }

   inline void End()
   {
      const detail::Frame f = detail::Stack().back();
      detail::Stack().pop_back();
      if (f.esc && f.open != nullptr && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
          !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
         *f.open = false;
      ImGui::End();
      app::PopElevatedPanelStyle();
   }
}
