#pragma once

#include <memory>

#include "TrackNodeBase.h"

namespace Tracking { class FaceTracker; }

// --- Face Track ---------------------------------------------------------
// Tracking pack, face. One face from an image/video wire: head position and turn, eyes, brows, mouth, smile
// and where the eyes look. "left" and "right" are the sides of the preview, so with `mirror` on they match what
// you see in a mirror.
class FaceTrackNode : public TrackNodeBase
{
public:
   enum Output
   {
      kPresent = 0,
      kHeadX, kHeadY,
      kYaw,     // turn: looking to the left of the preview = 0, right = 1
      kPitch,   // nod: looking down = 0, up = 1
      kRoll,    // head tilt
      kEyeL, kEyeR,   // eye open: closed = 0
      kBrow,    // brows raised
      kMouth,   // mouth open
      kSmile,
      kGazeX, kGazeY,   // where the eyes look inside the head
      kOutputCount
   };

   static INode* Create() { return new FaceTrackNode(); }

   FaceTrackNode();
   ~FaceTrackNode() override;

   int OutputCount() const override { return kOutputCount; }
   const char* OutputLabel(int index) const override;

   const std::vector<std::pair<int, int>>& OverlayLines() const override;
   bool OverlayKeyPoint(int i) const override;

protected:
   bool OpenTracker(Tracking::OrtRuntime& rt, const std::string& dir, std::string& err) override;
   void CloseTracker() override;
   bool Infer(const Frame& f, float* vals, std::vector<float>& overlay, bool& found, std::string& err) override;

private:
   std::unique_ptr<Tracking::FaceTracker> mTracker;
};
