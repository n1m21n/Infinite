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
#include "app/graph/GenerateSamplesCritic.inc"
#include "app/graph/GenerateSamplesNode.h"
#include "app/graph/OfflineGraph.h"
#include "audio/AudioFileWriter.h"
#include "app/graph/SampleStyles.h"
#include "app/graph/UserStyle.h"
#include "core/AudioSummary.h"
#include "audio/NoteModel.h"
#include "platform/AppPaths.h"
#include "audio/MusicTime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
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
   // INFINITE_GENSAMPLEPARITY=1: only what the wasm parity test compares (tools/web_gen_parity.py).
   const bool parity = getenv("INFINITE_GENSAMPLEPARITY") != nullptr;
   static const int kParityStyles[5] = { 0, 7, 14, 21, 28 }, kParityParts[6] = { 0, 1, 2, 3, 5, 6 };
   for (int i = 0; i < SampleStyles::Count(); i++)
      for (int pi = 0; pi < (int)SampleStyles::Part::Count; pi++)
         for (uint32_t seed = 1; seed <= (parity ? 1u : 3u); seed++)
         {
            if (parity && (std::find(kParityStyles, kParityStyles + 5, i) == kParityStyles + 5 ||
                           std::find(kParityParts, kParityParts + 6, pi) == kParityParts + 6))
               continue;
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
               if (!GenerateSamplesNode::Master((int)rec.part, left.data() + pre, right.data() + pre, frames, l, r))
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

// INFINITE_GENSAMPLEDIVERSITY=<dir> [INFINITE_GENSAMPLETAKES=<n per part>]: the diversity baseline's
// renders. Takes as the node makes them (a random seed draws the card, 2 bars, the generic card only:
// no personal profile, so the numbers do not depend on whose machine it runs), as <dir>/<part>-<i>.wav
// plus <dir>/takes.json with each take's recipe, the node types in its patch and a hash of its patch.
// tools/gensample_diversity.py reads it.
bool RunGenSampleDiversity(const std::string& dir, int takes)
{
   FILE* index = fopen((dir + "/takes.json").c_str(), "w");
   if (index == nullptr)
   {
      printf("GENSAMPLEDIVERSITY cannot write %s\n", dir.c_str());
      return false;
   }
   fprintf(index, "[\n");
   int made = 0, failed = 0;
   for (int pi = 0; pi < (int)SampleStyles::Part::Count; pi++)
      for (int i = 0; i < takes; i++)
      {
         SampleStyles::Recipe rec;
         const uint32_t seed = 1000u + (uint32_t)i * 37u + (uint32_t)pi * 100003u;
         rec.style = SampleStyles::At((int)((seed * 2654435761u >> 8) % (uint32_t)SampleStyles::Count())).id;
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
            if (!GenerateSamplesNode::Master((int)rec.part, left.data() + pre, right.data() + pre, frames, l, r))
               continue;
            std::vector<float> inter((size_t)frames * 2);
            for (int f = 0; f < frames; f++)
            {
               inter[(size_t)f * 2] = l[f];
               inter[(size_t)f * 2 + 1] = r[f];
            }
            const std::string name = std::string(SampleStyles::PartName(rec.part)) + "-" + std::to_string(i);
            if (!AudioRecordings::WriteWav(dir + "/" + name + ".wav", inter.data(), frames, kRate, 2))
               continue;
            // The node types the patch uses, in order, and a hash of the whole patch text.
            std::string types;
            size_t pos = 0;
            while ((pos = c.patch.find("\nnode ", pos)) != std::string::npos)
            {
               const size_t eol = c.patch.find('\n', pos + 1);
               const std::string line = c.patch.substr(pos + 1, eol - pos - 1);
               const size_t t = line.find(' ', line.find(' ', 5) + 1); // after "node N Category"
               types += (types.empty() ? "" : "|") + (t == std::string::npos ? line : line.substr(t + 1));
               pos = eol == std::string::npos ? c.patch.size() : eol;
            }
            const uint64_t hash = std::hash<std::string>()(c.patch);
            fprintf(index, "%s{\"file\":\"%s\",\"style\":\"%s\",\"part\":\"%s\",\"seed\":%u,\"root\":%d,\"scale\":\"%s\",\"bpm\":%.2f,\"types\":\"%s\",\"hash\":\"%llx\"}\n",
                    made > 0 ? "," : "", name.c_str(), rec.style.c_str(), SampleStyles::PartName(rec.part), rec.seed, c.root,
                    MusicTime::ScaleTable(c.scale).name, c.bpm, types.c_str(), (unsigned long long)hash);
            ok = true;
         }
         made += ok ? 1 : 0;
         failed += ok ? 0 : 1;
      }
   fprintf(index, "]\n");
   fclose(index);
   printf("GENSAMPLEDIVERSITY %d made, %d failed\n", made, failed);
   return failed == 0;
}

