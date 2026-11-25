#include "AiSelect.h"

#include "core/Extensions.h"
#include "core/tracking/OrtRuntime.h"
#include "platform/AppPaths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

namespace AiSelect
{
   namespace
   {
      constexpr int kSide = 1024;          // SAM 2.1 encoder input
      constexpr int kCacheSize = 2;        // embeddings kept (about 17 MB each)
      constexpr double kIdleUnload = 60.0; // seconds
      const float kMean[3] = { 0.485f, 0.456f, 0.406f };
      const float kStd[3] = { 0.229f, 0.224f, 0.225f };

      struct Embedding
      {
         uint64_t key = 0;
         int w = 0, h = 0;
         Tracking::OrtRuntime::Tensor e[3];
      };

      struct State
      {
         std::mutex m;                    // guards everything below except the worker's own locals
         std::condition_variable cv;
         Tracking::OrtRuntime rt;
         Tracking::OrtModel* enc = nullptr;
         Tracking::OrtModel* dec = nullptr;
         std::vector<std::shared_ptr<Embedding>> cache;
         std::shared_ptr<const Picture> queued;
         uint64_t queuedKey = 0;
         bool running = false;
         bool quit = false;
         std::string lastError;
         std::thread worker;
         std::atomic<double> lastUse{ 0.0 };
      };

      State& S()
      {
         static State* s = new State();   // never destroyed: the worker may outlive static teardown
         return *s;
      }

      double Now()
      {
         using namespace std::chrono;
         return duration<double>(steady_clock::now().time_since_epoch()).count();
      }

      std::string ModelDir()
      {
         const std::string dir = Extensions::PackDir("select");
         return dir.empty() ? std::string() : dir + "/models";
      }

      bool Exists(const std::string& p)
      {
         std::error_code ec;
         return std::filesystem::exists(AppPaths::FsPath(p), ec);
      }

      // Caller holds S().m.
      bool EnsureModels(std::string& err)
      {
         State& s = S();
         if (s.enc && s.dec)
            return true;
         const std::string dir = Extensions::PackDir("select");
         if (dir.empty()) { err = "the Select pack is not installed"; return false; }
         if (!s.rt.Load(Tracking::OrtRuntime::PackLibraryPath(dir), err))
            return false;
         bool gpu = false;
         // CPU for both: Core ML spends minutes compiling these graphs on first load (measured 2026-11-25).
         if (!s.enc && !(s.enc = s.rt.OpenModel(dir + "/models/sam_enc.onnx", false, gpu, err)))
            return false;
         if (!s.dec && !(s.dec = s.rt.OpenModel(dir + "/models/sam_dec.onnx", false, gpu, err)))
            return false;
         return true;
      }

      // Top-down RGBA -> 1 x 3 x 1024 x 1024, stretched (SAM 2 does not keep the aspect), ImageNet-normalised.
      void Preprocess(const Picture& p, std::vector<float>& chw)
      {
         chw.assign((size_t)3 * kSide * kSide, 0.0f);
         const size_t plane = (size_t)kSide * kSide;
         for (int y = 0; y < kSide; y++)
         {
            const float sy = std::clamp((y + 0.5f) * p.h / kSide - 0.5f, 0.0f, (float)(p.h - 1));
            const int y0 = (int)sy, y1 = std::min(y0 + 1, p.h - 1);
            const float fy = sy - y0;
            for (int x = 0; x < kSide; x++)
            {
               const float sx = std::clamp((x + 0.5f) * p.w / kSide - 0.5f, 0.0f, (float)(p.w - 1));
               const int x0 = (int)sx, x1 = std::min(x0 + 1, p.w - 1);
               const float fx = sx - x0;
               const uint8_t* a = &p.rgba[((size_t)y0 * p.w + x0) * 4];
               const uint8_t* b = &p.rgba[((size_t)y0 * p.w + x1) * 4];
               const uint8_t* c = &p.rgba[((size_t)y1 * p.w + x0) * 4];
               const uint8_t* d = &p.rgba[((size_t)y1 * p.w + x1) * 4];
               for (int k = 0; k < 3; k++)
               {
                  const float top = a[k] + (b[k] - a[k]) * fx, bot = c[k] + (d[k] - c[k]) * fx;
                  chw[k * plane + (size_t)y * kSide + x] = ((top + (bot - top) * fy) / 255.0f - kMean[k]) / kStd[k];
               }
            }
         }
      }

      bool Encode(const Picture& p, uint64_t key, std::shared_ptr<Embedding>& out, std::string& err)
      {
         if (p.w < 2 || p.h < 2 || p.rgba.size() < (size_t)p.w * p.h * 4) { err = "empty picture"; return false; }
         std::vector<float> chw;
         Preprocess(p, chw);
         Tracking::OrtModel* enc = nullptr;
         {
            std::lock_guard<std::mutex> lk(S().m);
            if (!EnsureModels(err))
               return false;
            enc = S().enc;
         }
         Tracking::OrtRuntime::Input in;
         in.name = "pixel_values";
         in.shape = { 1, 3, kSide, kSide };
         in.f32 = chw.data();
         auto e = std::make_shared<Embedding>();
         std::vector<Tracking::OrtRuntime::Tensor> outs;
         if (!S().rt.RunNamed(enc, { in }, outs, err))
            return false;
         if (outs.size() < 3) { err = "the encoder gave " + std::to_string(outs.size()) + " outputs, not 3"; return false; }
         for (int i = 0; i < 3; i++)
            e->e[i] = std::move(outs[i]);
         e->key = key;
         e->w = p.w;
         e->h = p.h;
         out = e;
         return true;
      }

