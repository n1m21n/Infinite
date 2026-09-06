#include "MotionTrackNode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "Platform.h"
#include "Transport.h"
#include "nodes/RemoveBgNode.h"
#include "nodes/VideoSourceNode.h"

namespace
{
   const std::vector<std::string> kInitModeNames = {
      "Auto (person)",
      "Auto (subject)",
      "Auto (motion)"
   };

   const std::vector<std::string> kMotionModelNames = {
      "Position",
      "Position + Scale",
      "Position + Scale + Rotation"
   };

   // Base64 helper
   static const char kBase64Chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";

   std::string Base64Encode(const unsigned char* data, size_t len)
   {
      std::string ret;
      ret.reserve(((len + 2) / 3) * 4);
      int val = 0, valb = -6;
      for (size_t i = 0; i < len; ++i)
      {
         val = (val << 8) + data[i];
         valb += 8;
         while (valb >= 0)
         {
            ret.push_back(kBase64Chars[(val >> valb) & 0x3F]);
            valb -= 6;
         }
      }
      if (valb > -6)
         ret.push_back(kBase64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
      while (ret.size() % 4)
         ret.push_back('=');
      return ret;
   }

   std::vector<unsigned char> Base64Decode(const std::string& str)
   {
      std::vector<int> T(256, -1);
      for (int i = 0; i < 64; i++)
         T[(unsigned char)kBase64Chars[i]] = i;

      std::vector<unsigned char> out;
      out.reserve(str.size() * 3 / 4);
      int val = 0, valb = -8;
      for (unsigned char c : str)
      {
         if (T[c] == -1)
            break;
         val = (val << 6) + T[c];
         valb += 6;
         if (valb >= 0)
         {
            out.push_back((unsigned char)((val >> valb) & 0xFF));
            valb -= 8;
         }
      }
      return out;
   }

   // Fast Image Pyramid and KLT structures
   struct GrayImage
   {
      int w = 0;
      int h = 0;
      std::vector<float> data;

      float Sample(float x, float y) const
      {
         if (w <= 0 || h <= 0 || data.empty())
            return 0.0f;
         x = std::clamp(x, 0.0f, (float)(w - 1));
         y = std::clamp(y, 0.0f, (float)(h - 1));
         int x0 = (int)x;
         int y0 = (int)y;
         int x1 = std::min(x0 + 1, w - 1);
         int y1 = std::min(y0 + 1, h - 1);
         float fx = x - (float)x0;
         float fy = y - (float)y0;

         float p00 = data[y0 * w + x0];
         float p10 = data[y0 * w + x1];
         float p01 = data[y1 * w + x0];
         float p11 = data[y1 * w + x1];

         return (1.0f - fx) * (1.0f - fy) * p00 +
                fx * (1.0f - fy) * p10 +
                (1.0f - fx) * fy * p01 +
                fx * fy * p11;
      }
   };

   struct Pyramid
   {
      static constexpr int kLevels = 4;
      GrayImage levels[kLevels];

      void Build(const std::vector<unsigned char>& rgba, int width, int height)
      {
         if (width <= 0 || height <= 0 || rgba.size() < (size_t)(width * height * 4))
            return;

         levels[0].w = width;
         levels[0].h = height;
         levels[0].data.resize(width * height);

         // Convert RGBA8 to Grayscale float: 0.299r + 0.587g + 0.114b
         const unsigned char* src = rgba.data();
         float* dst = levels[0].data.data();
         const size_t total = (size_t)(width * height);
         for (size_t i = 0; i < total; ++i)
         {
            dst[i] = (0.299f * src[i * 4 + 0] +
                      0.587f * src[i * 4 + 1] +
                      0.114f * src[i * 4 + 2]) * (1.0f / 255.0f);
         }

         // Downsample pyramid levels
         for (int l = 1; l < kLevels; ++l)
         {
            const GrayImage& prev = levels[l - 1];
            GrayImage& curr = levels[l];
            curr.w = std::max(1, prev.w / 2);
            curr.h = std::max(1, prev.h / 2);
            curr.data.resize(curr.w * curr.h);

            for (int y = 0; y < curr.h; ++y)
            {
               int py0 = std::min(y * 2, prev.h - 1);
               int py1 = std::min(py0 + 1, prev.h - 1);
               for (int x = 0; x < curr.w; ++x)
               {
                  int px0 = std::min(x * 2, prev.w - 1);
                  int px1 = std::min(px0 + 1, prev.w - 1);

                  float sum = prev.data[py0 * prev.w + px0] +
                              prev.data[py0 * prev.w + px1] +
                              prev.data[py1 * prev.w + px0] +
                              prev.data[py1 * prev.w + px1];
                  curr.data[y * curr.w + x] = sum * 0.25f;
               }
            }
         }
      }
   };

   struct Point2f
   {
      float x = 0.0f;
      float y = 0.0f;
   };

   // Shi-Tomasi corner detector inside bounding box
   std::vector<Point2f> DetectCorners(const GrayImage& img, float boxMinX, float boxMinY,
                                      float boxMaxX, float boxMaxY, int maxCorners)
   {
      std::vector<Point2f> corners;
      if (img.w <= 6 || img.h <= 6 || maxCorners <= 0)
         return corners;

      // The structural-tensor loop below samples gradients at px = x+dx (dx in
      // [-2,2]) using px-1 and px+1, so the real pixel reach from a candidate
      // (x,y) is +-3, not +-2. A margin of 2/3 left this one row/column short
      // and let (py+1)*w+px walk one row past the end of img.data whenever the
      // detection window touched the bottom or right edge (crashed on Analyze).
      int x0 = std::max(3, (int)boxMinX);
      int y0 = std::max(3, (int)boxMinY);
      int x1 = std::min(img.w - 4, (int)boxMaxX);
      int y1 = std::min(img.h - 4, (int)boxMaxY);
      if (x1 <= x0 || y1 <= y0)
         return corners;

      struct Candidate
      {
         int x, y;
         float score;
      };
      std::vector<Candidate> candidates;

      const int w = img.w;
      for (int y = y0; y <= y1; ++y)
      {
         for (int x = x0; x <= x1; ++x)
         {
            // Compute structural tensor over 5x5 window
            float sxx = 0.0f, syy = 0.0f, sxy = 0.0f;
            for (int dy = -2; dy <= 2; ++dy)
            {
               int py = y + dy;
               for (int dx = -2; dx <= 2; ++dx)
               {
                  int px = x + dx;
                  float ix = (img.data[py * w + (px + 1)] - img.data[py * w + (px - 1)]) * 0.5f;
                  float iy = (img.data[(py + 1) * w + px] - img.data[(py - 1) * w + px]) * 0.5f;
                  sxx += ix * ix;
                  syy += iy * iy;
                  sxy += ix * iy;
               }
            }
            // Minimum eigenvalue: 0.5 * (sxx + syy - sqrt((sxx - syy)^2 + 4 * sxy^2))
            float trace = sxx + syy;
            float det = sxx * syy - sxy * sxy;
            float disc = trace * trace - 4.0f * det;
            if (disc < 0.0f) disc = 0.0f;
            float lambdaMin = 0.5f * (trace - std::sqrt(disc));

            if (lambdaMin > 0.001f)
               candidates.push_back({ x, y, lambdaMin });
         }
      }

      std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
         return a.score > b.score;
      });

