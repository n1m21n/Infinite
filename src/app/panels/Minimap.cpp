// Minimap (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   // Drawn inside the ed::Suspend() block alongside the popups, so plain
   // ImGui widgets work normally without the editor intercepting the mouse.
   void DrawMinimap()
   {
      if (!gMinimapEnabled || gNodes.empty())
         return;

      const float pad = 14.0f;
      const float w = gMinimapSize;
      const float h = gMinimapSize * 0.72f;

      ImVec2 origin;
      switch (gMinimapCorner)
      {
         case 0: origin = ImVec2(gGraphScreenTL.x + pad, gGraphScreenTL.y + pad); break;
         case 1: origin = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x - w - pad,
                                 gGraphScreenTL.y + pad); break;
         case 2: origin = ImVec2(gGraphScreenTL.x + pad,
                                 gGraphScreenTL.y + gGraphScreenSize.y - h - pad); break;
         default: origin = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x - w - pad,
                                  gGraphScreenTL.y + gGraphScreenSize.y - h - pad); break;
      }

      // World-space bounds of every node, in an assumed node footprint - the
      // editor does not expose node sizes outside BeginNode/EndNode, and a
      // minimap dot does not need to be pixel-accurate to be useful.
      //
      // Deliberately NOT including the current viewport in this box: before
      // the view has ever been fit to content (or after zooming way out) the
      // viewport can span a world area far larger than where the nodes
      // actually are, which would crush every node down into one corner to
      // make room for a mostly-empty viewport rectangle. The map fits the
      // content; the viewport indicator below is clipped to the panel
      // instead, the same way a game minimap lets the camera frustum run off
      // the edge rather than rescaling the whole map to contain it.
      const float kNodeW = 260.0f, kNodeH = 90.0f;
      float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
      for (const GraphNode& gn : gNodes)
      {
         minX = std::min(minX, gn.liveX); maxX = std::max(maxX, gn.liveX + kNodeW);
         minY = std::min(minY, gn.liveY); maxY = std::max(maxY, gn.liveY + kNodeH);
      }

      const ImVec2 viewWorldTL = ed::ScreenToCanvas(gGraphScreenTL);
      const ImVec2 viewWorldBR = ed::ScreenToCanvas(
         ImVec2(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y));

      const float worldW = std::max(1.0f, maxX - minX);
      const float worldH = std::max(1.0f, maxY - minY);
      const float margin = 10.0f;
      // Uniform, not per-axis: a per-axis fit would stretch node rectangles
      // and make the layout unrecognisable against the real canvas.
      const float scale = std::min((w - margin * 2.0f) / worldW, (h - margin * 2.0f) / worldH);

      auto toMinimap = [&](ImVec2 world) -> ImVec2
      {
         return ImVec2(origin.x + margin + (world.x - minX) * scale,
                       origin.y + margin + (world.y - minY) * scale);
      };

      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const bool isLight = (0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b > 0.5f);

      ImDrawList* dl = ImGui::GetForegroundDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h),
                        IM_COL32((int)(t.panelBg.r * 255), (int)(t.panelBg.g * 255), (int)(t.panelBg.b * 255), (int)(gMinimapOpacity * 255)), tok::radius_tile);
      dl->AddRect(origin, ImVec2(origin.x + w, origin.y + h),
                  IM_COL32((int)(t.border.r * 255), (int)(t.border.g * 255), (int)(t.border.b * 255), 200), tok::radius_tile, 0, 1.0f);

      for (const GraphNode& gn : gNodes)
      {
         const ImVec2 a = toMinimap(ImVec2(gn.liveX, gn.liveY));
         const ImVec2 b = toMinimap(ImVec2(gn.liveX + kNodeW, gn.liveY + kNodeH));
         const bool selected = ed::IsNodeSelected(gn.NodeId());
         dl->AddRectFilled(a, b, selected ? tok::U32(tok::pal::c_FFBE5AE6) : tok::U32(tok::pal::c_6E96D2C8), 2.0f);
         if (FindIsMatch(gn.index))
            dl->AddRect(ImVec2(a.x - 1.5f, a.y - 1.5f), ImVec2(b.x + 1.5f, b.y + 1.5f),
                        ImGui::GetColorU32(AccentEmphasisSelected()), 2.0f, 0, 2.0f);
         if (selected)
         {
            // Selection must read as more than a hue swap (HIG focus/selection
            // guidance) - add a bright outline so it's legible for colorblind
            // users and in poor viewing conditions, on top of the color change.
            dl->AddRect(a, b, isLight ? tok::U32(tok::pal::c_282C37FF) : tok::U32(tok::pal::c_FFFFFFFF), 2.0f, 0, 1.5f);
         }
      }

      // The current viewport, outlined - the one thing a static "map" view
      // gives you that scrolling around by feel does not. Clipped to the
      // panel: a viewport far outside the node bounds (unfit, or zoomed out
      // past every node) still draws as much of its edge as overlaps rather
      // than being allowed to warp the map's own scale.
      {
         dl->PushClipRect(origin, ImVec2(origin.x + w, origin.y + h), true);
         const ImVec2 a = toMinimap(viewWorldTL);
         const ImVec2 b = toMinimap(viewWorldBR);
         dl->AddRect(a, b, isLight ? tok::U32(tok::pal::c_282C37DC) : tok::U32(tok::pal::c_FFFFFFDC), 2.0f, 0, 1.5f);
         dl->PopClipRect();
      }

      // Click or drag inside the minimap to jump to whatever node is nearest
      // that point - there is no public API to pan to an arbitrary empty
      // spot, only to navigate to a node, so this snaps to the closest one.
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##minimap", ImVec2(w, h));
      if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         const ImVec2 mouse = ImGui::GetMousePos();
         const float worldX = minX + (mouse.x - origin.x - margin) / scale;
         const float worldY = minY + (mouse.y - origin.y - margin) / scale;

         const GraphNode* nearest = nullptr;
         float bestDist = 1e18f;
         for (const GraphNode& gn : gNodes)
         {
            const float dx = (gn.liveX + kNodeW * 0.5f) - worldX;
            const float dy = (gn.liveY + kNodeH * 0.5f) - worldY;
            const float dist = dx * dx + dy * dy;
            if (dist < bestDist) { bestDist = dist; nearest = &gn; }
         }
         if (nearest != nullptr)
         {
            ed::SelectNode(nearest->NodeId());
            ed::NavigateToSelection(false, 0.0f);
         }
      }
   }
}
