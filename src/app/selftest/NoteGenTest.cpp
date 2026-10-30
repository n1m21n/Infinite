// INFINITE_NOTEGENTEST: the Piano Roll's note generator against its spec
// (docs/plans/piano-roll/README.md §12.6). Every type over every style, three
// seeds each: notes come out, sit in a playable register and in the page's
// key, never overlap on one pitch, fit the take, and the same seed gives the
// same notes twice. Then form, locks, range, the critic gate and the
// settings line's round trip.
#include "app/graph/NoteGen.h"
#include "app/graph/SampleStyles.h"
#include "audio/MusicTime.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>

namespace NoteGen
{
bool RunSelfTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   // INFINITE_NOTEGENSPEC="type=pads style=deep_house seed=2": print that one take and stop.
   if (const char* line = getenv("INFINITE_NOTEGENSPEC"))
   {
      GenSpec spec;
      if (!Parse(line, spec))
         return false;
      const Take t = Generate(spec);
      printf("NOTEGENSPEC %s -> %zu notes, length %lld, key %d/%d, seed %u, redraws %d, critic %.2f %s\n",
             Serialize(spec).c_str(), t.notes.size(), (long long)t.length, t.root, t.scale, t.seed, t.redraws, t.criticScore,
             t.error.c_str());
      for (const Arrange::Note& n : t.notes)
         printf("  s=%lld l=%lld p=%d v=%d\n", (long long)n.start, (long long)n.length, (int)n.pitch, (int)n.vel);
      return t.error.empty();
   }
   bool ok = true;
   auto check = [&](bool cond, const std::string& what) {
      if (!cond)
         printf("NOTEGENTEST %s FAIL\n", what.c_str());
      ok = ok && cond;
      return cond;
   };
   using Arrange::Note;
   using Arrange::Tick;

   auto sameNotes = [](const std::vector<Note>& a, const std::vector<Note>& b) { return a == b; };
   auto overlaps = [](const std::vector<Note>& notes) {
      std::map<int, Tick> heldUntil;
      for (const Note& n : notes)
      {
         auto it = heldUntil.find(n.pitch);
         if (it != heldUntil.end() && it->second > n.start)
            return true;
         heldUntil[n.pitch] = n.End();
      }
      return false;
   };

   // 1. Every type x every style x 3 seeds, in a set key (D dorian-or-minor: the page's key).
   const auto t0 = std::chrono::steady_clock::now();
   const int key = 2, scale = 1;
   int takes = 0, empty = 0, melodyJudged = 0, melodyPassed = 0;
   std::map<std::string, int> emptyBy;
   for (int t = 0; t < (int)Type::Count; t++)
      for (int si = 0; si < SampleStyles::Count(); si++)
         for (uint32_t seed : { 1u, 2u, 3u })
         {
            GenSpec spec;
            spec.type = (Type)t;
            spec.style = SampleStyles::At(si).id;
            spec.seed = seed;
            spec.bars = 4;
            spec.key = key;
            spec.scale = scale;
            const std::string tag = std::string(TypeName(spec.type)) + "/" + spec.style + "/" + std::to_string(seed);
            const Take a = Generate(spec);
            takes++;
            if (!check(a.error.empty(), tag + " error " + a.error))
               continue;
            if (a.notes.empty())
            {
               empty++;
               emptyBy[TypeName(spec.type)]++;
               printf("NOTEGENTEST %s no notes\n", tag.c_str());
               continue;
            }
            check(a.length == Arrange::kTicksPerBar * 4, tag + " take length");
            bool inRange = true, inKey = true, fits = true, ids = true;
            for (size_t i = 0; i < a.notes.size(); i++)
            {
               const Note& n = a.notes[i];
               inRange = inRange && n.pitch >= 21 && n.pitch <= 108 && n.vel >= 1 && n.vel <= 127;
               if (spec.type != Type::Drums)
                  inKey = inKey && MusicTime::ScaleContainsPitchClass(scale, ((n.pitch - key) % 12 + 12) % 12);
               fits = fits && n.start >= 0 && n.length > 0 && n.End() <= a.length;
               ids = ids && n.id == (uint32_t)i + 1;
            }
            check(inRange, tag + " register");
            check(inKey, tag + " in key");
            check(fits, tag + " fits the take");
            check(ids, tag + " ids");
            check(!overlaps(a.notes), tag + " overlap on one pitch");
            if (spec.type == Type::Drums)
            {
               bool gm = true;
               for (const Note& n : a.notes)
                  gm = gm && (n.pitch == 36 || n.pitch == 38 || n.pitch == 42 || n.pitch == 46 || n.pitch == 39 ||
                              n.pitch == 45 || n.pitch == 50 || n.pitch == 56);
               check(gm, tag + " GM drum map");
            }
            if (spec.type == Type::Melody)
            {
               melodyJudged++;
               if (a.criticScore >= 0.0f)
                  melodyPassed++;
            }
            if (seed == 1)
               check(sameNotes(a.notes, Generate(spec).notes), tag + " same seed, same notes");
         }
   const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   printf("NOTEGENTEST %d takes in %.0f ms (%.1f ms each), %d empty", takes, ms, ms / std::max(1, takes), empty);
   for (const auto& [type, n] : emptyBy)
      printf(" %s:%d", type.c_str(), n);
   printf("; melody critic passed %d/%d\n", melodyPassed, melodyJudged);
   // A type may come out empty for a card that has no such part; no more than one take in ten.
   check(empty * 10 <= takes, "empty takes under 10%");

