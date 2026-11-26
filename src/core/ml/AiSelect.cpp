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
         Tracking::OrtModel* depth = nullptr;
         Tracking::OrtModel* inpaint = nullptr;
         std::shared_ptr<const Picture> depthQueued;
         uint64_t depthQueuedKey = 0, depthKey = 0;
         int depthW = 0, depthH = 0;
         std::vector<float> depthMap;
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
         if (s.depth)   // one model family resident at a time
         {
            s.rt.CloseModel(s.depth);
            s.depth = nullptr;
         }
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

      // Caller holds S().m.
      bool EnsureDepth(std::string& err)
      {
         State& s = S();
         if (s.depth)
            return true;
         const std::string dir = Extensions::PackDir("select");
         if (dir.empty()) { err = "the Select pack is not installed"; return false; }
         if (!s.rt.Load(Tracking::OrtRuntime::PackLibraryPath(dir), err))
            return false;
         s.rt.CloseModel(s.enc);   // one model family resident; cached embeddings stay (the decoder is small)
         s.enc = nullptr;
         bool gpu = false;
         s.depth = s.rt.OpenModel(dir + "/models/depth.onnx", false, gpu, err);
         return s.depth != nullptr;
      }

      // Depth Anything V2: the long side at 518, both sides whole multiples of 14, aspect kept, ImageNet-normalised.
      // Its output (relative inverse depth, larger nearer) is stretched to 0..1 and resized to the picture.
      bool RunDepth(const Picture& p, std::vector<float>& out, std::string& err)
      {
         if (p.w < 2 || p.h < 2 || p.rgba.size() < (size_t)p.w * p.h * 4) { err = "empty picture"; return false; }
         const float sc = 518.0f / std::max(p.w, p.h);
         const int W = std::max(14, (int)std::lround(p.w * sc / 14.0f) * 14), H = std::max(14, (int)std::lround(p.h * sc / 14.0f) * 14);
         std::vector<float> chw((size_t)3 * W * H);
         const size_t plane = (size_t)W * H;
         for (int y = 0; y < H; y++)
         {
            const float sy = std::clamp((y + 0.5f) * p.h / H - 0.5f, 0.0f, (float)(p.h - 1));
            const int y0 = (int)sy, y1 = std::min(y0 + 1, p.h - 1);
            const float fy = sy - y0;
            for (int x = 0; x < W; x++)
            {
               const float sx = std::clamp((x + 0.5f) * p.w / W - 0.5f, 0.0f, (float)(p.w - 1));
               const int x0 = (int)sx, x1 = std::min(x0 + 1, p.w - 1);
               const float fx = sx - x0;
               const uint8_t* a = &p.rgba[((size_t)y0 * p.w + x0) * 4];
               const uint8_t* b = &p.rgba[((size_t)y0 * p.w + x1) * 4];
               const uint8_t* c = &p.rgba[((size_t)y1 * p.w + x0) * 4];
               const uint8_t* d = &p.rgba[((size_t)y1 * p.w + x1) * 4];
               for (int k = 0; k < 3; k++)
               {
                  const float top = a[k] + (b[k] - a[k]) * fx, bot = c[k] + (d[k] - c[k]) * fx;
                  chw[k * plane + (size_t)y * W + x] = ((top + (bot - top) * fy) / 255.0f - kMean[k]) / kStd[k];
               }
            }
         }
         Tracking::OrtModel* m = nullptr;
         {
            std::lock_guard<std::mutex> lk(S().m);
            if (!EnsureDepth(err))
               return false;
            m = S().depth;
         }
         Tracking::OrtRuntime::Input in;
         in.name = "pixel_values";
         in.shape = { 1, 3, H, W };
         in.f32 = chw.data();
         std::vector<Tracking::OrtRuntime::Tensor> outs;
         if (!S().rt.RunNamed(m, { in }, outs, err))
            return false;
         if (outs.empty() || outs[0].shape.size() < 2) { err = "the depth model gave no map"; return false; }
         const int oh = (int)outs[0].shape[outs[0].shape.size() - 2], ow = (int)outs[0].shape.back();
         const std::vector<float>& d = outs[0].data;
         if (oh < 1 || ow < 1 || d.size() < (size_t)ow * oh) { err = "the depth map is the wrong size"; return false; }
         const auto [lo, hi] = std::minmax_element(d.begin(), d.begin() + (size_t)ow * oh);
         const float span = std::max(*hi - *lo, 1e-6f);
         out.assign((size_t)p.w * p.h, 0.0f);
         for (int y = 0; y < p.h; y++)
         {
            const float sy = std::clamp((y + 0.5f) * oh / p.h - 0.5f, 0.0f, (float)(oh - 1));
            const int y0 = (int)sy, y1 = std::min(y0 + 1, oh - 1);
            const float fy = sy - y0;
            for (int x = 0; x < p.w; x++)
            {
               const float sx = std::clamp((x + 0.5f) * ow / p.w - 0.5f, 0.0f, (float)(ow - 1));
               const int x0 = (int)sx, x1 = std::min(x0 + 1, ow - 1);
               const float fx = sx - x0;
               const float top = d[(size_t)y0 * ow + x0] + (d[(size_t)y0 * ow + x1] - d[(size_t)y0 * ow + x0]) * fx;
               const float bot = d[(size_t)y1 * ow + x0] + (d[(size_t)y1 * ow + x1] - d[(size_t)y1 * ow + x0]) * fx;
               out[(size_t)y * p.w + x] = (top + (bot - top) * fy - *lo) / span;
            }
         }
         return true;
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
               s.cv.wait(lk, [&] { return s.quit || s.queued != nullptr || s.depthQueued != nullptr; });
               if (s.quit)
                  return;
               if (s.queued == nullptr)
               {
                  // A depth job (the encoder's queue goes first: a click waits on it).
                  std::shared_ptr<const Picture> dp = std::move(s.depthQueued);
                  const uint64_t dk = s.depthQueuedKey;
                  s.depthQueued.reset();
                  s.running = true;
                  lk.unlock();
                  std::vector<float> map;
                  std::string err;
                  const bool ok = RunDepth(*dp, map, err);
                  lk.lock();
                  s.running = false;
                  s.lastError = ok ? std::string() : err;
                  if (ok)
                  {
                     s.depthMap.swap(map);
                     s.depthKey = dk;
                     s.depthW = dp->w;
                     s.depthH = dp->h;
                  }
                  s.lastUse = Now();
                  continue;
               }
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
      return S().running || S().queued != nullptr || S().depthQueued != nullptr;
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
      if (!lk.owns_lock() || s.running || s.queued || s.depthQueued || (!s.enc && !s.dec && !s.depth && !s.inpaint && s.cache.empty()))
         return;
      if (Now() - s.lastUse.load() < kIdleUnload)
         return;
      s.rt.CloseModel(s.enc);
      s.rt.CloseModel(s.dec);
      s.rt.CloseModel(s.depth);
      s.rt.CloseModel(s.inpaint);
      s.enc = s.dec = s.depth = s.inpaint = nullptr;
      s.cache.clear();
   }

   void Unload()
   {
      State& s = S();
      std::lock_guard<std::mutex> lk(s.m);
      if (s.running)
         return;   // the worker still uses the encoder; Tick frees it later
      s.queued.reset();
      s.depthQueued.reset();
      s.rt.CloseModel(s.enc);
      s.rt.CloseModel(s.dec);
      s.rt.CloseModel(s.depth);
      s.rt.CloseModel(s.inpaint);
      s.enc = s.dec = s.depth = s.inpaint = nullptr;
      s.cache.clear();
      s.depthMap.clear();
      s.depthKey = 0;
   }

   bool DepthAvailable()
   {
      const std::string dir = Extensions::PackDir("select");
      return !dir.empty() && Exists(Tracking::OrtRuntime::PackLibraryPath(dir)) && Exists(dir + "/models/depth.onnx");
   }

   void RequestDepth(std::shared_ptr<const Picture> pic, uint64_t key)
   {
      State& s = S();
      std::lock_guard<std::mutex> lk(s.m);
      s.lastUse = Now();
      if ((s.depthKey == key && !s.depthMap.empty()) || (s.depthQueued && s.depthQueuedKey == key))
         return;
      if (!s.worker.joinable())
         s.worker = std::thread(WorkerMain);
      s.depthQueued = std::move(pic);
      s.depthQueuedKey = key;
      s.cv.notify_one();
   }

   bool DepthReady(uint64_t key)
   {
      std::lock_guard<std::mutex> lk(S().m);
      return S().depthKey == key && !S().depthMap.empty();
   }

   bool Depth(uint64_t key, std::vector<float>& out, int& w, int& h)
   {
      State& s = S();
      std::lock_guard<std::mutex> lk(s.m);
      if (s.depthKey != key || s.depthMap.empty())
         return false;
      out = s.depthMap;
      w = s.depthW;
      h = s.depthH;
      s.lastUse = Now();
      return true;
   }

   bool DepthNow(const Picture& pic, uint64_t key, std::string& err)
   {
      std::vector<float> map;
      if (!RunDepth(pic, map, err))
         return false;
      State& s = S();
      std::lock_guard<std::mutex> lk(s.m);
      s.depthMap.swap(map);
      s.depthKey = key;
      s.depthW = pic.w;
      s.depthH = pic.h;
      s.lastUse = Now();
      return true;
   }

   // ---- Inpainting ----

   namespace
   {
      const char* kInpaintModel = "/models/migan_pipeline_v2.onnx";

      // The runtime: the inpaint pack's own, else the select pack's (one library per process either way).
      std::string InpaintRuntime()
      {
         const std::string own = Extensions::PackDir("inpaint");
         if (!own.empty() && Exists(Tracking::OrtRuntime::PackLibraryPath(own)))
            return Tracking::OrtRuntime::PackLibraryPath(own);
         const std::string sel = Extensions::PackDir("select");
         if (!sel.empty() && Exists(Tracking::OrtRuntime::PackLibraryPath(sel)))
            return Tracking::OrtRuntime::PackLibraryPath(sel);
         return std::string();
      }
   }

   std::string InpaintMissing()
   {
      const std::string dir = Extensions::PackDir("inpaint");
      if (dir.empty())
         return "Install the Inpaint pack in Settings > Extensions";
      if (!Exists(dir + kInpaintModel))
         return "The Inpaint pack is missing its model: reinstall it";
      if (InpaintRuntime().empty())
         return "The Inpaint pack is missing its runtime: reinstall it";
      return std::string();
   }

   bool InpaintAvailable() { return InpaintMissing().empty(); }

   bool InpaintNow(const std::vector<uint8_t>& rgba, const std::vector<uint8_t>& mask, int w, int h, std::vector<uint8_t>& out,
                   std::string& err)
   {
      if (w < 2 || h < 2 || rgba.size() < (size_t)w * h * 4 || mask.size() < (size_t)w * h)
      {
         err = "empty picture";
         return false;
      }
      const std::string why = InpaintMissing();
      if (!why.empty())
      {
         err = why;
         return false;
      }
      const size_t plane = (size_t)w * h;
      std::vector<uint8_t> chw(plane * 3), m1(plane);
      for (size_t i = 0; i < plane; i++)
      {
         for (int k = 0; k < 3; k++)
            chw[k * plane + i] = rgba[i * 4 + k];
         m1[i] = mask[i] > 127 ? 255 : 0;   // MI-GAN: 255 = the hole
      }
      State& s = S();
      std::lock_guard<std::mutex> lk(s.m);
      s.lastUse = Now();
      if (!s.inpaint)
      {
         if (!s.rt.Load(InpaintRuntime(), err))
            return false;
         bool gpu = false;
         if (!(s.inpaint = s.rt.OpenModel(Extensions::PackDir("inpaint") + kInpaintModel, false, gpu, err)))
            return false;
      }
      using In = Tracking::OrtRuntime::Input;
      std::vector<In> ins(2);
      ins[0].name = "image";
      ins[0].shape = { 1, 3, h, w };
      ins[0].u8 = chw.data();
      ins[1].name = "mask";
      ins[1].shape = { 1, 1, h, w };
      ins[1].u8 = m1.data();
      std::vector<Tracking::OrtRuntime::Tensor> outs;
      if (!s.rt.RunNamed(s.inpaint, ins, outs, err))
         return false;
      if (outs.empty() || outs[0].data.size() != plane * 3)
      {
         err = "unexpected inpaint output";
         return false;
      }
      out = rgba;
      out.resize(plane * 4);
      for (size_t i = 0; i < plane; i++)
         if (m1[i] != 0)
            for (int k = 0; k < 3; k++)
               out[i * 4 + k] = (uint8_t)std::clamp(outs[0].data[k * plane + i], 0.0f, 255.0f);
      return true;
   }
}
