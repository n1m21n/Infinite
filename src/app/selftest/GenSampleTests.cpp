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
//   parts   each layer (drums, percussion, synths, piano, fx) alone for one
//           style per family, 1 bar: every part must sound.
#include "app/AppShared.h"
#include "app/graph/OfflineGraph.h"
#include "app/graph/SampleStyles.h"

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
