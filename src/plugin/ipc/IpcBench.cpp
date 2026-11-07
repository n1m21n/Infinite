// ipc_bench: measures and checks the plugin shell <-> engine protocol (src/plugin/ipc/PluginIpc.h).
// Spawns itself as the engine, so nothing else is needed on disk.
//
//   ipc_bench                 B-5 / M-8 numbers, one JSON line per block size (paced at 48 kHz)
//   ipc_bench --check         protocol checks; exit 0 = all pass
//   ipc_bench --engine <shm> <parent-pid>   (internal) the stand-in engine
//
// The stand-in engine's "graph": out L = 0.5 * main L + 0.25 * sidechain L, out R = 0.5 * main R
// + 0.25 * sidechain R, so the host side can predict every sample.
#include "plugin/ipc/PluginIpc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
   #include <signal.h>
#endif

using namespace PluginIpc;

namespace
{
   enum MsgType : uint32_t
   {
      kEcho = 10,       // engine sends the payload back
      kStallMs = 11,    // engine's audio thread sleeps this long on its next block
      kReportBlocks = 12, // engine reports each block's contents (type kBlockReport)
      kBlockReport = 13,
   };

   struct BlockReport
   {
      uint64_t startFrame;
      uint32_t frames, flags, numEvents, numParams;
      Event firstEvent;
      ParamPoint firstParam;
      double tempo, ppq;
      uint32_t transportFlags;
   };

   // ---- engine ---------------------------------------------------------------------------------

   int RunEngine(const std::string& name, int64_t parent)
   {
      Ipc::ExitWhenParentDies(parent);
      EngineSide e;
      if (!e.Open(name)) return 3;
      std::atomic<int> stallMs{0};
      std::atomic<bool> report{false};
      std::thread msgs([&] {
         Message m;
         while (!e.QuitRequested())
         {
            if (!e.Messages().Receive(&m, 100)) continue;
            if (m.type == kEcho) e.Messages().Send(kEcho, m.id, m.data.data(), m.data.size(), 5000);
            else if (m.type == kStallMs && m.data.size() == 4) { int v; memcpy(&v, m.data.data(), 4); stallMs = v; }
            else if (m.type == kReportBlocks) report = !m.data.empty() && m.data[0] != 0;
         }
      });
      Ipc::SetThreadRealtime(e.SampleRate(), e.MaxBlock());
      e.MarkAttached();
      BlockView v;
      while (!e.QuitRequested())
      {
         if (!e.NextBlock(&v, 0)) continue;
         if (const int s = stallMs.exchange(0)) std::this_thread::sleep_for(std::chrono::milliseconds(s));
         for (uint32_t c = 0; c < kOutChannels; ++c)
            for (uint32_t i = 0; i < v.frames; ++i)
               v.output[c][i] = 0.5f * v.input[c][i] + 0.25f * v.input[2 + c][i];
         if (report)
         {
            // Test helper only: a real engine never sends from its audio thread.
            BlockReport r{};
            r.startFrame = v.in->startFrame;
            r.frames = v.frames;
            r.flags = v.in->flags;
            r.numEvents = v.in->numEvents;
            r.numParams = v.in->numParams;
            if (r.numEvents) r.firstEvent = v.in->events[0];
            if (r.numParams) r.firstParam = v.in->params[0];
            r.tempo = v.in->transport.tempo;
            r.ppq = v.in->transport.ppq;
            r.transportFlags = v.in->transport.flags;
            e.Messages().Send(kBlockReport, 0, &r, sizeof r, 1000);
         }
         e.Complete(v);
      }
      msgs.join();
      return 0;
   }

   // ---- host helpers ---------------------------------------------------------------------------

   struct Rig
   {
      ShellSide shell;
      Ipc::Child engine;

      bool Start(double rate, uint32_t maxBlock)
      {
         if (!shell.Create(rate, maxBlock)) return false;
         const std::string self = Ipc::HelperPathBesideModule("ipc_bench");
         if (!Ipc::Spawn(self, {"--engine", shell.Name(), std::to_string(Ipc::SelfPid())}, &engine)) return false;
         return shell.WaitAttached(10000, &engine);
      }

      void Stop()
      {
         shell.RequestQuit();
         if (!Ipc::ChildWait(&engine, 1000)) Ipc::ChildKill(&engine);
         shell.Close();
      }
   };

   float Main(uint32_t c, int64_t t) { return 0.5f * float(std::sin(0.0137 * double(t) + c)); }
   float Side(uint32_t c, int64_t t) { return 0.3f * float(std::sin(0.0291 * double(t) + 2 * c)); }

