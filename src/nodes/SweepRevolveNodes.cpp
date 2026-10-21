#include "SweepRevolveNodes.h"

#include <algorithm>
#include <cmath>

namespace
{
   const Mesh kEmptyMesh;
}

void CurveSurfaceNode::RebuildIfNeeded()
{
   IGeometrySource* in[2] = { nullptr, nullptr };
   const int count = Inputs(in);

   unsigned long long stamp[2] = { 0, 0 };
   std::string warning;
   for (int i = 0; i < count; i++)
   {
      if (in[i] == nullptr)
         continue;
      const std::string w = DescribeGeometryMismatch(in[i], GeometryRequirement::kCurve);
      if (!w.empty() && warning.empty())
         warning = w;
      if (in[i]->GetCurve() != nullptr)
         stamp[i] = in[i]->CurveStamp();
   }
   mCookWarning = warning;

   double key[kMaxKey] = {};
   const int keyCount = Key(key);
   bool same = mBuilt && mBuiltInputs == count && mBuiltKeyCount == keyCount;
   for (int i = 0; i < count && same; i++)
      same = mBuiltInput[i] == in[i] && mBuiltStamp[i] == stamp[i];
   for (int i = 0; i < keyCount && same; i++)
      same = mBuiltKey[i] == key[i];
   if (same)
      return;

   mMesh = Build();

   mBuilt = true;
   mBuiltInputs = count;
   for (int i = 0; i < count; i++)
   {
      mBuiltInput[i] = in[i];
      mBuiltStamp[i] = stamp[i];
   }
   mBuiltKeyCount = keyCount;
   for (int i = 0; i < keyCount; i++)
      mBuiltKey[i] = key[i];
   mRevision = NextMeshRevision();
}

const Mesh& CurveSurfaceNode::GetMesh()
{
   if (bypassed)
      return Primary() ? Primary()->GetMesh() : kEmptyMesh;
   RebuildIfNeeded();
   return mMesh;
}

unsigned long long CurveSurfaceNode::MeshRevision()
{
   if (bypassed)
      return Primary() ? Primary()->MeshRevision() : 0;
   RebuildIfNeeded();
   return mRevision;
}

unsigned long long CurveSurfaceNode::MaterialRevision() const
{
   return ComputeContentRevision(GetMaterial(), mMaterialRevision, mLastMaterialHash);
}

void CurveSurfaceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   IGeometrySource* in[2] = { nullptr, nullptr };
   const int count = Inputs(in);
   for (int i = 0; i < count; i++)
      if (auto* upstream = dynamic_cast<INode*>(in[i]))
         upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

Mesh RevolveNode::Build()
{
   const Polyline* line = input ? input->GetCurve() : nullptr;
   if (line == nullptr)
      return Mesh();
   return MeshOps::Revolve(*line, axis, angle, segments, caps);
}

Mesh SweepNode::Build()
{
   const Polyline* way = path ? path->GetCurve() : nullptr;
   if (way == nullptr)
      return Mesh();

   const Polyline* shape = profile ? profile->GetCurve() : nullptr;
   Polyline round;
   if (shape == nullptr)
   {
      // No profile patched: a circle, so a path alone gives a tube.
      const int sides = std::max(3, std::min(profileSides, 64));
      for (int i = 0; i < sides; i++)
      {
         const float a = 6.28318530718f * (float)i / (float)sides;
         round.points.push_back(profileRadius * std::cos(a));
         round.points.push_back(profileRadius * std::sin(a));
         round.points.push_back(0.0f);
      }
      round.closed = true;
      shape = &round;
   }
   return MeshOps::Sweep(*shape, *way, startScale, endScale, twist, caps);
}
