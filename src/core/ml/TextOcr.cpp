#include "TextOcr.h"

#include "ModelRegistry.h"
#include "core/Extensions.h"
#include "platform/AppPaths.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>

namespace Ml
{
   namespace TextOcr
   {
      namespace
      {
         const float kMean[3] = { 0.485f, 0.456f, 0.406f };
         const float kStd[3] = { 0.229f, 0.224f, 0.225f };

         struct Box { int x0, y0, x1, y1; };

         const std::vector<std::string>& LoadKeys()
         {
            static std::mutex mu;
            static std::vector<std::string> keys;
            static std::string loadedPath;
            std::lock_guard<std::mutex> lk(mu);
            const std::string dir = Extensions::PackDir("ocr");
            const std::string path = dir.empty() ? std::string() : (dir + "/models/ppocr_keys_v1.txt");
            if (path == loadedPath && !keys.empty())
               return keys;
            keys.clear();
            loadedPath = path;
            if (path.empty())
               return keys;
            std::ifstream in(AppPaths::FsPath(path));
            std::string line;
            while (std::getline(in, line))
            {
               if (!line.empty() && line.back() == '\r')
                  line.pop_back();
               keys.push_back(std::move(line));
            }
            return keys;
         }

         std::vector<Box> ExtractBoxes(const float* prob, int w, int h, float thr, int minArea)
         {
            std::vector<uint8_t> vis(static_cast<size_t>(w) * h, 0);
            std::vector<Box> boxes;
            std::vector<std::pair<int, int>> stack;
            for (int y = 0; y < h; y++)
            {
               for (int x = 0; x < w; x++)
               {
                  const size_t idx = static_cast<size_t>(y) * w + x;
                  if (vis[idx] || prob[idx] <= thr)
                     continue;
                  vis[idx] = 1;
                  stack.clear();
                  stack.emplace_back(x, y);
                  int mnx = x, mxx = x, mny = y, mxy = y, area = 0;
                  while (!stack.empty())
                  {
                     auto [cx, cy] = stack.back();
                     stack.pop_back();
                     area++;
                     mnx = std::min(mnx, cx); mxx = std::max(mxx, cx);
                     mny = std::min(mny, cy); mxy = std::max(mxy, cy);
                     const int dx[4] = { -1, 1, 0, 0 };
                     const int dy[4] = { 0, 0, -1, 1 };
                     for (int d = 0; d < 4; d++)
                     {
                        const int nx = cx + dx[d], ny = cy + dy[d];
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h)
                        {
                           const size_t ni = static_cast<size_t>(ny) * w + nx;
                           if (!vis[ni] && prob[ni] > thr)
                           {
                              vis[ni] = 1;
                              stack.emplace_back(nx, ny);
                           }
                        }
                     }
                  }
                  if (area >= minArea)
                  {
                     const int bh = mxy - mny;
                     const int padX = std::max(4, static_cast<int>(bh * 0.25f));
                     const int padY = std::max(4, static_cast<int>(bh * 0.28f));
                     boxes.push_back({ std::max(0, mnx - padX), std::max(0, mny - padY),
                                       std::min(w - 1, mxx + padX), std::min(h - 1, mxy + padY) });
                  }
               }
            }
            return boxes;
         }

         std::string CtcDecode(const float* probs, int steps, int classes, const std::vector<std::string>& keys)
         {
            std::string out;
            int prev = 0;
            for (int t = 0; t < steps; t++)
            {
               const float* row = probs + static_cast<size_t>(t) * classes;
               int best = 0;
               float bestVal = row[0];
               for (int c = 1; c < classes; c++)
               {
                  if (row[c] > bestVal) { bestVal = row[c]; best = c; }
               }
               if (best != 0 && best != prev)
               {
                  const size_t ki = static_cast<size_t>(best - 1);
                  if (ki < keys.size())
                     out += keys[ki];
                  else if (ki == keys.size())
                     out.push_back(' ');
               }
               prev = best;
            }
            while (!out.empty() && out.front() == ' ') out.erase(out.begin());
            while (!out.empty() && out.back() == ' ') out.pop_back();
            return out;
         }
      } // namespace

      bool Available()
      {
         return ModelRegistry::Available("ppocr_det") && ModelRegistry::Available("ppocr_rec");
      }

      std::string Missing()
      {
         std::string m = ModelRegistry::Missing("ppocr_det");
         return m.empty() ? ModelRegistry::Missing("ppocr_rec") : m;
      }

