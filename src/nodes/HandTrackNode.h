#pragma once

#include <memory>

#include "TrackNodeBase.h"

namespace Tracking { class HandTracker; }

// --- Hand Track ---------------------------------------------------------
// Tracking pack, hand. One hand from an image/video wire as modulators: where it is, how it is held, and a
// fixed set of gestures (each 0 or 1) that can drive anything through the modulation matrix.
class HandTrackNode : public TrackNodeBase
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
      // gestures: 1 while the hand makes the shape, else 0
      kFist, kPoint, kPeace, kThumbsUp, kOk, kRock,
      kOutputCount
   };

   static INode* Create() { return new HandTrackNode(); }

   HandTrackNode();
   ~HandTrackNode() override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;

   const std::vector<std::pair<int, int>>& OverlayLines() const override;
   bool OverlayKeyPoint(int i) const override { return i == 4 || i == 8 || i == 12 || i == 16 || i == 20; }

protected:
   bool OpenTracker(Tracking::OrtRuntime& rt, const std::string& dir, std::string& err) override;
   bool Infer(const Frame& f, float* vals, std::vector<float>& overlay, bool& found, std::string& err) override;
   void CloseTracker() override;
   bool SmoothOutput(int i) const override { return i >= kPalmX && i < kFist; }

private:
   std::unique_ptr<Tracking::HandTracker> mTracker;
};
