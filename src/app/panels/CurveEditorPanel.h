// Curve Editor panel (docs/plans/curve-editor/README.md §8.2-§8.5): a docked editor for MSEG nodes, one page per
// node. The node is the model; the panel keeps only its view (zoom, tool, selection). Kept out of AppShared.h so
// touching the panel recompiles only the stage files that place it.
#pragma once

#include <cstdint>

#include "imgui.h"

namespace app
{
   extern bool gCurveEditorOpen;
   extern int gCurveEditorDock;       // 0 bottom, 1 right, 2 left, 3 top (the Terminal's codes)
   extern float gCurveEditorHeight;   // bottom and top
   extern float gCurveEditorWidth;    // right and left
   extern bool gCurveEditorClaimedKeys; // the pointer is over the panel: its keys, not the canvas's
   extern ImVec2 gCurveEditorRectMin, gCurveEditorRectMax;

   void DrawCurveEditorDocked(const char* id, const ImVec2& size);
   void CurveEditorOpenNode(uint64_t uid); // open the panel on that MSEG node's page
}
