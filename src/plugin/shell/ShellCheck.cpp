// plugin_shell_check: loads "Infinite Instrument.vst3" and "Infinite FX.vst3" the way a DAW does
// and runs them against the real engine (docs/plans/vst-au/README.md step 1.5).
//
//   INFINITE_PLUGIN_ENGINE=<Infinite binary> plugin_shell_check <Instrument.vst3> <FX.vst3>
//   INFINITE_PLUGIN_ENGINE=<Infinite binary> plugin_shell_check --metrics <Instrument.vst3> <FX.vst3> <R1.inf> <R2.inf>
//
// --metrics measures README section 10's M-4 (offline render twice, bit-identical), M-5 (offline
// vs paced realtime null <= -60 dBFS at max block 32/64/512/2048) and M-6 (impulse through R2,
// measured offset == reported latency) on the R patches, via $INFINITE_PLUGIN_PATCH.
//
// Checks: class names and categories, a scan (initialize + terminate) spawns no engine, the
// controller's 32 slots + bypass + 130 hidden MIDI-mapping parameters and their CC assignments,
// latency = max block and an infinite tail, FX offline pass-through with random block sizes
// (exact, delayed by L), two offline FX instances bit-identical, Instrument note-on -> sound and
// bypass -> silence, FX realtime paced, a killed engine leaves the host alive and the plugin
// silent, and no engine is left after terminate. State (2.1, on R2): getState before any engine,
// a restored instance sounds bit-identical and saves the same blob, setState while running swaps
// the patch, a block-size restart keeps it, a dead engine still saves the last known patch,
// garbage is refused. Slots (2.2, on S1: a Macro Knob on slot 1 drives a Gain): a fresh instance
// keeps the patch's value, slot automation moves the sound, two automated offline bounces are
// bit-identical (and parked), offline vs realtime null, a saved slot value comes back with the
// project, and with a controller connected: the slot takes the Macro's name and value, and turning
// the Macro in Infinite reaches the host as begin / perform / end. CC (S2): host CC reaches a MIDI CC
// binding saved with a hardware device id. View (2.3, macOS; --view runs only these): an editor
// opened before activation starts the engine and shows its frames at the view's size x backing
// scale, Shift+M (modulation matrix) changes the picture within M-10's 50 ms p95, a resize gives
// frames at the new size, a closed view stops the engine's readback and a reopened one shows frames
// again; prints B-6 (readback) and the M-10 pixel pipe estimate. PASS/FAIL per check; exit 0 only
// when all pass.
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <CoreFoundation/CoreFoundation.h>

