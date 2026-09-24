#pragma once

// B8 "Media I/O" instrumentation (docs/plans/perf/benchmark-suite.md §4).
//
// Deliberately separate from BenchReport.h: the platform video layers
// (Platform.mm, win/MediaWin.cpp, linux/MediaLinux.cpp) write into these, and
// they have no business pulling in nlohmann::json. Everything here is inert
// unless MediaIoEnabled() is set, which only the INFINITE_BENCH_B8 fixture
// does, before it opens any clip - so a handle opened outside the bench never
// allocates stats, and the per-frame cost everywhere else is one relaxed
// atomic load (or a null check on a handle's stats pointer).

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Bench
{
   inline std::atomic<bool>& MediaIoEnabled()
   {
      static std::atomic<bool> on{ false };
      return on;
   }

   inline double MediaNowMs()
   {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
   }

   // Single-producer, lock-free, allocation-free sample ring. Each ring has one
   // producer: the decode thread, or for macOS's loopDecodeMs and every
   // cacheHitMs the thread calling VideoFrameAt. The fixture
   // reads it on the main thread at the end of the run while that producer may
   // still be running, so every slot is an atomic (relaxed - a torn *set* of
   // samples near the write head is fine, a torn float is not).
   class SpscSampleRing
   {
   public:
      static constexpr uint32_t kCapacity = 1u << 14; // 16384: > 4 decodes/frame over a 2400-frame run

      void Push(double ms)
      {
         const uint64_t i = mWrite.load(std::memory_order_relaxed);
         mBuf[(size_t)(i & (kCapacity - 1))].store((float)ms, std::memory_order_relaxed);
         mWrite.store(i + 1, std::memory_order_release);
      }

      uint64_t Count() const { return mWrite.load(std::memory_order_acquire); }

      // Samples pushed since Count() read `from` (as many of them as the ring
      // still holds), oldest first. from = 0 is everything still held.
      std::vector<double> SnapshotSince(uint64_t from) const
      {
         const uint64_t n = mWrite.load(std::memory_order_acquire);
         const uint64_t take = std::min<uint64_t>(n - std::min(from, n), kCapacity);
         std::vector<double> out;
         out.reserve((size_t)take);
         for (uint64_t i = n - take; i < n; i++)
            out.push_back((double)mBuf[(size_t)(i & (kCapacity - 1))].load(std::memory_order_relaxed));
         return out;
      }

   private:
      std::array<std::atomic<float>, kCapacity> mBuf {};
      std::atomic<uint64_t> mWrite{ 0 };
   };

   // Per video handle, owned by the platform layer's handle and created in
   // VideoOpen only while MediaIoEnabled(). Reached through
   // Platform::VideoBenchStats.
   struct MediaDecodeStats
   {
      // One real decode: macOS DecodeNext (AVAssetReader sample, plus the
      // BGRA->RGBA flip for frames that get shown), Windows ReadNextVideoFrame (ReadSample + RGB32->RGBA flip),
      // Linux decodeOneFrame (avcodec + sws_scale + flip).
      SpscSampleRing decodeMs;
      // A request served from the decoded-frame cache instead of the decoder
      // (macOS TryUseCache, Linux TryUseCacheLocked). Windows has no cache.
      SpscSampleRing cacheHitMs;
      // The loop boundary: time from a reader restart/seek to the first frame
      // decoded after it. On macOS it is what the viewer waits: from the
      // VideoFrameAt call that asked for the seek to the call that handed back
      // the new position's first frame (~0 when the parked loop-point reader
      // takes the wrap).
      SpscSampleRing loopDecodeMs;

      std::atomic<uint32_t> decoded{ 0 };
      std::atomic<uint32_t> cacheHits{ 0 };
      // Frames decoded and then superseded before anything showed them.
      std::atomic<uint32_t> dropped{ 0 };
      std::atomic<uint32_t> readerRestarts{ 0 };

      // Decode thread only (Windows/Linux): steady-clock ms of the last
      // seek, or < 0 when the next decode is not the first after one.
      double restartStartMs = -1.0;

      double nominalFps = 0.0;    // 0 = the platform did not say
      double deliveredPts = -1.0; // main thread: pts of the frame VideoFrameAt last handed back
   };

   // Main-thread per-clip counters kept by VideoSourceNode while
   // MediaIoEnabled(). A "request" is one cook with a clip loaded.
   struct MediaClipCounters
   {
      int requests = 0;
      int uploads = 0;        // glTex(Sub)Image2D calls
      int newFrames = 0;      // uploads whose pts differs from the previous upload
      int repeats = 0;        // requests that showed no new frame (same pts re-uploaded, or no frame)
      int reuploads = 0;      // repeats that still paid for an upload (same pts handed back again)
      int failed = 0;         // VideoFrameAt returned false
      int skipped = 0;        // source frames the shown sequence jumped over (not counting loop wraps)
      int loopWraps = 0;
      int expectedNew = 0;    // requests whose position entered a new source frame
      int expectedRepeats = 0;// requests whose position stayed on the same source frame
      double lastPts = -1.0;
      long long lastExpectedIndex = -1;
      std::vector<double> uploadCpuMs;
   };

   // Main-thread camera counters kept by VideoInNode while MediaIoEnabled().
   struct MediaCameraCounters
   {
      int frames = 0;              // new frameSeq values seen
      double firstFrameMs = -1.0;
      double lastFrameMs = -1.0;
      unsigned long long lastSeq = 0;
      std::vector<double> intervalMs; // main-thread arrival interval between new frames
      std::vector<double> readUploadMs; // CameraReadFrame copy + glTexImage2D
   };
}
