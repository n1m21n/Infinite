#include "SplatSourceNode.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "AssetCache.h"

namespace
{
   // Case-insensitive, mirrors ModelSourceNode.cpp's HasLowerExtension - kept
   // as its own tiny local check rather than shared, same reasoning as there
   // (main.cpp's kModelExt/kSplatExt live at the drop-handler layer).
   bool HasLowerExtension(const std::string& path, const char* ext)
   {
      const size_t dot = path.find_last_of('.');
      if (dot == std::string::npos)
         return false;
      std::string got = path.substr(dot + 1);
      for (char& c : got) c = (char)std::tolower((unsigned char)c);
      return got == ext;
   }

   // Shared across every SplatSourceNode respawn in the process, keyed by
   // path+mtime/size (see AssetCache.h) - mirrors GetModelDecodeCache() in
   // ModelSourceNode.cpp so re-picking the same file, or an undo/redo
   // respawn, doesn't re-parse a multi-million-splat .ply.
   AssetCache<SplatIO::SplatCloud>& GetSplatDecodeCache()
   {
      static AssetCache<SplatIO::SplatCloud> cache(1024ull * 1024 * 1024);
      return cache;
   }

   size_t EstimateBytes(const SplatIO::SplatCloud& cloud)
   {
      return cloud.splats.size() * sizeof(SplatIO::Splat);
   }
}

bool SplatSourceNode::Load(const std::string& path)
{
   const bool isSplat = HasLowerExtension(path, "splat");
   const bool isPly = HasLowerExtension(path, "ply");
   if (!isSplat && !isPly)
   {
      mStatus = "unrecognized splat format (expected .ply or .splat)";
      return false;
   }

   auto& cache = GetSplatDecodeCache();
   if (const SplatIO::SplatCloud* hit = cache.Get(path))
   {
      mRawCloud = *hit;
   }
   else
   {
      SplatIO::SplatCloud decoded;
      std::string error;
      const bool ok = isSplat ? SplatIO::LoadSplatFile(path, decoded, error)
                               : SplatIO::LoadSplatPly(path, decoded, error);
      if (!ok)
      {
         mStatus = error.empty() ? "could not load splat cloud" : error;
         return false;
      }
      cache.Put(path, decoded, EstimateBytes(decoded));
      mRawCloud = std::move(decoded);
   }

   // Centroid of the raw cloud, used by RebuildDerivedCloud()'s crop radius -
   // computed once per load rather than every cook, since it only depends on
   // mRawCloud.
   mCentroid[0] = mCentroid[1] = mCentroid[2] = 0.0f;
   if (!mRawCloud.splats.empty())
   {
      double sum[3] = { 0.0, 0.0, 0.0 };
      for (const SplatIO::Splat& s : mRawCloud.splats)
      {
         sum[0] += s.px; sum[1] += s.py; sum[2] += s.pz;
      }
      const double n = (double)mRawCloud.splats.size();
      mCentroid[0] = (float)(sum[0] / n);
      mCentroid[1] = (float)(sum[1] / n);
      mCentroid[2] = (float)(sum[2] / n);
   }

   mPath = path;
   mLoadRevision = NextTextureRevision();

   const size_t slash = path.find_last_of('/');
   const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
   mStatus = name + " - " + std::to_string(mRawCloud.splats.size()) + " splats";
   if (!mRawCloud.hadTrainedFields)
      mStatus += " (no scale/opacity/color data - auto-estimated)";

   RebuildDerivedCloud();
   return true;
}

void SplatSourceNode::RebuildDerivedCloud()
{
   mBuiltFromLoadRevision = mLoadRevision;
   mBuiltCrop = crop;
   mBuiltMaxSplats = maxSplats;

   mDerivedCloud.splats.clear();
   mDerivedCloud.splats.reserve(mRawCloud.splats.size());

   const bool doCrop = crop > 0.0f;
   const float cropSq = crop * crop;
   for (const SplatIO::Splat& s : mRawCloud.splats)
   {
      if (doCrop)
      {
         const float dx = s.px - mCentroid[0];
         const float dy = s.py - mCentroid[1];
         const float dz = s.pz - mCentroid[2];
         if (dx * dx + dy * dy + dz * dz > cropSq)
            continue;
      }
      mDerivedCloud.splats.push_back(s);
   }

   // Budget/LOD: keep the top maxSplats by opacity*max(scale) - the same
   // "most visually significant first" ranking the design doc calls for,
   // since a plain random or positional cut would bias towards whichever
   // part of the scene happened to be exported first.
   if (maxSplats > 0 && (int)mDerivedCloud.splats.size() > maxSplats)
   {
      std::vector<SplatIO::Splat>& v = mDerivedCloud.splats;
      auto weight = [](const SplatIO::Splat& s) {
         const float maxScale = std::max(s.sx, std::max(s.sy, s.sz));
         return s.a * maxScale;
      };
      std::nth_element(v.begin(), v.begin() + maxSplats, v.end(),
         [&](const SplatIO::Splat& a, const SplatIO::Splat& b) { return weight(a) > weight(b); });
      v.resize(maxSplats);
   }

   float lo[3] = { 1e30f, 1e30f, 1e30f };
   float hi[3] = { -1e30f, -1e30f, -1e30f };
   for (const SplatIO::Splat& s : mDerivedCloud.splats)
   {
      const float p[3] = { s.px, s.py, s.pz };
      for (int k = 0; k < 3; k++)
      {
         lo[k] = std::min(lo[k], p[k]);
         hi[k] = std::max(hi[k], p[k]);
      }
   }
   if (!mDerivedCloud.splats.empty())
   {
      for (int k = 0; k < 3; k++) { mDerivedCloud.boundsMin[k] = lo[k]; mDerivedCloud.boundsMax[k] = hi[k]; }
   }
   else
   {
      for (int k = 0; k < 3; k++) { mDerivedCloud.boundsMin[k] = 0.0f; mDerivedCloud.boundsMax[k] = 0.0f; }
   }

   mDerivedRevision = NextTextureRevision();
}

void SplatSourceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   // Cache-signature discipline (new-geometry-node SKILL.md S4): crop and
   // maxSplats are the two fields this cook step reads that aren't already
   // covered by mLoadRevision, so both must gate the rebuild by hand or a
   // param change would silently freeze the derived cloud/SplatCloudRevision.
   if (mBuiltFromLoadRevision != mLoadRevision || mBuiltCrop != crop || mBuiltMaxSplats != maxSplats)
      RebuildDerivedCloud();
}
