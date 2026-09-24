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
      // affecting anything the fixture actually measures.
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

      j["projector_ms"] = projectorMs.Empty()
         ? nlohmann::json{ { "p50", nullptr }, { "p99", nullptr }, { "missed_vsync", nullptr } }
         : nlohmann::json{
              { "p50", projectorMs.Percentile(50) },
              { "p99", projectorMs.Percentile(99) },
              { "missed_vsync", projectorMissedVsyncFraction },
           };

      j["stages_cpu_ms"] = stagesCpuMs;
      j["stages_gpu_ms"] = stagesGpuMs;

      if (audioMeasured)
      {
         j["audio"] = {
            { "buffer", audioBuffer },
            { "sr", audioSampleRate },
            { "cb_load_p50", audioLoad.Percentile(50) },
            { "cb_load_p99", audioLoad.Percentile(99) },
            { "cb_load_max", audioLoad.Max() },
            { "xruns", audioXruns },
         };
      }
      else
      {
         j["audio"] = nullptr;
      }

      j["mem"] = {
         { "rss_mb", memRssEndMb },
         { "rss_mb_start", memRssStartMb },
      };

      j["nodes"] = nodes;
      j["tris"] = tris;
      j["draw_calls"] = drawCalls < 0 ? nlohmann::json(nullptr) : nlohmann::json(drawCalls);
      j["output_hash"] = outputHash.empty() ? "n/a" : outputHash;

      std::printf("BENCH_JSON %s\n", j.dump().c_str());
      std::fflush(stdout);
   }
}
