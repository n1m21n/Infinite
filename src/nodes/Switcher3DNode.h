#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Geometry3DNodes.h"
#include "Mesh.h"

// Cycles between up to four connected IGeometrySource inputs on a fixed
// interval, forwarding whichever one is active - the geometry-side
// counterpart to SwitcherNode, which does the same thing for images by
// rendering both candidates to an FBO and mixing. There is no FBO or shader
// here: switching which geometry is "active" just means picking a different
// upstream pointer to forward, not compositing pixels, so this node performs
// no rendering of its own.
//
// crossfade is kept as a saved/visited param, for patch-file and UI parity
// with SwitcherNode, but is NOT implemented in v1: interpolating between two
// arbitrary meshes' topology isn't generally well-defined (different vertex
// counts, different connectivity), and IGeometrySource's contract is a
// single mesh/material per source, so rendering both candidates at once with
// a blended opacity would mean this node no longer forwarding one upstream
// mesh but synthesizing a two-source composite - a materially bigger feature
// than "switch which input is active". v1 is a hard cut only; the field is
// reserved for a future version that adds real double-render blending.
class Switcher3DNode : public INode, public IGeometrySource
{
public:
   static const int kSlots = 4;

   static INode* Create() { return new Switcher3DNode(); }
   static const std::vector<std::string>& UnitNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned int GetMaterialTexture(int map) override;
   unsigned long long SurfaceTextureRevision() const override;
   MappingTransform GetMappingTransform() const override;
   // The active input is where the mesh actually comes from, not something
   // this node builds - same reasoning as GeometryOpNode's passthrough, so a
   // chain like InstanceOnPoints -> Switcher 3D -> Render 3D still draws
   // every instance instead of collapsing to a single un-instanced copy.
   IGeometrySource* PassthroughSource() const override;

   INode* BypassSource() override
   {
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            return dynamic_cast<INode*>(inputs[i]);
      return nullptr;
   }

   IGeometrySource* inputs[kSlots] = { nullptr, nullptr, nullptr, nullptr };
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &inputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo A", "geo B", "geo C", "geo D" };
      return (slot >= 0 && slot < kSlots) ? kNames[slot] : nullptr;
   }

   // Which slot is currently forwarded, live - computed from the transport
   // clock (or manualSlot), not cached, so it's always accurate even if
   // queried before this node's own CookIfNeeded runs this frame.
   int ActiveSlot() const;

   int unit = 0;             // 0 = beats, 1 = seconds
   float interval = 4.0f;    // switch every N beats/seconds
   // Reserved - see class comment. Not applied to the output in v1.
   float crossfade = 0.0f;
   bool manual = false;      // ignore the clock and hold manualSlot
   int manualSlot = 0;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("unit", unit); v.Float("interval", interval);
      v.Float("crossfade", crossfade);
      v.Bool("manual", manual); v.Int("manualSlot", manualSlot);
   }

private:
   // Read-only: which slot the clock/manual settings currently select, from
   // only the connected inputs (an unconnected slot is never picked). -1
   // when nothing is connected at all.
   int ComputeActiveSlot() const;
   IGeometrySource* Active() const;

   unsigned long long mMeshRevision = 0;
   IGeometrySource* mLastActive = nullptr;
   unsigned long long mLastUpstreamRevision = 0;
   bool mHasLastUpstreamRevision = false;
   Mesh mEmpty;
   int mLastCookFrame = -1;
};