#include "ShellCheckMac.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <signal.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
   Vst::HostApplication gHost;
   int gFails = 0;
   constexpr double kRate = 48000.0;
   constexpr int32 kBlock = 512;

   void Check(bool ok, const char* what, const std::string& detail = std::string())
   {
      std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
      std::fflush(stdout);
      if (!ok)
         gFails++;
   }

   std::vector<int> EnginePids()
   {
      std::vector<int> pids;
      if (FILE* f = popen("pgrep -f -- '--plugin-engine'", "r"))
      {
         int pid = 0;
         while (std::fscanf(f, "%d", &pid) == 1)
            pids.push_back(pid);
         pclose(f);
      }
      return pids;
   }

   struct Plugin
   {
      VST3::Hosting::Module::Ptr mod;
      VST3::Hosting::ClassInfo info;
      bool Load(const char* path, std::string& err)
      {
         mod = VST3::Hosting::Module::create(path, err);
         if (!mod)
            return false;
         for (auto& ci : mod->getFactory().classInfos())
            if (ci.category() == kVstAudioEffectClass)
            {
               info = ci;
               return true;
            }
         err = "no audio effect class";
         return false;
      }
   };

   struct Instance
   {
      IPtr<IComponent> comp;
      IPtr<IAudioProcessor> proc;
      bool fx = false;
      int32 mode = kOffline;
      int32 block = kBlock;

      bool Open(Plugin& p, bool isFx, int32 processMode, bool activate = true, int32 maxBlock = kBlock)
      {
         fx = isFx;
         mode = processMode;
         block = maxBlock;
         comp = p.mod->getFactory().createInstance<IComponent>(p.info.ID());
         if (!comp || comp->initialize(&gHost) != kResultOk)
            return false;
         proc = FUnknownPtr<IAudioProcessor>(comp.get());
         if (!proc)
            return false;
         return !activate || Activate();
      }

      bool Activate()
      {
         ProcessSetup setup { mode, kSample32, block, kRate };
         if (proc->setupProcessing(setup) != kResultOk)
            return false;
         comp->activateBus(kAudio, kOutput, 0, true);
         if (fx)
            comp->activateBus(kAudio, kInput, 0, true);
         comp->activateBus(kEvent, kInput, 0, true);
         if (comp->setActive(true) != kResultOk)
            return false;
         proc->setProcessing(true);
         return true;
      }

      void Close(bool activated = true)
      {
         if (activated)
         {
            proc->setProcessing(false);
            comp->setActive(false);
         }
         comp->terminate();
      }
   };

   float Sine(int64_t t) { return 0.5f * float(std::sin(2.0 * M_PI * 220.0 * double(t) / kRate)); }

   struct Script
   {
      int64_t noteOnAt = -1, noteOffAt = -1, bypassAt = -1;
      float (*input)(int64_t) = Sine;
      std::vector<std::pair<int64_t, int64_t>> notes; // extra (on, off) pairs, pitch 48 + index % 24
      float (*automation)(int64_t) = nullptr;         // points at each block's ends on automationId
      ParamID automationId = 0;                       // 0 = Slot 01; 2007 = MIDI CC 7
      std::function<void()> perBlock;                 // after each process() (a host's UI thread)
   };

   // Renders `total` frames in pseudo-random block sizes (1..max block); returns the left channel.
   std::vector<float> Render(Instance& in, int64_t total, bool paced, const Script& s = Script(), int* killPid = nullptr)
   {
      std::vector<float> out;
      out.reserve(size_t(total));
      const int32 kBlock = in.block;
      std::vector<float> il(kBlock), ir(kBlock), ol(kBlock), orr(kBlock);
      float* ins[2] = { il.data(), ir.data() };
      float* outs[2] = { ol.data(), orr.data() };
      AudioBusBuffers ib, ob;
      ib.numChannels = ob.numChannels = 2;
      ib.channelBuffers32 = ins;
      ob.channelBuffers32 = outs;
      EventList events(64);
      ParameterChanges params(4);
      ProcessContext ctx {};
      ProcessData d {};
      d.processMode = in.mode;
      d.symbolicSampleSize = kSample32;
      d.numInputs = in.fx ? 1 : 0;
      d.inputs = in.fx ? &ib : nullptr;
      d.numOutputs = 1;
      d.outputs = &ob;
      d.inputEvents = &events;
      d.inputParameterChanges = &params;
      d.processContext = &ctx;
      uint32_t rng = 12345;
      int64_t t = 0;
      auto next = std::chrono::steady_clock::now();
      while (t < total)
      {
         rng = rng * 1664525u + 1013904223u;
         int32 n = 1 + int32((rng >> 8) % uint32_t(kBlock));
         if (t + n > total)
            n = int32(total - t);
         for (int32 i = 0; i < n; i++)
            il[i] = ir[i] = s.input(t + i);
         events.clear();
         params.clearQueue();
         auto inBlock = [&](int64_t at) { return at >= t && at < t + n; };
         if (inBlock(s.noteOnAt))
         {
            Event e {};
            e.type = Event::kNoteOnEvent;
            e.sampleOffset = int32(s.noteOnAt - t);
            e.noteOn.pitch = 60;
            e.noteOn.velocity = 0.8f;
            e.noteOn.noteId = -1;
            events.addEvent(e);
         }
         if (inBlock(s.noteOffAt))
         {
            Event e {};
            e.type = Event::kNoteOffEvent;
            e.sampleOffset = int32(s.noteOffAt - t);
            e.noteOff.pitch = 60;
            e.noteOff.noteId = -1;
            events.addEvent(e);
         }
         for (size_t k = 0; k < s.notes.size(); k++)
            for (int on = 0; on < 2; on++)
            {
               const int64_t at = on ? s.notes[k].first : s.notes[k].second;
               if (!inBlock(at))
                  continue;
               Event e {};
               e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
               e.sampleOffset = int32(at - t);
               if (on)
               {
                  e.noteOn.pitch = int16(48 + k % 24);
                  e.noteOn.velocity = 0.5f + 0.4f * float(k % 5) / 4.0f;
                  e.noteOn.noteId = -1;
               }
               else
               {
                  e.noteOff.pitch = int16(48 + k % 24);
                  e.noteOff.noteId = -1;
               }
               events.addEvent(e);
            }
         if (inBlock(s.bypassAt))
         {
            int32 qi = 0, pi = 0;
            if (IParamValueQueue* q = params.addParameterData(1000, qi))
               q->addPoint(int32(s.bypassAt - t), 1.0, pi);
         }
         if (s.automation)
         {
            int32 qi = 0, pi = 0;
            if (IParamValueQueue* q = params.addParameterData(s.automationId, qi))
            {
               q->addPoint(0, s.automation(t), pi);
               if (n > 1)
                  q->addPoint(n - 1, s.automation(t + n - 1), pi);
            }
         }
         ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
         ctx.sampleRate = kRate;
         ctx.tempo = 120.0;
         ctx.projectTimeSamples = t;
         ctx.projectTimeMusic = double(t) / kRate * 2.0;
         d.numSamples = n;
         in.proc->process(d);
         for (int32 i = 0; i < n; i++)
            out.push_back(ol[i]);
         if (s.perBlock)
            s.perBlock();
         t += n;
         if (killPid && *killPid > 0 && t > total / 2)
         {
            kill(*killPid, SIGKILL);
            *killPid = 0;
         }
         if (paced)
         {
            next += std::chrono::nanoseconds(int64_t(1e9 * n / kRate));
            std::this_thread::sleep_until(next);
         }
      }
      return out;
   }

   double MaxErrDelayed(const std::vector<float>& out, int64_t from)
   {
      double e = 0.0;
      for (int64_t t = std::max<int64_t>(from, kBlock); t < int64_t(out.size()); t++)
         e = std::max(e, double(std::fabs(out[size_t(t)] - Sine(t - kBlock))));
      return e;
   }

   double Rms(const std::vector<float>& x, int64_t a, int64_t b)
   {
      double s = 0.0;
      b = std::min<int64_t>(b, int64_t(x.size()));
      for (int64_t i = a; i < b; i++)
         s += double(x[size_t(i)]) * x[size_t(i)];
      return b > a ? std::sqrt(s / double(b - a)) : 0.0;
   }

   void CheckController(Plugin& p, const char* label)
   {
      Instance in;
      if (!in.Open(p, false, kOffline, false))
      {
         Check(false, label, "open failed");
         return;
      }
      TUID cid;
      IPtr<IEditController> ec;
      if (in.comp->getControllerClassId(cid) == kResultOk)
         ec = p.mod->getFactory().createInstance<IEditController>(VST3::UID::fromTUID(cid));
      if (!ec || ec->initialize(&gHost) != kResultOk)
      {
         Check(false, label, "controller failed");
         in.Close(false);
         return;
      }
      int32 slots = 0, hidden = 0, bypass = 0;
      const int32 count = ec->getParameterCount();
      for (int32 i = 0; i < count; i++)
      {
         ParameterInfo pi {};
         ec->getParameterInfo(i, pi);
         if (pi.flags & ParameterInfo::kIsBypass)
            bypass++;
         else if (pi.flags & ParameterInfo::kIsHidden)
            hidden++;
         else if (pi.flags & ParameterInfo::kCanAutomate)
            slots++;
      }
      char d[160];
      std::snprintf(d, sizeof d, "%d params: %d slots, %d bypass, %d hidden", count, slots, bypass, hidden);
      std::string what = std::string(label) + ": 32 slots + bypass + 130 hidden MIDI-mapping params";
      Check(count == 163 && slots == 32 && bypass == 1 && hidden == 130, what.c_str(), d);

      FUnknownPtr<IMidiMapping> mm(ec);
      ParamID cc7 = 0, cc7ch5 = 0, bend = 0, press = 0;
      const bool ok = mm && mm->getMidiControllerAssignment(0, 0, 7, cc7) == kResultTrue &&
                      mm->getMidiControllerAssignment(0, 4, 7, cc7ch5) == kResultTrue &&
                      mm->getMidiControllerAssignment(0, 0, kPitchBend, bend) == kResultTrue &&
                      mm->getMidiControllerAssignment(0, 0, kAfterTouch, press) == kResultTrue;
      std::snprintf(d, sizeof d, "CC7 -> %u, CC7 ch5 -> %u, bend -> %u, pressure -> %u", cc7, cc7ch5, bend, press);
      what = std::string(label) + ": IMidiMapping maps CC / bend / pressure omni";
      Check(ok && cc7 == 2007 && cc7ch5 == 2007 && bend == 2128 && press == 2129, what.c_str(), d);
      ec->terminate();
      in.Close(false);
   }

   // ---- metrics (README section 10: M-4, M-5, M-6) on the R patches -------------------------

   float Signal(int64_t t)
   {
      uint32_t h = uint32_t(t) * 2654435761u;
      h ^= h >> 15;
      h *= 2246822519u;
      h ^= h >> 13;
      const float noise = float(h & 0xffff) / 32768.0f - 1.0f;
      return 0.35f * float(std::sin(2.0 * M_PI * 220.0 * double(t) / kRate)) + 0.1f * noise;
   }

   constexpr int64_t kImpulseAt = 4800;
   float Impulse(int64_t t) { return t == kImpulseAt ? 1.0f : 0.0f; }

   double Db(double x) { return x > 0.0 ? 20.0 * std::log10(x) : -999.0; }

   double NullRms(const std::vector<float>& a, const std::vector<float>& b)
   {
      const size_t n = std::min(a.size(), b.size());
      double s = 0.0;
      for (size_t i = 0; i < n; i++)
      {
         const double d = double(a[i]) - double(b[i]);
         s += d * d;
      }
      return n ? std::sqrt(s / double(n)) : 1.0;
   }

   struct Case
   {
      const char* name;
      Plugin* plugin;
      bool fx;
      std::string patch;
      Script script;
      int64_t total;
   };

   // One render of a case. Realtime: the engine gets `warmMs` to load before the first block,
   // then blocks are paced like a host's audio thread.
   std::vector<float> RenderCase(const Case& c, int32 mode, int32 block, uint32* latency = nullptr, int warmMs = 4000)
   {
      setenv("INFINITE_PLUGIN_PATCH", c.patch.c_str(), 1);
      Instance in;
      std::vector<float> r;
      if (in.Open(*c.plugin, c.fx, mode, true, block))
      {
         if (latency)
            *latency = in.proc->getLatencySamples();
         if (mode == kRealtime)
            std::this_thread::sleep_for(std::chrono::milliseconds(warmMs));
         r = Render(in, c.total, mode == kRealtime, c.script);
         in.Close();
      }
      unsetenv("INFINITE_PLUGIN_PATCH");
      return r;
   }


   // ---- state (step 2.1) -------------------------------------------------------------------

   std::vector<uint8_t> GetBlob(Instance& in)
   {
      MemoryStream ms;
      std::vector<uint8_t> b;
      if (in.comp->getState(&ms) == kResultOk)
         b.assign(ms.getData(), ms.getData() + ms.getSize());
      return b;
   }

   tresult SetBlob(Instance& in, const std::vector<uint8_t>& b)
   {
      MemoryStream ms(const_cast<uint8_t*>(b.data()), TSize(b.size()));
      ms.seek(0, IBStream::kIBSeekSet, nullptr);
      return in.comp->setState(&ms);
   }

   struct BlobInfo
   {
      bool ok = false;
      std::string header;
      uint32_t patchBytes = 0, zBytes = 0;
   };

   BlobInfo Inspect(const std::vector<uint8_t>& b)
   {
      BlobInfo i;
      auto u32 = [&](size_t p) { return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24; };
      if (b.size() < 16 || u32(0) != 0x53464E49)
         return i;
      const uint32_t hl = u32(4);
      if (8 + size_t(hl) + 8 > b.size())
         return i;
      i.header.assign(b.begin() + 8, b.begin() + 8 + hl);
      i.patchBytes = u32(8 + hl);
      i.zBytes = u32(12 + hl);
      i.ok = 16 + size_t(hl) + i.zBytes == b.size();
      return i;
   }

   double MaxErrVsDelayedSignal(const std::vector<float>& out, int64_t from, int32 L)
   {
      double e = 0.0;
      for (int64_t t = std::max<int64_t>(from, L); t < int64_t(out.size()); t++)
         e = std::max(e, double(std::fabs(out[size_t(t)] - Signal(t - L))));
      return e;
   }

   void RunStateChecks(Plugin& fx, const std::string& r2)
   {
      char d[200];
      Script sig;
      sig.input = Signal;
      const int64_t len = int64_t(kRate);

      // Before any engine: a valid blob, header complete, no patch (= the starting patch).
      {
         Instance a;
         a.Open(fx, true, kOffline, false);
         const BlobInfo i = Inspect(GetBlob(a));
         a.Close(false);
         Check(i.ok && i.patchBytes == 0 && i.header.find("\"format\":\"infinite-plugin-state\"") != std::string::npos &&
                  i.header.find("\"build\":\"fx\"") != std::string::npos && i.header.find("\"slots\":[") != std::string::npos,
               "state: getState before any engine is a valid blob with no patch", i.header.substr(0, 80));
      }

      // A: the R2 patch, from the engine.
      setenv("INFINITE_PLUGIN_PATCH", r2.c_str(), 1);
      Instance a;
      const bool aOk = a.Open(fx, true, kOffline);
      unsetenv("INFINITE_PLUGIN_PATCH");
      const auto ra = aOk ? Render(a, len, false, sig) : std::vector<float>();
      const std::vector<uint8_t> blobA = aOk ? GetBlob(a) : std::vector<uint8_t>();
      if (aOk)
         a.Close();
      const BlobInfo ia = Inspect(blobA);
      std::snprintf(d, sizeof d, "patch %u bytes, stored %u bytes", ia.patchBytes, ia.zBytes);
      Check(ia.ok && ia.patchBytes > 100 && ia.zBytes < ia.patchBytes, "state: getState carries the engine's patch, compressed", d);

      // B: fresh instance, setState before activation (a host loading a project).
      {
         Instance b;
         b.Open(fx, true, kOffline, false);
         const tresult set = SetBlob(b, blobA);
         const auto before = GetBlob(b);
         Check(set == kResultOk && before == blobA, "state: before the engine runs, getState returns exactly what setState got");
         const bool act = b.Activate();
         const auto rb = act ? Render(b, len, false, sig) : std::vector<float>();
         const auto blobB = act ? GetBlob(b) : std::vector<uint8_t>();
         b.Close();
         std::snprintf(d, sizeof d, "null %.1f dBFS", Db(NullRms(ra, rb)));
         Check(!rb.empty() && rb == ra, "state: restored instance renders bit-identical to the original", d);
         std::snprintf(d, sizeof d, "%zu vs %zu bytes", blobB.size(), blobA.size());
         Check(blobB == blobA, "state: restored engine saves the same patch (round trip equal)", d);
      }

      // C: running on the starting patch, then setState swaps to R2; block change; engine killed.
      {
         Instance c;
         c.Open(fx, true, kOffline);
         const auto pass = Render(c, len / 2, false, sig);
         const double passErr = MaxErrVsDelayedSignal(pass, 0, kBlock);
         SetBlob(c, blobA);
         const auto swapped = Render(c, len, false, sig);
         const double swapErr = MaxErrVsDelayedSignal(swapped, kBlock, kBlock);
         std::snprintf(d, sizeof d, "starting patch error %.2g, after setState %.2g", passErr, swapErr);
         Check(passErr == 0.0 && swapErr > 1e-2, "state: setState while running swaps the engine's patch", d);

         c.proc->setProcessing(false);
         c.comp->setActive(false);
         c.block = 256;
         const auto pidsBefore = EnginePids();
         const bool re = c.Activate();
         const auto blobC = GetBlob(c);
         const auto pidsAfter = EnginePids();
         Check(re && pidsAfter != pidsBefore && blobC == blobA, "state: a block-size restart keeps the patch");

         if (!pidsAfter.empty())
            kill(pidsAfter.back(), SIGKILL);
         std::this_thread::sleep_for(std::chrono::milliseconds(200));
         const auto t0 = std::chrono::steady_clock::now();
         const auto blobDead = GetBlob(c);
         const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
         std::snprintf(d, sizeof d, "getState took %.0f ms", ms);
         Check(blobDead == blobA && ms < 2500.0, "state: engine gone, getState returns the last known patch", d);
         c.Close();
      }

      {
         Instance g;
         g.Open(fx, true, kOffline, false);
         const std::vector<uint8_t> junk { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
         const tresult r = SetBlob(g, junk);
         auto bad = blobA;
         bad.resize(bad.size() - 7);
         const tresult r2b = SetBlob(g, bad);
         g.Close(false);
         Check(r == kResultFalse && r2b == kResultFalse, "state: garbage and truncated blobs are refused");
      }
   }


   // ---- slots (step 2.2) on S1 -------------------------------------------------------------

   // The host's UI thread: the shell hops engine messages onto the main queue.
   void Pump() { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true); }

   // Slot 01: 0 for 1 s, up to 1 over 2 s, then 1.
   float SlotRamp(int64_t t)
   {
      const double s = double(t) / kRate;
      return float(std::max(0.0, std::min(1.0, (s - 1.0) / 2.0)));
   }
   float SlotZero(int64_t) { return 0.0f; }

   long long ReadStat(const std::string& path, const char* key)
   {
      long long v = -1;
      if (FILE* f = std::fopen(path.c_str(), "r"))
      {
         char line[512] = {};
         if (std::fgets(line, sizeof line, f))
            if (const char* k = std::strstr(line, key))
               std::sscanf(k + std::strlen(key), " %lld", &v);
         std::fclose(f);
      }
      return v;
   }

   class TestHandler : public IComponentHandler
   {
   public:
      int begins = 0, performs = 0, ends = 0, titles = 0, values = 0;
      double lastValue = -1.0;
      tresult PLUGIN_API beginEdit(ParamID id) override { begins += id == 0; return kResultOk; }
      tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override
      {
         if (id == 0)
         {
            performs++;
            lastValue = v;
         }
         return kResultOk;
      }
      tresult PLUGIN_API endEdit(ParamID id) override { ends += id == 0; return kResultOk; }
      tresult PLUGIN_API restartComponent(int32 flags) override
      {
         titles += (flags & kParamTitlesChanged) != 0;
         values += (flags & kParamValuesChanged) != 0;
         return kResultOk;
      }
      tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
      {
         if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid))
         {
            *obj = this;
            return kResultOk;
         }
         *obj = nullptr;
         return kNoInterface;
      }
      uint32 PLUGIN_API addRef() override { return 1000; }
      uint32 PLUGIN_API release() override { return 1000; }
   };

   // Host CC (hidden params -> MIDI) reaches a MIDI CC binding saved with a hardware device id.
   void RunCcCheck(Plugin& fx, const std::string& s2)
   {
      char d[200];
      Case c { "S2", &fx, true, s2, Script(), int64_t(kRate) * 2 };
      c.script.input = Signal;
      const auto idle = RenderCase(c, kOffline, kBlock);
      c.script.automation = SlotZero;
      c.script.automationId = 2007;
      const auto moved = RenderCase(c, kOffline, kBlock);
      const double li = Db(Rms(idle, int64_t(kRate), int64_t(2 * kRate))), lm = Db(Rms(moved, int64_t(kRate), int64_t(2 * kRate)));
      std::snprintf(d, sizeof d, "no CC %.1f dBFS, host CC7 = 0 %.1f dBFS", li, lm);
      Check(!idle.empty() && lm - li > 40.0, "cc: host CC drives a MIDI CC binding learned on hardware (reattached to Host)", d);
   }

   void RunSlotChecks(Plugin& fx, const std::string& s1)
   {
      char d[240];
      const int64_t len = int64_t(kRate) * 4;
      const double inRms = [&] {
         std::vector<float> x(static_cast<size_t>(kRate));
         for (size_t i = 0; i < x.size(); i++)
            x[i] = Signal(int64_t(i));
         return Rms(x, 0, int64_t(x.size()));
      }();
      auto rel = [&](const std::vector<float>& r, double a, double b) { return Db(Rms(r, int64_t(a * kRate), int64_t(b * kRate))) - Db(inRms); };

      Case plain { "S1", &fx, true, s1, Script(), len };
      plain.script.input = Signal;
      Case ramp = plain;
      ramp.script.automation = SlotRamp;

      // No host value: the patch's knob (1 = 0 dB) stays.
      const auto r0 = RenderCase(plain, kOffline, kBlock);
      std::snprintf(d, sizeof d, "level %.1f dB re input", rel(r0, 1.0, 4.0));
      Check(!r0.empty() && std::fabs(rel(r0, 1.0, 4.0)) < 1.0, "slots: a fresh instance keeps the patch's Macro value", d);

      // Automation moves the Macro: slot 0 -> -60 dB, slot 1 -> 0 dB.
      const std::string stats = "/tmp/infinite-plugin-stats-" + std::to_string(getpid()) + ".txt";
      setenv("INFINITE_PLUGIN_STATS", stats.c_str(), 1);
      std::remove(stats.c_str());
      const auto ra = RenderCase(ramp, kOffline, kBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      const long long parks = ReadStat(stats, "parks");
      const long long timeouts = ReadStat(stats, "parkTimeouts");
      unsetenv("INFINITE_PLUGIN_STATS");
      std::remove(stats.c_str());
      const double low = rel(ra, 0.3, 0.9), high = rel(ra, 3.3, 4.0);
      std::snprintf(d, sizeof d, "slot 0: %.1f dB, slot 1: %.1f dB re input", low, high);
      Check(low < -50.0 && std::fabs(high) < 1.0, "slots: automation drives the bound Macro", d);

      const auto rb = RenderCase(ramp, kOffline, kBlock);
      std::snprintf(d, sizeof d, "null %.1f dBFS, parks %lld, park timeouts %lld", Db(NullRms(ra, rb)), parks, timeouts);
      Check(!rb.empty() && ra == rb && parks > 0 && timeouts == 0, "slots: two automated offline bounces bit-identical (parked)", d);

      const auto rt = RenderCase(ramp, kRealtime, kBlock);
      const double nul = Db(NullRms(ra, rt));
      std::snprintf(d, sizeof d, "null %.1f dBFS", nul);
      Check(ra.size() == rt.size() && nul <= -40.0, "slots: automated offline vs realtime null <= -40 dBFS", d);

      // A saved slot value comes back with the project (resent to the engine at start).
      {
         setenv("INFINITE_PLUGIN_PATCH", s1.c_str(), 1);
         Instance a;
         const bool ok = a.Open(fx, true, kOffline);
         unsetenv("INFINITE_PLUGIN_PATCH");
         Script z = plain.script;
         z.automation = SlotZero;
         if (ok)
            Render(a, int64_t(kRate), false, z);
         const auto blob = ok ? GetBlob(a) : std::vector<uint8_t>();
         if (ok)
            a.Close();
         const BlobInfo bi = Inspect(blob);
         Instance b;
         b.Open(fx, true, kOffline, false);
         SetBlob(b, blob);
         const auto rr = b.Activate() ? Render(b, len, false, plain.script) : std::vector<float>();
         b.Close();
         std::snprintf(d, sizeof d, "%s, level %.1f dB re input", bi.header.find("\"slotsSet\":1,") != std::string::npos ? "slotsSet 1" : "no slotsSet", rel(rr, 0.5, 4.0));
         Check(bi.header.find("\"slotsSet\":1,") != std::string::npos && rel(rr, 0.5, 4.0) < -50.0,
               "slots: a saved slot value comes back with the project", d);
      }

      // Controller connected: names, sync and an edit turned in Infinite.
      {
         setenv("INFINITE_PLUGIN_PATCH", s1.c_str(), 1);
         setenv("INFINITE_PLUGIN_TEST_EDIT", "300:0.25", 1);
         Instance a;
         TestHandler handler;
         IPtr<IEditController> ec;
         bool ok = a.Open(fx, true, kRealtime, false);
         TUID cid;
         if (ok && a.comp->getControllerClassId(cid) == kResultOk)
            ec = fx.mod->getFactory().createInstance<IEditController>(VST3::UID::fromTUID(cid));
         ok = ok && ec && ec->initialize(&gHost) == kResultOk;
         FUnknownPtr<IConnectionPoint> cpComp(a.comp), cpCtrl(ec);
         if (ok && cpComp && cpCtrl)
         {
            ec->setComponentHandler(&handler);
            cpComp->connect(cpCtrl);
            cpCtrl->connect(cpComp);
            ok = a.Activate();
         }
         else
            ok = false;
         unsetenv("INFINITE_PLUGIN_PATCH");
         unsetenv("INFINITE_PLUGIN_TEST_EDIT");
         std::vector<float> r;
         if (ok)
         {
            for (int i = 0; i < 400; i++)
            {
               Pump();
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Script sc = plain.script;
            sc.perBlock = Pump;
            r = Render(a, int64_t(kRate) * 6, true, sc);
            for (int i = 0; i < 50; i++)
            {
               Pump();
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
         }
         ParameterInfo pi {};
         if (ec)
            ec->getParameterInfo(0, pi);
         const bool named = std::u16string(reinterpret_cast<const char16_t*>(pi.title)) == u"Level";
         std::snprintf(d, sizeof d, "title %s, title restarts %d, value restarts %d", named ? "Level" : "not Level", handler.titles, handler.values);
         Check(ok && named && handler.titles >= 1 && handler.values >= 1, "slots: a bound slot takes the Macro's name and value", d);
         std::snprintf(d, sizeof d, "begin %d, perform %d (last %.3f), end %d, level after %.1f dB re input", handler.begins, handler.performs,
                       handler.lastValue, handler.ends, r.empty() ? -999.0 : rel(r, 5.0, 6.0));
         Check(ok && handler.begins >= 1 && handler.performs >= 1 && std::fabs(handler.lastValue - 0.25) < 1e-3 && handler.ends == handler.begins &&
                  !r.empty() && std::fabs(rel(r, 5.0, 6.0) + 45.0) < 2.0,
               "slots: turning the Macro in Infinite reaches the host as begin / perform / end", d);
         if (ok)
         {
            cpComp->disconnect(cpCtrl);
            cpCtrl->disconnect(cpComp);
            a.Close();
         }
         if (ec)
         {
            ec->setComponentHandler(nullptr);
            ec->terminate();
         }
      }
   }

   // ---- View (2.3) ---------------------------------------------------------------------------
   long long ReadStatLine(const std::string& path, const char* prefix, const char* key, int nth = 0)
   {
      long long v = -1;
      if (FILE* f = std::fopen(path.c_str(), "r"))
      {
         char line[512] = {};
         int seen = 0;
         while (std::fgets(line, sizeof line, f))
            if (std::strncmp(line, prefix, std::strlen(prefix)) == 0 && seen++ == nth)
            {
               if (const char* k = std::strstr(line, key))
                  std::sscanf(k + std::strlen(key), " %lld", &v);
               break;
            }
         std::fclose(f);
      }
      return v;
   }

   // No graphics (2.4): an engine that cannot open a GL window reports it and quits; the view says
   // so and offers no Restart. INFINITE_PLUGIN_NO_GL forces the path.
   void RunNoGraphicsCheck(Plugin& inst)
   {
      char d[256];
      setenv("INFINITE_PLUGIN_NO_GL", "1", 1);
      Instance a;
      IPtr<IEditController> ec;
      bool ok = a.Open(inst, false, kRealtime, false);
      TUID cid;
      if (ok && a.comp->getControllerClassId(cid) == kResultOk)
         ec = inst.mod->getFactory().createInstance<IEditController>(VST3::UID::fromTUID(cid));
      ok = ok && ec && ec->initialize(&gHost) == kResultOk;
      FUnknownPtr<IConnectionPoint> cpComp(a.comp), cpCtrl(ec);
      ok = ok && cpComp && cpCtrl;
      if (ok)
      {
         cpComp->connect(cpCtrl);
         cpCtrl->connect(cpComp);
      }
      IPtr<IPlugView> view;
      if (ok)
         view = owned(ec->createView(ViewType::kEditor));
      void* parent = ViewCheck::NewParent(1280, 800, nullptr);
      std::string label, button;
      bool said = false;
      if (view)
      {
         ViewRect r { 0, 0, 1280, 800 };
         view->onSize(&r);
         view->attached(parent, kPlatformTypeNSView);
         void* child = ViewCheck::Child(parent);
         const auto t0 = std::chrono::steady_clock::now();
         while (child && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 15.0)
         {
            ViewCheck::Pump(20);
            if (ViewCheck::Label(child) == "No graphics on this machine")
            {
               said = true;
               break;
            }
         }
         ViewCheck::Pump(1000); // a Stopped from the engine's exit must not replace the message
         if (child)
         {
            label = ViewCheck::Label(child);
            button = ViewCheck::Button(child);
         }
      }
      const bool gone = EnginePids().empty();
      std::snprintf(d, sizeof d, "label \"%s\", button \"%s\", engine %s", label.c_str(), button.c_str(), gone ? "gone" : "still running");
      Check(said && label == "No graphics on this machine" && button.empty() && gone,
            "view: an engine with no graphics says so in the editor (no Restart) and quits", d);
      if (view)
      {
         view->removed();
         view = nullptr;
      }
      if (ok)
      {
         cpComp->disconnect(cpCtrl);
         cpCtrl->disconnect(cpComp);
      }
      if (ec)
         ec->terminate();
      if (a.comp)
         a.Close(false);
      ViewCheck::FreeParent(parent);
      unsetenv("INFINITE_PLUGIN_NO_GL");
   }

   void RunViewChecks(Plugin& inst)
   {
      char d[256];
      const std::string engineStats = "/tmp/infinite-plugin-stats-" + std::to_string(getpid()) + ".txt";
      const std::string viewStats = "/tmp/infinite-plugin-view-" + std::to_string(getpid()) + ".txt";
      std::remove(engineStats.c_str());
      std::remove(viewStats.c_str());
      setenv("INFINITE_PLUGIN_STATS", engineStats.c_str(), 1);
      setenv("INFINITE_PLUGIN_VIEW_STATS", viewStats.c_str(), 1);

      Instance a;
      IPtr<IEditController> ec;
      bool ok = a.Open(inst, false, kRealtime, false);
      TUID cid;
      if (ok && a.comp->getControllerClassId(cid) == kResultOk)
         ec = inst.mod->getFactory().createInstance<IEditController>(VST3::UID::fromTUID(cid));
      ok = ok && ec && ec->initialize(&gHost) == kResultOk;
      FUnknownPtr<IConnectionPoint> cpComp(a.comp), cpCtrl(ec);
      ok = ok && cpComp && cpCtrl;
      if (ok)
      {
         cpComp->connect(cpCtrl);
         cpCtrl->connect(cpComp);
      }
      IPtr<IPlugView> view;
      if (ok)
         view = owned(ec->createView(ViewType::kEditor));
      Check(ok && view && view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue && view->canResize() == kResultTrue,
            "view: createView gives a resizable NSView editor");
      if (!view)
      {
         if (ec)
            ec->terminate();
         if (a.comp)
            a.Close(false);
         unsetenv("INFINITE_PLUGIN_STATS");
         unsetenv("INFINITE_PLUGIN_VIEW_STATS");
         return;
      }

      // M-10's size: 1600 x 1000 points; on a 2x screen that is B-6's 3200 x 2000 readback.
      constexpr int kW = 1600, kH = 1000;
      double scale = 1.0;
      void* parent = ViewCheck::NewParent(kW, kH, &scale);
      ViewRect r { 0, 0, kW, kH };
      view->onSize(&r);
      const auto t0 = std::chrono::steady_clock::now();
      const bool attached = view->attached(parent, kPlatformTypeNSView) == kResultOk;
      void* child = ViewCheck::Child(parent);
      int w = 0, h = 0;
      std::vector<uint32_t> px, base;
      auto waitSize = [&](int ww, int hh, int ms) {
         const auto s = std::chrono::steady_clock::now();
         while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count() < ms)
         {
            ViewCheck::Pump(5);
            if (ViewCheck::Surface(child, &w, &h, &px) && w == ww && h == hh)
               return true;
         }
         return false;
      };
      const int pw = int(kW * scale), ph = int(kH * scale);
      const bool first = attached && child && waitSize(pw, ph, 30000);
      const double firstS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      size_t distinct = 0;
      if (first)
      {
         std::vector<uint32_t> sorted = px;
         std::sort(sorted.begin(), sorted.end());
         distinct = size_t(std::unique(sorted.begin(), sorted.end()) - sorted.begin());
      }
      std::snprintf(d, sizeof d, "%d x %d px (scale %.0f) after %.1f s, %zu distinct colours in 96 x 96 samples", w, h, scale, firstS, distinct);
      Check(first && distinct > 8, "view: opened before activation, the engine starts and its frames fill the view", d);

      // Input -> pixels: Shift+M toggles the docked modulation matrix. Twenty trials, both ways.
      constexpr unsigned long kShift = 1ul << 17; // NSEventModifierFlagShift
      std::vector<double> lat;
      int missed = 0;
      std::string all;
      constexpr int kTrials = 20;
      for (int trial = 0; first && trial < kTrials; trial++)
      {
         ViewCheck::Pump(400);
         ViewCheck::Surface(child, &w, &h, &base);
         const auto k0 = std::chrono::steady_clock::now();
         ViewCheck::Flags(child, 56, kShift);
         ViewCheck::Key(child, 46, "M", kShift, true);
         ViewCheck::Key(child, 46, "M", kShift, false);
         ViewCheck::Flags(child, 56, 0);
         double ms = -1.0;
         while (ms < 0.0)
         {
            ViewCheck::Pump(1);
            const double el = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - k0).count();
            if (el > 1000.0)
               break;
            if (!ViewCheck::Surface(child, &w, &h, &px) || px.size() != base.size())
               continue;
            size_t diff = 0;
            for (size_t i = 0; i < px.size(); i++)
               diff += px[i] != base[i];
            if (diff > px.size() / 100)
               ms = el;
         }
         if (ms < 0.0)
            missed++;
         else
            lat.push_back(ms);
         char one[16];
         std::snprintf(one, sizeof one, "%s%.0f", all.empty() ? "" : " ", ms);
         all += one;
      }
      if (getenv("INFINITE_PLUGIN_VIEW_VERBOSE"))
         std::printf("      latencies ms: %s\n", all.c_str());
      std::sort(lat.begin(), lat.end());
      const double p50 = lat.empty() ? -1.0 : lat[lat.size() / 2];
      const double p95 = lat.empty() ? -1.0 : lat[std::min(lat.size() - 1, size_t(std::ceil(0.95 * double(lat.size()))) - 1)];
      std::snprintf(d, sizeof d, "%zu / %d trials changed the picture, p50 %.1f ms, p95 %.1f ms", lat.size(), kTrials, p50, p95);
      Check(first && missed == 0, "view: keys reach Infinite (Shift+M toggles the modulation matrix)", d);
      Check(first && missed == 0 && p95 <= 50.0, "M-10 input to pixels <= 50 ms p95", d);

      // A steady stretch at the M-10 size, for the readback and blit means.
      ViewCheck::Pump(3000);

      // Resize.
      ViewRect r2 { 0, 0, 1280, 800 };
      view->onSize(&r2);
      const bool resized = waitSize(int(1280 * scale), int(800 * scale), 5000);
      std::snprintf(d, sizeof d, "%d x %d px", w, h);
      Check(resized, "view: a resize gives frames at the new size", d);

      // Pop-out (2.4): what the top-bar tile asks for, sent as the processor message. The engine's
      // window comes on screen, the view says so and offers Bring back; pressing it returns frames.
      {
         const auto pids = EnginePids();
         const int pid = pids.empty() ? 0 : pids[0];
         auto popout = [&](int on) {
            IPtr<Vst::HostMessage> m = owned(new Vst::HostMessage);
            m->setMessageID("popout");
            m->getAttributes()->setInt("on", on);
            cpComp->notify(m);
         };
         auto waitFor = [&](const std::function<bool()>& ok, int ms) {
            const auto s = std::chrono::steady_clock::now();
            while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count() < ms)
            {
               ViewCheck::Pump(10);
               if (ok())
                  return true;
            }
            return false;
         };
         const int before = ViewCheck::OnScreenWindows(pid);
         popout(1);
         const bool out = waitFor([&] { return ViewCheck::Label(child) == "In its own window" && ViewCheck::OnScreenWindows(pid) > before; }, 5000);
         const bool blank = !ViewCheck::Surface(child, &w, &h, nullptr);
         std::snprintf(d, sizeof d, "label \"%s\", button \"%s\", engine windows on screen %d -> %d",
                       ViewCheck::Label(child).c_str(), ViewCheck::Button(child).c_str(), before, ViewCheck::OnScreenWindows(pid));
         Check(out && blank && ViewCheck::Button(child) == "Bring back", "view: popped out, the engine's window is on screen and the view says so", d);
         ViewCheck::Pump(500);
         ViewCheck::Press(child);
         const bool back = waitSize(int(1280 * scale), int(800 * scale), 5000);
         const auto h0 = std::chrono::steady_clock::now();
         const bool hidden = waitFor([&] { return ViewCheck::OnScreenWindows(pid) == before; }, 3000);
         const double hideMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - h0).count();
         std::snprintf(d, sizeof d, "label \"%s\", engine windows on screen %d (%.0f ms after frames returned)",
                       ViewCheck::Label(child).c_str(), ViewCheck::OnScreenWindows(pid), hideMs);
         Check(back && hidden && ViewCheck::Label(child).empty(),
               "view: Bring back hides the engine's window and frames return", d);
      }

      // Close, wait, reopen: no readback while closed, frames again after.
      view->removed();
      view = nullptr;
      ViewCheck::Pump(2000);
      IPtr<IPlugView> again = owned(ec->createView(ViewType::kEditor));
      bool reopened = false;
      if (again)
      {
         again->onSize(&r2);
         again->attached(parent, kPlatformTypeNSView);
         child = ViewCheck::Child(parent);
         reopened = child && waitSize(int(1280 * scale), int(800 * scale), 10000);
         ViewCheck::Pump(500);
         again->removed();
         again = nullptr;
      }
      ViewCheck::Pump(200);
      const long long last1 = ReadStatLine(viewStats, "view", "lastSeq", 0);
      const long long first2 = ReadStatLine(viewStats, "view", "firstSeq", 1);
      std::snprintf(d, sizeof d, "last frame shown %lld, first after 2 s closed %lld", last1, first2);
      Check(reopened && last1 > 0 && first2 > last1 && first2 - last1 <= 6, "view: closed, the engine reads nothing back; reopened, frames return", d);

      cpComp->disconnect(cpCtrl);
      cpCtrl->disconnect(cpComp);
      ec->terminate();
      a.Close(false);
      ViewCheck::FreeParent(parent);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));

      const long long frames = ReadStat(engineStats, "frames");
      const long long readUs = ReadStat(engineStats, "readbackUs");
      const long long blitUs = ReadStatLine(viewStats, "view", "blitUs", 0);
      const double cpu = (readUs >= 0 && blitUs >= 0) ? double(readUs + blitUs) * 30.0 / 1e6 * 100.0 : -1.0;
      std::snprintf(d, sizeof d, "B-6 readback %lld us/frame (%lld frames), view blit %lld us/frame -> %.1f %% of one core at 30 fps",
                    readUs, frames, blitUs, cpu);
      Check(frames > 0 && cpu >= 0.0 && cpu <= 10.0, "M-10 pixel pipe <= 10 % of one core (1600 x 1000 @2x, 30 fps)", d);
      unsetenv("INFINITE_PLUGIN_STATS");
      unsetenv("INFINITE_PLUGIN_VIEW_STATS");
      std::remove(engineStats.c_str());
      std::remove(viewStats.c_str());
   }

   int RunMetrics(Plugin& inst, Plugin& fx, const std::string& r1, const std::string& r2)
   {
      Script notes;
      for (int k = 0; k < 24; k++)
         notes.notes.push_back({ 2400 + int64_t(k) * 14000, 2400 + int64_t(k) * 14000 + 9000 });
      Script sig;
      sig.input = Signal;
      Script imp;
      imp.input = Impulse;
      const int64_t len = int64_t(kRate) * 8;
      Case cR1 { "R1", &inst, false, r1, notes, len };
      Case cR2 { "R2", &fx, true, r2, sig, len };
      Case cImp { "R2 impulse", &fx, true, r2, imp, int64_t(kRate) };

      std::printf("metrics: R1 = %s\n         R2 = %s\n", r1.c_str(), r2.c_str());
      char d[200];

      // M-4: the same project rendered twice offline, fresh instances, is bit-identical.
      for (const Case* c : { &cR1, &cR2 })
      {
         const auto a = RenderCase(*c, kOffline, kBlock);
         const auto b = RenderCase(*c, kOffline, kBlock);
         std::snprintf(d, sizeof d, "%zu samples, rms %.1f dBFS, null %.1f dBFS", a.size(), Db(Rms(a, 0, int64_t(a.size()))), Db(NullRms(a, b)));
         Check(!a.empty() && Rms(a, 0, int64_t(a.size())) > 1e-3 && a == b, (std::string("M-4 ") + c->name + ": two offline renders bit-identical").c_str(), d);
         // Guard: the R patch really loaded (it does not sound like the build's starting patch).
         Case start = *c;
         start.patch = "/nonexistent";
         const auto s0 = RenderCase(start, kOffline, kBlock);
         std::snprintf(d, sizeof d, "null vs starting patch %.1f dBFS", Db(NullRms(a, s0)));
         Check(Db(NullRms(a, s0)) > -40.0, (std::string("patch loaded: ") + c->name + " differs from the starting patch").c_str(), d);
      }

      // M-5: offline vs realtime null <= -60 dBFS RMS at 32 / 64 / 512 / 2048.
      for (int32 block : { 32, 64, 512, 2048 })
         for (const Case* c : { &cR1, &cR2 })
         {
            const auto off = RenderCase(*c, kOffline, block);
            const auto rt = RenderCase(*c, kRealtime, block);
            const double nul = Db(NullRms(off, rt));
            std::snprintf(d, sizeof d, "block %d: rms %.1f dBFS, null %.1f dBFS", block, Db(Rms(off, 0, int64_t(off.size()))), nul);
            Check(!off.empty() && off.size() == rt.size() && nul <= -60.0, (std::string("M-5 ") + c->name + ": offline vs realtime null").c_str(), d);
         }

      // M-6: impulse through R2; the first output sample sits exactly at the reported latency.
      for (int32 block : { 32, 64, 512, 2048 })
         for (int32 mode : { int32(kOffline), int32(kRealtime) })
         {
            uint32 L = 0;
            const auto r = RenderCase(cImp, mode, block, &L);
            int64_t first = -1;
            for (size_t i = 0; i < r.size() && first < 0; i++)
               if (std::fabs(r[i]) > 1e-6f)
                  first = int64_t(i);
            const int64_t offset = first < 0 ? -1 : first - kImpulseAt;
            std::snprintf(d, sizeof d, "%s block %d: reported %u, measured %lld", mode == kOffline ? "offline" : "realtime", block, L, (long long)offset);
            Check(offset == int64_t(L), "M-6 R2 impulse: measured offset == reported latency", d);
         }

      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      Check(EnginePids().empty(), "no engine left");
      std::printf("%s (%d failed)\n", gFails == 0 ? "METRICS PASS" : "METRICS FAIL", gFails);
      return gFails == 0 ? 0 : 1;
   }
}

