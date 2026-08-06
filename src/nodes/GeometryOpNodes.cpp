#include "GeometryOpNodes.h"

#include <algorithm>
#include <cmath>

namespace
{
   const std::vector<std::string> kOpNames = {
      "Transform", "Array", "Subdivide", "Solidify", "Extrude",
      "Wireframe", "Triangulate", "Normals", "Explode", "Twist"
   };
   const std::vector<std::string> kSourceNames = { "Vertices", "Edges", "Faces" };
   const Mesh kEmptyMesh;

   float Rand01(float seed, int index)
   {
      const float x = std::sin((seed + 1.0f) * (float)(index + 1) * 12.9898f) * 43758.5453f;
      return x - std::floor(x);
   }
}

const std::vector<std::string>& GeometryOpNode::OpNames() { return kOpNames; }

GeometryOpNode::Signature GeometryOpNode::CurrentSignature() const
{
   Signature s;
   s.op = op;
   s.count = count;
   s.levels = levels;
   s.axis = axis;
   s.a = amount;
   s.ox = offsetX; s.oy = offsetY; s.oz = offsetZ;
   s.rs = rotStep; s.ss = scaleStep; s.rad = radius;
   s.sm = smooth; s.th = thickness; s.ins = inset; s.sd = seed;
   s.radial = radial; s.keep = keepOriginal; s.flat = flatShade; s.flip = flipNormals;
   s.upstream = input;
   // Upstream triangle count stands in for "the mesh changed": cheap, and it
   // catches a primitive being switched or resubdivided upstream.
   s.upstreamTris = input ? input->GetMesh().indices.size() : 0;
   return s;
}

const Mesh& GeometryOpNode::GetMesh()
{
   if (input == nullptr)
      return kEmptyMesh;

   const Signature sig = CurrentSignature();
   if (mHasBuilt && sig == mBuilt)
      return mCache;

   const Mesh& src = input->GetMesh();
   switch (op)
   {
      case kTransform:
      {
         Mat4 m = Mat4::Scale(scaleStep, scaleStep, scaleStep);
         m = Mat4::Multiply(Mat4::RotationY(rotStep), m);
         m = Mat4::Multiply(Mat4::Translation(offsetX, offsetY, offsetZ), m);
         mCache = MeshOps::Transform(src, m);
         break;
      }
      case kArray:
         mCache = MeshOps::Array(src, count, offsetX, offsetY, offsetZ,
                                 rotStep, scaleStep, radial, radius);
         break;
      case kSubdivide:
         mCache = MeshOps::Subdivide(src, levels, smooth);
         break;
      case kSolidify:
         mCache = MeshOps::Solidify(src, thickness, keepOriginal);
         break;
      case kExtrude:
         mCache = MeshOps::Extrude(src, thickness, inset);
         break;
      case kWireframe:
         mCache = MeshOps::Wireframe(src, thickness);
         break;
      case kTriangulate:
         mCache = MeshOps::Triangulate(src, amount * 0.05f);
         break;
      case kNormals:
         mCache = MeshOps::RecalculateNormals(src, flatShade, flipNormals);
         break;
      case kExplode:
         mCache = MeshOps::Explode(src, amount * 0.3f, seed);
         break;
      default:
         mCache = MeshOps::Twist(src, amount * 3.0f, axis);
         break;
   }

   mBuilt = sig;
   mHasBuilt = true;
   return mCache;
}

void GeometryOpNode::GetMaterial(float outColor[3], float& outMetallic, float& outRoughness,
                                 float& outOpacity, int& outShading) const
{
   if (inheritMaterial && input != nullptr)
   {
      input->GetMaterial(outColor, outMetallic, outRoughness, outOpacity, outShading);
      return;
   }
   outColor[0] = color[0]; outColor[1] = color[1]; outColor[2] = color[2];
   outMetallic = metallic;
   outRoughness = roughness;
   outOpacity = opacity;
   outShading = shading;
}

unsigned int GeometryOpNode::GetSurfaceTexture()
{
   return input ? input->GetSurfaceTexture() : 0;
}

void GeometryOpNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
}

// ===================================================== Instance on Points

const std::vector<std::string>& InstanceOnPointsNode::SourceNames() { return kSourceNames; }

const Mesh& InstanceOnPointsNode::GetMesh()
{
   // The shape drawn per instance; the transforms come from InstanceTransforms.
   return instanceShape ? instanceShape->GetMesh() : mEmpty;
}

size_t InstanceOnPointsNode::TriangleCount() const
{
   if (instanceShape == nullptr)
      return 0;
   return (const_cast<IGeometrySource*>(instanceShape)->GetMesh().indices.size() / 3) * mTransforms.size();
}

