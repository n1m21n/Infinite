#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Background removal with U2Net through Windows ML + DirectML/DX12, with an
// OpenCV CPU fallback.
//
// Masking costs a GPU readback plus a Vision pass, which is far too slow to run
// every frame at video rates, so the mask is computed on demand (or at a capped
// interval for moving footage) and cached. The mask is then applied on the GPU
// each frame, which is cheap.
class RemoveBgNode : public INode
{
public:
   static INode* Create() { return new RemoveBgNode(); }
   static const std::vector<std::string>& ModeNames();
   static const std::vector<std::string>& BackendNames();
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
   int backend = 0;      // 0 = auto GPU, 1 = DirectML only, 2 = CPU
   int outputMode = 0;   // 0 = cutout, 1 = mask only, 2 = background only
   float feather = 0.0f;
   float threshold = 0.5f;
   float contrast = 1.0f;
   bool autoRefresh = true;       // retained for old patches; Windows refresh is always live
   float refreshBeats = 1.0f;
   float refreshFps = 5.0f;
   int frameCache = 2;
   float bgColor[3] = { 0.0f, 0.0f, 0.0f };
   float bgOpacity = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("backend", backend); v.Int("outputMode", outputMode);
      v.Float("feather", feather); v.Float("threshold", threshold);
      v.Float("contrast", contrast);
      v.Bool("autoRefresh", autoRefresh); v.Float("refreshBeats", refreshBeats);
      v.Float("refreshFps", refreshFps);
      v.Int("frameCache", frameCache);
      v.Color("bgColor", bgColor); v.Float("bgOpacity", bgOpacity);
   }

private:
   bool EnsureShader();
   void QueueMask(unsigned int srcTex, int w, int h);
   void WorkerLoop();
   void ConsumeCompletedMask();
   void PollReadbacks();
   void ReleaseGpuFrames(uint64_t upTo, uint64_t keep);

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   unsigned int mMaskTex = 0;
   unsigned int mPairedSourceTex = 0; // non-owning: texture of the GpuFrame paired with mMaskTex
   struct GpuFrame { uint64_t serial = 0; GLUtil::Fbo fbo; };
   struct PendingReadback { uint64_t serial = 0; unsigned int pbo = 0; void* fence = nullptr; int w = 0, h = 0; };
   std::deque<GpuFrame> mGpuFrames;          // full-res copies awaiting / paired with a mask
   std::vector<GpuFrame> mFreeGpuFrames;
   std::deque<PendingReadback> mPendingReadbacks;
   std::vector<unsigned int> mFreePbos;
   GLUtil::Fbo mSmall;                       // downscaled copy that gets read back
   unsigned int mReadFbo = 0;
   uint64_t mPairedSerial = 0;
   bool mShaderTried = false;
   bool mNeedsMask = false;
   int mLastCookFrame = -1;
   double mLastMaskRequestSeconds = -1000.0;
   std::string mStatus = "press Remove Background";

   std::thread mWorker;
   std::mutex mWorkerMutex;
   std::condition_variable mWorkerWake;
   bool mWorkerStop = false;
   bool mProcessing = false;
   struct FrameRequest
   {
      std::vector<unsigned char> pixels;
      int width = 0, height = 0, mode = 0, backend = 0;
      uint64_t serial = 0;
   };
   std::deque<FrameRequest> mRequests;
   uint64_t mNextSerial = 0;
   std::vector<unsigned char> mCompletedMask;
   int mCompletedWidth = 0;
   int mCompletedHeight = 0;
   uint64_t mCompletedSerial = 0;
   uint64_t mUploadedSerial = 0;
   std::string mCompletedStatus;
};
