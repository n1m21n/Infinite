// StatusDot: a small filled dot pinned to the top-right corner of the last item. One dot says one thing:
// Ok (engine running), Warn (running with trouble), Error (failed). Colours are the semantic action roles,
// so the meaning is the same everywhere (G4/G5/G14). Never the only signal: callers keep text or a tooltip.
#pragma once
#include "imgui.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/AppShared.h"

namespace StatusDot
{
   enum class State { Ok, Warn, Error };

   inline void OnLastItem(State s)
   {
      const tok::Pair8& pair = s == State::Ok ? tok::action_go : (s == State::Warn ? tok::action_learn : tok::action_record);
      const ImVec2 mx = ImGui::GetItemRectMax();
      const ImVec2 mn = ImGui::GetItemRectMin();
      const float r = tok::status_r;
      const ImVec2 c(mx.x - tok::space_1 - r, mn.y + tok::space_1 + r);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddCircleFilled(c, r + 1.0f, ImGui::GetColorU32(ImGuiCol_PopupBg, tok::status_dot_on_last_item_im_gui_col_popup_bg_alpha), tok::status_dot_on_last_item_segments);
      dl->AddCircleFilled(c, r, tok::U32(pair, CategoryColors::IsThemeLight()), tok::status_dot_on_last_item_segments);
   }
}
