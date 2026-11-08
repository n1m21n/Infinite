#include "plugin/engine/HostAudio.h"

#include "audio/AudioEngine.h"
#include "core/Transport.h"
#include "platform/Platform.h"
#include "platform/common/Ipc.h"
#include "plugin/engine/PluginView.h"
#include "plugin/ipc/PluginFrames.h"
#include "plugin/ipc/PluginIpc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

namespace PluginEngine
{
   namespace
   {
      constexpr uint32_t kEventGrid = 32; // MIDI lands on a 32-sample grid (8.2)
      constexpr uint64_t kWaitNs = 100ull * 1000ull * 1000ull;

      PluginIpc::EngineSide gSide;
      Options gOpt;
      std::atomic<bool> gActive { false };
      std::thread gThread;
      std::atomic<bool> gStop { false };
      std::atomic<Platform::AudioRenderCallback> gCallback { nullptr };
      std::atomic<void*> gUser { nullptr };
      std::atomic<uint64_t> gBlocks { 0 }, gJumps { 0 }, gRenders { 0 };
      std::atomic<float> gPeak { 0.0f };

      // The segment being rendered (host audio thread only; Audio In reads it on the same thread).
      const float* gIn[PluginIpc::kInChannels] = {};
      uint32_t gInFrames = 0;
      bool gHasSidechain = false;

      float gBypassGain = 0.0f; // 0 = processing, 1 = bypassed; ramps one block per change

      // ---- host slots (audio thread unless noted) ----
      constexpr uint32_t kSlotSub = 32;        // smoothing sub-block (samples)
      constexpr double kSlotSmoothSec = 0.010; // one-pole time constant
      constexpr uint64_t kParkTimeoutNs = 500ull * 1000ull * 1000ull;
      float gSlotTarget[kSlotCount] = {}, gSlotCur[kSlotCount] = {};
      uint32_t gSlotValid = 0;
      uint64_t gSamplePos = 0;    // samples handled since start (all blocks)
      uint64_t gSlotPos = 0;      // smoothing has run up to here
      uint32_t gGrid = 800;       // snapshot grid (samples), set from the rate at Init
      float gServed[kSlotCount] = {}; // last snapshot the main thread was parked on
      uint32_t gServedValid = 0;
      // Main -> audio.
      std::atomic<uint32_t> gBoundMask { 0 }, gSteppedMask { 0 };
      // Snapshot seqlock (audio writes, main reads).
      std::atomic<uint32_t> gSnapSeq { 0 };
      std::atomic<float> gSnapVal[kSlotCount];
      std::atomic<uint32_t> gSnapValid { 0 };
      // Park handshake: audio raises gParkReq, main serves it (gParkServed) and acks it on its next
      // frame (gParkAck).
      std::atomic<uint64_t> gParkReq { 0 }, gParkAck { 0 };
      std::atomic<uint64_t> gParks { 0 }, gParkTimeouts { 0 };

      void PublishSnapshot()
      {
         gSnapSeq.fetch_add(1, std::memory_order_acq_rel);
         for (int i = 0; i < kSlotCount; i++)
            gSnapVal[i].store(gSlotCur[i], std::memory_order_relaxed);
         gSnapValid.store(gSlotValid, std::memory_order_relaxed);
         gSnapSeq.fetch_add(1, std::memory_order_acq_rel);
      }

