#include "ModNet.h"

#include "ModelRegistry.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace Ml
{
   namespace ModNet
   {
      namespace
      {
         constexpr int kSide = 512;

         struct CacheState
         {
            std::mutex mu;
            uint64_t key = 0;
            int w = 0, h = 0;
            std::vector<uint8_t> matte;
            std::string lastError;
            LatestWorker worker;
         };

         CacheState& C()
         {
            static CacheState* c = new CacheState();
            return *c;
         }

         void Preprocess(const uint8_t* rgba, int w, int h, std::vector<float>& chw)
         {
            const size_t plane = static_cast<size_t>(kSide) * kSide;
            chw.assign(plane * 3, 0.0f);
            for (int y = 0; y < kSide; y++)
            {
               const float sy = std::clamp((y + 0.5f) * h / kSide - 0.5f, 0.0f, static_cast<float>(h - 1));
               const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, h - 1);
               const float fy = sy - y0;
               for (int x = 0; x < kSide; x++)
               {
                  const float sx = std::clamp((x + 0.5f) * w / kSide - 0.5f, 0.0f, static_cast<float>(w - 1));
                  const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, w - 1);
                  const float fx = sx - x0;
                  const uint8_t* a = &rgba[(static_cast<size_t>(y0) * w + x0) * 4];
                  const uint8_t* b = &rgba[(static_cast<size_t>(y0) * w + x1) * 4];
                  const uint8_t* c = &rgba[(static_cast<size_t>(y1) * w + x0) * 4];
                  const uint8_t* d = &rgba[(static_cast<size_t>(y1) * w + x1) * 4];
                  for (int k = 0; k < 3; k++)
                  {
                     const float top = a[k] + (b[k] - a[k]) * fx;
                     const float bot = c[k] + (d[k] - c[k]) * fx;
                     const float v = (top + (bot - top) * fy) / 255.0f;
                     chw[k * plane + static_cast<size_t>(y) * kSide + x] = (v - 0.5f) / 0.5f;
                  }
               }
            }
         }
      } // namespace

      bool Available() { return ModelRegistry::Available("modnet"); }
      std::string Missing() { return ModelRegistry::Missing("modnet"); }

      bool RunNow(const std::vector<uint8_t>& rgba, int w, int h,
                  std::vector<uint8_t>& outAlpha, std::string& err)
      {
         if (w < 2 || h < 2 || rgba.size() < static_cast<size_t>(w) * h * 4)
         {
            err = "empty picture";
            return false;
         }
         std::vector<float> chw;
         Preprocess(rgba.data(), w, h, chw);

         std::vector<Tracking::OrtRuntime::Tensor> outs;
         const bool ok = ModelRegistry::WithModel("modnet", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            Tracking::OrtRuntime::Input in;
            in.name = "input";
            in.shape = { 1, 3, kSide, kSide };
            in.f32 = chw.data();
            return rt.RunNamed(m, { in }, outs, e);
         }, err);
         if (!ok)
            return false;
         if (outs.empty() || outs[0].data.size() < static_cast<size_t>(kSide) * kSide)
         {
            err = "unexpected MODNet output";
            return false;
         }

         const float* matte = outs[0].data.data();
         outAlpha.assign(static_cast<size_t>(w) * h, 0);
         for (int y = 0; y < h; y++)
         {
            const float sy = std::clamp((y + 0.5f) * kSide / h - 0.5f, 0.0f, static_cast<float>(kSide - 1));
            const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, kSide - 1);
            const float fy = sy - y0;
            for (int x = 0; x < w; x++)
            {
               const float sx = std::clamp((x + 0.5f) * kSide / w - 0.5f, 0.0f, static_cast<float>(kSide - 1));
               const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, kSide - 1);
               const float fx = sx - x0;
               const float a = matte[static_cast<size_t>(y0) * kSide + x0];
               const float b = matte[static_cast<size_t>(y0) * kSide + x1];
               const float c = matte[static_cast<size_t>(y1) * kSide + x0];
               const float d = matte[static_cast<size_t>(y1) * kSide + x1];
               const float v = (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
               outAlpha[static_cast<size_t>(y) * w + x] =
                  static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            }
         }
         return true;
      }

      void Request(std::shared_ptr<const Picture> pic, uint64_t key)
      {
         if (!pic || !Available())
            return;
         CacheState& c = C();
         {
            std::lock_guard<std::mutex> lk(c.mu);
            if (c.key == key && !c.matte.empty())
               return;
         }
         c.worker.Submit([pic = std::move(pic), key](uint64_t) {
            std::vector<uint8_t> alpha;
            std::string err;
            const bool ok = RunNow(pic->rgba, pic->w, pic->h, alpha, err);
            CacheState& st = C();
            std::lock_guard<std::mutex> lk(st.mu);
            st.lastError = ok ? std::string() : err;
            if (ok)
            {
               st.matte.swap(alpha);
               st.key = key;
               st.w = pic->w;
               st.h = pic->h;
            }
         });
      }

      bool Ready(uint64_t key)
      {
         std::lock_guard<std::mutex> lk(C().mu);
         return C().key == key && !C().matte.empty();
      }

      bool Busy() { return C().worker.Busy(); }

      bool GetMatte(uint64_t key, std::vector<uint8_t>& outAlpha, int& w, int& h)
      {
         std::lock_guard<std::mutex> lk(C().mu);
         if (C().key != key || C().matte.empty())
            return false;
         outAlpha = C().matte;
         w = C().w;
         h = C().h;
         return true;
      }

      std::string LastError()
      {
         std::lock_guard<std::mutex> lk(C().mu);
         return C().lastError;
      }

      void ClearCache()
      {
         std::lock_guard<std::mutex> lk(C().mu);
         C().matte.clear();
         C().key = 0;
         C().w = C().h = 0;
      }
   } // namespace ModNet
} // namespace Ml
