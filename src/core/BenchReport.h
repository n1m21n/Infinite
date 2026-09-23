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
#include <cstdint>
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

   // Process RSS in MB "now", and the GL_RENDERER string of the current GL
   // context. Both go through Platform:: so every OS has a real
   // implementation (or an honest "n/a" on platforms where reading it isn't
   // worth the plumbing yet) - see windows-parity/linux-parity's three-sided
   // obligation.
   double ProcessRssMb();
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
      std::string outputHash;

      PercentileRing frameMs;
      PercentileRing projectorMs;
      double projectorMissedVsyncFraction = -1.0; // -1 = not measured
      nlohmann::json stagesCpuMs = nlohmann::json::object();
      nlohmann::json stagesGpuMs = nlohmann::json::object();

      // audio.* - -1 sentinel means "not measured by this fixture".
      bool audioMeasured = false;
      int audioBuffer = 0;
      double audioSampleRate = 0.0;
      PercentileRing audioLoad; // raw per-block load fraction samples
      uint64_t audioXruns = 0;

      double memRssStartMb = -1.0;
      double memRssEndMb = -1.0;

      void Emit() const;
   };
}
