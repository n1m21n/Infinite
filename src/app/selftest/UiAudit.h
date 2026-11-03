// UI audit (docs/plans/ui-system/ui-audit.md): debug builds record every ImGui item through Dear ImGui's
// test-engine hooks; widgets that draw more than ImGui knows about note it here. Release builds compile nothing.
#pragma once
#include "imgui.h"

namespace UiAudit
{
#ifdef IMGUI_ENABLE_TEST_ENGINE
   // A slider's value as 0..1 of its range and the well it draws (to hold against its hit rect).
   void NoteSlider(ImGuiID id, float frac, ImVec2 drawnMin, ImVec2 drawnMax);
#else
   inline void NoteSlider(ImGuiID, float, ImVec2, ImVec2) {}
#endif
}