      // Spatial non-maximum suppression / minimum distance
      const float minDistSq = 16.0f; // 4 pixels apart
      for (const auto& c : candidates)
      {
         bool tooClose = false;
         for (const auto& pt : corners)
         {
            float dx = (float)c.x - pt.x;
            float dy = (float)c.y - pt.y;
            if (dx * dx + dy * dy < minDistSq)
            {
               tooClose = true;
               break;
            }
         }
         if (!tooClose)
         {
            corners.push_back({ (float)c.x, (float)c.y });
            if ((int)corners.size() >= maxCorners)
               break;
         }
      }

      return corners;
   }

   // Pyramidal Lucas-Kanade optical flow with Forward-Backward consistency check
   bool TrackPointsKLT(const Pyramid& prevPyr, const Pyramid& currPyr,
                       const std::vector<Point2f>& prevPts,
                       std::vector<Point2f>& outCurrPts,
                       std::vector<bool>& outStatus)
   {
      const size_t n = prevPts.size();
      outCurrPts = prevPts;
      outStatus.assign(n, false);
      if (n == 0)
         return false;

      const int winSize = 7; // 7x7 patch
      const int halfWin = winSize / 2;

      auto RunLK = [&](const Pyramid& pyrA, const Pyramid& pyrB,
                       const std::vector<Point2f>& inPts,
                       std::vector<Point2f>& outPts,
                       std::vector<bool>& status)
      {
         outPts = inPts;
         status.assign(inPts.size(), false);
         for (size_t i = 0; i < inPts.size(); ++i)
         {
            Point2f pt = inPts[i];
            Point2f guess = pt;
            bool validPoint = true;

            for (int l = Pyramid::kLevels - 2; l >= 0; --l)
            {
               const GrayImage& I = pyrA.levels[l];
               const GrayImage& J = pyrB.levels[l];
               float scale = 1.0f / (float)(1 << l);

               Point2f pL = { pt.x * scale, pt.y * scale };
               Point2f gL = { guess.x * scale, guess.y * scale };

               float Gxx = 0.0f, Gyy = 0.0f, Gxy = 0.0f;
               std::vector<float> patchI(winSize * winSize);
               std::vector<float> patchIx(winSize * winSize);
               std::vector<float> patchIy(winSize * winSize);

               for (int dy = -halfWin; dy <= halfWin; ++dy)
               {
                  for (int dx = -halfWin; dx <= halfWin; ++dx)
                  {
                     float px = pL.x + (float)dx;
                     float py = pL.y + (float)dy;
                     float val = I.Sample(px, py);
                     float ix = (I.Sample(px + 1.0f, py) - I.Sample(px - 1.0f, py)) * 0.5f;
                     float iy = (I.Sample(px, py + 1.0f) - I.Sample(px, py - 1.0f)) * 0.5f;

                     int idx = (dy + halfWin) * winSize + (dx + halfWin);
                     patchI[idx] = val;
                     patchIx[idx] = ix;
                     patchIy[idx] = iy;

                     Gxx += ix * ix;
                     Gyy += iy * iy;
                     Gxy += ix * iy;
                  }
               }

               float det = Gxx * Gyy - Gxy * Gxy;
               if (det < 1e-6f)
               {
                  validPoint = false;
                  break;
               }
               float invDet = 1.0f / det;

               for (int iter = 0; iter < 4; ++iter)
               {
                  float bx = 0.0f, by = 0.0f;
                  for (int dy = -halfWin; dy <= halfWin; ++dy)
                  {
                     for (int dx = -halfWin; dx <= halfWin; ++dx)
                     {
                        int idx = (dy + halfWin) * winSize + (dx + halfWin);
                        float qx = gL.x + (float)dx;
                        float qy = gL.y + (float)dy;
                        float valJ = J.Sample(qx, qy);
                        float it = patchI[idx] - valJ;

                        bx += it * patchIx[idx];
                        by += it * patchIy[idx];
                     }
                  }

                  float deltaX = (Gyy * bx - Gxy * by) * invDet;
                  float deltaY = (Gxx * by - Gxy * bx) * invDet;

                  gL.x += deltaX;
                  gL.y += deltaY;

                  if (deltaX * deltaX + deltaY * deltaY < 0.0001f)
                     break;
               }

               guess.x = gL.x * (float)(1 << l);
               guess.y = gL.y * (float)(1 << l);
            }

            if (validPoint &&
                guess.x >= 0.0f && guess.x < (float)pyrB.levels[0].w &&
                guess.y >= 0.0f && guess.y < (float)pyrB.levels[0].h)
            {
               outPts[i] = guess;
               status[i] = true;
            }
            else
            {
               status[i] = false;
            }
         }
      };

      // 1. Forward pass
      std::vector<Point2f> fwdPts;
      std::vector<bool> fwdStatus;
      RunLK(prevPyr, currPyr, prevPts, fwdPts, fwdStatus);

      // 2. Backward pass for forward-backward validation
      std::vector<Point2f> backPts;
      std::vector<bool> backStatus;
      RunLK(currPyr, prevPyr, fwdPts, backPts, backStatus);

      // 3. Keep only points with forward-backward error < 1.5 px
      const float maxFbDistSq = 2.25f; // 1.5 px squared
      for (size_t i = 0; i < n; ++i)
      {
         if (fwdStatus[i] && backStatus[i])
         {
            float dx = backPts[i].x - prevPts[i].x;
            float dy = backPts[i].y - prevPts[i].y;
            if (dx * dx + dy * dy <= maxFbDistSq)
            {
               outCurrPts[i] = fwdPts[i];
               outStatus[i] = true;
               continue;
            }
         }
         outStatus[i] = false;
      }

      return true;
   }

   // Normalized Cross-Correlation (NCC)
   float ComputeNCC(const std::vector<float>& patchA, const std::vector<float>& patchB)
   {
      if (patchA.empty() || patchA.size() != patchB.size())
         return 0.0f;

      const size_t n = patchA.size();
      double sumA = 0.0, sumB = 0.0;
      for (size_t i = 0; i < n; ++i)
      {
         sumA += patchA[i];
         sumB += patchB[i];
      }
      double meanA = sumA / (double)n;
      double meanB = sumB / (double)n;

      double num = 0.0, denA = 0.0, denB = 0.0;
      for (size_t i = 0; i < n; ++i)
      {
         double da = (double)patchA[i] - meanA;
         double db = (double)patchB[i] - meanB;
         num += da * db;
         denA += da * da;
         denB += db * db;
      }
      double den = std::sqrt(denA * denB);
      if (den < 1e-6)
         return 0.0f;
      return (float)std::clamp(num / den, -1.0, 1.0);
   }

   void SamplePatch(const GrayImage& img, float cx, float cy, float w, float h,
                    int sampleW, int sampleH, float angleRad, std::vector<float>& outPatch)
   {
      outPatch.resize(sampleW * sampleH);
      float cosA = std::cos(angleRad);
      float sinA = std::sin(angleRad);

      for (int y = 0; y < sampleH; ++y)
      {
         float v = ((float)y + 0.5f) / (float)sampleH - 0.5f;
         for (int x = 0; x < sampleW; ++x)
         {
            float u = ((float)x + 0.5f) / (float)sampleW - 0.5f;

            float lx = u * w;
            float ly = v * h;

            float rx = lx * cosA - ly * sinA;
            float ry = lx * sinA + ly * cosA;

            float sx = cx + rx;
            float sy = cy + ry;

            outPatch[y * sampleW + x] = img.Sample(sx, sy);
         }
      }
   }

   // Coarse NCC search on pyramid level 2
   Point2f CoarseNCCSearch(const GrayImage& imgLevel2, const std::vector<float>& templateL2,
                          int tplW, int tplH, float predCx, float predCy,
                          float searchRadiusX, float searchRadiusY, float& outScore)
   {
      outScore = 0.0f;
      Point2f bestPos = { predCx, predCy };
      if (imgLevel2.w <= tplW || imgLevel2.h <= tplH || templateL2.empty())
         return bestPos;

      int minX = std::max((int)(predCx - searchRadiusX), 0);
      int maxX = std::min((int)(predCx + searchRadiusX), imgLevel2.w - tplW);
      int minY = std::max((int)(predCy - searchRadiusY), 0);
      int maxY = std::min((int)(predCy + searchRadiusY), imgLevel2.h - tplH);

      float bestNCC = -1.0f;
      std::vector<float> candPatch(tplW * tplH);

      for (int y = minY; y <= maxY; y += 2)
      {
         for (int x = minX; x <= maxX; x += 2)
         {
            for (int py = 0; py < tplH; ++py)
            {
               for (int px = 0; px < tplW; ++px)
               {
                  candPatch[py * tplW + px] = imgLevel2.data[(y + py) * imgLevel2.w + (x + px)];
               }
            }
            float ncc = ComputeNCC(templateL2, candPatch);
            if (ncc > bestNCC)
            {
               bestNCC = ncc;
               bestPos.x = (float)x + (float)tplW * 0.5f;
               bestPos.y = (float)y + (float)tplH * 0.5f;
            }
         }
      }

      outScore = std::max(0.0f, bestNCC);
      return bestPos;
   }
}

