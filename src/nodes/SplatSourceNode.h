#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "SplatIO.h"

// Loads a Gaussian splat point cloud (.ply from a 3DGS trainer, or
// antimatter15's compact .splat) and feeds it to Render3D's dedicated
// EWA-splatting pass via IGeometrySource::GetSplatCloud() - see
// docs/plans/gaussian-splat-node.md S6/S7, phase 3. Terminal source: no
// geometry input, no upstream to forward from.
//
// Mirrors ModelSourceNode's file-handling/caching/drop-target conventions
// (see new-geometry-node skill and ModelSourceNode.h/.cpp) rather than
// inventing new ones - AssetCache-backed decode, a `Load(path)` entry point
// used by both the file picker and the canvas drop handler, and a status
// string surfaced in the node body on failure.
//
// Crop radius and max-splat budget are edit-time cook-step reductions (cheap
// floater removal / LOD, per the design doc) baked into the derived cloud
// returned by GetSplatCloud(); point size / opacity / tint are live render
// multipliers read every frame by Render3D (IGeometrySource::
// SplatSizeMultiplier/SplatOpacityMultiplier/GetSplatTint), so they can be
// modulated without re-cooking the derived cloud or its GPU data texture.
class SplatSourceNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new SplatSourceNode(); }

   // No image output - same convention as ModelSourceNode/GeometryNode; the
   // node's own preview is the point-cloud-approximation viewport (see
   // NodeViewport.cpp), not a rendered texture.
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   // Terminal source: no mesh of its own, ever - see GetSplatCloud() below
   // for the component that actually carries the data. Matches
   // ParticleSystemNode's "static empty Mesh, MeshRevision() == 0" pattern
   // for a points/splats-only source.
   const Mesh& GetMesh() override { static Mesh empty; return empty; }
   unsigned long long MeshRevision() override { return 0; }
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override { return Material(); }

   const SplatIO::SplatCloud* GetSplatCloud() override
   {
      return (bypassed || mDerivedCloud.Empty()) ? nullptr : &mDerivedCloud;
   }
   unsigned long long SplatCloudRevision() override { return bypassed ? 0 : mDerivedRevision; }

   float SplatSizeMultiplier() const override { return pointSize; }
   float SplatOpacityMultiplier() const override { return opacity; }
   void GetSplatTint(float outRgb[3]) const override
   {
      outRgb[0] = tint[0]; outRgb[1] = tint[1]; outRgb[2] = tint[2];
   }

   // Loads path (.ply via SplatIO::LoadSplatPly, .splat via
   // SplatIO::LoadSplatFile, chosen by extension), decoding through the
   // shared AssetCache so re-picking the same file (or an undo/redo respawn)
   // doesn't re-parse it. Rebuilds the derived (cropped/decimated) cloud
   // immediately after a successful load. Returns false and fills Status()
   // with an error on failure, mirroring ModelSourceNode::Load.
   bool Load(const std::string& path);
   const std::string& Path() const { return mPath; }
   const std::string& Status() const { return mStatus; }
   size_t SplatCount() const { return mDerivedCloud.splats.size(); }
   size_t RawSplatCount() const { return mRawCloud.splats.size(); }

   // Reloads from whatever path a patch restored. Called after loading (same
   // contract as ModelSourceNode::ReloadFromPath).
   void ReloadFromPath()
   {
      if (!mPath.empty())
      {
         const std::string p = mPath;
         Load(p);
      }
   }

   // Render multipliers - modulatable via the generic ModSlider registration
   // (see node-ui-pillars: every ModSlider call auto-registers a ParamRef).
   float pointSize = 1.0f; // scale multiplier on the rendered splat radius
   float opacity = 1.0f;   // multiplies per-splat alpha
   float tint[3] = { 1.0f, 1.0f, 1.0f };

   // Edit-time derived-cloud reductions. 0 means "no crop" / "no limit".
   float crop = 0.0f;      // radius from the raw cloud's centroid; 0 = disabled
   int maxSplats = 0;      // budget/LOD top-K by opacity*max(scale); 0 = disabled

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mPath);
      v.Float("pointSize", pointSize);
      v.Float("opacity", opacity);
      v.Color("tint", tint);
      v.Float("crop", crop);
      v.Int("maxSplats", maxSplats);
   }

private:
   void RebuildDerivedCloud();

   std::string mPath;
   std::string mStatus = "no splat loaded";

   SplatIO::SplatCloud mRawCloud;    // exactly what was decoded off disk
   float mCentroid[3] = { 0.0f, 0.0f, 0.0f };
   unsigned long long mLoadRevision = 0; // bumps whenever mRawCloud is (re)loaded

   SplatIO::SplatCloud mDerivedCloud; // mRawCloud after crop/maxSplats
   unsigned long long mDerivedRevision = 0;

   // What mDerivedCloud was last built from - rebuilt only when one of these
   // actually changes (the cache-signature discipline from new-geometry-node
   // SKILL.md S4: CookIfNeeded reads crop/maxSplats, so both must gate the
   // rebuild or a param change would silently freeze the derived cloud).
   unsigned long long mBuiltFromLoadRevision = (unsigned long long)-1;
   float mBuiltCrop = -1.0f;
   int mBuiltMaxSplats = -1;

   int mLastCookFrame = -1;
};
