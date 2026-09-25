#pragma once

// Shared instrumentation for the INFINITE_BENCH performance suite
// (docs/plans/perf/benchmark-suite.md). Every fixture builds one BenchReport,
// fills in the sections it measures, and calls Emit() once at the end of its
// run to print a single `BENCH_JSON {...}` line that scripts/bench/run_all.sh
// greps for. Sections a fixture doesn't measure are left at their default
// (0/empty) rather than fabricated.
//
// Two different concurrency worlds meet here:
//  - PercentileRing is plain (not thread-safe) and is for main-thread-only
//    series (frame_ms, GPU stage ms already read back on the main thread).
//  - AudioLoadRing is the one piece the audio thread writes into. It is
//    lock-free/allocation-free by construction (fixed array, atomic index,
//    relaxed stores) so it is safe to feed from AudioEngine::Process. See
//    audio-pipeline-sweep / field-realtime for why that constraint exists.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace Bench
{
   // Main-thread-only percentile accumulator. Not fixed-capacity - a bench
   // run samples at most a few thousand frames, so a plain vector is fine and
   // keeps the percentile math simple (sort + index) rather than needing a
   // streaming approximation.
   class PercentileRing
   {
   public:
      void Push(double v) { mSamples.push_back(v); }
      size_t Count() const { return mSamples.size(); }
      bool Empty() const { return mSamples.empty(); }

      // Count of samples strictly greater than threshold - used for "missed
      // vsync" / "frames over 1.5x budget" style counts.
      size_t CountOver(double threshold) const
      {
         size_t n = 0;
         for (double v : mSamples)
            if (v > threshold)
               n++;
         return n;
      }

      double Min() const { return mSamples.empty() ? 0.0 : *std::min_element(mSamples.begin(), mSamples.end()); }
      double Max() const { return mSamples.empty() ? 0.0 : *std::max_element(mSamples.begin(), mSamples.end()); }
      double Mean() const
      {
         if (mSamples.empty())
            return 0.0;
         double sum = 0.0;
         for (double v : mSamples)
            sum += v;
         return sum / (double)mSamples.size();
      }

      double StdDev() const
      {
         if (mSamples.size() < 2)
            return 0.0;
         const double mean = Mean();
         double acc = 0.0;
         for (double v : mSamples)
            acc += (v - mean) * (v - mean);
         return std::sqrt(acc / (double)(mSamples.size() - 1));
      }

      // Percentile p in [0,100]. Nearest-rank method - fine for a few hundred
      // to a few thousand samples, and matches what the target table in
      // benchmark-suite.md is checking against (p50/p95/p99).
      double Percentile(double p) const
      {
         if (mSamples.empty())
            return 0.0;
         std::vector<double> sorted(mSamples);
         std::sort(sorted.begin(), sorted.end());
         const double rank = (p / 100.0) * (double)(sorted.size() - 1);
         const size_t lo = (size_t)rank;
         const size_t hi = std::min(lo + 1, sorted.size() - 1);
         const double frac = rank - (double)lo;
         return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
      }

      // {"p50":..,"p95":..,"p99":..,"max":..} - the shape every *_ms field in
      // BENCH_JSON uses.
      nlohmann::json ToJsonP5099Max() const
      {
         return nlohmann::json{
            { "p50", Percentile(50) },
            { "p95", Percentile(95) },
            { "p99", Percentile(99) },
            { "max", Max() },
         };
      }

      const std::vector<double>& Samples() const { return mSamples; }

   private:
      std::vector<double> mSamples;
   };

   // Lock-free, allocation-free ring the audio thread can push raw per-block
   // load-fraction samples into (see AudioEngine::Process). Fixed capacity;
   // once full, oldest samples are overwritten - a bench run drains it after
   // stopping the engine, well before wraparound would lose the window it
   // cares about (60s at a 128-sample block/48kHz is ~22,500 blocks; kCapacity
   // covers a 10-minute run at the smallest supported block size).
   class AudioLoadRing
   {
   public:
      static constexpr size_t kCapacity = 1 << 20; // ~1M samples

      // Audio thread only. instantLoad is (time spent in RunTopology) /
      // (block's real-time deadline), same numerator/denominator as the
      // existing smoothed AudioEngine::LastBlockLoad().
      void Push(float instantLoad)
      {
         const uint64_t idx = mWriteIndex.fetch_add(1, std::memory_order_relaxed);
         mSamples[idx % kCapacity].store(instantLoad, std::memory_order_relaxed);
      }

      void Reset() { mWriteIndex.store(0, std::memory_order_relaxed); }

      // Main thread only, after the audio thread has stopped (or is known to
      // be idle) - reads back whatever the ring currently holds.
      PercentileRing Drain() const
      {
         PercentileRing out;
         const uint64_t written = mWriteIndex.load(std::memory_order_relaxed);
         const uint64_t count = std::min<uint64_t>(written, kCapacity);
         const uint64_t start = written > kCapacity ? written - kCapacity : 0;
         for (uint64_t i = start; i < start + count; i++)
            out.Push((double)mSamples[i % kCapacity].load(std::memory_order_relaxed));
         return out;
      }

      // Main thread, while the audio thread keeps pushing: total pushes so
      // far, and the samples in [from, to) of that count (whatever of it the
      // ring still holds). B7's soak reads its 10 s windows this way.
      uint64_t Written() const { return mWriteIndex.load(std::memory_order_relaxed); }
      PercentileRing DrainRange(uint64_t from, uint64_t to) const
      {
         PercentileRing out;
         const uint64_t start = std::max(from, to > kCapacity ? to - kCapacity : 0);
         for (uint64_t i = start; i < to; i++)
            out.Push((double)mSamples[i % kCapacity].load(std::memory_order_relaxed));
         return out;
      }

   private:
      std::array<std::atomic<float>, kCapacity> mSamples {};
      std::atomic<uint64_t> mWriteIndex { 0 };
   };

   // A single named CPU stage's accumulated time this run, in milliseconds.
   // Fixtures own a std::map<std::string, PercentileRing> keyed by stage name
   // (see ScopedStageTimer below) rather than this struct directly - it just
   // documents the shape stages_cpu_ms/stages_gpu_ms serialize to.
   struct StageTimings
   {
      std::vector<std::pair<std::string, double>> stages; // name -> p50 ms, already reduced

      nlohmann::json ToJson() const
      {
         nlohmann::json j = nlohmann::json::object();
         for (auto& [name, ms] : stages)
            j[name] = ms;
         return j;
      }
   };

   // RAII CPU stage timer: construct at the top of a main-loop stage,
   // destructing pushes the elapsed ms into the named PercentileRing. Pass
   // the same map/ring every frame; percentiles come out of ToJsonP5099Max()
   // at report time, but stages_cpu_ms in BENCH_JSON is documented as a
   // single p50 per stage, so fixtures typically report Percentile(50) per
   // stage into StageTimings.
   class ScopedStageTimer
   {
   public:
      explicit ScopedStageTimer(PercentileRing& sink) : mSink(sink), mStart(NowMs()) {}
      ~ScopedStageTimer() { mSink.Push(NowMs() - mStart); }

      static double NowMs();

   private:
      PercentileRing& mSink;
      double mStart;
   };

   // Pipelined GPU timer query ring (GL_TIME_ELAPSED).
   // Manages an N-frame in-flight query ring per stage so reading back elapsed
   // GPU nanoseconds never causes synchronous CPU/GPU pipeline stalls.
   // Query objects are per-context and are not shared: never let a stage span
   // a glfwMakeContextCurrent (the projector loop does), or Begin and End land
   // in different contexts. Stages cannot nest either.
   class GpuTimerRing
   {
   public:
      static constexpr int kRingDepth = 4;

      GpuTimerRing();
      ~GpuTimerRing();

      GpuTimerRing(const GpuTimerRing&) = delete;
      GpuTimerRing& operator=(const GpuTimerRing&) = delete;

      // Returns true if GL timer queries are supported and available
      bool IsSupported();

      // Begin/End GPU timing for a named stage in the given frame
      void BeginStage(const std::string& stageName, int frameId);
      void EndStage(const std::string& stageName);

      // Poll completed query results from previous frames (non-blocking)
      void Poll(int currentFrameId);

      // Flush remaining in-flight queries (e.g. at end of benchmark before reporting)
      void Finish();

      // Clear all queries and percentiles
      void Reset();

      // Returns p50 ms per stage as a JSON object: {"cook": 0.12, ...}
      nlohmann::json ToJsonP50() const;

      const PercentileRing* GetStage(const std::string& stageName) const;

   private:
      // One ring entry holds every Begin/End interval a stage recorded in
      // one frame. The entry's per-frame total is pushed as one sample, so a
      // stage timed once per frame and a stage timed once per node instance
      // (NodeGpuRing below) both report "GPU ms per frame".
      struct FrameSlot
      {
         std::vector<unsigned int> queries; // grown on demand, reused
         int used = 0;
         int frameId = -1;
         bool inFlight = false;
         bool skipped = false; // an older frame still owned this entry
      };

      struct StageRing
      {
         std::array<FrameSlot, kRingDepth> slots {};
         PercentileRing samples;
         bool active = false;
      };

      void EnsureInitialized();
      // Returns true and pushes the frame total if every query is ready.
      static bool Harvest(FrameSlot& slot, PercentileRing& samples, bool wait);

      bool mInitialized = false;
      bool mSupported = false;
      std::string mCurrentActiveStage;
      std::map<std::string, StageRing> mStages;
   };

   // RAII helper for GPU stage timing
   struct ConditionalGpuStageTimer
   {
      GpuTimerRing* mRing = nullptr;
      std::string mStageName;
      bool mStopped = false;

      ConditionalGpuStageTimer(GpuTimerRing* ring, const char* stageName, int frameId)
         : mRing(ring)
      {
         // Name copied only when timing, so a disabled timer on a hot path
         // (every node cook) costs a null check and nothing else.
         if (mRing && stageName && *stageName)
         {
            mStageName = stageName;
            mRing->BeginStage(mStageName, frameId);
         }
      }

      void Stop()
      {
         if (mRing && !mStopped && !mStageName.empty())
         {
            mRing->EndStage(mStageName);
            mStopped = true;
         }
      }

      ~ConditionalGpuStageTimer()
      {
         Stop();
      }
   };

   // Per-node GPU attribution. nullptr except while a fixture that asked for
   // it (B2 with INFINITE_BENCH_B2GPUNODES) is sampling the cook stage. Nodes
   // that do real GPU work wrap their own draw, keyed by node type, after
   // they have pulled their inputs, so these intervals run one after another
   // and never nest. The fixture stops timing the enclosing "cook" stage
   // while this is set.
   inline GpuTimerRing*& NodeGpuRing()
   {
      static GpuTimerRing* ring = nullptr;
      return ring;
   }

   // B4 with INFINITE_BENCH_B4PASSES: Render 3D times its shadow, opaque,
   // transmissive and MSAA-resolve passes into NodeGpuRing() separately
   // instead of one "render3d" interval (which would nest around them).
   inline bool& Render3DPassSplit()
   {
      static bool split = false;
      return split;
   }

   // GPU memory breakdown categories
   enum class GpuMemCategory
   {
      Textures,
      RenderTargets,
      ShadowMaps,
      MeshBuffers,
      InstanceBuffers,
   };

   struct GpuMemBreakdown
   {
      double texturesMb = 0.0;
      double renderTargetsMb = 0.0;
      double shadowMapsMb = 0.0;
      double meshBuffersMb = 0.0;
      double instanceBuffersMb = 0.0;
      // Render targets split by the label each allocation was recorded with
      // ("Fbo", "ScratchFbo", "Render3D_MSAA", ...), so B9 shows what they are.
      std::map<std::string, double> renderTargetsByLabelMb;

      double TotalMb() const
      {
         return texturesMb + renderTargetsMb + shadowMapsMb + meshBuffersMb + instanceBuffersMb;
      }

      nlohmann::json ToJson() const
      {
         return nlohmann::json{
            { "textures_mb", texturesMb },
            { "render_targets_mb", renderTargetsMb },
            { "shadow_maps_mb", shadowMapsMb },
            { "mesh_buffers_mb", meshBuffersMb },
            { "instance_buffers_mb", instanceBuffersMb },
            { "render_targets_by_label_mb", renderTargetsByLabelMb }
         };
      }
   };

   namespace GpuMem
   {
      void RecordTexture(unsigned int id, GpuMemCategory cat, int w, int h, unsigned int internalFormat, bool mipmapped = false, const char* nodeType = nullptr);
      void ReleaseTexture(unsigned int id);

      void RecordRenderbuffer(unsigned int id, GpuMemCategory cat, int w, int h, unsigned int internalFormat, int samples = 1, const char* nodeType = nullptr);
      void ReleaseRenderbuffer(unsigned int id);

      void RecordBuffer(unsigned int id, GpuMemCategory cat, size_t bytes, const char* nodeType = nullptr);
      void ReleaseBuffer(unsigned int id);

      GpuMemBreakdown GetBreakdown();
      double GetTotalMb();
      void Reset();
   }

   // Linear regression slope in MB per 100 frames over (frameId, rssMb) samples
   double CalculateRssSlopeMbPer100f(const std::vector<std::pair<int, double>>& samples);

   // FNV-1a 64-bit over raw bytes - used for output_hash (a hash of the
   // Output texture's readback pixels). Not cryptographic; it only needs to
   // change when the rendered image changes, which is the quality-guard
   // contract in benchmark-suite.md §3.
   uint64_t Fnv1a64(const void* data, size_t len);

   // Reads back `width`x`height` RGBA8 pixels from whichever framebuffer/
   // texture is currently bound for reading (caller sets that up - see
   // GLUtil for the FBO-bind helpers) and returns Fnv1a64 of the raw bytes,
   // hex-formatted. Only ever called once at the end of a run - a full
   // glReadPixels is a pipeline stall, never do this per-frame.
   std::string HashFramebufferRGBA8(int width, int height);

   // Process RSS in MB "now", Process Footprint in MB "now", and the GL_RENDERER string
   // of the current GL context. All go through Platform:: so every OS has a real
   // implementation (or an honest "n/a" on platforms where reading it isn't
   // worth the plumbing yet) - see windows-parity/linux-parity's three-sided
   // obligation.
   double ProcessRssMb();
   double ProcessFootprintMb();
   std::string GlRendererString();
   std::string HwModelString();
   std::string GitCommitShaShort();

   // One JSON line, prefixed "BENCH_JSON " per benchmark-suite.md §3. Build
   // one of these per fixture, fill in whichever sections it measured, call
   // Emit() once.
   struct BenchReport
   {
      std::string bench;    // e.g. "B5_fundamentals"
      std::string variant;  // "s"/"m"/"l", or empty
      int frames = 0;
      int nodes = 0;
      int tris = 0;
      int drawCalls = -1; // Render 3D draw calls last frame; -1 = not measured (null)
      long long fboAllocsSteady = -1; // GLUtil::FboAllocationCount delta over the sample window; -1 = not measured (null)
      std::string outputHash;

      PercentileRing frameMs;
      PercentileRing projectorMs;
      double projectorMissedVsyncFraction = -1.0; // -1 = not measured

      // Detailed projector pacing (B3)
      bool projectorMeasured = false;
      int projectorRefreshHz = 0;
      int projectorTargetRateHz = 0;
      PercentileRing projectorPresentMs;
      double projectorJitterStdDev = 0.0;
      double projectorMissedVsyncPct = 0.0;

      // Input-to-photon latency in frames (B3)
      bool inputToPhotonMeasured = false;
      PercentileRing inputToPhotonFrames;

      // Targets pass/fail map
      std::map<std::string, nlohmann::json> targetsPass;

      // Canvas navigation (B6)
      bool canvasNavMeasured = false;
      double visibleNodesAvg = 0.0;
      double bodiesDrawnAvg = 0.0;
      double offscreenBodiesMsAvg = 0.0;
      PercentileRing panFrameMs;
      PercentileRing zoomFrameMs;
      PercentileRing dragFrameMs;
      PercentileRing dropdownFrameMs;
      double dragNodeMovedPx = 0.0;   // proves the synthetic drag moved a node
      int dropdownOpenFrames = 0;     // proves the popup actually opened
      double onVsyncFrac = 0.0;       // share of frame intervals on a refresh boundary

      // Media I/O (B8): per-clip decode/upload, per-window present, Syphon/
      // Spout publish, camera. Built by the fixture; null = not measured.
      nlohmann::json mediaIo = nullptr;

      nlohmann::json stagesCpuMs = nlohmann::json::object();
      nlohmann::json stagesGpuMs = nlohmann::json::object();

      // audio.* - -1 sentinel means "not measured by this fixture".
      bool audioMeasured = false;
      int audioBuffer = 0;
      double audioSampleRate = 0.0;
      PercentileRing audioLoad; // raw per-block load fraction samples
      uint64_t audioXruns = 0;         // deadline + os (AudioEngine::XrunCount)
      uint64_t audioXrunsDeadline = 0; // blocks that used the whole period
      uint64_t audioXrunsOs = 0;       // device-reported overload/underrun
      uint64_t audioXrunGaps = 0;      // callback-gap heuristic, info only

      double memRssStartMb = -1.0;
      double memRssBuiltMb = -1.0;
      double memRssF32Mb = -1.0;
      double memRssF152Mb = -1.0;
      double memRssEndMb = -1.0;
      double memRssPeakMb = -1.0;
      double memRssSlopeMbPer100f = 0.0;


      bool memDetailed = false;

      // OS-charged footprint (Platform::ProcessFootprintMb), same points as
      // the RSS fields. -1 = not measured, left out of the JSON.
      double memFootStartMb = -1.0;
      double memFootBuiltMb = -1.0;
      double memFootF32Mb = -1.0;
      double memFootF152Mb = -1.0;
      double memFootEndMb = -1.0;
      double memFootPeakMb = -1.0;
      double memFootSlopeMbPer100f = 0.0;

      double memGpuEstMb = -1.0;
      nlohmann::json memGpuEstBreakdown = nlohmann::json::object();

      // B7 soak: samples every 10 s plus the soak verdicts. null = not a soak.
      nlohmann::json soak = nullptr;

      void Emit() const;
   };
}
