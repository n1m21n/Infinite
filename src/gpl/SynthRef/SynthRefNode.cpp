// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
//
// Threading. A firmware core is single-threaded C, so every call into it is
// made under AudioSynthRefNode::mCoreMutex. The audio thread only ever
// try_locks it: if the main thread holds it (swapping a core, a rare heavy
// operation), that one block is silent instead of the callback waiting.
// Everything frequent goes the other way, lock-free: the main thread posts
// controls and MIDI into an SPSC command ring, the audio thread executes them
// right before rendering and publishes what the panel needs (LCD, LEDs,
// state) through triple buffers. Cheap one-off operations (load a preset,
// import a bank, export the state) run as a "job" on the audio thread while
// the main thread waits; with no audio running the main thread runs them
// itself under the lock.
#include "SynthRefNode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "SynthRefCore.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/NoteEventQueue.h"
#include "core/Base64.h"
#include "platform/Platform.h"

namespace fs = std::filesystem;

namespace
{
   int64_t NowMs()
   {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
   }

   // Lock-free triple buffer: one writer, one reader, the reader always gets
   // the newest complete value and the writer never waits.
   template <class T>
   struct Triple
   {
      T buf[3];
      std::atomic<int> middle { 1 };
      int write = 0, read = 2;
      T& Back() { return buf[write]; }
      void Publish() { write = middle.exchange(write | 4) & 3; }
      // True when a newer value arrived since the last Fetch.
      bool Fetch()
      {
         if ((middle.load(std::memory_order_acquire) & 4) == 0)
            return false;
         read = middle.exchange(read) & 3;
         return true;
      }
      const T& Front() const { return buf[read]; }
   };

   // Windowed-sinc polyphase resampler from the core's fixed rate to the
   // engine's. 32 taps, 256 interpolated phases, Kaiser (beta 7) window,
   // cutoff 0.92 x the lower Nyquist: passband flat to ~20 kHz at 44.1 ->
   // 48 kHz, images below -80 dB. Equal rates bypass it bit-exactly.
   class Resampler
   {
   public:
      static constexpr int kTaps = 32, kHalf = 16, kPhases = 256, kCap = 1 << 15;

      void Configure(double inRate, double outRate)
      {
         mBypass = std::fabs(inRate - outRate) < 1e-6;
         mStep = inRate / outRate;
         Reset();
         if (mBypass)
            return;
         const double fc = 0.5 * std::min(1.0, outRate / inRate) * 0.92;
         auto bessel0 = [](double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k)
            {
               term *= (x / (2.0 * k)) * (x / (2.0 * k));
               sum += term;
            }
            return sum;
         };
         const double beta = 7.0, i0b = bessel0(beta);
         mTable.assign((size_t)(kPhases + 1) * kTaps, 0.0f);
         for (int p = 0; p <= kPhases; ++p)
         {
            const double f = (double)p / kPhases;
            double row[kTaps], sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
               const double d = (double)(k - (kHalf - 1)) - f; // input index minus output position
               const double x = 2.0 * fc * d;
               const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
               const double w = d / kHalf;
               const double win = std::fabs(w) >= 1.0 ? 0.0 : bessel0(beta * std::sqrt(1.0 - w * w)) / i0b;
               row[k] = 2.0 * fc * sinc * win;
               sum += row[k];
            }
            for (int k = 0; k < kTaps; ++k)
               mTable[(size_t)p * kTaps + k] = (float)(row[k] / sum); // unity DC gain
         }
      }

      void Reset()
      {
         std::memset(mL, 0, sizeof(mL));
         std::memset(mR, 0, sizeof(mR));
         mWritten = 0;
         mPos = mBypass ? 0 : (uint64_t)(kHalf - 1);
         mFrac = 0.0;
      }

      void Push(const float* interleaved, int frames, int channels)
      {
         for (int i = 0; i < frames; ++i)
         {
            const size_t at = (size_t)(mWritten & (kCap - 1));
            mL[at] = interleaved[i * channels];
            mR[at] = channels > 1 ? interleaved[i * channels + 1] : mL[at];
            ++mWritten;
         }
      }

      // Whether `frames` outputs can be produced with the input pushed so far.
      bool CanProduce(int frames) const
      {
         if (mBypass)
            return mWritten >= mPos + (uint64_t)frames;
         const double end = (double)mPos + mFrac + mStep * (frames - 1);
         return (double)mWritten > end + kHalf + 1.0;
      }

      void Produce(float* L, float* R, int frames)
      {
         if (mBypass)
         {
            for (int i = 0; i < frames; ++i)
            {
               const size_t at = (size_t)(mPos++ & (kCap - 1));
               L[i] = mL[at];
               R[i] = mR[at];
            }
            return;
         }
         for (int i = 0; i < frames; ++i)
         {
            const double fp = mFrac * kPhases;
            const int p = std::min((int)fp, kPhases - 1);
            const float t = (float)(fp - p);
            const float* h0 = &mTable[(size_t)p * kTaps];
            const float* h1 = h0 + kTaps;
            const uint64_t first = mPos - (kHalf - 1);
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < kTaps; ++k)
            {
               const float h = h0[k] + (h1[k] - h0[k]) * t;
               const size_t at = (size_t)((first + k) & (kCap - 1));
               l += h * mL[at];
               r += h * mR[at];
            }
            L[i] = l;
            R[i] = r;
            mFrac += mStep;
            const double whole = std::floor(mFrac);
            mPos += (uint64_t)whole;
            mFrac -= whole;
         }
      }

      bool Bypass() const { return mBypass; }

   private:
      float mL[kCap], mR[kCap];
      std::vector<float> mTable;
      uint64_t mWritten = 0, mPos = 0;
      double mFrac = 0.0, mStep = 1.0;
      bool mBypass = true;
   };

   struct LcdFrame
   {
      std::vector<uint16_t> px;
      uint32_t revision = 0;
   };

   struct Snapshot
   {
      int32_t q[20] = {};
      uint8_t leds[128] = {};
      char project[16] = {};
      char pads[16][8] = {};
      char sources[8][8] = {};
      bool valid = false;
   };

   enum CmdType : uint8_t
   {
      kCmdControl,
      kCmdMidi,
      kCmdRelease,
      kCmdPanic
   };
   struct Cmd
   {
      uint8_t type = 0, kind = 0, count = 0, bytes[3] = {};
      uint16_t id = 0;
      int32_t value = 0;
   };

   // What the audio thread did with one device message, for MIDI DIAGNOSTICS.
   enum DiagWhat : uint8_t
   {
      kDiagIgnoredInactive, // not focused / not locked
      kDiagIgnoredDevice,   // another device
      kDiagMapped,          // target/id below
      kDiagKeyboard,        // played on the keyboard track
      kDiagPassed,          // forwarded raw to the keyboard track
      kDiagDropped,         // nothing to do (e.g. notes while the pin is cabled)
      kDiagPin              // a note from the notes pin
   };
   struct Diag
   {
      uint32_t device = 0;
      uint8_t bytes[3] = {}, size = 0;
      uint8_t what = 0, target = 0, id = 0, channel = 0;
      int8_t detents = 0; // encoder motion the firmware received
   };

   template <class T, int N>
   struct Spsc
   {
      T items[N];
      std::atomic<uint32_t> head { 0 }, tail { 0 };
      bool Push(const T& v)
      {
         const uint32_t h = head.load(std::memory_order_relaxed);
         if (h - tail.load(std::memory_order_acquire) >= (uint32_t)N)
            return false;
         items[h % N] = v;
         head.store(h + 1, std::memory_order_release);
         return true;
      }
      bool Pop(T& v)
      {
         const uint32_t t = tail.load(std::memory_order_relaxed);
         if (t == head.load(std::memory_order_acquire))
            return false;
         v = items[t % N];
         tail.store(t + 1, std::memory_order_release);
         return true;
      }
      void Clear() { tail.store(head.load()); }
   };

   std::atomic<const SynthRefNode*> gFocusedNode { nullptr };

   constexpr int kQueryKeys = 17; // SR_Q_TRACK .. SR_Q_EDIT_REVISION
}