void InstanceOnPointsNode::GetMaterial(float outColor[3], float& outMetallic, float& outRoughness,
                                       float& outOpacity, int& outShading) const
{
   outColor[0] = color[0]; outColor[1] = color[1]; outColor[2] = color[2];
   outMetallic = metallic;
   outRoughness = roughness;
   outOpacity = opacity;
   outShading = shading;
}

unsigned int InstanceOnPointsNode::GetSurfaceTexture()
{
   return instanceShape ? instanceShape->GetSurfaceTexture() : 0;
}

void InstanceOnPointsNode::Rebuild()
{
   mTransforms.clear();
   if (pointSource == nullptr)
      return;

   const Mesh& src = pointSource->GetMesh();
   if (src.Empty())
      return;

   const std::vector<MeshPoint> points = MeshOps::ToPoints(src, pointMode, maxPoints);
   mTransforms.reserve(points.size());

   for (size_t i = 0; i < points.size(); i++)
   {
      const MeshPoint& p = points[i];
      const float r0 = Rand01(seed, (int)i * 3 + 0);
      const float r1 = Rand01(seed, (int)i * 3 + 1);
      const float r2 = Rand01(seed, (int)i * 3 + 2);

      const float s = instanceScale * (1.0f + (r0 - 0.5f) * 2.0f * scaleRandom);
      Mat4 m = Mat4::Scale(s, s, s);

      if (rotationRandom > 0.0f)
      {
         m = Mat4::Multiply(Mat4::RotationY(r1 * 6.28318530f * rotationRandom), m);
         m = Mat4::Multiply(Mat4::RotationX(r2 * 6.28318530f * rotationRandom), m);
      }

      if (alignToNormal)
      {
         // Build a basis whose Y axis is the point normal, so instances stand
         // up off the surface instead of all facing the same way.
         float n[3] = { p.nx, p.ny, p.nz };
         const float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
         if (len > 1e-6f) { n[0] /= len; n[1] /= len; n[2] /= len; }
         float up[3] = { 0, 1, 0 };
         if (std::fabs(n[1]) > 0.99f) { up[0] = 1; up[1] = 0; }
         float t[3] = { up[1]*n[2] - up[2]*n[1], up[2]*n[0] - up[0]*n[2], up[0]*n[1] - up[1]*n[0] };
         const float tl = std::sqrt(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
         if (tl > 1e-6f) { t[0] /= tl; t[1] /= tl; t[2] /= tl; }
         const float b[3] = { n[1]*t[2] - n[2]*t[1], n[2]*t[0] - n[0]*t[2], n[0]*t[1] - n[1]*t[0] };

         Mat4 basis;
         basis.m[0] = t[0]; basis.m[1] = t[1]; basis.m[2] = t[2];
         basis.m[4] = n[0]; basis.m[5] = n[1]; basis.m[6] = n[2];
         basis.m[8] = b[0]; basis.m[9] = b[1]; basis.m[10] = b[2];
         m = Mat4::Multiply(basis, m);
      }

      m = Mat4::Multiply(Mat4::Translation(p.px + p.nx * normalOffset,
                                           p.py + p.ny * normalOffset,
                                           p.pz + p.nz * normalOffset), m);
      mTransforms.push_back(m);
   }
}

void InstanceOnPointsNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (auto* p = dynamic_cast<INode*>(pointSource))
      p->CookIfNeeded(frameId);
   if (auto* s = dynamic_cast<INode*>(instanceShape))
      s->CookIfNeeded(frameId);

   const size_t pointTris = pointSource ? pointSource->GetMesh().indices.size() : 0;
   const bool dirty =
      mBuiltPointSource != pointSource || mBuiltShape != instanceShape ||
      mBuiltPointTris != pointTris || mBuiltMode != pointMode || mBuiltMax != maxPoints ||
      mBuiltScale != instanceScale || mBuiltScaleRand != scaleRandom ||
      mBuiltRotRand != rotationRandom || mBuiltSeed != seed ||
      mBuiltOffset != normalOffset || mBuiltAlign != alignToNormal;

   if (!dirty)
      return;

   Rebuild();

   mBuiltPointSource = pointSource;
   mBuiltShape = instanceShape;
   mBuiltPointTris = pointTris;
   mBuiltMode = pointMode;
   mBuiltMax = maxPoints;
   mBuiltScale = instanceScale;
   mBuiltScaleRand = scaleRandom;
   mBuiltRotRand = rotationRandom;
   mBuiltSeed = seed;
   mBuiltOffset = normalOffset;
   mBuiltAlign = alignToNormal;
}
