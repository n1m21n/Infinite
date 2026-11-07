// The Infinite plugin shell, VST3 side (docs/plans/vst-au/README.md 8.2-8.4, step 1.5).
//
// One source, two builds: INFINITE_PLUGIN_FX=1 gives "Infinite FX" (audio effect: main stereo in,
// sidechain, MIDI in, stereo out), 0 gives "Infinite Instrument" (MIDI in, sidechain, stereo out).
//
//   host  setActive(true)  -> spawn `Infinite --plugin-engine <shm> <ppid> [--fx]` (lazy: a scan never spawns)
//         process(block)   -> host audio + MIDI + transport + slot points -> PluginIpc::ShellSide
//                          <- engine output of the block before (latency = max block)
//         terminate()      -> quit, wait 500 ms, kill
//         getState()       -> header + the engine's patch (.inf text), compressed; the last known
//                             patch when the engine is not running, not ready or not answering (2 s)
//         setState()       -> stored; sent to the engine now, or as its first message when it starts
//
// Parameters (fixed forever: hosts cache them per plugin ID):
//   0..31       Slot 01..Slot 32: drive the Macro nodes whose "Host slot" names them (2.2); a bound
//               slot takes the Macro's name (kParamTitlesChanged), and turning the Macro in Infinite
//               writes the slot back as a begin/perform/end edit (engine -> processor -> IMessage on
//               the UI thread -> controller). Only slots the host has set (automation, an edit,
//               a saved project: "slotsSet") are sent to the engine; a Macro bound to an unset slot
//               gives the slot its own value instead (a sync: setParamNormalized, no automation write)
//   1000        Bypass (the engine fades; FX passes the input, Instrument goes silent)
//   2000..2127  MIDI CC 0..127   hidden, omni: VST3 hands CC to plugins only as parameters
//   2128        Pitch bend       (IMidiMapping), turned back into MIDI for the engine here
//   2129        Channel pressure
#include "plugin/ipc/PluginIpc.h"
#include "platform/common/Ipc.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/common/commonstringconvert.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "miniz.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef INFINITE_PLUGIN_FX
#define INFINITE_PLUGIN_FX 0
#endif
#ifndef INFINITE_PLUGIN_VERSION
#define INFINITE_PLUGIN_VERSION "0.0.0"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace InfinitePlugin
{
   constexpr bool kFx = INFINITE_PLUGIN_FX != 0;

#if INFINITE_PLUGIN_FX
   static const FUID kProcessorUID(0x6A1F3C52, 0x9B0E4D17, 0xA3C85E21, 0x4F7D9B60);
   static const FUID kControllerUID(0x2C8E71A4, 0x5D3B4F09, 0x8E16A7C3, 0xB04F2D95);
   #define INFINITE_PLUGIN_NAME "Infinite FX"
   #define INFINITE_PLUGIN_CATEGORY "Fx"
#else
   static const FUID kProcessorUID(0x91D4B7E3, 0x0F6A4C28, 0xB5E39D74, 0x1A82C6F0);
   static const FUID kControllerUID(0x47B20E9D, 0xC81F4A63, 0x9D075B2E, 0x63F1A8C4);
   #define INFINITE_PLUGIN_NAME "Infinite Instrument"
   #define INFINITE_PLUGIN_CATEGORY "Instrument|Synth"
#endif

   constexpr ParamID kSlotFirst = 0;
   constexpr int32 kSlotCount = 32;
   constexpr ParamID kBypassId = 1000;
   constexpr ParamID kMidiCcFirst = 2000;        // + cc number 0..127
   constexpr ParamID kMidiPitchBend = 2128;
   constexpr ParamID kMidiChannelPressure = 2129;
   constexpr ParamID kMidiLast = kMidiChannelPressure;

   constexpr uint32 kStateMagic = 0x53464E49; // "INFS"
   constexpr int kStateVersion = 1;           // header "v": bumped only when the header changes

   // Message types (PluginEngine::MessageType in the engine).
   constexpr uint32_t kMsgReady = 1;
   constexpr uint32_t kMsgState = 2;
   constexpr uint32_t kMsgStateLoaded = 3;
   constexpr uint32_t kMsgSlotEdit = 4;  // payload: SlotEdit
   constexpr uint32_t kMsgSlotNames = 5; // payload: 32 '\0'-terminated UTF-8 names
   constexpr uint32_t kMsgGetState = 101;
   constexpr uint32_t kMsgSetState = 102;

   // ---------------------------------------------------------------------------------------
   // Plugin state (README 8.3): what a host project stores for one instance.
   //
   //   u32 "INFS" | u32 n | header JSON (n bytes) | u32 patch bytes | u32 compressed bytes | zlib
   //
   // header: {"format":"infinite-plugin-state","v":1,"appVersion":..,"build":"instrument"|"fx",
   //          "latencyMode":"block","bypass":..,"slotsSet":<bit mask>,"slots":[32 values]}
   // The patch is the .inf text the app saves; empty = the build's starting patch. The licence key
   // is never in here (8.5).
   struct State
   {
      bool bypass = false;
      uint32_t slotsSet = 0; // slots the host has a value for
      float slots[kSlotCount] = {};
      std::string patch;
   };

   void PutU32(std::vector<uint8_t>& b, uint32_t v)
   {
      for (int i = 0; i < 4; i++)
         b.push_back(uint8_t(v >> (8 * i)));
   }

   bool GetU32(const std::vector<uint8_t>& b, size_t& pos, uint32_t& v)
   {
      if (pos + 4 > b.size())
         return false;
      v = uint32_t(b[pos]) | uint32_t(b[pos + 1]) << 8 | uint32_t(b[pos + 2]) << 16 | uint32_t(b[pos + 3]) << 24;
      pos += 4;
      return true;
   }

   std::vector<uint8_t> EncodeState(const State& st)
   {
      std::string h = "{\"format\":\"infinite-plugin-state\",\"v\":" + std::to_string(kStateVersion) +
                      ",\"appVersion\":\"" INFINITE_PLUGIN_VERSION "\",\"build\":\"" + std::string(kFx ? "fx" : "instrument") +
                      "\",\"latencyMode\":\"block\",\"bypass\":" + (st.bypass ? "true" : "false") + ",\"slotsSet\":" + std::to_string(st.slotsSet) + ",\"slots\":[";
      for (int32 i = 0; i < kSlotCount; i++)
      {
         char num[32];
         std::snprintf(num, sizeof num, "%s%.9g", i ? "," : "", double(st.slots[i]));
         h += num;
      }
      h += "]}";
      std::vector<uint8_t> z(mz_compressBound(mz_ulong(st.patch.size())));
      mz_ulong zlen = mz_ulong(z.size());
      if (st.patch.empty())
         zlen = 0;
      else if (mz_compress2(z.data(), &zlen, reinterpret_cast<const unsigned char*>(st.patch.data()), mz_ulong(st.patch.size()), 6) != MZ_OK)
         zlen = 0;
      std::vector<uint8_t> b;
      b.reserve(16 + h.size() + zlen);
      PutU32(b, kStateMagic);
      PutU32(b, uint32_t(h.size()));
      b.insert(b.end(), h.begin(), h.end());
      PutU32(b, zlen ? uint32_t(st.patch.size()) : 0u);
      PutU32(b, uint32_t(zlen));
      b.insert(b.end(), z.begin(), z.begin() + zlen);
      return b;
   }

   // False for anything that is not our state (other plugin's blob, truncated, corrupt).
   bool DecodeState(const std::vector<uint8_t>& b, State& st)
   {
      size_t pos = 0;
      uint32_t magic = 0, hlen = 0, raw = 0, zlen = 0;
      if (!GetU32(b, pos, magic) || magic != kStateMagic || !GetU32(b, pos, hlen) || pos + hlen > b.size())
         return false;
      const std::string h(b.begin() + pos, b.begin() + pos + hlen);
      pos += hlen;
      if (h.find("\"format\":\"infinite-plugin-state\"") == std::string::npos)
         return false;
      State out;
      const size_t by = h.find("\"bypass\":");
      out.bypass = by != std::string::npos && h.compare(by + 9, 4, "true") == 0;
      const size_t ss = h.find("\"slotsSet\":");
      if (ss != std::string::npos)
         out.slotsSet = uint32_t(std::strtoul(h.c_str() + ss + 11, nullptr, 10));
      const size_t sl = h.find("\"slots\":[");
      if (sl != std::string::npos)
      {
         const char* c = h.c_str() + sl + 9;
         for (int32 i = 0; i < kSlotCount && *c && *c != ']'; i++)
         {
            char* end = nullptr;
            const double v = std::strtod(c, &end);
            if (end == c)
               break;
            out.slots[i] = float(std::max(0.0, std::min(1.0, v)));
            c = end;
            while (*c == ',' || *c == ' ')
               c++;
         }
      }
      if (!GetU32(b, pos, raw) || !GetU32(b, pos, zlen) || pos + zlen > b.size())
         return false;
      if (raw > 0)
      {
         out.patch.resize(raw);
         mz_ulong got = raw;
         if (mz_uncompress(reinterpret_cast<unsigned char*>(&out.patch[0]), &got, b.data() + pos, mz_ulong(zlen)) != MZ_OK || got != raw)
            return false;
      }
      st = std::move(out);
      return true;
   }

   bool ReadAll(IBStream* s, std::vector<uint8_t>& out)
   {
      uint8_t buf[65536];
      for (;;)
      {
         int32 got = 0;
         if (s->read(buf, sizeof buf, &got) != kResultOk || got <= 0)
            break;
         out.insert(out.end(), buf, buf + got);
      }
      return !out.empty();
   }

   bool IsFile(const std::string& p)
   {
      struct stat st {};
      return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
   }

   // Where the engine is, first hit wins:
   //   $INFINITE_PLUGIN_ENGINE                      (development and tests)
   //   beside the plugin: Helpers/Infinite Engine.app (macOS) / infinite-engine(.exe)
   //   the installed app
   std::string FindEngine()
   {
      if (const char* env = getenv("INFINITE_PLUGIN_ENGINE"))
         if (IsFile(env))
            return env;
#if defined(__APPLE__)
      const std::string beside = Ipc::HelperPathBesideModule("Infinite Engine.app/Contents/MacOS/Infinite");
      if (IsFile(beside))
         return beside;
      if (const char* home = getenv("HOME"))
      {
         const std::string user = std::string(home) + "/Applications/Infinite.app/Contents/MacOS/Infinite";
         if (IsFile(user))
            return user;
      }
      const std::string app = "/Applications/Infinite.app/Contents/MacOS/Infinite";
      if (IsFile(app))
         return app;
#else
      const std::string beside = Ipc::HelperPathBesideModule("infinite-engine");
      if (IsFile(beside))
         return beside;
#endif
      return {};
   }

   struct SlotEdit
   {
      uint32_t kind; // 0 begin, 1 perform, 2 end, 3 sync
      uint32_t slot;
      float value;
   };

   // ---------------------------------------------------------------------------------------
   // Engine messages for the controller arrive on the reader thread; VST3 wants IMessage and edits
   // on the UI thread. The relay queues them and hops: macOS a run loop source on the main run loop,
   // Windows a message-only window made on the UI thread. Linux has no host run loop without an
   // editor, so there the sink runs on the reader thread. Both hops are torn down in the destructor
   // (UI thread), so nothing queued can call into the bundle after terminate and unload (a main-queue
   // block could: auval crashed on exactly that).
   class UiRelay
   {
   public:
      struct Item
      {
         uint32_t type;
         std::vector<uint8_t> data;
      };
      using Sink = std::function<void(const Item&)>;

      // UI thread.
      explicit UiRelay(Sink sink) : sink_(std::move(sink))
      {
#if defined(__APPLE__)
         CFRunLoopSourceContext ctx {};
         ctx.info = this;
         ctx.perform = [](void* info) { static_cast<UiRelay*>(info)->Drain(); };
         loop_ = CFRunLoopGetMain();
         CFRetain(loop_);
         source_ = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
         CFRunLoopAddSource(loop_, source_, kCFRunLoopCommonModes);
#elif defined(_WIN32)
         // The class's WndProc lives in this module: registered with its handle while any relay
         // exists, unregistered with the last, so a reloaded bundle never meets a stale class.
         HMODULE mod = nullptr;
         GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&WndProc), &mod);
         inst_ = mod;
         if (sLive++ == 0)
         {
            WNDCLASSW wc {};
            wc.lpfnWndProc = &WndProc;
            wc.hInstance = inst_;
            wc.lpszClassName = kClass;
            RegisterClassW(&wc);
         }
         hwnd_ = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst_, nullptr);
         if (hwnd_)
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
#endif
      }
      // UI thread, after the reader thread has stopped.
      ~UiRelay()
      {
#if defined(__APPLE__)
         {
            std::lock_guard<std::mutex> lock(mutex_);
            CFRunLoopSourceInvalidate(source_);
            CFRelease(source_);
            source_ = nullptr;
         }
         CFRelease(loop_);
#elif defined(_WIN32)
         if (hwnd_)
            DestroyWindow(hwnd_);
         if (--sLive == 0)
            UnregisterClassW(kClass, inst_);
#endif
      }

      // Any thread.
      void Post(Item item)
      {
         std::unique_lock<std::mutex> lock(mutex_);
         queue_.push_back(std::move(item));
#if defined(__APPLE__)
         if (source_)
         {
            CFRunLoopSourceSignal(source_);
            CFRunLoopWakeUp(loop_);
         }
#elif defined(_WIN32)
         if (hwnd_)
            PostMessageW(hwnd_, WM_APP + 1, 0, 0);
#else
         lock.unlock();
         Drain();
#endif
      }

   private:
      void Drain()
      {
         std::vector<Item> items;
         {
            std::lock_guard<std::mutex> lock(mutex_);
            items.swap(queue_);
         }
         for (const Item& it : items)
            sink_(it);
      }
