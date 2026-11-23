// SettingsWindow: every setting of one thing (an adjustment, an effect, a layer style, a tool) in a small floating
// window, opened from that thing's gear. A card or bar keeps its one or two everyday controls; the rest live here.
// It is a floating window like the app's others: it stays open while you work on the canvas, moves by its title,
// and closes with its own close button, Esc while it has focus, or the gear again. One window per key; open state is
// kept for the session. Any panel can use it (the 2D View's Look cards and tool bars today).
#pragma once
#include <algorithm>
#include <map>
#include <string>
#include "app/AppShared.h"
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/components/FloatingWindow.h"

namespace SettingsWindow
{
   inline std::map<std::string, bool>& Opens()
   {
      static std::map<std::string, bool> m;
      return m;
   }
   inline bool IsOpen(const std::string& key)
   {
      const auto it = Opens().find(key);
      return it != Opens().end() && it->second;
   }
   inline void Toggle(const std::string& key)
   {
      bool& o = Opens()[key];
      o = !o;
   }
   inline void Close(const std::string& key) { Opens().erase(key); }
   inline void CloseAll() { Opens().clear(); }

   // The window's content width: the compact strip (tokens.json "compact").
   inline float Width() { return UiDensity::kPopoverW; }

   // Draws the window for `key` while it is open. `title` is shown in its title bar. `at` is where it first appears,
   // as the top-right corner (it opens to the left of the panel it came from); later it stays where it was moved.
   // Returns whether the body ran.
   template <class F> bool Draw(const std::string& key, const char* title, ImVec2 at, F&& body)
   {
      const auto it = Opens().find(key);
      if (it == Opens().end() || !it->second)
         return false;
      bool open = true;
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      FloatingWindow::Spec spec;
      spec.center = false;
      spec.pos = ImVec2(std::max(vp->WorkPos.x + Width(), at.x), at.y);
      spec.pivot = ImVec2(1.0f, 0.0f);
      spec.placeCond = ImGuiCond_Appearing;
      // The main Settings window's rule: controls keep one fixed width whatever the window does. So the width is
      // the strip's (rows never stretch) and the height resizes: it opens at its content's height (0 = fit once),
      // then keeps whatever height it is dragged to (a long list scrolls).
      spec.minSize = ImVec2(Width(), 60.0f);
      spec.maxSize = ImVec2(Width(), vp->WorkSize.y);
      spec.flags = ImGuiWindowFlags_NoSavedSettings;
      ImGui::SetNextWindowSize(ImVec2(Width(), 0.0f), ImGuiCond_Appearing);
      spec.pad = false;   // the compact strip: MoreRows sizes its column from the style's own window padding
      const std::string id = std::string(title) + "###setwin" + key;
      bool ran = false;
      if (FloatingWindow::Begin(id.c_str(), &open, spec))
      {
         body();
         ran = true;
      }
      FloatingWindow::End();
      if (!open)
         Opens()[key] = false;
      return ran;
   }
}