   // Expected engine output for frame t, given that the shell sends main (mono or stereo) + sidechain.
   float Expected(uint32_t c, int64_t t, bool mono, bool side)
   {
      if (t < 0) return 0.f;
      const float m = Main(mono ? 0 : c, t);
      return 0.5f * m + (side ? 0.25f * Side(c, t) : 0.f);
   }

   struct Renderer
   {
      uint32_t maxBlock;
      std::vector<float> in[kInChannels], out[kOutChannels];
      explicit Renderer(uint32_t mb) : maxBlock(mb)
      {
         for (auto& b : in) b.assign(mb, 0.f);
         for (auto& b : out) b.assign(mb, 0.f);
      }

      Result Block(ShellSide& s, int64_t t, uint32_t n, uint32_t flags, bool mono, bool side,
                   const Event* ev = nullptr, uint32_t nev = 0, const ParamPoint* pp = nullptr, uint32_t npp = 0,
                   const Transport* tr = nullptr)
      {
         for (uint32_t i = 0; i < n; ++i)
         {
            in[0][i] = Main(0, t + i);
            in[1][i] = Main(1, t + i);
            in[2][i] = Side(0, t + i);
            in[3][i] = Side(1, t + i);
         }
         const float* ins[kInChannels] = {in[0].data(), mono ? nullptr : in[1].data(), in[2].data(), in[3].data()};
         float* outs[kOutChannels] = {out[0].data(), out[1].data()};
         ProcessArgs a;
         a.in = ins;
         a.numIn = side ? 4 : (mono ? 1 : 2);
         a.out = outs;
         a.numOut = kOutChannels;
         a.frames = n;
         a.flags = flags | (side ? kSidechain : 0);
         if (tr) a.transport = *tr;
         a.events = ev;
         a.numEvents = nev;
         a.params = pp;
         a.numParams = npp;
         const uint64_t t0 = Ipc::MonotonicNs();
         const Result res = s.Process(a);
         callNs = Ipc::MonotonicNs() - t0;
         postedAt = t0;
         return res;
      }
      uint64_t callNs = 0, postedAt = 0;
   };

   int gFails = 0;
   void Check(bool ok, const char* what)
   {
      printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
      if (!ok) ++gFails;
   }

   uint32_t Rand(uint32_t& state) { state = state * 1664525u + 1013904223u; return state >> 8; }

   // Offline render of `total` frames with random block sizes; returns the left channel.
   std::vector<float> OfflineRun(uint32_t maxBlock, int64_t total, bool mono, bool side, double* maxErr)
   {
      Rig rig;
      std::vector<float> left;
      if (!rig.Start(48000, maxBlock)) { *maxErr = 1e9; return left; }
      Renderer r(maxBlock);
      uint32_t rng = 777;
      double err = 0;
      int64_t t = 0;
      const int64_t L = maxBlock;
      while (t < total)
      {
         uint32_t n = 1 + Rand(rng) % maxBlock;
         if (t + n > total) n = uint32_t(total - t);
         if (r.Block(rig.shell, t, n, kOffline, mono, side) != Result::Ok) err = 1e9;
         for (uint32_t i = 0; i < n; ++i)
         {
            left.push_back(r.out[0][i]);
            for (uint32_t c = 0; c < kOutChannels; ++c)
               err = std::max(err, double(std::fabs(r.out[c][i] - Expected(c, t + i - L, mono, side))));
         }
         t += n;
      }
      rig.Stop();
      *maxErr = err;
      return left;
   }

