#include "BenchReport.h"

#include "gl3.h"
#include "../platform/Platform.h"

#include <chrono>
#include <cstdio>

namespace Bench
{
   double ScopedStageTimer::NowMs()
   {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
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
      j["output_hash"] = outputHash.empty() ? "n/a" : outputHash;

      std::printf("BENCH_JSON %s\n", j.dump().c_str());
      std::fflush(stdout);
   }
}