      bool RunNow(const std::vector<uint8_t>& rgba, int w, int h,
                  std::vector<TextLine>& outLines, std::vector<uint8_t>& outMask,
                  std::string& err)
      {
         outLines.clear();
         outMask.assign(static_cast<size_t>(std::max(0, w)) * std::max(0, h), 0);
         if (w < 8 || h < 8 || rgba.size() < static_cast<size_t>(w) * h * 4)
         {
            err = "empty picture";
            return false;
         }

         // Resize long side <= 960, padded/rounded to multiple of 32 (PP-OCRv4 requirement).
         const float sc = std::min(1.0f, 960.0f / static_cast<float>(std::max(w, h)));
         const int dw = std::max(32, static_cast<int>(std::lround(w * sc / 32.0f)) * 32);
         const int dh = std::max(32, static_cast<int>(std::lround(h * sc / 32.0f)) * 32);
         const size_t dPlane = static_cast<size_t>(dw) * dh;
         std::vector<float> detIn(dPlane * 3, 0.0f);

         // BGR ImageNet-normalized NCHW
         for (int y = 0; y < dh; y++)
         {
            const float sy = std::clamp((y + 0.5f) * h / dh - 0.5f, 0.0f, static_cast<float>(h - 1));
            const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, h - 1);
            const float fy = sy - y0;
            for (int x = 0; x < dw; x++)
            {
               const float sx = std::clamp((x + 0.5f) * w / dw - 0.5f, 0.0f, static_cast<float>(w - 1));
               const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, w - 1);
               const float fx = sx - x0;
               const uint8_t* a = &rgba[(static_cast<size_t>(y0) * w + x0) * 4];
               const uint8_t* b = &rgba[(static_cast<size_t>(y0) * w + x1) * 4];
               const uint8_t* c = &rgba[(static_cast<size_t>(y1) * w + x0) * 4];
               const uint8_t* d = &rgba[(static_cast<size_t>(y1) * w + x1) * 4];
               for (int k = 0; k < 3; k++)
               {
                  const int srcC = 2 - k; // BGR
                  const float top = a[srcC] + (b[srcC] - a[srcC]) * fx;
                  const float bot = c[srcC] + (d[srcC] - c[srcC]) * fx;
                  const float v = (top + (bot - top) * fy) / 255.0f;
                  detIn[k * dPlane + static_cast<size_t>(y) * dw + x] = (v - kMean[k]) / kStd[k];
               }
            }
         }

         std::vector<Tracking::OrtRuntime::Tensor> detOuts;
         const bool detOk = ModelRegistry::WithModel("ppocr_det", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            Tracking::OrtRuntime::Input in;
            in.name = "x";
            in.shape = { 1, 3, dh, dw };
            in.f32 = detIn.data();
            return rt.RunNamed(m, { in }, detOuts, e);
         }, err);
         if (!detOk)
            return false;
         if (detOuts.empty() || detOuts[0].data.size() < dPlane)
         {
            err = "unexpected PP-OCR detector output";
            return false;
         }

         const float* pmap = detOuts[0].data.data();
         const std::vector<Box> detBoxes = ExtractBoxes(pmap, dw, dh, 0.30f, 30);

         // Upsample detector probability map into outMask (soft text selection mask)
         for (int y = 0; y < h; y++)
         {
            const int sy = std::clamp(y * dh / h, 0, dh - 1);
            for (int x = 0; x < w; x++)
            {
               const int sx = std::clamp(x * dw / w, 0, dw - 1);
               const float p = pmap[static_cast<size_t>(sy) * dw + sx];
               if (p > 0.25f)
                  outMask[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::lround(std::clamp((p - 0.25f) / 0.55f, 0.0f, 1.0f) * 255.0f));
            }
         }

         // Recognize each detected box (same "ppocr" family, so detector stays resident)
         const auto& keys = LoadKeys();
         const float sxToImg = static_cast<float>(w) / dw;
         const float syToImg = static_cast<float>(h) / dh;

         for (const Box& db : detBoxes)
         {
            TextLine tl;
            tl.x0 = std::clamp(static_cast<int>(std::floor(db.x0 * sxToImg)), 0, w - 1);
            tl.y0 = std::clamp(static_cast<int>(std::floor(db.y0 * syToImg)), 0, h - 1);
            tl.x1 = std::clamp(static_cast<int>(std::ceil(db.x1 * sxToImg)), 0, w - 1);
            tl.y1 = std::clamp(static_cast<int>(std::ceil(db.y1 * syToImg)), 0, h - 1);
            const int cw = tl.x1 - tl.x0, ch = tl.y1 - tl.y0;
            if (cw < 6 || ch < 6)
               continue;

            const int rh = 48;
            const int rw = std::clamp(static_cast<int>(std::lround(cw * (48.0f / ch))), 32, 320);
            const size_t rPlane = static_cast<size_t>(rw) * rh;
            std::vector<float> recIn(rPlane * 3, 0.0f);
            for (int y = 0; y < rh; y++)
            {
               const float sy = tl.y0 + std::clamp((y + 0.5f) * ch / rh - 0.5f, 0.0f, static_cast<float>(ch - 1));
               const int y0 = std::clamp(static_cast<int>(sy), 0, h - 1);
               const int y1 = std::min(y0 + 1, h - 1);
               const float fy = sy - y0;
               for (int x = 0; x < rw; x++)
               {
                  const float sx = tl.x0 + std::clamp((x + 0.5f) * cw / rw - 0.5f, 0.0f, static_cast<float>(cw - 1));
                  const int x0 = std::clamp(static_cast<int>(sx), 0, w - 1);
                  const int x1 = std::min(x0 + 1, w - 1);
                  const float fx = sx - x0;
                  const uint8_t* a = &rgba[(static_cast<size_t>(y0) * w + x0) * 4];
                  const uint8_t* b = &rgba[(static_cast<size_t>(y0) * w + x1) * 4];
                  const uint8_t* c = &rgba[(static_cast<size_t>(y1) * w + x0) * 4];
                  const uint8_t* d = &rgba[(static_cast<size_t>(y1) * w + x1) * 4];
                  for (int k = 0; k < 3; k++)
                  {
                     const int srcC = 2 - k;
                     const float top = a[srcC] + (b[srcC] - a[srcC]) * fx;
                     const float bot = c[srcC] + (d[srcC] - c[srcC]) * fx;
                     const float v = (top + (bot - top) * fy) / 255.0f;
                     recIn[k * rPlane + static_cast<size_t>(y) * rw + x] = (v - 0.5f) / 0.5f;
                  }
               }
            }

            std::vector<Tracking::OrtRuntime::Tensor> recOuts;
            std::string recErr;
            const bool recOk = ModelRegistry::WithModel("ppocr_rec", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
               Tracking::OrtRuntime::Input in;
               in.name = "x";
               in.shape = { 1, 3, rh, rw };
               in.f32 = recIn.data();
               return rt.RunNamed(m, { in }, recOuts, e);
            }, recErr);
            if (recOk && !recOuts.empty() && recOuts[0].shape.size() == 3)
            {
               const int steps = static_cast<int>(recOuts[0].shape[1]);
               const int classes = static_cast<int>(recOuts[0].shape[2]);
               tl.text = CtcDecode(recOuts[0].data.data(), steps, classes, keys);
            }
            outLines.push_back(std::move(tl));
         }

         std::sort(outLines.begin(), outLines.end(), [](const TextLine& a, const TextLine& b) {
            if (std::abs(a.y0 - b.y0) > 12) return a.y0 < b.y0;
            return a.x0 < b.x0;
         });
         return true;
      }

      void FallbackTextMask(const std::vector<uint8_t>& rgba, int w, int h,
                            std::vector<uint8_t>& outMask)
      {
         outMask.assign(static_cast<size_t>(std::max(0, w)) * std::max(0, h), 0);
         if (w < 4 || h < 4 || rgba.size() < static_cast<size_t>(w) * h * 4)
            return;
         // Compute vertical gradient (horizontal stroke edges are strong in text).
         std::vector<uint8_t> edge(static_cast<size_t>(w) * h, 0);
         auto lum = [&](int x, int y) -> float {
            const uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
         };
         for (int y = 1; y < h - 1; y++)
            for (int x = 1; x < w - 1; x++)
            {
               const float gx = std::abs(lum(x + 1, y) - lum(x - 1, y));
               const float gy = std::abs(lum(x, y + 1) - lum(x, y - 1));
               edge[static_cast<size_t>(y) * w + x] = (gx + gy > 60.0f) ? 255 : 0;
            }
         // Dilate horizontally by 4 px to join adjacent glyph strokes into text regions.
         for (int y = 1; y < h - 1; y++)
            for (int x = 0; x < w; x++)
            {
               uint8_t hit = 0;
               for (int dx = -4; dx <= 4 && !hit; dx++)
               {
                  const int nx = std::clamp(x + dx, 0, w - 1);
                  if (edge[static_cast<size_t>(y) * w + nx]) hit = 255;
               }
               outMask[static_cast<size_t>(y) * w + x] = hit;
            }
      }
   } // namespace TextOcr
} // namespace Ml