   int RunChecks()
   {
      // 1. layout
      const Layout l = ComputeLayout(8192);
      Check(l.ringFrames >= 4 * 8192 && l.total < 8u * 1024 * 1024, "layout: ring >= 4 max blocks, region < 8 MB at 8192");

      // 2. offline exactness, mono + sidechain, two runs identical (M-4 in miniature)
      double e1 = 0, e2 = 0, e3 = 0;
      const auto a = OfflineRun(512, 48000 * 2, true, true, &e1);
      const auto b = OfflineRun(512, 48000 * 2, true, true, &e2);
      OfflineRun(8192, 48000 * 2, false, false, &e3);
      Check(e1 == 0 && e2 == 0, "offline, random blocks <= 512, mono + sidechain: exact");
      Check(!a.empty() && a == b, "offline: two engine instances bit-identical");
      Check(e3 == 0, "offline, random blocks <= 8192, stereo: exact");

      // 3. events, params, transport reach the engine intact
      {
         Rig rig;
         Check(rig.Start(48000, 256), "start engine (256)");
         const uint8_t on = 1, off = 0;
         rig.shell.Messages().Send(kReportBlocks, 0, &on, 1, 1000);
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
         Renderer r(256);
         Event ev[600];
         for (uint32_t i = 0; i < 600; ++i) ev[i] = Event{i % 256, 0x90, uint8_t(60 + i % 12), 100, 0, NAN, int32_t(i)};
         ParamPoint pp[3] = {{5, 10, 0.25f}, {5, 200, 0.75f}, {31, 0, 1.f}};
         Transport tr;
         tr.tempo = 133.5;
         tr.ppq = 17.25;
         tr.flags = kPlaying | kTempoValid | kPpqValid;
         r.Block(rig.shell, 0, 256, kOffline, false, false, ev, 600, pp, 3, &tr);
         Message m;
         bool got = false;
         for (int i = 0; i < 20 && !got; ++i)
            got = rig.shell.Messages().Receive(&m, 100) && m.type == kBlockReport && m.data.size() == sizeof(BlockReport);
         BlockReport rep{};
         if (got) memcpy(&rep, m.data.data(), sizeof rep);
         Check(got && rep.frames == 256 && rep.numEvents == kMaxEvents && rep.firstEvent.data1 == 60 &&
                  rep.firstEvent.noteId == 0 && std::isnan(rep.firstEvent.value),
               "MIDI events arrive (600 sent, capped at 512, first intact)");
         Check(got && rep.numParams == 3 && rep.firstParam.slot == 5 && rep.firstParam.offset == 10 &&
                  rep.firstParam.value == 0.25f,
               "param points arrive");
         Check(got && rep.tempo == 133.5 && rep.ppq == 17.25 && rep.transportFlags == tr.flags &&
                  (rep.flags & kOffline),
               "transport and block flags arrive");

         // 4. message channel: 5 MB both ways while audio runs. Reports off first: the channel
         //    has one writer per direction, and reports come from the engine's audio thread.
         rig.shell.Messages().Send(kReportBlocks, 0, &off, 1, 1000);
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
         while (rig.shell.Messages().Receive(&m, 0)) {}
         std::vector<uint8_t> blob(5u * 1024 * 1024);
         uint32_t rng = 99;
         for (auto& x : blob) x = uint8_t(Rand(rng));
         std::atomic<bool> stop{false};
         std::thread audio([&] {
            Renderer rr(256);
            int64_t t = 256;
            while (!stop) { rr.Block(rig.shell, t, 256, 0, false, false); t += 256;
                            std::this_thread::sleep_for(std::chrono::microseconds(5333)); }
         });
         const uint64_t t0 = Ipc::MonotonicNs();
         const bool sent = rig.shell.Messages().Send(kEcho, 42, blob.data(), blob.size(), 5000);
         bool back = false;
         Message echo;
         for (int i = 0; i < 100 && !back; ++i)
            back = rig.shell.Messages().Receive(&echo, 100) && echo.type == kEcho;
         const double ms = double(Ipc::MonotonicNs() - t0) / 1e6;
         stop = true;
         audio.join();
         Check(sent && back && echo.id == 42 && echo.data == blob, "5 MB message round trip, intact, while audio runs");
         printf("  5 MB round trip %.0f ms\n", ms);
         rig.Stop();
      }

      // 5. stall + resync: realtime host, engine sleeps 400 ms; output is exact or silent, never stale
      {
         Rig rig;
         Check(rig.Start(48000, 256), "start engine (realtime)");
         Renderer r(256);
         const int64_t L = 256;
         int64_t t = 0;
         int late = 0, skipped = 0, staleSamples = 0, exactAfter = 0;
         auto next = std::chrono::steady_clock::now();
         const int blocks = 48000 * 2 / 256;
         for (int k = 0; k < blocks; ++k)
         {
            if (k == 40)
            {
               const int ms = 400;
               rig.shell.Messages().Send(kStallMs, 0, &ms, 4, 1000);
            }
            const Result res = r.Block(rig.shell, t, 256, 0, false, false);
            late += res == Result::Late;
            skipped += res == Result::Skipped;
            bool exact = true;
            for (uint32_t i = 0; i < 256; ++i)
            {
               const float got = r.out[0][i];
               const float want = Expected(0, t + i - L, false, false);
               if (got != want) exact = false;
               if (got != want && got != 0.f) ++staleSamples;
            }
            if (k >= blocks - 40) exactAfter += exact;
            t += 256;
            next += std::chrono::microseconds(5333);
            std::this_thread::sleep_until(next);
         }
         printf("  stall: %d late, %d skipped, %llu engine resyncs\n", late, skipped,
                (unsigned long long)rig.shell.Xruns());
         Check(late + skipped > 0 && skipped > 0, "engine stall: shell misses blocks and skips posting");
         Check(staleSamples == 0, "every output sample is exact or silence (no stale audio)");
         Check(exactAfter == 40, "output exact again after the stall");

         // 6. hang detection: engine sleeps 3.5 s, heartbeat age goes past 3 s
         const int ms = 3500;
         rig.shell.Messages().Send(kStallMs, 0, &ms, 4, 1000);
         std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the message thread arm it
         for (int k = 0; k < 3; ++k, t += 256) r.Block(rig.shell, t, 256, 0, false, false);
         uint64_t maxAge = 0;
         for (int i = 0; i < 40; ++i)
         {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            maxAge = std::max(maxAge, rig.shell.HeartbeatAgeMs());
         }
         Check(maxAge > 3000, "hang: heartbeat age passes 3 s");
         rig.Stop();
      }

      // 7. engine killed: realtime returns silence within the deadline; quit is prompt
      {
         Rig rig;
         Check(rig.Start(48000, 128), "start engine (kill drill)");
         Renderer r(128);
         int64_t t = 0;
         for (int k = 0; k < 20; ++k, t += 128) r.Block(rig.shell, t, 128, 0, false, false);
         Ipc::ChildKill(&rig.engine);
         uint64_t worst = 0;
         bool silent = true;
         for (int k = 0; k < 20; ++k, t += 128)
         {
            const uint64_t t0 = Ipc::MonotonicNs();
            r.Block(rig.shell, t, 128, 0, false, false);
            worst = std::max(worst, Ipc::MonotonicNs() - t0);
            if (k > 2) for (uint32_t i = 0; i < 128; ++i) silent &= r.out[0][i] == 0.f;
         }
         printf("  killed engine: worst call %.1f us\n", double(worst) / 1e3);
         Check(silent && worst < uint64_t(1e9 * 128 / 48000), "killed engine: silence, every call within one block period");
         rig.shell.Close();

         Rig q;
         q.Start(48000, 128);
         const uint64_t t0 = Ipc::MonotonicNs();
         q.shell.RequestQuit();
         const bool exited = Ipc::ChildWait(&q.engine, 1000);
         Check(exited, "RequestQuit: engine exits within 1 s");
         printf("  quit took %.1f ms\n", double(Ipc::MonotonicNs() - t0) / 1e6);
         q.Stop();
      }

      printf("%s\n", gFails ? "PLUGIN IPC CHECK: FAIL" : "PLUGIN IPC CHECK: PASS");
      return gFails ? 1 : 0;
   }