#if defined(__APPLE__)
      CFRunLoopRef loop_ = nullptr;
      CFRunLoopSourceRef source_ = nullptr;
#elif defined(_WIN32)
      static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
      {
         if (msg == WM_APP + 1)
         {
            if (auto* r = (UiRelay*)GetWindowLongPtrW(h, GWLP_USERDATA))
               r->Drain();
            return 0;
         }
         return DefWindowProcW(h, msg, w, l);
      }
      static constexpr const wchar_t* kClass = L"InfinitePluginUiRelay";
      static inline int sLive = 0; // UI thread only
      HINSTANCE inst_ = nullptr;
      HWND hwnd_ = nullptr;
#endif
      Sink sink_;
      std::mutex mutex_;
      std::vector<Item> queue_;
   };

   // ---------------------------------------------------------------------------------------
   // One engine process and its shared memory. Message thread only, except Process() and
   // the atomics, which the audio thread reads.
   class EngineLink
   {
   public:
      ~EngineLink() { Stop(); }

      bool Running() const { return shell_.Valid(); }
      // Message thread, before Start: where slot edits and names from the engine go.
      void SetRelay(UiRelay* relay) { relay_ = relay; }
      // The engine has loaded its patch (audio thread may read it).
      bool Ready() const { return ready_.load(std::memory_order_acquire); }
      bool Matches(double rate, uint32_t block) const { return Running() && rate_ == rate && block_ == block; }

      // `patch` (the stored state, may be empty) becomes the engine's first message, so the engine
      // starts on the project's patch instead of loading the starting patch first.
      void Start(double rate, uint32_t block, const std::string& patch)
      {
         Stop();
         if (!shell_.Create(rate, block))
            return;
         rate_ = rate;
         block_ = block;
         ready_.store(false);
         const std::string exe = FindEngine();
         std::vector<std::string> args { "--plugin-engine", shell_.Name(), std::to_string((long long)Ipc::SelfPid()) };
         if (kFx)
            args.push_back("--fx");
         if (!patch.empty())
            args.push_back("--state");
         // Development and tests (tools/plugin): start the engine on a given patch instead of the
         // build's starting patch. Like $INFINITE_PLUGIN_ENGINE, never set by a host.
         if (const char* patch = getenv("INFINITE_PLUGIN_PATCH"))
            if (FILE* f = std::fopen(patch, "rb"))
            {
               std::fclose(f);
               args.push_back(patch);
            }
         if (exe.empty() || !Ipc::Spawn(exe, args, &child_))
         {
            child_ = Ipc::Child {};
            spawnFailed_.store(true);
            return;
         }
         spawnFailed_.store(false);
         stopReader_.store(false);
         // The only reader of the engine's messages. In 1.5 it waits for "ready"; 2.1 adds state.
         reader_ = std::thread([this] {
            PluginIpc::Message m;
            while (!stopReader_.load())
            {
               if (shell_.Messages().Receive(&m, 200))
               {
                  if (m.type == kMsgReady)
                  {
                     if (!m.data.empty())
                        std::fprintf(stderr, "Infinite plugin: engine audio did not start: %.*s\n", (int)m.data.size(), (const char*)m.data.data());
                     ready_.store(true);
                  }
                  else if (m.type == kMsgStateLoaded)
                  {
                     if (!m.data.empty())
                        std::fprintf(stderr, "Infinite plugin: patch did not load: %.*s\n", (int)m.data.size(), (const char*)m.data.data());
                  }
                  else if ((m.type == kMsgSlotEdit || m.type == kMsgSlotNames) && relay_)
                     relay_->Post(UiRelay::Item { m.type, m.data });
                  else if (m.type == kMsgState)
                  {
                     std::lock_guard<std::mutex> lock(replyMutex_);
                     replies_[m.id] = std::string(m.data.begin(), m.data.end());
                     replyCv_.notify_all();
                  }
               }
               else if (!Ipc::ChildAlive(child_))
                  break;
            }
         });
         if (!patch.empty())
            shell_.Messages().Send(kMsgSetState, NextId(), patch.data(), patch.size(), 5000);
      }

      // Message thread. The engine's current patch; false when it is not running, not ready yet
      // (then nothing has changed since the stored state), dead, or silent for timeoutMs.
      bool RequestPatch(std::string& out, int timeoutMs)
      {
         if (!Running() || !ready_.load() || !Ipc::ChildAlive(child_))
            return false;
         const uint32_t id = NextId();
         if (!shell_.Messages().Send(kMsgGetState, id, nullptr, 0, timeoutMs))
            return false;
         std::unique_lock<std::mutex> lock(replyMutex_);
         if (!replyCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return replies_.count(id) > 0; }))
            return false;
         out = std::move(replies_[id]);
         replies_.erase(id);
         return true;
      }

      // Message thread: replaces the running engine's patch. Offline renders wait for the ready
      // that follows the load.
      void SendPatch(const std::string& patch)
      {
         if (!Running() || !Ipc::ChildAlive(child_))
            return;
         ready_.store(false);
         shell_.Messages().Send(kMsgSetState, NextId(), patch.data(), patch.size(), 5000);
      }

      void Stop()
      {
         if (!shell_.Valid())
            return;
         shell_.RequestQuit();
         if (child_.pid > 0 && !Ipc::ChildWait(&child_, 500))
            Ipc::ChildKill(&child_);
         stopReader_.store(true);
         if (reader_.joinable())
            reader_.join();
         child_ = Ipc::Child {};
         shell_.Close();
         ready_.store(false);
         std::lock_guard<std::mutex> lock(replyMutex_);
         replies_.clear();
      }

      // Audio thread, offline only: an offline render must not depend on spawn timing, so wait for
      // the engine to load its patch, for as long as it is alive (60 s cap for a stuck start).
      void WaitReadyOffline()
      {
         if (ready_.load(std::memory_order_acquire) || spawnFailed_.load())
            return;
         const uint64_t t0 = Ipc::MonotonicNs();
         while (!ready_.load(std::memory_order_acquire) && Ipc::ChildAlive(child_) && Ipc::MonotonicNs() - t0 < 60ull * 1000000000ull)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      PluginIpc::Result Process(const PluginIpc::ProcessArgs& a) { return shell_.Process(a); }
      int64_t Pid() const { return child_.pid; }

   private:
      uint32_t NextId() { return ++lastId_; }

      PluginIpc::ShellSide shell_;
      std::mutex replyMutex_;
      std::condition_variable replyCv_;
      std::map<uint32_t, std::string> replies_;
      uint32_t lastId_ = 0;
      Ipc::Child child_;
      UiRelay* relay_ = nullptr;
      std::thread reader_;
      std::atomic<bool> stopReader_ { false };
      std::atomic<bool> ready_ { false };
      std::atomic<bool> spawnFailed_ { false };
      double rate_ = 0.0;
      uint32_t block_ = 0;
   };

   // ---------------------------------------------------------------------------------------
   class Processor : public AudioEffect
   {
   public:
      Processor() { setControllerClass(kControllerUID); }
      ~Processor() override = default;
      static FUnknown* Create(void*) { return static_cast<IAudioProcessor*>(new Processor); }

      tresult PLUGIN_API initialize(FUnknown* context) override
      {
         const tresult r = AudioEffect::initialize(context);
         if (r != kResultOk)
            return r;
         if (kFx)
            addAudioInput(STR16("Input"), SpeakerArr::kStereo, kMain);
         addAudioInput(STR16("Sidechain"), SpeakerArr::kStereo, kAux, 0); // inactive until the host turns it on
         addAudioOutput(STR16("Output"), SpeakerArr::kStereo, kMain);
         addEventInput(STR16("MIDI In"), 16);
         relay_ = std::make_shared<UiRelay>([this](const UiRelay::Item& it) { ToController(it); });
         link_.SetRelay(relay_.get());
         return kResultOk;
      }

      tresult PLUGIN_API terminate() override
      {
         link_.Stop();
         link_.SetRelay(nullptr);
         relay_.reset(); // after the reader thread is gone: a hop still queued finds nothing
         return AudioEffect::terminate();
      }

      tresult PLUGIN_API setBusArrangements(SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) override
      {
         const int32 wantIns = kFx ? 2 : 1;
         if (numIns != wantIns || numOuts != 1)
            return kResultFalse;
         for (int32 i = 0; i < numIns; i++)
            if (ins[i] != SpeakerArr::kStereo && ins[i] != SpeakerArr::kMono)
               return kResultFalse;
         if (outs[0] != SpeakerArr::kStereo)
            return kResultFalse; // wider or mono out: refuse, the host falls back to our stereo default
         return AudioEffect::setBusArrangements(ins, numIns, outs, numOuts);
      }

      tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }

      tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
      {
         if (setup.maxSamplesPerBlock <= 0 || setup.maxSamplesPerBlock > int32(PluginIpc::kMaxBlockLimit) || setup.sampleRate <= 0.0)
            return kResultFalse;
         return AudioEffect::setupProcessing(setup);
      }

      uint32 PLUGIN_API getLatencySamples() override { return uint32(processSetup.maxSamplesPerBlock); }
      uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }

      tresult PLUGIN_API setActive(TBool state) override
      {
         // The engine is spawned on the first activation and kept across deactivation (host suspend);
         // a new rate or block size needs a fresh region, so it restarts.
         if (state && !link_.Matches(processSetup.sampleRate, uint32_t(processSetup.maxSamplesPerBlock)))
         {
            // The restart keeps what the user built: take the patch from the old engine first.
            std::string current;
            if (link_.RequestPatch(current, 2000))
               patch_ = std::move(current);
            link_.Start(processSetup.sampleRate, uint32_t(processSetup.maxSamplesPerBlock), patch_);
            resendSlots_.store(true); // a fresh engine knows no slot values yet
         }
         return AudioEffect::setActive(state);
      }

      tresult PLUGIN_API setState(IBStream* state) override
      {
         if (!state)
            return kInvalidArgument;
         std::vector<uint8_t> bytes;
         State st;
         if (!ReadAll(state, bytes) || !DecodeState(bytes, st))
            return kResultFalse;
         bypass_ = st.bypass;
         std::memcpy(slots_, st.slots, sizeof slots_);
         slotsSet_.store(st.slotsSet);
         patch_ = std::move(st.patch);
         resendSlots_.store(true);
         // Not running yet: Start sends it. Running: the engine swaps to it now.
         if (!patch_.empty())
            link_.SendPatch(patch_);
         return kResultOk;
      }

      tresult PLUGIN_API getState(IBStream* state) override
      {
         if (!state)
            return kInvalidArgument;
         std::string current;
         if (link_.RequestPatch(current, 2000))
            patch_ = std::move(current); // else the last known patch: a save never loses one
         State st;
         st.bypass = bypass_;
         std::memcpy(st.slots, slots_, sizeof slots_);
         st.slotsSet = slotsSet_.load();
         st.patch = patch_;
         const std::vector<uint8_t> bytes = EncodeState(st);
         int32 wrote = 0;
         if (state->write(const_cast<uint8_t*>(bytes.data()), int32(bytes.size()), &wrote) != kResultOk || wrote != int32(bytes.size()))
            return kResultFalse;
         return kResultOk;
      }

      tresult PLUGIN_API process(ProcessData& data) override
      {
         numEvents_ = 0;
         numParams_ = 0;
         ReadParameters(data);
         ReadEvents(data);
         if (data.numSamples <= 0 || data.numOutputs < 1)
            return kResultOk;

         const uint32_t n = uint32_t(data.numSamples);
         AudioBusBuffers& outBus = data.outputs[0];
         float** out = outBus.channelBuffers32;
         if (!out)
            return kResultOk;
         // A one-channel output bus still gets stereo from the engine: the right side goes to scratch.
         float* outs[2] = { out[0], outBus.numChannels > 1 ? out[1] : monoRight_.data() };

         // Inputs: main (FX) and sidechain; mono buses feed both sides.
         const float* ins[PluginIpc::kInChannels] = {};
         int32 bus = 0;
         if (kFx && data.numInputs > bus && data.inputs[bus].channelBuffers32 && data.inputs[bus].numChannels > 0)
         {
            ins[0] = data.inputs[bus].channelBuffers32[0];
            ins[1] = data.inputs[bus].numChannels > 1 ? data.inputs[bus].channelBuffers32[1] : ins[0];
         }
         if (kFx)
            bus++;
         bool sidechain = false;
         if (data.numInputs > bus && data.inputs[bus].channelBuffers32 && data.inputs[bus].numChannels > 0)
         {
            BusList* auxList = getBusList(kAudio, kInput);
            Bus* aux = auxList && int32(auxList->size()) > bus ? FCast<Bus>(auxList->at(bus)) : nullptr;
            if (aux && aux->isActive())
            {
               ins[2] = data.inputs[bus].channelBuffers32[0];
               ins[3] = data.inputs[bus].numChannels > 1 ? data.inputs[bus].channelBuffers32[1] : ins[2];
               sidechain = true;
            }
         }

         // Either says offline: Steinberg's auwrapper re-runs setupProcessing(kRealtime) on AU
         // Initialize and carries an earlier OfflineRender only in the per-block processMode.
         const bool offline = processSetup.processMode == kOffline || data.processMode == kOffline;
         if (offline)
            link_.WaitReadyOffline();

         PluginIpc::ProcessArgs a;
         a.in = ins;
         a.numIn = PluginIpc::kInChannels;
         a.out = outs;
         a.numOut = 2;
         a.frames = n;
         a.flags = (offline ? PluginIpc::kOffline : 0u) | (sidechain ? PluginIpc::kSidechain : 0u) | (bypass_ ? PluginIpc::kBypass : 0u);
         a.transport = ReadTransport(data.processContext);
         a.events = events_;
         a.numEvents = numEvents_;
         a.params = params_;
         a.numParams = numParams_;

         const PluginIpc::Result r = link_.Process(a);
         outBus.silenceFlags = r == PluginIpc::Result::Ok ? 0 : ((uint64(1) << outBus.numChannels) - 1);
         return kResultOk;
      }

   private:
      void ReadParameters(ProcessData& data)
      {
         // Every slot at offset 0 once the engine is ready after a start or a state load (its Macros
         // exist then); host points this block follow
         // and win (the engine sorts by offset, stable).
         if (link_.Ready() && resendSlots_.exchange(false))
         {
            const uint32_t set = slotsSet_.load();
            for (int32 i = 0; i < kSlotCount; i++)
               if (set & (1u << i))
                  params_[numParams_++] = PluginIpc::ParamPoint { uint16_t(i), 0, slots_[i] };
         }
         IParameterChanges* changes = data.inputParameterChanges;
         if (!changes)
            return;
         const int32 count = changes->getParameterCount();
         for (int32 i = 0; i < count; i++)
         {
            IParamValueQueue* q = changes->getParameterData(i);
            if (!q)
               continue;
            const ParamID id = q->getParameterId();
            const int32 points = q->getPointCount();
            for (int32 p = 0; p < points; p++)
            {
               int32 offset = 0;
               ParamValue v = 0.0;
               if (q->getPoint(p, offset, v) != kResultOk)
                  continue;
               offset = std::max(0, std::min(offset, std::max(0, data.numSamples - 1)));
               if (id == kBypassId)
                  bypass_ = v >= 0.5;
               else if (id < ParamID(kSlotFirst + kSlotCount))
               {
                  slots_[id - kSlotFirst] = float(v);
                  slotsSet_.fetch_or(1u << (id - kSlotFirst));
                  if (numParams_ < PluginIpc::kMaxParamPoints)
                     params_[numParams_++] = PluginIpc::ParamPoint { uint16_t(id - kSlotFirst), uint16_t(offset), float(v) };
               }
               else if (id >= kMidiCcFirst && id <= kMidiLast)
                  AddMidiParam(id, uint32_t(offset), v);
            }
         }
      }

      // MIDI-mapping parameter point -> MIDI message on channel 1 (the mapping is omni).
      void AddMidiParam(ParamID id, uint32_t offset, ParamValue v)
      {
         v = std::max(0.0, std::min(1.0, v));
         PluginIpc::Event e {};
         e.offset = offset;
         e.noteId = -1;
         e.value = float(v);
         if (id == kMidiPitchBend)
         {
            const int bend = int(std::lround(v * 16383.0));
            e.status = 0xE0;
            e.data1 = uint8_t(bend & 0x7F);
            e.data2 = uint8_t((bend >> 7) & 0x7F);
            e.value = float(v * 2.0 - 1.0);
         }
         else if (id == kMidiChannelPressure)
         {
            e.status = 0xD0;
            e.data1 = uint8_t(std::lround(v * 127.0));
         }
         else
         {
            e.status = 0xB0;
            e.data1 = uint8_t(id - kMidiCcFirst);
            e.data2 = uint8_t(std::lround(v * 127.0));
         }
         Push(e);
      }

      void Push(const PluginIpc::Event& e)
      {
         if (numEvents_ < PluginIpc::kMaxEvents)
            events_[numEvents_++] = e;
      }

      static uint8_t Vel7(float v) { return uint8_t(std::max(0, std::min(127, int(std::lround(v * 127.f))))); }

      void ReadEvents(ProcessData& data)
      {
         IEventList* list = data.inputEvents;
         if (list)
         {
            const int32 count = list->getEventCount();
            for (int32 i = 0; i < count; i++)
            {
               Event ev {};
               if (list->getEvent(i, ev) != kResultOk)
                  continue;
               PluginIpc::Event e {};
               e.offset = uint32_t(std::max(0, std::min(ev.sampleOffset, std::max(0, data.numSamples - 1))));
               e.value = NAN;
               e.noteId = -1;
               switch (ev.type)
               {
               case Event::kNoteOnEvent:
               {
                  const uint8_t ch = uint8_t(ev.noteOn.channel & 0x0F);
                  const uint8_t vel = Vel7(ev.noteOn.velocity);
                  // Velocity 0 would read as a note-off in MIDI.
                  e.status = uint8_t(0x90 | ch);
                  e.data1 = uint8_t(ev.noteOn.pitch & 0x7F);
                  e.data2 = vel == 0 ? 1 : vel;
                  e.value = ev.noteOn.velocity;
                  e.noteId = ev.noteOn.noteId;
                  Push(e);
                  break;
               }
               case Event::kNoteOffEvent:
                  e.status = uint8_t(0x80 | (ev.noteOff.channel & 0x0F));
                  e.data1 = uint8_t(ev.noteOff.pitch & 0x7F);
                  e.data2 = Vel7(ev.noteOff.velocity);
                  e.noteId = ev.noteOff.noteId;
                  Push(e);
                  break;
               case Event::kPolyPressureEvent:
                  e.status = uint8_t(0xA0 | (ev.polyPressure.channel & 0x0F));
                  e.data1 = uint8_t(ev.polyPressure.pitch & 0x7F);
                  e.data2 = Vel7(ev.polyPressure.pressure);
                  e.value = ev.polyPressure.pressure;
                  e.noteId = ev.polyPressure.noteId;
                  Push(e);
                  break;
               default:
                  break;
               }
            }
         }
         // Parameter points (CC) and events arrive in two lists; the engine wants one, in time order.
         std::stable_sort(events_, events_ + numEvents_, [](const PluginIpc::Event& x, const PluginIpc::Event& y) { return x.offset < y.offset; });
      }

      static PluginIpc::Transport ReadTransport(const ProcessContext* c)
      {
         PluginIpc::Transport t;
         if (!c)
            return t;
         if (c->state & ProcessContext::kTempoValid)
         {
            t.tempo = c->tempo;
            t.flags |= PluginIpc::kTempoValid;
         }
         if (c->state & ProcessContext::kProjectTimeMusicValid)
         {
            t.ppq = c->projectTimeMusic;
            t.flags |= PluginIpc::kPpqValid;
         }
         if (c->state & ProcessContext::kBarPositionValid)
            t.barStartPpq = c->barPositionMusic;
         if (c->state & ProcessContext::kCycleValid)
         {
            t.loopStartPpq = c->cycleStartMusic;
            t.loopEndPpq = c->cycleEndMusic;
         }
         if (c->state & ProcessContext::kTimeSigValid)
         {
            t.sigNum = c->timeSigNumerator;
            t.sigDen = c->timeSigDenominator;
         }
         t.hostSamplePos = c->projectTimeSamples;
         if (c->state & ProcessContext::kPlaying)
            t.flags |= PluginIpc::kPlaying;
         if (c->state & ProcessContext::kCycleActive)
            t.flags |= PluginIpc::kLooping;
         if (c->state & ProcessContext::kRecording)
            t.flags |= PluginIpc::kRecording;
         return t;
      }

      // UI thread: an engine message for the controller.
      void ToController(const UiRelay::Item& it)
      {
         IMessage* msg = allocateMessage();
         if (!msg)
            return;
         if (it.type == kMsgSlotEdit && it.data.size() >= sizeof(SlotEdit))
         {
            SlotEdit e;
            std::memcpy(&e, it.data.data(), sizeof e);
            if (e.slot >= uint32_t(kSlotCount))
            {
               msg->release();
               return;
            }
            const uint32_t bit = 1u << e.slot;
            if (e.kind == 3 && (slotsSet_.load() & bit))
            {
               msg->release(); // the host has its own value; the engine gets it with the resend
               return;
            }
            if (e.kind == 1 || e.kind == 3)
            {
               slots_[e.slot] = std::max(0.0f, std::min(1.0f, e.value)); // a save before the host echoes it
               slotsSet_.fetch_or(bit);
            }
            msg->setMessageID("slotEdit");
            msg->getAttributes()->setInt("kind", e.kind);
            msg->getAttributes()->setInt("slot", e.slot);
            msg->getAttributes()->setFloat("value", e.value);
         }
         else if (it.type == kMsgSlotNames)
         {
            msg->setMessageID("slotNames");
            msg->getAttributes()->setBinary("names", it.data.data(), uint32(it.data.size()));
         }
         else
         {
            msg->release();
            return;
         }
         sendMessage(msg);
         msg->release();
      }

      EngineLink link_;
      std::shared_ptr<UiRelay> relay_;
      std::atomic<bool> resendSlots_ { true };
      std::atomic<uint32_t> slotsSet_ { 0 };
      std::string patch_; // last known patch (.inf text); message thread only
      bool bypass_ = false;
      float slots_[kSlotCount] = {};
      PluginIpc::Event events_[PluginIpc::kMaxEvents] {};
      uint32_t numEvents_ = 0;
      PluginIpc::ParamPoint params_[PluginIpc::kMaxParamPoints] {};
      uint32_t numParams_ = 0;
      std::vector<float> monoRight_ = std::vector<float>(PluginIpc::kMaxBlockLimit);
   };

   // ---------------------------------------------------------------------------------------
   class Controller : public EditController, public IMidiMapping
   {
   public:
      static FUnknown* Create(void*) { return static_cast<IEditController*>(new Controller); }

      tresult PLUGIN_API initialize(FUnknown* context) override
      {
         const tresult r = EditController::initialize(context);
         if (r != kResultOk)
            return r;
         for (int32 i = 0; i < kSlotCount; i++)
         {
            char name[16];
            std::snprintf(name, sizeof name, "Slot %02d", i + 1);
            UString128 title(name);
            parameters.addParameter(title, nullptr, 0, 0.0, ParameterInfo::kCanAutomate, kSlotFirst + ParamID(i));
         }
         parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0, ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassId);
         // Hidden MIDI-mapping parameters: the host writes them from CC; not automatable, not shown.
         for (int32 cc = 0; cc < 128; cc++)
         {
            char name[24];
            std::snprintf(name, sizeof name, "MIDI CC %d", cc);
            UString128 title(name);
            parameters.addParameter(title, nullptr, 0, 0.0, ParameterInfo::kIsHidden, kMidiCcFirst + ParamID(cc));
         }
         parameters.addParameter(STR16("MIDI Pitch Bend"), nullptr, 0, 0.5, ParameterInfo::kIsHidden, kMidiPitchBend);
         parameters.addParameter(STR16("MIDI Channel Pressure"), nullptr, 0, 0.0, ParameterInfo::kIsHidden, kMidiChannelPressure);
         return kResultOk;
      }

      tresult PLUGIN_API setComponentState(IBStream* state) override
      {
         if (!state)
            return kInvalidArgument;
         std::vector<uint8_t> bytes;
         State st;
         if (!ReadAll(state, bytes) || !DecodeState(bytes, st))
            return kResultFalse;
         setParamNormalized(kBypassId, st.bypass ? 1.0 : 0.0);
         for (int32 i = 0; i < kSlotCount; i++)
            setParamNormalized(kSlotFirst + ParamID(i), st.slots[i]);
         return kResultOk;
      }

      // From the processor (UI thread): a bound Macro turned in Infinite, or the slot names changed.
      tresult PLUGIN_API notify(IMessage* message) override
      {
         if (!message)
            return kInvalidArgument;
         IAttributeList* attr = message->getAttributes();
         if (attr && std::strcmp(message->getMessageID(), "slotEdit") == 0)
         {
            int64 kind = 0, slot = -1;
            double value = 0.0;
            attr->getInt("kind", kind);
            attr->getInt("slot", slot);
            attr->getFloat("value", value);
            if (slot < 0 || slot >= kSlotCount)
               return kResultFalse;
            const ParamID id = kSlotFirst + ParamID(slot);
            value = std::max(0.0, std::min(1.0, value));
            if (kind == 0)
               beginEdit(id);
            else if (kind == 1)
            {
               setParamNormalized(id, value);
               performEdit(id, value);
            }
            else if (kind == 2)
               endEdit(id);
            else if (kind == 3)
            {
               setParamNormalized(id, value);
               if (componentHandler)
                  componentHandler->restartComponent(kParamValuesChanged);
            }
            return kResultOk;
         }
         if (attr && std::strcmp(message->getMessageID(), "slotNames") == 0)
         {
            const void* data = nullptr;
            uint32 size = 0;
            if (attr->getBinary("names", data, size) != kResultOk || !data)
               return kResultFalse;
            const char* p = static_cast<const char*>(data);
            const char* end = p + size;
            bool changed = false;
            for (int32 i = 0; i < kSlotCount && p < end; i++)
            {
               const char* z = static_cast<const char*>(std::memchr(p, 0, size_t(end - p)));
               if (!z)
                  break;
               std::string name(p, z);
               p = z + 1;
               if (name.empty())
               {
                  char def[16];
                  std::snprintf(def, sizeof def, "Slot %02d", i + 1);
                  name = def;
               }
               Parameter* param = parameters.getParameter(kSlotFirst + ParamID(i));
               if (!param)
                  continue;
               const std::u16string u = Steinberg::StringConvert::convert(name);
               ParameterInfo& info = param->getInfo();
               String128 title {};
               const size_t n = std::min(u.size(), size_t(127));
               for (size_t c = 0; c < n; c++)
                  title[c] = TChar(u[c]);
               if (std::memcmp(title, info.title, sizeof title) != 0)
               {
                  std::memcpy(info.title, title, sizeof title);
                  changed = true;
               }
            }
            if (changed && componentHandler)
               componentHandler->restartComponent(kParamTitlesChanged);
            return kResultOk;
         }
         return EditController::notify(message);
      }

      // IMidiMapping: every channel of the one event bus maps to the same (omni) parameters.
      tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 /*channel*/, CtrlNumber midiControllerNumber, ParamID& id) override
      {
         if (busIndex != 0)
            return kResultFalse;
         if (midiControllerNumber >= 0 && midiControllerNumber < 128)
            id = kMidiCcFirst + ParamID(midiControllerNumber);
         else if (midiControllerNumber == kPitchBend)
            id = kMidiPitchBend;
         else if (midiControllerNumber == kAfterTouch)
            id = kMidiChannelPressure;
         else
            return kResultFalse;
         return kResultTrue;
      }

      OBJ_METHODS(Controller, EditController)
      DEFINE_INTERFACES
         DEF_INTERFACE(IMidiMapping)
      END_DEFINE_INTERFACES(EditController)
      REFCOUNT_METHODS(EditController)
   };
}

BEGIN_FACTORY_DEF("Infinite", "https://infinitedevices.org", "mailto:hello@infinitedevices.org")

DEF_CLASS2(INLINE_UID_FROM_FUID(InfinitePlugin::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           INFINITE_PLUGIN_NAME, Vst::kDistributable, INFINITE_PLUGIN_CATEGORY, INFINITE_PLUGIN_VERSION, kVstVersionString,
           InfinitePlugin::Processor::Create)

DEF_CLASS2(INLINE_UID_FROM_FUID(InfinitePlugin::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
           INFINITE_PLUGIN_NAME " Controller", 0, "", INFINITE_PLUGIN_VERSION, kVstVersionString,
           InfinitePlugin::Controller::Create)

END_FACTORY