      // Runs slot smoothing from gSlotPos to block offset `to` (block starts at gSamplePos),
      // applying the block's points (sorted by offset) as it passes them and publishing a snapshot
      // at every grid point it lands on.
      void SlotsRunTo(uint32_t to, const PluginIpc::ParamPoint* pts, uint32_t numPts, uint32_t& pi)
      {
         const double tau = kSlotSmoothSec * gSide.SampleRate();
         const uint64_t end = gSamplePos + to;
         const uint32_t stepped = gSteppedMask.load(std::memory_order_relaxed);
         while (true)
         {
            const uint32_t off = (uint32_t)(gSlotPos - gSamplePos);
            for (; pi < numPts && pts[pi].offset <= off; pi++)
            {
               const int s = pts[pi].slot;
               if (s >= kSlotCount)
                  continue;
               const float v = std::clamp(pts[pi].value, 0.0f, 1.0f);
               gSlotTarget[s] = v;
               if (!(gSlotValid & (1u << s)))
                  gSlotCur[s] = v; // the first value lands at once
               gSlotValid |= 1u << s;
            }
            if (gSlotPos % gGrid == 0)
               PublishSnapshot();
            if (gSlotPos >= end)
               break;
            uint64_t next = std::min<uint64_t>(end, (gSlotPos / kSlotSub + 1) * kSlotSub);
            next = std::min<uint64_t>(next, (gSlotPos / gGrid + 1) * gGrid);
            if (pi < numPts)
               next = std::min<uint64_t>(next, std::max<uint64_t>(gSlotPos + 1, gSamplePos + pts[pi].offset));
            const double n = (double)(next - gSlotPos);
            const float a = (float)std::exp(-n / tau);
            for (int s = 0; s < kSlotCount; s++)
            {
               if (!(gSlotValid & (1u << s)) || gSlotCur[s] == gSlotTarget[s])
                  continue;
               if (stepped & (1u << s))
                  gSlotCur[s] = gSlotTarget[s];
               else
               {
                  gSlotCur[s] = gSlotTarget[s] + (gSlotCur[s] - gSlotTarget[s]) * a;
                  if (std::fabs(gSlotCur[s] - gSlotTarget[s]) < 1e-6f)
                     gSlotCur[s] = gSlotTarget[s];
               }
            }
            gSlotPos = next;
         }
      }

