// plugin_engine_check: drives the real Infinite binary in plugin engine mode through the shell's
// side of the protocol (PluginIpc::ShellSide), the way the VST3/AU shell will (step 1.4).
//
//   plugin_engine_check <Infinite binary> <tests/plugin dir>
//
// Checks: FX pass-through (Audio In -> Audio Out returns the input, delayed by L), instrument
// (host MIDI -> Analog makes sound only after the note), the same offline render twice, one
// transport jump seen when the host relocates, and a clean quit. Prints PASS/FAIL per check;
// exit code 0 only when all pass.
#include "plugin/engine/HostAudio.h"
#include "plugin/ipc/PluginIpc.h"
#include "platform/common/Ipc.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
   constexpr double kRate = 48000.0;
   constexpr uint32_t kBlock = 512;
   int gFails = 0;

   void Check(bool ok, const char* what, const std::string& detail = std::string())
   {
      std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
      std::fflush(stdout);
      if (!ok)
         gFails++;
   }

   struct Engine
   {
      PluginIpc::ShellSide shell;
      Ipc::Child child;
      std::string statsPath;

      bool Start(const std::string& exe, const std::string& patch, bool fx, std::string& err)
      {
         if (!shell.Create(kRate, kBlock))
         {
            err = "shm create failed";
            return false;
         }
         statsPath = "/tmp/plugin_engine_stats_" + std::to_string((long long)Ipc::SelfPid()) + ".txt";
         std::remove(statsPath.c_str());
#if defined(_WIN32)
         _putenv_s("INFINITE_PLUGIN_STATS", statsPath.c_str());
#else
         setenv("INFINITE_PLUGIN_STATS", statsPath.c_str(), 1);
#endif
         std::vector<std::string> args { "--plugin-engine", shell.Name(), std::to_string((long long)Ipc::SelfPid()) };
         if (fx)
            args.push_back("--fx");
         args.push_back(patch);
         if (!Ipc::Spawn(exe, args, &child))
         {
            err = "spawn failed";
            return false;
         }
         if (!shell.WaitAttached(30000, &child))
         {
            err = "engine never attached";
            return false;
         }
         PluginIpc::Message m;
         const auto t0 = std::chrono::steady_clock::now();
         while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60))
         {
            if (shell.Messages().Receive(&m, 200) && m.type == PluginEngine::kMsgReady)
            {
               if (!m.data.empty())
               {
                  err = "engine audio: " + std::string(m.data.begin(), m.data.end());
                  return false;
               }
               return true;
            }
            if (!Ipc::ChildAlive(child))
            {
               err = "engine exited during start";
               return false;
            }
         }
         err = "no ready message";
         return false;
      }

      // Returns "blocks N jumps M" from the stats file, after a clean quit.
      bool Quit(uint64_t* jumps)
      {
         shell.RequestQuit();
         const bool exited = Ipc::ChildWait(&child, 10000);
         if (!exited)
            Ipc::ChildKill(&child);
         std::ifstream f(statsPath);
         std::string word;
         unsigned long long blocks = 0, j = 0;
         if (f >> word >> blocks >> word >> j)
            *jumps = j;
         std::ifstream again(statsPath);
         std::string line;
         std::getline(again, line);
         std::printf("      engine stats: %s\n", line.c_str());
         std::remove(statsPath.c_str());
         return exited;
      }
   };

   // Runs `blocks` offline host blocks. in: main stereo input (may be empty). events: per block.
   struct HostRun
   {
      std::vector<float> outL, outR;
   };

   PluginIpc::Transport HostTransport(uint32_t block, uint32_t jumpAt)
   {
      PluginIpc::Transport t;
      t.tempo = 120.0;
      t.flags = PluginIpc::kPlaying | PluginIpc::kTempoValid | PluginIpc::kPpqValid;
      const double beatsPerBlock = kBlock * (t.tempo / 60.0) / kRate;
      t.ppq = block < jumpAt ? block * beatsPerBlock : 64.0 + (block - jumpAt) * beatsPerBlock;
      return t;
   }

   bool RunBlocks(Engine& e, uint32_t blocks, const std::vector<float>* in,
                  const std::vector<std::vector<PluginIpc::Event>>& events, uint32_t jumpAt, HostRun* run)
   {
      std::vector<float> l(kBlock), r(kBlock), il(kBlock), ir(kBlock);
      for (uint32_t b = 0; b < blocks; b++)
      {
         if (in)
            for (uint32_t i = 0; i < kBlock; i++)
               il[i] = ir[i] = (*in)[b * kBlock + i];
         const float* ins[4] = { il.data(), ir.data(), nullptr, nullptr };
         float* outs[2] = { l.data(), r.data() };
         PluginIpc::ProcessArgs a;
         a.in = ins;
         a.numIn = 2;
         a.out = outs;
         a.numOut = 2;
         a.frames = kBlock;
         a.flags = PluginIpc::kOffline;
         a.transport = HostTransport(b, jumpAt);
         if (b < events.size() && !events[b].empty())
         {
            a.events = events[b].data();
            a.numEvents = (uint32_t)events[b].size();
         }
         if (e.shell.Process(a) != PluginIpc::Result::Ok)
            return false;
         run->outL.insert(run->outL.end(), l.begin(), l.end());
         run->outR.insert(run->outR.end(), r.begin(), r.end());
      }
      return true;
   }

   double Rms(const std::vector<float>& x, size_t from, size_t to)
   {
      double s = 0.0;
      to = std::min(to, x.size());
      if (to <= from)
         return 0.0;
      for (size_t i = from; i < to; i++)
         s += (double)x[i] * x[i];
      return std::sqrt(s / (double)(to - from));
   }

   PluginIpc::Event Note(uint8_t status, uint8_t note, uint8_t vel, uint32_t offset)
   {
      PluginIpc::Event ev {};
      ev.offset = offset;
      ev.status = status;
      ev.data1 = note;
      ev.data2 = vel;
      ev.value = NAN;
      ev.noteId = -1;
      return ev;
   }

   void CheckFx(const std::string& exe, const std::string& dir)
   {
      Engine e;
      std::string err;
      if (!e.Start(exe, dir + "/fx-passthrough.inf", true, err))
      {
         Check(false, "fx: engine starts", err);
         Ipc::ChildKill(&e.child);
         return;
      }
      Check(true, "fx: engine starts");
      const uint32_t blocks = 200;
      std::vector<float> in(blocks * kBlock);
      for (size_t i = 0; i < in.size(); i++)
         in[i] = 0.5f * (float)std::sin(2.0 * M_PI * 440.0 * (double)i / kRate);
      HostRun run;
      const bool ok = RunBlocks(e, blocks, &in, {}, ~0u, &run);
      Check(ok, "fx: offline blocks complete");
      // Output frame t is the engine's frame t - L (L = block). Compare the second half.
      double maxErr = 0.0, peak = 0.0;
      for (size_t t = blocks * kBlock / 2; t < run.outL.size(); t++)
      {
         maxErr = std::max(maxErr, (double)std::fabs(run.outL[t] - in[t - kBlock]));
         maxErr = std::max(maxErr, (double)std::fabs(run.outR[t] - in[t - kBlock]));
         peak = std::max(peak, (double)std::fabs(run.outL[t]));
      }
      char d[128];
      std::snprintf(d, sizeof(d), "max error %.2e, peak %.3f (input 0.500)", maxErr, peak);
      Check(maxErr < 1e-3, "fx: Audio In -> Audio Out returns the host input, delayed by L", d);
      uint64_t jumps = 0;
      Check(e.Quit(&jumps), "fx: quits cleanly on request");
   }

   bool InstrumentRun(const std::string& exe, const std::string& dir, HostRun* run, uint64_t* jumps, std::string& err)
   {
      Engine e;
      if (!e.Start(exe, dir + "/instrument.inf", false, err))
      {
         Ipc::ChildKill(&e.child);
         return false;
      }
      std::vector<std::vector<PluginIpc::Event>> ev(300);
      ev[20].push_back(Note(0x90, 60, 100, 100));
      ev[120].push_back(Note(0x80, 60, 0, 37));
      if (!RunBlocks(e, 300, nullptr, ev, 200, run))
      {
         err = "offline blocks failed";
         Ipc::ChildKill(&e.child);
         return false;
      }
      if (!e.Quit(jumps))
      {
         err = "did not quit";
         return false;
      }
      return true;
   }

   void CheckInstrument(const std::string& exe, const std::string& dir)
   {
      HostRun a, b;
      uint64_t jumpsA = 0, jumpsB = 0;
      std::string err;
      const bool okA = InstrumentRun(exe, dir, &a, &jumpsA, err);
      Check(okA, "instrument: engine runs 300 offline blocks and quits", err);
      if (!okA)
         return;
      // Note on in host block 20 at offset 100 -> output stream frame 21*512 + 100 - L... = engine
      // frame 20*512+96 (32-sample grid), heard at output frame +L.
      const size_t onAt = 20 * kBlock + 96 + kBlock;
      const double before = Rms(a.outL, 0, onAt);
      const double during = Rms(a.outL, onAt + 256, 120 * kBlock);
      size_t first = 0;
      for (size_t i = 0; i < a.outL.size(); i++)
         if (std::fabs(a.outL[i]) > 1e-4f)
         {
            first = i;
            break;
         }
      char d[160];
      std::snprintf(d, sizeof(d), "rms before %.2e, after %.3f, first sound at frame %zu (note grid frame %zu)", before, during, first, onAt);
      Check(before < 1e-5 && during > 1e-3, "instrument: host note-on drives MIDI Notes -> Analog", d);
      Check(first >= onAt && first < onAt + 64, "instrument: note lands on its 32-sample grid point", d);
      std::snprintf(d, sizeof(d), "jumps %llu", (unsigned long long)jumpsA);
      Check(jumpsA == 1, "transport: one jump when the host relocates (block 200)", d);

      const bool okB = InstrumentRun(exe, dir, &b, &jumpsB, err);
      Check(okB, "repeat: second engine run", err);
      if (!okB)
         return;
      double maxDiff = 0.0;
      for (size_t i = 0; i < std::min(a.outL.size(), b.outL.size()); i++)
         maxDiff = std::max(maxDiff, (double)std::fabs(a.outL[i] - b.outL[i]));
      const double ra = Rms(a.outL, 0, a.outL.size()), rb = Rms(b.outL, 0, b.outL.size());
      std::snprintf(d, sizeof(d), "max sample diff %.2e, rms %.4f vs %.4f", maxDiff, ra, rb);
      Check(ra > 0.0 && std::fabs(ra - rb) / ra < 0.01, "repeat: same offline render twice gives the same sound", d);
   }
}

int main(int argc, char** argv)
{
   if (argc < 3)
   {
      std::fprintf(stderr, "usage: plugin_engine_check <Infinite binary> <tests/plugin dir>\n");
      return 2;
   }
   const std::string exe = argv[1], dir = argv[2];
   CheckFx(exe, dir);
   CheckInstrument(exe, dir);
   std::printf("%s (%d failed)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
   return gFails == 0 ? 0 : 1;
}
