#include "ModelSourceNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>

#include "AssetCache.h"
#include "Platform.h"
#include "Transport.h"

namespace
{
   struct RawModelData
   {
      std::vector<Platform::ModelVertex> vertices;
      std::vector<unsigned int> indices;
   };

   // Shared across every ModelSourceNode respawn in the process - see
   // AssetCache.h. Caches the raw decoded vertex/index buffers, before
   // per-node Normalize() runs, so two nodes pointed at the same file (or
   // one node respawned by undo/redo) don't re-run Platform::LoadModel.
   AssetCache<RawModelData>& GetModelDecodeCache()
   {
      static AssetCache<RawModelData> cache(512ull * 1024 * 1024);
      return cache;
   }
}

ModelSourceNode::~ModelSourceNode()
{
   GLUtil::DestroyFbo(mPreview);
}

bool ModelSourceNode::Load(const std::string& path)
{
   auto& cache = GetModelDecodeCache();
   const RawModelData* cached = nullptr;
   RawModelData decoded;
   if (const RawModelData* hit = cache.Get(path))
   {
      cached = hit;
   }
   else
   {
      std::string error;
      if (!Platform::LoadModel(path, decoded.vertices, decoded.indices, error))
      {
         mStatus = error.empty() ? "could not load model" : error;
         return false;
      }
      const size_t bytes = decoded.vertices.size() * sizeof(Platform::ModelVertex) +
                            decoded.indices.size() * sizeof(unsigned int);
      cache.Put(path, decoded, bytes);
      cached = &decoded;
   }

   mMesh.vertices.clear();
   mMesh.indices = cached->indices;
   mMesh.vertices.reserve(cached->vertices.size());
   for (const Platform::ModelVertex& src : cached->vertices)
   {
      Vertex v;
      v.px = src.px; v.py = src.py; v.pz = src.pz;
      v.nx = src.nx; v.ny = src.ny; v.nz = src.nz;
      v.u = src.u; v.v = src.v;
      mMesh.vertices.push_back(v);
   }

   Normalize();
   mPath = path;
   mMeshRevision = NextMeshRevision();

   const size_t slash = path.find_last_of('/');
   const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
   mStatus = name + " - " + std::to_string(mMesh.indices.size() / 3) + " triangles";
   return true;
}

void ModelSourceNode::Normalize()
{
   if (mMesh.vertices.empty())
      return;

   float lo[3] = { 1e30f, 1e30f, 1e30f };
   float hi[3] = { -1e30f, -1e30f, -1e30f };
   for (const Vertex& v : mMesh.vertices)
   {
      const float p[3] = { v.px, v.py, v.pz };
      for (int k = 0; k < 3; k++)
      {
         // A single NaN would poison the bounds and collapse the whole model,
         // so non-finite vertices are left out of the fit.
         if (!std::isfinite(p[k]))
            continue;
         lo[k] = std::min(lo[k], p[k]);
         hi[k] = std::max(hi[k], p[k]);
      }
   }

   const float centre[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
   const float extent = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));
   // Matched to the primitives, which all sit inside a unit box, so an imported
   // model lands at a comparable size to a Cube rather than dwarfing it.
   const float scale = (normalizeScale && extent > 1e-6f) ? (1.0f / extent) : 1.0f;

   for (Vertex& v : mMesh.vertices)
   {
      if (recenter)
      {
         v.px -= centre[0];
         v.py -= centre[1];
         v.pz -= centre[2];
      }
      v.px *= scale;
      v.py *= scale;
      v.pz *= scale;
   }
}

Mat4 ModelSourceNode::GetModelMatrix() const
{
   const float spin = spinY * (float)Transport::Instance().Beats();
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY + spin), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX), m);
   m = Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
   return m;
}

Material ModelSourceNode::GetMaterial() const
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

unsigned int ModelSourceNode::GetSurfaceTexture()
{
   return mTextureInput.IsConnected() && mTextureInput.GetSource()
             ? mTextureInput.GetSource()->GetOutputTexture()
             : 0;
}

unsigned int ModelSourceNode::GetOutputTexture()
{
   return GLUtil::FboTexture(mPreview);
}

void ModelSourceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);
}
