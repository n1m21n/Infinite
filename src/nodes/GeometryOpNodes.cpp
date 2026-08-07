#include "GeometryOpNodes.h"

#include <algorithm>
#include <cmath>

namespace
{
   const std::vector<std::string> kOpNames = {
      "Transform", "Array", "Subdivide", "Solidify", "Extrude",
      "Wireframe", "Triangulate", "Normals", "Explode", "Twist",
      "Smooth", "Mirror", "Screw",
      "Select", "Delete Selected", "Transform Selected", "Extrude Selected"
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
   s.selectMode = selectMode;
   s.selectA = selectA; s.selectB = selectB; s.selectC = selectC;
   s.selectSeed = selectSeed; s.normalAmount = normalAmount;
   s.selectInvert = selectInvert; s.selectAppend = selectAppend;
   s.keepSelected = keepSelected; s.moveAlongNormals = moveAlongNormals;
   s.iter = iterations;
   s.screwSteps = screwSteps;
   s.mirrorOffset = mirrorOffset;
   s.turns = turns;
   s.rise = rise;
   s.radiusOffset = radiusOffset;
   s.weldSeam = weldSeam;
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
   if (bypassed)
      return input->GetMesh();

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
      case kSmooth:
         mCache = MeshOps::Smooth(src, iterations, amount);
         break;
      case kMirror:
         mCache = MeshOps::Mirror(src, axis, mirrorOffset, weldSeam, keepOriginal);
         break;
      case kScrew:
         mCache = MeshOps::Screw(src, screwSteps, turns, rise, radiusOffset, axis);
         break;
      case kSelect:
         mCache = MeshOps::Select(src, selectMode, selectA, selectB, selectC, axis,
                                  selectSeed, selectInvert, selectAppend);
         break;
      case kDeleteSelected:
         mCache = MeshOps::DeleteSelected(src, keepSelected);
         break;
      case kTransformSelected:
      {
         Mat4 m = Mat4::Scale(scaleStep, scaleStep, scaleStep);
         m = Mat4::Multiply(Mat4::RotationY(rotStep), m);
         m = Mat4::Multiply(Mat4::Translation(offsetX, offsetY, offsetZ), m);
         mCache = MeshOps::TransformSelected(src, m, moveAlongNormals, normalAmount);
         break;
      }
      case kExtrudeSelected:
         mCache = MeshOps::ExtrudeSelected(src, thickness, inset);
         break;
      default:
         mCache = MeshOps::Twist(src, amount * 3.0f, axis);
         break;
   }

   mBuilt = sig;
   mHasBuilt = true;
   mMeshRevision = NextMeshRevision();
   return mCache;
}

unsigned long long GeometryOpNode::MeshRevision()
{
   // Both early-outs in GetMesh() hand back somebody else's mesh, so they have
   // to hand back that mesh's stamp too rather than this node's.
   if (input == nullptr)
      return 0;
   if (bypassed)
      return input->MeshRevision();
   GetMesh();
   return mMeshRevision;
}

Material GeometryOpNode::GetMaterial() const
{
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();

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
   return m;
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

unsigned long long InstanceOnPointsNode::MeshRevision()
{
   // The uploaded mesh is the shape's, so the stamp has to be the shape's too.
   return instanceShape ? instanceShape->MeshRevision() : 0;
}

size_t InstanceOnPointsNode::TriangleCount() const
{
   if (instanceShape == nullptr)
      return 0;
   return (const_cast<IGeometrySource*>(instanceShape)->GetMesh().indices.size() / 3) * mTransforms.size();
}

Material InstanceOnPointsNode::GetMaterial() const
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
   return m;
}

unsigned int InstanceOnPointsNode::GetSurfaceTexture()
{
   return instanceShape ? instanceShape->GetSurfaceTexture() : 0;
}

void InstanceOnPointsNode::Rebuild()
{
   mTransforms.clear();
   mColors.clear();

   // A patched point cloud replaces mesh sampling: the positions already exist,
   // so there is nothing to sample.
   if (cloudSource != nullptr)
   {
      const std::vector<Particle>& cloud = cloudSource->GetPoints();
      mTransforms.reserve(cloud.size());
      mColors.reserve(cloud.size() * 3);
      for (const Particle& p : cloud)
      {
         if (!p.alive)
            continue;
         const float s = instanceScale * p.scale;
         Mat4 m = Mat4::Scale(s, s, s);
         m = Mat4::Multiply(Mat4::Translation(p.px, p.py, p.pz), m);
         mTransforms.push_back(m);
         mColors.push_back(p.r);
         mColors.push_back(p.g);
         mColors.push_back(p.b);
      }
      return;
   }

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
   if (auto* c = dynamic_cast<INode*>(cloudSource))
      c->CookIfNeeded(frameId);

   // A simulated cloud changes every frame while it is running, so its own
   // revision stamp is what decides dirtiness rather than any parameter here.
   const unsigned long long cloudRevision = cloudSource ? cloudSource->PointRevision() : 0;
   const size_t pointTris = pointSource ? pointSource->GetMesh().indices.size() : 0;
   const bool dirty =
      mBuiltPointSource != pointSource || mBuiltShape != instanceShape ||
      mBuiltCloud != (const void*)cloudSource || mBuiltCloudRevision != cloudRevision ||
      mBuiltPointTris != pointTris || mBuiltMode != pointMode || mBuiltMax != maxPoints ||
      mBuiltScale != instanceScale || mBuiltScaleRand != scaleRandom ||
      mBuiltRotRand != rotationRandom || mBuiltSeed != seed ||
      mBuiltOffset != normalOffset || mBuiltAlign != alignToNormal;

   if (!dirty)
      return;

   Rebuild();
   mInstanceRevision = NextMeshRevision();

   mBuiltPointSource = pointSource;
   mBuiltShape = instanceShape;
   mBuiltCloud = cloudSource;
   mBuiltCloudRevision = cloudRevision;
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
