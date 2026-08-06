#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Platform.h"

// Video file source. Playback position comes from the global Transport, so the
// play/pause button freezes video alongside every modulator, and the same patch
// re-renders identically when recording.
class VideoSourceNode : public INode
{
public:
   static INode* Create() { return new VideoSourceNode(); }
   ~VideoSourceNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;

   bool OpenViaDialog();
   bool Open(const std::string& path);

   const std::string& LastError() const { return mLastError; }
   const std::string& LoadedPath() const { return mLoadedPath; }
   double Duration() const { return mDuration; }
   double Position() const { return mPosition; }

   bool loop = true;
   float speed = 1.0f;

private:
   void EnsurePlaceholder();

   Platform::VideoHandle* mVideo = nullptr;
   unsigned int mTex = 0;
   int mWidth = 0;
   int mHeight = 0;
   bool mHasPlaceholder = false;
   double mDuration = 0.0;
   double mPosition = 0.0;
   double mLastTransportSeconds = 0.0;
   std::vector<unsigned char> mFrame;
   std::string mLoadedPath;
   std::string mLastError;
   int mLastCookFrame = -1;
};
