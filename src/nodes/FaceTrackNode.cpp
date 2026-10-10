#include "FaceTrackNode.h"

#include <algorithm>
#include <cmath>

#include "core/tracking/FaceTracker.h"

namespace
{
   const char* kLabels[] = {
      "present", "head x", "head y", "yaw", "pitch", "roll",
      "eye L", "eye R", "brows", "mouth", "smile", "gaze x", "gaze y"
   };

   float Clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }

   // The face parts drawn in the preview, as runs of landmark indices (MediaPipe face mesh numbering).
   struct Run { std::vector<int> idx; bool closed; };
   const std::vector<Run>& Runs()
   {
      static const std::vector<Run> r = {
         {{10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152,
           148, 176, 149, 150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109}, true},
         {{33, 246, 161, 160, 159, 158, 157, 173, 133, 155, 154, 153, 145, 144, 163, 7}, true},
         {{263, 466, 388, 387, 386, 385, 384, 398, 362, 382, 381, 380, 374, 373, 390, 249}, true},
         {{61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291, 375, 321, 405, 314, 17, 84, 181, 91, 146}, true},
         {{70, 63, 105, 66, 107}, false},
         {{336, 296, 334, 293, 300}, false},
         {{468}, false},
         {{473}, false},
      };
      return r;
   }

   const std::vector<int>& OverlayIndices()
   {
      static std::vector<int> all;
      if (all.empty())
         for (const Run& r : Runs()) all.insert(all.end(), r.idx.begin(), r.idx.end());
      return all;
   }
}

FaceTrackNode::FaceTrackNode() = default;
FaceTrackNode::~FaceTrackNode() { StopWorker(); }

const char* FaceTrackNode::OutputLabel(int index) const
{
   return (index < 0 || index >= kOutputCount) ? "out" : kLabels[index];
}

const std::vector<std::pair<int, int>>& FaceTrackNode::OverlayLines() const
{
   static std::vector<std::pair<int, int>> lines;
   if (lines.empty())
   {
      int base = 0;
      for (const Run& r : Runs())
      {
         const int n = (int)r.idx.size();
         for (int i = 0; i + 1 < n; i++) lines.emplace_back(base + i, base + i + 1);
         if (r.closed && n > 2) lines.emplace_back(base + n - 1, base);
         base += n;
      }
   }
   return lines;
}

bool FaceTrackNode::OverlayKeyPoint(int i) const
{
   return i >= (int)OverlayIndices().size() - 2;   // the two irises
}

bool FaceTrackNode::OpenTracker(Tracking::OrtRuntime& rt, const std::string& dir, std::string& err)
{
   mTracker = std::make_unique<Tracking::FaceTracker>();
   return mTracker->Open(rt, dir, true, err);
}

void FaceTrackNode::CloseTracker()
{
   if (mTracker) mTracker->Close();
   mTracker.reset();
}