      // Offline, at a grid point, when a bound slot differs from what the main thread last saw:
      // wait until a frame has applied this snapshot and cooked (README 8.2, offline lock).
      void MaybePark()
      {
         const uint32_t bound = gBoundMask.load(std::memory_order_relaxed);
         const uint32_t valid = gSlotValid & bound;
         bool changed = (valid & ~gServedValid) != 0;
         for (int s = 0; s < kSlotCount && !changed; s++)
            if ((valid & (1u << s)) && gSlotCur[s] != gServed[s])
               changed = true;
         if (!changed)
            return;
         std::memcpy(gServed, gSlotCur, sizeof gServed);
         gServedValid = gSlotValid;
         const uint64_t req = gParkReq.load(std::memory_order_relaxed) + 1;
         gParkReq.store(req, std::memory_order_release);
         const uint64_t t0 = Ipc::MonotonicNs();
         while (gParkAck.load(std::memory_order_acquire) < req)
         {
            if (Ipc::MonotonicNs() - t0 > kParkTimeoutNs || gStop.load(std::memory_order_relaxed) || gSide.QuitRequested())
            {
               gParkTimeouts.fetch_add(1, std::memory_order_relaxed);
               gParkAck.store(req, std::memory_order_release); // a served frame that comes late is ignored
               return;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
         }
         gParks.fetch_add(1, std::memory_order_relaxed);
      }

      void InjectEvent(const PluginIpc::Event& e, bool bypassed)
      {
         const uint8_t kind = e.status & 0xF0;
         if (kind < 0x80 || kind == 0xF0)
            return;
         // While bypassed only releases go through, so nothing sticks and nothing new starts.
         if (bypassed && !(kind == 0x80 || (kind == 0x90 && e.data2 == 0) || kind == 0xB0))
            return;
         const unsigned char bytes[3] = { e.status, (unsigned char)(e.data1 & 0x7F), (unsigned char)(e.data2 & 0x7F) };
         const size_t len = (kind == 0xC0 || kind == 0xD0) ? 2 : 3;
         Platform::MidiInjectBytes(bytes, len, Platform::kMidiHostDevice);
      }

      void RenderSegment(Platform::AudioRenderCallback cb, void* user, const PluginIpc::BlockView& v, uint32_t pos, uint32_t n)
      {
         float* out[2] = { v.output[0] + pos, v.output[1] + pos };
         std::memset(out[0], 0, n * sizeof(float));
         std::memset(out[1], 0, n * sizeof(float));
         for (uint32_t c = 0; c < PluginIpc::kInChannels; c++)
            gIn[c] = v.input[c] ? v.input[c] + pos : nullptr;
         gInFrames = n;
         cb(out, 2, (int)n, user);
         gInFrames = 0;
         gRenders.fetch_add(1, std::memory_order_relaxed);
         float peak = gPeak.load(std::memory_order_relaxed);
         for (uint32_t i = 0; i < n; i++)
            peak = std::max(peak, std::fabs(out[0][i]));
         gPeak.store(peak, std::memory_order_relaxed);
      }

      void HandleBlock(const PluginIpc::BlockView& v)
      {
         const PluginIpc::BlockIn& b = *v.in;
         const uint32_t frames = v.frames;
         Platform::AudioRenderCallback cb = gCallback.load(std::memory_order_acquire);
         void* user = gUser.load(std::memory_order_acquire);

         const PluginIpc::Transport& t = b.transport;
         Transport::HostTime ht;
         ht.playing = (t.flags & PluginIpc::kPlaying) != 0;
         ht.tempoValid = (t.flags & PluginIpc::kTempoValid) != 0;
         ht.bpm = t.tempo;
         ht.ppqValid = (t.flags & PluginIpc::kPpqValid) != 0;
         ht.ppq = t.ppq;
         ht.sigNum = t.sigNum;
         ht.sigDen = t.sigDen;
         if (Transport::Instance().FollowHost(ht))
            gJumps.fetch_add(1, std::memory_order_relaxed);

         // Slot points in time order (the shell writes them parameter by parameter).
         PluginIpc::ParamPoint pts[PluginIpc::kMaxParamPoints];
         const uint32_t numPts = std::min(b.numParams, PluginIpc::kMaxParamPoints);
         std::copy(b.params, b.params + numPts, pts);
         std::stable_sort(pts, pts + numPts, [](const PluginIpc::ParamPoint& x, const PluginIpc::ParamPoint& y) { return x.offset < y.offset; });
         uint32_t pi = 0;

         gHasSidechain = (b.flags & PluginIpc::kSidechain) != 0;
         const bool bypass = (b.flags & PluginIpc::kBypass) != 0;
         const float g0 = gBypassGain, g1 = bypass ? 1.0f : 0.0f;
         gBypassGain = g1;
         const bool render = cb != nullptr && !(g0 >= 1.0f && g1 >= 1.0f);

         if (!render)
         {
            for (uint32_t e = 0; e < b.numEvents; e++)
               InjectEvent(b.events[e], true);
            std::memset(v.output[0], 0, frames * sizeof(float));
            std::memset(v.output[1], 0, frames * sizeof(float));
         }
         else
         {
            // With a Macro bound, segments also end on the snapshot grid, so offline the engine can
            // park between them; unbound, segmentation stays as it was.
            const bool bound = gBoundMask.load(std::memory_order_relaxed) != 0;
            const bool lock = bound && (b.flags & PluginIpc::kOffline) != 0;
            uint32_t pos = 0, ev = 0;
            while (pos < frames)
            {
               while (ev < b.numEvents && std::min(b.events[ev].offset & ~(kEventGrid - 1), frames - 1) <= pos)
                  InjectEvent(b.events[ev++], bypass);
               if (bound)
               {
                  SlotsRunTo(pos, pts, numPts, pi);
                  if (lock && (gSamplePos + pos) % gGrid == 0)
                     MaybePark();
               }
               uint32_t end = std::min(frames, pos + (uint32_t)kAudioMaxBlockFrames);
               if (ev < b.numEvents)
                  end = std::min(end, b.events[ev].offset & ~(kEventGrid - 1));
               if (bound)
                  end = std::min<uint64_t>(end, ((gSamplePos + pos) / gGrid + 1) * gGrid - gSamplePos);
               RenderSegment(cb, user, v, pos, end - pos);
               pos = end;
            }
            while (ev < b.numEvents)
               InjectEvent(b.events[ev++], bypass); // offsets past the block (malformed host data)
         }
         SlotsRunTo(frames, pts, numPts, pi);
         gSamplePos += frames;

         // Bypass: FX passes the main input, Instrument goes silent; a change ramps over the block.
         if (g0 > 0.0f || g1 > 0.0f)
         {
            for (int c = 0; c < 2; c++)
            {
               const float* dry = gOpt.fx ? v.input[c] : nullptr;
               float* o = v.output[c];
               for (uint32_t i = 0; i < frames; i++)
               {
                  const float g = g0 + (g1 - g0) * ((float)(i + 1) / (float)frames);
                  o[i] = o[i] * (1.0f - g) + (dry ? dry[i] * g : 0.0f);
               }
            }
         }
         gBlocks.fetch_add(1, std::memory_order_relaxed);
      }

      void ThreadMain()
      {
         Ipc::SetThreadRealtime(gSide.SampleRate(), gSide.MaxBlock());
         gSide.MarkAttached();
         PluginIpc::BlockView v;
         while (!gStop.load(std::memory_order_relaxed))
         {
            if (!gSide.NextBlock(&v, kWaitNs))
            {
               if (gSide.QuitRequested())
                  break;
               continue;
            }
            HandleBlock(v);
            gSide.Complete(v);
         }
      }

      bool HostOpen(Platform::AudioRenderCallback callback, void* userData, double& outSampleRate, std::string& outError)
      {
         if (!gActive.load())
         {
            outError = "Plugin host not connected";
            return false;
         }
         gCallback.store(callback, std::memory_order_release);
         gUser.store(userData, std::memory_order_release);
         outSampleRate = gSide.SampleRate();
         return true;
      }

      void HostClose()
      {
         // The thread keeps serving the shell (silence) so it never sees a hang while the engine
         // restarts its topology; only the callback goes away.
         gCallback.store(nullptr, std::memory_order_release);
         gUser.store(nullptr, std::memory_order_release);
      }

      int HostBufferFrames() { return (int)gSide.MaxBlock(); }

      const Platform::AudioDeviceOverride kHostDevice { &HostOpen, &HostClose, &HostBufferFrames };
   }