const std::vector<std::string>& MotionTrackNode::InitModeNames() { return kInitModeNames; }
const std::vector<std::string>& MotionTrackNode::MotionModelNames() { return kMotionModelNames; }

MotionTrackNode::MotionTrackNode()
{
   for (int i = 0; i < kOutputCount; i++)
   {
      mTaps[i].owner = this;
      mTaps[i].index = i;
   }
}

MotionTrackNode::~MotionTrackNode()
{
   CancelAnalysis();
}

const char* MotionTrackNode::OutputLabel(int index) const
{
   static const char* kNames[kOutputCount] = { "x", "y", "scale", "rot" };
   if (index < 0 || index >= kOutputCount)
      return "out";
   return kNames[index];
}

IModulator* MotionTrackNode::ModulatorOutput(int index)
{
   if (index < 0 || index >= kOutputCount)
      return nullptr;
   return &mTaps[index];
}

float MotionTrackNode::Value(int index) const
{
   switch (index)
   {
      case kOutX: return std::clamp(mCurrentSample.x + offsetX, 0.0f, 1.0f);
      case kOutY: return std::clamp(mCurrentSample.y + offsetY, 0.0f, 1.0f);
      case kOutScale: return std::clamp((mCurrentSample.scale - 0.2f) / (5.0f - 0.2f), 0.0f, 1.0f);
      case kOutRotation: return std::clamp((mCurrentSample.rotation + 180.0f) / 360.0f, 0.0f, 1.0f);
      default: return 0.0f;
   }
}

