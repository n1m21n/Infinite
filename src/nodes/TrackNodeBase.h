#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Modulation.h"

namespace Tracking { class OrtRuntime; }

// --- Tracking pack nodes: shared base -----------------------------------
// Hand, Face and Pose Track read one image/video wire and publish what they see as modulators. The nodes are
// always registered; the models and ONNX Runtime live in the "tracking" extension pack. Without the pack
// every output reads 0 and the params panel shows an install hint. Output names are static, so patches load
// without the pack. Output 0 is always "present".
//
// Inference runs on a worker thread (newest frame wins), so the render thread only reads back a small frame
// and picks up the latest published values. Axes: x 0 = left of the preview, y 0 = bottom (up is 1), after
// `mirror`. The same small frame and the landmark points are kept for the live preview in the node body.
class TrackNodeBase : public INode
{
public:
   static constexpr int kMaxOutputs = 24;

   TrackNodeBase();
   ~TrackNodeBase() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   IModulator* ModulatorOutput(int index) override;

   ImageCable& Input() { return mInput; }
   float Value(int index) const;

   // For tests and for callers that already hold pixels: bypasses the GL readback. rgb is top-down RGB8,
   // stride w*3.
   void SubmitFrame(const unsigned char* rgb, int w, int h);
   // Blocks until the worker has consumed everything submitted so far.
   void WaitIdle();

   // Status line for the params panel: install hint, "loading", "tracking 1.4 ms".
   std::string Status() const;
   bool PackMissing() const { return mPackMissing.load(); }
   float LastMs() const { return mLastMs.load(); }

   // ---- live preview (UI thread) ----
   // GL texture of the last frame the tracker saw (bottom-up rows), 0 until one arrived.
   unsigned int PreviewTexture() const { return mTex; }
   float PreviewAspect() const { return mTexW > 0 && mTexH > 0 ? (float)mTexW / (float)mTexH : 4.f / 3.f; }
   // Landmark points of the last tracked frame, normalised to the frame (x right, y down, NOT mirrored).
   // Empty when nothing is tracked.
   void GetOverlay(std::vector<float>& xy) const;
   // Index pairs into the overlay points that are joined by a line (a skeleton or contours).
   virtual const std::vector<std::pair<int, int>>& OverlayLines() const;
   // Points drawn bigger than the rest (fingertips, irises, wrists).
   virtual bool OverlayKeyPoint(int /*i*/) const { return false; }

   bool mirror = true;
   float smoothing = 0.5f;  // 0 = raw, 1 = very smooth (One-Euro, speed adaptive)
   float holdMs = 150.0f;   // keep the last values this long after the subject is lost

   void VisitParams(ParamVisitor& v) override
   {
      v.Bool("mirror", mirror);
      v.Float("smoothing", smoothing);
      v.Float("holdMs", holdMs);
   }

protected:
   // Derived classes call this first in their destructor: the worker uses their tracker.
   void StopWorker();

   struct Frame
   {
      std::vector<unsigned char> rgb;
      int w = 0, h = 0;
      double t = 0;
   };

   // Worker thread. Load the models from the pack folder `dir`.
   virtual bool OpenTracker(Tracking::OrtRuntime& rt, const std::string& dir, std::string& err) = 0;
   // Worker thread. Fill `vals` (OutputCount() entries, 0..1) and `overlay` (x,y pairs in 0..1 of the frame),
   // set `found`. Return false with `err` on an inference error.
   virtual bool Infer(const Frame& f, float* vals, std::vector<float>& overlay, bool& found, std::string& err) = 0;
   // Worker thread, when it exits: release the models while the runtime is still alive.
   virtual void CloseTracker() {}
   // Whether output i goes through the One-Euro smoother (flags and gestures should not).
   virtual bool SmoothOutput(int i) const { return i != 0; }

private:
   struct Tap : public IModulator
   {
      TrackNodeBase* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   void EnsureWorker();
   void WorkerLoop();
   void Publish(const float* vals, bool found, double t, const std::vector<float>* overlay);

   ImageCable mInput;
   Tap mTaps[kMaxOutputs];

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
   float mVals[kMaxOutputs] = {};
   std::vector<float> mOverlay;
   double mFoundAt = -1e9;
   bool mFound = false;

   std::atomic<bool> mPackMissing{false};
   std::atomic<float> mLastMs{0.f};
   mutable std::mutex mStatusMu;
   std::string mStatus = "waiting for an image";

   // One-Euro state per output (worker thread only)
   struct Euro { float x = 0, dx = 0; double t = -1; };
   Euro mEuro[kMaxOutputs];
};
