// PanelRail: the fixed column down the window's right edge that opens and closes the panels (Library, Viewport,
// Modulation matrix, Performance, Arrangement). The shell window is narrowed by kWidth, so docked panels never meet it.
// Items are IconTile toggles on one centre line; the first item shares the top bar's centre line.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/Divider.h"
#include "app/ui/design/components/IconTile.h"

namespace PanelRail
{
   constexpr float kWidth = 44.0f; // tile 28 + space_2 either side

   // Begins the rail window against the right edge of the viewport work area; always pair with End().
   inline void Begin(const ImGuiViewport* vp)
   {
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kWidth, vp->WorkPos.y));
      ImGui::SetNextWindowSize(ImVec2(kWidth, vp->WorkSize.y));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::Begin("##panelrail", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
      ImGui::PopStyleVar(2);
      // The hairline that sets the rail apart from the canvas, below the top bar's own row.
      const ImVec2 p = ImGui::GetWindowPos();
      Divider::Vertical(UiLayout::Rect { p.x, p.y + tok::bar_h, 1.0f, ImGui::GetWindowHeight() - tok::bar_h }, 0.0f);
      ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
   }

   // One toggle, centred in the rail at `y` (window-local, the tile's top edge). Returns true on click.
   inline bool Item(const char* id, float y, const char* glyphOff, const char* glyphOn, bool on, const char* tooltip)
   {
      ImGui::SetCursorPos(ImVec2((kWidth - tok::tile) * 0.5f, y));
      const bool clicked = IconTile::Draw(id, glyphOff, glyphOn, on, tok::tile);
      if (ImGui::IsItemHovered())
         app::HelpTip("%s", tooltip);
      return clicked;
   }

   inline void End() { ImGui::End(); }
}
