#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "INode.h"
#include "GLUtil.h"
#include "NdiRuntime.h"

// NDI In node: takes any NDI video source on the network as an image. One
// worker thread per node owns the NDI finder and receiver (discovery and
// capture can block on the network, so neither ever runs on the UI, audio or
// cook thread); it drops the newest frame into a one-slot mailbox and the cook
// uploads it. No frame yet -> a placeholder and a "waiting" status. Video only.
class NdiInNode : public INode
{
public:
   static INode* Create() { return new NdiInNode(); }

   NdiInNode();
   ~NdiInNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mW > 0 ? mW : kPlaceholder; }
   int GetOutputHeight() const override { return mH > 0 ? mH : kPlaceholder; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }
   bool IsHardwareDriven() const override { return true; }

   std::vector<std::string> AvailableSources() const; // full NDI names, thread-safe copy
   const std::string& SourceName() const { return mTarget; }
   void SelectSource(const std::string& fullName);
   bool IsReceiving() const { return mHaveFrame; }
   bool Connected() const { return mConnected.load(); }

   void VisitParams(ParamVisitor& v) override { v.Text("sourceName", mTarget); }

private:
   void Worker();
   bool EnsureShader();
   void EnsurePlaceholder();

   static constexpr int kPlaceholder = 256;

   // worker-owned state is touched only inside Worker(); shared state below
   std::thread mThread;
   std::atomic<bool> mStop{false};
   std::atomic<bool> mConnected{false};
   mutable std::mutex mMutex;
   std::vector<std::string> mSources; // guarded
   std::string mTarget;               // guarded (read by worker, written by UI)
   std::vector<unsigned char> mFrame; // guarded: newest BGRA frame, top-down, tight rows
   int mFrameW = 0, mFrameH = 0;
   unsigned long long mFrameSeq = 0;

   // cook-thread state
   GLUtil::Fbo mOut;
   unsigned int mProg = 0;
   unsigned int mUploadTex = 0;
   unsigned int mPlaceholderTex = 0;
   unsigned long long mUploadedSeq = 0;
   bool mHaveFrame = false;
   int mW = 0, mH = 0;
   unsigned long long mRevision = 0;
   int mLastCookFrame = -1;
};
