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
// garbage is refused. PASS/FAIL per check; exit 0 only when all pass.
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <signal.h>
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
         ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
         ctx.sampleRate = kRate;
         ctx.tempo = 120.0;
         ctx.projectTimeSamples = t;
         ctx.projectTimeMusic = double(t) / kRate * 2.0;
         d.numSamples = n;
         in.proc->process(d);
         for (int32 i = 0; i < n; i++)
            out.push_back(ol[i]);
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

   std::this_thread::sleep_for(std::chrono::milliseconds(300));
   Check(EnginePids().size() == baseline, "no engine left after terminate");
   std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
   return gFails == 0 ? 0 : 1;
}
