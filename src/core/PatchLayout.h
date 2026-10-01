#pragma once

#include "Patch.h"

#include <map>

// Auto-layout for a patch whose nodes carry no `pos` record (hand-written or
// headless-authored files): without it every node loads stacked at 0,0.
//
// Pure data in, positions out - no ImGui, no node editor - so the GUI, the
// self-test and any future headless path share one placement rule. The GUI
// feeds it measured node sizes (ed::GetNodeSize) once the nodes have drawn;
// anything without a measurement falls back to EstimateSize.
//
// Layout: three horizontal bands (Picture, Sound, Modulation). Inside a band,
// columns run left->right by wiring depth (sources left, sinks right) and nodes
// stack top->bottom. A Comment is not part of the graph: `# near <id>` on the
// line before its node block puts it just above that node, `# band <name>`
// makes it the band's header (both read into NodeRecord::layoutHint).
//
// This never runs on --canonicalize/--validate: those write what was read.
namespace PatchLayout
{
   struct Size
   {
      float w = 0.0f, h = 0.0f;
   };

   struct Pos
   {
      float x = 0.0f, y = 0.0f;
   };

   // True when the patch has nodes and none of them had a `pos` line.
   bool NeedsLayout(const Patch::Data& data);

   // Generous canvas-unit size for a node that has not been measured yet.
   Size EstimateSize(const Patch::NodeRecord& node);

   // Position per NodeRecord::index. `measured` maps NodeRecord::index to a
   // drawn size; an entry with w or h <= 0, or no entry, uses EstimateSize.
   std::map<int, Pos> Compute(const Patch::Data& data, const std::map<int, Size>* measured = nullptr);

   // Data-level convenience: writes Compute's result into x/y and marks every
   // node hasPos. Returns false (and touches nothing) when NeedsLayout is false.
   bool Apply(Patch::Data& data, const std::map<int, Size>* measured = nullptr);
}
