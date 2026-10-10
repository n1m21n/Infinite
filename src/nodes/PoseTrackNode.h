#pragma once

#include <memory>

#include "TrackNodeBase.h"

namespace Tracking { class PoseTracker; }

// --- Pose Track ---------------------------------------------------------
// Tracking pack, body. One person from an image/video wire: head, wrists, how high the arms are, how wide
// they spread, lean and distance. "left" and "right" are the sides of the preview. Works best with the torso in
// view.
class PoseTrackNode : public TrackNodeBase
{
public:
   enum Output
   {
      kPresent = 0,
      kBodyX, kBodyY,
      kHeadX, kHeadY,
      kWristLX, kWristLY, kWristRX, kWristRY,
      kArmL, kArmR,   // arm raised: hanging = 0, overhead = 1
      kSpread,        // distance between the wrists
      kLean,          // torso tilt
      kSize,          // body size in the frame (a depth cue)
      kOutputCount
   };

   static INode* Create() { return new PoseTrackNode(); }

   PoseTrackNode();
   ~PoseTrackNode() override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;

   const std::vector<std::pair<int, int>>& OverlayLines() const override;
   bool OverlayKeyPoint(int i) const override { return i == 0 || i == 15 || i == 16; }

protected:
   bool OpenTracker(Tracking::OrtRuntime& rt, const std::string& dir, std::string& err) override;
   void CloseTracker() override;
   bool Infer(const Frame& f, float* vals, std::vector<float>& overlay, bool& found, std::string& err) override;

private:
   std::unique_ptr<Tracking::PoseTracker> mTracker;
};
