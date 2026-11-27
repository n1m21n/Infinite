#include "ModelRegistry.h"

#include "core/Extensions.h"
#include "platform/AppPaths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#if defined(__APPLE__)
   #include <pthread/qos.h>
#elif defined(_WIN32)
   #include <windows.h>
#elif defined(__linux__)
   #include <sys/resource.h>
#endif

namespace Ml
{
   namespace
   {
      constexpr double kIdleUnloadSeconds = 60.0;

      double SteadyNow()
      {
         using namespace std::chrono;
         return duration<double>(steady_clock::now().time_since_epoch()).count();
      }

      bool FileExists(const std::string& path)
      {
         if (path.empty())
            return false;
         std::error_code ec;
         return std::filesystem::exists(AppPaths::FsPath(path), ec);
      }

      uint64_t FileSize(const std::string& path)
      {
         std::error_code ec;
         const auto sz = std::filesystem::file_size(AppPaths::FsPath(path), ec);
         return ec ? 0ull : static_cast<uint64_t>(sz);
      }

      // Locates libonnxruntime from the model's own pack first, then any known ML pack.
      std::string FindOrtLibrary(const std::string& preferredPack)
      {
         static const char* const kFallbackPacks[] = { "select", "ocr", "matte", "inpaint", "tracking" };
         auto tryPack = [](const std::string& packId) -> std::string {
            const std::string dir = Extensions::PackDir(packId);
            if (dir.empty())
               return {};
            const std::string lib = Tracking::OrtRuntime::PackLibraryPath(dir);
            return FileExists(lib) ? lib : std::string();
         };
         if (!preferredPack.empty())
         {
            std::string lib = tryPack(preferredPack);
            if (!lib.empty())
               return lib;
         }
         for (const char* p : kFallbackPacks)
         {
            std::string lib = tryPack(p);
            if (!lib.empty())
               return lib;
         }
         return {};
      }

      void SetWorkerLowPriority()
      {
#if defined(__APPLE__)
         pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(_WIN32)
         SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__linux__)
         setpriority(PRIO_PROCESS, 0, 10);
#endif
      }

      struct RegistryState
      {
         std::mutex specMu;
         std::vector<ModelSpec> specs;
         bool builtinsRegistered = false;

         std::mutex rtMu;
         Tracking::OrtRuntime rt;
         std::string activeFamily;
         std::map<std::string, Tracking::OrtModel*> sessions;
         std::map<std::string, std::string> verifiedSha; // path -> verified sha256
         std::function<void()> externalEvict;
         std::atomic<double> lastUse{ 0.0 };
      };

      RegistryState& R()
      {
         static RegistryState* s = new RegistryState(); // outlives static teardown
         return *s;
      }

