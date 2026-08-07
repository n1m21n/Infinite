#pragma once

#include <string>

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

   // Loads `path` into the texture. Returns false and sets LastError() on failure.
   bool Load(const std::string& path);

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

   unsigned int mTex = 0;
   int mWidth = 0;
   int mHeight = 0;
   bool mHasPlaceholder = false;
   std::string mLoadedPath;
   std::string mLastError;
};
