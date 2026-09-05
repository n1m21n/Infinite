#pragma once

#include <string>
#include <vector>

#include "GLUtil.h"
#include "INode.h"
#include "ImageSourceNode.h"

// Folder-backed image sequence. Only the current and next images are kept as
// GL textures; ImageSourceNode's shared decode cache makes revisiting a file
// cheap without retaining an unbounded folder's pixels in this node.
class SlideshowNode : public INode
{
public:
   enum class FitMode
   {
      Native,
      BestFit,
      ProportionalFit
   };

   enum class Transition
   {
      Fade,
      SlideLeft,
      SlideRight,
      WipeLeft,
      WipeRight,
      ZoomFade
   };

   static INode* Create() { return new SlideshowNode(); }
   static const std::vector<std::string>& FitModeNames();
   static const std::vector<std::string>& TransitionNames();

   ~SlideshowNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }

   bool LoadFolder(const std::string& path);
   bool LoadViaDialog();
   void ReloadFromFolder();

   const std::string& FolderPath() const { return mFolderPath; }
   const std::string& LastError() const { return mLastError; }
   int ImageCount() const { return (int)mFiles.size(); }
   int CurrentImageNumber() const;
   std::string CurrentFileName() const;

   float holdDuration = 3.0f;
   float transitionDuration = 1.0f;
   int transition = 0;
   int fitMode = 2;
   float width = 1920.0f;
   float height = 1080.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("folder", mFolderPath);
      v.Float("holdDuration", holdDuration);
      v.Float("transitionDuration", transitionDuration);
      v.Int("transition", transition);
      v.Int("fitMode", fitMode);
      v.Float("width", width);
      v.Float("height", height);
   }

private:
   struct Signature
   {
      unsigned long long folderGeneration = 0;
      int imageA = -1;
      int imageB = -1;
      int sourceWidthA = 0;
      int sourceHeightA = 0;
      int sourceWidthB = 0;
      int sourceHeightB = 0;
      int outputWidth = 0;
      int outputHeight = 0;
      int fit = 0;
      int transitionType = 0;
      float progress = 0.0f;

      bool operator==(const Signature& other) const;
   };

   bool EnsureShader();
   bool ResolveFrames(long long ordinal, int& slotA, int& slotB, int& indexA, int& indexB);
   bool LoadSlot(int slot, int fileIndex);

   std::string mFolderPath;
   std::vector<std::string> mFiles;
   std::vector<int> mPlayableIndices;
   std::string mLastError;

   ImageSourceNode mSources[2];
   int mLoadedIndices[2] = { -1, -1 };
   int mCurrentIndex = -1;

   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   unsigned long long mFolderGeneration = 0;
   unsigned long long mRevision = 0;
   bool mHasBuilt = false;
   Signature mBuilt;
};