// ---------------------------------------------------------------------------
// Audio half
// ---------------------------------------------------------------------------
class AudioSynthRefNode : public AudioNode
{
public:
   using JobFn = int (*)(AudioSynthRefNode& self, const SynthRefCoreApi& api, void* inst, void* ctx);

   AudioSynthRefNode() : mRawCursor(Platform::MidiRawStreamPosition()) { mRs.Configure(44100.0, 44100.0); }
   ~AudioSynthRefNode() override
   {
      std::lock_guard<std::mutex> lock(mCoreMutex);
      mCore.reset();
   }

   // ---- main thread ----
   void InstallCore(std::unique_ptr<SynthRefCore> core)
   {
      std::unique_ptr<SynthRefCore> old;
      {
         std::lock_guard<std::mutex> lock(mCoreMutex);
         old = std::move(mCore);
         mCore = std::move(core);
         mInfo = mCore ? &mCore->Info() : nullptr;
         const size_t px = mInfo && mInfo->lcdFormat == SR_LCD_RGB565 ? (size_t)mInfo->lcdWidth * mInfo->lcdHeight : 0;
         for (auto& b : mLcd.buf)
         {
            b.px.assign(px, 0);
            b.revision = 0;
         }
         mLcdRevision = 0xFFFFFFFFu;
         mRs.Configure(mInfo ? (double)mInfo->sampleRate : 44100.0, mOutRate);
         std::memset(mLastCc, 0xFF, sizeof(mLastCc));
         std::memset(mHeldPin, 0xFF, sizeof(mHeldPin));
         std::memset(mHeldDevice, 0xFF, sizeof(mHeldDevice));
         mSnapCountdown = 0;
         mTextCountdown = 0;
         mCommands.Clear();
         // The panel must not read the old firmware's state as the new one's
         // (main thread is the only snapshot reader, so this is safe here).
         for (Snapshot& snap : mSnap.buf)
            snap.valid = false;
      }
      // `old` (and its private DLL copy) goes here, outside the lock.
   }

   bool HasCore() const { return mInfo != nullptr; }
   const SynthRefCoreInfo* Info() const { return mInfo; }

   // Runs fn on the audio thread before its next render, waiting for it. With
   // no audio running, runs it here under the lock. Returns fn's result, or
   // INT32_MIN without a core.
   int RunJob(JobFn fn, void* ctx)
   {
      if (!mInfo)
         return INT32_MIN;
      int result = 0;
      if (!Begin(mJob, fn, ctx, result))
         return result;
      const int64_t deadline = NowMs() + 400;
      while (!Poll(mJob, result))
      {
         if (NowMs() > deadline && Cancel(mJob))
            return RunLocked(fn, ctx);
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return result;
   }

   // The same without waiting (state export while the user edits): false if
   // the previous export is still in flight. PollExport turns true once, with
   // the result, when it is done.
   bool BeginExport(JobFn fn, void* ctx)
   {
      if (!mInfo || mExport.state.load() != 0)
         return false;
      int result = 0;
      if (!Begin(mExport, fn, ctx, result))
      {
         mExport.result = result;
         mExport.state.store(2, std::memory_order_release);
      }
      return true;
   }
   bool PollExport(int& result)
   {
      if (Poll(mExport, result))
         return true;
      // Audio stopped with the export queued: run it here.
      if (mExport.state.load() == 1 && !AudioLive() && Cancel(mExport))
      {
         result = RunLocked(mExport.fn, mExport.ctx);
         return true;
      }
      return false;
   }
   // Drops a queued export, or waits for a running one (before a core swap).
   void SettleExport()
   {
      int ignored = 0;
      if (Cancel(mExport))
         return;
      while (mExport.state.load() != 0 && !Poll(mExport, ignored))
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }

   // Runs fn here, on the main thread, under the lock (the audio thread skips
   // the blocks it overlaps). For rare work that allocates.
   int RunLocked(JobFn fn, void* ctx)
   {
      std::lock_guard<std::mutex> lock(mCoreMutex);
      return mCore ? fn(*this, mCore->Api(), mCore->Inst(), ctx) : INT32_MIN;
   }

   void Post(const Cmd& c) { mCommands.Push(c); }
   void InjectDeviceMidi(const Platform::MidiRawMessage& m) { mInjected.Push(m); }
   // Test harness: the caller renders synchronously, so jobs run in place.
   void SetDirectJobs(bool on) { mDirectJobs.store(on); }

   void SetGainDb(float db) { mGain.store(db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f), std::memory_order_relaxed); }
   void SetDevice(uint32_t id, bool active, bool cablePlays, int mapMode)
   {
      // Losing focus, another device or another mapping mode: whatever the
      // controller holds is released (its note-offs would not reach us).
      const uint32_t oldId = mDeviceId.exchange(id, std::memory_order_relaxed);
      const bool wasActive = mDeviceActive.exchange(active, std::memory_order_relaxed);
      const int oldMode = mMapMode.exchange(mapMode, std::memory_order_relaxed);
      if ((wasActive && !active) || oldId != id || oldMode != mapMode)
         mReleaseDevice.store(true, std::memory_order_relaxed);
      mCablePlays.store(cablePlays, std::memory_order_relaxed);
   }
   void SetTable(const SrMapTable* table)
   {
      std::lock_guard<std::mutex> lock(mCoreMutex);
      mHasTable = table != nullptr && table->valid;
      if (mHasTable)
         mTable = *table;
      std::memset(mLastCc, 0xFF, sizeof(mLastCc));
      mReleaseDevice.store(true, std::memory_order_relaxed);
   }

   // Main thread, while no audio callback runs this node (audio stopped, or
   // the node not wired to an output): the firmware keeps running in real
   // time, silently, like the hardware with its volume down, so its screen,
   // presets, panel and MIDI DIAGNOSTICS stay alive.
   void IdleTick()
   {
      const int64_t now = NowMs();
      if (mIdleLastMs == 0 || now - mIdleLastMs > 200)
         mIdleLastMs = now - 16;
      std::lock_guard<std::mutex> lock(mCoreMutex);
      if (!mCore)
         return;
      const SynthRefCoreApi& api = mCore->Api();
      void* inst = mCore->Inst();
      mIdleFrames += (double)(now - mIdleLastMs) * mInfo->sampleRate / 1000.0;
      mIdleLastMs = now;
      Cmd c;
      while (mCommands.Pop(c))
         Execute(api, inst, c);
      DeviceMidi(api, inst);
      int blocks = 0;
      while (mIdleFrames >= mInfo->blockFrames && blocks < 64)
      {
         api.render(inst);
         mIdleFrames -= mInfo->blockFrames;
         ++blocks;
      }
      if (blocks == 64)
         mIdleFrames = 0.0;
      mRs.Reset(); // audio resumes from silence, not from stale input
      if (blocks > 0)
         Publish(api, inst, (int)(blocks * mInfo->blockFrames * mOutRate / mInfo->sampleRate));
   }

   bool FetchSnapshot() { return mSnap.Fetch(); }
   const Snapshot& Snap() const { return mSnap.Front(); }
   const uint16_t* Lcd(uint32_t& revision)
   {
      mLcd.Fetch();
      const LcdFrame& f = mLcd.Front();
      revision = f.revision;
      return f.px.empty() ? nullptr : f.px.data();
   }
   bool PopDiag(Diag& d) { return mDiag.Pop(d); }
   float Peak(int ch) { return mPeak[ch & 1].exchange(0.0f, std::memory_order_relaxed); }
   int64_t LastMidiMs() const { return mLastMidiMs.load(std::memory_order_relaxed); }
   bool AudioLive() const { return NowMs() - mLastProcessMs.load() < 250; }

