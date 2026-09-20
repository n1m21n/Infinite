#include "MotionTrackNode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "GLUtil.h"
#include "Platform.h"
#include "Transport.h"
#include "nodes/VideoSourceNode.h"

namespace
{
   const std::vector<std::string> kInitModeNames = {
      "Auto (subject)",
      "Auto (motion)",
      "Manual box"
   };

   const std::vector<std::string> kMotionModelNames = {
      "Position",
      "Position + Scale",
      "Position + Scale + Rotation"
   };

   const std::vector<std::string> kOverlayStyleNames = {
      "Ring",
      "Box",
      "Crosshair",
      "Ring + trail"
   };

   const char* kOverlayFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uInput;\n"
      "uniform int uHasInput;\n"
      "uniform int uHasTrack;\n"
      "uniform int uShowOverlay;\n"
      "uniform int uStyle;\n"
      "uniform vec2 uCenter;\n"
      "uniform vec2 uBoxSize;\n"
      "uniform float uRotation;\n" // in radians
      "uniform vec3 uColor;\n"
      "uniform float uAlpha;\n"
      "uniform float uConfidence;\n"
      "uniform int uLost;\n"
      "uniform float uAspect;\n"
      "uniform int uTrailCount;\n"
      "uniform vec2 uTrail[32];\n"
      "\n"
      "vec2 rotate2d(vec2 p, float a) {\n"
      "   float s = sin(a), c = cos(a);\n"
      "   return vec2(p.x * c - p.y * s, p.x * s + p.y * c);\n"
      "}\n"
      "\n"
      "float sdBox(vec2 p, vec2 b) {\n"
      "   vec2 d = abs(p) - b;\n"
      "   return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   vec4 base = (uHasInput != 0) ? texture(uInput, vUv) : vec4(0.0, 0.0, 0.0, 1.0);\n"
      "   if (uShowOverlay == 0 || uHasTrack == 0) {\n"
      "      fragColor = base;\n"
      "      return;\n"
      "   }\n"
      "\n"
      "   // Aspect-corrected coordinate space around tracking center\n"
      "   vec2 aspectScale = vec2(max(1.0, uAspect), max(1.0, 1.0 / max(0.001, uAspect)));\n"
      "   vec2 diff = (vUv - uCenter) * aspectScale;\n"
      "   vec2 rotDiff = rotate2d(diff, -uRotation);\n"
      "   vec2 halfBox = uBoxSize * 0.5 * aspectScale;\n"
      "\n"
      "   vec3 drawColor = (uLost != 0) ? vec3(1.0, 0.25, 0.25) : uColor;\n"
      "   float overlayAlpha = 0.0;\n"
      "   float thickness = 0.003;\n"
      "\n"
      "   if (uStyle == 0 || uStyle == 3) { // Ring or Ring+trail\n"
      "      float radius = max(halfBox.x, halfBox.y) * 1.25;\n"
      "      float dist = abs(length(diff) - radius);\n"
      "      overlayAlpha = smoothstep(thickness + 0.0015, thickness - 0.0015, dist);\n"
      "\n"
      "      // Add center dot\n"
      "      float centerDot = smoothstep(0.005, 0.002, length(diff));\n"
      "      overlayAlpha = max(overlayAlpha, centerDot * 0.85);\n"
      "\n"
      "      // Add subtle cross tick marks\n"
      "      vec2 tick = abs(diff);\n"
      "      if ((tick.x < 0.0015 && abs(tick.y - radius) < radius * 0.25) ||\n"
      "          (tick.y < 0.0015 && abs(tick.x - radius) < radius * 0.25)) {\n"
      "         overlayAlpha = max(overlayAlpha, 0.9);\n"
      "      }\n"
      "\n"
      "      if (uStyle == 3) { // Trail dots\n"
      "         for (int i = 0; i < 32; i++) {\n"
      "            if (i >= uTrailCount) break;\n"
      "            vec2 tDiff = (vUv - uTrail[i]) * aspectScale;\n"
      "            float tDist = length(tDiff);\n"
      "            float tFade = 1.0 - (float(i) / float(max(1, uTrailCount)));\n"
      "            float tAlpha = smoothstep(0.004, 0.001, tDist) * tFade * 0.75;\n"
      "            overlayAlpha = max(overlayAlpha, tAlpha);\n"
      "         }\n"
      "      }\n"
      "   } else if (uStyle == 1) { // Box\n"
      "      float d = sdBox(rotDiff, halfBox);\n"
      "      float outline = smoothstep(thickness + 0.0015, thickness - 0.0015, abs(d));\n"
      "      overlayAlpha = outline;\n"
      "\n"
      "      // Corner brackets accent\n"
      "      vec2 cornerDist = abs(rotDiff) - halfBox;\n"
      "      if (max(cornerDist.x, cornerDist.y) < 0.015 && min(cornerDist.x, cornerDist.y) > -halfBox.x * 0.35) {\n"
      "         overlayAlpha = max(overlayAlpha, 0.95);\n"
      "      }\n"
      "   } else if (uStyle == 2) { // Crosshair\n"
      "      float radius = max(halfBox.x, halfBox.y) * 1.1;\n"
      "      float ringDist = abs(length(diff) - radius);\n"
      "      float ringAlpha = smoothstep(thickness + 0.0015, thickness - 0.0015, ringDist) * 0.5;\n"
      "\n"
      "      vec2 lineDist = abs(rotDiff);\n"
      "      float hLine = (lineDist.y < thickness && abs(rotDiff.x) < radius * 1.3) ? 1.0 : 0.0;\n"
      "      float vLine = (lineDist.x < thickness && abs(rotDiff.y) < radius * 1.3) ? 1.0 : 0.0;\n"
      "      // Center cutout\n"
      "      if (length(diff) < radius * 0.25) {\n"
      "         hLine = 0.0; vLine = 0.0;\n"
      "      }\n"
      "      overlayAlpha = max(ringAlpha, max(hLine, vLine));\n"
      "      float centerDot = smoothstep(0.004, 0.0015, length(diff));\n"
      "      overlayAlpha = max(overlayAlpha, centerDot);\n"
      "   }\n"
      "\n"
      "   overlayAlpha *= uAlpha * clamp(uConfidence * 1.25, 0.25, 1.0);\n"
      "   vec3 blended = mix(base.rgb, drawColor, overlayAlpha);\n"
      "   fragColor = vec4(blended, base.a);\n"
      "}\n";

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
      if (img.w <= 4 || img.h <= 4 || maxCorners <= 0)
         return corners;