namespace
{
   // One AudioSummary measurement as a JSON line.
   void WriteSpectrum(FILE* out, const char* part, int take, const char* kind, const std::vector<float>& l, const std::vector<float>& r)
   {
      AudioSummary::Analyzer an(kRate);
      an.Push(l.data(), r.data(), (int)l.size());
      const AudioSummary::Result s = an.Finish();
      const double rms = std::sqrt(0.5 * (s.rms[0] * s.rms[0] + s.rms[1] * s.rms[1]));
      fprintf(out, "{\"part\":\"%s\",\"take\":%d,\"kind\":\"%s\",\"peak\":%.6g,\"rms\":%.6g,\"silence\":%.4f,\"corr\":%.4f,\"bands\":[",
              part, take, kind, s.samplePeak, rms, s.silenceRatio, s.correlation);
      for (int b = 0; b < AudioSummary::kBands; b++)
         fprintf(out, "%s%.6g", b > 0 ? "," : "", s.bandPower[b]);
      fprintf(out, "]}\n");
   }

   // Four one-pole passes: a steep enough slope to empty the band above (low) or below (high) `hz`.
   void OnePole(std::vector<float>& x, double hz, bool high)
   {
      const double a = std::exp(-2.0 * M_PI * hz / kRate);
      for (int pass = 0; pass < 4; pass++)
      {
         double y = 0.0;
         for (float& v : x)
         {
            y = (1.0 - a) * v + a * y;
            v = high ? (float)(v - y) : (float)y;
         }
      }
   }
}

// INFINITE_GENSAMPLEDRUMS=<file>: the compiled patch of `takes` seeds per style for the full mix and the
// drums part, one JSON line each, no render. tools/universe/benchmark.py B4 reads the Drum Sequencer's
// lane steps from them, so the realism test scores the grooves the app ships, not a Python stand-in.
bool RunGenSampleDrums(const std::string& file, int takes)
{
   FILE* out = fopen(file.c_str(), "w");
   if (out == nullptr)
   {
      printf("GENSAMPLEDRUMS cannot write %s\n", file.c_str());
      return false;
   }
   int made = 0;
   for (int i = 0; i < SampleStyles::Count(); i++)
      for (SampleStyles::Part part : { SampleStyles::Part::Full, SampleStyles::Part::Drums })
         for (int t = 0; t < takes; t++)
         {
            SampleStyles::Recipe rec;
            rec.style = SampleStyles::At(i).id;
            rec.part = part;
            rec.bars = 2;
            rec.seed = 9000u + (uint32_t)t * 131u;
            const SampleStyles::Compiled c = SampleStyles::Compile(rec);
            if (!c.error.empty())
               continue;
            std::string esc;
            for (char ch : c.patch)
            {
               if (ch == '"' || ch == '\\')
                  esc += '\\', esc += ch;
               else if (ch == '\n')
                  esc += "\\n";
               else if ((unsigned char)ch >= 0x20)
                  esc += ch;
            }
            fprintf(out, "{\"style\":\"%s\",\"part\":%d,\"seed\":%u,\"bpm\":%.2f,\"patch\":\"%s\"}\n", rec.style.c_str(), (int)part, rec.seed, c.bpm, esc.c_str());
            made++;
         }
   fclose(out);
   printf("GENSAMPLEDRUMS %d patches written\n", made);
   return made > 0;
}

