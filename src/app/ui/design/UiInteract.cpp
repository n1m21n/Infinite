#include "app/ui/design/UiInteract.h"
#include <unordered_set>

namespace UiInteract
{
   namespace
   {
      std::vector<Node> sBuilding, sLast;
      std::unordered_set<ImGuiID> sSeen;
      int sDup = 0, sDupLast = 0;
   }

   ImGuiID IdFor(const char* key) { return ImGui::GetID(key); }

   State Item(const char* key, const UiLayout::Rect& r, Role role, const char* label, bool enabled, const char* value)
   {
      State s;
      s.id = ImGui::GetID(key);
      if (!sSeen.insert(s.id).second)
         sDup++;
      ImGui::SetCursorScreenPos(ImVec2(r.x, r.y));
      ImGui::BeginDisabled(!enabled);
      s.clicked = ImGui::InvisibleButton(key, ImVec2(r.w, r.h)) && enabled;
      ImGui::EndDisabled();
      s.hovered = enabled && ImGui::IsItemHovered();
      s.down = enabled && ImGui::IsItemActive();
      s.focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
      sBuilding.push_back(Node{ s.id, key, label != nullptr ? label : "", value != nullptr ? value : "", role, enabled, r });
      return s;
   }

   void BeginFrame()
   {
      sLast.swap(sBuilding);
      sBuilding.clear();
      sSeen.clear();
      sDupLast = sDup;
      sDup = 0;
   }

   const std::vector<Node>& Tree() { return sLast; }

   const Node* Find(const char* key)
   {
      for (const Node& n : sLast)
         if (n.key == key)
            return &n;
      return nullptr;
   }

   int DuplicateKeys() { return sDupLast; }
}
