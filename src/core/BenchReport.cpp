#include "BenchReport.h"

#include <cstring>

#include "gl3.h"
#include "../platform/Platform.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace Bench
{
   double ScopedStageTimer::NowMs()
   {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
   }

   GpuTimerRing::GpuTimerRing() = default;

   // Deliberately no GL calls here: the fixture's ring is a function-local
   // static, destroyed after the GL context is gone, and query objects die
   // with their context anyway.
   GpuTimerRing::~GpuTimerRing() = default;

   void GpuTimerRing::EnsureInitialized()
   {
      if (mInitialized)
         return;
      mInitialized = true;
      mSupported = false;

      // On macOS's Metal-backed GL, glEndQuery(GL_TIME_ELAPSED) flushes the
      // context and blocks until the GPU catches up, so timing stalls the
      // CPU and can inflate frame_ms. INFINITE_BENCH_GPUTIMERS=0 turns the
      // queries off (stages_gpu_ms comes out empty) for an unperturbed run.
      if (const char* env = std::getenv("INFINITE_BENCH_GPUTIMERS"))
      {
         if (std::strcmp(env, "0") == 0)
            return;
      }
      if (std::getenv("INFINITE_BENCH_B9SCENE") || std::getenv("INFINITE_BENCH_B9") || std::getenv("INFINITE_BENCH_B9MEMORY"))
      {
         return;
      }

#if !defined(__APPLE__)
      if (!glad_glGenQueries || !glad_glDeleteQueries || !glad_glBeginQuery ||
          !glad_glEndQuery || !glad_glGetQueryObjectuiv || !glad_glGetQueryObjectui64v)
      {
         return;
      }
#endif

      // Clear any pending GL error state
      for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}

      GLuint probe = 0;
      glGenQueries(1, &probe);
      if (glGetError() != GL_NO_ERROR || probe == 0)
         return;

      glBeginQuery(GL_TIME_ELAPSED, probe);
      GLenum errBegin = glGetError();
      glEndQuery(GL_TIME_ELAPSED);
      GLenum errEnd = glGetError();
      glDeleteQueries(1, &probe);

      if (errBegin == GL_NO_ERROR && errEnd == GL_NO_ERROR)
      {
         mSupported = true;
      }
   }

   bool GpuTimerRing::IsSupported()
   {
      EnsureInitialized();
      return mSupported;
   }

   bool GpuTimerRing::Harvest(FrameSlot& slot, PercentileRing& samples, bool wait)
   {
      if (!slot.inFlight)
         return true;
      if (slot.used > 0 && !wait)
      {
         // Queries complete in submission order: the last one ready means
         // all of them are.
         GLuint available = 0;
         glGetQueryObjectuiv(slot.queries[slot.used - 1], GL_QUERY_RESULT_AVAILABLE, &available);
         if (!available)
            return false;
      }
      GLuint64 totalNs = 0;
      for (int i = 0; i < slot.used; ++i)
      {
         GLuint64 ns = 0;
         glGetQueryObjectui64v(slot.queries[i], GL_QUERY_RESULT, &ns);
         totalNs += ns;
      }
      if (!slot.skipped && slot.used > 0)
         samples.Push((double)totalNs * 1e-6);
      slot.inFlight = false;
      slot.used = 0;
      slot.skipped = false;
      return true;
   }

   void GpuTimerRing::BeginStage(const std::string& stageName, int frameId)
   {
      EnsureInitialized();
      if (!mSupported || stageName.empty())
         return;

      // GL_TIME_ELAPSED queries cannot be nested
      if (!mCurrentActiveStage.empty())
         return;

      auto& ring = mStages[stageName];
      if (ring.active)
         return;

      const int fid = frameId >= 0 ? frameId : 0;
      auto& slot = ring.slots[fid % kRingDepth];

      if (slot.frameId != fid)
      {
         // First interval of this stage this frame. The entry still holds a
         // frame kRingDepth frames old; take its result without blocking, or
         // give up on timing this stage for the whole frame - a partial
         // per-frame total would read as a fast frame.
         if (!Harvest(slot, ring.samples, false))
         {
            slot.frameId = fid;
            slot.skipped = true;
            slot.inFlight = true; // keep the old queries until they land
            return;
         }
         slot.frameId = fid;
      }
      if (slot.skipped)
         return;

      if (slot.used == (int)slot.queries.size())
      {
         GLuint id = 0;
         glGenQueries(1, &id);
         if (id == 0)
            return;
         slot.queries.push_back(id);
      }

      glBeginQuery(GL_TIME_ELAPSED, slot.queries[slot.used]);
      slot.used++;
      slot.inFlight = true;
      ring.active = true;
      mCurrentActiveStage = stageName;
   }

   void GpuTimerRing::EndStage(const std::string& stageName)
   {
      if (!mSupported || stageName.empty())
         return;

      auto it = mStages.find(stageName);
      if (it == mStages.end() || !it->second.active)
         return;

      glEndQuery(GL_TIME_ELAPSED);
      it->second.active = false;
      if (mCurrentActiveStage == stageName)
         mCurrentActiveStage.clear();
   }

   void GpuTimerRing::Poll(int currentFrameId)
   {
      if (!mSupported)
         return;

      // Only frames before the current one, so the pipeline is never stalled
      // on work just submitted.
      for (auto& [name, ring] : mStages)
         for (auto& slot : ring.slots)
            if (slot.inFlight && slot.frameId < currentFrameId)
               Harvest(slot, ring.samples, false);
   }

   void GpuTimerRing::Finish()
   {
      if (!mSupported)
         return;

      if (!mCurrentActiveStage.empty())
         EndStage(mCurrentActiveStage);

      // Final pipeline drain at the end of the benchmark run
      glFinish();

      for (auto& [name, ring] : mStages)
         for (auto& slot : ring.slots)
            Harvest(slot, ring.samples, true);
   }

   void GpuTimerRing::Reset()
   {
      for (auto& [name, ring] : mStages)
      {
         for (auto& slot : ring.slots)
         {
            if (!slot.queries.empty())
               glDeleteQueries((GLsizei)slot.queries.size(), slot.queries.data());
            slot.queries.clear();
         }
      }
      mStages.clear();
      mCurrentActiveStage.clear();
      mInitialized = false;
      mSupported = false;
   }

   nlohmann::json GpuTimerRing::ToJsonP50() const
   {
      nlohmann::json j = nlohmann::json::object();
      for (const auto& [name, ring] : mStages)
      {
         if (!ring.samples.Empty())
            j[name] = ring.samples.Percentile(50);
      }
      return j;
   }

   const PercentileRing* GpuTimerRing::GetStage(const std::string& stageName) const
   {
      auto it = mStages.find(stageName);
      return (it != mStages.end()) ? &it->second.samples : nullptr;
   }

   uint64_t Fnv1a64(const void* data, size_t len)
   {
      const auto* bytes = static_cast<const unsigned char*>(data);
      uint64_t hash = 1469598103934665603ULL; // FNV offset basis
      for (size_t i = 0; i < len; i++)
      {
         hash ^= bytes[i];
         hash *= 1099511628211ULL; // FNV prime
      }
      return hash;
   }

   std::string HashFramebufferRGBA8(int width, int height)
   {
      if (width <= 0 || height <= 0)
         return "n/a";
      std::vector<unsigned char> pixels((size_t)width * (size_t)height * 4);
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      // When the hash moves, the raw pixels say by how much:
      // INFINITE_BENCH_DUMPRGBA=<file> writes them for scripts/bench/rgbadiff.py.
      if (const char* dumpPath = std::getenv("INFINITE_BENCH_DUMPRGBA"))
      {
         if (FILE* f = std::fopen(dumpPath, "wb"))
         {
            std::fwrite(pixels.data(), 1, pixels.size(), f);
            std::fclose(f);
         }
      }
      char hex[17];
      std::snprintf(hex, sizeof(hex), "%016llx",
                    (unsigned long long)Fnv1a64(pixels.data(), pixels.size()));
      return std::string(hex);
   }

   double ProcessRssMb() { return Platform::ProcessRssMb(); }
   double ProcessFootprintMb() { return Platform::ProcessFootprintMb(); }
   std::string HwModelString() { return Platform::HwModelString(); }

   std::string GlRendererString()
   {
      const GLubyte* renderer = glGetString(GL_RENDERER);
      return renderer ? std::string(reinterpret_cast<const char*>(renderer)) : "unknown";
   }

   std::string GitCommitShaShort()
   {
      // Diagnostic labelling only (BENCH_JSON's "commit" field) - a shell-out
      // that fails or is unavailable degrades to "unknown" rather than
      // affecting anything the fixture actually measures. run_all.sh exports
      // INFINITE_BENCH_COMMIT so an app run outside the repo is still labelled.
      if (const char* env = std::getenv("INFINITE_BENCH_COMMIT"); env && *env)
         return env;
#if defined(_WIN32)
      FILE* pipe = _popen("git rev-parse --short HEAD 2>NUL", "r");
#else
      FILE* pipe = popen("git rev-parse --short HEAD 2>/dev/null", "r");
#endif
      if (!pipe)
         return "unknown";
      char buf[64] = {};
      const char* got = std::fgets(buf, sizeof(buf), pipe);
#if defined(_WIN32)
      _pclose(pipe);
#else
      pclose(pipe);
#endif
      if (!got)
         return "unknown";
      std::string sha(buf);
      while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r'))
         sha.pop_back();
      return sha.empty() ? "unknown" : sha;
   }

   namespace GpuMem
   {
      struct AllocationEntry
      {
         GpuMemCategory category;
         size_t bytes = 0;
         std::string nodeType;
      };

      static std::map<unsigned int, AllocationEntry> sTextures;
      static std::map<unsigned int, AllocationEntry> sRenderbuffers;
      static std::map<unsigned int, AllocationEntry> sBuffers;

      static size_t BytesPerPixel(unsigned int internalFormat)
      {
         switch (internalFormat)
         {
            case GL_RGBA32F:
               return 16;
            case GL_RGBA16F:
               return 8;
            case GL_RGB16F:
               return 6;
            case GL_RGBA8:
            case GL_RGBA:
            case GL_UNSIGNED_INT:
               return 4;
            case GL_RGB8:
            case GL_RGB:
               return 3;
            case GL_RG8:
            case GL_RG16:
            case GL_RG:
               return 2;
            case GL_R8:
            case GL_RED:
            case GL_R8UI:
               return 1;
            case GL_DEPTH_COMPONENT24:
               return 3;
            case GL_DEPTH_COMPONENT32:
            case GL_DEPTH_COMPONENT32F:
            case GL_DEPTH24_STENCIL8:
               return 4;
            case GL_DEPTH_COMPONENT16:
               return 2;
            default:
               return 4;
         }
      }

      static size_t CalcTextureBytes(int w, int h, unsigned int internalFormat, bool mipmapped)
      {
         if (w <= 0 || h <= 0)
            return 0;
         const size_t bpp = BytesPerPixel(internalFormat);
         if (!mipmapped)
            return (size_t)w * (size_t)h * bpp;

         size_t total = 0;
         int curW = w;
         int curH = h;
         while (true)
         {
            int mw = std::max(1, curW);
            int mh = std::max(1, curH);
            total += (size_t)mw * (size_t)mh * bpp;
            if (curW <= 1 && curH <= 1)
               break;
            curW /= 2;
            curH /= 2;
         }
         return total;
      }

      static size_t CalcRenderbufferBytes(int w, int h, unsigned int internalFormat, int samples)
      {
         if (w <= 0 || h <= 0)
            return 0;
         const size_t bpp = BytesPerPixel(internalFormat);
         const size_t s = (size_t)std::max(1, samples);
         return (size_t)w * (size_t)h * bpp * s;
      }

      void RecordTexture(unsigned int id, GpuMemCategory cat, int w, int h, unsigned int internalFormat, bool mipmapped, const char* nodeType)
      {
         if (id == 0)
            return;
         size_t bytes = CalcTextureBytes(w, h, internalFormat, mipmapped);
         sTextures[id] = AllocationEntry{ cat, bytes, nodeType ? nodeType : "" };
      }

      void ReleaseTexture(unsigned int id)
      {
         if (id == 0)
            return;
         sTextures.erase(id);
      }

      void RecordRenderbuffer(unsigned int id, GpuMemCategory cat, int w, int h, unsigned int internalFormat, int samples, const char* nodeType)
      {
         if (id == 0)
            return;
         size_t bytes = CalcRenderbufferBytes(w, h, internalFormat, samples);
         sRenderbuffers[id] = AllocationEntry{ cat, bytes, nodeType ? nodeType : "" };
      }

      void ReleaseRenderbuffer(unsigned int id)
      {
         if (id == 0)
            return;
         sRenderbuffers.erase(id);
      }

      void RecordBuffer(unsigned int id, GpuMemCategory cat, size_t bytes, const char* nodeType)
      {
         if (id == 0)
            return;
         sBuffers[id] = AllocationEntry{ cat, bytes, nodeType ? nodeType : "" };
      }

      void ReleaseBuffer(unsigned int id)
      {
         if (id == 0)
            return;
         sBuffers.erase(id);
      }

      GpuMemBreakdown GetBreakdown()
      {
         GpuMemBreakdown bd;
         auto addBytes = [&bd](GpuMemCategory cat, size_t bytes, const std::string& label)
         {
            const double mb = (double)bytes / (1024.0 * 1024.0);
            switch (cat)
            {
               case GpuMemCategory::Textures: bd.texturesMb += mb; break;
               case GpuMemCategory::RenderTargets:
                  bd.renderTargetsMb += mb;
                  bd.renderTargetsByLabelMb[label] += mb;
                  break;
               case GpuMemCategory::ShadowMaps: bd.shadowMapsMb += mb; break;
               case GpuMemCategory::MeshBuffers: bd.meshBuffersMb += mb; break;
               case GpuMemCategory::InstanceBuffers: bd.instanceBuffersMb += mb; break;
            }
         };

         for (const auto& [id, entry] : sTextures)
            addBytes(entry.category, entry.bytes, entry.nodeType);
         for (const auto& [id, entry] : sRenderbuffers)
            addBytes(entry.category, entry.bytes, entry.nodeType);
         for (const auto& [id, entry] : sBuffers)
            addBytes(entry.category, entry.bytes, entry.nodeType);

         return bd;
      }

      double GetTotalMb()
      {
         return GetBreakdown().TotalMb();
      }

      void Reset()
      {
         sTextures.clear();
         sRenderbuffers.clear();
         sBuffers.clear();
      }
   }

   double CalculateRssSlopeMbPer100f(const std::vector<std::pair<int, double>>& samples)
   {
      if (samples.size() < 2)
         return 0.0;
      double sumX = 0.0, sumY = 0.0;
      for (const auto& [x, y] : samples)
      {
         sumX += x;
         sumY += y;
      }
      const double meanX = sumX / (double)samples.size();
      const double meanY = sumY / (double)samples.size();

      double num = 0.0, den = 0.0;
      for (const auto& [x, y] : samples)
      {
         const double dx = (double)x - meanX;
         const double dy = y - meanY;
         num += dx * dy;
         den += dx * dx;
      }
      if (den == 0.0)
         return 0.0;
      const double slopePerFrame = num / den;
      return slopePerFrame * 100.0;
   }

   void BenchReport::Emit() const
   {
      nlohmann::json j;
      j["bench"] = bench;
      j["variant"] = variant;
      j["machine"] = HwModelString();
      j["gpu"] = GlRendererString();
      j["commit"] = GitCommitShaShort();
      j["frames"] = frames;

      j["frame_ms"] = frameMs.ToJsonP5099Max();

      if (projectorMeasured)
      {
         j["projector"] = {
            { "refresh_hz", projectorRefreshHz },
            { "target_rate_hz", projectorTargetRateHz },
            { "present_interval_ms", projectorPresentMs.ToJsonP5099Max() },
            { "jitter_stddev_ms", projectorJitterStdDev },
            { "missed_vsync_pct", projectorMissedVsyncPct },
         };
         j["projector_ms"] = {
            { "p50", projectorPresentMs.Percentile(50) },
            { "p99", projectorPresentMs.Percentile(99) },
            { "missed_vsync", projectorMissedVsyncPct / 100.0 },
         };
      }
      else if (!projectorMs.Empty())
      {
         j["projector_ms"] = {
            { "p50", projectorMs.Percentile(50) },
            { "p99", projectorMs.Percentile(99) },
            { "missed_vsync", projectorMissedVsyncFraction },
         };
      }
      else
      {
         j["projector_ms"] = { { "p50", nullptr }, { "p99", nullptr }, { "missed_vsync", nullptr } };
      }

      j["stages_cpu_ms"] = stagesCpuMs;
      j["stages_gpu_ms"] = stagesGpuMs;

      if (inputToPhotonMeasured)
      {
         j["input_to_photon_frames"] = {
            { "p50", inputToPhotonFrames.Percentile(50) },
            { "max", inputToPhotonFrames.Max() },
            { "samples", (int)inputToPhotonFrames.Count() },
         };
      }

      if (canvasNavMeasured)
      {
         j["canvas_nav"] = {
            { "visible_nodes_avg", visibleNodesAvg },
            { "bodies_drawn_avg", bodiesDrawnAvg },
            { "offscreen_bodies_ms_avg", offscreenBodiesMsAvg },
            { "pan_frame_ms", panFrameMs.ToJsonP5099Max() },
            { "zoom_frame_ms", zoomFrameMs.ToJsonP5099Max() },
            { "drag_frame_ms", dragFrameMs.ToJsonP5099Max() },
            { "dropdown_frame_ms", dropdownFrameMs.ToJsonP5099Max() },
            { "drag_node_moved_px", dragNodeMovedPx },
            { "dropdown_open_frames", dropdownOpenFrames },
            { "on_vsync_frac", onVsyncFrac }
         };
      }

      if (!mediaIo.is_null())
         j["media_io"] = mediaIo;
      if (!slowFrames.is_null())
         j["slow_frames"] = slowFrames;

      if (audioMeasured)
      {
         j["audio"] = {
            { "buffer", audioBuffer },
            { "sr", audioSampleRate },
            { "cb_load_p50", audioLoad.Percentile(50) },
            { "cb_load_p99", audioLoad.Percentile(99) },
            { "cb_load_max", audioLoad.Max() },
            { "xruns", audioXruns },
            { "xruns_deadline", audioXrunsDeadline },
            { "xruns_os", audioXrunsOs },
            { "xrun_gaps", audioXrunGaps },
         };
      }
      else
      {
         j["audio"] = nullptr;
      }

      nlohmann::json memObj = {
         { "rss_mb", memRssEndMb },
         { "rss_mb_start", memRssStartMb },
      };
      if (memRssBuiltMb >= 0.0)
         memObj["rss_built_mb"] = memRssBuiltMb;
      if (memRssF32Mb >= 0.0)
         memObj["rss_f32_mb"] = memRssF32Mb;
      if (memRssF152Mb >= 0.0)
         memObj["rss_f152_mb"] = memRssF152Mb;
      if (memRssPeakMb >= 0.0)
         memObj["rss_peak_mb"] = memRssPeakMb;
      if (memDetailed)
         memObj["rss_slope_mb_per_100f"] = memRssSlopeMbPer100f;
      if (memFootEndMb >= 0.0)
      {
         memObj["footprint_mb"] = memFootEndMb;
         memObj["footprint_start_mb"] = memFootStartMb;
         if (memFootBuiltMb >= 0.0)
            memObj["footprint_built_mb"] = memFootBuiltMb;
         if (memFootF32Mb >= 0.0)
            memObj["footprint_f32_mb"] = memFootF32Mb;
         if (memFootF152Mb >= 0.0)
            memObj["footprint_f152_mb"] = memFootF152Mb;
         if (memFootPeakMb >= 0.0)
            memObj["footprint_peak_mb"] = memFootPeakMb;
         if (memDetailed)
            memObj["footprint_slope_mb_per_100f"] = memFootSlopeMbPer100f;
      }
      if (memGpuEstMb >= 0.0)
      {
         memObj["gpu_est_mb"] = memGpuEstMb;
         memObj["gl_tex_mb_est"] = memGpuEstMb;
         memObj["gpu_est_breakdown"] = memGpuEstBreakdown;
      }
      j["mem"] = memObj;

      if (!targetsPass.empty())
      {
         nlohmann::json tp = nlohmann::json::object();
         for (const auto& [k, v] : targetsPass)
            tp[k] = v;
         j["targets_pass"] = tp;
      }

      if (!soak.is_null())
         j["soak"] = soak;
      if (!offlineRender.is_null())
         j["offline_render"] = offlineRender;

      j["nodes"] = nodes;
      j["tris"] = tris;
      j["draw_calls"] = drawCalls < 0 ? nlohmann::json(nullptr) : nlohmann::json(drawCalls);
      j["fbo_allocs_steady"] = fboAllocsSteady < 0 ? nlohmann::json(nullptr) : nlohmann::json(fboAllocsSteady);
      j["output_hash"] = outputHash.empty() ? "n/a" : outputHash;

      std::printf("BENCH_JSON %s\n", j.dump().c_str());
      std::fflush(stdout);
   }
}
