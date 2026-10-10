#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Modulation.h"

// --- Hand Track ---------------------------------------------------------
// Tracking pack, hand. Reads one hand from an image/video wire (Video In,
// Image Source, any image node) and publishes it as modulators. The node is
// always registered; the models and ONNX Runtime live in the "tracking"
// extension pack. Without the pack every output reads 0 and the params panel
// shows an install hint. Outputs are static so patches load without the pack.
//
// Inference runs on a worker thread (newest frame wins), so the render thread
// only reads back a small frame and picks up the latest published values.
// Axes: x 0 = left of the image, y 0 = bottom (up is 1), after `mirror`.
class HandTrackNode : public INode
{
public:
   enum Output
   {
      kPresent = 0,
      kPalmX, kPalmY,
      kIndexX, kIndexY,
      kThumbX, kThumbY,
      kPinch,   // thumb and index tips touching = 1
      kOpen,    // fist = 0, spread fingers = 1
      kRoll,    // hand rotation, -90..+90 degrees = 0..1
      kSize,    // hand size in the frame (a depth cue)
      kOutputCount
   };

   static INode* Create() { return new HandTrackNode(); }

   HandTrackNode();
   ~HandTrackNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;

   ImageCable& Input() { return mInput; }
   float Value(int index) const;

   // For tests and for callers that already hold pixels: bypasses the GL
   // readback. rgb is top-down RGB8, stride w*3.
   void SubmitFrame(const unsigned char* rgb, int w, int h);
   // Blocks until the worker has consumed everything submitted so far.
   void WaitIdle();

   // Status line for the params panel: install hint, "loading", "tracking 1.4 ms".
   std::string Status() const;
   bool PackMissing() const { return mPackMissing.load(); }
   float LastMs() const { return mLastMs.load(); }

   bool mirror = true;
   float smoothing = 0.5f;  // 0 = raw, 1 = very smooth (One-Euro, speed adaptive)
   float holdMs = 150.0f;   // keep the last pose this long after the hand is lost

   void VisitParams(ParamVisitor& v) override
   {
      v.Bool("mirror", mirror);
      v.Float("smoothing", smoothing);
      v.Float("holdMs", holdMs);
   }

private:
   struct Tap : public IModulator
   {
      HandTrackNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   struct Frame
   {
      std::vector<unsigned char> rgb;
      int w = 0, h = 0;
      double t = 0;
   };

   void EnsureWorker();
   void WorkerLoop();
   void Publish(const float* vals, bool found, double t);

   ImageCable mInput;
   Tap mTaps[kOutputCount];

   // GL readback
   unsigned int mFbo = 0, mTex = 0, mProgram = 0;
   int mTexW = 0, mTexH = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   std::vector<unsigned char> mGl;

   // worker
   std::thread mThread;
   std::mutex mMu;
   std::condition_variable mCv, mIdleCv;
   Frame mPending;
   bool mHavePending = false, mBusy = false, mStop = false;

   // published
   mutable std::mutex mOutMu;
   float mVals[kOutputCount] = {};
   double mFoundAt = -1e9;
   bool mFound = false;

   std::atomic<bool> mPackMissing{false};
   std::atomic<float> mLastMs{0.f};
   mutable std::mutex mStatusMu;
   std::string mStatus = "waiting for an image";

   // One-Euro state per output (worker thread only)
   struct Euro { float x = 0, dx = 0; double t = -1; };
   Euro mEuro[kOutputCount];
   float mMirrorSeen = -1;
};
