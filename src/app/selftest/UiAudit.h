// UI audit (docs/plans/ui-system/ui-audit.md): debug builds record every ImGui item through Dear ImGui's
// test-engine hooks; widgets that draw more than ImGui knows about note it here. Release builds compile nothing.
#pragma once
#include <string>
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
   // Something drawn as part of the last item, or beside it: kind 0 the item's own text, 1 its icon (ink box, font
   // size, tip, glyph), 2 loose text on a tile row, 3 a form label, 4 a checkbox label (tip = the text), 5 a button's
   // text beside its icon. For the 2D View's layout checks.
   void NoteInk(int kind, ImVec2 mn, ImVec2 mx, float size = 0.0f, const char* tip = nullptr, const char* glyph = nullptr);
   // The last item is drawn as the chosen one of its group (a selected chip): clicking it again may rightly do nothing.
   void NoteOn();
   // Journeys (src/app/selftest/Journeys.cpp): the rect last frame of the nth (0-based) item whose label is `label`
   // (or whose icon tip is), in a window whose name contains `inWindow` (null or empty: any). Needs the item hooks on.
   bool FindItem(const char* label, const char* inWindow, int nth, ImVec2* mn, ImVec2* mx);
   // Names the last item for FindItem only (a dropdown, whose ImGui code registers no label): the audit does not
   // count it as a control to check or poke.
   void NoteName(const char* label);
   // Labels last frame that share `label`'s visible text or ## id: what a failed lookup nearly matched.
   std::string NearMiss(const char* label);
#else
   inline void NoteSlider(ImGuiID, float, ImVec2, ImVec2, float = 0.0f) {}
   inline void NoteKind(const char*, const char*, float = 0.0f, float = 0.0f) {}
   inline void NoteFill(ImVec4) {}
   inline void NoteNode(ImVec2, ImVec2, ImVec4) {}
   inline void NoteRow(float, float, float, float, int) {}
   inline void NoteInk(int, ImVec2, ImVec2, float = 0.0f, const char* = nullptr, const char* = nullptr) {}
   inline void NoteOn() {}
   inline bool FindItem(const char*, const char*, int, ImVec2*, ImVec2*) { return false; }
   inline void NoteName(const char*) {}
   inline std::string NearMiss(const char*) { return {}; }
#endif
}