int main(int argc, char** argv)
{
   if (argc >= 3 && std::string(argv[1]) == "--view")
   {
      Plugin inst;
      std::string err;
      if (!inst.Load(argv[2], err))
      {
         std::fprintf(stderr, "load failed: %s\n", err.c_str());
         return 1;
      }
      RunViewChecks(inst);
      RunNoGraphicsCheck(inst);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      Check(EnginePids().empty(), "no engine left");
      std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
      return gFails == 0 ? 0 : 1;
   }
   if (argc >= 6 && std::string(argv[1]) == "--metrics")
   {
      Plugin inst, fx;
      std::string err;
      if (!inst.Load(argv[2], err) || !fx.Load(argv[3], err))
      {
         std::fprintf(stderr, "load failed: %s\n", err.c_str());
         return 1;
      }
      return RunMetrics(inst, fx, argv[4], argv[5]);
   }
   if (argc < 3)
   {
      std::fprintf(stderr, "usage: INFINITE_PLUGIN_ENGINE=<Infinite binary> %s [--metrics] <Instrument.vst3> <FX.vst3> [<R1.inf> <R2.inf>]\n", argv[0]);
      return 2;
   }
   Plugin inst, fx;
   std::string err;
   if (!inst.Load(argv[1], err) || !fx.Load(argv[2], err))
   {
      std::fprintf(stderr, "load failed: %s\n", err.c_str());
      return 1;
   }
   Check(inst.info.name() == "Infinite Instrument" && inst.info.subCategoriesString().find("Instrument") != std::string::npos,
         "instrument: class name and Instrument category", inst.info.name() + " / " + inst.info.subCategoriesString());
   Check(fx.info.name() == "Infinite FX" && fx.info.subCategoriesString().find("Fx") != std::string::npos,
         "fx: class name and Fx category", fx.info.name() + " / " + fx.info.subCategoriesString());

   const size_t baseline = EnginePids().size();
   {
      Instance a, b;
      const bool ok = a.Open(inst, false, kOffline, false) && b.Open(fx, true, kOffline, false);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      const size_t during = EnginePids().size();
      a.Close(false);
      b.Close(false);
      Check(ok && during == baseline, "scan: initialize + terminate spawns no engine");
   }

   CheckController(inst, "instrument");
   CheckController(fx, "fx");

   // FX offline: two instances, random block sizes.
   {
      Instance a, b;
      if (!a.Open(fx, true, kOffline) || !b.Open(fx, true, kOffline))
      {
         Check(false, "fx: offline instances open");
         return 1;
      }
      char d[160];
      std::snprintf(d, sizeof d, "latency %u, tail %s", a.proc->getLatencySamples(), a.proc->getTailSamples() == kInfiniteTail ? "infinite" : "finite");
      Check(a.proc->getLatencySamples() == uint32(kBlock) && a.proc->getTailSamples() == kInfiniteTail, "latency = max block, tail infinite", d);
      const int64_t total = int64_t(kRate) * 2;
      const auto ra = Render(a, total, false);
      const auto rb = Render(b, total, false);
      a.Close();
      b.Close();
      const double e = MaxErrDelayed(ra, 0);
      std::snprintf(d, sizeof d, "max error %.2e", e);
      Check(e < 1e-4, "fx offline: starting patch passes the input, delayed by L, random block sizes", d);
      Check(ra == rb, "fx offline: two instances bit-identical");
   }

   // Instrument offline: note on -> sound; bypass -> silence.
   {
      Instance a;
      if (!a.Open(inst, false, kOffline))
      {
         Check(false, "instrument: offline instance opens");
         return 1;
      }
      Script s;
      s.noteOnAt = 12000;
      s.noteOffAt = 60000;
      s.bypassAt = 40000;
      const auto r = Render(a, 72000, false, s);
      a.Close();
      const double before = Rms(r, 0, s.noteOnAt + kBlock);
      const double during = Rms(r, s.noteOnAt + kBlock + 1000, s.bypassAt);
      const double bypassed = Rms(r, s.bypassAt + kBlock + 4800, int64_t(r.size()));
      char d[160];
      std::snprintf(d, sizeof d, "rms before %.2e, after note %.3f, bypassed %.2e", before, during, bypassed);
      Check(before < 1e-5 && during > 1e-3, "instrument offline: host note-on plays the starting synth", d);
      Check(bypassed < 1e-5, "instrument offline: bypass parameter silences it", d);
   }

   // FX realtime, paced like a host: exact once the engine is up.
   {
      Instance a;
      if (!a.Open(fx, true, kRealtime))
      {
         Check(false, "fx: realtime instance opens");
         return 1;
      }
      const int64_t total = int64_t(kRate) * 4;
      const auto r = Render(a, total, true);
      a.Close();
      const double e = MaxErrDelayed(r, total - int64_t(kRate));
      char d[160];
      std::snprintf(d, sizeof d, "max error over the last second %.2e", e);
      Check(e < 1e-4, "fx realtime: paced blocks pass through once the engine is up", d);
   }

   // Crash drill: kill the engine mid-run.
   {
      Instance a;
      a.Open(fx, true, kRealtime);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      const auto pids = EnginePids();
      int victim = pids.empty() ? 0 : pids.back();
      const int64_t total = int64_t(kRate) * 4;
      const auto r = Render(a, total, true, Script(), &victim);
      a.Close();
      double tail = 0.0;
      for (size_t i = r.size() * 7 / 8; i < r.size(); i++)
         tail = std::max(tail, double(std::fabs(r[i])));
      char d[160];
      std::snprintf(d, sizeof d, "peak after kill %.3g", tail);
      Check(!pids.empty() && tail == 0.0, "crash drill: engine killed, host alive, plugin silent", d);
   }

   RunStateChecks(fx, std::string(INFINITE_SOURCE_ROOT) + "/tests/plugin/patches/R2-fx.inf");
   RunSlotChecks(fx, std::string(INFINITE_SOURCE_ROOT) + "/tests/plugin/patches/S1-slot.inf");
   RunCcCheck(fx, std::string(INFINITE_SOURCE_ROOT) + "/tests/plugin/patches/S2-cc.inf");
   RunViewChecks(inst);
   RunNoGraphicsCheck(inst);

   std::this_thread::sleep_for(std::chrono::milliseconds(300));
   Check(EnginePids().size() == baseline, "no engine left after terminate");
   std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
   return gFails == 0 ? 0 : 1;
}
