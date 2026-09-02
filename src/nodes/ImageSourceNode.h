#pragma once

#include <string>
#include <vector>

#include "INode.h"

// File-backed source node. Loads via stb_image on demand (Load()), uploads to a
// GL texture, and just hands that texture downstream every frame - no per-frame
// work once loaded. Falls back to a procedural checker so an unwired/failed
// graph still shows something instead of black.
class ImageSourceNode : public INode
{
public:
   static INode* Create() { return new ImageSourceNode(); }
   ~ImageSourceNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;
   // Loaded (or placeholder) texture only changes when Load()/EnsurePlaceholder()
   // actually re-upload it - not every frame - so downstream FilterNode chains
   // can cache against a stable stamp instead of always assuming new pixels.
   unsigned long long TextureRevision() const override { return mRevision; }

   // Loads `path` into the texture. Returns false and sets LastError() on failure.
   // A "gltf://<path>#<slot>" pseudo-path (slot one of
   // albedo|roughness|metallic|normal|ao|emission) routes to
   // GltfImport::DecodeCached instead of a real file decode - see
   // LoadFromDecoded's comment for why this exists.
   bool Load(const std::string& path);

   // Uploads already-decoded RGBA8 pixels directly, skipping Platform's
   // file/OS decoders entirely. Used for textures derived in memory from a
   // glTF/GLB import (embedded or channel-split maps that were never their
   // own file on disk). `pseudoPath` is stored as LoadedPath() the same way
   // a real path would be, so VisitParams/save-load and ReloadFromPath()
   // still work - see the "gltf://" scheme documented on Load().
   bool LoadFromDecoded(const std::vector<unsigned char>& pixels, int w, int h,
                        const std::string& pseudoPath);

   // Opens the native picker, then loads. Returns false if cancelled or failed.
   bool LoadViaDialog();
   const std::string& LastError() const { return mLastError; }
   const std::string& LoadedPath() const { return mLoadedPath; }

   std::string pathInput; // bound to the ImGui text field

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("path", mLoadedPath);
   }

   void ReloadFromPath()
   {
      if (!mLoadedPath.empty())
      {
         const std::string p = mLoadedPath;
         Load(p);
      }
   }

private:
   void EnsurePlaceholder();
   // Shared GL-upload tail of Load()/LoadFromDecoded()/the gltf:// branch of
   // Load() - everything after pixels are in hand, parameterized on the
   // pixel buffer instead of whatever decoded it.
   void UploadPixels(const std::vector<unsigned char>& pixels, int w, int h);

   unsigned int mTex = 0;
   int mWidth = 0;
   int mHeight = 0;
   bool mHasPlaceholder = false;
   std::string mLoadedPath;
   std::string mLastError;
   unsigned long long mRevision = 0;
};