      void RegisterBuiltinsLocked(RegistryState& s)
      {
         if (s.builtinsRegistered)
            return;
         s.builtinsRegistered = true;

         auto add = [&](ModelSpec m) {
            if (m.family.empty())
               m.family = m.id;
            s.specs.push_back(std::move(m));
         };

         // Block 1 existing models (registered so About Infinite lists their licences uniformly)
         add({ "sam21_tiny_enc", "SAM 2.1 Tiny (Encoder)", "2.1", "select", "models/sam_enc.onnx",
               "", 0, "Apache-2.0",
               "Meta SAM 2.1 Tiny (Apache-2.0). https://github.com/facebookresearch/sam2",
               { 1, 3, 1024, 1024 }, EpPref::CpuOnly, "sam" });
         add({ "sam21_tiny_dec", "SAM 2.1 Tiny (Decoder)", "2.1", "select", "models/sam_dec.onnx",
               "", 0, "Apache-2.0",
               "Meta SAM 2.1 Tiny (Apache-2.0). https://github.com/facebookresearch/sam2",
               {}, EpPref::CpuOnly, "sam" });
         add({ "depth_anything_v2_s", "Depth Anything V2 Small", "2.0", "select", "models/depth.onnx",
               "", 0, "Apache-2.0",
               "Depth Anything V2 Small (Apache-2.0). https://github.com/DepthAnything/Depth-Anything-V2",
               { 1, 3, 518, 518 }, EpPref::CpuOnly, "depth" });
         add({ "migan_places2", "MI-GAN 512 Inpaint", "0.1.0", "inpaint", "models/migan_pipeline_v2.onnx",
               "", 0, "MIT (Places2 research data)",
               "MI-GAN (Picsart AI Research, MIT). Trained on Places2 (academic research/education). Opt-in pack.",
               { 1, 3, 512, 512 }, EpPref::CpuOnly, "inpaint" });

         // Block 5A verified clean drop-in models (Phase 0 LICENCES.md + Phase 3 Python ONNX proofs)
         add({ "modnet", "MODNet Portrait Matte", "1.0.0", "select", "models/modnet.onnx",
               "25f165da9bfd30830a575f1f0490f1acd995975cb349bc02f3d79332e1fe5cf6", 12980000, "Apache-2.0",
               "MODNet (ZHKKKe, Apache-2.0 code & weights). Ke et al., AAAI 2022. https://github.com/ZHKKKe/MODNet",
               { 1, 3, 512, 512 }, EpPref::CpuOnly, "modnet" });
         add({ "ppocr_det", "PP-OCRv4 Mobile Detector", "4.0", "ocr", "models/ppocr_det.onnx",
               "d2a7720d45a54257208b1e13e36a8479894cb74155a5efe29462512d42f49da9", 4750000, "Apache-2.0",
               "PaddleOCR PP-OCRv4 Mobile / RapidOCR (Apache-2.0 code & weights). https://github.com/PaddlePaddle/PaddleOCR",
               { 1, 3, 640, 640 }, EpPref::CpuOnly, "ppocr" });
         add({ "ppocr_rec", "PP-OCRv4 Mobile Recognizer", "4.0", "ocr", "models/ppocr_rec.onnx",
               "48fc40f24f6d2a207a2b1091d3437eb3cc3eb6b676dc3ef9c37384005483683b", 10860000, "Apache-2.0",
               "PaddleOCR PP-OCRv4 Mobile / RapidOCR (Apache-2.0 code & weights). https://github.com/PaddlePaddle/PaddleOCR",
               { 1, 3, 48, 320 }, EpPref::CpuOnly, "ppocr" });
         add({ "dfine_s", "D-FINE-S Object Detector (COCO)", "1.0.1", "select", "models/dfine_s_coco.onnx",
               "900316a5c18fd176ff5bd08048768c892d051156d9b0fe0bf692e753331e8ed1", 31630000, "Apache-2.0",
               "D-FINE-S COCO-only mixed FP16/FP32 checkpoint (Peterande, Apache-2.0, MS COCO CC-BY-4.0). https://github.com/Peterande/D-FINE",
               { 1, 3, 640, 640 }, EpPref::CpuOnly, "dfine" });
         add({ "p3m_net", "P3M-Net Hair Matte", "1.0.0", "matte", "models/p3m_net.onnx",
               "", 0, "MIT",
               "P3M-Net & P3M-10k (MIT). Li et al., Privacy-Preserving Portrait Matting, ACM MM 2021. https://github.com/JizhiziLi/P3M",
               { 1, 3, 512, 512 }, EpPref::CpuOnly, "p3m" });
         add({ "span_2x_clean", "Clean SPAN 2x Upscaler", "1.0.0", "upscale", "models/span_2x_clean.onnx",
               "5dafe91d3466726d03a2c07c5c8020fc77d66e806428eaa88941ceb6e9fef509", 1618174, "Apache-2.0",
               "Clean-room SPAN 2x (Apache-2.0), trained on PD12M CC0 + Infinite headless renders + LPIPS (BSD-2).",
               { 1, 3, 256, 256 }, EpPref::CpuOnly, "span" });
      }