      void Store(std::shared_ptr<Embedding> e)
      {
         State& s = S();
         s.cache.erase(std::remove_if(s.cache.begin(), s.cache.end(), [&](const auto& c) { return c->key == e->key; }), s.cache.end());
         s.cache.insert(s.cache.begin(), std::move(e));
         if ((int)s.cache.size() > kCacheSize)
            s.cache.resize(kCacheSize);
      }

      void WorkerMain()
      {
         State& s = S();
         for (;;)
         {
            std::shared_ptr<const Picture> pic;
            uint64_t key = 0;
            {
               std::unique_lock<std::mutex> lk(s.m);
               s.cv.wait(lk, [&] { return s.quit || s.queued != nullptr; });
               if (s.quit)
                  return;
               pic = std::move(s.queued);
               key = s.queuedKey;
               s.queued.reset();
               s.running = true;
            }
            std::shared_ptr<Embedding> e;
            std::string err;
            const bool ok = Encode(*pic, key, e, err);
            std::lock_guard<std::mutex> lk(s.m);
            s.running = false;
            s.lastError = ok ? std::string() : err;
            if (ok)
               Store(std::move(e));
            s.lastUse = Now();
         }
      }

      std::shared_ptr<Embedding> Find(uint64_t key)
      {
         for (const auto& c : S().cache)
            if (c->key == key)
               return c;
         return nullptr;
      }
   }

   std::string Missing()
   {
      const std::string dir = Extensions::PackDir("select");
      if (dir.empty())
         return "Install the Select pack in Settings > Extensions for AI selection";
      if (!Exists(Tracking::OrtRuntime::PackLibraryPath(dir)))
         return "The Select pack has no runtime for this system";
      if (!Exists(dir + "/models/sam_enc.onnx") || !Exists(dir + "/models/sam_dec.onnx"))
         return "The Select pack is missing its models; reinstall it";
      return {};
   }

   bool Available() { return Missing().empty(); }

   uint64_t KeyOf(const Picture& p)
   {
      uint64_t h = 1469598103934665603ull;
      auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
      mix((uint64_t)p.w);
      mix((uint64_t)p.h);
      // Every byte of a sparse lattice plus whole rows every 64: fast and enough to tell edits apart.
      const size_t n = p.rgba.size();
      for (size_t i = 0; i < n; i += 61)
         mix(p.rgba[i]);
      for (int y = 0; y < p.h; y += 64)
         for (size_t i = (size_t)y * p.w * 4, e = i + (size_t)p.w * 4; i < e && i < n; i++)
            mix(p.rgba[i]);
      return h;
   }

   void Request(std::shared_ptr<const Picture> pic, uint64_t key)
   {
      State& s = S();
      std::lock_guard<std::mutex> lk(s.m);
      s.lastUse = Now();
      if (Find(key) || (s.queued && s.queuedKey == key))
         return;
      if (!s.worker.joinable())
         s.worker = std::thread(WorkerMain);
      s.queued = std::move(pic);
      s.queuedKey = key;
      s.cv.notify_one();
   }

   bool Ready(uint64_t key)
   {
      std::lock_guard<std::mutex> lk(S().m);
      return Find(key) != nullptr;
   }

   bool Busy()
   {
      std::lock_guard<std::mutex> lk(S().m);
      return S().running || S().queued != nullptr;
   }

   std::string LastError()
   {
      std::lock_guard<std::mutex> lk(S().m);
      return S().lastError;
   }

   bool EncodeNow(const Picture& pic, uint64_t key, std::string& err)
   {
      std::shared_ptr<Embedding> e;
      if (!Encode(pic, key, e, err))
         return false;
      std::lock_guard<std::mutex> lk(S().m);
      Store(std::move(e));
      S().lastUse = Now();
      return true;
   }

