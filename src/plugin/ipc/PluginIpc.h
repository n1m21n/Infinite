#pragma once
// Shell <-> engine protocol for the Infinite plugin (docs/plans/vst-au/README.md 8.3, step 1.3).
//
// One shared-memory region per plugin instance, created by the shell (host process), opened by the
// engine helper. Everything the two sides exchange lives in it:
//
//   Header      magic, version, layout, rate, max block, wake words, heartbeat, attach/quit flags
//   Slots       one BlockIn per posted host block: frames, offline flag, transport, MIDI, param points
//   inRing      host input audio, indexed by absolute frame (main stereo + sidechain stereo)
//   outRing     engine output audio, indexed by absolute frame (stereo)
//   toEngine    framed byte ring: state, view input, commands   (message threads only)
//   toShell     framed byte ring: state replies, edits to automate, status
//
// Audio timing (8.2): the shell posts block [P, P+n) and reads output [P-L, P-L+n), L = max block.
// It waits only if the engine has not reached P-L+n: bounded in realtime (silence + xrun on a
// miss), unbounded offline. If the engine stalls and the slots or ring would overflow, the shell
// skips posting (silence + xrun) and the engine zero-fills the gap when it sees the next slot, so
// stale audio never comes back (resync).
//
// Realtime rules: ShellSide::Process and EngineSide's block calls never allocate or lock; the
// message calls are for message / main threads only.