   // ---- AudioNode ----
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      std::lock_guard<std::mutex> lock(mCoreMutex);
      mOutRate = sampleRate > 0.0 ? sampleRate : 48000.0;
      mRs.Configure(mInfo ? (double)mInfo->sampleRate : 44100.0, mOutRate);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      // Cable removed: notes it started are released on the next block.
      if (mInbox != nullptr && inbox == nullptr)
         mReleasePin.store(true, std::memory_order_relaxed);
      mInbox = inbox;
      mCursor = cursor;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& out) override
   {
      mLastProcessMs.store(NowMs(), std::memory_order_relaxed);
      const int n = out.numFrames;
      float* L = out.numChannels > 0 ? out.channels[0] : nullptr;
      float* R = out.numChannels > 1 ? out.channels[1] : nullptr;
      if (L == nullptr)
         return;

      std::unique_lock<std::mutex> lock(mCoreMutex, std::try_to_lock);
      if (!lock.owns_lock())
      {
         // The main thread is swapping or loading: inputs stay queued for
         // the next block (a note-off must never be dropped).
         Silence(out);
         return;
      }
      if (!mCore)
      {
         DrainInputsSilently(); // under the lock: the cursors are shared with IdleTick
         Silence(out);
         return;
      }
      const SynthRefCoreApi& api = mCore->Api();
      void* inst = mCore->Inst();

      Cmd c;
      while (mCommands.Pop(c))
         Execute(api, inst, c);
      RunPosted(mJob, api, inst);
      RunPosted(mExport, api, inst);
      PinNotes(api, inst);
      DeviceMidi(api, inst);

      // Render whole core blocks until the resampler can fill this block.
      const int channels = (int)mInfo->channels;
      for (int guard = 0; !mRs.CanProduce(n) && guard < 256; ++guard)
         mRs.Push(api.render(inst), (int)mInfo->blockFrames, channels);
      if (!mRs.CanProduce(n))
      {
         // A block larger than the resampler ring can hold: never read stale input.
         Silence(out);
         return;
      }
      float* right = R != nullptr ? R : mScratch;
      if (R == nullptr && n > kScratch)
      {
         Silence(out);
         return;
      }
      mRs.Produce(L, right, n);

      Publish(api, inst, n);
      lock.unlock();

      const float target = mGain.load(std::memory_order_relaxed);
      float peakL = 0.0f, peakR = 0.0f;
      for (int i = 0; i < n; ++i)
      {
         mGainSmoothed += (target - mGainSmoothed) * 0.002f;
         L[i] *= mGainSmoothed;
         right[i] *= mGainSmoothed;
         peakL = std::max(peakL, std::fabs(L[i]));
         peakR = std::max(peakR, std::fabs(right[i]));
      }
      AtomicMax(mPeak[0], peakL);
      AtomicMax(mPeak[1], peakR);
      for (int ch = 2; ch < out.numChannels; ++ch)
         std::memcpy(out.channels[ch], ch % 2 ? right : L, sizeof(float) * n);
   }