   // 2. Chords and pads hold several notes at once.
   {
      // Most notes sounding at once (a Humanizer spreads a chord's starts).
      auto maxStack = [](const std::vector<Note>& notes) {
         int most = 0;
         for (const Note& n : notes)
         {
            int sounding = 0;
            for (const Note& o : notes)
               sounding += o.start <= n.start && o.End() > n.start ? 1 : 0;
            most = std::max(most, sounding);
         }
         return most;
      };
      GenSpec s;
      s.style = "deep_house";
      s.key = key;
      s.scale = scale;
      s.type = Type::Chords;
      check(maxStack(Generate(s).notes) >= 2, "chords stack");
      s.type = Type::Pads;
      check(maxStack(Generate(s).notes) >= 2, "pads stack");
   }

   // 3. Form: AABA is four times the bars, and its A sections repeat.
   {
      GenSpec s;
      s.type = Type::Bass;
      s.style = "deep_house";
      s.bars = 2;
      s.form = Form::AABA;
      const Take t = Generate(s);
      check(t.length == Arrange::kTicksPerBar * 8, "form length");
      const Tick sec = Arrange::kTicksPerBar * 2;
      std::vector<std::pair<Tick, int>> a0, a1, a3;
      for (const Note& n : t.notes)
      {
         const int which = (int)(n.start / sec);
         auto rel = std::make_pair(n.start % sec, (int)n.pitch);
         if (which == 0) a0.push_back(rel);
         if (which == 1) a1.push_back(rel);
         if (which == 3) a3.push_back(rel);
      }
      check(!a0.empty() && a0 == a1 && a0 == a3, "form AABA repeats A");
   }

   // 4. Locks and range.
   {
      GenSpec s;
      s.type = Type::Melody;
      s.style = "deep_house";
      s.key = key;
      s.scale = scale;
      const Take src = Generate(s);
      GenSpec r = s;
      r.seed = 9;
      r.source = src.notes;
      r.lockRhythm = true;
      const Take lr = Generate(r);
      bool rhythm = lr.notes.size() <= src.notes.size() && !lr.notes.empty();
      std::set<std::pair<Tick, Tick>> srcRhythm;
      for (const Note& n : src.notes)
         srcRhythm.insert({ n.start, n.length });
      for (const Note& n : lr.notes)
         rhythm = rhythm && srcRhythm.count({ n.start, n.length }) > 0;
      check(rhythm, "lock rhythm keeps the source's starts and lengths");

      GenSpec g = s;
      g.rangeLo = 60;
      g.rangeHi = 72;
      bool inside = true;
      for (const Note& n : Generate(g).notes)
         inside = inside && n.pitch >= 60 && n.pitch <= 72;
      check(inside, "range folds into 60..72");
   }

   // 5. The settings line round-trips.
   {
      GenSpec s;
      s.type = Type::Arp;
      s.style = "melodic_techno";
      s.seed = 42;
      s.bars = 8;
      s.form = Form::AAAB;
      s.key = 5;
      s.scale = 0;
      s.creativity = 0.6f;
      s.swing = 0.25f;
      s.arpMode = 2;
      s.lockPitch = true;
      GenSpec back;
      check(Parse(Serialize(s) + " future=7", back), "parse");
      check(Serialize(back) == Serialize(s), "settings line round trip: " + Serialize(back));
   }

   printf("NOTEGENTEST %s\n", ok ? "ALL OK" : "FAILED");
   return ok;
}
} // namespace NoteGen