bool FaceTrackNode::Infer(const Frame& f, float* v, std::vector<float>& overlay, bool& found, std::string& err)
{
   Tracking::FaceResult r;
   if (!mTracker || !mTracker->Process(f.rgb.data(), f.w, f.h, r, err))
      return false;
   found = r.found;
   if (!found) return true;

   const float W = (float)f.w, H = (float)f.h;
   auto X = [&](int i) { const float x = r.lm[i][0] / W; return mirror ? 1.f - x : x; };
   auto Y = [&](int i) { return 1.f - r.lm[i][1] / H; };
   auto dist = [&](int a, int b) { return std::hypot(r.lm[a][0] - r.lm[b][0], r.lm[a][1] - r.lm[b][1]); };

   for (int i : OverlayIndices())
   {
      overlay.push_back(r.lm[i][0] / W);
      overlay.push_back(r.lm[i][1] / H);
   }

   const float faceW = std::max(1e-3f, dist(234, 454));
   const float faceH = std::max(1e-3f, dist(10, 152));

   v[kHeadX] = X(1);
   v[kHeadY] = Y(1);

   // Turn and nod from where the nose tip sits between the cheeks and between forehead and chin, measured
   // along the face's own axes so a tilted head does not read as a turn.
   const float ux = (r.lm[454][0] - r.lm[234][0]) / faceW, uy = (r.lm[454][1] - r.lm[234][1]) / faceW;
   const float mx = (r.lm[454][0] + r.lm[234][0]) * 0.5f, my = (r.lm[454][1] + r.lm[234][1]) * 0.5f;
   const float turn = ((r.lm[1][0] - mx) * ux + (r.lm[1][1] - my) * uy) / faceW;   // + = nose toward image right
   v[kYaw] = Clamp01(0.5f + (mirror ? -turn : turn) * 1.4f);
   const float ax = (r.lm[152][0] - r.lm[10][0]) / faceH, ay = (r.lm[152][1] - r.lm[10][1]) / faceH;
   const float t = ((r.lm[1][0] - r.lm[10][0]) * ax + (r.lm[1][1] - r.lm[10][1]) * ay) / faceH;
   v[kPitch] = Clamp01(0.5f - (t - 0.58f) * 3.0f);
   float roll = std::atan2(r.lm[263][1] - r.lm[33][1], r.lm[263][0] - r.lm[33][0]);   // + = clockwise
   if (mirror) roll = -roll;
   v[kRoll] = Clamp01(0.5f + roll / 3.14159265f);

   // eyes: lid gap over eye width
   auto eyeOpen = [&](int up, int lo, int outer, int inner)
   {
      return Clamp01((dist(up, lo) / std::max(1e-3f, dist(outer, inner)) - 0.10f) / 0.20f);
   };
   const float eyeA = eyeOpen(159, 145, 33, 133);    // the eye on the preview-left when not mirrored
   const float eyeB = eyeOpen(386, 374, 263, 362);
   v[kEyeL] = mirror ? eyeB : eyeA;
   v[kEyeR] = mirror ? eyeA : eyeB;

   // brows: gap between brow and upper lid over eye width
   const float browA = (r.lm[159][1] - r.lm[105][1]) / std::max(1e-3f, dist(33, 133));
   const float browB = (r.lm[386][1] - r.lm[334][1]) / std::max(1e-3f, dist(263, 362));
   v[kBrow] = Clamp01(((browA + browB) * 0.5f - 0.35f) / 0.40f);

   const float mouthW = std::max(1e-3f, dist(61, 291));
   v[kMouth] = Clamp01(dist(13, 14) / mouthW / 0.6f);
   v[kSmile] = Clamp01((mouthW / faceW - 0.34f) / 0.14f);

   // gaze: iris position between the eye corners and between the lids
   auto along = [&](int iris, int from, int to)
   {
      const float dx = r.lm[to][0] - r.lm[from][0], dy = r.lm[to][1] - r.lm[from][1];
      const float len2 = std::max(1e-6f, dx * dx + dy * dy);
      return ((r.lm[iris][0] - r.lm[from][0]) * dx + (r.lm[iris][1] - r.lm[from][1]) * dy) / len2;
   };
   const float toRight = (along(468, 33, 133) + (1.f - along(473, 263, 362))) * 0.5f;   // 0.5 = centred
   const float gx = Clamp01(0.5f + (toRight - 0.5f) * 2.2f);
   v[kGazeX] = mirror ? 1.f - gx : gx;
   const float vert = ((r.lm[468][1] - r.lm[159][1]) / std::max(1e-3f, r.lm[145][1] - r.lm[159][1]) +
                       (r.lm[473][1] - r.lm[386][1]) / std::max(1e-3f, r.lm[374][1] - r.lm[386][1])) * 0.5f;
   v[kGazeY] = Clamp01(0.5f + (0.5f - vert) * 2.5f);
   return true;
}
