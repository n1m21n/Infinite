#include "ModelSourceNode.h"

#include "gl3.h"
#include <algorithm>
#include <cctype>
#include <cmath>

#include "AssetCache.h"
#include "GltfImport.h"
#include "Platform.h"
#include "PlyPoints.h"
#include "Skinning.h"
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

   // The same for .ply point clouds, before Normalize().
   AssetCache<PlyPoints::Cloud>& GetPointDecodeCache()
   {
      static AssetCache<PlyPoints::Cloud> cache(512ull * 1024 * 1024);
      return cache;
   }
}

ModelSourceNode::~ModelSourceNode()
{
   GLUtil::DestroyFbo(mPreview);
   GpuPointsGL::DestroyTarget(mGpuTarget);
}

namespace
{
   // Case-insensitive, no bundled extension-utility shared with main.cpp's
   // kModelExt/kGltfExt (that lives at the drop-handler layer, not the node
   // layer), so this is its own tiny check.
   bool HasLowerExtension(const std::string& path, const char* ext)
   {
      const size_t dot = path.find_last_of('.');
      if (dot == std::string::npos)
         return false;
      std::string got = path.substr(dot + 1);
      for (char& c : got) c = (char)std::tolower((unsigned char)c);
      return got == ext;
   }
}

bool ModelSourceNode::Load(const std::string& path)
{
   const bool isGltf = HasLowerExtension(path, "gltf") || HasLowerExtension(path, "glb");

   std::string note;
   // A face-less .ply is a point cloud. A .ply with faces, or one this reader
   // skips, goes on to the mesh path below as before.
   const PlyPoints::Cloud* cloud = nullptr;
   PlyPoints::Cloud readCloud;
   std::string plyError;
   if (HasLowerExtension(path, "ply") && GetModelDecodeCache().Get(path) == nullptr)
   {
      auto& pointCache = GetPointDecodeCache();
      if (const PlyPoints::Cloud* hit = pointCache.Get(path))
         cloud = hit;
      else if (PlyPoints::Read(path, readCloud, plyError) == PlyPoints::Kind::kPoints)
      {
         pointCache.Put(path, readCloud, readCloud.points.size() * sizeof(Particle));
         cloud = &readCloud;
      }
   }

   if (cloud != nullptr)
   {
      if (cloud->points.empty())
      {
         mStatus = "PLY has no points";
         return false;
      }
      mMesh = Mesh();
      mPoints = cloud->points;
      mIsPoints = true;
      if (!cloud->hasColor)
         note += " (no colour)";
   }
   else if (isGltf)
   {
      std::string error;
      const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(path, error);
      if (pkg == nullptr)
      {
         mStatus = error.empty() ? "could not load glTF model" : error;
         return false;
      }

      mMesh.vertices.clear();
      mMesh.indices = pkg->indices;
      mMesh.vertices.reserve(pkg->vertices.size());
      for (const Platform::ModelVertex& src : pkg->vertices)
      {
         Vertex v;
         v.px = src.px; v.py = src.py; v.pz = src.pz;
         v.nx = src.nx; v.ny = src.ny; v.nz = src.nz;
         v.u = src.u; v.v = src.v;
         mMesh.vertices.push_back(v);
      }

      mRig = GltfImport::GltfDecodePackage();
      if (Skinning::HasRig(*pkg))
      {
         mRig.vertices = pkg->vertices;
         mRig.skinJoints = pkg->skinJoints;
         mRig.skinWeights = pkg->skinWeights;
         mRig.joints = pkg->joints;
         mRig.clips = pkg->clips;
         mRig.morphNames = pkg->morphNames;
         mRig.morphDeltas = pkg->morphDeltas;
         mRig.morphDefaults = pkg->morphDefaults;
         // A newly picked file starts on its first clip with the file's own
         // morph weights; a patch reopening the same path keeps its values.
         if (path != mPath)
         {
            clip = mRig.clips.empty() ? 0 : 1;
            for (int i = 0; i < kMaxMorphs; i++)
               morph[i] = i < (int)mRig.morphDefaults.size() ? mRig.morphDefaults[i] : 0.0f;
            pose.Clear();
         }
         std::string rig;
         if (!mRig.joints.empty())
            rig = std::to_string(mRig.joints.size()) + " joints, " + std::to_string(mRig.clips.size()) + " clips";
         if (!mRig.morphDeltas.empty())
            rig += (rig.empty() ? "" : ", ") + std::to_string(mRig.morphDeltas.size()) +
                   (mRig.morphDeltas.size() == 1 ? " morph" : " morphs");
         note += " (" + rig + ")";
      }

      if (pkg->hadMultipleMaterials)
         note += " (first of multiple materials)";
      if (pkg->hadMultipleUVSets)
         note += " (TEXCOORD_0 only)";
   }
   else
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
            if (error.empty())
               error = plyError;
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
   }

   if (!isGltf || cloud != nullptr)
      mRig = GltfImport::GltfDecodePackage();
   if (cloud == nullptr)
   {
      mIsPoints = false;
      mPoints.clear();
   }
   // A skin draws its rest pose, not the raw bind vertices (a skeleton with
   // its own scale or turn moves them), so the fit is taken from that.
   if (!mRig.joints.empty() && mRig.vertices.size() == mMesh.vertices.size())
   {
      std::vector<Platform::ModelVertex> rest;
      Skinning::Evaluate(mRig, -1, 0.0f, {}, rest);
      for (size_t i = 0; i < rest.size(); i++)
      {
         Vertex& v = mMesh.vertices[i];
         v.px = rest[i].px; v.py = rest[i].py; v.pz = rest[i].pz;
         v.nx = rest[i].nx; v.ny = rest[i].ny; v.nz = rest[i].nz;
      }
   }
   Normalize();
   mPath = path;
   mMeshRevision = NextMeshRevision();
   mPoseKey.clear();
   mPosedTime = 0.0f;
   mAppliedPointSize = -1.0f; // ApplyPointSize writes the scales and moves the point revision

   const size_t slash = path.find_last_of('/');
   const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
   if (mIsPoints)
      mStatus = name + " - " + std::to_string(mPoints.size()) + " points" + note;
   else
      mStatus = name + " - " + std::to_string(mMesh.indices.size() / 3) + " triangles" + note;
   return true;
}

