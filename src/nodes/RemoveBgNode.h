#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Platform.h"

// Background removal using the OS's own on-device segmentation - no model
// download, no network, no API key.
//
// Masking costs a GPU readback plus a segmentation pass, which is far too
// slow to run every frame at video rates (worse still on Windows, where
// there is no fast fixed-function path like Vision), so the mask is computed
// on a background worker thread with a latest-only request queue: never more
// than one request in flight and one queued, so a slow segmentation pass
// never builds an unbounded backlog and never stalls the render thread.
//
// The mask a worker produces is only ever composited with the exact source
// frame it was computed from (see mPairedSrcTex) - never with whatever frame
// happens to be current when the result arrives - so a moving subject never
// shows a mask torn from N frames of motion ago.
class RemoveBgNode : public INode
{
public:
   static INode* Create() { return new RemoveBgNode(); }
   static const std::vector<std::string>& ModeNames();
   static const std::vector<std::string>& OutputModeNames();

   ~RemoveBgNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }

   void RequestMask() { mNeedsMask = true; }
   const std::string& Status() const { return mStatus; }
   bool HasMask() const { return mMaskTex != 0; }

   int mode = 0;         // 0 = subject, 1 = person
   int outputMode = 0;   // 0 = cutout, 1 = mask only, 2 = background only
   float feather = 0.0f;
   float threshold = 0.5f;
   float contrast = 1.0f;
   bool autoRefresh = false;      // recompute periodically, for video
   float refreshBeats = 1.0f;
   float bgColor[3] = { 0.0f, 0.0f, 0.0f };
   float bgOpacity = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("outputMode", outputMode);
      v.Float("feather", feather); v.Float("threshold", threshold);
      v.Float("contrast", contrast);
      v.Bool("autoRefresh", autoRefresh); v.Float("refreshBeats", refreshBeats);
      v.Color("bgColor", bgColor); v.Float("bgOpacity", bgOpacity);
   }

private:
   // One pending request slot - not a deque. A new frame overwrites whatever
   // was queued but not yet picked up by the worker; it never queues behind
   // a backlog of stale frames.
   struct FrameRequest
   {
      std::vector<unsigned char> pixels;
      int width = 0, height = 0;
      Platform::MattingMode mode = Platform::MattingMode::Subject;
      uint64_t serial = 0;
   };

   // Carries the mask back paired with a copy of the exact source pixels it
   // was computed from, so the two are always composited together.
   struct FrameResult
   {
      std::vector<unsigned char> mask;
      std::vector<unsigned char> pixels;
      int width = 0, height = 0;
      uint64_t serial = 0;
      bool ok = false;
      std::string error;
   };

   bool EnsureShader();
   void SubmitMaskRequest(unsigned int srcTex, int w, int h);
   void PollMaskResult();
   void EnsureWorkerStarted();
   void WorkerThreadMain();

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   unsigned int mMaskTex = 0;
   unsigned int mPairedSrcTex = 0; // the frame mMaskTex was computed from - main thread/GL only
   bool mShaderTried = false;
   bool mNeedsMask = false;
   int mLastCookFrame = -1;
   double mLastMaskBeat = -1000.0;
   std::string mStatus = "press Remove Background";

   // Cross-thread ownership: the main thread owns this node and every GL
   // object; the worker thread only ever reads its own request copy and
   // writes into mPendingResult. The worker never touches a GL object - it
   // receives already-read-back CPU pixels and returns CPU pixels.
   std::thread mWorkerThread;
   bool mWorkerStarted = false;

   std::mutex mRequestMutex;
   std::condition_variable mRequestCv;
   FrameRequest mPendingRequest;   // guarded by mRequestMutex
   bool mRequestPending = false;   // guarded by mRequestMutex
   bool mStopWorker = false;       // guarded by mRequestMutex

   std::mutex mResultMutex;
   FrameResult mPendingResult;           // guarded by mResultMutex
   std::atomic<bool> mResultReady { false };

   uint64_t mNextSerial = 1;
   uint64_t mLastAppliedSerial = 0;
   bool mMaskInFlight = false;
};