   bool ParseArgs(int argc, char** argv, Options* out)
   {
      for (int i = 1; i < argc; i++)
      {
         if (std::strcmp(argv[i], "--plugin-engine") != 0)
            continue;
         if (i + 2 >= argc)
            return false;
         Options o;
         o.shmName = argv[i + 1];
         o.parentPid = std::strtoll(argv[i + 2], nullptr, 10);
         for (int j = i + 3; j < argc; j++)
         {
            if (std::strcmp(argv[j], "--fx") == 0)
               o.fx = true;
            else if (std::strcmp(argv[j], "--state") == 0)
               o.initialState = true;
            else if (argv[j][0] != '-' && o.patchPath.empty())
               o.patchPath = argv[j];
         }
         // Absolute now: glfwInit moves the working directory into the app bundle on macOS.
         if (!o.patchPath.empty())
         {
            std::error_code ec;
            const std::filesystem::path abs = std::filesystem::absolute(o.patchPath, ec);
            if (!ec)
               o.patchPath = abs.string();
         }
         *out = o;
         return true;
      }
      return false;
   }

   bool Init(const Options& opt, std::string& outError)
   {
      gOpt = opt;
      if (!gSide.Open(opt.shmName))
      {
         outError = "Could not open plugin shared memory " + opt.shmName;
         return false;
      }
      if (opt.parentPid > 0)
         Ipc::ExitWhenParentDies(opt.parentPid);
      Platform::PluginModeFlag().store(true);
      Platform::AudioDeviceOverrideRef() = &kHostDevice;
      // The host is always the MIDI source here: list it (kMidiHostDevice) from the start, so CC
      // bindings loaded with a patch reattach to it without anyone pressing Learn.
      std::string midiError;
      Platform::MidiStart(midiError);
      gGrid = std::max<uint32_t>(kSlotSub, (uint32_t)std::lround(gSide.SampleRate() / 60.0 / kSlotSub) * kSlotSub);
      gActive.store(true);
      gThread = std::thread(&ThreadMain);
      gThread.detach(); // lives until quit; the process exits right after
      return true;
   }

