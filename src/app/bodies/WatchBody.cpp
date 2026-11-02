// Apple Watch node card: status, watch picker, Recenter, the three params, then every output as a live meter
// (roll, pitch and yaw included).
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "platform/WatchLink.h"

namespace app
{
   void DrawWatchParams(WatchNode* n)
   {
      const float colW = kParamWidth;
      const float gutter = 16.0f;
      ImGui::TextDisabled("%s", n->Status().c_str());

      NodeSeparator("watch", colW * 2 + gutter);
      int current = 0;
      const std::vector<std::string> names = n->WatchChoices(current);
      ImGui::BeginGroup();
      DropdownButton("watch", names, current, [n](int i)
      {
         const std::vector<WatchLink::WatchInfo> seen = WatchLink::ListWatches();
         if (i == 0)
            n->watch.clear();
         else if (i - 1 < (int)seen.size())
            n->watch = seen[i - 1].name;   // the saved-but-absent entry is last and already selected
      }, colW);
      if (ActionButton::Draw("Recenter", ImVec2(colW, 0)))
         n->Recenter();
      ImGui::EndGroup();
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();
      ModSlider("smooth", &n->smooth, 0.0f, 1.0f, "%.2f", colW);
      ModSlider("sens", &n->sens, 0.5f, 2.0f, "%.2f", colW);
      ModSlider("range", &n->range, 15.0f, 180.0f, "%.0f", colW);
      ImGui::EndGroup();

      NodeSeparator("outputs", colW * 2 + gutter);
      DrawOutputMeters(n, colW, 2, gutter);
   }
}