private:
   static constexpr int kScratch = 8192;

   // A job handed to the audio thread: 0 idle, 1 posted, 3 running, 2 done.
   struct JobSlot
   {
      std::atomic<int> state { 0 };
      JobFn fn = nullptr;
      void* ctx = nullptr;
      int result = 0;
   };

   // Posts a job, or (no audio running) runs it here and returns false with
   // `result` set.
   bool Begin(JobSlot& slot, JobFn fn, void* ctx, int& result)
   {
      if (mDirectJobs.load() || !AudioLive())
      {
         result = RunLocked(fn, ctx);
         return false;
      }
      slot.fn = fn;
      slot.ctx = ctx;
      slot.state.store(1, std::memory_order_release);
      return true;
   }
   static bool Poll(JobSlot& slot, int& result)
   {
      if (slot.state.load(std::memory_order_acquire) != 2)
         return false;
      result = slot.result;
      slot.state.store(0, std::memory_order_release);
      return true;
   }
   // Takes back a job the audio thread has not started.
   static bool Cancel(JobSlot& slot)
   {
      int expected = 1;
      return slot.state.compare_exchange_strong(expected, 0);
   }
   // Audio thread, under the lock.
   void RunPosted(JobSlot& slot, const SynthRefCoreApi& api, void* inst)
   {
      int expected = 1;
      if (slot.state.load(std::memory_order_acquire) == 1 && slot.state.compare_exchange_strong(expected, 3))
      {
         slot.result = slot.fn(*this, api, inst, slot.ctx);
         slot.state.store(2, std::memory_order_release);
      }
   }

   static unsigned CountTrailingZeros(uint32_t m)
   {
      unsigned n = 0;
      while ((m & 1u) == 0 && n < 32)
      {
         m >>= 1;
         ++n;
      }
      return n;
   }

   static void AtomicMax(std::atomic<float>& a, float v)
   {
      float cur = a.load(std::memory_order_relaxed);
      while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed))
      {
      }
   }

   static void Silence(AudioBuffer& out)
   {
      for (int ch = 0; ch < out.numChannels; ++ch)
         std::memset(out.channels[ch], 0, sizeof(float) * out.numFrames);
   }

   // Without the core, inputs still advance so nothing replays later.
   void DrainInputsSilently()
   {
      if (mInbox != nullptr && mCursor >= 0)
      {
         NoteEvent ev[64];
         while (mInbox->Pop(mCursor, ev, 64) > 0)
         {
         }
      }
      Platform::MidiRawMessage raw[64];
      while (Platform::MidiReadRawSince(mRawCursor, raw, 64) > 0)
      {
      }
   }

   void Execute(const SynthRefCoreApi& api, void* inst, const Cmd& c)
   {
      switch (c.type)
      {
      case kCmdControl: api.control(inst, c.kind, c.id, c.value); break;
      case kCmdMidi: api.midi(inst, c.bytes, c.count); break;
      case kCmdRelease: api.release(inst); break;
      case kCmdPanic:
         api.panic(inst);
         std::memset(mHeldPin, 0xFF, sizeof(mHeldPin));
         std::memset(mHeldDevice, 0xFF, sizeof(mHeldDevice));
         break;
      default: break;
      }
   }

   void Send3(const SynthRefCoreApi& api, void* inst, uint8_t a, uint8_t b, uint8_t c, int count = 3)
   {
      const uint8_t m[3] = { a, b, c };
      api.midi(inst, m, (uint32_t)count);
   }

   // A note for the instrument's keyboard: sent on the channel of the track
   // the keyboard plays, and its note-off follows it there even if the
   // keyboard moved to another track meanwhile.
   void KeyboardNote(const SynthRefCoreApi& api, void* inst, uint8_t* held, int note, int velocity)
   {
      note &= 127;
      if (velocity > 0)
      {
         if (held[note] < 16)
            Send3(api, inst, (uint8_t)(0x80 | held[note]), (uint8_t)note, 0);
         held[note] = (uint8_t)mKeyChannel;
         Send3(api, inst, (uint8_t)(0x90 | mKeyChannel), (uint8_t)note, (uint8_t)std::clamp(velocity, 1, 127));
      }
      else if (held[note] < 16)
      {
         Send3(api, inst, (uint8_t)(0x80 | held[note]), (uint8_t)note, 0);
         held[note] = 0xFF;
      }
   }

   void ReleaseHeld(const SynthRefCoreApi& api, void* inst, uint8_t* held)
   {
      for (int i = 0; i < 128; ++i)
         if (held[i] < 16)
         {
            Send3(api, inst, (uint8_t)(0x80 | held[i]), (uint8_t)i, 0);
            held[i] = 0xFF;
         }
   }

   void PinNotes(const SynthRefCoreApi& api, void* inst)
   {
      if (mReleasePin.exchange(false, std::memory_order_relaxed))
         ReleaseHeld(api, inst, mHeldPin);
      if (mInbox == nullptr || mCursor < 0)
         return;
      NoteEvent ev[64];
      int got = 0;
      while ((got = mInbox->Pop(mCursor, ev, 64)) > 0)
         for (int i = 0; i < got; ++i)
         {
            const NoteEvent& e = ev[i];
            if (e.bendUpdate)
            {
               // Infinite's bends are semitones; the firmware's range is +-2.
               const int v = std::clamp((int)std::lround(8192.0 + e.bendSemitones / 2.0 * 8191.0), 0, 16383);
               Send3(api, inst, (uint8_t)(0xE0 | mKeyChannel), (uint8_t)(v & 127), (uint8_t)(v >> 7));
               continue;
            }
            if (e.note < 0 || e.note > 127)
               continue;
            const int vel = e.isNoteOn ? std::clamp((int)std::lround(e.velocity * 127.0f), 1, 127) : 0;
            KeyboardNote(api, inst, mHeldPin, e.note, vel);
            mLastMidiMs.store(NowMs(), std::memory_order_relaxed);
            Diag d;
            d.size = 3;
            d.bytes[0] = (uint8_t)((vel ? 0x90 : 0x80) | mKeyChannel);
            d.bytes[1] = (uint8_t)e.note;
            d.bytes[2] = (uint8_t)vel;
            d.what = kDiagPin;
            mDiag.Push(d);
         }
   }

   void Control(const SynthRefCoreApi& api, void* inst, int kind, int id, int value) { api.control(inst, (uint32_t)kind, (uint32_t)id, value); }

   void DeviceMidi(const SynthRefCoreApi& api, void* inst)
   {
      if (mReleaseDevice.exchange(false, std::memory_order_relaxed))
      {
         ReleaseHeld(api, inst, mHeldDevice);
         for (uint32_t m = mHeldMappedPads; m != 0; m &= m - 1)
            Control(api, inst, SR_CTL_PAD, (int)CountTrailingZeros(m), 0);
         for (uint32_t m = mHeldMappedButtons; m != 0; m &= m - 1)
            Control(api, inst, SR_CTL_BUTTON, (int)CountTrailingZeros(m), 0);
         mHeldMappedPads = mHeldMappedButtons = 0;
      }
      const uint32_t device = mDeviceId.load(std::memory_order_relaxed);
      const bool active = mDeviceActive.load(std::memory_order_relaxed);
      const bool cablePlays = mCablePlays.load(std::memory_order_relaxed);
      const bool autoMap = mMapMode.load(std::memory_order_relaxed) == SynthRefNode::kMapAuto;
      const SrMapTable* t = mHasTable ? &mTable : nullptr;

      Platform::MidiRawMessage raw[64];
      int got = 0;
      while ((got = ReadDeviceMidi(raw, 64)) > 0)
         for (int i = 0; i < got; ++i)
         {
            const Platform::MidiRawMessage& m = raw[i];
            Diag d;
            d.device = m.device;
            d.size = (uint8_t)m.size;
            std::memcpy(d.bytes, m.bytes, 3);
            if (device == 0 || m.device != device)
               continue; // other devices are not this node's business (not logged)
            if (!active)
            {
               d.what = kDiagIgnoredInactive;
               mDiag.Push(d);
               continue;
            }
            mLastMidiMs.store(NowMs(), std::memory_order_relaxed);
            const int st = m.bytes[0] & 0xF0, ch = m.bytes[0] & 15;
            d.channel = (uint8_t)ch;
            const int d1 = m.bytes[1] & 127, d2 = m.bytes[2] & 127;
            d.what = kDiagDropped;
            if (st == 0x80 || st == 0x90)
            {
               const int vel = st == 0x90 ? d2 : 0;
               const SrMapEntry e = t ? t->note[ch][d1] : SrMapEntry {};
               if (vel == 0 && mHeldDevice[d1] < 16)
               {
                  // A note-off follows its note-on to the keyboard, whatever
                  // the mapping or the pin say now.
                  KeyboardNote(api, inst, mHeldDevice, d1, 0);
                  d.what = kDiagKeyboard;
               }
               else if (autoMap && e.target == kSrPad && e.id < 32)
               {
                  Control(api, inst, SR_CTL_PAD, e.id, vel);
                  mHeldMappedPads = vel ? (mHeldMappedPads | (1u << e.id)) : (mHeldMappedPads & ~(1u << e.id));
                  d.what = kDiagMapped;
               }
               else if (autoMap && e.target == kSrButton && e.id < 32)
               {
                  Control(api, inst, SR_CTL_BUTTON, e.id, vel > 0 ? 1 : 0);
                  mHeldMappedButtons = vel ? (mHeldMappedButtons | (1u << e.id)) : (mHeldMappedButtons & ~(1u << e.id));
                  d.what = kDiagMapped;
               }
               else if (vel > 0 && !cablePlays && (t == nullptr || e.target == kSrNone || e.target == kSrKeyboard) &&
                        (t == nullptr || t->keyboardChannel < 0 || ch == t->keyboardChannel || e.target == kSrKeyboard))
               {
                  KeyboardNote(api, inst, mHeldDevice, d1, vel);
                  d.what = kDiagKeyboard;
               }
               d.target = e.target;
               d.id = e.id;
            }
            else if (st == 0xB0)
            {
               const SrMapEntry e = t ? t->cc[ch][d1] : SrMapEntry {};
               d.target = e.target;
               d.id = e.id;
               if (autoMap && e.target != kSrNone)
               {
                  d.what = kDiagMapped;
                  d.detents = (int8_t)MappedCc(api, inst, e, ch, d1, d2);
               }
               else if (d1 == 64 && (t == nullptr || t->sustainToPedal))
               {
                  Control(api, inst, SR_CTL_PEDAL, 0, d2);
                  d.what = kDiagMapped;
                  d.target = kSrPedal;
                  d.id = 0;
               }
               else if ((d1 == 1 && (t == nullptr || t->passMod)) || d1 == 120 || d1 == 123 || autoMap)
               {
                  // In MIDI LEARN mode other CCs belong to Infinite's MIDI map.
                  Send3(api, inst, (uint8_t)(0xB0 | mKeyChannel), (uint8_t)d1, (uint8_t)d2);
                  d.what = kDiagPassed;
               }
            }
            else if ((st == 0xE0 && (t == nullptr || t->passBend)) || (st == 0xD0 && (t == nullptr || t->passPressure)) ||
                     st == 0xC0 || st == 0xA0)
            {
               const int count = st == 0xD0 || st == 0xC0 ? 2 : 3;
               Send3(api, inst, (uint8_t)(st | mKeyChannel), (uint8_t)d1, (uint8_t)d2, count);
               d.what = kDiagPassed;
            }
            mDiag.Push(d);
         }
   }

   // The controller stream: Infinite's MIDI inputs, then (tests only) messages
   // injected as if a device had sent them.
   int ReadDeviceMidi(Platform::MidiRawMessage* out, int max)
   {
      int n = Platform::MidiReadRawSince(mRawCursor, out, max);
      Platform::MidiRawMessage m;
      while (n < max && mInjected.Pop(m))
         out[n++] = m;
      return n;
   }

   // Returns the encoder detents sent (0 for other targets), for the panel.
   int MappedCc(const SynthRefCoreApi& api, void* inst, const SrMapEntry& e, int ch, int cc, int v)
   {
      int16_t& last = mLastCc[ch][cc];
      if (e.target == kSrEncoder)
      {
         int delta = 0;
         if (e.input == kSrAbsolute)
            delta = last >= 0 ? v - last : 0; // first move only establishes the position
         else
            delta = SrRelativeDelta(e.input, v);
         last = (int16_t)v;
         delta = std::clamp(delta, -127, 127);
         if (delta != 0)
            Control(api, inst, SR_CTL_ENCODER, e.id, delta);
         return delta;
      }
      else if (e.target == kSrFader)
      {
         int value = v;
         if (e.input != kSrAbsolute)
            value = std::clamp((last >= 0 ? last : 64) + SrRelativeDelta(e.input, v), 0, 127);
         last = (int16_t)value;
         Control(api, inst, SR_CTL_FADER, e.id, value);
      }
      else if (e.target == kSrButton && e.id < 32)
      {
         Control(api, inst, SR_CTL_BUTTON, e.id, v >= 64 ? 1 : 0);
         mHeldMappedButtons = v >= 64 ? (mHeldMappedButtons | (1u << e.id)) : (mHeldMappedButtons & ~(1u << e.id));
      }
      else if (e.target == kSrPad && e.id < 32)
      {
         Control(api, inst, SR_CTL_PAD, e.id, v);
         mHeldMappedPads = v ? (mHeldMappedPads | (1u << e.id)) : (mHeldMappedPads & ~(1u << e.id));
      }
      else if (e.target == kSrPedal)
         Control(api, inst, SR_CTL_PEDAL, e.id, v);
      else if (e.target == kSrPressure)
         Control(api, inst, SR_CTL_PRESSURE, e.id, v);
      return 0;
   }

   // State for the panel: LEDs and queries ~every 20 ms of audio, texts
   // ~every 100 ms, the screen whenever the firmware redrew it (<= 60 Hz).
   void Publish(const SynthRefCoreApi& api, void* inst, int frames)
   {
      mSnapCountdown -= frames;
      mLcdCountdown -= frames;
      if (mLcdCountdown <= 0 && !mLcd.Back().px.empty())
      {
         mLcdCountdown = (int)(mOutRate / 60.0);
         uint32_t rev = 0;
         const uint16_t* px = api.lcd(inst, &rev);
         if (px != nullptr && rev != mLcdRevision)
         {
            mLcdRevision = rev;
            LcdFrame& f = mLcd.Back();
            std::memcpy(f.px.data(), px, f.px.size() * sizeof(uint16_t));
            f.revision = rev; // travels with its pixels
            mLcd.Publish();
         }
      }
      if (mSnapCountdown > 0)
         return;
      mSnapCountdown = (int)(mOutRate * 0.02);
      Snapshot& s = mSnap.Back();
      for (int k = 0; k < kQueryKeys; ++k)
         s.q[k] = api.query(inst, (uint32_t)k);
      const int32_t keyCh = s.q[SR_Q_KEY_CHANNEL];
      mKeyChannel = keyCh >= 0 && keyCh < 16 ? keyCh : 0;
      api.leds(inst, s.leds, sizeof(s.leds));
      if (--mTextCountdown <= 0)
      {
         mTextCountdown = 5;
         api.text(inst, SR_TEXT_PROJECT, 0, s.project, sizeof(s.project));
         for (int p = 0; p < 16; ++p)
            api.text(inst, SR_TEXT_PAD_LABEL, (uint32_t)p, s.pads[p], sizeof(s.pads[p]));
         for (int t = 0; t < 8; ++t)
            api.text(inst, SR_TEXT_TRACK_SOURCE, (uint32_t)t, s.sources[t], sizeof(s.sources[t]));
         mTexts = s; // keep texts across the snapshots that skip them
      }
      else
      {
         std::memcpy(s.project, mTexts.project, sizeof(s.project));
         std::memcpy(s.pads, mTexts.pads, sizeof(s.pads));
         std::memcpy(s.sources, mTexts.sources, sizeof(s.sources));
      }
      s.valid = true;
      mSnap.Publish();
   }

   std::mutex mCoreMutex;
   std::unique_ptr<SynthRefCore> mCore;
   const SynthRefCoreInfo* mInfo = nullptr;
   Resampler mRs;
   double mOutRate = 48000.0;
   float mScratch[kScratch] = {};

   Spsc<Cmd, 1024> mCommands;
   Spsc<Diag, 512> mDiag;
   Spsc<Platform::MidiRawMessage, 256> mInjected;
   JobSlot mJob, mExport;
   std::atomic<bool> mDirectJobs { false };
   int64_t mIdleLastMs = 0;
   double mIdleFrames = 0.0;

   std::atomic<float> mGain { 1.0f };
   float mGainSmoothed = 1.0f;
   std::atomic<float> mPeak[2] = { 0.0f, 0.0f };
   std::atomic<int64_t> mLastProcessMs { 0 }, mLastMidiMs { 0 };

   std::atomic<uint32_t> mDeviceId { 0 };
   std::atomic<bool> mDeviceActive { false }, mCablePlays { false }, mReleaseDevice { false }, mReleasePin { false };
   uint32_t mHeldMappedPads = 0, mHeldMappedButtons = 0; // controller holds to release with the device
   std::atomic<int> mMapMode { SynthRefNode::kMapAuto };
   SrMapTable mTable;
   bool mHasTable = false;
   int16_t mLastCc[16][128];
   uint8_t mHeldPin[128], mHeldDevice[128];
   int mKeyChannel = 0;
   unsigned long long mRawCursor = 0;
   NoteEventQueue* mInbox = nullptr;
   int mCursor = -1;

   Triple<Snapshot> mSnap;
   Snapshot mTexts;
   Triple<LcdFrame> mLcd;
   uint32_t mLcdRevision = 0xFFFFFFFFu;
   int mSnapCountdown = 0, mLcdCountdown = 0, mTextCountdown = 0;
};

