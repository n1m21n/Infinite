#include "HandTrackNode.h"

#include <algorithm>
#include <cmath>

#include "core/tracking/HandTracker.h"

namespace
{
   const char* kLabels[] = {
      "present", "palm x", "palm y", "index x", "index y", "thumb x", "thumb y",
      "pinch", "open", "roll", "size",
      "fist", "point", "peace", "thumbs up", "ok", "rock"
   };

   float Clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
}

HandTrackNode::HandTrackNode() = default;
HandTrackNode::~HandTrackNode()
{
   // The worker uses mTracker; stop it before the tracker goes.
   StopWorker();
}

const char* HandTrackNode::OutputLabel(int index) const
{
   return (index < 0 || index >= kOutputCount) ? "out" : kLabels[index];
}

const std::vector<std::pair<int, int>>& HandTrackNode::OverlayLines() const
{
   static const std::vector<std::pair<int, int>> kLines = {
      {0, 1}, {1, 2}, {2, 3}, {3, 4},
      {0, 5}, {5, 6}, {6, 7}, {7, 8},
      {5, 9}, {9, 10}, {10, 11}, {11, 12},
      {9, 13}, {13, 14}, {14, 15}, {15, 16},
      {13, 17}, {17, 18}, {18, 19}, {19, 20}, {0, 17}
   };
   return kLines;
}

bool HandTrackNode::OpenTracker(Tracking::OrtRuntime& rt, const std::string& dir, std::string& err)
{
   mTracker = std::make_unique<Tracking::HandTracker>();
   return mTracker->Open(rt, dir, true, err);
}

void HandTrackNode::CloseTracker()
{
   if (mTracker) mTracker->Close();
   mTracker.reset();
}

bool HandTrackNode::Infer(const Frame& f, float* v, std::vector<float>& overlay, bool& found, std::string& err)
{
   Tracking::HandResult r;
   if (!mTracker || !mTracker->Process(f.rgb.data(), f.w, f.h, r, err))
      return false;
   found = r.found;
   if (!found) return true;

   const float W = (float)f.w, H = (float)f.h;
   auto X = [&](int i) { const float x = r.lm[i][0] / W; return mirror ? 1.f - x : x; };
   auto Y = [&](int i) { return 1.f - r.lm[i][1] / H; };
   auto dist = [&](int a, int b) { return std::hypot(r.lm[a][0] - r.lm[b][0], r.lm[a][1] - r.lm[b][1]); };

   overlay.resize(42);
   for (int i = 0; i < 21; i++) { overlay[i * 2] = r.lm[i][0] / W; overlay[i * 2 + 1] = r.lm[i][1] / H; }

   const float s = std::max(1e-3f, dist(0, 9));  // wrist to middle knuckle
   float tips = 0;
   for (int i : {8, 12, 16, 20}) tips += dist(0, i);
   tips /= 4.f * s;
   const float vx = r.lm[9][0] - r.lm[0][0], vy = r.lm[9][1] - r.lm[0][1];
   float roll = std::atan2(vx, -vy);          // 0 = fingers up, + = clockwise
   if (mirror) roll = -roll;
   v[kPalmX] = (X(0) + X(5) + X(9) + X(13) + X(17)) / 5.f;
   v[kPalmY] = (Y(0) + Y(5) + Y(9) + Y(13) + Y(17)) / 5.f;
   v[kIndexX] = X(8); v[kIndexY] = Y(8);
   v[kThumbX] = X(4); v[kThumbY] = Y(4);
   v[kPinch] = 1.f - Clamp01((dist(4, 8) / s - 0.15f) / 0.85f);
   v[kOpen] = Clamp01((tips - 1.0f) / 1.0f);
   v[kRoll] = Clamp01(0.5f + roll / 3.14159265f);
   v[kSize] = Clamp01(s / (0.4f * H));

   // Gestures from which fingers are out. A finger is out when its tip is clearly farther from the wrist than
   // its middle joint, which holds at any hand rotation.
   auto out = [&](int tip, int pip) { return dist(0, tip) > dist(0, pip) * 1.15f; };
   const bool idx = out(8, 6), mid = out(12, 10), ring = out(16, 14), pnk = out(20, 18);
   const bool thumb = dist(4, 17) > dist(3, 17) * 1.1f && dist(4, 5) > 0.5f * s;
   const bool pinch = v[kPinch] > 0.6f;
   const bool thumbUp = r.lm[4][1] < r.lm[5][1] - 0.3f * s;   // thumb tip above the index knuckle (image y down)
   v[kFist] = (!idx && !mid && !ring && !pnk && !thumbUp) ? 1.f : 0.f;
   v[kPoint] = (idx && !mid && !ring && !pnk) ? 1.f : 0.f;
   v[kPeace] = (idx && mid && !ring && !pnk) ? 1.f : 0.f;
   v[kRock] = (idx && !mid && !ring && pnk) ? 1.f : 0.f;
   v[kThumbsUp] = (thumb && thumbUp && !idx && !mid && !ring && !pnk) ? 1.f : 0.f;
   v[kOk] = (pinch && mid && ring && pnk) ? 1.f : 0.f;
   return true;
}
