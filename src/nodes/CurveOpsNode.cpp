#include "CurveOpsNode.h"

#include "CurveOps.h"

namespace
{
   const Mesh kEmptyMesh;
}

const std::vector<std::string>& CurveOpsNode::ModeNames()
{
   static const std::vector<std::string> names = { "Resample", "Simplify", "Smooth", "Offset" };
   return names;
}

void CurveOpsNode::RebuildIfNeeded()
{
   const Polyline* src = input ? input->GetCurve() : nullptr;
   const unsigned long long upstream = src ? input->CurveStamp() : 0;
   if (mBuilt && mBuiltInput == input && mBuiltUpstream == upstream && mBuiltMode == mode &&
       mBuiltCount == count && mBuiltIterations == iterations && mBuiltArc == arcSteps &&
       mBuiltSides == sides && mBuiltTolerance == tolerance && mBuiltStrength == strength &&
       mBuiltDistance == distance && mBuiltRadius == radius)
      return;

   mLine = Polyline();
   mMesh = Mesh();
   if (src != nullptr && !src->Empty())
   {
      switch (mode)
      {
         case kSimplify: mLine = CurveOps::Simplify(*src, tolerance); break;
         case kSmooth:   mLine = CurveOps::Smooth(*src, iterations, strength); break;
         case kOffset:   mLine = CurveOps::Offset(*src, distance, arcSteps); break;
         default:        mLine = CurveOps::Resample(*src, count); break;
      }
      if (!mLine.Empty())
         mMesh = MeshOps::TubeAlong(mLine, radius, std::max(3, sides), 0.0f);
   }

   mBuilt = true;
   mBuiltInput = input; mBuiltUpstream = upstream; mBuiltMode = mode;
   mBuiltCount = count; mBuiltIterations = iterations; mBuiltArc = arcSteps;
   mBuiltSides = sides; mBuiltTolerance = tolerance; mBuiltStrength = strength;
   mBuiltDistance = distance; mBuiltRadius = radius;
   mRevision = NextMeshRevision();
}

const Mesh& CurveOpsNode::GetMesh()
{
   if (bypassed)
      return input ? input->GetMesh() : kEmptyMesh;
   RebuildIfNeeded();
   return mMesh;
}

unsigned long long CurveOpsNode::MeshRevision()
{
   if (bypassed)
      return input ? input->MeshRevision() : 0;
   RebuildIfNeeded();
   return mRevision;
}

const Polyline* CurveOpsNode::GetCurve()
{
   if (input == nullptr)
      return nullptr;
   if (bypassed)
      return input->GetCurve();
   RebuildIfNeeded();
   return mLine.Empty() ? nullptr : &mLine;
}

unsigned long long CurveOpsNode::CurveStamp()
{
   if (input == nullptr)
      return 0;
   if (bypassed)
      return input->CurveStamp();
   RebuildIfNeeded();
   return mRevision;
}

unsigned long long CurveOpsNode::MaterialRevision() const
{
   return ComputeContentRevision(GetMaterial(), mMaterialRevision, mLastMaterialHash);
}

void CurveOpsNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}