void MotionTrackNode::ClearTrack()
{
   std::lock_guard<std::mutex> lock(mTrackMutex);
   mTrack.clear();
   mTrackedFrames = 0;
   mLostFrames = 0;
   mAvgConfidence = 0.0f;
   mStatus = "track cleared";
   mIsStale = false;
}

void MotionTrackNode::SetTrack(const std::vector<TrackSample>& samples)
{
   std::lock_guard<std::mutex> lock(mTrackMutex);
   mTrack = samples;
   mTrackedFrames = (int)mTrack.size();
   mLostFrames = 0;
   float sumConf = 0.0f;
   for (const auto& s : mTrack)
   {
      if (s.lost) mLostFrames++;
      sumConf += s.confidence;
   }
   mAvgConfidence = mTrack.empty() ? 0.0f : sumConf / (float)mTrack.size();
   char buf[128];
   snprintf(buf, sizeof(buf), "tracked %d frames · %d lost · %.2f avg",
            mTrackedFrames, mLostFrames, mAvgConfidence);
   mStatus = buf;
   mIsStale = false;
}

bool MotionTrackNode::SampleAtTime(double seconds, TrackSample& out) const
{
   std::lock_guard<std::mutex> lock(mTrackMutex);
   if (mTrack.empty())
      return false;

   if (seconds <= mTrack.front().t)
   {
      out = mTrack.front();
      return true;
   }
   if (seconds >= mTrack.back().t)
   {
      out = mTrack.back();
      return true;
   }

   // Binary search for surrounding samples
   auto it = std::lower_bound(mTrack.begin(), mTrack.end(), seconds,
                              [](const TrackSample& s, double t) { return s.t < t; });
   if (it == mTrack.begin())
   {
      out = *it;
      return true;
   }

   const TrackSample& s1 = *it;
   const TrackSample& s0 = *(it - 1);

   double dt = s1.t - s0.t;
   float alpha = (dt > 1e-6) ? (float)((seconds - s0.t) / dt) : 0.0f;
   alpha = std::clamp(alpha, 0.0f, 1.0f);

   out.t = seconds;
   out.x = s0.x + (s1.x - s0.x) * alpha;
   out.y = s0.y + (s1.y - s0.y) * alpha;
   out.scale = s0.scale + (s1.scale - s0.scale) * alpha;
   out.rotation = s0.rotation + (s1.rotation - s0.rotation) * alpha;
   out.confidence = s0.confidence + (s1.confidence - s0.confidence) * alpha;
   out.lost = s0.lost || s1.lost;
   return true;
}

void MotionTrackNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mInput.IsConnected())
      mInput.Pull(frameId);

   double t = Transport::Instance().Seconds();
   TrackSample s;
   if (SampleAtTime(t, s))
      mCurrentSample = s;
}

void MotionTrackNode::StartAnalysis()
{
   if (mIsAnalyzing.load())
      return;

   std::string path;
   INode* cur = mInput.GetSource();
   while (cur != nullptr)
   {
      if (auto* videoSrc = dynamic_cast<VideoSourceNode*>(cur))
      {
         path = videoSrc->LoadedPath();
         break;
      }
      if (auto* rbg = dynamic_cast<RemoveBgNode*>(cur))
         cur = rbg->Input().GetSource();
      else if (INode* bypass = cur->BypassSource())
         cur = bypass;
      else
         break;
   }

   if (path.empty())
   {
      mStatus = "no video source connected";
      return;
   }

   if (mWorkerThread.joinable())
      mWorkerThread.join();

   mAbortWorker.store(false);
   mProgress.store(0.0f);
   mIsAnalyzing.store(true);
   mStatus = "starting analysis...";

   mWorkerThread = std::thread(&MotionTrackNode::WorkerThreadMain, this,
                               path,
                               (InitMode)initMode,
                               (MotionModel)motionModel,
                               searchScale,
                               featureCount,
                               minConfidence,
                               adapt,
                               sampleFps);
}

void MotionTrackNode::CancelAnalysis()
{
   mAbortWorker.store(true);
   if (mWorkerThread.joinable())
      mWorkerThread.join();
   mIsAnalyzing.store(false);
}