   bool Active() { return gActive.load(std::memory_order_relaxed); }
   bool IsFx() { return gOpt.fx; }
   bool QuitRequested()
   {
      if (!gActive.load() || !gSide.QuitRequested())
         return false;
      static bool written = false;
      if (!written)
      {
         written = true;
         if (const char* path = std::getenv("INFINITE_PLUGIN_STATS"))
            if (FILE* f = std::fopen(path, "w"))
            {
               std::fprintf(f, "blocks %llu jumps %llu renders %llu peak %g parks %llu parkTimeouts %llu frames %llu readbackUs %llu\n", (unsigned long long)gBlocks.load(),
                            (unsigned long long)gJumps.load(), (unsigned long long)gRenders.load(), (double)gPeak.load(),
                            (unsigned long long)gParks.load(), (unsigned long long)gParkTimeouts.load(),
                            (unsigned long long)PluginView::FramesPublished(), (unsigned long long)PluginView::MeanReadbackUs());
               std::fclose(f);
            }
      }
      return true;
   }

   namespace
   {
      bool gStarted = false;
      std::string gStartError;
      int gFramesSinceStart = 0;
      SaveStateFn gSaveState = nullptr;
      LoadStateFn gLoadState = nullptr;

      void HandleMessage(const PluginIpc::Message& m)
      {
         if (m.type == kMsgGetState)
         {
            const std::string text = gSaveState ? gSaveState() : std::string();
            gSide.Messages().Send(kMsgState, m.id, text.data(), text.size(), 2000);
         }
         else if (m.type == kMsgSetState)
         {
            std::string error;
            const std::string text(m.data.begin(), m.data.end());
            if (!gLoadState)
               error = "no state handler";
            else
               gLoadState(text, error);
            gSide.Messages().Send(kMsgStateLoaded, m.id, error.data(), error.size(), 2000);
            // Ready again once the new graph has settled, so an offline render waits for it.
            SetStarted(gStartError);
         }
         else if (m.type == PluginFrames::kMsgView || m.type == PluginFrames::kMsgViewInput || m.type == PluginFrames::kMsgPopout)
            PluginView::HandleMessage(m.type, m.data.data(), m.data.size());
      }
   }

   void SetStateHandlers(SaveStateFn save, LoadStateFn load)
   {
      gSaveState = save;
      gLoadState = load;
   }

   bool TakeInitialState(std::string& text, int timeoutMs)
   {
      if (!gActive.load())
         return false;
      const uint64_t t0 = Ipc::MonotonicNs();
      PluginIpc::Message m;
      while (Ipc::MonotonicNs() - t0 < uint64_t(timeoutMs) * 1000000ull && !gSide.QuitRequested())
      {
         if (!gSide.Messages().Receive(&m, 50))
            continue;
         if (m.type == kMsgSetState)
         {
            text.assign(m.data.begin(), m.data.end());
            gSide.Messages().Send(kMsgStateLoaded, m.id, nullptr, 0, 2000);
            return true;
         }
         HandleMessage(m); // a get before the patch exists answers with the empty patch
      }
      return false;
   }

   void SetStarted(const std::string& error)
   {
      gStarted = true;
      gStartError = error;
      gFramesSinceStart = 0;
   }

   namespace
   {
      uint64_t gParkServed = 0; // main thread: park request this frame serves (acked next frame)
      int gFramesSinceServed = 1000;
   }

