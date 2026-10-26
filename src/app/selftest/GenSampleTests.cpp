// INFINITE_GENSAMPLETEST: the Generate Samples render path, off the main thread.
//
//   types   every type a style can place, in the smallest patch that sounds
//           it (SampleStyles::Probe), built and prepared on the main thread
//           and rendered for 2 s on a worker. Fails on non-finite output or
//           silence: a node that needs the live engine to make sound would
//           generate empty samples.
//   styles  every style card compiled at one seed (three with =full) and rendered for
//           2 bars on a worker, motions on. Same checks, plus the time a
//           2-bar generate takes against its 0.5 s budget: reported always,
//           failed past twice that in optimised builds only.
//   parts   each layer (synths, pads, plucks, percussion, drums) alone for one
//           style per family, 1 bar: every part must sound.
//
// INFINITE_GENSAMPLEBATCH=<dir>: not a test. Renders the website's generate
// pool the way the node makes a loop (pre-roll, redraws, -14 LUFS): every
// style x part at seeds 1-3, 2 bars, as <dir>/<style>-<part>-<seed>.wav plus
// <dir>/pool.json. tools/catalog/catalog.py pool publishes it.
#include "app/AppShared.h"
#include "app/graph/GenerateSamplesNode.h"
#include "app/graph/OfflineGraph.h"
#include "audio/AudioFileWriter.h"
#include "app/graph/SampleStyles.h"
#include "audio/MusicTime.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <set>
#include <thread>

namespace app
{
namespace
{
   constexpr double kRate = 48000.0;

   struct Result
   {
      bool ok = false;
      bool finite = true;
      double rmsDb = -200.0;
      double ms = 0.0;
      std::string error;
   };

