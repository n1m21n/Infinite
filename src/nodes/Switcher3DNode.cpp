#include "Switcher3DNode.h"

#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kUnitNames = { "beats", "seconds" };
}

const std::vector<std::string>& Switcher3DNode::UnitNames()
{
   return kUnitNames;
}

int Switcher3DNode::ComputeActiveSlot() const
{
   int connected[kSlots];
   int count = 0;
   for (int i = 0; i < kSlots; i++)
   {
      if (inputs[i] != nullptr)
         connected[count++] = i;
   }
   if (count == 0)
      return -1;

   if (manual || count == 1)
   {
      const int pick = manual ? std::max(0, std::min(manualSlot, kSlots - 1)) : connected[0];
      return inputs[pick] != nullptr ? pick : connected[0];
   }

   const double clock = (unit == 0) ? Transport::Instance().Beats()
                                    : Transport::Instance().Seconds();
   const double step = std::max(0.01f, interval);
   const double pos = clock / step;
   const long long index = (long long)std::floor(pos);
   return connected[(int)(((index % count) + count) % count)];
}

IGeometrySource* Switcher3DNode::Active() const
{
   const int slot = ComputeActiveSlot();
   return (slot >= 0 && slot < kSlots) ? inputs[slot] : nullptr;
}

int Switcher3DNode::ActiveSlot() const
{
   const int slot = ComputeActiveSlot();
   return slot >= 0 ? slot : 0;
}

IGeometrySource* Switcher3DNode::PassthroughSource() const
{
   return Active();
}

const SplatIO::SplatCloud* Switcher3DNode::GetSplatCloud()
{
   IGeometrySource* active = Active();
   return active ? active->GetSplatCloud() : nullptr;
}

unsigned long long Switcher3DNode::SplatCloudRevision()
{
   IGeometrySource* active = Active();
   return active ? active->SplatCloudRevision() : 0;
}

float Switcher3DNode::SplatSizeMultiplier() const
{
   IGeometrySource* active = Active();
   return active ? active->SplatSizeMultiplier() : 1.0f;
}

float Switcher3DNode::SplatOpacityMultiplier() const
{
   IGeometrySource* active = Active();
   return active ? active->SplatOpacityMultiplier() : 1.0f;
}

void Switcher3DNode::GetSplatTint(float outRgb[3]) const
{
   IGeometrySource* active = Active();
   if (active) active->GetSplatTint(outRgb);
   else outRgb[0] = outRgb[1] = outRgb[2] = 1.0f;
}

Mat4 Switcher3DNode::GetInstanceGroupMatrix() const
{
   IGeometrySource* active = Active();
   return active ? active->GetInstanceGroupMatrix() : Mat4::Identity();
}

const std::vector<unsigned char>* Switcher3DNode::InstanceSelection() const
{
   IGeometrySource* active = Active();
   return active ? active->InstanceSelection() : nullptr;
}

unsigned long long Switcher3DNode::InstanceSelectionRevision() const
{
   IGeometrySource* active = Active();
   return active ? active->InstanceSelectionRevision() : 0;
}

const std::vector<Mat4>* Switcher3DNode::InstanceTransformOverride() const
{
   IGeometrySource* active = Active();
   return active ? active->InstanceTransformOverride() : nullptr;
}

Mat4 Switcher3DNode::GetModelMatrix() const
{
   IGeometrySource* active = Active();
   return active ? active->GetModelMatrix() : Mat4::Identity();
}

Material Switcher3DNode::GetMaterial() const
{
   IGeometrySource* active = Active();
   return active ? active->GetMaterial() : Material();
}

MappingTransform Switcher3DNode::GetMappingTransform() const
{
   IGeometrySource* active = Active();
   return active ? active->GetMappingTransform() : MappingTransform();
}

unsigned int Switcher3DNode::GetSurfaceTexture()
{
   IGeometrySource* active = Active();
   return active ? active->GetSurfaceTexture() : 0;
}

unsigned int Switcher3DNode::GetMaterialTexture(int map)
{
   IGeometrySource* active = Active();
   return active ? active->GetMaterialTexture(map) : 0;
}

unsigned long long Switcher3DNode::SurfaceTextureRevision() const
{
   IGeometrySource* active = Active();
   return active ? active->SurfaceTextureRevision() : 0;
}

const Mesh& Switcher3DNode::GetMesh()
{
   IGeometrySource* active = Active();
   if (active == nullptr)
   {
      mLastActive = nullptr;
      mHasLastUpstreamRevision = false;
      return mEmpty;
   }

   // Bump our own revision only when the active *pointer* changed (a switch
   // happened) or the active source's own mesh actually changed - never on a
   // frame where the same slot stays active and nothing upstream moved. A
   // node that bumped on every call here would defeat every downstream
   // cache (Render3D's SceneSignature, GeometryOpNode's Signature, ...).
   const unsigned long long upstreamRev = active->MeshRevision();
   if (!mHasLastUpstreamRevision || active != mLastActive || upstreamRev != mLastUpstreamRevision)
   {
      mMeshRevision++;
      mLastActive = active;
      mLastUpstreamRevision = upstreamRev;
      mHasLastUpstreamRevision = true;
   }
   return active->GetMesh();
}

unsigned long long Switcher3DNode::MeshRevision()
{
   GetMesh(); // ensures the bookkeeping above ran for this call
   return mMeshRevision;
}

void Switcher3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   // Every connected input is cooked, not just the active one: an unseen
   // slot's own simulation (Cloth, Ocean, ...) should keep advancing while
   // switched away from, and CookIfNeeded is idempotent per frame anyway.
   for (int i = 0; i < kSlots; i++)
   {
      if (auto* upstream = dynamic_cast<INode*>(inputs[i]))
         upstream->CookIfNeeded(frameId);
   }
}
