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
   extern uint64_t gCurveAssignUid; // the MSEG whose Assign is armed: the next knob clicked on the canvas binds to it

   extern bool gCurveRecordPicking; // Record (Any knob) waits for a knob click on the canvas
   void CurveEditorRecordPicked(int nodeIndex, int paramIndex);
   void CurveEditorAddMotionGroup(int kind); // 0 Position, 1 Rotation, 2 Scale: X Y Z pages bound to the canvas selection
   void DrawCurveEditorDocked(const char* id, const ImVec2& size);
   void CurveEditorOpenNode(uint64_t uid); // open the panel on that MSEG node's page
}