#include "platform/common/Ipc.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PluginIpc
{
   constexpr uint32_t kMagic = 0x494E4650; // "INFP"
   constexpr uint32_t kVersion = 1;
   constexpr uint32_t kMaxBlockLimit = 8192;
   constexpr uint32_t kInChannels = 4;  // main L/R, sidechain L/R
   constexpr uint32_t kOutChannels = 2;
   constexpr uint32_t kSlots = 64;      // power of two
   constexpr uint32_t kMaxEvents = 512; // MIDI events per block
   constexpr uint32_t kMaxParamPoints = 256;
   constexpr uint32_t kMessageRingBytes = 1u << 20; // per direction, power of two
   constexpr uint32_t kMessageChunk = 64u * 1024u;   // larger messages are split

   struct Transport
   {
      double tempo = 120.0;
      double ppq = 0.0;          // quarter notes at block start
      double barStartPpq = 0.0;
      double loopStartPpq = 0.0, loopEndPpq = 0.0;
      int64_t hostSamplePos = 0; // host timeline position at block start
      int32_t sigNum = 4, sigDen = 4;
      uint32_t flags = 0;        // kPlaying | kLooping | kRecording | kTempoValid | kPpqValid
   };
   enum TransportFlags : uint32_t
   {
      kPlaying = 1, kLooping = 2, kRecording = 4, kTempoValid = 8, kPpqValid = 16
   };

   struct Event
   {
      uint32_t offset;   // sample offset in the block
      uint8_t status;    // MIDI status byte (channel in the low nibble)
      uint8_t data1, data2, pad;
      float value;       // high-resolution value (pitch bend -1..1, pressure 0..1); NaN if unused
      int32_t noteId;    // host note id, -1 if none
   };

   struct ParamPoint
   {
      uint16_t slot;     // 0..31 host slots, 32.. MIDI-mapping params (shell decides)
      uint16_t offset;   // sample offset in the block
      float value;       // normalized 0..1
   };

   enum BlockFlags : uint32_t
   {
      kOffline = 1, kSidechain = 2, kBypass = 4,
      kRelease = 8 // first block after the host suspended processing: let go of held notes (step 2.5)
   };

   struct BlockIn
   {
      uint64_t startFrame;
      uint32_t frames;
      uint32_t flags;
      Transport transport;
      uint32_t numEvents;
      uint32_t numParams;
      Event events[kMaxEvents];
      ParamPoint params[kMaxParamPoints];
   };

   struct alignas(64) Header
   {
      uint32_t magic, version;
      uint64_t totalBytes;
      double sampleRate;
      uint32_t maxBlock, ringFrames;
      uint64_t slotsOffset, inRingOffset, outRingOffset, toEngineOffset, toShellOffset;

      alignas(64) std::atomic<uint64_t> postedSeq;   // shell: slots posted
      std::atomic<uint32_t> postedWake;              // engine waits on this
      alignas(64) std::atomic<uint64_t> consumedSeq; // engine: slots finished
      std::atomic<uint64_t> doneFrames;              // engine: end of last cooked block
      std::atomic<uint32_t> doneWake;                // shell waits on this
      alignas(64) std::atomic<uint64_t> heartbeat;   // engine bumps at least every 100 ms
      std::atomic<uint32_t> attached;
      std::atomic<uint32_t> quit;
      std::atomic<uint64_t> xruns;                   // shell counts misses (shown in the view)
      std::atomic<uint64_t> gaps;                    // engine counts resyncs
   };

   struct alignas(64) ByteRing
   {
      std::atomic<uint64_t> head; // writer
      alignas(64) std::atomic<uint64_t> tail; // reader
      alignas(64) std::atomic<uint32_t> wake; // bumped by the writer
      uint8_t data[kMessageRingBytes];
   };

   struct Layout
   {
      uint32_t ringFrames;
      uint64_t slots, inRing, outRing, toEngine, toShell, total;
   };
   Layout ComputeLayout(uint32_t maxBlock);

   // A whole message, reassembled from chunks. `type` is free for the caller (state, edit, ...).
   struct Message
   {
      uint32_t type = 0;
      uint32_t id = 0;
      std::vector<uint8_t> data;
   };

   // Message channel over one ByteRing; one writer thread and one reader thread per direction.
   class Channel
   {
   public:
      void Attach(ByteRing* out, ByteRing* in) { out_ = out; in_ = in; }
      // Blocks up to timeoutMs while the ring is full. False on timeout (message not sent in full).
      bool Send(uint32_t type, uint32_t id, const void* data, size_t len, int timeoutMs);
      // Returns true and fills `msg` when a whole message has arrived. Waits up to timeoutMs.
      bool Receive(Message* msg, int timeoutMs);

   private:
      bool WriteChunk(const void* hdr, size_t hl, const void* data, size_t len, int timeoutMs);
      ByteRing* out_ = nullptr;
      ByteRing* in_ = nullptr;
      Message partial_;
      uint32_t expect_ = 0;
      bool inPartial_ = false;
   };

   // ---- host side --------------------------------------------------------------------------

   struct ProcessArgs
   {
      const float* const* in = nullptr; // up to kInChannels, entries may be null
      uint32_t numIn = 0;
      float* const* out = nullptr;      // kOutChannels
      uint32_t numOut = 0;
      uint32_t frames = 0;
      uint32_t flags = 0;               // BlockFlags
      Transport transport;
      const Event* events = nullptr;
      uint32_t numEvents = 0;
      const ParamPoint* params = nullptr;
      uint32_t numParams = 0;
   };

   enum class Result
   {
      Ok,        // output written
      Late,      // engine missed the realtime deadline: silence written, xrun counted
      Skipped,   // engine stalled, block not posted: silence written, xrun counted
      NoEngine   // not attached: silence written
   };

   class ShellSide
   {
   public:
      ~ShellSide() { Close(); }
      // Creates the region. Call from setupProcessing / setActive (not realtime).
      bool Create(double sampleRate, uint32_t maxBlock);
      void Close();
      const std::string& Name() const { return shm_.name; }
      bool Valid() const { return h_ != nullptr; }

      // Host audio thread. Realtime-safe.
      Result Process(const ProcessArgs& a);
      uint32_t Latency() const { return h_ ? h_->maxBlock : 0; }

      // Waits (non-realtime) until the engine has finished everything posted so far.
      bool WaitDone(uint64_t timeoutNs);
      uint64_t Posted() const { return posted_; }

      bool EngineAttached() const;
      // Waits (non-realtime) up to timeoutMs for the engine to attach; used before offline renders.
      bool WaitAttached(int timeoutMs, const Ipc::Child* engine);
      // Milliseconds since the heartbeat last changed; for the message thread's hang check.
      uint64_t HeartbeatAgeMs();
      // The raw counter, for a second watcher with its own clock (the shell's reader thread).
      uint64_t Heartbeat() const { return h_ ? h_->heartbeat.load(std::memory_order_relaxed) : 0; }
      void RequestQuit();
      uint64_t Xruns() const { return h_ ? h_->xruns.load() : 0; }

      Channel& Messages() { return chan_; }

   private:
      Ipc::SharedMemory shm_;
      Header* h_ = nullptr;
      BlockIn* slots_ = nullptr;
      float* inRing_ = nullptr;
      float* outRing_ = nullptr;
      uint64_t posted_ = 0, seq_ = 0;
      Channel chan_;
      uint64_t lastBeat_ = 0, lastBeatNs_ = 0;
      uint64_t lateMark_ = ~0ull; // doneFrames at the last realtime miss
   };

   // ---- engine side ------------------------------------------------------------------------

   struct BlockView
   {
      const BlockIn* in = nullptr;
      // Contiguous copies for the block; valid until Complete().
      float* input[kInChannels] = {};
      float* output[kOutChannels] = {};
      uint32_t frames = 0;
      bool gapBefore = false; // a resync happened just before this block
   };

   class EngineSide
   {
   public:
      ~EngineSide() { Close(); }
      bool Open(const std::string& name);
      void Close();
      double SampleRate() const { return h_ ? h_->sampleRate : 0.0; }
      uint32_t MaxBlock() const { return h_ ? h_->maxBlock : 0; }

      void MarkAttached();
      // Engine audio thread. Waits up to timeoutNs for the next block; bumps the heartbeat.
      // False on timeout or quit (check QuitRequested()).
      bool NextBlock(BlockView* v, uint64_t timeoutNs);
      // Publishes v->output for the block and wakes the shell.
      void Complete(const BlockView& v);
      bool QuitRequested() const { return !h_ || h_->quit.load(std::memory_order_acquire) != 0; }
      void Beat() { if (h_) h_->heartbeat.fetch_add(1, std::memory_order_relaxed); }
      uint64_t Gaps() const { return h_ ? h_->gaps.load() : 0; }
      // Blocks the shell answered with silence because the engine was late (shown in the top bar).
      uint64_t Xruns() const { return h_ ? h_->xruns.load(std::memory_order_relaxed) : 0; }

      Channel& Messages() { return chan_; }

   private:
      Ipc::SharedMemory shm_;
      Header* h_ = nullptr;
      BlockIn* slots_ = nullptr;
      float* inRing_ = nullptr;
      float* outRing_ = nullptr;
      uint64_t read_ = 0;   // next slot seq
      uint64_t done_ = 0;   // frames
      uint32_t seenWake_ = 0;
      std::vector<float> scratch_;
      Channel chan_;
   };
}