void MotionTrackNode::WorkerThreadMain(std::string videoPath,
                                      InitMode chosenInitMode,
                                      MotionModel chosenMotionModel,
                                      float chosenSearchScale,
                                      int chosenFeatureCount,
                                      float chosenMinConfidence,
                                      float chosenAdapt,
                                      float chosenSampleFps)
{
   std::string err;
   Platform::VideoHandle* handle = Platform::VideoOpen(videoPath, err);
   if (!handle)
   {
      mStatus = "failed to open video: " + err;
      mIsAnalyzing.store(false);
      return;
   }

   int width = Platform::VideoWidth(handle);
   int height = Platform::VideoHeight(handle);
   double duration = Platform::VideoDuration(handle);

   if (width <= 0 || height <= 0 || duration <= 0.0)
   {
      Platform::VideoClose(handle);
      mStatus = "invalid video dimensions/duration";
      mIsAnalyzing.store(false);
      return;
   }

   // 1. Fetch initial frame for initialization
   std::vector<unsigned char> framePixels;
   while (!Platform::VideoFrameAt(handle, 0.0, framePixels) && Platform::VideoDecodeIsCatchingUp(handle))
   {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (mAbortWorker.load())
      {
         Platform::VideoClose(handle);
         mIsAnalyzing.store(false);
         return;
      }
   }

   if (framePixels.empty())
   {
      Platform::VideoClose(handle);
      mStatus = "failed to read first frame";
      mIsAnalyzing.store(false);
      return;
   }

   // Platform::VideoOpen only knows the track's declared "natural size";
   // decoding the first frame can reveal a different actual pixel-buffer
   // size (rotation metadata, clean aperture, non-square pixels). Re-read
   // the dimensions now that a frame has actually been decoded (VideoFrameAt
   // updates them to match) and confirm framePixels really holds width *
   // height * 4 bytes before indexing into it anywhere below - every frame
   // buffer this function touches (framePixels, f1, f2, mask) is indexed
   // with these numbers and a mismatch here was reading past the end of the
   // buffer on some clips (crashed on Analyze).
   width = Platform::VideoWidth(handle);
   height = Platform::VideoHeight(handle);
   if (width <= 0 || height <= 0 || framePixels.size() < (size_t)width * (size_t)height * 4)
   {
      Platform::VideoClose(handle);
      mStatus = "unexpected video frame size";
      mIsAnalyzing.store(false);
      return;
   }

   // 2. Initialize tracking bounding box. Center-frame default, used only if
   // auto-detectors fail.
   float initBoxX = 0.5f;
   float initBoxY = 0.5f;
   float initBoxW = 0.25f;
   float initBoxH = 0.35f;

   Platform::MattingMode matMode = (chosenInitMode == kAutoPerson) ? Platform::MattingMode::Person : Platform::MattingMode::Subject;

   if (chosenInitMode == kAutoPerson || chosenInitMode == kAutoSubject)
   {
      mStatus = "detecting " + std::string(chosenInitMode == kAutoPerson ? "person" : "subject") + " (" + Platform::MattingBackend() + ")...";
      std::vector<unsigned char> mask;
      std::string maskErr;
      bool ok = Platform::SubjectMask(framePixels, width, height, matMode, mask, maskErr);
      if (!ok && chosenInitMode == kAutoSubject)
      {
         // Fallback to Person mode if Subject mode unavailable
         matMode = Platform::MattingMode::Person;
         ok = Platform::SubjectMask(framePixels, width, height, matMode, mask, maskErr);
      }

      if (ok && mask.size() >= (size_t)width * (size_t)height)
      {
         int minX = width, maxX = 0, minY = height, maxY = 0;
         int64_t sumX = 0, sumY = 0;
         int maskCount = 0;
         for (int y = 0; y < height; ++y)
         {
            for (int x = 0; x < width; ++x)
            {
               if (mask[y * width + x] > 128)
               {
                  maskCount++;
                  sumX += x;
                  sumY += y;
                  if (x < minX) minX = x;
                  if (x > maxX) maxX = x;
                  if (y < minY) minY = y;
                  if (y > maxY) maxY = y;
               }
            }
         }
         if (maskCount > 50 && maxX > minX && maxY > minY)
         {
            initBoxX = (float)sumX / ((float)maskCount * (float)width);
            initBoxY = (float)sumY / ((float)maskCount * (float)height);
            initBoxW = std::clamp(((float)(maxX - minX)) / (float)width * 1.15f, 0.08f, 0.85f);
            initBoxH = std::clamp(((float)(maxY - minY)) / (float)height * 1.15f, 0.08f, 0.85f);
         }
         else
         {
            chosenInitMode = kAutoMotion;
         }
      }
      else
      {
         chosenInitMode = kAutoMotion;
      }
   }

   if (chosenInitMode == kAutoMotion)
   {
      mStatus = "detecting motion...";
      // 3-frame temporal difference
      std::vector<unsigned char> f1, f2;
      double dt = 1.0 / std::max(1.0f, chosenSampleFps);
      while (!Platform::VideoFrameAt(handle, dt, f1) && Platform::VideoDecodeIsCatchingUp(handle))
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      while (!Platform::VideoFrameAt(handle, dt * 2.0, f2) && Platform::VideoDecodeIsCatchingUp(handle))
         std::this_thread::sleep_for(std::chrono::milliseconds(1));

      const size_t frameBytes = (size_t)width * (size_t)height * 4;
      if (f1.size() >= frameBytes && f2.size() >= frameBytes)
      {
         int minX = width, maxX = 0, minY = height, maxY = 0;
         int diffCount = 0;
         for (int i = 0; i < width * height; ++i)
         {
            int d0 = std::abs((int)framePixels[i * 4] - (int)f1[i * 4]);
            int d1 = std::abs((int)f1[i * 4] - (int)f2[i * 4]);
            if (d0 > 25 && d1 > 25)
            {
               int x = i % width;
               int y = i / width;
               diffCount++;
               if (x < minX) minX = x;
               if (x > maxX) maxX = x;
               if (y < minY) minY = y;
               if (y > maxY) maxY = y;
            }
         }
         if (diffCount > 50 && maxX > minX && maxY > minY)
         {
            initBoxX = ((float)minX + (float)maxX) * 0.5f / (float)width;
            initBoxY = ((float)minY + (float)maxY) * 0.5f / (float)height;
            initBoxW = std::clamp(((float)(maxX - minX)) / (float)width * 1.15f, 0.08f, 0.85f);
            initBoxH = std::clamp(((float)(maxY - minY)) / (float)height * 1.15f, 0.08f, 0.85f);
         }
      }
   }

   mTrackBoxW = initBoxW;
   mTrackBoxH = initBoxH;

   // 3. Setup tracker state
   Pyramid prevPyr, currPyr;
   prevPyr.Build(framePixels, width, height);

   // Extract initial template patch
   int tplW = 48, tplH = 48;
   std::vector<float> refTemplate;
   SamplePatch(prevPyr.levels[0], initBoxX * (float)width, initBoxY * (float)height,
               initBoxW * (float)width, initBoxH * (float)height,
               tplW, tplH, 0.0f, refTemplate);

   int tplL2W = 16, tplL2H = 16;
   std::vector<float> refTemplateL2;
   SamplePatch(prevPyr.levels[2], initBoxX * (float)prevPyr.levels[2].w, initBoxY * (float)prevPyr.levels[2].h,
               initBoxW * (float)prevPyr.levels[2].w, initBoxH * (float)prevPyr.levels[2].h,
               tplL2W, tplL2H, 0.0f, refTemplateL2);

   float currBoxX = initBoxX;
   float currBoxY = initBoxY;
   float currScale = 1.0f;
   float currRotation = 0.0f;
   float velX = 0.0f, velY = 0.0f;

   std::vector<TrackSample> results;
   results.push_back({ 0.0, currBoxX, currBoxY, currScale, currRotation, 1.0f, false });

   double step = 1.0 / std::max(1.0f, chosenSampleFps);
   double t = step;
   int totalSteps = (int)std::ceil(duration / step);
   int currentStep = 1;

   int lostCount = 0;
   int lostStreak = 0;

   mStatus = "tracking...";

   while (t <= duration && !mAbortWorker.load())
   {
      std::vector<unsigned char> currPixels;
      while (!Platform::VideoFrameAt(handle, t, currPixels) && Platform::VideoDecodeIsCatchingUp(handle))
      {
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
         if (mAbortWorker.load())
            break;
      }
      if (currPixels.empty() || mAbortWorker.load())
         break;

      currPyr.Build(currPixels, width, height);

      // Stage 1: Corner Detection inside current bounding box
      float curW = initBoxW * currScale * (float)width;
      float curH = initBoxH * currScale * (float)height;
      float boxPxMinX = currBoxX * (float)width - curW * 0.5f;
      float boxPxMaxX = currBoxX * (float)width + curW * 0.5f;
      float boxPxMinY = currBoxY * (float)height - curH * 0.5f;
      float boxPxMaxY = currBoxY * (float)height + curH * 0.5f;

      std::vector<Point2f> corners = DetectCorners(prevPyr.levels[0], boxPxMinX, boxPxMinY, boxPxMaxX, boxPxMaxY, chosenFeatureCount);

      // Stage 2: Forward-Backward Verified Lucas-Kanade Optical Flow
      std::vector<Point2f> trackedCorners;
      std::vector<bool> status;
      TrackPointsKLT(prevPyr, currPyr, corners, trackedCorners, status);

      std::vector<Point2f> inliersPrev, inliersCurr;
      for (size_t i = 0; i < corners.size(); ++i)
      {
         if (status[i])
         {
            inliersPrev.push_back(corners[i]);
            inliersCurr.push_back(trackedCorners[i]);
         }
      }

      float fitDx = velX;
      float fitDy = velY;
      float fitScale = currScale;
      float fitRot = currRotation;

      if (inliersPrev.size() >= 2)
      {
         std::vector<float> dxs(inliersPrev.size()), dys(inliersPrev.size());
         for (size_t i = 0; i < inliersPrev.size(); ++i)
         {
            dxs[i] = (inliersCurr[i].x - inliersPrev[i].x) / (float)width;
            dys[i] = (inliersCurr[i].y - inliersPrev[i].y) / (float)height;
         }
         std::sort(dxs.begin(), dxs.end());
         std::sort(dys.begin(), dys.end());
         float medDx = dxs[dxs.size() / 2];
         float medDy = dys[dys.size() / 2];

         // Median residual rejection
         std::vector<float> residuals(inliersPrev.size());
         for (size_t i = 0; i < inliersPrev.size(); ++i)
         {
            float rx = dxs[i] - medDx;
            float ry = dys[i] - medDy;
            residuals[i] = std::sqrt(rx * rx + ry * ry);
         }
         std::vector<float> sortedRes = residuals;
         std::sort(sortedRes.begin(), sortedRes.end());
         float medRes = sortedRes[sortedRes.size() / 2];
         float maxRes = std::max(0.015f, 2.5f * medRes);

         float sumDx = 0.0f, sumDy = 0.0f;
         int inlierCount = 0;
         std::vector<Point2f> cleanPrev, cleanCurr;
         for (size_t i = 0; i < inliersPrev.size(); ++i)
         {
            if (residuals[i] <= maxRes)
            {
               sumDx += dxs[i];
               sumDy += dys[i];
               cleanPrev.push_back(inliersPrev[i]);
               cleanCurr.push_back(inliersCurr[i]);
               inlierCount++;
            }
         }

         if (inlierCount > 0)
         {
            fitDx = sumDx / (float)inlierCount;
            fitDy = sumDy / (float)inlierCount;
         }

         // Fit Scale
         if (chosenMotionModel >= kPositionScale && cleanPrev.size() >= 3)
         {
            std::vector<float> scaleRatios;
            for (size_t i = 0; i < cleanPrev.size(); ++i)
            {
               for (size_t j = i + 1; j < cleanPrev.size(); ++j)
               {
                  float dPrev = std::hypot(cleanPrev[i].x - cleanPrev[j].x, cleanPrev[i].y - cleanPrev[j].y);
                  float dCurr = std::hypot(cleanCurr[i].x - cleanCurr[j].x, cleanCurr[i].y - cleanCurr[j].y);
                  if (dPrev > 6.0f)
                     scaleRatios.push_back(dCurr / dPrev);
               }
            }
            if (!scaleRatios.empty())
            {
               std::sort(scaleRatios.begin(), scaleRatios.end());
               float medScaleRatio = scaleRatios[scaleRatios.size() / 2];
               fitScale = std::clamp(currScale * medScaleRatio, 0.2f, 5.0f);
            }
         }

         // Fit Rotation
         if (chosenMotionModel == kPositionScaleRotation && cleanPrev.size() >= 3)
         {
            std::vector<float> angleDiffs;
            for (size_t i = 0; i < cleanPrev.size(); ++i)
            {
               for (size_t j = i + 1; j < cleanPrev.size(); ++j)
               {
                  float aPrev = std::atan2(cleanPrev[j].y - cleanPrev[i].y, cleanPrev[j].x - cleanPrev[i].x);
                  float aCurr = std::atan2(cleanCurr[j].y - cleanCurr[i].y, cleanCurr[j].x - cleanCurr[i].x);
                  float da = (aCurr - aPrev) * (180.0f / 3.141592653589793f);
                  while (da > 180.0f) da -= 360.0f;
                  while (da < -180.0f) da += 360.0f;
                  angleDiffs.push_back(da);
               }
            }
            if (!angleDiffs.empty())
            {
               std::sort(angleDiffs.begin(), angleDiffs.end());
               float medAngleDiff = angleDiffs[angleDiffs.size() / 2];
               fitRot += medAngleDiff;
               while (fitRot > 180.0f) fitRot -= 360.0f;
               while (fitRot < -180.0f) fitRot += 360.0f;
            }
         }
      }

      float flowX = std::clamp(currBoxX + fitDx, 0.0f, 1.0f);
      float flowY = std::clamp(currBoxY + fitDy, 0.0f, 1.0f);

      // Stage 3: Detection-by-Tracking Anchor
      // Periodically (every 5 frames) or when optical flow inliers drop,
      // run Platform::SubjectMask to re-anchor directly on the subject's true centroid.
      bool isAnchorStep = (chosenInitMode == kAutoPerson || chosenInitMode == kAutoSubject) &&
                          ((currentStep % 5 == 0) || inliersPrev.size() < 4);
      bool anchored = false;

      if (isAnchorStep)
      {
         std::vector<unsigned char> mask;
         std::string maskErr;
         if (Platform::SubjectMask(currPixels, width, height, matMode, mask, maskErr) &&
             mask.size() >= (size_t)width * (size_t)height)
         {
            // Search in window around predicted optical flow position
            int winW = (int)(initBoxW * fitScale * (float)width * 1.5f);
            int winH = (int)(initBoxH * fitScale * (float)height * 1.5f);
            int winMinX = std::max(0, (int)(flowX * (float)width) - winW);
            int winMaxX = std::min(width - 1, (int)(flowX * (float)width) + winW);
            int winMinY = std::max(0, (int)(flowY * (float)height) - winH);
            int winMaxY = std::min(height - 1, (int)(flowY * (float)height) + winH);

            int cMinX = width, cMaxX = 0, cMinY = height, cMaxY = 0;
            int64_t cSumX = 0, cSumY = 0;
            int cCount = 0;

            for (int y = winMinY; y <= winMaxY; ++y)
            {
               for (int x = winMinX; x <= winMaxX; ++x)
               {
                  if (mask[y * width + x] > 128)
                  {
                     cCount++;
                     cSumX += x;
                     cSumY += y;
                     if (x < cMinX) cMinX = x;
                     if (x > cMaxX) cMaxX = x;
                     if (y < cMinY) cMinY = y;
                     if (y > cMaxY) cMaxY = y;
                  }
               }
            }

            // Fallback to full frame search if window didn't capture the subject
            if (cCount < 50)
            {
               cMinX = width; cMaxX = 0; cMinY = height; cMaxY = 0;
               cSumX = 0; cSumY = 0; cCount = 0;
               for (int y = 0; y < height; ++y)
               {
                  for (int x = 0; x < width; ++x)
                  {
                     if (mask[y * width + x] > 128)
                     {
                        cCount++;
                        cSumX += x;
                        cSumY += y;
                        if (x < cMinX) cMinX = x;
                        if (x > cMaxX) cMaxX = x;
                        if (y < cMinY) cMinY = y;
                        if (y > cMaxY) cMaxY = y;
                     }
                  }
               }
            }

            if (cCount > 50 && cMaxX > cMinX && cMaxY > cMinY)
            {
               float detX = (float)cSumX / ((float)cCount * (float)width);
               float detY = (float)cSumY / ((float)cCount * (float)height);
               float detW = std::clamp(((float)(cMaxX - cMinX)) / (float)width * 1.15f, 0.08f, 0.85f);
               float detH = std::clamp(((float)(cMaxY - cMinY)) / (float)height * 1.15f, 0.08f, 0.85f);

               // Fuse optical flow with detected anchor
               float blendWeight = (inliersPrev.size() >= 5) ? 0.5f : 0.85f;
               currBoxX = flowX * (1.0f - blendWeight) + detX * blendWeight;
               currBoxY = flowY * (1.0f - blendWeight) + detY * blendWeight;
               initBoxW = initBoxW * 0.85f + detW * 0.15f;
               initBoxH = initBoxH * 0.85f + detH * 0.15f;
               mTrackBoxW = initBoxW;
               mTrackBoxH = initBoxH;
               anchored = true;
            }
         }
      }

      if (!anchored)
      {
         currBoxX = flowX;
         currBoxY = flowY;
         currScale = fitScale;
         currRotation = fitRot;
      }

      // Stage 4: Confidence scoring
      float inlierRatio = (float)inliersPrev.size() / (float)std::max(1, (int)corners.size());
      float confidence = anchored ? 0.95f : std::clamp(0.45f + 0.55f * inlierRatio, 0.1f, 1.0f);
      bool lost = (inliersPrev.empty() && !anchored);

      if (!lost)
      {
         velX = fitDx;
         velY = fitDy;
         lostStreak = 0;
      }
      else
      {
         lostCount++;
         lostStreak++;
      }

      results.push_back({ t, currBoxX, currBoxY, currScale, currRotation, confidence, lost });

      prevPyr = currPyr;
      t += step;
      currentStep++;
      mProgress.store((float)currentStep / (float)std::max(1, totalSteps));
   }

   Platform::VideoClose(handle);

   if (!results.empty() && !mAbortWorker.load())
   {
      SetTrack(results);
   }

   mIsAnalyzing.store(false);
}

