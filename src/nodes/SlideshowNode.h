#pragma once

#include <chrono>
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

   // Turbo 0.50: manual stepping (UI, MIDI learn, Performance Mode, CV via
   // the buttons' pins). Applied on the next cook. Restart cuts to the first
   // image and restarts the hold timer; next / prev play the chosen
   // transition, then the auto-advance carries on from the new image.
   void RequestRestart() { mRequest = Request::Restart; }
   void RequestNext() { mRequest = Request::Next; }
   void RequestPrev() { mRequest = Request::Prev; }

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
   // direction: +1 shows ordinal -> ordinal + 1, -1 (manual prev) ordinal -> ordinal - 1.
   bool ResolveFrames(long long ordinal, int direction, int& slotA, int& slotB, int& indexA, int& indexB);
   void AutoPhase(double now, double hold, double fade, double step, long long& ordinal, float& progress) const;
   void ApplyRequest(double now, double hold, double fade, double step);
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

   // Turbo 0.50: manual stepping state (runtime only, not saved). With no
   // step taken, offset 0 and origin 0 reproduce the old pure-transport timing.
   enum class Request { None, Restart, Next, Prev };
   Request mRequest = Request::None;
   long long mOrdinalOffset = 0;
   double mTimeOrigin = 0.0; // transport seconds where mOrdinalOffset's hold began
   bool mManual = false;     // a next/prev transition is running (wall clock, so it animates with the transport stopped)
   long long mManualFrom = 0;
   long long mManualTo = 0;
   std::chrono::steady_clock::time_point mManualStart;
};