      int x0 = std::max(2, (int)boxMinX);
      int y0 = std::max(2, (int)boxMinY);
      int x1 = std::min(img.w - 3, (int)boxMaxX);
      int y1 = std::min(img.h - 3, (int)boxMaxY);
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

   // Pyramidal Lucas-Kanade optical flow
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

      for (size_t i = 0; i < n; ++i)
      {
         Point2f pt = prevPts[i];
         Point2f guess = pt;

         bool validPoint = true;
         // Iterate from coarse to fine pyramid levels
         for (int l = Pyramid::kLevels - 2; l >= 0; --l)
         {
            const GrayImage& I = prevPyr.levels[l];
            const GrayImage& J = currPyr.levels[l];
            float scale = 1.0f / (float)(1 << l);

            Point2f pL = { pt.x * scale, pt.y * scale };
            Point2f gL = { guess.x * scale, guess.y * scale };

            // Spatial gradients G matrix on image I
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

            // 4 LK iterations
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
             guess.x >= 0.0f && guess.x < (float)currPyr.levels[0].w &&
             guess.y >= 0.0f && guess.y < (float)currPyr.levels[0].h)
         {
            outCurrPts[i] = guess;
            outStatus[i] = true;
         }
         else
         {
            outStatus[i] = false;
         }
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
const std::vector<std::string>& MotionTrackNode::OverlayStyleNames() { return kOverlayStyleNames; }

MotionTrackNode::MotionTrackNode()
{
   for (int i = 0; i < kOutputCount; ++i)
   {
      mTaps[i].owner = this;
      mTaps[i].outputIndex = i;
   }
   mCurrentValues[kScale] = 1.0f;
   mCurrentValues[kRotation] = 0.5f; // 0 degrees
   mCurrentValues[kConfidence] = 1.0f;
}

MotionTrackNode::~MotionTrackNode()
{
   CancelAnalysis();
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

const char* MotionTrackNode::OutputLabel(int index) const
{
   static const char* kLabels[] = {
      "image", "x", "y", "scale", "rotation", "confidence"
   };
   return (index >= 0 && index < kOutputCount) ? kLabels[index] : "out";
}

IModulator* MotionTrackNode::ModulatorOutput(int index)
{
   // Output 0 is the image output (GetOutputTexture), outputs 1..5 are modulators
   if (index >= 1 && index < kOutputCount)
      return &mTaps[index];
   return nullptr;
}

float MotionTrackNode::Value(int outputIndex) const
{
   if (outputIndex >= 0 && outputIndex < kOutputCount)
      return mCurrentValues[outputIndex];
   return 0.0f;
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

   unsigned int srcTex = mInput.Pull(frameId);
   int inW = mInput.Width();
   int inH = mInput.Height();

   // Retrieve time from transport
   double t = Transport::Instance().Seconds();
   TrackSample s;
   if (SampleAtTime(t, s))
   {
      mCurrentSample = s;
      mCurrentValues[kX] = std::clamp(s.x + offsetX, 0.0f, 1.0f);
      mCurrentValues[kY] = std::clamp(s.y + offsetY, 0.0f, 1.0f);
      mCurrentValues[kScale] = std::clamp(s.scale, 0.0f, 10.0f);
      mCurrentValues[kRotation] = std::clamp((s.rotation + 180.0f) / 360.0f, 0.0f, 1.0f);
      mCurrentValues[kConfidence] = std::clamp(s.confidence, 0.0f, 1.0f);

      // Record trail
      if (mTrailCount == 0 ||
          std::abs(mTrailX[mTrailHead] - s.x) > 0.002f ||
          std::abs(mTrailY[mTrailHead] - s.y) > 0.002f)
      {
         mTrailHead = (mTrailHead + 1) % kTrailLength;
         mTrailX[mTrailHead] = s.x;
         mTrailY[mTrailHead] = s.y;
         if (mTrailCount < kTrailLength)
            mTrailCount++;
      }
   }

   if (srcTex == 0 || inW <= 0 || inH <= 0)
   {
      inW = 1280;
      inH = 720;
   }

   if (!GLUtil::EnsureFbo(mOut, inW, inH))
      return;
   if (!EnsureShader())
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex, inW, inH]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uInput"), 0);
      glUniform1i(glGetUniformLocation(mProgram, "uHasInput"), srcTex != 0 ? 1 : 0);
      glUniform1i(glGetUniformLocation(mProgram, "uHasTrack"), HasTrack() ? 1 : 0);
      glUniform1i(glGetUniformLocation(mProgram, "uShowOverlay"), showOverlay ? 1 : 0);
      glUniform1i(glGetUniformLocation(mProgram, "uStyle"), overlayStyle);

      float drawX = std::clamp(mCurrentSample.x + offsetX, 0.0f, 1.0f);
      float drawY = std::clamp(mCurrentSample.y + offsetY, 0.0f, 1.0f);
      glUniform2f(glGetUniformLocation(mProgram, "uCenter"), drawX, drawY);

      float baseBoxW = manualBoxW * mCurrentSample.scale * overlaySize;
      float baseBoxH = manualBoxH * mCurrentSample.scale * overlaySize;
      glUniform2f(glGetUniformLocation(mProgram, "uBoxSize"), baseBoxW, baseBoxH);

      float rotRad = mCurrentSample.rotation * (3.141592653589793f / 180.0f);
      glUniform1f(glGetUniformLocation(mProgram, "uRotation"), rotRad);

      glUniform3f(glGetUniformLocation(mProgram, "uColor"), overlayColor[0], overlayColor[1], overlayColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uAlpha"), 1.0f);
      glUniform1f(glGetUniformLocation(mProgram, "uConfidence"), mCurrentSample.confidence);
      glUniform1i(glGetUniformLocation(mProgram, "uLost"), mCurrentSample.lost ? 1 : 0);

      float aspect = (inH > 0) ? (float)inW / (float)inH : 1.0f;
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), aspect);

      // Pack trail
      glUniform1i(glGetUniformLocation(mProgram, "uTrailCount"), mTrailCount);
      float trailCoords[64] = { 0 };
      for (int i = 0; i < mTrailCount; ++i)
      {
         int idx = (mTrailHead - i + kTrailLength) % kTrailLength;
         trailCoords[i * 2 + 0] = mTrailX[idx];
         trailCoords[i * 2 + 1] = mTrailY[idx];
      }
      glUniform2fv(glGetUniformLocation(mProgram, "uTrail"), 32, trailCoords);
   });
}