// ----------------------------------------------------------------------------
// Quantized Delta-encoding / Base64 serialization
// ----------------------------------------------------------------------------
std::string MotionTrackNode::EncodeTrack(const std::vector<TrackSample>& track)
{
   if (track.empty())
      return "";

   // Header: version "T1" + sample count uint32
   std::vector<unsigned char> bytes;
   bytes.push_back('T');
   bytes.push_back('1');

   uint32_t count = (uint32_t)track.size();
   bytes.push_back((count >> 0) & 0xFF);
   bytes.push_back((count >> 8) & 0xFF);
   bytes.push_back((count >> 16) & 0xFF);
   bytes.push_back((count >> 24) & 0xFF);

   int16_t prevX = 0, prevY = 0, prevScale = 0, prevRot = 0;

   for (const auto& s : track)
   {
      // Quantize
      uint32_t tMs = (uint32_t)(s.t * 1000.0);
      uint16_t qX = (uint16_t)std::clamp((int)(s.x * 65535.0f), 0, 65535);
      uint16_t qY = (uint16_t)std::clamp((int)(s.y * 65535.0f), 0, 65535);
      uint16_t qScale = (uint16_t)std::clamp((int)(s.scale * 6553.5f), 0, 65535);
      uint16_t qRot = (uint16_t)std::clamp((int)((s.rotation + 180.0f) * (65535.0f / 360.0f)), 0, 65535);
      uint8_t qConf = (uint8_t)std::clamp((int)(s.confidence * 255.0f), 0, 255);
      uint8_t qLost = s.lost ? 1 : 0;

      // Delta encode spatial fields
      int16_t deltaX = (int16_t)(qX - prevX);
      int16_t deltaY = (int16_t)(qY - prevY);
      int16_t deltaScale = (int16_t)(qScale - prevScale);
      int16_t deltaRot = (int16_t)(qRot - prevRot);

      prevX = qX;
      prevY = qY;
      prevScale = qScale;
      prevRot = qRot;

      // Append bytes
      bytes.push_back((tMs >> 0) & 0xFF);
      bytes.push_back((tMs >> 8) & 0xFF);
      bytes.push_back((tMs >> 16) & 0xFF);
      bytes.push_back((tMs >> 24) & 0xFF);

      bytes.push_back((deltaX >> 0) & 0xFF);
      bytes.push_back((deltaX >> 8) & 0xFF);

      bytes.push_back((deltaY >> 0) & 0xFF);
      bytes.push_back((deltaY >> 8) & 0xFF);

      bytes.push_back((deltaScale >> 0) & 0xFF);
      bytes.push_back((deltaScale >> 8) & 0xFF);

      bytes.push_back((deltaRot >> 0) & 0xFF);
      bytes.push_back((deltaRot >> 8) & 0xFF);

      bytes.push_back(qConf);
      bytes.push_back(qLost);
   }

   return Base64Encode(bytes.data(), bytes.size());
}

