#include "PoseTrackNode.h"

#include <algorithm>
#include <cmath>

#include "core/tracking/PoseTracker.h"

namespace
{
   const char* kLabels[] = {
      "present", "body x", "body y", "head x", "head y",
      "wrist L x", "wrist L y", "wrist R x", "wrist R y",
      "arm L", "arm R", "spread", "lean", "size"
   };

   float Clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
}

PoseTrackNode::PoseTrackNode() = default;
PoseTrackNode::~PoseTrackNode() { StopWorker(); }

const char* PoseTrackNode::OutputLabel(int index) const
{
   return (index < 0 || index >= kOutputCount) ? "out" : kLabels[index];
}

const std::vector<std::pair<int, int>>& PoseTrackNode::OverlayLines() const
{
   static const std::vector<std::pair<int, int>> kLines = {
      {11, 12}, {11, 13}, {13, 15}, {12, 14}, {14, 16}, {11, 23}, {12, 24}, {23, 24},
      {23, 25}, {25, 27}, {24, 26}, {26, 28}, {15, 19}, {16, 20}, {27, 31}, {28, 32}, {0, 9}, {0, 10}
   };
   return kLines;
}

bool PoseTrackNode::OpenTracker(Tracking::OrtRuntime& rt, const std::string& dir, std::string& err)
{
   mTracker = std::make_unique<Tracking::PoseTracker>();
   return mTracker->Open(rt, dir, true, err);
}

void PoseTrackNode::CloseTracker()
{
   if (mTracker) mTracker->Close();
   mTracker.reset();
}

bool PoseTrackNode::Infer(const Frame& f, float* v, std::vector<float>& overlay, bool& found, std::string& err)
{
   Tracking::PoseResult r;
   if (!mTracker || !mTracker->Process(f.rgb.data(), f.w, f.h, r, err))
      return false;
   found = r.found;
   if (!found) return true;

   const float W = (float)f.w, H = (float)f.h;
   auto X = [&](int i) { const float x = r.lm[i][0] / W; return mirror ? 1.f - x : x; };
   auto Y = [&](int i) { return 1.f - r.lm[i][1] / H; };
   auto dist = [&](int a, int b) { return std::hypot(r.lm[a][0] - r.lm[b][0], r.lm[a][1] - r.lm[b][1]); };

   overlay.resize(33 * 2);
   for (int i = 0; i < 33; i++) { overlay[i * 2] = r.lm[i][0] / W; overlay[i * 2 + 1] = r.lm[i][1] / H; }

   // landmark 11 is the person's left side, which shows on the right of an unmirrored image
   const int pl = mirror ? 15 : 16, pr = mirror ? 16 : 15;   // wrists on the preview-left / preview-right
   const int sl = mirror ? 11 : 12, sr = mirror ? 12 : 11;   // shoulders
   const float sx = (r.lm[11][0] + r.lm[12][0]) * 0.5f, sy = (r.lm[11][1] + r.lm[12][1]) * 0.5f;
   const float hx = (r.lm[23][0] + r.lm[24][0]) * 0.5f, hy = (r.lm[23][1] + r.lm[24][1]) * 0.5f;
   const float torso = std::max(1e-3f, std::hypot(sx - hx, sy - hy));
   const float shoulderW = std::max(1e-3f, dist(11, 12));

   v[kBodyX] = mirror ? 1.f - (sx + hx) * 0.5f / W : (sx + hx) * 0.5f / W;
   v[kBodyY] = 1.f - (sy + hy) * 0.5f / H;
   v[kHeadX] = X(0); v[kHeadY] = Y(0);
   v[kWristLX] = X(pl); v[kWristLY] = Y(pl);
   v[kWristRX] = X(pr); v[kWristRY] = Y(pr);
   auto raise = [&](int wrist, int shoulder)
   {
      return Clamp01(((r.lm[shoulder][1] - r.lm[wrist][1]) / torso + 0.25f) / 1.25f);
   };
   v[kArmL] = raise(pl, sl);
   v[kArmR] = raise(pr, sr);
   v[kSpread] = Clamp01(dist(15, 16) / shoulderW / 4.f);
   float lean = std::atan2(sx - hx, hy - sy);   // 0 = upright, + = leaning to the image right
   if (mirror) lean = -lean;
   v[kLean] = Clamp01(0.5f + lean / 1.0472f);
   v[kSize] = Clamp01(torso / (0.5f * H));
   return true;
}