bool RunGenSampleSpectrum(const std::string& file, int takes)
{
   FILE* out = fopen(file.c_str(), "w");
   if (out == nullptr)
   {
      printf("GENSAMPLESPECTRUM cannot write %s\n", file.c_str());
      return false;
   }
   int made = 0;
   for (int pi = 0; pi < (int)SampleStyles::Part::Count; pi++)
      for (int i = 0; i < takes; i++)
      {
         SampleStyles::Recipe rec;
         const uint32_t seed = 5000u + (uint32_t)i * 37u + (uint32_t)pi * 100003u;
         rec.style = SampleStyles::At((int)((seed * 2654435761u >> 8) % (uint32_t)SampleStyles::Count())).id;
         rec.part = (SampleStyles::Part)pi;
         rec.bars = 2;
         rec.seed = seed;
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
         std::vector<float> left, right;
         if (!g.Prepare(kRate) || !g.Render(pre + frames, left, right))
            continue;
         std::vector<float> l(left.begin() + pre, left.begin() + pre + frames), r(right.begin() + pre, right.begin() + pre + frames);
         double peak = 0.0, sum = 0.0;
         bool finite = true;
         for (int f = 0; f < frames; f++)
         {
            finite = finite && std::isfinite(l[f]) && std::isfinite(r[f]);
            peak = std::max(peak, (double)std::max(std::abs(l[f]), std::abs(r[f])));
            sum += 0.5 * ((double)l[f] * l[f] + (double)r[f] * r[f]);
         }
         const double rms = std::sqrt(sum / std::max(1, frames));
         if (!finite || rms < 1e-4) // silent or broken: Master's other checks drop these before the gate
            continue;
         const char* part = SampleStyles::PartName(rec.part);
         WriteSpectrum(out, part, i, "clean", l, r);
         std::vector<float> wl = l, wr = r; // a sine at 3.2 kHz twice the take's power
         for (int f = 0; f < frames; f++)
         {
            const float s = (float)(2.0 * rms * std::sin(2.0 * M_PI * 3200.0 * f / kRate));
            wl[f] += s;
            wr[f] += s;
         }
         WriteSpectrum(out, part, i, "whistle", wl, wr);
         std::vector<float> nr(l.size());
         for (int f = 0; f < frames; f++)
            nr[f] = -l[f];
         WriteSpectrum(out, part, i, "phase", l, nr);
         std::vector<float> bl = l, br = r; // driven 30x into a hard clip
         for (int f = 0; f < frames; f++)
         {
            bl[f] = std::clamp(bl[f] * (float)(30.0 / std::max(1e-6, peak)), -1.0f, 1.0f);
            br[f] = std::clamp(br[f] * (float)(30.0 / std::max(1e-6, peak)), -1.0f, 1.0f);
         }
         WriteSpectrum(out, part, i, "brickwall", bl, br);
         std::vector<float> dl = l, dr = r; // the first 8% survives
         for (int f = frames * 8 / 100; f < frames; f++)
            dl[f] = dr[f] = 0.0f;
         WriteSpectrum(out, part, i, "dropout", dl, dr);
         std::vector<float> ml = l, mr = r;
         OnePole(ml, 150.0, false);
         OnePole(mr, 150.0, false);
         WriteSpectrum(out, part, i, "muffled", ml, mr);
         std::vector<float> tl = l, tr = r;
         OnePole(tl, 1500.0, true);
         OnePole(tr, 1500.0, true);
         WriteSpectrum(out, part, i, "thin", tl, tr);
         made++;
      }
   fclose(out);
   printf("GENSAMPLESPECTRUM %d takes measured\n", made);
   return made > 0;
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
   if (const char* dump = getenv("INFINITE_GENSAMPLEPATCH")) // "<style>:<part>:<seed>": print the compiled patch and stop
   {
      SampleStyles::Recipe rec;
      char style[64] = {};
      int pi = 0;
      unsigned sd = 1;
      if (sscanf(dump, "%63[^:]:%d:%u", style, &pi, &sd) == 3)
      {
         rec.style = style;
         rec.part = (SampleStyles::Part)pi;
         rec.seed = sd;
         SampleStyles::Compiled c = SampleStyles::Compile(rec);
         if (const char* file = getenv("INFINITE_GENSAMPLEFILE")) // render an edited copy of the patch instead
         {
            FILE* f = fopen(file, "rb");
            std::string text;
            char buf[4096];
            size_t got;
            while (f != nullptr && (got = fread(buf, 1, sizeof(buf), f)) > 0)
               text.append(buf, got);
            if (f != nullptr)
               fclose(f);
            c.patch = text;
         }
         printf("%s\n", c.patch.c_str());
         const int frames = (int)(4 * 60.0 / c.bpm * kRate);
         Result r = RenderOnWorker(c, frames);
         printf("GENSAMPLEPATCH rms %.1f dB (%zu motions)\n", r.rmsDb, c.motions.size());
         c.motions.clear();
         r = RenderOnWorker(c, frames);
         printf("GENSAMPLEPATCH without motions rms %.1f dB\n", r.rmsDb);
         for (int t = getenv("INFINITE_GENSAMPLEFILE") ? 14 : 1; t <= (getenv("INFINITE_GENSAMPLEFILE") ? 14 : 24); t++)
         {
            c.tap = t;
            r = RenderOnWorker(c, frames);
            printf("GENSAMPLEPATCH tap %d rms %.1f dB %s\n", t, r.rmsDb, r.error.c_str());
         }
      }
      return true;
   }
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
         if (!r.ok || seed == 1 || r.ms > 1000.0)
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
   // Profile: a player whose notes are mostly A (pitch class 9), played loud and off the grid. A personal
   // recipe should land on that root most of the time, a plain one not, and still render.
   if (runs("profile"))
   {
      printf("GENSAMPLETEST profile\n");
      const std::string dir = AppPaths::TempDir() + "/infinite-userstyle-test";
      std::filesystem::create_directories(AppPaths::FsPath(dir));
      std::vector<NoteModel::Event> ev;
      for (int i = 0; i < 512; i++)
      {
         NoteModel::Event e;
         e.note = (uint8_t)(i % 5 == 0 ? 60 : 57);
         e.metric = (uint8_t)((i * 3) & 15);
         e.ioiTicks = 5;
         e.durTicks = 12;
         ev.push_back(e);
      }
      auto writeInts = [&](const char* name, int v)
      {
         std::ofstream f(AppPaths::FsPath(dir + "/" + name), std::ios::binary | std::ios::trunc);
         const uint32_t n = 32;
         f.write(reinterpret_cast<const char*>(&n), sizeof(n));
         for (uint32_t i = 0; i < n; i++)
         {
            const int32_t x = v;
            f.write(reinterpret_cast<const char*>(&x), sizeof(x));
         }
      };
      {
         std::ofstream f(AppPaths::FsPath(dir + "/notes_style.bin"), std::ios::binary | std::ios::trunc);
         const std::string text = NoteModel::EncodeEvents(ev);
         f.write(text.data(), (std::streamsize)text.size());
      }
      writeInts("velocity_profile.bin", 120);
      writeInts("quantize_profile.bin", 5);
      UserStyle::UseDirectory(dir);
      const UserStyle::Profile& pr = UserStyle::Get();
      const bool read = pr.valid && pr.hasPitch && pr.hasVelocity && pr.hasTiming && pr.pitchClass[9] > 0.4f &&
                        pr.velMean > 0.9f && pr.offGrid > 0.9f;
      int onA = 0, onAPlain = 0;
      for (uint32_t seed = 1; seed <= 20; seed++)
      {
         SampleStyles::Recipe rec;
         rec.style = "deep_house";
         rec.seed = seed;
         rec.personal = true;
         onA += SampleStyles::Compile(rec).root == 9 ? 1 : 0;
         rec.personal = false;
         onAPlain += SampleStyles::Compile(rec).root == 9 ? 1 : 0;
      }
      SampleStyles::Recipe rec;
      rec.style = "deep_house";
      rec.part = SampleStyles::Part::Plucks;
      rec.personal = true;
      const SampleStyles::Compiled c = SampleStyles::Compile(rec);
      const Result r = RenderOnWorker(c, (int)(4 * 60.0 / c.bpm * kRate));
      printf("  read %s, root A %d/20 personal vs %d/20 plain, render %s\n", read ? "ok" : "FAIL", onA, onAPlain, r.ok ? "ok" : "FAIL");
      fails += (read && onA >= 9 && onA > onAPlain + 5 && r.ok) ? 0 : 1;
      // The player's chords and note length come out of the same notes, and the weight grows with them.
      const bool shaped = pr.durW > 0.0f && pr.pitchW > 0.0f && pr.pitchW < UserStyle::kMaxWeight + 1e-6f;
      // Taste: a card that lands is drawn more often than one that is skipped, and none is ruled out.
      for (int i = 0; i < 6; i++)
      {
         UserStyle::NoteGenerate(1, 5);
         UserStyle::NoteKept();
      }
      for (int i = 0; i < 6; i++)
      {
         UserStyle::NoteGenerate(1, 7);
         UserStyle::NoteGenerate(1, 8); // straight after: card 7 was skipped
         UserStyle::NoteKept();
      }
      int drewLanded = 0, drewSkipped = 0;
      for (int i = 0; i < 1000; i++)
      {
         const int c = UserStyle::DrawCard(1, (i + 0.5) / 1000.0, SampleStyles::Count());
         drewLanded += c == 5;
         drewSkipped += c == 7;
      }
      const bool tasteOk = drewLanded > 35 && drewSkipped * 10 < drewLanded * 6 && drewSkipped > 0;
      printf("  weights pitch %.2f dur %.2f, taste landed %d / skipped %d of 1000 draws: %s\n", pr.pitchW, pr.durW, drewLanded, drewSkipped,
             shaped && tasteOk ? "ok" : "FAIL");
      fails += (shaped && tasteOk) ? 0 : 1;
      {
         // Knob habits: a real habit (bins 20-22) plus a lone stray wobble (bin 55) and noise.
         float h[64] = {};
         for (int i = 0; i < 64; i++)
            h[i] = 0.2f;
         h[20] = 100.0f;
         h[21] = 140.0f;
         h[22] = 90.0f;
         h[55] = 6.0f;
         int inHabit = 0, stray = 0, drawn = 0;
         for (int i = 0; i < 1000; i++)
         {
            float v = 0.0f;
            if (!UserStyle::DrawFromHistogram(h, 0.0f, 64.0f, (i + 0.5) / 1000.0, v))
               continue;
            drawn++;
            inHabit += v >= 17.0f && v <= 26.0f;
            stray += v >= 53.0f;
         }
         const bool knobsOk = drawn == 1000 && inHabit > 950 && stray == 0;
         float none = 0.0f, zero[64] = {};
         const bool emptyOk = !UserStyle::DrawFromHistogram(zero, 0.0f, 1.0f, 0.5, none);
         printf("  knob habit: %d of 1000 in the habit, %d stray, empty histogram refused: %s\n", inHabit, stray,
                knobsOk && emptyOk ? "ok" : "FAIL");
         fails += (knobsOk && emptyOk) ? 0 : 1;
      }
      {
         // 16-feature contextual preference critic: keeping high-syncopation / high-novelty takes and skipping flat ones.
         float hi[UserStyle::kFeatureDim] = {}, lo[UserStyle::kFeatureDim] = {};
         hi[0] = 0.6f; hi[2] = 0.7f; hi[5] = 0.8f; hi[8] = 0.75f; hi[15] = 1.0f;
         lo[0] = 0.2f; lo[2] = 0.05f; lo[5] = 0.1f; lo[8] = 0.1f; lo[15] = 1.0f;
         for (int i = 0; i < 8; i++)
         {
            UserStyle::NoteGenerate(0, 2);
            UserStyle::NoteFeatures(hi);
            UserStyle::NoteKept();
         }
         for (int i = 0; i < 8; i++)
         {
            UserStyle::NoteGenerate(0, 3);
            UserStyle::NoteFeatures(lo);
            UserStyle::NoteGenerate(0, 4); // immediate redraw = skipped lo
            UserStyle::NoteFeatures(hi);
            UserStyle::NoteKept();
         }
         float wHi = 0.0f, wLo = 0.0f;
         const float sHi = UserStyle::ScoreFeatures(hi, 0.5, wHi);
         const float sLo = UserStyle::ScoreFeatures(lo, 0.5, wLo);
         const bool featOk = wHi > 0.35f && sHi > 0.60f && sLo < 0.40f && (sHi - sLo) > 0.25f;
         printf("  feature critic: weight %.2f, kept score %.2f vs skipped %.2f: %s\n", wHi, sHi, sLo, featOk ? "ok" : "FAIL");
         fails += featOk ? 0 : 1;

         // 32-band spectral & dynamic gate: balanced spectrum accepted, narrow whistle & phase cancellation rejected.
         double goodBands[32] = {}, whistleBands[32] = {};
         for (int i = 0; i < 32; i++)
         {
            goodBands[i] = 0.03;
            whistleBands[i] = 0.002;
         }
         whistleBands[24] = 0.90;
         const bool spGood = SampleCriticData::VerifyAudioSpectrum(0, goodBands, 0.85, 0.25, 0.05, 0.45);
         const bool spWhistle = !SampleCriticData::VerifyAudioSpectrum(0, whistleBands, 0.85, 0.25, 0.05, 0.45);
         const bool spPhase = !SampleCriticData::VerifyAudioSpectrum(0, goodBands, 0.85, 0.25, 0.05, -0.75);
         const bool specGateOk = spGood && spWhistle && spPhase && SampleCriticData::kStyleDirectiveCount >= 34;
         printf("  spectral & genre graph critic: %d styles, balanced ok, whistle & phase rejected: %s\n",
                SampleCriticData::kStyleDirectiveCount, specGateOk ? "ok" : "FAIL");
         fails += specGateOk ? 0 : 1;
      }
      UserStyle::UseDirectory("");
      std::filesystem::remove_all(AppPaths::FsPath(dir));
   }
   // Tempo: reads this machine's own movement log (so reported, not failed, when there is no history).
   if (only == "tempo")
   {
      printf("GENSAMPLETEST tempo (this machine's movement log)\n");
      float bpm = 0.0f, w = 0.0f;
      const auto t0 = std::chrono::steady_clock::now();
      bool got = false;
      while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 240.0)
      {
         got = UserStyle::DrawTempo(0.5, bpm, w);
         if (got)
            break;
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      printf("  %s: median %.0f bpm, weight %.2f, %.1f s\n", got ? "history" : "no history yet", bpm, w,
             std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
      int hist[8] = {};
      for (int i = 0; i < 400 && got; i++)
      {
         UserStyle::DrawTempo((i + 0.5) / 400.0, bpm, w);
         hist[std::clamp((int)bpm / 30, 0, 7)]++;
      }
      printf("  draws per 30 bpm band from 0:");
      for (int h : hist)
         printf(" %d", h);
      printf("\n");
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
