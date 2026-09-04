#pragma once

#include <memory>
#include <string>

#include "INode.h"

// Editor-side wrapper around an INode: owns it, and carries the stable integer
// ids imgui-node-editor needs for the node itself and each of its pins.
//
// Id layout keeps pin<->node lookup arithmetic-only. Node i owns the block
// [i*1050, i*1050+1049]:
//    +0             the node
//    +1 .. +99      image input pins  (slot = offset-1)
//    +100 .. +899   parameter pins    (param = offset-100)
//    +900 .. +989   colour pins       (colour = offset-900)
//    +990 .. +1049  output pins       (output = offset-990), 60 slots
// Link ids live in their own range and are tracked separately, so they can never
// collide with node or pin ids.
//
// Colour pins deliberately sit in a block of their own rather than sharing the
// parameter counter. Parameter indices are patch-file keys, so slipping colour
// pins into the same sequence would renumber every slider that follows a swatch
// and silently repoint the modulation in existing patches.
//
// The output block used to stop at +998 (kStride was 1000), which only fit 9
// outputs before OutputPinId() overflowed into the next node's block (offset
// wraps past kStride back to 0) - silently corrupting the highest-numbered
// output pin(s) on any node with 10+ outputs (ImageAnalyzeNode's `cy`,
// AudioAnalyzeNode's `b6`-`b8`). kStride was widened to 1050 to give the
// output block real headroom instead of just enough for what exists today.
struct GraphNode
{
   static const int kStride = 1050;
   static const int kParamBase = 100;
   static const int kColorBase = 900;
   static const int kOutputBase = 990; // outputs occupy 990..1049

   std::unique_ptr<INode> node;
   std::string typeName;
   std::string category;
   int index = 0;
   bool showParams = false; // params start collapsed so the preview leads
   // Per-node mini 3D viewport (geometry-producing nodes only). On by default
   // for a new node - seeing what a node actually produced is worth more than
   // the render cost, and NodeViewport::Render already skips redrawing when
   // nothing changed, so an idle viewport is cheap. Still a per-node toggle
   // (via ViewportToggle/context menu) for anyone who wants to turn it off.
   bool showMiniViewport = true;
   // Audio nodes only (see docs/plans/audio/audio-node-ui-system.md §1):
   // Tier 1 params sit in an always-visible compact grid, so "params visible
   // at all" (showParams, above - unused by audio nodes) and "showing the
   // full Tier 2 set" are two different questions. Starts collapsed, same
   // reasoning as showParams.
   bool showAdvancedParams = false;
   bool hasModulatedParams = false; // recomputed each frame, drives the collapsed "mod" tag
   bool hasBipolarParams = false;   // ditto: true if any binding on this node is bipolar - "mod±"
   bool hasPaletteColors = false;   // ditto, for the collapsed "pal" tag
   // ditto: true if any param on this node carries a typed expression. Not a
   // tag of its own - it exists so a collapsed node with a live expression
   // still runs the register-only params pass that keeps it evaluating (see
   // gParamRegisterOnly in main.cpp).
   bool hasExpressionParams = false;
   // ditto: true if any param on this node is bound to the performance matrix.
   // Keeps collapsed nodes evaluating and registering into FrameParams().
   bool hasPerfPanelParams = false;

   // Where to place the node the first frame it appears (canvas coords).
   float spawnX = 0.0f;
   float spawnY = 0.0f;
   bool needsPosition = true;

   // Build step 15 (FieldGraphNode "Instrument Mode" encapsulation): true
   // for a node mounted by a FieldGraphNode in encapsulated mode. Canvas-
   // presentation state, same category as spawnX/spawnY/liveX/liveY above,
   // not a property of the INode itself - a hidden node still cooks, still
   // registers its params for modulation, and still counts toward audio
   // topology every frame exactly like a visible node; only its node-editor
   // box (ed::BeginNode/EndNode, so also picking/dragging/selection) is
   // skipped. No existing "hide from canvas" flag was found on this struct
   // to piggyback on (checked first, per that step's own doc) - this is the
   // minimal new one.
   bool hiddenFromCanvas = false;

   // The node's live canvas position, refreshed every frame while the editor
   // draws it. Saving reads this rather than calling ed::GetNodePosition,
   // because saving can be triggered from the menu bar or a keyboard shortcut,
   // both of which run outside ed::SetCurrentEditor - and the editor's getters
   // dereference the current context without checking it for null.
   float liveX = 0.0f;
   float liveY = 0.0f;

   int NodeId() const { return index * kStride; }
   int InputPinId(int slot) const { return index * kStride + 1 + slot; }
   int ParamPinId(int paramIndex) const { return index * kStride + kParamBase + paramIndex; }
   int ColorPinId(int colorIndex) const { return index * kStride + kColorBase + colorIndex; }
   int OutputPinId(int output = 0) const { return index * kStride + kOutputBase + output; }

   static int NodeIndexFromPin(int pinId) { return pinId / kStride; }
   static int OffsetFromPin(int pinId) { return pinId % kStride; }
   static bool IsOutputPin(int pinId)
   {
      const int off = OffsetFromPin(pinId);
      return off >= kOutputBase && off < kStride;
   }
   static int OutputIndexFromPin(int pinId) { return OffsetFromPin(pinId) - kOutputBase; }
   static bool IsParamPin(int pinId)
   {
      int off = OffsetFromPin(pinId);
      return off >= kParamBase && off < kColorBase;
   }
   static bool IsColorPin(int pinId)
   {
      int off = OffsetFromPin(pinId);
      return off >= kColorBase && off < kOutputBase;
   }
   static int ColorIndexFromPin(int pinId) { return OffsetFromPin(pinId) - kColorBase; }
   static bool IsInputPin(int pinId)
   {
      int off = OffsetFromPin(pinId);
      return off >= 1 && off < kParamBase;
   }
   static int InputSlotFromPin(int pinId) { return OffsetFromPin(pinId) - 1; }
   static int ParamIndexFromPin(int pinId) { return OffsetFromPin(pinId) - kParamBase; }
};
