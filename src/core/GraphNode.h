#pragma once

#include <memory>
#include <string>

#include "INode.h"

// Editor-side wrapper around an INode: owns it, and carries the stable integer
// ids imgui-node-editor needs for the node itself and each of its pins.
//
// Id layout keeps pin<->node lookup arithmetic-only. Node i owns the block
// [i*1000, i*1000+999]:
//    +0            the node
//    +1 .. +99     image input pins  (slot = offset-1)
//    +100 .. +899  parameter pins    (param = offset-100)
//    +900 .. +989  colour pins       (colour = offset-900)
//    +990 .. +998  output pins
// Link ids live in their own range and are tracked separately, so they can never
// collide with node or pin ids.
//
// Colour pins deliberately sit in a block of their own rather than sharing the
// parameter counter. Parameter indices are patch-file keys, so slipping colour
// pins into the same sequence would renumber every slider that follows a swatch
// and silently repoint the modulation in existing patches.
struct GraphNode
{
   static const int kStride = 1000;
   static const int kParamBase = 100;
   static const int kColorBase = 900;
   static const int kOutputBase = 990; // outputs occupy 990..998

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

   // Where to place the node the first frame it appears (canvas coords).
   float spawnX = 0.0f;
   float spawnY = 0.0f;
   bool needsPosition = true;

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
