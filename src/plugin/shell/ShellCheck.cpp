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
// controller's 128 slots (hidden until bound) + bypass + 130 hidden MIDI-mapping parameters and their CC assignments,
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
// binding saved with a hardware device id. Drill (2.5; --drill runs only these): crash restart
// opens the last pushed patch, a SIGSTOPped engine is killed by the watchdog, a suspend releases
// held notes, the picture choice round-trips. View (2.3, macOS; --view runs only these): an editor
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
#include <libproc.h>

#include "ShellCheckMac.h"
#include "core/License.h"
#include "platform/common/Ipc.h"
#include "monocypher/monocypher-ed25519.h"

#include <algorithm>
#include <atomic>
#include <time.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <signal.h>
#include <sys/wait.h>
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

   // ---- licence fixture (step 2.6) ------------------------------------------------------------
   // A test key pair from a fixed seed; the build accepts its public key through
   // $INFINITE_LICENSE_PUBKEY (INFINITE_LICENSE_TEST_KEY builds only). Keys live in a temp folder
   // ($INFINITE_LICENSE_DIR), never the user's own.
   struct LicenseFixture
   {
      std::string dir;
      uint8_t secret[64] = {}, pub[32] = {};

      static std::string B64Url(const std::string& in)
      {
         static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
         std::string out;
         uint32_t acc = 0;
         int bits = 0;
         for (unsigned char c : in)
         {
            acc = (acc << 8) | c;
            bits += 8;
            while (bits >= 6)
            {
               bits -= 6;
               out += a[(acc >> bits) & 63];
            }
         }
         if (bits > 0)
            out += a[(acc << (6 - bits)) & 63];
         return out;
      }

      std::string Key(const std::string& id, int major, int v = 1) const
      {
         const std::string payload = "{\"v\":" + std::to_string(v) + ",\"id\":\"" + id +
                                     "\",\"email\":\"check@example.com\",\"txn\":\"txn_check\",\"seats\":1,\"major\":" +
                                     std::to_string(major) + ",\"iat\":1762560000}";
         const std::string signedPart = "INF1." + B64Url(payload);
         uint8_t sig[64];
         crypto_ed25519_sign(sig, secret, reinterpret_cast<const uint8_t*>(signedPart.data()), signedPart.size());
         return signedPart + "." + B64Url(std::string(reinterpret_cast<const char*>(sig), 64));
      }

      static void Write(const std::string& path, const std::string& text)
      {
         std::ofstream f(path, std::ios::binary | std::ios::trunc);
         f << text;
      }
      void WriteKey(const std::string& key) const { Write(dir + "/license.inflic", key + "\n"); }
      void RemoveKey() const { std::remove((dir + "/license.inflic").c_str()); }
      std::string StoredKey() const
      {
         std::ifstream f(dir + "/license.inflic");
         std::string line;
         std::getline(f, line);
         return line;
      }
      // A fresh refund list: the engine does not fetch one.
      void WriteRevoked(const std::string& idsJson, int64_t fetched) const
      {
         Write(dir + "/license-revoked.json", "{\"revoked\":[" + idsJson + "],\"fetched\":" + std::to_string(fetched) + "}");
      }

      void Setup()
      {
         char tmpl[] = "/tmp/infinite-license-check-XXXXXX";
         dir = mkdtemp(tmpl) ? tmpl : "/tmp/infinite-license-check";
         uint8_t seed[32];
         for (int i = 0; i < 32; i++)
            seed[i] = uint8_t(0xA5 ^ (i * 7));
         crypto_ed25519_key_pair(secret, pub, seed);
         char hex[65];
         for (int i = 0; i < 32; i++)
            std::snprintf(hex + 2 * i, 3, "%02x", pub[i]);
         setenv("INFINITE_LICENSE_DIR", dir.c_str(), 1);
         setenv("INFINITE_LICENSE_PUBKEY", hex, 1);
         WriteKey(Key("INF-CHEC-K234", 1));
         WriteRevoked("", int64_t(std::time(nullptr)));
      }
      ~LicenseFixture()
      {
         if (!dir.empty())
         {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
         }
      }
   };
   LicenseFixture gLicense;

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
      double rate = kRate; // C6 changes it between activations

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
         ProcessSetup setup { mode, kSample32, block, rate };
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
         ctx.sampleRate = in.rate;
         ctx.tempo = 120.0;
         ctx.projectTimeSamples = t;
         ctx.projectTimeMusic = double(t) / in.rate * 2.0;
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
            next += std::chrono::nanoseconds(int64_t(1e9 * n / in.rate));
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
      std::string what = std::string(label) + ": 128 hidden slots + bypass + 130 hidden MIDI-mapping params";
      Check(count == 259 && slots == 0 && bypass == 1 && hidden == 258, what.c_str(), d);

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

   // ---- soak (step 2.7: M-7, M-8, M-9; driven by tools/plugin/soak.py) -----------------------
   // One host audio thread processes every instance in turn each 256-sample block at 48 kHz, paced
   // on absolute deadlines like a DAW's callback: R1 Instruments get a note pattern, R2 FX get
   // Signal(). An idle phase (silence, no notes) comes first for the idle memory reading. Every
   // `reportSec` one JSON line goes to stdout: phase, xruns per instance since the measured window
   // began (the shell's own counter, "stats" message), engine pids, and the host thread's CPU per
   // instance (thread CPU time inside process() / wall, M-8: the shell + IPC cost, as the engine
   // runs in its own process). Exit 0 when no instance missed a block, none restarted, and every
   // instance stayed under 3 % of a core.

   struct SoakStats
   {
      int64 xruns = 0, pid = 0, licensed = 0;
   };

   SoakStats QueryStats(Instance& a)
   {
      SoakStats st;
      FUnknownPtr<IConnectionPoint> cp(a.comp);
      IPtr<Vst::HostMessage> m = owned(new Vst::HostMessage);
      m->setMessageID("stats");
      if (cp && cp->notify(m) == kResultOk)
         if (IAttributeList* attr = m->getAttributes())
         {
            attr->getInt("xruns", st.xruns);
            attr->getInt("enginePid", st.pid);
            attr->getInt("licensed", st.licensed);
         }
      return st;
   }

   double ThreadCpuNs()
   {
      timespec ts {};
      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
      return double(ts.tv_sec) * 1e9 + double(ts.tv_nsec);
   }

   // An Instrument and an FX in one host: two bundles, each with its own copy of the IPC code. Both
   // must get an engine (2.7: their shared-memory names used to collide, so the FX never started).
   void CheckMixedInstances(Plugin& instP, Plugin& fxP)
   {
      Instance a, b, c;
      const bool ok = a.Open(instP, false, kRealtime) && b.Open(fxP, true, kRealtime) && c.Open(instP, false, kRealtime);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      const SoakStats sa = ok ? QueryStats(a) : SoakStats {}, sb = ok ? QueryStats(b) : SoakStats {}, sc = ok ? QueryStats(c) : SoakStats {};
      char d[128];
      std::snprintf(d, sizeof d, "engine pids %lld, %lld, %lld", (long long)sa.pid, (long long)sb.pid, (long long)sc.pid);
      Check(ok && sa.pid > 0 && sb.pid > 0 && sc.pid > 0 && sa.pid != sb.pid && sb.pid != sc.pid,
            "Instrument + FX + Instrument active in one host: each gets its own engine", d);
      for (Instance* x : { &a, &b, &c })
         if (x->comp)
            x->Close();
   }

   int RunSoak(Plugin& instP, Plugin& fxP, const std::string& r1, const std::string& r2, double minutes, int nInst, int nFx,
               int idleSec, int reportSec)
   {
      constexpr int32 kSoakBlock = 256;
      const int n = nInst + nFx;
      std::vector<Instance> ins(static_cast<size_t>(n));
      bool opened = true;
      for (int i = 0; i < n && opened; i++)
      {
         const bool fx = i >= nInst;
         setenv("INFINITE_PLUGIN_PATCH", (fx ? r2 : r1).c_str(), 1);
         opened = ins[size_t(i)].Open(fx ? fxP : instP, fx, kRealtime, true, kSoakBlock);
         unsetenv("INFINITE_PLUGIN_PATCH");
      }
      if (!opened)
      {
         Check(false, "soak: open instances");
         return 1;
      }

      std::atomic<bool> stop { false }, playing { false };
      std::atomic<int64_t> overruns { 0 }, maxCallNs { 0 }, frames { 0 };
      std::vector<std::atomic<double>> cpuNs(static_cast<size_t>(n));
      for (auto& c : cpuNs)
         c.store(0.0);

      std::atomic<int64_t> maxCallInst { -1 }, maxCallCpuNs { 0 };
      std::atomic<bool> realtimeThread { false };
      std::thread host([&] {
         // A DAW's audio callback runs time-constrained; so does this one (the engine's does too).
         realtimeThread.store(Ipc::SetThreadRealtime(kRate, kSoakBlock));
         std::vector<float> il(kSoakBlock), ir(kSoakBlock), ol(kSoakBlock), orr(kSoakBlock);
         float* inPtr[2] = { il.data(), ir.data() };
         float* outPtr[2] = { ol.data(), orr.data() };
         AudioBusBuffers ib, ob;
         ib.numChannels = ob.numChannels = 2;
         ib.channelBuffers32 = inPtr;
         ob.channelBuffers32 = outPtr;
         EventList events(64);
         ParameterChanges params(4);
         ProcessContext ctx {};
         ProcessData d {};
         d.processMode = kRealtime;
         d.symbolicSampleSize = kSample32;
         d.numOutputs = 1;
         d.outputs = &ob;
         d.inputEvents = &events;
         d.inputParameterChanges = &params;
         d.processContext = &ctx;
         d.numSamples = kSoakBlock;
         const int64_t period = int64_t(1e9 * kSoakBlock / kRate);
         constexpr int64_t kNoteEvery = 24000, kNoteLen = 19200; // a note every 0.5 s, 0.4 s long
         int64_t t = 0, playT = 0;
         auto next = std::chrono::steady_clock::now();
         while (!stop.load())
         {
            const bool play = playing.load();
            for (int32 i = 0; i < kSoakBlock; i++)
               il[size_t(i)] = ir[size_t(i)] = play ? Signal(playT + i) : 0.0f;
            events.clear();
            if (play)
               for (int64_t at = (playT + kNoteEvery - 1) / kNoteEvery * kNoteEvery - kNoteEvery; at < playT + kSoakBlock; at += kNoteEvery)
                  for (int on = 0; on < 2; on++)
                  {
                     const int64_t when = on ? at : at + kNoteLen;
                     if (when < playT || when >= playT + kSoakBlock || at < 0)
                        continue;
                     Event e {};
                     e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
                     e.sampleOffset = int32(when - playT);
                     const int16 pitch = int16(48 + (at / kNoteEvery * 7) % 24);
                     if (on)
                     {
                        e.noteOn.pitch = pitch;
                        e.noteOn.velocity = 0.7f;
                        e.noteOn.noteId = -1;
                     }
                     else
                     {
                        e.noteOff.pitch = pitch;
                        e.noteOff.noteId = -1;
                     }
                     events.addEvent(e);
                  }
            ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid | (play ? ProcessContext::kPlaying : 0);
            ctx.sampleRate = kRate;
            ctx.tempo = 120.0;
            ctx.projectTimeSamples = playT;
            ctx.projectTimeMusic = double(playT) / kRate * 2.0;
            const auto cycle0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; i++)
            {
               Instance& a = ins[size_t(i)];
               d.numInputs = a.fx ? 1 : 0;
               d.inputs = a.fx ? &ib : nullptr;
               params.clearQueue();
               const double c0 = ThreadCpuNs();
               const auto w0 = std::chrono::steady_clock::now();
               a.proc->process(d);
               const int64_t callNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - w0).count();
               const double callCpu = ThreadCpuNs() - c0;
               cpuNs[size_t(i)].store(cpuNs[size_t(i)].load() + callCpu);
               if (callNs > maxCallNs.load())
               {
                  maxCallNs.store(callNs);
                  maxCallInst.store(i);
                  maxCallCpuNs.store(int64_t(callCpu));
               }
            }
            if (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - cycle0).count() > period)
               overruns.fetch_add(1);
            t += kSoakBlock;
            if (play)
               playT += kSoakBlock;
            frames.store(t);
            next += std::chrono::nanoseconds(period);
            std::this_thread::sleep_until(next);
         }
      });

      auto report = [&](const char* phase, double elapsed, const std::vector<int64>& base, const std::vector<double>& cpu0, double wall,
                        std::vector<SoakStats>& now) {
         now.clear();
         std::string x, p, c;
         for (int i = 0; i < n; i++)
         {
            now.push_back(QueryStats(ins[size_t(i)]));
            const double pct = wall > 0 ? 100.0 * (cpuNs[size_t(i)].load() - cpu0[size_t(i)]) / (wall * 1e9) : 0.0;
            char b[64];
            x += (i ? "," : "") + std::to_string((long long)(now.back().xruns - base[size_t(i)]));
            p += (i ? "," : "") + std::to_string((long long)now.back().pid);
            std::snprintf(b, sizeof b, "%s%.3f", i ? "," : "", pct);
            c += b;
         }
         std::printf("{\"soak\":1,\"phase\":\"%s\",\"t\":%.1f,\"instruments\":%d,\"fx\":%d,\"xruns\":[%s],\"pids\":[%s],\"cpu_pct\":[%s],"
                     "\"host_overruns\":%lld,\"max_call_us\":%.1f,\"max_call_inst\":%lld,\"max_call_cpu_us\":%.1f,\"rt\":%d}\n",
                     phase, elapsed, nInst, nFx, x.c_str(), p.c_str(), c.c_str(), (long long)overruns.load(), maxCallNs.load() / 1000.0,
                     (long long)maxCallInst.load(), maxCallCpuNs.load() / 1000.0, realtimeThread.load() ? 1 : 0);
         std::fflush(stdout);
      };

      // Idle: engines start and load, the host plays silence with no notes.
      const auto t0 = std::chrono::steady_clock::now();
      auto since = [&](std::chrono::steady_clock::time_point a) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count(); };
      while (since(t0) < idleSec)
         ViewCheck::Pump(200);
      std::vector<SoakStats> now;
      std::vector<int64> zero(size_t(n), 0), base(size_t(n), 0);
      std::vector<double> cpu0(size_t(n), 0.0);
      report("idle", since(t0), zero, cpu0, since(t0), now);
      std::vector<int64> pids0;
      bool licensed = true;
      for (int i = 0; i < n; i++)
      {
         base[size_t(i)] = now[size_t(i)].xruns;
         pids0.push_back(now[size_t(i)].pid);
         licensed = licensed && now[size_t(i)].licensed && now[size_t(i)].pid > 0;
         cpu0[size_t(i)] = cpuNs[size_t(i)].load();
      }
      overruns.store(0);
      maxCallNs.store(0);

      // Measured window: notes and input for `minutes`.
      playing.store(true);
      const auto p0 = std::chrono::steady_clock::now();
      double nextReport = reportSec;
      while (since(p0) < minutes * 60.0)
      {
         ViewCheck::Pump(200);
         if (since(p0) >= nextReport)
         {
            report("play", since(p0), base, cpu0, since(p0), now);
            nextReport += reportSec;
         }
      }
      report("end", since(p0), base, cpu0, since(p0), now);
      const double wall = since(p0);
      stop.store(true);
      host.join();

      int64 xruns = 0;
      bool samePids = true;
      double worstCpu = 0.0;
      for (int i = 0; i < n; i++)
      {
         xruns += now[size_t(i)].xruns - base[size_t(i)];
         samePids = samePids && now[size_t(i)].pid == pids0[size_t(i)];
         worstCpu = std::max(worstCpu, 100.0 * (cpuNs[size_t(i)].load() - cpu0[size_t(i)]) / (wall * 1e9));
      }
      for (auto& a : ins)
         a.Close();

      char dsc[200];
      std::snprintf(dsc, sizeof dsc, "%d instances (%d R1 + %d R2), 48 kHz / 256, %.1f min", n, nInst, nFx, wall / 60.0);
      Check(licensed, "soak: every instance licensed with an engine", dsc);
      std::snprintf(dsc, sizeof dsc, "%lld missed blocks; host thread overran its block %lld times, longest call %.0f us", (long long)xruns,
                    (long long)overruns.load(), maxCallNs.load() / 1000.0);
      Check(xruns == 0, "M-7: no engine deadline misses", dsc);
      Check(samePids, "soak: no engine restarted");
      std::snprintf(dsc, sizeof dsc, "worst %.2f %% of one core per instance", worstCpu);
      Check(worstCpu <= 3.0, "M-8: shell + IPC <= 3 % of one core per instance", dsc);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      Check(EnginePids().empty(), "no engine left");
      std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
      return gFails == 0 ? 0 : 1;
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
         std::snprintf(d, sizeof d, "%s, level %.1f dB re input", bi.header.find("\"slotsSet\":[0]") != std::string::npos ? "slotsSet 1" : "no slotsSet", rel(rr, 0.5, 4.0));
         Check(bi.header.find("\"slotsSet\":[0]") != std::string::npos && rel(rr, 0.5, 4.0) < -50.0,
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
         // Only bound slots show in the host; the rest are hidden "Unused NN". The name is saved.
         ParameterInfo p1 {};
         if (ec)
            ec->getParameterInfo(1, p1);
         const bool shown = (pi.flags & ParameterInfo::kCanAutomate) && !(pi.flags & ParameterInfo::kIsHidden);
         const bool hidden = (p1.flags & ParameterInfo::kIsHidden) && !(p1.flags & ParameterInfo::kCanAutomate) &&
                             std::u16string(reinterpret_cast<const char16_t*>(p1.title)) == u"Unused 02";
         const std::string hdr = ok ? Inspect(GetBlob(a)).header : std::string();
         const bool saved = hdr.find("\"names\":[[0,\"Level\"]]") != std::string::npos;
         std::snprintf(d, sizeof d, "slot 1 %s, slot 2 %s, %d params, names %s", shown ? "shown" : "not shown",
                       hidden ? "hidden" : "not hidden", ec ? int(ec->getParameterCount()) : 0, saved ? "saved" : "not saved");
         Check(ok && shown && hidden && saved, "slots: only bound slots show in the host, by name, saved with the project", d);
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

      // Right-click > Host automation on a plain knob (no Macro): automation drives it, and the
      // host sees the node's name.
      {
         const std::string s2 = std::string(INFINITE_SOURCE_ROOT) + "/tests/plugin/patches/S2-hostparam.inf";
         Case hp { "S2", &fx, true, s2, Script(), len };
         hp.script.input = Signal;
         hp.script.automation = SlotRamp;
         const auto rh = RenderCase(hp, kOffline, kBlock);
         const double low = rel(rh, 0.3, 0.9), high = rel(rh, 3.3, 4.0);
         std::snprintf(d, sizeof d, "slot 0: %.1f dB, slot 1: %.1f dB re input", low, high);
         Check(!rh.empty() && low < -50.0 && high > 6.0, "slots: host automation on a knob drives it", d);

         setenv("INFINITE_PLUGIN_PATCH", s2.c_str(), 1);
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
         if (ok)
            for (int i = 0; i < 300; i++)
            {
               Pump();
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
         ParameterInfo pi {};
         if (ec)
            ec->getParameterInfo(0, pi);
         const std::u16string t(reinterpret_cast<const char16_t*>(pi.title));
         const bool named = t == u"Gain";
         std::string t8;
         for (char16_t c : t)
            t8 += c < 128 ? char(c) : '?';
         std::snprintf(d, sizeof d, "slot 1 title \"%s\", %s", t8.c_str(),
                       (pi.flags & ParameterInfo::kCanAutomate) ? "automatable" : "not automatable");
         Check(ok && named && (pi.flags & ParameterInfo::kCanAutomate), "slots: a host-automated knob shows in the host by its node's name", d);
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
   // ---- failure drill (step 2.5; tools/plugin/crash_drill.py --self runs only these) --------

   // Gone or a zombie waiting to be reaped: either way it runs no more.
   bool ProcessGone(int pid)
   {
      char cmd[64];
      std::snprintf(cmd, sizeof cmd, "ps -o stat= -p %d", pid);
      std::string st;
      if (FILE* f = popen(cmd, "r"))
      {
         char buf[32] = {};
         if (std::fgets(buf, sizeof buf, f))
            st = buf;
         pclose(f);
      }
      return st.empty() || st[0] == 'Z';
   }

   void SendRestart(Instance& a)
   {
      FUnknownPtr<IConnectionPoint> cp(a.comp);
      IPtr<Vst::HostMessage> m = owned(new Vst::HostMessage);
      m->setMessageID("restart");
      if (cp)
         cp->notify(m);
   }

   int NewestEngine(const std::vector<int>& before)
   {
      for (int pid : EnginePids())
         if (std::find(before.begin(), before.end(), pid) == before.end())
            return pid;
      return 0;
   }

   void RunDrillChecks(Plugin& inst, const std::string& r1)
   {
      char d[256];
      const int64_t sec = int64_t(kRate);

      // Crash restart: the engine starts on R1 with no setState (a session the host never saved),
      // is killed, and Restart opens what was on screen, not the starting patch.
      const auto before = EnginePids();
      setenv("INFINITE_PLUGIN_PATCH", r1.c_str(), 1);
      Instance a;
      const bool ok = a.Open(inst, false, kRealtime);
      unsetenv("INFINITE_PLUGIN_PATCH");
      if (!ok)
      {
         Check(false, "drill: instrument opens");
         return;
      }
      // Realtime start takes a few seconds (spawn, GL, first frames); getState carries a patch once
      // the engine is ready, and it pushes the same text right after.
      std::vector<uint8_t> blob1;
      for (int i = 0; i < 20 && Inspect(blob1).patchBytes == 0; i++)
      {
         Render(a, sec, true);
         blob1 = GetBlob(a);
      }
      Render(a, sec, true);
      int pid = NewestEngine(before);
      if (pid > 0)
         kill(pid, SIGKILL);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      SendRestart(a);
      Script probe;
      probe.noteOnAt = 5 * sec;
      probe.noteOffAt = 6 * sec;
      const auto up = Render(a, 7 * sec, true, probe);
      const std::vector<uint8_t> blob2 = GetBlob(a);
      const BlobInfo i1 = Inspect(blob1), i2 = Inspect(blob2);
      const double upRms = Rms(up, 5 * sec + kBlock + 1000, 6 * sec);
      std::snprintf(d, sizeof d, "patch %u bytes before the kill, %u after the restart, note rms %.3g", i1.patchBytes, i2.patchBytes, upRms);
      Check(pid > 0 && i1.patchBytes > 100 && blob2 == blob1 && upRms > 1e-3, "drill: after a crash, Restart opens the patch the engine last pushed", d);

      // Hang: a stopped engine (SIGSTOP stands in for a deadlock) is killed by the watchdog; the host
      // keeps running and the plugin plays silence.
      pid = NewestEngine(before);
      if (pid > 0)
         kill(pid, SIGSTOP);
      const auto t0 = std::chrono::steady_clock::now();
      double goneAfter = -1.0;
      Script watch;
      watch.perBlock = [&] {
         if (goneAfter < 0.0 && pid > 0 && ProcessGone(pid))
            goneAfter = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      };
      const auto hung = Render(a, 7 * sec, true, watch);
      double tail = 0.0;
      for (size_t i = hung.size() - size_t(sec); i < hung.size(); i++)
         tail = std::max(tail, double(std::fabs(hung[i])));
      std::snprintf(d, sizeof d, "engine gone after %.1f s, peak in the last second %.3g", goneAfter, tail);
      Check(pid > 0 && goneAfter > 2.0 && goneAfter < 6.0 && tail == 0.0, "drill: a hung engine is stopped in 3-6 s, the host plays silence", d);
      if (pid > 0 && goneAfter < 0.0)
         kill(pid, SIGKILL); // never leave a stopped engine behind

      // Restart after the hang: a new engine with the same patch that plays.
      SendRestart(a);
      Script note;
      note.noteOnAt = 5 * sec;
      note.noteOffAt = 6 * sec;
      const auto back = Render(a, 7 * sec, true, note);
      const std::vector<uint8_t> blob3 = GetBlob(a);
      const double rms = Rms(back, 5 * sec + kBlock + 1000, 6 * sec);
      std::snprintf(d, sizeof d, "note rms %.3g, patch %u bytes", rms, Inspect(blob3).patchBytes);
      Check(blob3 == blob1 && rms > 1e-3, "drill: Restart after a hang brings the engine back with its patch", d);
      a.Close();

      // Suspend: a note held across setProcessing(false/true) is released, like a host switching the
      // plugin off and on with a key down. The control run holds the same note without the suspend.
      auto heldTail = [&](bool suspend) {
         Instance s;
         if (!s.Open(inst, false, kRealtime))
            return -1.0;
         Script on;
         on.noteOnAt = sec;
         Render(s, 2 * sec, true, on);
         if (suspend)
         {
            s.proc->setProcessing(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            s.proc->setProcessing(true);
         }
         const auto r = Render(s, 3 * sec, true);
         s.Close();
         return Rms(r, 2 * sec, 3 * sec);
      };
      const double held = heldTail(false), released = heldTail(true);
      std::snprintf(d, sizeof d, "rms 2 s later: held %.3g, after suspend %.3g", held, released);
      Check(held > 1e-3 && released >= 0.0 && released < held * 0.05, "drill: a host suspend releases held notes", d);

      // Picture/canvas choice: saved in the header and restored by setState.
      {
         Instance p;
         p.Open(inst, false, kOffline, false);
         std::vector<uint8_t> b = GetBlob(p);
         const std::string from = "\"picture\":false", to = "\"picture\":true ";
         auto it = std::search(b.begin(), b.end(), from.begin(), from.end());
         const bool had = it != b.end();
         if (had)
            std::copy(to.begin(), to.end(), it);
         SetBlob(p, b);
         const BlobInfo back = Inspect(GetBlob(p));
         p.Close(false);
         Check(had && back.header.find("\"picture\":true") != std::string::npos, "drill: the picture/canvas choice round-trips through the state");
      }
   }

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
         // A locked screen freezes the window server's on-screen list, so only frames and label count then.
         const bool locked = ViewCheck::ScreenLocked();
         if (locked)
            std::strncat(d, " (screen locked: window list not checked)", sizeof d - std::strlen(d) - 1);
         Check(back && (hidden || locked) && ViewCheck::Label(child).empty(),
               "view: Bring back hides the engine's window and frames return", d);
      }

      // Close, wait, reopen: no readback while closed, frames again after.
      view->removed();
      view = nullptr;
      ViewCheck::Pump(2000);
      IPtr<IPlugView> again = owned(ec->createView(ViewType::kEditor));
      bool reopened = false;
      double warmS = -1.0;
      if (again)
      {
         again->onSize(&r2);
         const auto o0 = std::chrono::steady_clock::now();
         again->attached(parent, kPlatformTypeNSView);
         child = ViewCheck::Child(parent);
         reopened = child && waitSize(int(1280 * scale), int(800 * scale), 10000);
         warmS = std::chrono::duration<double>(std::chrono::steady_clock::now() - o0).count();
         ViewCheck::Pump(500);
         again->removed();
         again = nullptr;
      }
      ViewCheck::Pump(200);
      const long long last1 = ReadStatLine(viewStats, "view", "lastSeq", 0);
      const long long first2 = ReadStatLine(viewStats, "view", "firstSeq", 1);
      std::snprintf(d, sizeof d, "last frame shown %lld, first after 2 s closed %lld", last1, first2);
      Check(reopened && last1 > 0 && first2 > last1 && first2 - last1 <= 6, "view: closed, the engine reads nothing back; reopened, frames return", d);
      // C1 (README 8.8): a DAW user opens and closes the editor all session. Ten cycles, frames
      // each time, the engine's physical footprint up by less than 20 MB.
      {
         const auto pids = EnginePids();
         const int pid = pids.empty() ? 0 : pids[0];
         auto footprintMb = [](int p) -> double {
            rusage_info_v2 ri {};
            if (p <= 0 || proc_pid_rusage(p, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&ri)) != 0)
               return -1.0;
            return double(ri.ri_phys_footprint) / (1024.0 * 1024.0);
         };
         // One warm cycle first: the first reopen after a long close regrows the shrunk window.
         double fp0 = -1.0;
         int good = 0;
         for (int i = 0; i <= 10; i++)
         {
            IPtr<IPlugView> v = owned(ec->createView(ViewType::kEditor));
            if (!v)
               break;
            v->onSize(&r2);
            v->attached(parent, kPlatformTypeNSView);
            child = ViewCheck::Child(parent);
            const bool ok = child && waitSize(int(1280 * scale), int(800 * scale), 10000);
            ViewCheck::Pump(300);
            v->removed();
            v = nullptr;
            ViewCheck::Pump(300);
            if (i == 0)
               fp0 = footprintMb(pid);
            else if (ok)
               good++;
         }
         ViewCheck::Pump(1500);
         const double fp1 = footprintMb(pid);
         std::snprintf(d, sizeof d, "%d / 10 opens showed frames, engine footprint %.0f -> %.0f MB (%+.1f)", good, fp0, fp1, fp1 - fp0);
         Check(good == 10 && fp0 > 0 && fp1 - fp0 < 20.0, "C1: editor open/close x10, frames each time, footprint up < 20 MB", d);
      }
      // M-10 open time. Cold: no engine running, so spawn + GL + patch load + first frame (the OS
      // file cache is warm; a first launch after a reboot is slower). Warm: engine running, view reopened.
      std::snprintf(d, sizeof d, "cold %.2f s (<= 4), warm %.2f s (<= 1.5)", firstS, warmS);
      Check(first && reopened && firstS <= 4.0 && warmS <= 1.5, "M-10 editor open: cold <= 4 s, warm <= 1.5 s", d);

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

namespace
{
   // An editor on an instance with a controller, as a DAW opens one (offscreen window).
   struct Editor
   {
      Instance a;
      IPtr<IEditController> ec;
      FUnknownPtr<IConnectionPoint> cpComp, cpCtrl;
      IPtr<IPlugView> view;
      void* parent = nullptr;
      void* child = nullptr;
      bool opened = false;

      bool Open(Plugin& p, bool fx)
      {
         opened = a.Open(p, fx, kRealtime);
         TUID cid;
         if (opened && a.comp->getControllerClassId(cid) == kResultOk)
            ec = p.mod->getFactory().createInstance<IEditController>(VST3::UID::fromTUID(cid));
         if (!ec || ec->initialize(&gHost) != kResultOk)
            return false;
         cpComp = FUnknownPtr<IConnectionPoint>(a.comp);
         cpCtrl = FUnknownPtr<IConnectionPoint>(ec);
         if (!cpComp || !cpCtrl)
            return false;
         cpComp->connect(cpCtrl);
         cpCtrl->connect(cpComp);
         view = owned(ec->createView(ViewType::kEditor));
         if (!view)
            return false;
         parent = ViewCheck::NewParent(1280, 800, nullptr);
         ViewRect r { 0, 0, 1280, 800 };
         view->onSize(&r);
         view->attached(parent, kPlatformTypeNSView);
         child = ViewCheck::Child(parent);
         return child != nullptr;
      }

      // Pumps until pred() or the timeout; true when it held.
      bool WaitFor(const std::function<bool()>& pred, double seconds)
      {
         const auto t0 = std::chrono::steady_clock::now();
         while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds)
         {
            ViewCheck::Pump(20);
            if (pred())
               return true;
         }
         return pred();
      }
      std::string Label() { return child ? ViewCheck::Label(child) : std::string(); }

      void Close()
      {
         if (view)
         {
            view->removed();
            view = nullptr;
         }
         if (cpComp && cpCtrl)
         {
            cpComp->disconnect(cpCtrl);
            cpCtrl->disconnect(cpComp);
         }
         if (ec)
            ec->terminate();
         ec = nullptr;
         if (opened)
            a.Close();
         opened = false;
         if (parent)
            ViewCheck::FreeParent(parent);
         parent = child = nullptr;
      }
   };

   pid_t StartHttpServer(const std::string& dir, int port)
   {
      const pid_t pid = fork();
      if (pid == 0)
      {
         const std::string p = std::to_string(port);
         freopen("/dev/null", "w", stderr);
         freopen("/dev/null", "w", stdout);
         execlp("python3", "python3", "-m", "http.server", p.c_str(), "--bind", "127.0.0.1", "--directory", dir.c_str(), (char*)nullptr);
         _exit(127);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      return pid;
   }

   void RunLicenseChecks(Plugin& inst, Plugin& fx)
   {
      char d[256];
      const std::string valid = gLicense.Key("INF-CHEC-K234", 1);

      // ---- License.cpp on its own ----
      {
         const std::string wrapped = "Your key:\n  \"" + valid.substr(0, 40) + "\n  " + valid.substr(40) + "\"\n\nThanks";
         Check(License::Extract(wrapped) == valid && License::Extract("no key here").empty(),
               "licence: a key wrapped across lines, in quotes, is found; text without one gives none");
         std::string tampered = valid;
         const size_t at = tampered.size() - 20; // inside the signature
         tampered[at] = tampered[at] == 'A' ? 'B' : 'A';
         const License::Info ok = License::Check(valid, {}, 1);
         const License::Info bad = License::Check(tampered, {}, 1);
         const License::Info old = License::Check(gLicense.Key("INF-CHEC-K234", 0), {}, 1);
         const License::Info newer = License::Check(gLicense.Key("INF-CHEC-K234", 2), {}, 1);
         const License::Info v2 = License::Check(gLicense.Key("INF-CHEC-K234", 1, 2), {}, 1);
         const License::Info refunded = License::Check(valid, { "INF-OTHR-2345", "INF-CHEC-K234" }, 1);
         std::snprintf(d, sizeof d, "valid \"%s\", tampered \"%s\", major 0 \"%s\", refunded \"%s\"", License::Message(ok).c_str(),
                       License::Message(bad).c_str(), License::Message(old).c_str(), License::Message(refunded).c_str());
         Check(ok.status == License::Status::Valid && ok.id == "INF-CHEC-K234" && newer.status == License::Status::Valid &&
                   bad.status == License::Status::BadSignature && old.status == License::Status::WrongMajor &&
                   License::Message(old) == "Your key covers 0.x" && v2.status == License::Status::WrongVersion &&
                   refunded.status == License::Status::Revoked && License::Message(refunded) == "This key was refunded",
               "licence: signature, major, payload version and refund list checked", d);
      }

      // A licensed FX instance's state, to restore into an unlicensed one.
      std::vector<uint8_t> blob;
      {
         Instance a;
         if (a.Open(fx, true, kOffline))
         {
            Render(a, int64_t(kRate), false);
            blob = GetBlob(a);
            a.Close();
         }
      }

      gLicense.RemoveKey();
      const size_t baseline = EnginePids().size();

      // ---- no key: Instrument silent, FX dry by exactly L, no engine, state untouched ----
      {
         Instance a;
         const bool ok = a.Open(inst, false, kOffline);
         Script s;
         s.noteOnAt = 4800;
         s.noteOffAt = 40000;
         const auto r = ok ? Render(a, 48000, false, s) : std::vector<float>();
         const size_t during = EnginePids().size();
         a.Close();
         double peak = 0.0;
         for (float x : r)
            peak = std::max(peak, double(std::fabs(x)));
         std::snprintf(d, sizeof d, "peak %.3g, engines %zu -> %zu", peak, baseline, during);
         Check(ok && !r.empty() && peak == 0.0 && during == baseline, "licence: no key, the Instrument is silent and spawns no engine", d);
      }
      {
         Instance a;
         const bool ok = a.Open(fx, true, kOffline);
         const auto r = ok ? Render(a, int64_t(kRate) * 2, false) : std::vector<float>();
         const size_t during = EnginePids().size();
         std::vector<uint8_t> back;
         bool stateOk = false;
         if (ok && !blob.empty())
         {
            stateOk = SetBlob(a, blob) == kResultOk;
            back = GetBlob(a);
         }
         a.Close();
         double e = 0.0;
         for (int64_t t = 0; t < int64_t(r.size()); t++)
            e = std::max(e, double(std::fabs(r[size_t(t)] - (t >= kBlock ? Sine(t - kBlock) : 0.0f))));
         std::snprintf(d, sizeof d, "max error vs input delayed %d: %.2e, engines %zu -> %zu", kBlock, e, baseline, during);
         Check(ok && !r.empty() && e == 0.0 && during == baseline, "licence: no key, the FX passes its input dry, delayed by exactly L", d);
         Check(stateOk && !blob.empty() && back == blob, "licence: no key, getState returns exactly what setState got");
      }

      // ---- the sheet: message, Paste key, bad and old keys refused, a valid key starts the engine ----
      {
         Editor e;
         const bool ok = e.Open(inst, false);
         const bool sheet = ok && e.WaitFor([&] { return e.Label() == "Enter your key"; }, 5.0);
         const std::string button = e.child ? ViewCheck::Button(e.child) : std::string();
         const size_t during = EnginePids().size();
         std::snprintf(d, sizeof d, "label \"%s\", button \"%s\", engines %zu -> %zu", e.Label().c_str(), button.c_str(), baseline, during);
         Check(sheet && button == "Paste key" && during == baseline, "licence: the editor shows the licence sheet, no engine", d);

         const bool typed = e.child && ViewCheck::TypeKey(e.child, "hello");
         const bool refused = typed && e.WaitFor([&] { return e.Label() == "Not a valid key"; }, 3.0);
         ViewCheck::Pump(1500); // the once-a-second recheck must not clear the message
         const bool kept = e.Label() == "Not a valid key";
         ViewCheck::TypeKey(e.child, gLicense.Key("INF-CHEC-K234", 0));
         const bool old = e.WaitFor([&] { return e.Label() == "Your key covers 0.x"; }, 3.0);
         std::snprintf(d, sizeof d, "label \"%s\"", e.Label().c_str());
         Check(refused && kept && old && EnginePids().size() == baseline, "licence: the sheet refuses a bad key and an older major's", d);

         ViewCheck::TypeKey(e.child, "  " + valid + "\n");
         const bool started = e.WaitFor([&] { return EnginePids().size() > baseline; }, 10.0);
         const bool left = e.WaitFor([&] { return e.Label() != "Your key covers 0.x"; }, 5.0);
         std::snprintf(d, sizeof d, "engine %s, label \"%s\", stored %s", started ? "started" : "none", e.Label().c_str(),
                       gLicense.StoredKey() == valid ? "yes" : "no");
         Check(started && left && gLicense.StoredKey() == valid, "licence: a valid key from the sheet is stored and starts the engine", d);
         e.Close();
         e.WaitFor([&] { return EnginePids().size() == baseline; }, 3.0);
      }

      // ---- a key stored by another instance starts this one (recheck while the sheet shows) ----
      {
         gLicense.RemoveKey();
         Editor e;
         const bool ok = e.Open(fx, true);
         const bool sheet = ok && e.WaitFor([&] { return e.Label() == "Enter your key"; }, 5.0);
         gLicense.WriteKey(valid);
         const auto t0 = std::chrono::steady_clock::now();
         const bool started = sheet && e.WaitFor([&] { return EnginePids().size() > baseline; }, 5.0);
         const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
         std::snprintf(d, sizeof d, "engine after %.1f s", s);
         Check(started && s < 3.0, "licence: a key entered in another instance starts this one", d);
         e.Close();
         e.WaitFor([&] { return EnginePids().size() == baseline; }, 3.0);
      }

      // ---- refunded: the engine's weekly check finds the id, the engine stops, the FX goes dry ----
      {
         const std::string served = gLicense.dir + "/served";
         std::filesystem::create_directories(served);
         LicenseFixture::Write(served + "/revoked.json", "{\"revoked\":[\"INF-CHEC-K234\"]}");
         const int port = 18000 + int(getpid() % 1000);
         const pid_t server = StartHttpServer(served, port);
         setenv("INFINITE_LICENSE_REVOKED_URL", ("http://127.0.0.1:" + std::to_string(port) + "/revoked.json").c_str(), 1);
         gLicense.WriteRevoked("", 0); // a week old: the engine fetches
         gLicense.WriteKey(valid);
         Instance a;
         const bool ok = a.Open(fx, true, kRealtime);
         const auto r = ok ? Render(a, int64_t(kRate) * 8, true) : std::vector<float>();
         const size_t after = EnginePids().size();
         a.Close();
         const License::Info cur = License::Current(1);
         const double e = MaxErrDelayed(r, int64_t(r.size()) - int64_t(kRate));
         std::snprintf(d, sizeof d, "engines after %zu (baseline %zu), stored key \"%s\", dry error over the last second %.2e", after,
                       baseline, License::Message(cur).c_str(), e);
         Check(ok && after == baseline && cur.status == License::Status::Revoked && e == 0.0,
               "licence: a refunded key stops the engine within the run; the FX plays dry", d);
         unsetenv("INFINITE_LICENSE_REVOKED_URL");
         if (server > 0)
         {
            kill(server, SIGTERM);
            waitpid(server, nullptr, 0);
         }
         gLicense.WriteRevoked("", int64_t(std::time(nullptr)));
      }
   }
}

int main(int argc, char** argv)
{
   // Every check runs licensed (a temp key, never the user's); RunLicenseChecks takes it away.
   gLicense.Setup();
   // For au_check: a test key, its public key and a fresh refund list into <dir> (an out-of-process
   // AU host gets no environment, so it reads them from the real per-user folder; remove after).
   if (argc >= 3 && std::string(argv[1]) == "--write-license")
   {
      const std::string dir = argv[2];
      std::filesystem::create_directories(dir);
      char hex[65];
      for (int i = 0; i < 32; i++)
         std::snprintf(hex + 2 * i, 3, "%02x", gLicense.pub[i]);
      LicenseFixture::Write(dir + "/license.inflic", gLicense.Key("INF-CHEC-K234", 1) + "\n");
      LicenseFixture::Write(dir + "/license-test-key.hex", std::string(hex) + "\n");
      LicenseFixture::Write(dir + "/license-revoked.json", "{\"revoked\":[],\"fetched\":" + std::to_string((long long)std::time(nullptr)) + "}");
      return 0;
   }
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
   if (argc >= 3 && std::string(argv[1]) == "--drill")
   {
      Plugin inst;
      std::string err;
      if (!inst.Load(argv[2], err))
      {
         std::fprintf(stderr, "load failed: %s\n", err.c_str());
         return 1;
      }
      RunDrillChecks(inst, std::string(INFINITE_SOURCE_ROOT) + "/tests/plugin/patches/R1-instrument.inf");
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      Check(EnginePids().empty(), "no engine left");
      std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
      return gFails == 0 ? 0 : 1;
   }
   // --soak <minutes> <Instrument.vst3> <FX.vst3> <R1.inf> <R2.inf> [instruments fx idleSec reportSec]
   if (argc >= 7 && std::string(argv[1]) == "--soak")
   {
      Plugin inst, fx;
      std::string err;
      if (!inst.Load(argv[3], err) || !fx.Load(argv[4], err))
      {
         std::fprintf(stderr, "load failed: %s\n", err.c_str());
         return 1;
      }
      auto arg = [&](int i, int def) { return argc > i ? std::atoi(argv[i]) : def; };
      return RunSoak(inst, fx, argv[5], argv[6], std::atof(argv[2]), arg(7, 2), arg(8, 2), arg(9, 20), arg(10, 10));
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
      std::fprintf(stderr, "usage: INFINITE_PLUGIN_ENGINE=<Infinite binary> %s [--metrics | --view | --drill | --soak <minutes>] <Instrument.vst3> <FX.vst3> [<R1.inf> <R2.inf>]\n", argv[0]);
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

   // C6 (README 8.8): the DAW changes the sample rate with the project open (44.1 -> 48 -> 96 kHz):
   // deactivate, setupProcessing at the new rate, activate. Note 60 must stay at 261.6 Hz.
   {
      Instance a;
      bool ok = a.Open(inst, false, kOffline, false);
      std::string d6;
      bool all = ok;
      for (double sr : { 44100.0, 48000.0, 96000.0 })
      {
         if (!ok)
            break;
         a.rate = sr;
         if (!a.Activate())
         {
            all = false;
            d6 += " activate failed at " + std::to_string(int(sr));
            break;
         }
         Script s;
         s.noteOnAt = int64_t(sr * 0.25);
         s.noteOffAt = int64_t(sr * 1.75);
         const auto r = Render(a, int64_t(sr * 2.0), false, s);
         a.proc->setProcessing(false);
         a.comp->setActive(false);
         // Pitch by autocorrelation over 0.5 s of the held note, lags for 100..1000 Hz.
         const int64_t from = int64_t(sr * 0.75), len = int64_t(sr * 0.5);
         // A sub-octave oscillator makes 2x the period score as high as 1x, so take the first
         // local peak within 90 % of the best rather than the best itself.
         int bestLag = 0;
         double best = 0.0;
         const int lo = int(sr / 1000.0), hi = int(sr / 100.0);
         std::vector<double> ac(size_t(hi + 2), 0.0);
         if (int64_t(r.size()) >= from + len + hi + 1)
            for (int lag = lo; lag <= hi + 1; lag++)
            {
               double acc = 0.0;
               for (int64_t i = from; i < from + len; i++)
                  acc += double(r[size_t(i)]) * double(r[size_t(i + lag)]);
               ac[size_t(lag)] = acc;
               best = std::max(best, acc);
            }
         for (int lag = lo + 1; lag <= hi && best > 0.0; lag++)
            if (ac[size_t(lag)] >= 0.9 * best && ac[size_t(lag)] >= ac[size_t(lag - 1)] && ac[size_t(lag)] >= ac[size_t(lag + 1)])
            {
               bestLag = lag;
               break;
            }
         const double hz = bestLag > 0 ? sr / double(bestLag) : 0.0;
         const bool good = std::fabs(hz - 261.63) / 261.63 < 0.012;
         all = all && good;
         char b[64];
         std::snprintf(b, sizeof b, " %.1f kHz: %.1f Hz;", sr / 1000.0, hz);
         d6 += b;
      }
      if (ok)
         a.comp->terminate();
      Check(ok && all, "C6: sample rate 44.1 -> 48 -> 96 kHz, re-prepared, note 60 stays 261.6 Hz", d6);
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
   RunDrillChecks(inst, std::string(INFINITE_SOURCE_ROOT) + "/tests/plugin/patches/R1-instrument.inf");
   CheckMixedInstances(inst, fx);
   RunLicenseChecks(inst, fx);

   std::this_thread::sleep_for(std::chrono::milliseconds(300));
   Check(EnginePids().size() == baseline, "no engine left after terminate");
   std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
   return gFails == 0 ? 0 : 1;
}