bool MotionTrackNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kOverlayFrag);
   return mProgram != 0;
}

void MotionTrackNode::StartAnalysis()
{
   if (mIsAnalyzing.load())
      return;

   std::string path;
   if (INode* src = mInput.GetSource())
   {
      if (auto* videoSrc = dynamic_cast<VideoSourceNode*>(src))
         path = videoSrc->LoadedPath();
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
                               sampleFps,
                               manualBoxX, manualBoxY, manualBoxW, manualBoxH);
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
                                      float chosenSampleFps,
                                      float boxX, float boxY, float boxW, float boxH)
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

   // 2. Initialize tracking bounding box
   float initBoxX = boxX;
   float initBoxY = boxY;
   float initBoxW = std::max(0.05f, boxW);
   float initBoxH = std::max(0.05f, boxH);

   if (chosenInitMode == kAutoSubject)
   {
      mStatus = "detecting subject (" + Platform::MattingBackend() + ")...";
      std::vector<unsigned char> mask;
      std::string maskErr;
      if (Platform::SubjectMask(framePixels, width, height, Platform::MattingMode::Subject, mask, maskErr) &&
          !mask.empty())
      {
         // Find bounding box of mask > 128
         int minX = width, maxX = 0, minY = height, maxY = 0;
         int maskCount = 0;
         for (int y = 0; y < height; ++y)
         {
            for (int x = 0; x < width; ++x)
            {
               if (mask[y * width + x] > 128)
               {
                  maskCount++;
                  if (x < minX) minX = x;
                  if (x > maxX) maxX = x;
                  if (y < minY) minY = y;
                  if (y > maxY) maxY = y;
               }
            }
         }
         if (maskCount > 50 && maxX > minX && maxY > minY)
         {
            initBoxX = ((float)minX + (float)maxX) * 0.5f / (float)width;
            initBoxY = ((float)minY + (float)maxY) * 0.5f / (float)height;
            initBoxW = std::max(0.05f, ((float)(maxX - minX)) / (float)width);
            initBoxH = std::max(0.05f, ((float)(maxY - minY)) / (float)height);
         }
         else
         {
            chosenInitMode = kAutoMotion; // Fallback to motion
         }
      }
      else
      {
         chosenInitMode = kAutoMotion; // Fallback to motion
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

      if (!f1.empty() && !f2.empty())
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
            initBoxW = std::max(0.05f, ((float)(maxX - minX)) / (float)width);
            initBoxH = std::max(0.05f, ((float)(maxY - minY)) / (float)height);
         }
      }
   }

   // 3. Setup tracker state
   Pyramid prevPyr, currPyr;
   prevPyr.Build(framePixels, width, height);

   // Extract initial template patch
   int tplW = 48, tplH = 48;
   std::vector<float> refTemplate;
   SamplePatch(prevPyr.levels[0], initBoxX * (float)width, initBoxY * (float)height,
               initBoxW * (float)width, initBoxH * (float)height,
               tplW, tplH, 0.0f, refTemplate);

   // Level 2 template for coarse NCC
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

   int trackedCount = 1;
   int lostCount = 0;
   float sumConfidence = 1.0f;

   float activeSearchScale = chosenSearchScale;

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

      // Stage 1: Predict
      float predX = currBoxX + velX;
      float predY = currBoxY + velY;

      // Stage 2: Coarse NCC search on Level 2
      float searchRadX = activeSearchScale * initBoxW * currScale * (float)prevPyr.levels[2].w * 0.5f;
      float searchRadY = activeSearchScale * initBoxH * currScale * (float)prevPyr.levels[2].h * 0.5f;
      float coarseScore = 0.0f;
      Point2f coarsePosL2 = CoarseNCCSearch(currPyr.levels[2], refTemplateL2, tplL2W, tplL2H,
                                           predX * (float)prevPyr.levels[2].w,
                                           predY * (float)prevPyr.levels[2].h,
                                           searchRadX, searchRadY, coarseScore);
      Point2f coarsePos = {
         coarsePosL2.x * (float)(1 << 2) / (float)width,
         coarsePosL2.y * (float)(1 << 2) / (float)height
      };

      // Stage 3: Feature Detection & Pyramidal KLT refinement
      float boxPxMinX = (currBoxX - initBoxW * currScale * 0.5f) * (float)width;
      float boxPxMaxX = (currBoxX + initBoxW * currScale * 0.5f) * (float)width;
      float boxPxMinY = (currBoxY - initBoxH * currScale * 0.5f) * (float)height;
      float boxPxMaxY = (currBoxY + initBoxH * currScale * 0.5f) * (float)height;

      std::vector<Point2f> corners = DetectCorners(prevPyr.levels[0], boxPxMinX, boxPxMinY, boxPxMaxX, boxPxMaxY, chosenFeatureCount);
      std::vector<Point2f> trackedCorners;
      std::vector<bool> status;
      TrackPointsKLT(prevPyr, currPyr, corners, trackedCorners, status);

      // Stage 4: Fit Motion Model
      std::vector<Point2f> inliersPrev, inliersCurr;
      for (size_t i = 0; i < corners.size(); ++i)
      {
         if (status[i])
         {
            inliersPrev.push_back(corners[i]);
            inliersCurr.push_back(trackedCorners[i]);
         }
      }

      float fitDx = coarsePos.x - currBoxX;
      float fitDy = coarsePos.y - currBoxY;
      float fitScale = currScale;
      float fitRot = currRotation;

      if (inliersPrev.size() >= 3)
      {
         // Median translation
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
         float maxRes = std::max(0.01f, 2.0f * medRes);

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

         // Fit Scale if enabled
         if (chosenMotionModel >= kPositionScale && cleanPrev.size() >= 3)
         {
            std::vector<float> scaleRatios;
            for (size_t i = 0; i < cleanPrev.size(); ++i)
            {
               for (size_t j = i + 1; j < cleanPrev.size(); ++j)
               {
                  float dPrev = std::hypot(cleanPrev[i].x - cleanPrev[j].x, cleanPrev[i].y - cleanPrev[j].y);
                  float dCurr = std::hypot(cleanCurr[i].x - cleanCurr[j].x, cleanCurr[i].y - cleanCurr[j].y);
                  if (dPrev > 8.0f)
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

         // Fit Rotation if enabled
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

      float nextX = std::clamp(currBoxX + fitDx, 0.0f, 1.0f);
      float nextY = std::clamp(currBoxY + fitDy, 0.0f, 1.0f);

      // Stage 5: Score Confidence
      std::vector<float> currPatch;
      float rotRad = fitRot * (3.141592653589793f / 180.0f);
      SamplePatch(currPyr.levels[0], nextX * (float)width, nextY * (float)height,
                  initBoxW * fitScale * (float)width, initBoxH * fitScale * (float)height,
                  tplW, tplH, rotRad, currPatch);
      float confidence = std::max(0.0f, ComputeNCC(refTemplate, currPatch));

      // Stage 6 & 7: Adapt or Lost
      bool lost = confidence < chosenMinConfidence;
      if (!lost)
      {
         velX = nextX - currBoxX;
         velY = nextY - currBoxY;
         currBoxX = nextX;
         currBoxY = nextY;
         currScale = fitScale;
         currRotation = fitRot;
         activeSearchScale = chosenSearchScale;

         // Template adaptation
         if (chosenAdapt > 0.0f)
         {
            for (size_t i = 0; i < refTemplate.size(); ++i)
               refTemplate[i] = (1.0f - chosenAdapt) * refTemplate[i] + chosenAdapt * currPatch[i];
         }
      }
      else
      {
         lostCount++;
         // Coast on predicted velocity
         currBoxX = std::clamp(currBoxX + velX, 0.0f, 1.0f);
         currBoxY = std::clamp(currBoxY + velY, 0.0f, 1.0f);
         activeSearchScale = chosenSearchScale * 1.5f; // Widen search
      }

      trackedCount++;
      sumConfidence += confidence;

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
   v.Int("motionModel", motionModel);
   v.Float("searchScale", searchScale);
   v.Int("featureCount", featureCount);
   v.Float("minConfidence", minConfidence);
   v.Float("adapt", adapt);
   v.Float("smooth", smooth);
   v.Float("sampleFps", sampleFps);
   v.Float("offsetX", offsetX);
   v.Float("offsetY", offsetY);
   v.Bool("showOverlay", showOverlay);
   v.Int("overlayStyle", overlayStyle);
   v.Color("overlayColor", overlayColor);
   v.Float("overlaySize", overlaySize);
   v.Float("manualBoxX", manualBoxX);
   v.Float("manualBoxY", manualBoxY);
   v.Float("manualBoxW", manualBoxW);
   v.Float("manualBoxH", manualBoxH);

   std::string encoded = EncodeTrack(mTrack);
   v.Text("track", encoded);
   if (!encoded.empty())
   {
      std::vector<TrackSample> decoded = DecodeTrack(encoded);
      if (!decoded.empty())
         SetTrack(decoded);
   }
}
