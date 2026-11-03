// UI audit (docs/plans/ui-system/ui-audit.md): debug builds record every ImGui item through Dear ImGui's
// test-engine hooks; widgets that draw more than ImGui knows about note it here. Release builds compile nothing.
#pragma once
#include "imgui.h"

namespace UiAudit
{
#ifdef IMGUI_ENABLE_TEST_ENGINE
   // A slider's value as 0..1 of its range and the well it draws (to hold against its hit rect).
   void NoteSlider(ImGuiID id, float frac, ImVec2 drawnMin, ImVec2 drawnMax, float valueTextW = 0.0f);
   // The last item's kind ("dropdown", "colour", "checkbox"; these are InvisibleButtons, which ImGui does not report) and label and, when it draws its own text, that text's width against the room for it.
   void NoteKind(const char* kind, const char* label, float textW = 0.0f, float availW = 0.0f);
   // The last item's resting fill (alpha blended over whatever is behind), for the P10 contrast budget.
   void NoteFill(ImVec4 fill);
   // One node's drawn box and body colour, in the canvas space its items are in.
   void NoteNode(ImVec2 mn, ImVec2 mx, ImVec4 bg);
   // One AudioKnobRow: the P1 grid (cell i spans x0 + i*cellW .. +cellW, rows span y0..y0+rowH).
   void NoteRow(float x0, float y0, float rowH, float cellW, int count);
#else
   inline void NoteSlider(ImGuiID, float, ImVec2, ImVec2, float = 0.0f) {}
   inline void NoteKind(const char*, const char*, float = 0.0f, float = 0.0f) {}
   inline void NoteFill(ImVec4) {}
   inline void NoteNode(ImVec2, ImVec2, ImVec4) {}
   inline void NoteRow(float, float, float, float, int) {}
#endif
}