   // B-5 (engine round trip) and M-8 (what the host thread pays per Process call), paced at 48 kHz.
   int RunBench()
   {
      for (uint32_t block : {32u, 64u, 128u, 256u, 512u, 1024u, 2048u})
      {
         Rig rig;
         const uint64_t a0 = Ipc::MonotonicNs();
         if (!rig.Start(48000, block)) { printf("{\"block\": %u, \"error\": \"start\"}\n", block); return 1; }
         const double attachMs = double(Ipc::MonotonicNs() - a0) / 1e6;
         Renderer r(block);
         const int iters = std::max(200, int(48000 * 3 / block));
         std::vector<double> rt, call;
         int64_t t = 0;
         auto next = std::chrono::steady_clock::now();
         for (int k = 0; k < iters; ++k, t += block)
         {
            r.Block(rig.shell, t, block, 0, false, false);
            rig.shell.WaitDone(1000000000ull);
            const uint64_t t2 = Ipc::MonotonicNs();
            if (k >= 20) { call.push_back(double(r.callNs) / 1e3); rt.push_back(double(t2 - r.postedAt) / 1e3); }
            next += std::chrono::nanoseconds(int64_t(1e9 * block / 48000));
            std::this_thread::sleep_until(next);
         }
         std::sort(rt.begin(), rt.end());
         std::sort(call.begin(), call.end());
         const double blockUs = 1e6 * block / 48000;
         auto pct = [](const std::vector<double>& v, double p) { return v[size_t(double(v.size() - 1) * p)]; };
         printf("{\"block\": %u, \"block_us\": %.1f, \"rt_p50_us\": %.1f, \"rt_p99_us\": %.1f, \"rt_p99_pct_of_block\": %.1f, "
                "\"call_p50_us\": %.1f, \"call_p99_us\": %.1f, \"call_cpu_pct\": %.2f, \"xruns\": %llu, \"attach_ms\": %.1f}\n",
                block, blockUs, pct(rt, 0.5), pct(rt, 0.99), 100.0 * pct(rt, 0.99) / blockUs, pct(call, 0.5),
                pct(call, 0.99), 100.0 * pct(call, 0.5) / blockUs, (unsigned long long)rig.shell.Xruns(), attachMs);
         fflush(stdout);
         rig.Stop();
      }
      return 0;
   }
}

int main(int argc, char** argv)
{
   if (argc == 4 && std::string(argv[1]) == "--engine") return RunEngine(argv[2], std::atoll(argv[3]));
   if (argc == 2 && std::string(argv[1]) == "--check") return RunChecks();
   return RunBench();
}