// ---------------------------------------------------------------------------
// Main-thread half
// ---------------------------------------------------------------------------
namespace
{
   struct StateJob
   {
      std::vector<uint8_t>* out;
   };

   // ctx: the std::vector<uint8_t> to fill (sized to stateBytesMax).
   int SaveStateIntoJob(AudioSynthRefNode&, const SynthRefCoreApi& api, void* inst, void* ctx)
   {
      auto* out = static_cast<std::vector<uint8_t>*>(ctx);
      const uint32_t need = api.save_state(inst, nullptr, 0);
      if (need == 0 || need > out->size())
         return -1;
      const uint32_t n = api.save_state(inst, out->data(), (uint32_t)out->size());
      return n <= out->size() ? (int)n : -1;
   }

   int SaveStateJob(AudioSynthRefNode&, const SynthRefCoreApi& api, void* inst, void* ctx)
   {
      auto* job = static_cast<StateJob*>(ctx);
      const uint32_t need = api.save_state(inst, nullptr, 0);
      if (need == 0 || need > job->out->size())
         return -1;
      return (int)api.save_state(inst, job->out->data(), (uint32_t)job->out->size());
   }

   struct ImportJob
   {
      uint32_t kind, slot;
      const std::vector<uint8_t>* data;
   };

   int ImportDataJob(AudioSynthRefNode&, const SynthRefCoreApi& api, void* inst, void* ctx)
   {
      auto* job = static_cast<ImportJob*>(ctx);
      return api.import_data(inst, job->kind, job->slot, job->data->data(), (uint32_t)job->data->size());
   }

   struct IndexJob
   {
      int index;
      bool factory;
   };

   int LoadIndexJob(AudioSynthRefNode&, const SynthRefCoreApi& api, void* inst, void* ctx)
   {
      auto* job = static_cast<IndexJob*>(ctx);
      return job->factory ? api.load_factory(inst, (uint32_t)job->index) : api.load_program(inst, (uint32_t)job->index);
   }

   struct NamesJob
   {
      std::vector<std::string>* programs;
      std::vector<std::string>* factories;
   };

   int NamesJobFn(AudioSynthRefNode&, const SynthRefCoreApi& api, void* inst, void* ctx)
   {
      auto* job = static_cast<NamesJob*>(ctx);
      char buf[64];
      const int programs = api.query(inst, SR_Q_PROGRAM_COUNT);
      for (int i = 0; programs != SR_QUERY_NONE && i < programs && i < 1024; ++i)
      {
         api.text(inst, SR_TEXT_PROGRAM, (uint32_t)i, buf, sizeof(buf));
         job->programs->push_back(buf); // allocates: run with RunLocked, never as an audio job
      }
      const int factories = api.query(inst, SR_Q_FACTORY_COUNT);
      for (int i = 0; factories != SR_QUERY_NONE && i < factories && i < 256; ++i)
      {
         api.text(inst, SR_TEXT_FACTORY, (uint32_t)i, buf, sizeof(buf));
         job->factories->push_back(buf);
      }
      return 0;
   }

   std::string Lower(std::string s)
   {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
   }
}