std::vector<MotionTrackNode::TrackSample> MotionTrackNode::DecodeTrack(const std::string& encoded)
{
   std::vector<TrackSample> track;
   if (encoded.empty())
      return track;

   std::vector<unsigned char> bytes = Base64Decode(encoded);
   if (bytes.size() < 6 || bytes[0] != 'T' || bytes[1] != '1')
      return track;

   uint32_t count = (uint32_t)bytes[2] |
                    ((uint32_t)bytes[3] << 8) |
                    ((uint32_t)bytes[4] << 16) |
                    ((uint32_t)bytes[5] << 24);

   const size_t kSampleBytes = 14;
   if (bytes.size() < 6 + count * kSampleBytes)
      return track;

   track.reserve(count);
   size_t offset = 6;
   int16_t runningX = 0, runningY = 0, runningScale = 0, runningRot = 0;

   for (uint32_t i = 0; i < count; ++i)
   {
      uint32_t tMs = (uint32_t)bytes[offset + 0] |
                     ((uint32_t)bytes[offset + 1] << 8) |
                     ((uint32_t)bytes[offset + 2] << 16) |
                     ((uint32_t)bytes[offset + 3] << 24);

      int16_t deltaX = (int16_t)((uint16_t)bytes[offset + 4] | ((uint16_t)bytes[offset + 5] << 8));
      int16_t deltaY = (int16_t)((uint16_t)bytes[offset + 6] | ((uint16_t)bytes[offset + 7] << 8));
      int16_t deltaScale = (int16_t)((uint16_t)bytes[offset + 8] | ((uint16_t)bytes[offset + 9] << 8));
      int16_t deltaRot = (int16_t)((uint16_t)bytes[offset + 10] | ((uint16_t)bytes[offset + 11] << 8));
      uint8_t qConf = bytes[offset + 12];
      uint8_t qLost = bytes[offset + 13];

      runningX += deltaX;
      runningY += deltaY;
      runningScale += deltaScale;
      runningRot += deltaRot;

      TrackSample s;
      s.t = (double)tMs / 1000.0;
      s.x = (float)(uint16_t)runningX / 65535.0f;
      s.y = (float)(uint16_t)runningY / 65535.0f;
      s.scale = (float)(uint16_t)runningScale / 6553.5f;
      s.rotation = ((float)(uint16_t)runningRot * (360.0f / 65535.0f)) - 180.0f;
      s.confidence = (float)qConf / 255.0f;
      s.lost = (qLost != 0);

      track.push_back(s);
      offset += kSampleBytes;
   }

   return track;
}

void MotionTrackNode::VisitParams(ParamVisitor& v)
{
   v.Int("initMode", initMode);
   if (initMode < 0 || initMode >= kInitModeCount)
      initMode = kAutoPerson;
   v.Int("motionModel", motionModel);
   v.Float("searchScale", searchScale);
   v.Int("featureCount", featureCount);
   v.Float("minConfidence", minConfidence);
   v.Float("adapt", adapt);
   v.Float("smooth", smooth);
   v.Float("sampleFps", sampleFps);
   v.Float("offsetX", offsetX);
   v.Float("offsetY", offsetY);
   v.Float("trackBoxW", mTrackBoxW);
   v.Float("trackBoxH", mTrackBoxH);

   std::string encoded = EncodeTrack(mTrack);
   v.Text("track", encoded);
   if (!encoded.empty())
   {
      std::vector<TrackSample> decoded = DecodeTrack(encoded);
      if (!decoded.empty())
         SetTrack(decoded);
   }
}
