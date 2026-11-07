// plugin_shell_check: loads "Infinite Instrument.vst3" and "Infinite FX.vst3" the way a DAW does
// and runs them against the real engine (docs/plans/vst-au/README.md step 1.5).
//
//   INFINITE_PLUGIN_ENGINE=<Infinite binary> plugin_shell_check <Instrument.vst3> <FX.vst3>
//
// Checks: class names and categories, a scan (initialize + terminate) spawns no engine, the
// controller's 32 slots + bypass + 130 hidden MIDI-mapping parameters and their CC assignments,
// latency = max block and an infinite tail, FX offline pass-through with random block sizes
// (exact, delayed by L), two offline FX instances bit-identical, Instrument note-on -> sound and
// bypass -> silence, FX realtime paced, a killed engine leaves the host alive and the plugin
// silent, and no engine is left after terminate. PASS/FAIL per check; exit 0 only when all pass.
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

      bool Open(Plugin& p, bool isFx, int32 processMode, bool activate = true)
      {
         fx = isFx;
         mode = processMode;
         comp = p.mod->getFactory().createInstance<IComponent>(p.info.ID());
         if (!comp || comp->initialize(&gHost) != kResultOk)
            return false;
         proc = FUnknownPtr<IAudioProcessor>(comp.get());
         if (!proc)
            return false;
         if (!activate)
            return true;
         ProcessSetup setup { processMode, kSample32, kBlock, kRate };
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
   };

   // Renders `total` frames in pseudo-random block sizes (1..kBlock); returns the left channel.
   std::vector<float> Render(Instance& in, int64_t total, bool paced, const Script& s = Script(), int* killPid = nullptr)
   {
      std::vector<float> out;
      out.reserve(size_t(total));
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
            il[i] = ir[i] = Sine(t + i);
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
}

int main(int argc, char** argv)
{
   if (argc < 3)
   {
      std::fprintf(stderr, "usage: INFINITE_PLUGIN_ENGINE=<Infinite binary> %s <Instrument.vst3> <FX.vst3>\n", argv[0]);
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

   std::this_thread::sleep_for(std::chrono::milliseconds(300));
   Check(EnginePids().size() == baseline, "no engine left after terminate");
   std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
   return gFails == 0 ? 0 : 1;
}
