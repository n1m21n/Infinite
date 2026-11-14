// Field Patcher panel (internal/plans/field-patcher/README.md §8): a docked box editor for Field programs, one page
// per Field Notes / Field Primitive / Field Modifier node, plus scratch pages with no node yet. The node's source text
// is the model; boxes are a view of it (core/field/FieldBoxGraph.h). Kept out of AppShared.h so touching the panel
// recompiles only the files that place it.
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"

namespace app
{
   extern bool gFieldPatcherOpen;
   extern int gFieldPatcherDock;          // 0 bottom, 3 top
   extern float gFieldPatcherHeight;
   extern bool gFieldPatcherClaimedKeys;  // the pointer is over the panel: its keys, not the canvas's
   extern bool gFieldPatcherMaximised;    // ⤢: the panel takes the whole window
   extern float gFieldPatcherShownHeight; // this frame's height (gFieldPatcherHeight, or the window when maximised)
   extern ImVec2 gFieldPatcherRectMin, gFieldPatcherRectMax; // the panel on screen this frame (Library drops)

   void DrawFieldPatcherDocked(const char* id, const ImVec2& size);
   void FieldPatcherOpenNode(uint64_t uid); // open the panel on that Field node's page
   void FieldPatcherOpenStarter(int index); // a scratch page with that starter (core/field/FieldPatcherStarters.h)

   // Scratch pages and pins, saved with the patch.
   std::string FieldPatcherSaveState();
   void FieldPatcherLoadState(const std::string& text);

   int FieldPatcherSelfCheck(); // INFINITE_FIELDPATCHERTEST: the panel's gestures on real nodes; returns failures
   // Key test hooks: the front page's canvas centre, its counts, and selecting a box (kind 0) or wire (kind 1).
   extern bool gFieldPatcherTestCmd; // key test: Cmd held (the backend re-reads real modifiers)
   bool FieldPatcherTestCanvasCentre(ImVec2& out);
   void FieldPatcherTestCounts(int& boxes, int& wires);
   void FieldPatcherTestSelect(int kind, int index);
}