SynthRefNode::SynthRefNode()
{
   for (float& k : learnKnob)
      k = 0.5f;
   ReloadProfiles();
}

void (*SynthRefNode::sDeleteTexture)(unsigned int) = nullptr;

SynthRefNode::~SynthRefNode()
{
   const SynthRefNode* self = this;
   gFocusedNode.compare_exchange_strong(self, nullptr);
   if (lcdTexture != 0 && sDeleteTexture != nullptr)
      sDeleteTexture(lcdTexture);
}

int64_t SynthRefNode::TouchAgeMs(int target, int id) const
{
   if (target < 0 || target >= kSrTargets || id < 0 || id >= 32 || mTouch[target][id] == 0)
      return INT64_MAX;
   return NowMs() - mTouch[target][id];
}

void SynthRefNode::EnsureAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSynthRefNode>();
}

AudioNode* SynthRefNode::GetAudioNode()
{
   EnsureAudioNode();
   return mAudioNode.get();
}

bool SynthRefNode::CoreReady() const { return mAudioNode && mAudioNode->HasCore(); }
const SynthRefCoreInfo* SynthRefNode::Info() const { return mAudioNode ? mAudioNode->Info() : nullptr; }

void SynthRefNode::OpenCoreFromState()
{
   EnsureAudioNode();
   mAudioNode->SettleExport();
   mExportBusy = false;
   mOpenedCore = coreId;
   mAppliedState = state;
   mStateRevision = -1;
   std::string error;
   std::unique_ptr<SynthRefCore> core = SynthRefCore::Open(coreId, error);
   if (!core)
   {
      // The saved state stays in `state` untouched for when the core returns.
      mCoreError = error;
      mAudioNode->InstallCore(nullptr);
      mProgramNames.clear();
      mFactoryNames.clear();
      return;
   }
   mCoreError.clear();
   const SynthRefCoreApi& api = core->Api();
   bool loaded = false;
   if (!state.empty())
   {
      std::vector<uint8_t> bytes;
      if (Base64::Decode(state, bytes) && !bytes.empty())
         loaded = api.load_state(core->Inst(), bytes.data(), (uint32_t)bytes.size()) == 0;
      if (!loaded)
         mCoreError = "the saved firmware state was not accepted; started from factory state";
   }
   if (!loaded)
      api.boot(core->Inst(), SR_BOOT_CLEAR);
   else
   {
      // `state` is this revision: nothing to export until the user edits.
      const int32_t rev = api.query(core->Inst(), SR_Q_EDIT_REVISION);
      if (rev != SR_QUERY_NONE)
         mStateRevision = rev;
   }
   mAudioNode->InstallCore(std::move(core));
   mAppliedMapKey.clear();
   RefreshNames();
}

void SynthRefNode::RefreshNames()
{
   mProgramNames.clear();
   mFactoryNames.clear();
   if (!CoreReady())
      return;
   NamesJob job { &mProgramNames, &mFactoryNames };
   mAudioNode->RunLocked(NamesJobFn, &job);
   mProgramOptions.clear();
   mFactoryOptions.clear();
   for (size_t i = 0; i < mProgramNames.size(); ++i)
   {
      char line[48];
      snprintf(line, sizeof(line), "%03d %s", (int)i + 1, mProgramNames[i].c_str());
      mProgramOptions.push_back(line);
   }
   if (!mFactoryNames.empty())
   {
      mFactoryOptions.push_back("factory project...");
      mFactoryOptions.insert(mFactoryOptions.end(), mFactoryNames.begin(), mFactoryNames.end());
   }
}

bool SynthRefNode::SwitchCore(const std::string& id)
{
   if (id == coreId && CoreReady())
      return true;
   coreId = id;
   state.clear(); // another firmware's storage means nothing to this one
   OpenCoreFromState();
   return CoreReady();
}

void SynthRefNode::ResetFirmware()
{
   state.clear();
   OpenCoreFromState();
}

void SynthRefNode::SendControl(int kind, int id, int value)
{
   if (!mAudioNode)
      return;
   Cmd c;
   c.type = kCmdControl;
   c.kind = (uint8_t)kind;
   c.id = (uint16_t)id;
   c.value = value;
   mAudioNode->Post(c);
}

void SynthRefNode::SendMidi(const uint8_t* bytes, int count)
{
   if (!mAudioNode || count < 1 || count > 3)
      return;
   Cmd c;
   c.type = kCmdMidi;
   c.count = (uint8_t)count;
   std::memcpy(c.bytes, bytes, (size_t)count);
   mAudioNode->Post(c);
}

void SynthRefNode::Panic()
{
   if (!mAudioNode)
      return;
   Cmd c;
   c.type = kCmdPanic;
   mAudioNode->Post(c);
}

int SynthRefNode::LoadFactory(int index)
{
   if (!CoreReady())
      return -1;
   IndexJob job { index, true };
   return mAudioNode->RunJob(LoadIndexJob, &job);
}

int SynthRefNode::LoadProgram(int program)
{
   if (!CoreReady())
      return -1;
   IndexJob job { program, false };
   return mAudioNode->RunJob(LoadIndexJob, &job);
}

int SynthRefNode::ImportFile(const std::string& path, int dx7Bank, std::string& message)
{
   const SynthRefCoreInfo* info = Info();
   if (info == nullptr)
   {
      message = "no firmware running";
      return -1;
   }
   std::error_code ec;
   const auto size = fs::file_size(fs::u8path(path), ec);
   if (ec || size == 0 || size > (1u << 20))
   {
      message = ec ? "cannot read " + path : "not a Synth REF file (empty or larger than 1 MB): " + path;
      return -1;
   }
   std::ifstream f(fs::u8path(path), std::ios::binary);
   std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
   if (data.size() != size)
   {
      message = "cannot read " + path;
      return -1;
   }
   const std::string ext = Lower(fs::u8path(path).extension().u8string());
   uint32_t kind = 0, slot = 0;
   const char* what = "";
   if (ext == ".syx" && data.size() == 4104)
   {
      kind = SR_IMPORT_DX7_BANK;
      slot = (uint32_t)std::clamp(dx7Bank, 0, 3);
      what = "DX7 32-voice bank";
   }
   else if (ext == ".syx" && data.size() == 163)
   {
      kind = SR_IMPORT_DX7_VOICE;
      what = "DX7 voice";
   }
   else if (ext == ".syx")
   {
      message = "a .syx must be a DX7 single voice (163 bytes) or 32-voice bank (4104 bytes); this one has " +
                std::to_string(data.size());
      return -1;
   }
   else if (ext == ".fm1")
   {
      kind = SR_IMPORT_PROJECT;
      what = "project";
   }
   else if (ext == ".fm1t")
   {
      kind = SR_IMPORT_TIMBRE;
      what = "timbre";
   }
   else
   {
      message = "unsupported file type " + ext;
      return -1;
   }
   if ((info->importKinds & kind) == 0)
   {
      message = std::string(info->coreName) + " does not import a " + what;
      return -1;
   }
   ImportJob job { kind, slot, &data };
   const int r = mAudioNode->RunJob(ImportDataJob, &job);
   if (r != 0)
   {
      message = std::string(what) + " refused by the firmware (" + std::to_string(r) +
                (r == -4 ? ": select a DX7 track first)" : ")");
      return r;
   }
   message = std::string(what) + " loaded" +
             (kind == SR_IMPORT_DX7_BANK ? " into library bank " + std::to_string(slot + 1) + " (programs " +
                                              std::to_string(slot * 32 + 1) + "-" + std::to_string(slot * 32 + 32) + ")"
                                         : std::string());
   RefreshNames();
   return 0;
}

