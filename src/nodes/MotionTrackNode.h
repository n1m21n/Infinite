#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Modulation.h"

// Motion Track node.
// Tracks the dominant moving object in a video clip using normalized cross-correlation
// and pyramidal Lucas-Kanade optical flow. Emits position, scale, and rotation as
// modulator outputs - wire them into any param (a Transform's position, for example)
// to move something else along the tracked path. Produces no image of its own.
class MotionTrackNode : public INode
{
public:
   enum InitMode
   {
      kAutoPerson = 0,
      kAutoSubject,
      kAutoMotion,
      kInitModeCount
   };

   enum MotionModel
   {
      kPosition = 0,
      kPositionScale,
      kPositionScaleRotation,
      kMotionModelCount
   };

   enum Output
   {
      kOutX = 0,
      kOutY,
      kOutScale,
      kOutRotation,
      kOutputCount
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

   MotionTrackNode();
   ~MotionTrackNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;
   float Value(int index) const;

   ImageCable& Input() { return mInput; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "image" : nullptr; }

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
   const TrackSample& CurrentSample() const { return mCurrentSample; }
   const std::vector<TrackSample>& Track() const { return mTrack; }
   void SetTrack(const std::vector<TrackSample>& samples);
   float TrackBoxW() const { return mTrackBoxW; }
   float TrackBoxH() const { return mTrackBoxH; }

   // Configuration & parameters
   int initMode = kAutoPerson;
   int motionModel = kPositionScale;
   float searchScale = 2.0f;
   int featureCount = 40;
   float minConfidence = 0.55f;
   float adapt = 0.15f;
   float smooth = 0.0f;
   float sampleFps = 30.0f;
   float offsetX = 0.0f;
   float offsetY = 0.0f;

   void VisitParams(ParamVisitor& v) override;

private:
   void WorkerThreadMain(std::string videoPath,
                         InitMode chosenInitMode,
                         MotionModel chosenMotionModel,
                         float chosenSearchScale,
                         int chosenFeatureCount,
                         float chosenMinConfidence,
                         float chosenAdapt,
                         float chosenSampleFps);

   static std::string EncodeTrack(const std::vector<TrackSample>& track);
   static std::vector<TrackSample> DecodeTrack(const std::string& encoded);

   // Publishes Value(index) as an IModulator - see AnalyzeNodes.h's identical Tap pattern.
   struct Tap : public IModulator
   {
      MotionTrackNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };
   Tap mTaps[kOutputCount];

   ImageCable mInput;
   int mLastCookFrame = -1;

   TrackSample mCurrentSample;
   float mTrackBoxW = 0.25f;
   float mTrackBoxH = 0.25f;

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
};