   void BeginSlotFrame(SlotFrame* out)
   {
      *out = SlotFrame {};
      if (!gActive.load())
         return;
      // Offline lock running: the audio thread renders one grid step in well under a frame, so a
      // short wait lets nearly every frame serve one.
      if (gFramesSinceServed <= 2)
      {
         const uint64_t t0 = Ipc::MonotonicNs();
         while (gParkReq.load(std::memory_order_acquire) <= gParkAck.load(std::memory_order_acquire) &&
                Ipc::MonotonicNs() - t0 < 10ull * 1000000ull)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      const uint64_t req = gParkReq.load(std::memory_order_acquire);
      if (req > gParkAck.load(std::memory_order_acquire))
      {
         gParkServed = req;
         out->served = true;
         gFramesSinceServed = 0;
         Transport::Instance().SetFramePinned(true, (double)gGrid / gSide.SampleRate());
      }
      else
         gFramesSinceServed++;
      for (int attempt = 0; attempt < 8; attempt++)
      {
         const uint32_t s0 = gSnapSeq.load(std::memory_order_acquire);
         if (s0 & 1)
         {
            std::this_thread::yield();
            continue;
         }
         for (int i = 0; i < kSlotCount; i++)
            out->values[i] = gSnapVal[i].load(std::memory_order_relaxed);
         out->validMask = gSnapValid.load(std::memory_order_relaxed);
         if (gSnapSeq.load(std::memory_order_acquire) == s0)
            break;
      }
   }

   void SetSlotBindings(uint32_t boundMask, uint32_t steppedMask)
   {
      gSteppedMask.store(steppedMask, std::memory_order_relaxed);
      gBoundMask.store(boundMask, std::memory_order_relaxed);
   }

   bool SlotFramePublishes() { return gFramesSinceServed > 2; }

   bool SendToShell(uint32_t type, const void* data, size_t size, int timeoutMs)
   {
      return gActive.load() && gSide.Messages().Send(type, 0, data, size, timeoutMs);
   }

   void SendSlotEdit(SlotEditKind kind, int slot, float value)
   {
      if (!gActive.load() || slot < 0 || slot >= kSlotCount)
         return;
      const SlotEdit e { (uint32_t)kind, (uint32_t)slot, value };
      gSide.Messages().Send(kMsgSlotEdit, 0, &e, sizeof e, 200);
   }

   void SendSlotNames(const std::string (&names)[kSlotCount])
   {
      if (!gActive.load())
         return;
      std::string payload;
      for (const std::string& n : names)
      {
         payload += n.substr(0, 127);
         payload.push_back('\0');
      }
      gSide.Messages().Send(kMsgSlotNames, 0, payload.data(), payload.size(), 1000);
   }

   uint64_t SlotParks() { return gParks.load(); }
   uint64_t SlotParkTimeouts() { return gParkTimeouts.load(); }

   void MainFrameTick()
   {
      if (!gActive.load())
         return;
      // The frame that served a grid point has cooked: let the audio thread go on.
      if (gParkServed > 0)
      {
         gParkAck.store(std::max(gParkAck.load(std::memory_order_relaxed), gParkServed), std::memory_order_release);
         gParkServed = 0;
         Transport::Instance().SetFramePinned(false);
      }
      PluginIpc::Message m;
      while (gSide.Messages().Receive(&m, 0))
         HandleMessage(m);
      if (!gStarted || gFramesSinceStart > 3)
         return;
      if (++gFramesSinceStart == 3)
         gSide.Messages().Send(kMsgReady, 0, gStartError.data(), gStartError.size(), 1000);
   }

   int ReadHostInput(int bus, float* const* out, int frames, int channelOffset, bool mono)
   {
      if (gInFrames == 0 || bus < 0 || bus > 1 || (bus == 1 && !gHasSidechain))
         return 0;
      const int n = std::min(frames, (int)gInFrames);
      const float* l = gIn[bus * 2];
      const float* r = gIn[bus * 2 + 1];
      if (mono)
         l = r = (channelOffset >= 1 ? r : l);
      if (!l || !r)
         return 0;
      std::memcpy(out[0], l, n * sizeof(float));
      std::memcpy(out[1], r, n * sizeof(float));
      return n;
   }

   uint64_t BlocksHandled() { return gBlocks.load(); }
   uint64_t JumpsSeen() { return gJumps.load(); }
}