   Result RenderOnWorker(const SampleStyles::Compiled& c, int frames)
   {
      Result r;
      const auto t0 = std::chrono::steady_clock::now();
      Patch::Data data;
      if (!Patch::ReadText(c.patch, data, r.error))
         return r;
      OfflineGraph g;
      if (!g.Build(data, c.tap))
      {
         r.error = g.Error();
         return r;
      }
      for (const OfflineGraph::Motion& m : c.motions)
         g.AddMotion(m);
      if (!g.Prepare(kRate))
      {
         r.error = g.Error();
         return r;
      }
      std::vector<float> left, right;
      std::thread worker([&] { g.Render(frames, left, right); });
      worker.join();
      r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      double sum = 0.0;
      for (int i = 0; i < frames; i++)
      {
         if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
            r.finite = false;
         sum += (double)left[i] * left[i] + (double)right[i] * right[i];
      }
      r.rmsDb = 10.0 * std::log10(sum / (2.0 * std::max(1, frames)) + 1e-20);
      r.ok = r.finite && r.rmsDb > -60.0;
      return r;
   }
}

bool RunGenSampleBatch(const std::string& dir)
{
   FILE* index = fopen((dir + "/pool.json").c_str(), "w");
   if (index == nullptr)
   {
      printf("GENSAMPLEBATCH cannot write %s\n", dir.c_str());
      return false;
   }
   fprintf(index, "[\n");
   int made = 0, failed = 0;
   for (int i = 0; i < SampleStyles::Count(); i++)
      for (int pi = 0; pi < (int)SampleStyles::Part::Count; pi++)
         for (uint32_t seed = 1; seed <= 3; seed++)
         {
            SampleStyles::Recipe rec;
            rec.style = SampleStyles::At(i).id;
            rec.part = (SampleStyles::Part)pi;
            rec.bars = 2;
            bool ok = false;
            for (int attempt = 0; attempt <= 3 && !ok; attempt++)
            {
               rec.seed = seed + (uint32_t)attempt * 7919u;
               const SampleStyles::Compiled c = SampleStyles::Compile(rec);
               const int bar = (int)std::lround(4.0 * 60.0 / std::max(20.0f, c.bpm) * kRate);
               const int pre = bar * GenerateSamplesNode::PrerollBars(rec.bars);
               const int frames = bar * rec.bars;
               Patch::Data data;
               std::string err;
               OfflineGraph g;
               if (!c.error.empty() || !Patch::ReadText(c.patch, data, err) || !g.Build(data, c.tap))
                  continue;
               for (const OfflineGraph::Motion& m : c.motions)
                  g.AddMotion(m);
               std::vector<float> left, right, l, r;
               if (!g.Prepare(kRate) || !g.Render(pre + frames, left, right))
                  continue;
               if (!GenerateSamplesNode::Master(left.data() + pre, right.data() + pre, frames, l, r))
                  continue;
               std::vector<float> inter((size_t)frames * 2);
               for (int f = 0; f < frames; f++)
               {
                  inter[(size_t)f * 2] = l[f];
                  inter[(size_t)f * 2 + 1] = r[f];
               }
               const std::string name = rec.style + "-" + SampleStyles::PartName(rec.part) + "-" + std::to_string(seed);
               if (!AudioRecordings::WriteWav(dir + "/" + name + ".wav", inter.data(), frames, kRate, 2))
                  continue;
               fprintf(index, "%s{\"file\":\"%s\",\"style\":\"%s\",\"part\":\"%s\",\"seed\":%u,\"root\":%d,\"scale\":\"%s\",\"bpm\":%.2f,\"label\":\"%s\"}\n",
                       made > 0 ? "," : "", name.c_str(), rec.style.c_str(), SampleStyles::PartName(rec.part), rec.seed, c.root,
                       MusicTime::ScaleTable(c.scale).name, c.bpm, GenerateSamplesNode::Label(rec, c.root, c.scale).c_str());
               ok = true;
            }
            made += ok ? 1 : 0;
            failed += ok ? 0 : 1;
            if (!ok)
               printf("  %-24s %-10s seed %u FAIL\n", rec.style.c_str(), SampleStyles::PartName(rec.part), seed);
         }
   fprintf(index, "]\n");
   fclose(index);
   printf("GENSAMPLEBATCH %d made, %d failed\n", made, failed);
   return failed == 0;
}

bool RunGenSampleTest()
{
   // INFINITE_GENSAMPLETEST=1 runs every section, the styles at seed 1 (a
   // debug build renders all three seeds in ~10 minutes); =full every
   // section at seeds 1-3; =types, =styles (seeds 1-3) or =parts one.
   const char* env = getenv("INFINITE_GENSAMPLETEST");
   const std::string arg = env != nullptr ? env : "1";
   const std::string only = arg == "1" || arg == "full" ? "" : arg;
   const uint32_t seeds = arg == "1" ? 1u : 3u;
   auto runs = [&](const char* section) { return only.empty() || only == section; };
   int fails = 0;
   if (runs("types"))
      printf("GENSAMPLETEST types\n");
   for (const std::string& type : runs("types") ? SampleStyles::PoolTypes() : std::vector<std::string>())
   {
      const Result r = RenderOnWorker(SampleStyles::Probe(type), (int)(2.0 * kRate));
      printf("  %-20s %s rms %7.1f dB %6.1f ms %s\n", type.c_str(), r.ok ? "ok  " : "FAIL", r.rmsDb, r.ms, r.error.c_str());
      fails += r.ok ? 0 : 1;
   }
   double worst = 0.0;
   if (runs("styles"))
      printf("GENSAMPLETEST styles (2 bars, seeds 1-%u)\n", seeds);
   for (int i = 0; runs("styles") && i < SampleStyles::Count(); i++)
      for (uint32_t seed = 1; seed <= seeds; seed++)
      {
         SampleStyles::Recipe rec;
         rec.style = SampleStyles::At(i).id;
         rec.seed = seed;
         const SampleStyles::Compiled c = SampleStyles::Compile(rec);
         const int frames = (int)(2 * 4 * 60.0 / c.bpm * kRate);
         const Result r = RenderOnWorker(c, frames);
         worst = std::max(worst, r.ms);
         if (!r.ok || seed == 1)
            printf("  %-24s seed %u %s rms %7.1f dB %6.1f ms %s\n", rec.style.c_str(), seed, r.ok ? "ok  " : "FAIL", r.rmsDb, r.ms, r.error.c_str());
         fails += r.ok ? 0 : 1;
      }
   // Parts: each layer alone, for the first style of every family (every
   // style would be 34 x 5 more renders; the families cover the layer code).
   if (runs("parts"))
      printf("GENSAMPLETEST parts (1 bar, seed 1, one style per family)\n");
   std::set<std::string> families;
   for (int i = 0; runs("parts") && i < SampleStyles::Count(); i++)
   {
      if (!families.insert(SampleStyles::At(i).family).second)
         continue;
      for (int pi = 1; pi < (int)SampleStyles::Part::Count; pi++)
      {
         // As the node does: a silent draw is redrawn up to 3 times at another seed.
         SampleStyles::Recipe rec;
         rec.style = SampleStyles::At(i).id;
         rec.part = (SampleStyles::Part)pi;
         Result r;
         int attempt = 0;
         for (; attempt <= 3 && !r.ok; attempt++)
         {
            rec.seed = 1 + (uint32_t)attempt * 7919u;
            const SampleStyles::Compiled c = SampleStyles::Compile(rec);
            r = RenderOnWorker(c, (int)(4 * 60.0 / c.bpm * kRate));
         }
         if (!r.ok || attempt > 1)
            printf("  %-24s %-10s %s rms %7.1f dB, %d redraws %s\n", rec.style.c_str(), SampleStyles::PartName(rec.part), r.ok ? "ok  " : "FAIL",
                   r.rmsDb, attempt - 1, r.error.c_str());
         fails += r.ok ? 0 : 1;
      }
   }
   if (runs("styles"))
      printf("GENSAMPLETEST slowest 2-bar generate %.1f ms (budget 500)\n", worst);
#ifdef NDEBUG
   const bool timed = true;
#else
   const bool timed = false; // -O0 DSP runs 10-50x slower: only an optimised build's time means anything
#endif
   if (timed && worst > 1000.0)
   {
      printf("GENSAMPLETEST FAIL: over twice the budget\n");
      fails++;
   }
   printf("GENSAMPLETEST %s (%d failures)\n", fails == 0 ? "PASS" : "FAIL", fails);
   return fails == 0;
}
}