void ModelSourceNode::Normalize()
{
   if (mMesh.vertices.empty() && mPoints.empty())
      return;

   float lo[3] = { 1e30f, 1e30f, 1e30f };
   float hi[3] = { -1e30f, -1e30f, -1e30f };
   auto grow = [&](float x, float y, float z)
   {
      const float p[3] = { x, y, z };
      for (int k = 0; k < 3; k++)
      {
         // A single NaN would poison the bounds and collapse the whole model,
         // so non-finite vertices are left out of the fit.
         if (!std::isfinite(p[k]))
            continue;
         lo[k] = std::min(lo[k], p[k]);
         hi[k] = std::max(hi[k], p[k]);
      }
   };
   for (const Vertex& v : mMesh.vertices)
      grow(v.px, v.py, v.pz);
   for (const Particle& p : mPoints)
      grow(p.px, p.py, p.pz);

   const float centre[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
   const float extent = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));
   // Matched to the primitives, which all sit inside a unit box, so an imported
   // model lands at a comparable size to a Cube rather than dwarfing it.
   const float scale = (normalizeScale && extent > 1e-6f) ? (1.0f / extent) : 1.0f;
   mFitScale = scale;
   for (int k = 0; k < 3; k++)
      mFitCentre[k] = recenter ? centre[k] : 0.0f;

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
   for (Particle& p : mPoints)
   {
      if (recenter)
      {
         p.px -= centre[0];
         p.py -= centre[1];
         p.pz -= centre[2];
      }
      p.px *= scale;
      p.py *= scale;
      p.pz *= scale;
   }
}

void ModelSourceNode::ApplyPointSize()
{
   const float size = std::max(0.0f, pointSize);
   if (size == mAppliedPointSize)
      return;
   mAppliedPointSize = size;
   for (Particle& p : mPoints)
      p.scale = size;
   mPointRevision = NextMeshRevision();
}

const std::vector<Particle>* ModelSourceNode::GetPointCloud()
{
   if (bypassed || !mIsPoints)
      return nullptr;
   ApplyPointSize();
   return &mPoints;
}

unsigned long long ModelSourceNode::PointCloudRevision()
{
   if (bypassed || !mIsPoints)
      return 0;
   ApplyPointSize();
   return mPointRevision;
}

