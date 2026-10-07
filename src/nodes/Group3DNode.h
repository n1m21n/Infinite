#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Geometry3DNodes.h"
#include "Mesh.h"

// Carries several geometry sources through one pin into Render 3D, so a scene
// is not capped at Render 3D's four geometry inputs. Unlike Join Geometry it
// does not merge anything: every child keeps its own mesh, material, texture,
// mapping and transform, and Render 3D draws each one as if it had its own
// pin. Groups can be patched into groups.
//
// To anything other than Render 3D (and another Group 3D) it is an empty
// source - there is no single mesh to hand a Transform or Material, which is
// exactly why Join Geometry exists beside it.
class Group3DNode : public INode, public IGeometrySource
{
public:
   static const int kSlots = 8;

   static INode* Create() { return new Group3DNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int /*frameId*/) override {}

   const Mesh& GetMesh() override { return mEmpty; }
   unsigned long long MeshRevision() override { return 0; }
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override { return Material(); }

   // The connected inputs, in pin order.
   int GroupChildCount() const override
   {
      int count = 0;
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            count++;
      return count;
   }
   IGeometrySource* GroupChild(int index) const override
   {
      for (int i = 0; i < kSlots; i++)
      {
         if (inputs[i] == nullptr)
            continue;
         if (index-- == 0)
            return inputs[i];
      }
      return nullptr;
   }

   IGeometrySource* inputs[kSlots] = {};
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &inputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo A", "geo B", "geo C", "geo D",
                                      "geo E", "geo F", "geo G", "geo H" };
      return (slot >= 0 && slot < kSlots) ? kNames[slot] : nullptr;
   }

   void VisitParams(ParamVisitor& /*v*/) override {}

private:
   Mesh mEmpty;
};
