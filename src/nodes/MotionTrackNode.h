#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "GLUtil.h"
#include "INode.h"
#include "ImageCable.h"
#include "Modulation.h"
#include "Platform.h"

// Motion Track node.
// Tracks the dominant moving object in a video clip using normalized cross-correlation
// and pyramidal Lucas-Kanade optical flow. Emits position, scale, rotation, and confidence
// as continuous modulation signals with an in-frame visual tracking overlay.
class MotionTrackNode : public INode
{
public:
   enum Output
   {
      kImage = 0,
      kX,
      kY,
      kScale,
      kRotation,
      kConfidence,
      kOutputCount
   };

   enum InitMode
   {
      kAutoSubject = 0,
      kAutoMotion,
      kManualBox,
      kInitModeCount
   };

   enum MotionModel
   {
      kPosition = 0,
      kPositionScale,
      kPositionScaleRotation,
      kMotionModelCount
   };

   enum OverlayStyle
   {
      kRing = 0,
      kBox,
      kCrosshair,
      kRingTrail,
      kOverlayStyleCount
   };

   struct TrackSample
   {
      double t = 0.0;
      float x = 0.5f;
      float y = 0.5f;
      float scale = 1.0f;
      float rotation = 0.0f; // degrees (-180..+180)
      float confidence = 1.0f;
      bool lost = false;
   };

   static INode* Create() { return new MotionTrackNode(); }
   static const std::vector<std::string>& InitModeNames();
   static const std::vector<std::string>& MotionModelNames();
   static const std::vector<std::string>& OverlayStyleNames();

   MotionTrackNode();
   ~MotionTrackNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;

   ImageCable& Input() { return mInput; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "image" : nullptr; }
   INode* BypassSource() override { return mInput.GetSource(); }

   float Value(int outputIndex) const;

   // Analysis controls & status
   void StartAnalysis();
   void CancelAnalysis();
   bool IsAnalyzing() const { return mIsAnalyzing.load(); }
   float Progress() const { return mProgress.load(); }
   const std::string& Status() const { return mStatus; }
   int TrackedFrames() const { return mTrackedFrames; }
   int LostFrames() const { return mLostFrames; }
   float AvgConfidence() const { return mAvgConfidence; }
   bool HasTrack() const { return !mTrack.empty(); }
   void ClearTrack();
   bool IsStale() const { return mIsStale; }
   void MarkStale() { mIsStale = true; }

   // Track evaluation
   bool SampleAtTime(double seconds, TrackSample& out) const;
   const std::vector<TrackSample>& Track() const { return mTrack; }
   void SetTrack(const std::vector<TrackSample>& samples);

   // Configuration & parameters
   int initMode = kAutoSubject;
   int motionModel = kPositionScale;
   float searchScale = 2.0f;
   int featureCount = 40;
   float minConfidence = 0.55f;
   float adapt = 0.15f;
   float smooth = 0.0f;
   float sampleFps = 30.0f;
   float offsetX = 0.0f;
   float offsetY = 0.0f;

   bool showOverlay = true;
   int overlayStyle = kRing;
   float overlayColor[3] = { 0.0f, 1.0f, 1.0f }; // cyan
   float overlaySize = 1.0f;

   // Manual box region in normalized (0..1) coords
   float manualBoxX = 0.5f;
   float manualBoxY = 0.5f;
   float manualBoxW = 0.2f;
   float manualBoxH = 0.2f;

   void VisitParams(ParamVisitor& v) override;

private:
   struct Tap : public IModulator
   {
      MotionTrackNode* owner = nullptr;
      int outputIndex = 0;
      float Value01() override { return owner ? owner->Value(outputIndex) : 0.0f; }
   };

   bool EnsureShader();
   void WorkerThreadMain(std::string videoPath,
                         InitMode chosenInitMode,
                         MotionModel chosenMotionModel,
                         float chosenSearchScale,
                         int chosenFeatureCount,
                         float chosenMinConfidence,
                         float chosenAdapt,
                         float chosenSampleFps,
                         float boxX, float boxY, float boxW, float boxH);

   static std::string EncodeTrack(const std::vector<TrackSample>& track);
   static std::vector<TrackSample> DecodeTrack(const std::string& encoded);

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   Tap mTaps[kOutputCount];
   float mCurrentValues[kOutputCount] = { 0 };
   TrackSample mCurrentSample;

   std::vector<TrackSample> mTrack;
   std::string mStatus = "press Analyze to track";
   int mTrackedFrames = 0;
   int mLostFrames = 0;
   float mAvgConfidence = 0.0f;
   bool mIsStale = false;

   // Worker thread state
   std::thread mWorkerThread;
   std::atomic<bool> mIsAnalyzing { false };
   std::atomic<bool> mAbortWorker { false };
   std::atomic<float> mProgress { 0.0f };
   mutable std::mutex mTrackMutex;

   // Position trail for overlay
   static constexpr int kTrailLength = 32;
   float mTrailX[kTrailLength] = { 0 };
   float mTrailY[kTrailLength] = { 0 };
   int mTrailCount = 0;
   int mTrailHead = 0;
};
