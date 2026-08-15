#include "CurveNode.h"

#include <algorithm>
#include <cmath>

constexpr int CurveNode::kMaxPoints;

namespace
{
   const std::vector<std::string> kKindNames = {
      "Catmull-Rom", "Bezier", "B-Spline", "Linear"
   };
   const std::vector<std::string> kPresetNames = {
      "Arc", "Spiral", "Wave", "Scatter", "Circle"
   };

   float Hash(float seed, int index)
   {
      const float x = std::sin((seed + 1.0f) * (float)(index + 1) * 12.9898f) * 43758.5453f;
      return (x - std::floor(x)) * 2.0f - 1.0f;
   }
}

const std::vector<std::string>& CurveNode::KindNames() { return kKindNames; }
const std::vector<std::string>& CurveNode::PresetNames() { return kPresetNames; }

void CurveNode::RebuildIfNeeded()
{
   if (mBuiltKind == kind && mBuiltPreset == preset && mBuiltCount == pointCount &&
       mBuiltSegments == segments && mBuiltClosed == closed && mBuiltSpread == spread &&
       mBuiltHeight == height && mBuiltTwist == twist && mBuiltSeed == seed &&
       mBuiltRadius == radius && mBuiltSides == sides && mBuiltTaper == taper &&
       !mLine.Empty())
      return;

   const int count = std::max(2, std::min(pointCount, kMaxPoints));
   std::vector<float> control;
   control.reserve((size_t)count * 3);

   const float twistRad = twist * 3.14159265f / 180.0f;
   for (int i = 0; i < count; i++)
   {
      const float t = (count > 1) ? (float)i / (float)(count - 1) : 0.0f;
      const float angle = t * 6.28318530718f + twistRad;
      float x = 0, y = 0, z = 0;
      switch (preset)
      {
         case 1: // spiral - radius grows along the curve
            x = std::cos(angle * 2.0f) * spread * t;
            y = (t - 0.5f) * height * 2.0f;
            z = std::sin(angle * 2.0f) * spread * t;
            break;
         case 2: // wave
            x = (t - 0.5f) * spread * 2.0f;
            y = std::sin(angle) * height;
            z = std::cos(angle * 0.5f) * spread * 0.3f;
            break;
         case 3: // scatter - deterministic from the seed, so it is reproducible
            x = Hash(seed, i * 3 + 0) * spread;
            y = Hash(seed, i * 3 + 1) * height;
            z = Hash(seed, i * 3 + 2) * spread;
            break;
         case 4: // circle
            x = std::cos(t * 6.28318530718f + twistRad) * spread;
            y = 0.0f;
            z = std::sin(t * 6.28318530718f + twistRad) * spread;
            break;
         case 0:
         default: // arc
            x = (t - 0.5f) * spread * 2.0f;
            y = std::sin(t * 3.14159265f) * height;
            z = 0.0f;
            break;
      }
      control.push_back(x);
      control.push_back(y);
      control.push_back(z);
   }

   mLine = MeshOps::BuildCurve(control, kind, segments, closed);
   mMesh = MeshOps::TubeAlong(mLine, radius, sides, taper);

   mBuiltKind = kind; mBuiltPreset = preset; mBuiltCount = pointCount;
   mBuiltSegments = segments; mBuiltClosed = closed; mBuiltSpread = spread;
   mBuiltHeight = height; mBuiltTwist = twist; mBuiltSeed = seed;
   mBuiltRadius = radius; mBuiltSides = sides; mBuiltTaper = taper;
   mRevision = NextMeshRevision();
}

const Mesh& CurveNode::GetMesh()
{
   RebuildIfNeeded();
   return mMesh;
}

unsigned long long CurveNode::MeshRevision()
{
   RebuildIfNeeded();
   return mRevision;
}

const Polyline& CurveNode::GetPolyline()
{
   RebuildIfNeeded();
   return mLine;
}

unsigned long long CurveNode::CurveRevision()
{
   RebuildIfNeeded();
   return mRevision;
}

Mat4 CurveNode::GetModelMatrix() const
{
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material CurveNode::GetMaterial() const
{
   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.shading = shading;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   m.ior = ior;
   m.transmission = transmission;
   m.transmissionRoughness = transmissionRoughness;
   m.specular = specular;
   m.clearcoat = clearcoat;
   m.clearcoatRoughness = clearcoatRoughness;
   m.subsurface = subsurface;
   m.subsurfaceColor[0] = subsurfaceColor[0];
   m.subsurfaceColor[1] = subsurfaceColor[1];
   m.subsurfaceColor[2] = subsurfaceColor[2];
   m.subsurfaceRadius = subsurfaceRadius;
   return m;
}

void CurveNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   RebuildIfNeeded();
}
