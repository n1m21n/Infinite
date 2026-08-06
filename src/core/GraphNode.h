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
//    +999          the image output pin
// Link ids live in their own range and are tracked separately, so they can never
// collide with node or pin ids.
struct GraphNode
{
   static const int kStride = 1000;
   static const int kParamBase = 100;
   static const int kOutputBase = 990; // outputs occupy 990..998

   std::unique_ptr<INode> node;
   std::string typeName;
   std::string category;
   int index = 0;
   bool showParams = false; // params start collapsed so the preview leads
   bool hasModulatedParams = false; // recomputed each frame, drives the collapsed "mod" tag

   // Where to place the node the first frame it appears (canvas coords).
   float spawnX = 0.0f;
   float spawnY = 0.0f;
   bool needsPosition = true;

   int NodeId() const { return index * kStride; }
   int InputPinId(int slot) const { return index * kStride + 1 + slot; }
   int ParamPinId(int paramIndex) const { return index * kStride + kParamBase + paramIndex; }
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
      return off >= kParamBase && off < kOutputBase;
   }
   static bool IsInputPin(int pinId)
   {
      int off = OffsetFromPin(pinId);
      return off >= 1 && off < kParamBase;
   }
   static int InputSlotFromPin(int pinId) { return OffsetFromPin(pinId) - 1; }
   static int ParamIndexFromPin(int pinId) { return OffsetFromPin(pinId) - kParamBase; }
};