bool SynthRefNode::ExportProject(const std::string& path, std::string& message)
{
   if (!CoreReady())
   {
      message = "no firmware running";
      return false;
   }
   std::vector<uint8_t> buf(1u << 16);
   struct Ctx
   {
      std::vector<uint8_t>* buf;
   } ctx { &buf };
   const int n = mAudioNode->RunJob(
      [](AudioSynthRefNode&, const SynthRefCoreApi& api, void* inst, void* c) -> int {
         auto* x = static_cast<Ctx*>(c);
         return (int)api.export_project(inst, x->buf->data(), (uint32_t)x->buf->size());
      },
      &ctx);
   if (n <= 0 || (size_t)n > buf.size())
   {
      message = "the firmware did not export a project";
      return false;
   }
   std::ofstream f(fs::u8path(path), std::ios::binary);
   f.write((const char*)buf.data(), n);
   message = f ? "project saved to " + path : "cannot write " + path;
   return (bool)f;
}

bool SynthRefNode::RefreshState()
{
   const SynthRefCoreInfo* info = Info();
   if (info == nullptr)
      return false;
   std::vector<uint8_t> buf(info->stateBytesMax);
   StateJob job { &buf };
   const int n = mAudioNode->RunJob(SaveStateJob, &job);
   if (n <= 0 || (size_t)n > buf.size())
      return false;
   state = Base64::Encode(buf.data(), (size_t)n);
   mAppliedState = state;
   return true;
}

const uint16_t* SynthRefNode::LcdPixels(uint32_t& revision)
{
   if (!mAudioNode)
      return nullptr;
   const SynthRefCoreInfo* info = Info();
   if (info == nullptr || info->lcdFormat != SR_LCD_RGB565)
      return nullptr;
   return mAudioNode->Lcd(revision);
}

int SynthRefNode::Query(int key) const
{
   if (!mAudioNode || key < 0 || key >= kQueryKeys)
      return SR_QUERY_NONE;
   const Snapshot& s = mAudioNode->Snap();
   return s.valid ? s.q[key] : SR_QUERY_NONE;
}

int SynthRefNode::Led(int index) const
{
   if (!mAudioNode || index < 0 || index >= 128)
      return 0;
   return mAudioNode->Snap().leds[index];
}

std::string SynthRefNode::ProjectName() const { return mAudioNode ? std::string(mAudioNode->Snap().project) : std::string(); }
std::string SynthRefNode::PadLabel(int pad) const
{
   return mAudioNode && pad >= 0 && pad < 16 ? std::string(mAudioNode->Snap().pads[pad]) : std::string();
}
std::string SynthRefNode::TrackSource(int track) const
{
   return mAudioNode && track >= 0 && track < 8 ? std::string(mAudioNode->Snap().sources[track]) : std::string();
}

float SynthRefNode::MeterPeak(int ch) const { return mAudioNode ? mAudioNode->Peak(ch) : 0.0f; }
bool SynthRefNode::MidiRecent() const { return mAudioNode && NowMs() - mAudioNode->LastMidiMs() < 150; }
bool SynthRefNode::AudioLive() const { return mAudioNode && mAudioNode->AudioLive(); }

bool SynthRefNode::DeviceActive() const
{
   return !deviceKey.empty() && (deviceLock || gFocusedNode.load() == this);
}

void SynthRefNode::Focus() { gFocusedNode.store(this); }
bool SynthRefNode::Focused() const { return gFocusedNode.load() == this; }

bool SynthRefNode::DeviceConnected() const
{
   if (deviceKey.empty())
      return false;
   for (const auto& d : Platform::MidiDevices())
      if (d.key == deviceKey)
         return true;
   return false;
}

void SynthRefNode::Rescan()
{
   std::string error;
   Platform::MidiRescan(error);
   mDeviceStatus = error.empty() ? "rescanned: " + Platform::MidiDeviceSummary() : "rescan: " + error;
   mAppliedMapKey.clear();
}

void SynthRefNode::ReloadProfiles()
{
   mProfileErrors.clear();
   mProfiles = SrLoadProfiles(mProfileErrors);
   mActiveProfile = nullptr;
   mAppliedMapKey.clear();
}

void SynthRefNode::ApplyMapping()
{
   const SrProfile* p = SrFindProfile(mProfiles, deviceKey, coreId, profileId);
   const std::string key = (p ? p->id : std::string("-")) + "|" + deviceKey + "|" + coreId + "|" + mOpenedCore;
   if (key == mAppliedMapKey)
      return;
   mAppliedMapKey = key;
   mActiveProfile = p;
   mAudioNode->SetTable(p ? &p->table : nullptr);
}

// Keeps the device link alive: the MIDI layer opens every input once and
// shares it, so a controller plugged back in only needs a rescan, done here
// when the OS lists the saved device again (every 2 s, cheap enumeration).
void SynthRefNode::PollDevice()
{
   const double now = (double)NowMs();
   if (now < mNextDevicePoll || deviceKey.empty())
      return;
   mNextDevicePoll = now + 2000.0;
   if (DeviceConnected())
   {
      mDeviceStatus = "connected";
      mAvailableNames.clear();
      return;
   }
   const std::string base = Lower(deviceKey.substr(0, deviceKey.find(" #")));
   std::string list;
   bool listed = false;
   for (const std::string& n : Platform::MidiAvailableInputNames())
   {
      list += n + "|";
      listed = listed || Lower(n) == base;
   }
   if (!listed)
   {
      mAvailableNames = list;
      mDeviceStatus = "not connected - plug it in (the selection is kept)";
      return;
   }
   // Listed again: rescan once per change of the OS list (a replug), never in
   // a loop - a rescan reopens every input of the app - and at most every
   // 5 s across all Synth REF nodes.
   static int64_t lastRescan = 0;
   const int64_t ms = NowMs();
   if (list != mAvailableNames && ms - lastRescan > 5000)
   {
      mAvailableNames = list;
      lastRescan = ms;
      Rescan();
      mDeviceStatus = DeviceConnected() ? "reconnected" : "listed by Windows but could not be opened (another app using it?)";
   }
   else if (list != mAvailableNames)
      mNextDevicePoll = now + 500.0; // try again once the 5 s pass
   else
      mDeviceStatus = "listed by Windows but not open (another app using it?) - close it and press RESCAN";
}

void SynthRefNode::DrainDiagnostics()
{
   Diag d;
   while (mAudioNode->PopDiag(d))
   {
      char line[200];
      const int st = d.bytes[0] & 0xF0;
      // Panel feedback: the virtual control follows the physical one.
      const bool on = st == 0x90 && d.bytes[2] > 0;
      if (d.what == kDiagKeyboard || d.what == kDiagPin)
         midiNoteHeld[d.bytes[1] & 127] = on;
      if (d.what == kDiagMapped && d.target < kSrTargets && d.id < 32)
      {
         mTouch[d.target][d.id] = NowMs();
         if (d.target == kSrEncoder && d.id < 8)
            encoderTurns[d.id] += d.detents;
         if (d.target == kSrPad && d.id < 32)
            midiPads = (st == 0x90 || st == 0xB0) && d.bytes[2] > 0 ? (midiPads | (1u << d.id)) : (midiPads & ~(1u << d.id));
         if (d.target == kSrFader && d.id < 4 && st == 0xB0)
            uiFader[d.id] = d.bytes[2];
         if (d.target == kSrPedal)
            uiPedal = d.bytes[2] >= 64;
      }
      if (d.what == kDiagPassed && st == 0xE0)
         uiFader[4] = d.bytes[2];
      if (d.what == kDiagPassed && st == 0xB0 && d.bytes[1] == 1)
         uiFader[5] = d.bytes[2];
      const char* type = st == 0x90 ? (d.bytes[2] ? "NOTE ON" : "NOTE OFF")
                       : st == 0x80 ? "NOTE OFF"
                       : st == 0xB0 ? "CC"
                       : st == 0xE0 ? "PITCH BEND"
                       : st == 0xD0 ? "PRESSURE"
                       : st == 0xC0 ? "PROGRAM"
                       : st == 0xA0 ? "POLY AT"
                                    : "MSG";
      std::string result;
      switch (d.what)
      {
      case kDiagIgnoredInactive: result = "ignored: this Synth REF is not focused (select it, or lock the device)"; break;
      case kDiagIgnoredDevice: result = "ignored: other device"; break;
      case kDiagMapped:
      {
         std::string name;
         if (const SynthRefCoreInfo* info = Info())
         {
            const int kind = d.target == kSrEncoder ? SR_CTL_ENCODER : d.target == kSrFader ? SR_CTL_FADER
                           : d.target == kSrButton  ? SR_CTL_BUTTON  : d.target == kSrPad   ? SR_CTL_PAD
                           : d.target == kSrPedal   ? SR_CTL_PEDAL   : SR_CTL_PRESSURE;
            for (uint32_t i = 0; i < info->controlCount; ++i)
               if (info->controls[i].kind == kind && info->controls[i].id == d.id && info->controls[i].name)
                  name = info->controls[i].name;
         }
         result = std::string("-> ") + SrTargetName(d.target) + " " + std::to_string(d.id + 1) + (name.empty() ? "" : " (" + name + ")");
         break;
      }
      case kDiagKeyboard: result = "-> keyboard"; break;
      case kDiagPassed: result = "-> passed to the keyboard track"; break;
      case kDiagPin: result = "notes pin -> keyboard"; break;
      default: result = noteInput.IsConnected() && (st == 0x80 || st == 0x90) ? "dropped: the notes pin is cabled, it plays the notes"
                                                                              : "dropped: not in the profile"; break;
      }
      if (d.what == kDiagPin)
         snprintf(line, sizeof(line), "pin  %-10s note %3d vel %3d  %s", type, d.bytes[1], d.bytes[2], result.c_str());
      else if (d.size >= 3 || st == 0xD0 || st == 0xC0)
         snprintf(line, sizeof(line), "ch%-2d %-10s %3d %3d  %s", d.channel + 1, type, d.bytes[1], d.size >= 3 ? d.bytes[2] : 0,
                  result.c_str());
      else
         snprintf(line, sizeof(line), "%-10s  %s", type, result.c_str());
      mLastEvent = line;
      mDiag.push_back(line);
      while (mDiag.size() > 64)
         mDiag.pop_front();
   }
}

void SynthRefNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   EnsureAudioNode();

   // A new firmware choice, a patch load or an undo that changed the saved
   // state: (re)open the core from `state`.
   if (coreId != mOpenedCore || state != mAppliedState)
      OpenCoreFromState();

   volume = std::clamp(volume, -60.0f, 12.0f);
   view = std::clamp(view, 0, 1);
   mapMode = std::clamp(mapMode, 0, 1);
   mAudioNode->SetGainDb(volume);
   ApplyMapping();
   PollDevice();
   mAudioNode->SetDevice(Platform::MidiDeviceIdForKey(deviceKey), DeviceActive(), noteInput.IsConnected(), mapMode);
   if (CoreReady() && !mAudioNode->AudioLive())
      mAudioNode->IdleTick();
   DrainDiagnostics();
   mAudioNode->FetchSnapshot();

   // MIDI LEARN strip: parameter motion (mouse, Infinite's MIDI map, a
   // modulator) becomes the instrument's own control events.
   if (!mLearnPrimed)
   {
      std::copy(std::begin(learnKnob), std::end(learnKnob), mSentKnob);
      std::copy(std::begin(learnFader), std::end(learnFader), mSentFader);
      mLearnPrimed = true;
   }
   if (mapMode == kMapLearn && CoreReady())
   {
      for (int i = 0; i < kLearnKnobs; ++i)
      {
         learnKnob[i] = std::clamp(learnKnob[i], 0.0f, 1.0f);
         const int detents = (int)std::lround((learnKnob[i] - mSentKnob[i]) * 127.0f);
         if (detents != 0)
         {
            SendControl(SR_CTL_ENCODER, i, std::clamp(detents, -127, 127));
            mSentKnob[i] += detents / 127.0f;
         }
      }
      for (int i = 0; i < kLearnFaders; ++i)
      {
         learnFader[i] = std::clamp(learnFader[i], 0.0f, 1.0f);
         if (learnFader[i] != mSentFader[i])
         {
            SendControl(SR_CTL_FADER, i, (int)std::lround(learnFader[i] * 127.0f));
            mSentFader[i] = learnFader[i];
         }
      }
      for (int i = 0; i < kLearnPads; ++i)
         if (learnPadHeld[i] != mSentPad[i])
         {
            SendControl(SR_CTL_PAD, i, learnPadHeld[i] ? 100 : 0);
            mSentPad[i] = learnPadHeld[i];
         }
   }
   else
   {
      std::copy(std::begin(learnKnob), std::end(learnKnob), mSentKnob);
      std::copy(std::begin(learnFader), std::end(learnFader), mSentFader);
   }

   // Keep `state` current so save, undo and copy carry the firmware as it
   // is: re-export when the firmware's storage or project changed and has
   // been still for a moment (an edit burst exports once).
   const int rev = Query(SR_Q_EDIT_REVISION);
   if (mExportBusy)
   {
      int n = 0;
      if (mAudioNode->PollExport(n))
      {
         mExportBusy = false;
         if (n > 0 && (size_t)n <= mExportBuf.size())
         {
            state = Base64::Encode(mExportBuf.data(), (size_t)n);
            mAppliedState = state;
            mStateRevision = mExportRevision;
         }
      }
   }
   else if (CoreReady() && rev != SR_QUERY_NONE && rev != mStateRevision)
   {
      const int64_t now = NowMs();
      if (mStateRevision == -1 && !state.empty())
         mStateRevision = rev; // a state the core rejected: kept as it was
      else
      {
         if (mStatePendingSince == 0)
            mStatePendingSince = now;
         if (now - mStatePendingSince > 300 || state.empty())
         {
            mExportBuf.resize(Info()->stateBytesMax);
            mExportRevision = rev;
            mExportBusy = mAudioNode->BeginExport(SaveStateIntoJob, &mExportBuf);
            mStatePendingSince = 0;
         }
      }
   }
}

void SynthRefNode::VisitParams(ParamVisitor& v)
{
   v.Text("core", coreId);
   v.Float("volume", volume);
   v.Int("view", view);
   v.Text("device", deviceKey);
   v.Bool("lock", deviceLock);
   v.Int("mapmode", mapMode);
   v.Text("profile", profileId);
   char key[16];
   for (int i = 0; i < kLearnKnobs; ++i)
   {
      snprintf(key, sizeof(key), "k%d", i + 1);
      v.Float(key, learnKnob[i]);
   }
   for (int i = 0; i < kLearnFaders; ++i)
   {
      snprintf(key, sizeof(key), "f%d", i + 1);
      v.Float(key, learnFader[i]);
   }
   v.Text("state", state);
}

void SynthRefNode::InjectDeviceMidiForTest(const uint8_t* bytes, int count)
{
   EnsureAudioNode();
   Platform::MidiRawMessage m;
   m.device = Platform::MidiDeviceIdForKey(deviceKey);
   m.size = std::clamp(count, 1, 3);
   for (int i = 0; i < m.size; ++i)
      m.bytes[i] = bytes[i];
   mAudioNode->InjectDeviceMidi(m);
}

bool SynthRefNode::RenderForTest(float* left, float* right, int frames, double sampleRate, int block)
{
   EnsureAudioNode();
   mAudioNode->SetDirectJobs(true);
   if (coreId != mOpenedCore || state != mAppliedState)
      OpenCoreFromState();
   if (!CoreReady())
      return false;
   AudioSynthRefNode& a = *mAudioNode;
   if (a.preparedForSampleRate != sampleRate)
   {
      a.PrepareToPlay(sampleRate, block);
      a.preparedForSampleRate = sampleRate;
   }
   int done = 0;
   while (done < frames)
   {
      const int n = std::min(std::max(1, block), frames - done);
      float* ch[2] = { left + done, right + done };
      AudioBuffer out;
      out.channels = ch;
      out.numChannels = 2;
      out.numFrames = n;
      a.ProcessBlock(nullptr, 0, out);
      done += n;
   }
   mAudioNode->FetchSnapshot();
   return true;
}