   bool Decode(uint64_t key, const Prompt& prompt, std::vector<uint8_t>& out, std::string& err)
   {
      std::shared_ptr<Embedding> e;
      Tracking::OrtModel* dec = nullptr;
      {
         std::lock_guard<std::mutex> lk(S().m);
         e = Find(key);
         if (!e) { err = "the picture is not analysed yet"; return false; }
         if (!EnsureModels(err))
            return false;
         dec = S().dec;
         S().lastUse = Now();
      }
      const float sx = (float)kSide / e->w, sy = (float)kSide / e->h;
      const int np = (int)prompt.xy.size() / 2;
      std::vector<float> pts((size_t)std::max(np, 0) * 2);
      std::vector<int64_t> lab((size_t)std::max(np, 0));
      for (int i = 0; i < np; i++)
      {
         pts[(size_t)i * 2] = prompt.xy[(size_t)i * 2] * sx;
         pts[(size_t)i * 2 + 1] = prompt.xy[(size_t)i * 2 + 1] * sy;
         lab[(size_t)i] = i < (int)prompt.labels.size() ? prompt.labels[(size_t)i] : 1;
      }
      float box[4] = { prompt.box[0] * sx, prompt.box[1] * sy, prompt.box[2] * sx, prompt.box[3] * sy };
      if (np == 0 && !prompt.hasBox) { err = "no click or box"; return false; }

      using In = Tracking::OrtRuntime::Input;
      std::vector<In> ins(6);
      ins[0].name = "input_points";
      ins[0].shape = { 1, 1, np, 2 };
      ins[0].f32 = pts.data();
      ins[1].name = "input_labels";
      ins[1].shape = { 1, 1, np };
      ins[1].i64 = lab.data();
      ins[2].name = "input_boxes";
      ins[2].shape = { 1, prompt.hasBox ? 1 : 0, 4 };
      ins[2].f32 = box;
      for (int i = 0; i < 3; i++)
      {
         ins[3 + i].name = "image_embeddings." + std::to_string(i);
         ins[3 + i].shape = e->e[i].shape;
         ins[3 + i].f32 = e->e[i].data.data();
      }
      // A zero-sized tensor still needs a non-null buffer.
      static float zf = 0.0f;
      static int64_t zi = 0;
      if (np == 0) { ins[0].f32 = &zf; ins[1].i64 = &zi; }
      std::vector<Tracking::OrtRuntime::Tensor> outs;
      if (!S().rt.RunNamed(dec, ins, outs, err))
         return false;
      if (outs.size() < 2 || outs[1].shape.size() != 5) { err = "unexpected decoder output"; return false; }
      const auto& iou = outs[0].data;
      const auto& masks = outs[1];
      const int nm = (int)masks.shape[2], mh = (int)masks.shape[3], mw = (int)masks.shape[4];
      if (nm < 1 || mh < 1 || mw < 1) { err = "empty decoder output"; return false; }
      const size_t plane = (size_t)mh * mw;
      // Which of the three masks: a box names one thing, so the best-scored mask. A lone click is ambiguous
      // (part or whole); Photoshop's Object Selection picks the whole object, so the largest mask. On COCO
      // (ai_eval S1, 2026-11-25) largest scores IoU 0.75 against 0.50 for best-scored.
      int pick = 0;
      for (int k = 1; k < nm && k < (int)iou.size(); k++)
         if (iou[(size_t)k] > iou[(size_t)pick]) pick = k;
      if (!prompt.hasBox && np == 1)
      {
         size_t bestArea = 0;
         for (int k = 0; k < nm; k++)
         {
            size_t area = 0;
            for (size_t i = 0; i < plane; i++)
               area += masks.data[(size_t)k * plane + i] > 0.0f;
            if (area > bestArea) { bestArea = area; pick = k; }
         }
      }
      const float* lo = &masks.data[(size_t)pick * plane];
      out.assign((size_t)e->w * e->h, 0);
      for (int y = 0; y < e->h; y++)
      {
         const float fy = std::clamp((y + 0.5f) * mh / e->h - 0.5f, 0.0f, (float)(mh - 1));
         const int y0 = (int)fy, y1 = std::min(y0 + 1, mh - 1);
         const float ty = fy - y0;
         for (int x = 0; x < e->w; x++)
         {
            const float fx = std::clamp((x + 0.5f) * mw / e->w - 0.5f, 0.0f, (float)(mw - 1));
            const int x0 = (int)fx, x1 = std::min(x0 + 1, mw - 1);
            const float tx = fx - x0;
            const float a = lo[(size_t)y0 * mw + x0], b = lo[(size_t)y0 * mw + x1];
            const float c = lo[(size_t)y1 * mw + x0], d = lo[(size_t)y1 * mw + x1];
            const float v = (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
            // Logit to coverage: hard inside, a short ramp across zero for an anti-aliased edge.
            out[(size_t)y * e->w + x] = (uint8_t)std::lround(std::clamp(0.5f + v * 0.5f, 0.0f, 1.0f) * 255.0f);
         }
      }
      return true;
   }

   void Tick(double nowSeconds)
   {
      (void)nowSeconds;
      State& s = S();
      std::unique_lock<std::mutex> lk(s.m, std::try_to_lock);
      if (!lk.owns_lock() || s.running || s.queued || (!s.enc && !s.dec && s.cache.empty()))
         return;
      if (Now() - s.lastUse.load() < kIdleUnload)
         return;
      s.rt.CloseModel(s.enc);
      s.rt.CloseModel(s.dec);
      s.enc = s.dec = nullptr;
      s.cache.clear();
   }

   void Unload()
   {
      State& s = S();
      std::lock_guard<std::mutex> lk(s.m);
      if (s.running)
         return;   // the worker still uses the encoder; Tick frees it later
      s.queued.reset();
      s.rt.CloseModel(s.enc);
      s.rt.CloseModel(s.dec);
      s.enc = s.dec = nullptr;
      s.cache.clear();
   }
}