      void CloseAllSessionsLocked(RegistryState& s)
      {
         for (auto& kv : s.sessions)
            s.rt.CloseModel(kv.second);
         s.sessions.clear();
         s.activeFamily.clear();
      }
   } // namespace

   namespace ModelRegistry
   {
      void Register(ModelSpec spec)
      {
         RegistryState& s = R();
         std::lock_guard<std::mutex> lk(s.specMu);
         RegisterBuiltinsLocked(s);
         if (spec.family.empty())
            spec.family = spec.id;
         for (auto& existing : s.specs)
         {
            if (existing.id == spec.id)
            {
               existing = std::move(spec);
               return;
            }
         }
         s.specs.push_back(std::move(spec));
      }

      const ModelSpec* Find(const std::string& id)
      {
         RegistryState& s = R();
         std::lock_guard<std::mutex> lk(s.specMu);
         RegisterBuiltinsLocked(s);
         for (const auto& m : s.specs)
            if (m.id == id)
               return &m;
         return nullptr;
      }

      std::vector<ModelSpec> List()
      {
         RegistryState& s = R();
         std::lock_guard<std::mutex> lk(s.specMu);
         RegisterBuiltinsLocked(s);
         return s.specs;
      }

      std::string ModelPath(const std::string& id)
      {
         const ModelSpec* spec = Find(id);
         if (!spec)
            return {};
         const std::string dir = Extensions::PackDir(spec->pack);
         if (dir.empty())
            return {};
         return dir + "/" + spec->file;
      }

      std::string Missing(const std::string& id)
      {
         const ModelSpec* spec = Find(id);
         if (!spec)
            return "Unknown ML model: " + id;
         const std::string dir = Extensions::PackDir(spec->pack);
         if (dir.empty())
            return "Install the " + spec->pack + " pack in Settings > Extensions";
         const std::string path = dir + "/" + spec->file;
         if (!FileExists(path))
            return "The " + spec->pack + " pack is missing " + spec->name + "; update or reinstall it";
         if (FindOrtLibrary(spec->pack).empty())
            return "No ONNX Runtime library found for this platform";
         return {};
      }

      bool Available(const std::string& id)
      {
         return Missing(id).empty();
      }

      std::vector<InstalledModel> Installed()
      {
         std::vector<InstalledModel> out;
         for (const ModelSpec& spec : List())
         {
            const std::string dir = Extensions::PackDir(spec.pack);
            if (dir.empty())
               continue;
            const std::string path = dir + "/" + spec.file;
            if (!FileExists(path))
               continue;
            InstalledModel im;
            im.spec = spec;
            im.fullPath = path;
            im.diskBytes = FileSize(path);
            out.push_back(std::move(im));
         }
         return out;
      }

      bool VerifyFileSha256(const std::string& path, const std::string& expectedHex, std::string& err)
      {
         if (expectedHex.empty())
            return true;
         std::ifstream in(AppPaths::FsPath(path), std::ios::binary);
         if (!in)
         {
            err = "cannot read model file: " + path;
            return false;
         }
         std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
         const std::string actual = Extensions::Sha256Hex(bytes.data(), bytes.size());
         std::string exp = expectedHex;
         for (char& c : exp)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
         if (actual != exp)
         {
            err = "model checksum mismatch (expected " + exp.substr(0, 12) + "..., got " + actual.substr(0, 12) + "...)";
            return false;
         }
         return true;
      }

      bool WithModel(const std::string& id,
                     const std::function<bool(Tracking::OrtRuntime&, Tracking::OrtModel*, std::string&)>& fn,
                     std::string& err)
      {
         const ModelSpec* specPtr = Find(id);
         if (!specPtr)
         {
            err = "unknown model: " + id;
            return false;
         }
         const ModelSpec spec = *specPtr;
         const std::string why = Missing(id);
         if (!why.empty())
         {
            err = why;
            return false;
         }
         const std::string path = Extensions::PackDir(spec.pack) + "/" + spec.file;
         const std::string libPath = FindOrtLibrary(spec.pack);

         RegistryState& s = R();
         std::lock_guard<std::mutex> lk(s.rtMu);
         s.lastUse.store(SteadyNow(), std::memory_order_relaxed);

         if (!s.rt.Loaded() && !s.rt.Load(libPath, err))
            return false;

         // Enforce strictly one model family resident at a time across both ModelRegistry and AiSelect.
         if (s.activeFamily != spec.family)
         {
            CloseAllSessionsLocked(s);
            if (s.externalEvict)
               s.externalEvict();
            s.activeFamily = spec.family;
         }

         Tracking::OrtModel* model = nullptr;
         auto it = s.sessions.find(spec.id);
         if (it != s.sessions.end())
         {
            model = it->second;
         }
         else
         {
            if (!spec.sha256.empty())
            {
               auto vit = s.verifiedSha.find(path);
               if (vit == s.verifiedSha.end() || vit->second != spec.sha256)
               {
                  if (!VerifyFileSha256(path, spec.sha256, err))
                     return false;
                  s.verifiedSha[path] = spec.sha256;
               }
            }
            const bool preferGpu = (spec.epPrefs == EpPref::PreferCoreMl);
            bool usedGpu = false;
            model = s.rt.OpenModel(path, preferGpu, usedGpu, err);
            if (!model)
               return false;
            s.sessions[spec.id] = model;
         }

         const bool ok = fn(s.rt, model, err);
         s.lastUse.store(SteadyNow(), std::memory_order_relaxed);
         return ok;
      }

      void SetExternalEvict(std::function<void()> fn)
      {
         RegistryState& s = R();
         std::lock_guard<std::mutex> lk(s.rtMu);
         s.externalEvict = std::move(fn);
      }

      void EvictLocal()
      {
         RegistryState& s = R();
         std::lock_guard<std::mutex> lk(s.rtMu);
         CloseAllSessionsLocked(s);
      }

      void Tick(double nowSeconds)
      {
         (void)nowSeconds;
         RegistryState& s = R();
         std::unique_lock<std::mutex> lk(s.rtMu, std::try_to_lock);
         if (!lk.owns_lock() || s.sessions.empty())
            return;
         if (SteadyNow() - s.lastUse.load(std::memory_order_relaxed) < kIdleUnloadSeconds)
            return;
         CloseAllSessionsLocked(s);
      }

      void Unload()
      {
         EvictLocal();
      }

      std::string ActiveFamily()
      {
         RegistryState& s = R();
         std::lock_guard<std::mutex> lk(s.rtMu);
         return s.activeFamily;
      }
   } // namespace ModelRegistry

   // ---- LatestWorker ----
   struct LatestWorker::Impl
   {
      std::mutex mu;
      std::condition_variable cv;
      std::function<void(uint64_t)> queued;
      uint64_t queuedSerial = 0;
      std::atomic<uint64_t> latestSerial{ 0 };
      std::atomic<uint64_t> completedSerial{ 0 };
      std::atomic<bool> running{ false };
      bool quit = false;
      std::thread thread;

      void Run()
      {
         SetWorkerLowPriority();
         for (;;)
         {
            std::function<void(uint64_t)> fn;
            uint64_t serial = 0;
            {
               std::unique_lock<std::mutex> lk(mu);
               cv.wait(lk, [&] { return quit || queued != nullptr; });
               if (quit)
                  return;
               fn = std::move(queued);
               serial = queuedSerial;
               queued = nullptr;
               running.store(true, std::memory_order_release);
            }
            fn(serial);
            completedSerial.store(serial, std::memory_order_release);
            running.store(false, std::memory_order_release);
         }
      }
   };

   LatestWorker::LatestWorker() : m_impl(new Impl()) {}

   LatestWorker::~LatestWorker()
   {
      if (!m_impl)
         return;
      {
         std::lock_guard<std::mutex> lk(m_impl->mu);
         m_impl->quit = true;
         m_impl->queued = nullptr;
      }
      m_impl->cv.notify_one();
      if (m_impl->thread.joinable())
         m_impl->thread.join();
      delete m_impl;
   }

   uint64_t LatestWorker::Submit(std::function<void(uint64_t)> fn)
   {
      std::lock_guard<std::mutex> lk(m_impl->mu);
      if (!m_impl->thread.joinable())
         m_impl->thread = std::thread([this] { m_impl->Run(); });
      const uint64_t s = m_impl->latestSerial.fetch_add(1, std::memory_order_acq_rel) + 1;
      m_impl->queued = std::move(fn);
      m_impl->queuedSerial = s;
      m_impl->cv.notify_one();
      return s;
   }

   bool LatestWorker::Busy() const
   {
      std::lock_guard<std::mutex> lk(m_impl->mu);
      return m_impl->running.load(std::memory_order_acquire) || m_impl->queued != nullptr;
   }

   uint64_t LatestWorker::LatestSerial() const
   {
      return m_impl->latestSerial.load(std::memory_order_acquire);
   }

   uint64_t LatestWorker::CompletedSerial() const
   {
      return m_impl->completedSerial.load(std::memory_order_acquire);
   }
} // namespace Ml
