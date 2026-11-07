#include "plugin/engine/HostAudio.h"

#include "audio/AudioEngine.h"
#include "core/Transport.h"
#include "platform/Platform.h"
#include "platform/common/Ipc.h"
#include "plugin/ipc/PluginIpc.h"

#include <algorithm>
#include <atomic>
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
         Platform::MidiInjectBytes(bytes, len, 0);
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
            uint32_t pos = 0, ev = 0;
            while (pos < frames)
            {
               while (ev < b.numEvents && std::min(b.events[ev].offset & ~(kEventGrid - 1), frames - 1) <= pos)
                  InjectEvent(b.events[ev++], bypass);
               uint32_t end = std::min(frames, pos + (uint32_t)kAudioMaxBlockFrames);
               if (ev < b.numEvents)
                  end = std::min(end, b.events[ev].offset & ~(kEventGrid - 1));
               RenderSegment(cb, user, v, pos, end - pos);
               pos = end;
            }
            while (ev < b.numEvents)
               InjectEvent(b.events[ev++], bypass); // offsets past the block (malformed host data)
         }

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
               std::fprintf(f, "blocks %llu jumps %llu renders %llu peak %g\n", (unsigned long long)gBlocks.load(),
                            (unsigned long long)gJumps.load(), (unsigned long long)gRenders.load(), (double)gPeak.load());
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
   }

   void SetStarted(const std::string& error)
   {
      gStarted = true;
      gStartError = error;
      gFramesSinceStart = 0;
   }

   void MainFrameTick()
   {
      if (!gActive.load() || !gStarted || gFramesSinceStart > 3)
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