const GpuPoints* ModelSourceNode::GetGpuPoints()
{
   if (bypassed || !mIsPoints || !GpuPointsGL::Enabled())
      return nullptr;
   ApplyPointSize();
   if (mGpuUploadedRevision != mPointRevision)
   {
      mGpuUploadedRevision = mPointRevision;
      mGpuValid = GpuPointsGL::Upload(mGpuTarget, mGpuPoints, mPoints, PointBaseSize(), "Model3D_GpuPoints");
      mGpuPoints.revision = mPointRevision;
   }
   return mGpuValid ? &mGpuPoints : nullptr;
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

unsigned long long ModelSourceNode::MaterialRevision() const
{
   return ComputeContentRevision(GetMaterial(), mMaterialRevision, mLastMaterialHash);
}

unsigned int ModelSourceNode::GetSurfaceTexture()
{
   return mTextureInput.Texture();
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

   UpdatePose();
}

std::vector<std::string> ModelSourceNode::ClipNames() const
{
   std::vector<std::string> names{ "rest pose" };
   for (const GltfImport::GltfClip& c : mRig.clips)
      names.push_back(c.name);
   return names;
}

void ModelSourceNode::ApplyFit(std::vector<Vertex>& out, const std::vector<Platform::ModelVertex>& src) const
{
   out.resize(src.size());
   for (size_t i = 0; i < src.size(); i++)
   {
      Vertex& v = out[i];
      v.px = (src[i].px - mFitCentre[0]) * mFitScale;
      v.py = (src[i].py - mFitCentre[1]) * mFitScale;
      v.pz = (src[i].pz - mFitCentre[2]) * mFitScale;
      v.nx = src[i].nx; v.ny = src[i].ny; v.nz = src[i].nz;
      v.u = src[i].u; v.v = src[i].v;
   }
}

void ModelSourceNode::UpdatePose()
{
   if (!HasRig() || mIsPoints)
      return;

   const int clipIndex = std::clamp(clip, 0, (int)mRig.clips.size()) - 1;
   float time = 0.0f;
   if (clipIndex >= 0)
   {
      const float duration = mRig.clips[clipIndex].duration;
      const Transport& tr = Transport::Instance();
      const double run = clipSync == 1
         ? tr.Beats() / std::max(0.01, (double)clipBeats) * duration
         : tr.Seconds();
      time = (float)std::fmod(run * clipSpeed, duration > 0.0f ? (double)duration : 1.0) + clipPhase * duration;
   }

   std::vector<float> weights(mRig.morphDeltas.size(), 0.0f);
   for (int i = 0; i < MorphCount(); i++)
      weights[i] = morph[i];

   std::vector<float> key;
   key.reserve(2 + weights.size() + RigPose::kSlots * 4);
   key.push_back((float)clipIndex);
   key.push_back(time);
   key.insert(key.end(), weights.begin(), weights.end());
   const int jointCount = (int)mRig.joints.size();
   pose.AppendKey(key, jointCount);
   if (key == mPoseKey)
      return;
   mPoseKey = key;
   mPosedTime = time;

   std::vector<float> offsets;
   pose.ApplySlots(offsets, jointCount);
   // Target and pole come in fitted space; the solver works in the file's.
   Skinning::IkGoal goal;
   const bool ik = pose.Goal(jointCount, mFitCentre, mFitScale, goal);
   std::vector<Platform::ModelVertex> posed;
   Skinning::Evaluate(mRig, clipIndex, time, weights, posed, &mJointWorld, offsets.empty() ? nullptr : &offsets,
                      ik ? &goal : nullptr);
   ApplyFit(mMesh.vertices, posed);
   // The fit (move by -centre, then scale) goes onto the joints too.
   for (size_t j = 0; j < mJointWorld.size() / 16; j++)
   {
      float* m = &mJointWorld[j * 16];
      for (int c = 0; c < 3; c++)
         for (int r = 0; r < 3; r++)
            m[c * 4 + r] *= mFitScale;
      for (int r = 0; r < 3; r++)
         m[12 + r] = (m[12 + r] - mFitCentre[r]) * mFitScale;
   }
   mMeshRevision = NextMeshRevision();
}

bool ModelSourceNode::JointMatrix(int j, float out[16])
{
   UpdatePose();
   if (j < 0 || (size_t)(j + 1) * 16 > mJointWorld.size())
      return false;
   std::copy(&mJointWorld[j * 16], &mJointWorld[j * 16] + 16, out);
   return true;
}
