// Music style cards compiled into a patch. See SampleStyles.h; the card format
// is documented in tools/catalog/styles_music.json's _doc.
#include "app/graph/SampleStyles.h"

#include "app/graph/UserStyle.h"
#include "audio/MusicTime.h"
#include "nodes/DrumSequencerNode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace
{
   // Compile's picks list while it runs (Compile is reentrant per thread, never nested).
   thread_local std::vector<std::string>* gPicks = nullptr;
   void NotePick(std::string what)
   {
      if (gPicks)
         gPicks->push_back(std::move(what));
   }
   struct PickScope
   {
      explicit PickScope(std::vector<std::string>& v) { gPicks = &v; }
      ~PickScope() { gPicks = nullptr; }
   };

   struct GenStyle
   {
      const char* id;
      const char* name;
      const char* family;
      int bpmLo, bpmHi;
      float swing;
      const char* scales;
      const char* drums[8];
      const char* bass;
      const char* fx;
      const char* synth;
      const char* ban;
      float mod;
      bool sidechain;
      const char* notefx;
      float groove, fill, gain, trim;
      bool hasChords;
      const char* chordSizes;
      float chordBeats;
      int chordSteps;
      float chordStrum, chordHuman;
      bool hasMotif;
      const char* motifSynth;
      float motifDensity;
      int motifOctave;
      float motifGate, motifGain;
      int motifRange;
      float motifRate;
      const char* motifNotefx;
   };

   struct GenType
   {
      const char* name;
      const char* category;
      int noteSlot;           // -1 = no note pin
      const char* audioSlots; // comma list
   };

   struct GenRow
   {
      const char* type;
      const char* key;
      char tag;     // f, i, b
      float lo, hi;
      int options;  // > 0: an enum of this many entries
      bool moves;   // a modulatable timbre param a style's LFOs may sweep
   };

#include "app/graph/GenerateSamplesTables.inc"
#include "app/graph/GenerateSamplesCritic.inc"
#include "app/graph/GenerateSamplesGrooves.inc"
#include "app/graph/GenerateSamplesGrooves909.inc"
#include "app/graph/GenerateSamplesVoices.inc"
#include "app/graph/GenerateSamplesNotes.inc"

   const char* kKitFiles[8] = { "01-kick", "02-snare", "03-closed-hat", "04-open-hat", "05-clap", "06-low-tom", "07-high-tom", "08-bell" };
   // styled.py's ban-free families that always get a chord layer.
   const char* kChordFamilies[] = { "ambient", "house", "trance", "downtempo", "retro", "techno", "experimental", "bass_music", "piano" };

   std::vector<std::string> Split(const char* list)
   {
      std::vector<std::string> out;
      std::string cur;
      for (const char* p = list; *p; p++)
      {
         if (*p == ',')
         {
            out.push_back(cur);
            cur.clear();
         }
         else
            cur += *p;
      }
      if (!cur.empty())
         out.push_back(cur);
      return out;
   }

   // splitmix64 over an FNV-1a hash of the seed string: one stream per
   // (style, seed), the same keying styled.py's random.Random("id:seed") uses.
   struct Rng
   {
      uint64_t s;
      explicit Rng(const std::string& key)
      {
         s = 1469598103934665603ull;
         for (unsigned char c : key)
            s = (s ^ c) * 1099511628211ull;
      }
      uint64_t Next()
      {
         uint64_t z = (s += 0x9E3779B97F4A7C15ull);
         z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
         z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
         return z ^ (z >> 31);
      }
      double Uniform() { return (double)(Next() >> 11) * (1.0 / 9007199254740992.0); }
      double Uniform(double lo, double hi) { return lo + (hi - lo) * Uniform(); }
      int Range(int n) { return n > 0 ? (int)(Next() % (uint64_t)n) : 0; }
      int Int(int lo, int hi) { return lo + Range(hi - lo + 1); }
      template <typename T> const T& Choice(const std::vector<T>& v) { return v[(size_t)Range((int)v.size())]; }
   };

   const GenType* TypeOf(const std::string& name)
   {
      for (const GenType& t : kGenTypes)
         if (name == t.name)
            return &t;
      return nullptr;
   }

   std::string Num(double v)
   {
      char b[32];
      snprintf(b, sizeof(b), "%.6g", v);
      return b;
   }

   using Params = std::map<std::string, std::pair<char, std::string>>; // key -> (tag, text)

   // A patch under construction: scene.Graph's add/connect, writing .inf lines.
   struct Graph
   {
      std::vector<std::string> lines { "infinite-patch 1", "rev 2" };
      std::map<int, std::string> types;
      std::set<std::pair<int, int>> used; // (dst, slot) already wired
      int n = 0;
      float variation = 0.5f;
      float tone = 0.0f, energy = 0.0f, space = 0.0f, mood = 0.0f; // the recipe's dots
      // Set by a builder around the Add of a voice that plays above the bass
      // (keys, leads, chords, plucks, pads): mood and energy move its octave.
      bool melodic = false;

      // Mood and energy are felt through every part of a sound, not only its
      // notes (music-emotion studies, e.g. Juslin and Laukka 2003; Eerola 2013):
      //   happy  (bright, driving): major, high register, bright, quick even strikes, dry
      //   tense  (dark, driving):   harmonic minor, sharp attacks, resonant and rough, dry
      //   sad    (dark, calm):      natural minor, low register, dark, soft and slow, long tail
      //   calm   (bright, calm):    major with sevenths, soft swells, slow, wide and wet
      // The quadrant is only named past 0.35 on a dot; below that nothing changes its kind.
      enum Quad { kNoQuad, kHappy, kTense, kSad, kCalm };
      Quad Quadrant() const
      {
         if (mood > 0.35f)
            return energy < -0.2f ? kCalm : kHappy;
         if (mood < -0.35f)
            return energy > 0.2f ? kTense : kSad;
         return kNoQuad;
      }
      // Reverb and tail: calm and sad music sits in more room, driving music closer.
      double Room() const { return std::clamp((double)space - 0.5 * energy + 0.2 * std::max(0.0f, -mood) * std::max(0.0f, -energy), -1.0, 1.0); }
      double Tension() const { return (double)std::max(0.0f, -mood) * std::max(0.0f, energy); }

      // Chords: a mood picks a progression that says it (in the scale mood chose), energy
      // picks how often the chord is struck and how often it changes.
      void ShapeChords(Params& ps, int seed) const
      {
         if (std::abs(mood) <= 0.35f && std::abs(energy) <= 0.35f)
            return;
         auto num = [&](const char* k, double d) { auto it = ps.find(k); return it == ps.end() ? d : std::stod(it->second.second); };
         const double r0 = num("rateBeats", 1.0);
         double strike = r0, change = std::max(r0, 4.0);
         if (energy > 0.35f)
            strike = std::min(r0, energy > 0.75f ? 0.5 : 1.0), change = 2.0;
         else if (energy < -0.35f)
            strike = std::max(r0, 4.0), change = strike;
         ps["rateBeats"] = { 'f', Num(strike) };
         ps["strumMs"] = { 'f', Num(std::clamp(num("strumMs", 15.0) * std::pow(2.0, -1.2 * energy), 0.0, 120.0)) };
         const Quad q = Quadrant();
         if (q == kNoQuad)
            return;
         // Scale degrees, 0-based; types: 0 the chord knob, 1 seventh, 2 sus2, 3 sus4.
         struct Prog { int n; int deg[4]; int type[4]; };
         static const Prog kHappyP[] = { { 4, { 0, 4, 5, 3 }, {} }, { 4, { 0, 3, 4, 0 }, {} }, { 4, { 0, 5, 3, 4 }, {} }, { 4, { 3, 0, 4, 4 }, { 0, 0, 3, 0 } } };
         static const Prog kTenseP[] = { { 4, { 0, 3, 4, 0 }, {} }, { 4, { 0, 5, 6, 4 }, {} }, { 4, { 0, 1, 4, 4 }, { 0, 0, 3, 0 } }, { 4, { 0, 0, 5, 4 }, {} } };
         // Sad stays on the minor chords (i, iv, v of natural minor): VI, III and VII are major and lift it.
         static const Prog kSadP[] = { { 4, { 0, 3, 0, 4 }, {} }, { 4, { 0, 4, 3, 0 }, {} }, { 4, { 0, 3, 5, 4 }, {} }, { 4, { 0, 0, 3, 3 }, {} } };
         static const Prog kCalmP[] = { { 4, { 0, 3, 0, 3 }, { 1, 1, 1, 1 } }, { 4, { 0, 5, 3, 0 }, { 1, 1, 1, 2 } }, { 4, { 3, 0, 1, 0 }, { 1, 1, 1, 2 } }, { 4, { 0, 2, 3, 3 }, { 1, 1, 1, 2 } } };
         const Prog* list = q == kHappy ? kHappyP : q == kTense ? kTenseP : q == kSad ? kSadP : kCalmP;
         Rng r("affect-chords:" + std::to_string(seed));
         const Prog& p = list[r.Range(4)];
         const int per = std::clamp((int)std::lround(change / strike), 1, 8);
         const int chords = std::max(1, std::min(p.n, 8 / per));
         ps["mode"] = { 'i', "1" };
         ps["progSteps"] = { 'i', std::to_string(chords * per) };
         for (int c = 0; c < 8; c++)
         {
            const int i = std::min(c / per, chords - 1);
            ps["progDeg" + std::to_string(c)] = { 'i', std::to_string(c < chords * per ? p.deg[i] : 0) };
            ps["progType" + std::to_string(c)] = { 'i', std::to_string(c < chords * per ? p.type[i] : 0) };
         }
         if (q == kCalm)
            ps["chordSize"] = { 'i', "4" };
         else if (q == kTense || q == kHappy)
            ps["chordSize"] = { 'i', "3" };
         ps["upperHarmonics"] = { 'f', Num(q == kHappy ? 35.0 : q == kSad ? 5.0 : 15.0) };
      }

      // The dots, applied to one node's params as it is placed. A key the
      // seed left at its default starts from the node's own default, so a dot
      // moves every voice, not only the ones the seed touched.
      void Shape(const std::string& type, Params& ps, int seed) const
      {
         if (type == "Chorder")
            ShapeChords(ps, seed);
         if (type == "Arpeggiator" && std::abs(energy) > 0.35f)
         {
            auto it = ps.find("rateBeats");
            if (it != ps.end())
            {
               const double rb = std::stod(it->second.second);
               it->second.second = Num(energy > 0.0f ? std::min(rb, 0.25) : std::min(rb * 2.0, 1.0));
            }
         }
         auto scale = [&](const char* key, double fallback, double mul, double lo, double hi, bool fill)
         {
            auto it = ps.find(key);
            if (it == ps.end() && !fill)
               return;
            const double v = it == ps.end() ? fallback : std::stod(it->second.second);
            ps[key] = { 'f', Num(std::clamp(v * mul, lo, hi)) };
         };
         const bool synth = type == "Analog" || type == "Wavetable" || type == "Metallic" || type == "Oscillator" || type == "Spectral Synth" ||
                            type == "Field Synth" || type == "Partials";
         // Tense is not dark: anger and fear are bright and harsh, so tension opens the filter back up.
         // The bass takes half of it: darkened as much as the voices above it, it turns to sub and muddies the mix.
         const double feel = 0.6 * energy + 0.45 * mood + 0.9 * Tension();
         const double bright = std::pow(2.0, 1.2 * tone + (melodic ? feel : 0.5 * feel));
         if (synth && bright != 1.0)
         {
            for (const char* k : { "cutoff", "a.cutoff", "filterCutoff" })
               scale(k, 12000.0, bright, 80.0, 18000.0, (type == "Analog" && !strcmp(k, "cutoff")) || (type == "Wavetable" && !strcmp(k, "a.cutoff")) ||
                                                            (type == "Metallic" && !strcmp(k, "filterCutoff")));
            scale("b.cutoff", 12000.0, bright, 80.0, 18000.0, false);
            scale("p.cutoff", 6000.0, bright, 200.0, 10000.0, false);
         }
         // Energy shapes how long the voices above the bass hold; a bass held longer only rings as sub.
         const double tail = std::pow(2.0, 1.5 * space - (melodic ? 0.6 * energy : 0.0));
         if (synth && energy != 0.0f)
         {
            // Calm swells in and holds its notes; driving strikes hard and lets go.
            const double soft = 60.0 * std::max(0.0f, -energy);
            auto attack = [&](const char* key)
            {
               auto it = ps.find(key);
               if (it == ps.end() || it->second.first != 'f')
                  return;
               const double v = std::stod(it->second.second);
               it->second.second = Num(std::clamp(energy > 0.0f ? v * (1.0 - 0.6 * energy) : v + soft, 0.5, 8000.0));
            };
            for (const char* k : { "attack", "a.ampAttack", "ampAttack" })
               attack(k);
            const double hold = melodic ? std::pow(2.0, -0.8 * energy) : 1.0;
            for (const char* k : { "decay", "a.ampDecay", "ampDecay" })
               scale(k, 0.0, hold, 20.0, 12000.0, false);
         }
         if (synth && melodic && (mood != 0.0f || energy != 0.0f))
         {
            // Register: happy sits higher, sad lower.
            const int lift = std::clamp((int)std::lround(0.55 * mood + 0.3 * energy), -1, 1);
            auto it = ps.find("a.octave");
            if (type == "Wavetable" && lift != 0)
            {
               const int o = it == ps.end() ? 0 : std::stoi(it->second.second);
               ps["a.octave"] = { 'i', std::to_string(std::clamp(o + lift, -2, 2)) };
            }
         }
         if (synth && Tension() > 0.0)
         {
            // Tense: a resonant edge and beating voices, the roughness of anger and fear.
            const double t = Tension();
            for (const char* k : { "a.resonance", "resonance" })
               if (ps.count(k) || (type == "Wavetable" && !strcmp(k, "a.resonance")))
                  ps[k] = { 'f', Num(std::clamp((ps.count(k) ? std::stod(ps[k].second) : 0.0) + 0.45 * t, 0.0, 0.85)) };
            if (type == "Wavetable")
            {
               const double d = ps.count("a.detune") ? std::stod(ps["a.detune"].second) : 0.0;
               ps["a.detune"] = { 'f', Num(std::clamp(d + 14.0 * t, 0.0, std::max(40.0, d))) };
            }
         }
         if (synth && tail != 1.0)
         {
            scale("release", 250.0, tail, 5.0, 8000.0, type == "Analog" || type == "Partials");
            scale("a.ampRelease", 260.0, tail, 5.0, 8000.0, type == "Wavetable");
            scale("b.ampRelease", 260.0, tail, 5.0, 8000.0, false);
            scale("ampRelease", 250.0, tail, 5.0, 8000.0, type == "Spectral Synth");
         }
         auto add = [&](const char* key, double by, double lo, double hi)
         {
            auto it = ps.find(key);
            if (it != ps.end() && it->second.first == 'f')
               it->second.second = Num(std::clamp(std::stod(it->second.second) + by, lo, hi));
         };
         const double room = Room();
         if (type == "Reverb" && room != 0.0)
         {
            add("mix", 0.25 * room, 0.0, 0.9);
            scale("decay", 1.2, std::pow(2.0, room), 0.2, 12.0, false);
         }
         // A tempo delay, not the few-ms bursts that build a clap.
         auto tm = ps.find("timeMs");
         if (type == "Delay" && room != 0.0 && tm != ps.end() && std::stod(tm->second.second) > 60.0)
            add("mix", 0.15 * room, 0.0, 0.8);
         if ((type == "Note Sequencer" || type == "Drum Sequencer") && energy != 0.0f)
            for (auto& [k, v] : ps)
               if (k.rfind("vel", 0) == 0 && k.size() > 3 && isdigit((unsigned char)k[3]) && v.first == 'f' && std::stod(v.second) > 0.0)
                  v.second = Num(std::clamp(std::stod(v.second) * (1.0 + 0.3 * energy), 0.15, 1.0));
      }

      // Seeded params (universe.py seeded_params): each ui row leaves its
      // default with chance 0.7 * variation, to a uniform (log for wide
      // positive ranges) value. File keys get a bundled kit sample.
      Params Seeded(const std::string& type, int seed)
      {
         Rng r(type + ":" + std::to_string(seed));
         Params out;
         static const std::map<std::string, std::vector<std::string>> kFileKeys = {
            { "Sampler", { "path" } }, { "Granular", { "filePath" } }, { "Partials", { "path", "pathB" } } };
         auto files = kFileKeys.find(type);
         if (files != kFileKeys.end())
            for (const std::string& k : files->second)
               out[k] = { 's', DrumSequencerNode::KitDir() + "/" + kKitFiles[r.Range(8)] + ".wav" };
         if (seed == 0)
            return out;
         const double pSet = 0.7 * std::clamp(variation, 0.0f, 1.0f);
         for (const GenRow& row : kGenRows)
         {
            if (type != row.type || r.Uniform() > pSet)
               continue;
            if (row.tag == 'b')
               out[row.key] = { 'b', std::to_string(r.Range(2)) };
            else if (row.options > 0)
               out[row.key] = { 'i', std::to_string(r.Range(row.options)) };
            else if (row.hi > row.lo)
            {
               if (row.tag == 'i')
                  out[row.key] = { 'i', std::to_string(r.Int((int)row.lo, (int)row.hi)) };
               else if (row.lo > 0.0f && row.hi / row.lo > 50.0f)
                  out[row.key] = { 'f', Num(std::exp(r.Uniform(std::log(row.lo), std::log(row.hi)))) };
               else
                  out[row.key] = { 'f', Num(r.Uniform(row.lo, row.hi)) };
            }
         }
         // Parameter Critic: clamp against learned safe bounds [p05, p95] & enforce 2D audibility invariants.
         for (const SampleCriticData::SafeParam& sp : SampleCriticData::kSafeParams)
            if (type == sp.type)
            {
               auto it = out.find(sp.key);
               if (it != out.end() && it->second.first == 'f')
               {
                  const double v = std::stod(it->second.second);
                  const double span = std::max(0.0, (double)sp.p95 - (double)sp.p05);
                  const double lo = (double)sp.p05 - 0.15 * span;
                  const double hi = (double)sp.p95 + 0.15 * span;
                  if (v < lo || v > hi)
                     it->second.second = Num(std::clamp(v, lo, hi));
               }
            }
         for (const char* vk : { "volume", "a.volume" })
         {
            auto it = out.find(vk);
            if (it != out.end() && it->second.first == 'f' && std::stod(it->second.second) < 0.25)
               it->second.second = "0.65";
         }
         for (const char* ck : { "cutoff", "a.cutoff", "filterCutoff" })
         {
            auto it = out.find(ck);
            if (it != out.end() && it->second.first == 'f' && std::stod(it->second.second) < 220.0)
               it->second.second = "420";
         }
         if (type == "Wavetable")
         {
            out["a.on"] = { 'b', "1" };
            auto mx = out.find("mix"), bOn = out.find("b.on");
            if (mx != out.end() && (bOn == out.end() || bOn->second.second == "0") && std::stod(mx->second.second) > 0.65)
               mx->second.second = "0.45";
         }
         return out;
      }

      int Add(const std::string& type, int seed, const Params& force = {})
      {
         const GenType* t = TypeOf(type);
         n++;
         lines.push_back("node " + std::to_string(n) + " " + (t ? t->category : "Synths") + " " + type);
         Params ps = Seeded(type, seed);
         for (const auto& kv : force)
            ps[kv.first] = kv.second;
         Shape(type, ps, seed);
         for (const auto& [k, v] : ps)
            lines.push_back(std::string("  ") + v.first + " " + k + " " + v.second);
         lines.push_back("end");
         types[n] = type;
         return n;
      }

      // First free pin of the kind; false when the node has none left.
      bool Connect(int src, int dst, bool note)
      {
         const GenType* t = TypeOf(types[dst]);
         if (t == nullptr)
            return false;
         std::vector<int> slots;
         if (note)
         {
            if (t->noteSlot >= 0)
               slots.push_back(t->noteSlot);
         }
         else
            for (const std::string& s : Split(t->audioSlots))
               slots.push_back(std::stoi(s));
         for (int s : slots)
            if (!used.count({ dst, s }))
            {
               used.insert({ dst, s });
               lines.push_back(std::string(note ? "note " : "aud ") + std::to_string(dst) + " " + std::to_string(s) + " " + std::to_string(src));
               return true;
            }
         return false;
      }

      std::string Text() const
      {
         std::string out;
         for (const std::string& l : lines)
            out += l + "\n";
         return out;
      }
   };

   Params F(std::initializer_list<std::pair<const char*, double>> fs)
   {
      Params p;
      for (const auto& [k, v] : fs)
         p[k] = { 'f', Num(v) };
      return p;
   }

   Params NoteFx(const std::string& type)
   {
      if (type == "Humanizer") return F({ { "timingMs", 14.0 }, { "velocityPct", 18.0 } });
      if (type == "Note Echo") { Params p = F({ { "decay", 55.0 }, { "rateBeats", 0.75 } }); p["repeats"] = { 'i', "3" }; return p; }
      if (type == "Velocity Curve") return F({ { "curve", 1.6 } });
      if (type == "Note Strum") return F({ { "strumMs", 45.0 } });
      return {};
   }

   // Python's int(c, 36): a pattern digit is a scale degree, '.' a rest.
   int Degree(char c)
   {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'z') return c - 'a' + 10;
      if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
      return 0;
   }

   // ---- Compositional Proposer & 3-Stage Trained Critic ----------------------------------------

   uint16_t MaskOfLane(const std::string& s)
   {
      uint16_t m = 0;
      for (int i = 0; i < 16 && i < (int)s.size(); i++)
         if (s[(size_t)i] != '.')
            m |= (uint16_t)(1u << i);
      return m;
   }

   std::string LaneOfMask(uint16_t m, uint16_t ghostMask = 0)
   {
      if (m == 0)
         return "";
      std::string out(16, '.');
      for (int i = 0; i < 16; i++)
         if (m & (1u << i))
            out[(size_t)i] = (ghostMask & (1u << i)) ? 'o' : 'x';
      return out;
   }

   uint16_t EuclideanMask(int k, int rot)
   {
      if (k <= 0) return 0;
      if (k >= 16) return 0xffffu;
      uint16_t m = 0;
      for (int i = 0; i < 16; i++)
         if (((i * k) % 16) < k)
            m |= (uint16_t)(1u << ((i + rot) & 15));
      return m;
   }

   int PopCount16(uint16_t m)
   {
      int c = 0;
      for (; m; m &= (uint16_t)(m - 1))
         c++;
      return c;
   }

   int MaxRun16(uint16_t m)
   {
      int best = 0, cur = 0;
      for (int i = 0; i < 16; i++)
      {
         cur = (m & (1u << i)) ? (cur + 1) : 0;
         best = std::max(best, cur);
      }
      return best;
   }

   float BerlyneSweetSpot(float ll, float lo, float mid, float hi)
   {
      if (ll <= mid)
         return std::clamp(0.5f + 0.5f * (ll - lo) / std::max(1e-4f, mid - lo), 0.2f, 1.0f);
      return std::clamp(1.0f - 0.7f * (ll - mid) / std::max(1e-4f, hi - mid), 0.15f, 1.0f);
   }

   bool CriticJudgeDrum(const uint16_t m[8], const uint16_t cardM[8], bool ambient, float& outScore, float& outSync)
   {
      int activeLanes = 0, totalHits = 0;
      for (int l = 0; l < 8; l++)
      {
         const int h = PopCount16(m[l]);
         if (h > 0)
         {
            activeLanes++;
            totalHits += h;
         }
         if (l != 2 && l != 3)
         {
            if (h > 12 || MaxRun16(m[l]) > 8)
               return false; // stuck / machine-gun lane
         }
      }
      if (totalHits == 0)
         return false;
      const float dens = (float)totalHits / (float)std::max(48, activeLanes * 16);
      if (dens > 0.60f || (float)totalHits / (float)(activeLanes * 16) < 0.05f)
         return false;
      const int kh = PopCount16(m[0]), sh = PopCount16(m[1]);
      if (!ambient && kh == 0)
         return false;
      if (!ambient && kh >= 4 && (m[0] & 0x5555u) == 0)
         return false; // wrong meter: no kick on any 8th-note pulse
      if (kh >= 5 && sh >= 5 && m[0] == m[1])
         return false; // 100% kick-snare clash

      // Plausibility log-likelihood under corpus step model
      float ll = 0.0f;
      for (int l = 0; l < 8; l++)
         for (int s = 0; s < 16; s++)
         {
            const float p = std::clamp(SampleCriticData::kDrumStepP[l][s], 1e-4f, 1.0f - 1e-4f);
            const bool hit = (m[l] & (1u << s)) != 0;
            ll += std::log2(hit ? p : (1.0f - p));
         }
      ll /= 128.0f;
      if (ll < SampleCriticData::kDrumFloor)
         return false;

      // Syncopation & card-escape novelty
      static const int kMetW[16] = { 4, 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1 };
      float sync = 0.0f, syncTot = 0.0f;
      for (int l : { 0, 1, 4, 5, 6 })
         for (int s = 0; s < 16; s++)
            if (m[l] & (1u << s))
            {
               syncTot += 1.0f;
               if (kMetW[s] <= 2 && (m[l] & (1u << ((s + 1) & 15))) == 0)
                  sync += (float)(3 - kMetW[s]) * 0.5f;
            }
      const float anchor = ((m[0] & 1u) || (m[0] & (1u << 8))) ? 1.0f : 0.4f;
      outSync = std::min(1.0f, (sync / std::max(1.0f, syncTot)) * anchor);
      int diff = 0;
      for (int l = 0; l < 8; l++)
         diff += PopCount16(m[l] ^ cardM[l]);
      const float cdist = std::min(1.0f, (float)diff / 18.0f);
      const float surp = BerlyneSweetSpot(ll, SampleCriticData::kDrumFloor, SampleCriticData::kDrumMidLl, SampleCriticData::kDrumHiLl);
      outScore = 0.38f * cdist + 0.35f * outSync + 0.27f * surp;
      return true;
   }

   const SampleCriticData::StyleDirective& StyleDirectiveFor(const char* styleId)
   {
      if (styleId != nullptr)
         for (int i = 0; i < SampleCriticData::kStyleDirectiveCount; i++)
            if (std::strcmp(SampleCriticData::kStyleDirectives[i].id, styleId) == 0)
               return SampleCriticData::kStyleDirectives[i];
      return SampleCriticData::kStyleDirectives[0];
   }

   int PickDirectiveKind(Rng& r, const float w[6])
   {
      double sum = 0.0;
      for (int i = 0; i < 6; i++)
         sum += std::max(0.0f, w[i]);
      if (sum <= 1e-6)
         return r.Range(6);
      double u = r.Uniform() * sum;
      for (int i = 0; i < 6; i++)
      {
         u -= std::max(0.0f, w[i]);
         if (u <= 0.0)
            return i;
      }
      return 5;
   }

   void ProposeGroove(Rng& cr, const GenStyle& st, bool mine, const char* inLanes[8], std::string outLanes[8], float& outNov, float& outSync)
   {
      const SampleCriticData::StyleDirective& dir = StyleDirectiveFor(st.id);
      const float emoGroove = dir.pocket;     // groove pocket
      uint16_t baseM[8] = {}, baseG[8] = {};
      for (int l = 0; l < 8; l++)
      {
         const std::string s = inLanes[l] ? inLanes[l] : "";
         baseM[l] = MaskOfLane(s);
         for (int i = 0; i < 16 && i < (int)s.size(); i++)
            if (s[(size_t)i] == 'o')
               baseG[l] |= (uint16_t)(1u << i);
      }
      const bool ambient = std::string(st.family) == "ambient";
      uint16_t bestM[8];
      for (int l = 0; l < 8; l++) bestM[l] = baseM[l];
      float bestScore = -1.0f, bestSync = 0.2f;
      CriticJudgeDrum(baseM, baseM, ambient, bestScore, bestSync);

      for (int cand = 0; cand < 8; cand++)
      {
         uint16_t m[8];
         for (int l = 0; l < 8; l++) m[l] = baseM[l];
         // Hat density from real bars (groove-2h.md): the hat count is drawn from the counts of real unique bars, capped at
         // the card's count + 4, and its steps from the shipped step table, tripled where the card already hits.
         // Euclidean hat masks and cluster hats doubled the hat lane (B4 AUC 0.79).
         for (int lane = 2; lane <= 3; lane++)
         {
            if (lane == 3 && baseM[3] == 0 && cr.Uniform() >= 0.3)
               continue;
            int n = std::min((int)SampleCriticData::kHatCount[lane - 2][cr.Range(SampleCriticData::kHatCountN)],
                             PopCount16(baseM[lane]) + 4);
            if (n == 0 && m[lane])
               n = PopCount16(m[lane]);
            n = std::min(n, 14);
            m[lane] = 0;
            float w[16];
            for (int s = 0; s < 16; s++)
               w[s] = SampleCriticData::kDrumStepP[lane][s] * ((baseM[lane] >> s) & 1u ? 3.0f : 1.0f);
            for (int k = 0; k < n; k++)
            {
               float tot = 0.0f;
               for (int s = 0; s < 16; s++)
                  tot += w[s];
               if (tot <= 0.0f)
                  break;
               float u = (float)cr.Uniform() * tot;
               for (int s = 0; s < 16; s++)
               {
                  u -= w[s];
                  if (u <= 0.0f || s == 15)
                  {
                     if (w[s] > 0.0f)
                     {
                        m[lane] |= (uint16_t)(1u << s);
                        w[s] = 0.0f;
                     }
                     break;
                  }
               }
            }
         }
         if (!ambient && m[0] == 0)
            m[0] = 0x0101u;
         float sc = 0.0f, sy = 0.0f;
         if (CriticJudgeDrum(m, baseM, ambient, sc, sy))
         {
            sc += 0.10f * sy * emoGroove;
            if (mine)
            {
               float phi[UserStyle::kFeatureDim] = {};
               phi[0] = (float)PopCount16(m[0]) / 8.0f;
               phi[1] = (float)PopCount16(m[2]) / 16.0f;
               phi[2] = sy;
               phi[3] = st.swing * 3.0f;
               phi[4] = st.groove;
               phi[5] = sc;
               phi[15] = 1.0f;
               float tw = 0.0f;
               const float ts = UserStyle::ScoreFeatures(phi, cr.Uniform(), tw);
               sc += tw * (ts - 0.5f) * 1.6f;
            }
            if (sc > bestScore)
            {
               bestScore = sc;
               bestSync = sy;
               for (int l = 0; l < 8; l++) bestM[l] = m[l];
            }
         }
      }
      outNov = std::max(0.1f, bestScore);
      outSync = bestSync;
      for (int l = 0; l < 8; l++)
         outLanes[l] = LaneOfMask(bestM[l], baseG[l]);
   }

   bool CriticJudgeBass(const std::string& st, const std::string& cardBass, uint16_t kickMask, float& outScore)
   {
      if (st.size() != 16)
         return false;
      int notes = 0, rootFifth = 0, prevDeg = -1, maxJump = 0, offbeats = 0, kickLock = 0;
      float ll = 0.0f;
      for (int s = 0; s < 16; s++)
      {
         const char c = st[(size_t)s];
         int sym = 0;
         if (c != '.')
         {
            const int d = Degree(c);
            if (d < 0 || d > 9)
               return false;
            sym = d + 1;
            notes++;
            if (d == 0 || d == 4 || d == 7)
               rootFifth++;
            if (prevDeg >= 0)
               maxJump = std::max(maxJump, std::abs(d - prevDeg));
            prevDeg = d;
            if (s & 1)
               offbeats++;
            if (kickMask & (1u << s))
               kickLock++;
         }
         ll += SampleCriticData::kBassLogP[s & 3][sym];
      }
      if (notes < 2 || notes > 12 || maxJump > 7)
         return false;
      if ((float)rootFifth / (float)notes < 0.35f)
         return false;
      ll /= 16.0f;
      if (ll < SampleCriticData::kBassFloor)
         return false;
      int diff = 0;
      for (int s = 0; s < 16 && s < (int)cardBass.size(); s++)
         if (st[(size_t)s] != cardBass[(size_t)s])
            diff++;
      const float cdist = std::min(1.0f, (float)diff / 8.0f);
      const float pocket = std::min(1.0f, ((float)offbeats + 0.5f * (float)kickLock) / (float)std::max(1, notes));
      outScore = 0.45f * cdist + 0.55f * pocket;
      return true;
   }

   std::string ProposeBass(Rng& cr, bool mine, const std::string& cardBass, const std::string& kickLane)
   {
      if (cardBass.find_first_not_of('.') == std::string::npos)
         return cardBass;
      const uint16_t km = MaskOfLane(kickLane);
      std::string best = cardBass;
      float bestScore = -1.0f;
      CriticJudgeBass(cardBass, cardBass, km, bestScore);
      static const int kDegPool[8] = { 0, 0, 0, 2, 3, 4, 4, 6 };
      for (int cand = 0; cand < 8; cand++)
      {
         std::string s(16, '.');
         const int mode = cr.Range(3);
         int prev = 0, hits = 0;
         for (int i = 0; i < 16; i++)
         {
            const bool kOn = (km & (1u << i)) != 0;
            bool on = false;
            if (i == 0)
               on = true;
            else if (mode == 0) // kick-locked pocket + offbeat answer
               on = kOn || ((i == 6 || i == 10 || i == 14) && cr.Uniform() < 0.45);
            else if (mode == 1) // offbeat pump
               on = (i % 4 == 2) || (kOn && (i == 0 || i == 8)) || ((i == 7 || i == 15) && cr.Uniform() < 0.35);
            else // mutated card rhythm with new scale degree motion
               on = (cardBass[(size_t)i] != '.' && cr.Uniform() < 0.8) || (i % 2 == 1 && cr.Uniform() < 0.22);
            if (on)
            {
               int d = (i == 0 || (kOn && cr.Uniform() < 0.65)) ? 0 : kDegPool[cr.Range(8)];
               if (std::abs(d - prev) > 5)
                  d = 4;
               prev = d;
               s[(size_t)i] = (char)('0' + d);
               hits++;
            }
         }
         float sc = 0.0f;
         if (CriticJudgeBass(s, cardBass, km, sc))
         {
            if (mine)
            {
               float phi[UserStyle::kFeatureDim] = {};
               phi[9] = (float)hits / 16.0f;
               phi[15] = 1.0f;
               float tw = 0.0f;
               const float ts = UserStyle::ScoreFeatures(phi, cr.Uniform(), tw);
               sc += tw * (ts - 0.5f) * 1.4f;
            }
            if (sc > bestScore)
            {
               bestScore = sc;
               best = s;
            }
         }
      }
      return best;
   }

   int PrevIntervalBin(int iv, bool first)
   {
      if (first) return 6;
      if (iv <= -5) return 0;
      if (iv <= -3) return 1;
      if (iv <= -1) return 2;
      if (iv == 0)  return 3;
      if (iv <= 2)  return 4;
      if (iv <= 4)  return 5;
      return 6;
   }

   bool CriticJudgePitches(const std::vector<int>& pitches, int root, float& outScore);

   bool CriticJudgeMelody(const std::vector<std::pair<int, bool>>& seq, int root, int scaleId, int octave, float& outScore)
   {
      std::vector<int> pitches;
      for (int k = 0; k < (int)seq.size(); k++)
         if (seq[(size_t)k].second)
         {
            int n = MusicTime::DegreeToNote(seq[(size_t)k].first, octave + 1, root, scaleId);
            while (n < 28) n += 12;
            pitches.push_back(n);
         }
      return CriticJudgePitches(pitches, root, outScore);
   }

   bool CriticJudgePitches(const std::vector<int>& pitches, int root, float& outScore)
   {
      if (pitches.size() < 6)
         return false;
      int lo = pitches[0], hi = pitches[0], maxLeap = 0, extremeLeaps = 0;
      std::set<int> distinct, distinctIv;
      float ll = 0.0f;
      int prevIv = 0;
      for (size_t i = 0; i < pitches.size(); i++)
      {
         lo = std::min(lo, pitches[i]);
         hi = std::max(hi, pitches[i]);
         distinct.insert(pitches[i]);
         if (i + 1 < pitches.size())
         {
            const int iv = pitches[i + 1] - pitches[i];
            maxLeap = std::max(maxLeap, std::abs(iv));
            if (std::abs(iv) > 12 || std::abs(iv) == 6)
               extremeLeaps++;
            distinctIv.insert(std::clamp(iv, -12, 12));
            const int pb = PrevIntervalBin(prevIv, i == 0);
            const int rel = ((pitches[i] - root) % 12 + 12) % 12;
            const int idx = std::clamp(iv, -12, 12) + 12;
            ll += SampleCriticData::kMelodyLogP[pb][rel][idx];
            prevIv = iv;
         }
      }
      if (hi - lo > 36 || maxLeap > 24 || distinct.size() < 3)
         return false;
      const int nSteps = (int)pitches.size() - 1;
      if ((float)extremeLeaps / (float)std::max(1, nSteps) > 0.65f)
         return false;
      ll /= (float)std::max(1, nSteps);
      if (ll < SampleCriticData::kMelodyFloor)
         return false;
      const float surp = BerlyneSweetSpot(ll, SampleCriticData::kMelodyFloor, SampleCriticData::kMelodyMidLl, SampleCriticData::kMelodyHiLl);
      const float ivVar = std::min(1.0f, (float)distinctIv.size() / 6.0f);
      outScore = 0.55f * surp + 0.45f * ivVar;
      return true;
   }

   void ProposeMotif(Rng& cr, const GenStyle& st, bool mine, int root, int scaleId, std::vector<std::pair<int, bool>>& seq, float& outNov)
   {
      const SampleCriticData::StyleDirective& dir = StyleDirectiveFor(st.id);
      const float emoHappy = dir.affect[0];        // valence
      const float emoSad   = 1.0f - dir.affect[0]; // its opposite pole
      const float emoIntel = dir.complexity;
      std::vector<std::pair<int, bool>> best = seq;
      float bestScore = -1.0f;
      CriticJudgeMelody(seq, root, scaleId, st.motifOctave, bestScore);

      static const int kMoves[8] = { -3, -2, -1, -1, 0, 1, 1, 2 };
      for (int cand = 0; cand < 8; cand++)
      {
         std::vector<std::pair<int, bool>> cur;
         cur.reserve(16);
         int deg = cr.Range(4), active = 0;
         int arcType = cr.Range(4); // 0 arch, 1 wave, 2 climb, 3 cascade
         if (emoHappy > emoSad + 0.20f && cr.Uniform() < 0.45)
            arcType = 2; // rising uplift contour
         else if (emoSad > emoHappy + 0.20f && cr.Uniform() < 0.45)
            arcType = 3; // descending sighing cascade
         int prevDeg = deg;
         for (int k = 0; k < 16; k++)
         {
            if (k == 0)
               deg = cr.Range(3) * 2; // 0, 2, or 4
            else if (k == 7)
               deg = std::clamp(deg + (cr.Range(2) ? 1 : -1), -2, st.motifRange); // half-cadence tension
            else if (k == 15)
               deg = cr.Range(3) == 0 ? 4 : 0; // full-cadence resolution to root or fifth
            else
            {
               int move = kMoves[cr.Range(8)];
               if ((arcType == 0 && k < 8) || arcType == 2)
                  move = cr.Uniform() < (0.35 + 0.20 * emoHappy) ? std::abs(move) : move;
               else if ((arcType == 0 && k >= 8) || arcType == 3)
                  move = cr.Uniform() < (0.35 + 0.20 * emoSad) ? -std::abs(move) : move;
               if (std::abs(deg - prevDeg) >= 3 && cr.Uniform() < 0.58)
                  move = (deg > prevDeg) ? -1 : 1;
               prevDeg = deg;
               deg = std::clamp(deg + move, -2, st.motifRange);
            }
            const bool on = (k % 4 == 0) || (k == 7) || (k == 15) || (cr.Uniform() < st.motifDensity);
            if (on) active++;
            cur.push_back({ deg, on });
         }
         float sc = 0.0f;
         if (CriticJudgeMelody(cur, root, scaleId, st.motifOctave, sc))
         {
            sc += 0.08f * emoIntel * sc;
            if (mine)
            {
               float phi[UserStyle::kFeatureDim] = {};
               phi[6] = (float)active / 16.0f;
               phi[7] = (float)st.motifRange / 8.0f;
               phi[8] = sc;
               phi[15] = 1.0f;
               float tw = 0.0f;
               const float ts = UserStyle::ScoreFeatures(phi, cr.Uniform(), tw);
               sc += tw * (ts - 0.5f) * 1.5f;
            }
            if (sc > bestScore)
            {
               bestScore = sc;
               best = cur;
            }
         }
      }
      seq = best;
      outNov = std::max(0.2f, bestScore);
   }
}

namespace
{
   // The seeded draw leaves engine A off half the time and scatters the pitch
   // envelope and fine tune; a built voice sets all of them.
   void CleanWavetable(std::map<std::string, std::pair<char, std::string>>& k)
   {
      k["a.on"] = { 'b', "1" };
      k["a.volume"] = { 'f', "1" };
      k["a.fine"] = { 'f', "0" };
      k["pitchBend"] = { 'f', "0" };
      k["glide"] = { 'f', "0" };
      k["a.pitchAmount"] = { 'f', "0" };
      k["a.pitchAttack"] = { 'f', "0" };
      k["a.pitchSustain"] = { 'f', "0" };
      k["a.filterAttack"] = { 'f', "0" };
      k["a.warpAmount"] = { 'f', "0" };
      k["fmDepth"] = { 'f', "0" };
      k["b.on"] = { 'b', "0" };
   }
   // Designed voices (owner's review, 2026-10-29: "too dark, too distorted, too muddy"; leads "should sound
   // massive"). Every Wavetable key is pinned, so nothing is left to the seeded draw. Numbers follow Welsh's
   // Synthesizer Cookbook (lead, bass and pluck patches) and Szabo's supersaw: seven voices spread wide with
   // free-running phases, a second stack an octave up, a gentle low-pass that stays open.
   enum class Voice { Supersaw, SawStack, SquareLead, BrassStack, FmStack, SynthBass, ReeseBass, SynthPluck };

   // One engine: its table and frame, its unison stack, its register and level; pitch envelope off.
   void Engine(Params& k, char e, int table, double pos, int unison, double detune, int octave, double vol)
   {
      const std::string p = std::string(1, e) + ".";
      k[p + "on"] = { 'b', "1" };
      k[p + "table"] = { 'i', std::to_string(table) };
      k[p + "position"] = { 'f', Num(pos) };
      k[p + "unison"] = { 'i', std::to_string(unison) };
      k[p + "detune"] = { 'f', Num(std::round(detune * 10.0) / 10.0) };
      k[p + "stereoWidth"] = { 'f', unison > 1 ? "1" : "0" };
      k[p + "phaseRandomize"] = { 'f', unison > 1 ? "1" : "0" };
      k[p + "octave"] = { 'i', std::to_string(octave) };
      k[p + "semi"] = { 'i', "0" };
      k[p + "fine"] = { 'f', "0" };
      k[p + "phase"] = { 'f', "0" };
      k[p + "pan"] = { 'f', "0" };
      k[p + "volume"] = { 'f', Num(vol) };
      k[p + "warpMode"] = { 'i', "0" };
      k[p + "warpAmount"] = { 'f', "0" };
      k[p + "warpRatio"] = { 'f', "1" };
      for (const char* s : { "pitchAmount", "pitchAttack", "pitchDecay", "pitchSustain", "pitchRelease" })
         k[p + s] = { 'f', "0" };
   }
   // The engine's filter and its envelope: `amt` octaves of opening at the envelope's peak (ms, sustain 0-1).
   void EngineFilter(Params& k, char e, int type, double cut, double res, double amt, double a, double d, double s, double r)
   {
      const std::string p = std::string(1, e) + ".";
      k[p + "filterType"] = { 'i', std::to_string(type) };
      k[p + "cutoff"] = { 'f', Num(std::round(cut)) };
      k[p + "resonance"] = { 'f', Num(std::round(res * 100.0) / 100.0) };
      k[p + "filterAmount"] = { 'f', Num(std::round(amt * 100.0) / 100.0) };
      k[p + "filterAttack"] = { 'f', Num(std::round(a)) };
      k[p + "filterDecay"] = { 'f', Num(std::round(d)) };
      k[p + "filterSustain"] = { 'f', Num(std::round(s * 100.0) / 100.0) };
      k[p + "filterRelease"] = { 'f', Num(std::round(r)) };
   }
   void EngineAmp(Params& k, char e, double a, double d, double s, double r)
   {
      const std::string p = std::string(1, e) + ".";
      k[p + "ampAttack"] = { 'f', Num(std::round(a)) };
      k[p + "ampDecay"] = { 'f', Num(std::round(d)) };
      k[p + "ampSustain"] = { 'f', Num(std::round(s * 100.0) / 100.0) };
      k[p + "ampRelease"] = { 'f', Num(std::round(r)) };
   }

   // Basic Shapes frames: sine 0, triangle 1/3, saw 2/3, square 1.
   constexpr double kSine = 0.0, kSaw = 0.667, kSquare = 1.0;

   // A whole voice: both engines, the A/B balance (equal power), one filter and one amp shape on both.
   Params DesignVoice(Voice v, Rng& r)
   {
      NotePick("voice:designed:" + std::to_string((int)v));
      Params k;
      k["volume"] = { 'f', "0.9" };
      k["frequency"] = { 'f', "440" };
      k["glide"] = { 'f', "0" };
      k["pitchBend"] = { 'f', "0" };
      k["fmDepth"] = { 'f', "0" };
      k["fmMode"] = { 'i', "0" };
      double mix = 0.4, cut = 8000.0, res = 0.1, amt = 1.0, fa = 2.0, fd = 400.0, fs = 0.6, fr = 300.0;
      double aa = 3.0, ad = 300.0, as = 0.85, ar = r.Uniform(180.0, 320.0);
      int filter = 1; // LP12
      switch (v)
      {
      case Voice::Supersaw:
      {
         const bool multi = r.Uniform() < 0.5; // Basic Shapes' saw, or the Multi Saw table's frames
         Engine(k, 'a', multi ? 24 : 0, multi ? r.Uniform(0.3, 0.7) : kSaw, 7, r.Uniform(34.0, 48.0), 0, 1.0);
         Engine(k, 'b', 0, kSaw, 5, r.Uniform(18.0, 28.0), 1, 0.7);
         mix = r.Uniform(0.3, 0.42), cut = r.Uniform(6500.0, 10000.0), amt = 0.8;
         break;
      }
      case Voice::SawStack: // a wide saw stack over a square an octave down
         Engine(k, 'a', 0, kSaw, 5, r.Uniform(24.0, 34.0), 0, 1.0);
         Engine(k, 'b', 0, kSquare, 1, 0.0, -1, 0.8);
         mix = r.Uniform(0.35, 0.45), cut = r.Uniform(5000.0, 8000.0), amt = 1.2, fs = 0.5;
         break;
      case Voice::SquareLead: // a hollow square stack with a saw an octave up for edge
         Engine(k, 'a', 0, kSquare, 3, r.Uniform(12.0, 20.0), 0, 1.0);
         Engine(k, 'b', 0, kSaw, 3, r.Uniform(16.0, 24.0), 1, 0.6);
         filter = 2, mix = 0.35, cut = r.Uniform(4000.0, 6500.0), res = 0.15, amt = 1.5, fd = 250.0, fs = 0.45;
         break;
      case Voice::BrassStack: // brass frames, swelled in, a saw stack under it for width
         Engine(k, 'a', 53, r.Uniform(0.3, 0.7), 3, r.Uniform(10.0, 18.0), 0, 1.0);
         Engine(k, 'b', 0, kSaw, 5, r.Uniform(26.0, 34.0), 0, 0.7);
         mix = 0.45, cut = r.Uniform(3500.0, 6000.0), amt = 1.5, fa = 40.0, fd = 350.0, aa = 25.0;
         break;
      case Voice::FmStack: // a bell tone on top of a supersaw body
         Engine(k, 'a', 12, r.Uniform(0.2, 0.5), 2, 8.0, 0, 1.0);
         Engine(k, 'b', 0, kSaw, 7, r.Uniform(30.0, 40.0), 0, 0.7);
         mix = 0.5, cut = r.Uniform(8000.0, 11000.0), amt = 0.6;
         break;
      case Voice::SynthBass: // one saw or square over a sine at its root; the envelope gives the bite
         Engine(k, 'a', 0, r.Uniform() < 0.6 ? kSaw : kSquare, 1, 0.0, 0, 1.0);
         Engine(k, 'b', 0, kSine, 1, 0.0, 0, 0.9); // at the note: an octave under a bass note is under 40 Hz, sub no speaker gives back
         filter = 2, mix = 0.45, cut = r.Uniform(900.0, 1600.0), res = r.Uniform(0.1, 0.3), amt = 2.0, fa = 0.0;
         fd = r.Uniform(160.0, 280.0), fs = 0.25, fr = 80.0, aa = 1.0, ad = 300.0, as = 0.8, ar = 60.0;
         break;
      case Voice::ReeseBass: // two saws beating against each other over a clean sine
         Engine(k, 'a', 0, kSaw, 2, r.Uniform(10.0, 18.0), 0, 1.0);
         Engine(k, 'b', 0, kSine, 1, 0.0, 0, 0.9); // at the note: an octave under a bass note is under 40 Hz, sub no speaker gives back
         filter = 2, mix = 0.45, cut = r.Uniform(1200.0, 2200.0), res = 0.1, amt = 0.8, fa = 0.0, fd = 300.0, fs = 0.5, fr = 100.0;
         aa = 2.0, ad = 300.0, as = 0.9, ar = 80.0;
         break;
      case Voice::SynthPluck: // a short bright stab: the filter snaps shut
         Engine(k, 'a', 0, kSaw, 3, r.Uniform(10.0, 18.0), 0, 1.0);
         Engine(k, 'b', 0, kSquare, 1, 0.0, 1, 0.4);
         filter = 2, mix = 0.3, cut = r.Uniform(1400.0, 2400.0), res = 0.2, amt = 3.0, fa = 0.0;
         fd = r.Uniform(180.0, 320.0), fs = 0.1, fr = 200.0, aa = 1.0, ad = r.Uniform(350.0, 600.0), as = 0.25, ar = 250.0;
         break;
      }
      k["mix"] = { 'f', Num(std::round(mix * 100.0) / 100.0) };
      for (char e : { 'a', 'b' })
      {
         EngineFilter(k, e, filter, cut, res, amt, fa, fd, fs, fr);
         EngineAmp(k, e, aa, ad, as, ar);
      }
      return k;
   }
   // A voice from the Vital bank (owner, 2026-10-30: "data is gold and a slight variation can give many results"):
   // a real preset's oscillators, filter sweep and envelopes, drawn from the presets of the part's style and jittered.
   // The guardrails keep the review's complaints out: melodic voices open to at least 2.5 kHz at the sweep's peak,
   // nothing resonates past 0.6, releases stay short enough for a loop, and the louder engine plays the note itself
   // (the part's own register logic places it), the other keeping its interval to it.
   enum class BankRole { Bass, Lead, Synth, Pluck };
   Params BankVoice(BankRole role, Rng& r)
   {
      static const int kStyles[][2] = { { 0, 0 }, { 2, 2 }, { 1, 4 }, { 4, 1 } }; // Bass; Lead; Keys, Sequence; Sequence, Keys
      std::vector<const BankRow*> pool;
      // Diva rows (src 1) stay out for now: on the same 27 takes they made leads darker (centroid 689 -> 581 Hz, low
      // share 0.13 -> 0.20) and synths lower (low 0.58 -> 0.65). Their cutoffs are fitted to the renders' average.
      for (const BankRow& v : kBankVoices)
         if (v.src == 0 && (v.style == kStyles[(int)role][0] || v.style == kStyles[(int)role][1]))
            pool.push_back(&v);
      const BankRow& v = *pool[(size_t)r.Range((int)pool.size())];
      static const char* const kRoleNames[] = { "bass", "lead", "synth", "pluck" };
      NotePick(std::string("voice:vital:") + kRoleNames[(int)role] + ":" + std::to_string(&v - kBankVoices));
      const bool bass = role == BankRole::Bass, pluck = role == BankRole::Pluck;
      const BankOsc* o[2] = { &v.a, &v.b };
      if (!v.a.on || (v.b.on && v.b.level > v.a.level))
         std::swap(o[0], o[1]); // engine A is the louder one
      Params k;
      k["volume"] = { 'f', "0.9" };
      k["frequency"] = { 'f', "440" };
      k["glide"] = { 'f', "0" };
      k["pitchBend"] = { 'f', "0" };
      k["fmDepth"] = { 'f', "0" };
      k["fmMode"] = { 'i', "0" };
      const auto jit = [&](double x, double spread) { return x * std::exp2(r.Uniform(-spread, spread)); };
      for (int i = 0; i < 2; i++)
      {
         const char e = i ? 'b' : 'a';
         if (!o[i]->on)
         {
            Engine(k, e, 0, kSaw, 1, 0.0, 0, 0.0);
            k[std::string(1, e) + ".on"] = { 'b', "0" };
            continue;
         }
         // Semitones off engine A, folded into octaves the node takes (+-4) and a semi it takes (+-12).
         int rel = std::clamp(o[i]->semi - o[0]->semi, -12, 24); // nothing more than an octave under the note: that is the mud
         if (!bass && rel < 0)
            rel += 12; // a melodic line keeps its second engine over the note, not under it
         int oct = (int)std::floor((rel + 6) / 12.0);
         const int semi = rel - 12 * oct;
         if (!bass && o[i]->table == 0 && o[i]->pos < 0.4f)
            oct += 1; // a sine or triangle carries only its fundamental: an octave up, or the line measures as bass
         const double detune = std::min(50.0, jit(o[i]->detune, 0.25));
         Engine(k, e, o[i]->table, std::clamp(o[i]->pos + (o[i]->table ? r.Uniform(-0.08, 0.08) : 0.0), 0.0, 1.0),
                bass && i == 0 ? std::min(o[i]->unison, 4) : o[i]->unison, detune, oct, i ? std::clamp(o[1]->level / std::max(0.05f, o[0]->level), 0.2f, 1.0f) : 1.0);
         k[std::string(1, e) + ".semi"] = { 'i', std::to_string(semi) };
      }
      k["mix"] = { 'f', o[1]->on ? "0.45" : "0" };
      int type = v.filter;
      double cut = jit(v.cutoff, 0.3), amt = std::clamp((double)v.famt, -3.0, 5.0), res = std::min(0.6, (double)v.res);
      if (type >= 4 && type <= 6)
         cut = std::min(cut, bass ? 60.0 : 400.0); // a high-pass stays under the note
      else if (type == 7 || type == 8)
         type = 1, cut = std::max(cut, 1500.0); // a band-pass thins a loop to a phone: a low-pass at its centre instead
      if (type >= 1 && type <= 3)
      {
         const double floorPeak = bass ? 500.0 : 2500.0, roof = bass ? 3500.0 : 16000.0;
         if (cut * std::exp2(std::max(0.0, amt)) < floorPeak)
            cut = floorPeak / std::exp2(std::max(0.0, amt));
         if (!bass && cut * std::exp2(std::max(0.0, amt) * v.fs) < 1500.0)
            cut = 1500.0 / std::exp2(std::max(0.0, amt) * v.fs); // where the sweep settles stays open too
         cut = std::clamp(cut, bass ? 150.0 : 400.0, roof);
      }
      double fa = std::min(200.0, (double)v.fa), fd = jit(std::clamp((double)v.fd, 40.0, 2000.0), 0.3), fs = v.fs, fr = std::min(600.0, (double)v.fr);
      double aa = std::min(bass || pluck ? 5.0 : role == BankRole::Lead ? 60.0 : 30.0, (double)v.aa);
      double ad = jit(std::clamp((double)v.ad, 60.0, 3000.0), 0.25), as = v.as;
      double ar = std::min(bass ? 120.0 : pluck ? 350.0 : 600.0, (double)v.ar);
      if (pluck)
         as = std::min(as, 0.25), ad = std::clamp(ad, 200.0, 900.0);
      for (char e : { 'a', 'b' })
      {
         EngineFilter(k, e, type, cut, res, amt, fa, fd, fs, fr);
         EngineAmp(k, e, aa, ad, as, ar);
      }
      return k;
   }
   // The note bank (tools/universe/note_bank.py): progressions and one-bar lines the owner's note library plays, as
   // degrees of the family the scale belongs to (a degree is a different chord in the other family).
   bool MinorFamily(int scale)
   {
      using namespace MusicTime;
      return !(scale == kMajor || scale == kLydian || scale == kMixolydian || scale == kMajorPentatonic);
   }
   const int* BankProg(int scale, Rng& r)
   {
      std::vector<int> pool;
      for (int i = 0; i < (int)(sizeof(kNoteProgs) / sizeof(kNoteProgs[0])); i++)
         if (kNoteProgs[i].minor == (MinorFamily(scale) ? 1 : 0))
            pool.push_back(i);
      const int i = pool[(size_t)r.Range((int)pool.size())];
      NotePick("prog:" + std::to_string(i));
      return kNoteProgs[i].d;
   }
   // A bank line as 16 (degree, velocity 0..1) steps, velocity 0 a rest. The slight variation that makes one line many:
   // a note now and then steps to its neighbour degree, never the downbeat.
   std::vector<std::pair<int, double>> BankLine(int scale, bool bass, Rng& r)
   {
      std::vector<int> pool;
      for (int i = 0; i < (int)(sizeof(kNoteLines) / sizeof(kNoteLines[0])); i++)
         if (kNoteLines[i].minor == (MinorFamily(scale) ? 1 : 0) && kNoteLines[i].bass == (bass ? 1 : 0))
            pool.push_back(i);
      const int li = pool[(size_t)r.Range((int)pool.size())];
      NotePick("line:" + std::to_string(li));
      const NoteLine& l = kNoteLines[li];
      std::vector<std::pair<int, double>> out(16, { 0, 0.0 });
      for (int i = 0; i < 16; i++)
         if (l.d[i] != '.')
         {
            int d = l.d[i] - 'a';
            if (i > 0 && r.Uniform() < 0.1)
               d += r.Range(2) ? 1 : -1;
            out[(size_t)i] = { d, (l.v[i] - '0') / 9.0 };
         }
      return out;
   }
   // A pluck is a note that is struck and then dies: a bright start that
   // closes fast. Each kit is a Wavetable voice whose own filter envelope
   // (octaves of sweep from a dark cutoff) gives the snap, whose amp envelope
   // has no sustain, and whose table is an instrument that is plucked or
   // struck. Ranges are drawn per take, so no two takes of a kit match.
   struct PluckKit
   {
      const char* name;
      int tableA, tableB;      // two tables to draw between (same when one)
      double posLo, posHi;     // wavetable position
      int filter;              // 1 = lp 12, 2 = lp 24
      double cutLo, cutHi;     // resting cutoff, Hz
      double amtLo, amtHi;     // filter envelope, octaves above the cutoff on the strike
      double ampLo, ampHi;     // amp decay, ms
      double resLo, resHi;
      int unisonLo, unisonHi;
      double detuneLo, detuneHi;
      double pitchLo, pitchHi; // strike pitch chirp, semitones
      int octaveLo, octaveHi;
   };
   const PluckKit kPluckKits[] = {
      { "string", 50, 50, 0.15, 0.85, 2, 450, 1400, 2.5, 4.5, 260, 720, 0.0, 0.25, 1, 2, 4, 14, 0, 0, 0, 1 },
      { "harp", 50, 53, 0.1, 0.6, 1, 900, 2400, 1.5, 3.0, 500, 1100, 0.0, 0.15, 1, 1, 0, 0, 0, 0, 0, 1 },
      { "mallet", 16, 49, 0.0, 0.6, 1, 2500, 6000, 0.5, 1.5, 150, 480, 0.0, 0.1, 1, 1, 0, 0, 0, 2, 0, 1 },
      { "kalimba", 55, 55, 0.05, 0.7, 1, 3000, 7000, 0.0, 1.0, 300, 800, 0.0, 0.1, 1, 1, 0, 0, 0, 0, 0, 1 },
      { "tine", 17, 17, 0.0, 0.7, 1, 2000, 5000, 1.0, 2.0, 300, 800, 0.0, 0.1, 1, 2, 3, 9, 0, 0, 0, 0 },
      { "saw stab", 24, 0, 0.45, 0.8, 2, 380, 1000, 3.5, 5.5, 160, 420, 0.0, 0.2, 3, 5, 10, 26, 0, 0, -1, 0 },
      { "squelch", 4, 35, 0.2, 0.7, 2, 300, 800, 4.0, 6.0, 140, 360, 0.3, 0.6, 1, 2, 3, 12, 0, 0, -1, 0 },
      { "fm pluck", 12, 32, 0.1, 0.7, 1, 4000, 9000, 0.0, 1.0, 180, 520, 0.0, 0.1, 1, 2, 2, 8, 3, 7, 0, 1 },
      { "glass", 11, 60, 0.1, 0.8, 1, 3500, 8000, 0.5, 1.5, 250, 700, 0.0, 0.2, 1, 3, 4, 14, 0, 1, 0, 1 },
   };
   // A pad is a note that swells in, holds and leaves slowly: the attack is
   // hundreds of ms to seconds, the filter opens with it, and the release hangs
   // into the reverb. Unison and width make it a field of sound rather than a line.
   struct PadKit
   {
      const char* name;
      int tableA, tableB;
      double posLo, posHi;
      int filter;
      double cutLo, cutHi;
      double amtLo, amtHi;     // filter swell, octaves
      double atkLo, atkHi;     // amp attack, ms
      double relLo, relHi;     // amp release, ms
      int unisonLo, unisonHi;
      double detuneLo, detuneHi;
      int octaveLo, octaveHi;
   };
   const PadKit kPadKits[] = {
      { "warm saw", 24, 0, 0.4, 0.8, 1, 500, 1600, 1.0, 2.5, 500, 2200, 2000, 4500, 5, 7, 12, 30, -1, 0 },
      { "strings", 0, 24, 0.55, 0.85, 1, 900, 2600, 0.5, 1.5, 700, 2800, 1800, 4000, 6, 8, 8, 20, 0, 0 },
      { "choir", 43, 5, 0.1, 0.7, 1, 1500, 4000, 0.3, 1.0, 500, 1800, 1800, 4200, 2, 4, 4, 12, 0, 0 },
      { "glass", 11, 60, 0.1, 0.9, 1, 3000, 9000, 0.0, 0.8, 600, 2600, 2200, 4500, 2, 5, 6, 18, 0, 1 },
      { "shimmer", 62, 63, 0.0, 1.0, 1, 2500, 8000, 0.0, 1.0, 900, 3000, 2500, 4500, 2, 4, 6, 20, 0, 1 },
      { "dark drone", 56, 59, 0.0, 1.0, 2, 250, 900, 1.0, 3.0, 1200, 3500, 2500, 4500, 2, 4, 2, 10, -2, -1 },
      { "organ", 14, 2, 0.0, 0.6, 1, 2000, 6000, 0.0, 0.5, 300, 900, 900, 2200, 1, 2, 2, 8, -1, 0 },
      { "soft keys", 17, 6, 0.0, 0.6, 1, 1500, 4500, 0.2, 1.0, 300, 1200, 1800, 4000, 1, 3, 3, 10, 0, 0 },
   };
   constexpr int kPadKitCount = (int)(sizeof(kPadKits) / sizeof(kPadKits[0]));
   constexpr int kPluckKitCount = (int)(sizeof(kPluckKits) / sizeof(kPluckKits[0]));
}

namespace SampleStyles
{
const char* PartName(Part p)
{
   static const char* const kNames[] = { "all", "synths", "pads", "plucks", "percussion", "drums", "bass", "fx", "foley", "keys", "leads", "textures", "kicks", "snares", "hats", "toms" };
   const int i = (int)p;
   return i >= 0 && i < (int)Part::Count ? kNames[i] : kNames[0];
}

int Count() { return (int)(sizeof(kGenStyles) / sizeof(kGenStyles[0])); }

Style At(int i)
{
   const GenStyle& s = kGenStyles[std::clamp(i, 0, Count() - 1)];
   return { s.id, s.name, s.family };
}

int Find(const std::string& id)
{
   for (int i = 0; i < Count(); i++)
      if (id == kGenStyles[i].id)
         return i;
   return -1;
}

Compiled Compile(const Recipe& recipe)
{
   Compiled out;
   PickScope picks(out.picks);
   const int si = Find(recipe.style);
   if (si < 0)
   {
      out.error = "unknown style " + recipe.style;
      return out;
   }
   const GenStyle& st = kGenStyles[si];
   Rng rng(std::string(st.id) + ":" + std::to_string(recipe.seed));
   const int cardBpm = rng.Int(st.bpmLo, st.bpmHi);
   const int cardRoot = rng.Range(12);
   const std::vector<std::string> scales = Split(st.scales);
   const int scale = scales.empty() ? 0 : std::stoi(rng.Choice(scales));
   // The player's own habits (empty when off or not yet learned): how hard
   // the generator leans on them. A card stays a direction, not a rule.
   const UserStyle::Profile& user = UserStyle::Get();
   const bool mine = recipe.personal && user.valid;
   int root0 = recipe.key >= 0 ? recipe.key % 12 : cardRoot;
   int scale0 = recipe.scale >= 0 ? recipe.scale : scale;
   if (mine && user.hasPitch)
   {
      // The root the player lands on most, drawn by how often they play it, and
      // the card's scale that holds the most of what they play from that root.
      if (recipe.key < 0 && rng.Uniform() < std::min(0.95, (double)user.pitchW + 0.15))
      {
         double r = rng.Uniform();
         for (int i = 0; i < 12; i++)
         {
            r -= user.pitchClass[i];
            if (r <= 0.0)
            {
               root0 = i;
               break;
            }
         }
      }
      if (recipe.scale < 0 && !scales.empty())
      {
         double best = -1.0;
         for (const std::string& sc : scales)
         {
            const int id = std::stoi(sc);
            const MusicTime::ScaleDef& def = MusicTime::ScaleTable(id);
            double fit = 0.0;
            for (int k = 0; k < def.count; k++)
               fit += user.pitchClass[(root0 + def.intervals[k]) % 12];
            if (fit > best)
            {
               best = fit;
               scale0 = id;
            }
         }
      }
   }
   // No history yet: lean the root toward where catalogue music sits. Spotify's whole-catalogue count
   // (Ning, 2015) puts G major at 10.7% and C major at 10.2%, with the guitar and piano keys next; the
   // other weights are an estimate in that spirit. Its own stream, so nothing else moves.
   if (recipe.personal && recipe.key < 0 && !(mine && user.hasPitch))
   {
      static const double kPrior[12] = { 10.2, 3.0, 9.0, 4.0, 6.5, 6.0, 3.5, 10.7, 4.0, 8.0, 5.0, 4.0 }; // C .. B
      Rng pr(std::string("prior:") + std::to_string(recipe.seed));
      if (pr.Uniform() < 0.6)
      {
         double total = 0.0, r;
         for (double w : kPrior)
            total += w;
         r = pr.Uniform() * total;
         for (int i = 0; i < 12; i++)
         {
            r -= kPrior[i];
            if (r <= 0.0)
            {
               root0 = i;
               break;
            }
         }
      }
   }
   // Tempo: where the player usually runs the transport, when they have a history of it.
   float myBpm = 0.0f, bpmW = 0.0f;
   bool tempoFromUser = false;
   if (recipe.personal && recipe.bpm <= 0.0f && UserStyle::DrawTempo(rng.Uniform(), myBpm, bpmW) && rng.Uniform() < bpmW)
      tempoFromUser = true, out.bpm = std::clamp(myBpm + (float)rng.Uniform(-1.5, 1.5), (float)st.bpmLo * 0.6f, (float)st.bpmHi * 1.6f);
   else
      out.bpm = recipe.bpm > 0.0f ? recipe.bpm : (float)cardBpm;
   // Energy and mood lean the tempo (busier and brighter feel faster), within
   // reach of the card's band; a set tempo is the player's and stays.
   auto leanTempo = [&]()
   {
      if (recipe.bpm > 0.0f || tempoFromUser || (recipe.energy == 0.0f && recipe.mood == 0.0f))
         return;
      out.bpm = std::round(out.bpm * (1.0f + 0.15f * recipe.energy + 0.05f * recipe.mood) * 10.0f) / 10.0f;
   };
   leanTempo();
   // Mood swaps the scale family when the card picked it: mode is the
   // strongest single happy/sad cue in music, so a minor card turns major.
   // A strong mood takes a seven-note scale: a pentatonic has no full triads,
   // so its chords never sound plainly major or minor. Dark and driving is
   // tense (harmonic minor: the raised seventh pulls); dark and calm is sad.
   if (recipe.scale < 0)
   {
      using namespace MusicTime;
      if (recipe.mood > 0.35f)
         switch (scale0)
         {
            case kNaturalMinor: case kHarmonicMinor: case kMelodicMinor: case kPhrygian: case kLocrian: case kMinorPentatonic: case kBlues:
            case kMajorPentatonic: scale0 = kMajor; break;
            case kDorian: scale0 = kMixolydian; break;
            default: break;
         }
      else if (recipe.mood < -0.35f)
         switch (scale0)
         {
            case kMajor: case kMajorPentatonic: case kMinorPentatonic: case kBlues: case kNaturalMinor:
               scale0 = recipe.energy > 0.35f || recipe.mood < -0.85f ? kHarmonicMinor : kNaturalMinor; break;
            case kLydian: case kMixolydian: scale0 = kDorian; break;
            default: break;
         }
   }
   out.root = root0;
   out.scale = scale0;
   const int root = out.root;
   const Part part = recipe.part;
   const bool full = part == Part::Full;
   // Each of kick and bass alone matches the library's loops of its part, but summed in the full mix they put 39% of
   // the power at 40-80 Hz against 19% in the library's electronic full mixes, and the loudness match then turns the
   // mids down to 0.34x (mix_bands.py). In the full mix only, both sit lower; no draw moves.
   const float fullLowDb = full ? -6.0f : 0.0f;
   // Every layer is built whatever the part, so the random stream stays the
   // same and a part is that layer of the seed's full mix; a layer the part
   // leaves out goes no further than the mixer (the offline graph renders
   // only what reaches the tap). The drums still feed the sidechains.
   const bool pads = part == Part::Pads, plucks = part == Part::Plucks;
   const bool wantBass = full || part == Part::Synths;
   bool wantChords = full || pads || plucks;
   const bool wantMotif = wantBass;
   // Pads and plucks play the chords on a voice whose envelope is set for
   // them (the seeded spread runs attacks up to 4 s, which is neither). The
   // voice and envelope come from their own stream, so nothing else moves.
   Rng shapeRng(std::string("shape:") + std::to_string(recipe.seed));
   std::string shapedVoice;
   Params shapedEnv;
   double padWet = 0.45, padDecay = 3.0, padDelayMix = 0.25, padDelayFb = 40.0, padDelayBeats = 0.75;
   bool padDelay = false;
   bool pluckArp = true;
   double pluckRate = 0.25, pluckGate = 50.0, pluckStab = 1.0;
   int pluckMode = 0, pluckOctaves = 2;
   if (pads || plucks)
   {
      const bool analog = shapeRng.Range(2) == 0 && !plucks && !pads;
      shapedVoice = analog ? "Analog" : "Wavetable";
      const PluckKit* kit = plucks ? &kPluckKits[shapeRng.Range(kPluckKitCount)] : nullptr;
      const PadKit* pk = pads ? &kPadKits[shapeRng.Range(kPadKitCount)] : nullptr;
      if (pads)
         shapedVoice = "Wavetable";
      const char* const keys[4] = { analog ? "attack" : "a.ampAttack", analog ? "decay" : "a.ampDecay", analog ? "sustain" : "a.ampSustain",
                                    analog ? "release" : "a.ampRelease" };
      // A pluck rings about as long as this player holds a note.
      const double theirDecay = mine && user.durW > 0.0f ? std::clamp((double)user.durBeats * 60.0 / out.bpm * 1000.0 * 0.8, 60.0, 600.0) : 0.0;
      double decay = pads ? shapeRng.Uniform(800.0, 1600.0) : shapeRng.Uniform(kit->ampLo, kit->ampHi);
      if (plucks && theirDecay > 0.0)
         decay += (theirDecay - decay) * user.durW;
      const double env[4] = { pads ? shapeRng.Uniform(pk->atkLo, pk->atkHi) : shapeRng.Uniform(1.0, 5.0),
                              decay,
                              pads ? shapeRng.Uniform(0.7, 0.9) : 0.0,
                              pads ? shapeRng.Uniform(pk->relLo, pk->relHi) : shapeRng.Uniform(80.0, 200.0) };
      double fin[4] = { env[0], env[1], env[2], env[3] };
      // Where this player leaves the envelope knobs (milliseconds here; the habit is log2 seconds).
      // Their own stream, so the sound's other draws do not move when the habit does.
      if (mine)
      {
         Rng habit(std::string("habit:") + std::to_string(recipe.seed));
         const char* const roles[4] = { "env.attack", "env.decay", "env.sustain", "env.release" };
         for (int i = 0; i < 4; i++)
         {
            float v = 0.0f, w = 0.0f;
            const double u = habit.Uniform(), pick = habit.Uniform();
            if (!UserStyle::DrawKnob(roles[i], u, v, w) || pick >= w)
               continue;
            const double theirs = i == 2 ? std::clamp((double)v, 0.0, 1.0) : std::clamp(std::exp2((double)v) * 1000.0, 1.0, 4000.0);
            // Pluck decay is already pulled toward the note length; the habit only nudges that.
            const double share = (i == 1 && plucks) ? 0.5 : 1.0;
            fin[i] += (theirs - fin[i]) * share * w;
         }
         fin[2] = plucks ? 0.0 : fin[2]; // a pluck has no sustain
      }
      for (int i = 0; i < 4; i++)
         shapedEnv[keys[i]] = { 'f', Num(std::round(fin[i] * 1000.0) / 1000.0) };
      if (pads)
      {
         CleanWavetable(shapedEnv);
         auto put = [&](const char* k, double v) { shapedEnv[k] = { 'f', Num(std::round(v * 1000.0) / 1000.0) }; };
         auto putI = [&](const char* k, int v) { shapedEnv[k] = { 'i', std::to_string(v) }; };
         putI("a.table", shapeRng.Range(2) == 0 ? pk->tableA : pk->tableB);
         put("a.position", shapeRng.Uniform(pk->posLo, pk->posHi));
         putI("a.filterType", pk->filter);
         put("a.cutoff", shapeRng.Uniform(pk->cutLo, pk->cutHi));
         put("a.resonance", shapeRng.Uniform(0.0, 0.25));
         // The swell: the filter opens over about the same time the amp does.
         put("a.filterAmount", shapeRng.Uniform(pk->amtLo, pk->amtHi));
         put("a.filterAttack", fin[0] * shapeRng.Uniform(0.8, 1.6));
         put("a.filterDecay", shapeRng.Uniform(1200.0, 3500.0));
         put("a.filterSustain", shapeRng.Uniform(0.3, 0.8));
         put("a.filterRelease", fin[3]);
         putI("a.unison", pk->unisonLo + shapeRng.Range(pk->unisonHi - pk->unisonLo + 1));
         put("a.detune", shapeRng.Uniform(pk->detuneLo, pk->detuneHi));
         put("a.stereoWidth", shapeRng.Uniform(0.6, 1.0));
         putI("a.octave", pk->octaveLo + shapeRng.Range(pk->octaveHi - pk->octaveLo + 1));
         padWet = shapeRng.Uniform(0.35, 0.7);
         padDecay = shapeRng.Uniform(2.5, 7.0);
         padDelay = shapeRng.Uniform() < 0.6;
         padDelayMix = shapeRng.Uniform(0.15, 0.4);
         padDelayFb = shapeRng.Uniform(30.0, 60.0);
         padDelayBeats = shapeRng.Range(2) == 0 ? 0.75 : 1.5;
      }
      if (plucks)
      {
         CleanWavetable(shapedEnv);
         auto put = [&](const char* k, double v) { shapedEnv[k] = { 'f', Num(std::round(v * 1000.0) / 1000.0) }; };
         auto putI = [&](const char* k, int v) { shapedEnv[k] = { 'i', std::to_string(v) }; };
         putI("a.table", shapeRng.Range(2) == 0 ? kit->tableA : kit->tableB);
         put("a.position", shapeRng.Uniform(kit->posLo, kit->posHi));
         putI("a.filterType", kit->filter);
         put("a.cutoff", shapeRng.Uniform(kit->cutLo, kit->cutHi));
         put("a.resonance", shapeRng.Uniform(kit->resLo, kit->resHi));
         // The snap: the filter opens on the strike and closes in a share of the note's life.
         put("a.filterAmount", shapeRng.Uniform(kit->amtLo, kit->amtHi));
         put("a.filterAttack", 0.0);
         put("a.filterDecay", std::max(40.0, fin[1] * shapeRng.Uniform(0.3, 0.8)));
         put("a.filterSustain", 0.0);
         put("a.filterRelease", 60.0);
         putI("a.unison", kit->unisonLo + shapeRng.Range(kit->unisonHi - kit->unisonLo + 1));
         put("a.detune", shapeRng.Uniform(kit->detuneLo, kit->detuneHi));
         put("a.stereoWidth", shapeRng.Uniform(0.2, 0.9));
         if (kit->pitchHi > 0.0)
         {
            put("a.pitchAmount", shapeRng.Uniform(kit->pitchLo, kit->pitchHi));
            put("a.pitchAttack", 0.0);
            put("a.pitchDecay", shapeRng.Uniform(18.0, 55.0));
            put("a.pitchSustain", 0.0);
            put("a.pitchRelease", 20.0);
         }
         putI("a.octave", kit->octaveLo + shapeRng.Range(kit->octaveHi - kit->octaveLo + 1));
      }
      // How the plucks are played: an arpeggio at some rate, or chord stabs.
      pluckArp = shapeRng.Uniform() < 0.78;
      static const double kRates[] = { 0.125, 0.25, 0.25, 0.5, 0.1667 };
      pluckRate = kRates[shapeRng.Range(5)];
      pluckMode = shapeRng.Range(6);
      pluckOctaves = 1 + shapeRng.Range(3);
      pluckGate = shapeRng.Uniform(25.0, 70.0);
      static const double kStab[] = { 0.5, 1.0, 1.0, 2.0 };
      pluckStab = kStab[shapeRng.Range(4)];
   }

   std::set<std::string> ban;
   for (const std::string& b : Split(st.ban))
      ban.insert(b);
   auto pick = [&](const char* list) -> std::string
   {
      std::vector<std::string> ok;
      for (const std::string& n : Split(list))
         if (!ban.count(n) && TypeOf(n) != nullptr)
            ok.push_back(n);
      return ok.empty() ? std::string() : rng.Choice(ok);
   };
   // A card is a direction, not a rule: two in five times a voice comes from
   // any card's pool, so one card + part does not always sound the same.
   auto pickVoice = [&](const char* list) -> std::string
   {
      if (rng.Uniform() < 0.4)
      {
         std::vector<std::string> any;
         for (int i = 0; i < Count(); i++)
            for (const std::string& n : Split(kGenStyles[i].synth))
               if (!ban.count(n) && TypeOf(n) != nullptr && std::find(any.begin(), any.end(), n) == any.end())
                  any.push_back(n);
         if (!any.empty())
            return rng.Choice(any);
      }
      return pick(list);
   };
   auto seed = [&]() { return rng.Range(1 << 30); };
   // A step's gate, drawn toward how long this player holds a note.
   auto myGate = [&](double card, double stepBeats) -> double
   {
      if (!mine || user.durW <= 0.0f || stepBeats <= 0.0)
         return card;
      const double theirs = std::clamp((double)user.durBeats / stepBeats * 100.0, 15.0, 100.0);
      return card + (theirs - card) * user.durW;
   };

   Graph g;
   g.variation = recipe.variation;
   g.tone = std::clamp(recipe.tone, -1.0f, 1.0f);
   g.energy = std::clamp(recipe.energy, -1.0f, 1.0f);
   g.space = std::clamp(recipe.space, -1.0f, 1.0f);
   g.mood = std::clamp(recipe.mood, -1.0f, 1.0f);
   std::vector<std::pair<int, float>> chans; // (node, gain dB)
   std::vector<int> synths;
   int layers = 0; // layers the full mix has, whatever the part keeps

   // `clean`: a melodic voice keeps only an echo or a chorus, set by hand. Drive, saturation, crushing and a second
   // filter drawn at random are what made the synths distorted and muddy (owner's review, 2026-10-29). The card's
   // picks are drawn either way, so every part after keeps its draws.
   auto chain = [&](int src, int fxN, bool clean = false)
   {
      int end = src;
      for (int i = 0; i < fxN; i++)
      {
         const std::string n = pick(st.fx);
         if (n.empty())
            break;
         const int sd = seed();
         int id = 0;
         if (!clean)
            id = g.Add(n, sd);
         else if (n == "Delay")
         {
            Rng fr(std::string("cleanfx:") + std::to_string(sd));
            id = g.Add("Delay", sd, F({ { "mix", fr.Uniform(0.15, 0.25) }, { "timeMs", (fr.Uniform() < 0.6 ? 0.75 : 0.5) * 60000.0 / out.bpm },
                                         { "feedback", fr.Uniform(25.0, 40.0) }, { "tone", -0.2 }, { "sync", 0.0 }, { "wow", 0.0 }, { "flutter", 0.0 },
                                         { "saturation", 0.0 }, { "diffusion", 0.0 } })); // sync off, or timeMs is ignored
         }
         else if (n == "Chorus")
         {
            Rng fr(std::string("cleanfx:") + std::to_string(sd));
            id = g.Add("Chorus", sd, F({ { "mix", fr.Uniform(0.2, 0.35) }, { "delay", fr.Uniform(10.0, 16.0) }, { "depth", fr.Uniform(2.0, 5.0) },
                                          { "rate", fr.Uniform(0.2, 0.6) }, { "spread", 0.8 } }));
         }
         if (id == 0)
            continue;
         g.Connect(end, id, false);
         end = id;
      }
      return end;
   };
   auto noteChain = [&](int src, const char* list)
   {
      int cur = src;
      for (const std::string& nf : Split(list))
      {
         const int id = g.Add(nf, seed(), NoteFx(nf));
         g.Connect(cur, id, true);
         cur = id;
      }
      return cur;
   };
   // Nothing sits below E1 (41 Hz): lower than that is felt, not heard, and vanishes on most speakers.
   auto degreeNote = [&](int deg, int octave) {
      int n = MusicTime::DegreeToNote(deg, octave + 1, root, out.scale);
      while (n < 28)
         n += 12;
      return n;
   };

   // Drums: the card's 16-step lanes over two bars, each hit breathing a
   // little, ghost notes between the beats and maybe a fill into bar 3.
   // Drums and percussion are the card's lanes split in two (kick, snare,
   // hats | clap, toms, bell). A card with no drums asked for drums gets a
   // plain kit; one with no percussion plays its kit's figures on the
   // percussion sounds (snare on clap, kick on low tom, open hat on high
   // tom, closed hat on bell).
   const char* lanes[8];
   bool hasDrums = false;
   for (int i = 0; i < 8; i++)
   {
      lanes[i] = st.drums[i] != nullptr ? st.drums[i] : "";
      hasDrums = hasDrums || *lanes[i] != 0;
   }
   const bool subPart = part == Part::Kicks || part == Part::Snares || part == Part::Hats || part == Part::Toms;
   const bool drumPart = part == Part::Drums || part == Part::Percussion || subPart;
   auto none = [&](int from) { return !*lanes[from] && !*lanes[from + 1] && !*lanes[from + 2] && !*lanes[from + 3]; };
   if (drumPart && none(0) && ((part != Part::Percussion && part != Part::Toms) || none(4)))
   {
      lanes[0] = "x...x...x...x...";
      lanes[1] = "....x.......x...";
      lanes[2] = "..x...x...x...x.";
      hasDrums = true;
   }
   if ((part == Part::Percussion || part == Part::Toms) && none(4))
   {
      lanes[4] = lanes[1];
      lanes[5] = lanes[0];
      lanes[6] = lanes[3];
      lanes[7] = lanes[2];
      hasDrums = true;
   }
   // A genre feel for the drum family: its own stream, so nothing else moves.
   // Direction, not rules: one skeleton per feel, the hits then breathe as usual.
   if (drumPart)
   {
      Rng feel(std::string("feel:") + std::to_string(recipe.seed));
      struct Feel { const char* k; const char* s; const char* h; const char* o; const char* c; double lo, hi; };
      static const Feel kFeels[] = {
         { "x...x...x...x...", "....x.......x...", "..x...x...x...x.", "..o...o...o...o.", "....x.......x...", 118, 128 },   // house
         { "x...x...x...x...", "................", "xxxxxxxxxxxxxxxx", "..o...o...o...o.", "....x.......x...", 126, 136 },   // techno
         { "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "......o.......o.", "....x.......x...", 86, 98 },     // boom bap
         { "x.......x.x.....", "........x.......", "xxxxxxxxxxxxxxxx", "......o.......o.", "........x.......", 134, 148 },  // trap
         { "x.........x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "......o.........", "....x.......x...", 170, 176 },  // dnb
         { "x.....x.....x...", "....x..x..x.x...", "x.x.x.x.x.x.x.x.", "........o.......", "....x.......x...", 112, 140 },  // breakbeat
      };
      const int nf = (int)(sizeof(kFeels) / sizeof(kFeels[0]));
      const Feel& f = kFeels[feel.Range(nf)];
      const bool own = recipe.bpm > 0.0f || tempoFromUser;
      if (subPart && !own)
      {
         out.bpm = (float)feel.Uniform(f.lo, f.hi);
         leanTempo();
      }
      if (subPart)
      {
         lanes[0] = f.k;
         lanes[1] = f.s;
         lanes[2] = f.h;
         lanes[3] = f.o;
         lanes[4] = f.c;
         if (!*lanes[1])
            lanes[1] = "....x.......x...";
         lanes[5] = "x...............";
         lanes[6] = "........x.......";
         lanes[7] = "..x.....x.....x.";
         hasDrums = true;
      }
      // Hats, toms and percussion play a groove of their own, not just the
      // feel's skeleton: closed hat, open hat | low tom, high tom | clap, bell.
      if (part == Part::Hats)
      {
         static const char* kHats[][2] = {
            { "x.x.x.x.x.x.x.x.", "......o.......o." },   // eighths
            { "xoxoxoxoxoxoxoxo", "" },                    // sixteenths, accented
            { "x.o.x.o.x.o.x.o.", "..x...x...x...x." },    // offbeat open
            { "x.x.xxx.x.x.xxxx", "" },                    // trap rolls
            { "x..x..x..x..x..x", "" },                    // dotted
            { "x.......x.......", "....o.......o..." },    // sparse
            { "xoxxxoxoxoxxxoxo", "..............o." },    // ghosted sixteenths
            { "x.xxx.x.x.xxx.x.", "......x.......x." },    // broken
            { "o.x.o.x.o.x.o.xx", "" },                    // swung eighths with a push
            { "xxx.xxx.xxx.xxx.", "...x.......x...." },    // triplet feel
         };
         const int pick = feel.Range((int)(sizeof(kHats) / sizeof(kHats[0])));
         lanes[2] = kHats[pick][0];
         lanes[3] = kHats[pick][1];
      }
      if (part == Part::Snares && feel.Uniform() < 0.6)
         lanes[4] = ""; // a snare on its own, no clap on top
      if (part == Part::Toms)
      {
         static const char* kToms[][2] = {
            { "x...............", "........x......." },
            { "x..x..x.........", "..........x.x..." },
            { "x.......x.x.....", "....x.......x..x" },
            { "x.x.............", "......x.x.x....." },
            { "x..x....x..x....", "..x...x...x..x.." },
            { "x...x...x...x...", "..x...x...x...xx" },
         };
         const int pick = feel.Range((int)(sizeof(kToms) / sizeof(kToms[0])));
         lanes[5] = kToms[pick][0];
         lanes[6] = kToms[pick][1];
      }
   }
   if (part == Part::Percussion)
   {
      Rng feel(std::string("perc:") + std::to_string(recipe.seed));
      static const char* kPerc[][4] = {
         { "....x.......x...", "x..x..x.........", "........x.x.....", "..x...x...x...x." },
         { "....x..x....x...", "x.....x...x.....", "..x.x.....x.x...", "x.x.x.x.x.x.x.x." },
         { "................", "x..x....x..x....", "....x..x....x..x", "o.o.o.o.o.o.o.o." },
         { "....x.......x.x.", "x.......x.......", "..x.....x.x.....", "..o..o..o..o..o." },
         { "..x...x...x...x.", "x...x.......x...", "......x.x.......", "x.....x.....x..." },
         { "....x.......x...", "x..x..x..x..x...", "..x..x..x..x..x.", "o...o...o...o..." },
      };
      const int pick = feel.Range((int)(sizeof(kPerc) / sizeof(kPerc[0])));
      lanes[4] = kPerc[pick][0];
      lanes[5] = kPerc[pick][1];
      lanes[6] = kPerc[pick][2];
      lanes[7] = kPerc[pick][3];
      hasDrums = true;
   }
   if (part == Part::Drums)
      lanes[4] = lanes[5] = lanes[6] = lanes[7] = "";
   if (part == Part::Percussion)
      lanes[0] = lanes[1] = lanes[2] = lanes[3] = "";
   if (part == Part::Kicks)
      lanes[1] = lanes[2] = lanes[3] = lanes[4] = lanes[5] = lanes[6] = lanes[7] = "";
   if (part == Part::Snares)
      lanes[0] = lanes[2] = lanes[3] = lanes[5] = lanes[6] = lanes[7] = "";
   if (part == Part::Hats)
      lanes[0] = lanes[1] = lanes[4] = lanes[5] = lanes[6] = lanes[7] = "";
   if (part == Part::Toms)
      lanes[0] = lanes[1] = lanes[2] = lanes[3] = lanes[4] = lanes[7] = ""; // no bell: it is the high metallic ring

   // The Drums part plays a groove of its genre, not the card's one figure (owner's review, 2026-10-29: "after some
   // generation the pattern repeats"). A groove is a skeleton, hats and maybe ghost notes, each a figure several
   // Groove MIDI drummers share, put together from the genres whose drummers play the card's family: breaks and
   // drum & bass take funk and hip-hop breaks at their own tempo, as jungle does; four-on-the-floor families keep the
   // kick and backbeat and take the hats and ghosts; bass music is half-time. Its own stream: only this part moves.
   std::string bank[4], clap909;
   if (part == Part::Drums && hasDrums)
   {
      Rng gb(std::string("groove-bank:") + std::to_string(recipe.seed));
      const std::string fam = st.family;
      enum { kFunk, kHiphop, kRock, kPop, kSoul, kReggae, kLatin, kAfrobeat, kNewOrleans, kDance };
      std::vector<int> pool;
      const bool four = fam == "house" || fam == "techno" || fam == "trance" || fam == "hard";
      const bool half = fam == "bass_music";
      if (fam == "breaks" || fam == "drum_and_bass")
         pool = { kFunk, kFunk, kHiphop, kRock };
      else if (fam == "downtempo" || fam == "piano")
         pool = { kHiphop, kFunk, kSoul, kPop, kLatin };
      else if (fam == "retro")
         pool = { kRock, kPop, kDance };
      else if (four)
         pool = { kFunk, kLatin, kAfrobeat, kDance, kRock };
      else if (half)
         pool = { kHiphop, kFunk };
      else
         pool = { kFunk, kHiphop, kRock, kLatin, kAfrobeat };
      auto inPool = [&](int genre) { return std::find(pool.begin(), pool.end(), genre) != pool.end(); };
      std::vector<const GrooveSkeleton*> sk;
      std::vector<const GrooveHats*> hs;
      std::vector<const GrooveGhosts*> gh;
      for (const GrooveSkeleton& x : kGrooveSkeletons)
         if (inPool(x.genre))
            sk.push_back(&x);
      for (const GrooveHats& x : kGrooveHats)
         if (inPool(x.genre))
            hs.push_back(&x);
      for (const GrooveGhosts& x : kGrooveGhosts)
         if (inPool(x.genre))
            gh.push_back(&x);
      if (!sk.empty() && !hs.empty())
      {
         const GrooveSkeleton& s0 = *sk[(size_t)gb.Range((int)sk.size())];
         const GrooveHats& h0 = *hs[(size_t)gb.Range((int)hs.size())];
         std::string kick = s0.k, snare = s0.s;
         if (four)
         {
            kick = "x...x...x...x...";
            snare = *lanes[1] ? std::string(lanes[1]) : std::string("....x.......x...");
            snare.resize(16, '.');
         }
         else if (half)
         {
            snare = "........x.......";
            for (int i = 6; i <= 9; i++) // the half-time backbeat on 3 stands alone
               kick[(size_t)i] = '.';
         }
         if (!gh.empty() && gb.Uniform() < 0.6)
         {
            const std::string gs = gh[(size_t)gb.Range((int)gh.size())]->s;
            for (int i = 0; i < 16; i++)
               if (snare[(size_t)i] == '.' && gs[(size_t)i] == 'o')
                  snare[(size_t)i] = 'o';
         }
         bank[0] = kick, bank[1] = snare, bank[2] = h0.h, bank[3] = h0.o;
         // Four-on-the-floor takes the hats, open hat and clap of a 909 pattern on about half its takes (item 17):
         // drawn after the Groove MIDI draws, so the other half is unchanged.
         if (four && gb.Uniform() < 0.5)
         {
            std::vector<const Groove909*> nine;
            for (const Groove909& x : kGroove909)
               if (x.four)
                  nine.push_back(&x);
            const Groove909& g = *nine[(size_t)gb.Range((int)nine.size())];
            bank[2] = g.h, bank[3] = g.o;
            if (std::string(g.c).find('x') != std::string::npos)
               clap909 = g.c;
         }
         if (bank[3].find_first_not_of('.') == std::string::npos)
            bank[3].clear();
         for (int i = 0; i < 4; i++)
            lanes[i] = bank[i].c_str();
         if (!four)
            lanes[4] = ""; // the skeleton's snare is the backbeat; a card clap on top doubles it
         else if (!clap909.empty())
            lanes[4] = clap909.c_str();
      }
   }

   Rng criticRng(std::string("critic:") + st.id + ":" + std::to_string(recipe.seed));
   std::string propLanes[8];
   float drumNov = 0.0f, drumSync = 0.0f, melNov = 0.0f;
   if (hasDrums && !subPart && part != Part::Percussion && std::string(st.family) != "ambient")
   {
      ProposeGroove(criticRng, st, mine, lanes, propLanes, drumNov, drumSync);
      for (int i = 0; i < 8; i++)
         lanes[i] = propLanes[i].c_str();
      if (part == Part::Drums)
         lanes[4] = lanes[5] = lanes[6] = lanes[7] = "";
   }

   // Energy thins or fills the closed hats: the cheapest measured lever on how
   // busy a groove feels (groove-2h). Calm keeps the eighths, then quarters;
   // driving fills the eighths, then every sixteenth with ghosts between.
   std::string hatLane;
   const char* cardHats = lanes[2];
   if (lanes[2] && *lanes[2] && std::abs(recipe.energy) > 0.3f)
   {
      std::string h = lanes[2];
      h.resize(16, '.');
      const int keep = recipe.energy < -0.7f ? 4 : 2;
      std::string thin = h;
      for (int k = 0; k < 16; k++)
      {
         if (recipe.energy < 0.0f && k % keep)
            thin[(size_t)k] = '.';
         else if (recipe.energy > 0.0f && thin[(size_t)k] == '.' && (k % 2 == 0 || recipe.energy > 0.7f))
            thin[(size_t)k] = k % 2 ? 'o' : 'x';
      }
      if (thin.find_first_not_of('.') != std::string::npos)
      {
         hatLane = thin;
         lanes[2] = hatLane.c_str();
      }
   }

   int drums = 0, duckKey = 0;
   bool is808Kick = false;
   if (hasDrums)
   {
      // A swung card plays a real machine's swing (MPC 3000 / SP 1200 16ths, item 17): one within 0.12 of the card's,
      // else the nearest. Its own stream.
      float swing = st.swing;
      if (st.swing > 0.02f)
      {
         std::vector<float> near;
         float best = kSwing909[0];
         for (float v : kSwing909)
         {
            if (std::abs(v - st.swing) <= 0.12f)
               near.push_back(v);
            if (std::abs(v - st.swing) < std::abs(best - st.swing))
               best = v;
         }
         Rng sr(std::string("swing909:") + std::to_string(recipe.seed));
         swing = near.empty() ? best : near[(size_t)sr.Range((int)near.size())];
      }
      Params force = F({ { "swing", swing }, { "volume", 0.9 } });
      force["steps"] = { 'i', "32" };
      force["rate"] = { 'i', "12" };
      force["run"] = { 'b', "1" };
      const char* fillLane = nullptr;
      const int fillIdx[] = { 1, 4, 5, 2 }; // snare, clap, tom, closed hat
      for (int li : fillIdx)
         if (*lanes[li])
         {
            fillLane = lanes[li];
            break;
         }
      const int fn = (fillLane != nullptr && rng.Uniform() < st.fill) ? 2 + rng.Range(3) : 0;
      // Velocity feel: one metric accent depth per take, downbeats over 8ths over 16ths, drawn wide from
      // theory (feel.json's corpus bands are the critic's limits, not a target). Its own stream, so the
      // patterns and the kit voicing do not move when the feel does.
      Rng fr(std::string("feel:") + std::to_string(recipe.seed));
      const double accentDepth = std::min(0.45, fr.Uniform(0.0, 0.32) * (0.6 + 0.8 * st.groove));
      static const double kLaneAccent[8] = { 0.6, 0.6, 1.0, 0.8, 0.6, 0.8, 0.8, 1.0 }; // hats carry the alternation
      // The hats energy added or took away draw from their own stream: the
      // shared one is spent on the card's lane exactly as with the dots at
      // zero, so the voices picked after it stay the same sample.
      Rng hx(std::string("hats:") + std::to_string(recipe.seed));
      for (int i = 0; i < 8; i++)
      {
         const std::string lane = "lane" + std::to_string(i);
         force[lane + "_path"] = { 's', DrumSequencerNode::KitDir() + "/" + kKitFiles[i] + ".wav" };
         force[lane + "_volume"] = { 'f', Num(i == 2 ? 0.55 : part == Part::Percussion && i == 5 ? 0.6 : i == 0 ? 0.85 * std::pow(10.0, fullLowDb / 20.0) : 0.85) };
         const std::string pat = lanes[i];
         const std::string cardPat = i == 2 && !hatLane.empty() ? std::string(cardHats) : pat;
         const bool ghostLane = i == 1 || i == 2 || i == 3 || i == 4;
         auto stepVel = [&](const std::string& p, int k, Rng& r)
         {
            const char c = p.empty() ? '.' : p[(size_t)(k % 16)];
            double v = c == 'x' ? 1.0 : c == 'o' ? 0.45 : 0.0;
            if (v > 0.0)
            {
               const double metric = k % 4 == 0 ? 1.0 : k % 2 == 0 ? 0.72 : 0.5;
               v *= 1.0 - 2.0 * accentDepth * kLaneAccent[i] * (1.0 - metric);
               v = std::clamp(v + r.Uniform(-0.12, 0.12) * st.groove, 0.2, 1.0);
            }
            if (v > 0.0 && mine && user.hasVelocity)
               v = std::clamp(v * (1.0 + (std::clamp(user.velMean / 0.75, 0.6, 1.15) - 1.0) * user.velW), 0.2, 1.0); // as hard as they play
            else if (!p.empty() && ghostLane && k % 2 == 1 && r.Uniform() < 0.22 * st.groove * (1.0 + 2.0 * (mine && user.hasTiming ? user.offGrid * user.timingW : 0.0)))
               v = 0.18 + 0.1 * r.Uniform();
            return v;
         };
         for (int k = 0; k < 32; k++)
         {
            double v = stepVel(cardPat, k, rng);
            if (cardPat != pat)
            {
               const char was = cardPat[(size_t)(k % 16)], now = pat[(size_t)(k % 16)];
               if (now == '.')
                  v = 0.0;
               else if (now != was)
                  v = stepVel(pat, k, hx);
            }
            if (fn > 0 && k >= 32 - fn)
            {
               if (lanes[i] == fillLane)
                  v = 0.55 + 0.45 * (k - (32 - fn) + 1) / fn;
               else if (i == 0 && k == 31)
                  v = 0.0;
            }
            force[lane + "_step" + std::to_string(k)] = { 'f', Num(std::round(v * 1000.0) / 1000.0) };
         }
      }
      // Each take voices its own kit: a character that shapes every lane, then
      // each lane's pitch, decay, snap and place in the stereo field. Its own
      // stream, so the grooves above do not move when the voicing does.
      Rng dr(std::string("drum:") + std::to_string(recipe.seed));
      struct Character { double pitch, decay, snap, spread; };
      static const Character kChars[] = { { 0.0, 0.35, 0.5, 0.3 },   // tight
                                          { -3.0, 0.0, 0.0, 0.2 },   // boomy
                                          { 2.0, 0.2, 0.9, 0.5 },    // crunchy
                                          { -1.0, 0.1, -0.5, 0.4 },  // soft
                                          { 4.0, 0.5, 0.7, 0.6 } };  // snappy
      const Character& cr = kChars[dr.Range(5)];
      const bool lowLane[8] = { true, false, false, false, false, true, false, false };
      for (int i = 0; i < 8; i++)
      {
         const std::string lane = "lane" + std::to_string(i);
         // Sample kicks, snares and toms sit lower: a kick under its pitch, a snare with body, toms at tom pitch.
         static const double kLower[8] = { -1.0, -3.0, 0.0, 0.0, 0.0, -3.0, -4.0, 0.0 };
         const double pitch = cr.pitch * (lowLane[i] ? 0.5 : 1.0) + dr.Uniform(-4.0, 4.0) * (lowLane[i] ? 0.6 : 1.0) + (subPart || part == Part::Drums ? kLower[i] : 0.0);
         force[lane + "_pitch"] = { 'f', Num(std::round(pitch * 100.0) / 100.0) };
         force[lane + "_finetune"] = { 'f', Num(std::round(dr.Uniform(-30.0, 30.0))) };
         force[lane + "_decay"] = { 'f', Num(std::round(std::clamp(cr.decay + dr.Uniform(-0.2, 0.3), 0.0, 0.85) * 100.0) / 100.0) };
         force[lane + "_transient"] = { 'f', Num(std::round(std::clamp(cr.snap + dr.Uniform(-0.4, 0.4), -1.0, 1.0) * 100.0) / 100.0) };
         const double pan = (i == 0 || i == 1) ? 0.0 : dr.Uniform(-cr.spread, cr.spread);
         force[lane + "_pan"] = { 'f', Num(std::round(pan * 100.0) / 100.0) };
      }
      // A kick or hat built from a synth, in place of the sample, two takes in five:
      // pitch-dropped sub kicks from tight to 808, and metallic hats.
      struct Replace { int lane; int node; float gain; };
      std::vector<Replace> synthDrums;
      auto lanePattern = [&](int li, double base, std::vector<double>& v)
      {
         v.assign(16, 0.0);
         const std::string pat = lanes[li];
         for (int k = 0; k < 16; k++)
         {
            const char c = pat.empty() ? '.' : pat[(size_t)k];
            v[(size_t)k] = c == 'x' ? 1.0 : c == 'o' ? base : 0.0;
         }
      };
      auto drumSeq = [&](int li, int note, double gate, int& outNode)
      {
         std::vector<double> v;
         lanePattern(li, 0.5, v);
         Params p = F({ { "rateBeats", 0.25 }, { "gatePercent", gate } });
         p["steps"] = { 'i', "16" };
         p["rateMode"] = { 'i', "0" };
         for (int k = 0; k < 16; k++)
         {
            p["note" + std::to_string(k)] = { 'i', std::to_string(note) };
            p["en" + std::to_string(k)] = { 'b', v[(size_t)k] > 0.0 ? "1" : "0" };
            p["vel" + std::to_string(k)] = { 'f', Num(v[(size_t)k] > 0.0 ? v[(size_t)k] : 0.0) };
         }
         outNode = g.Add("Note Sequencer", dr.Range(1 << 30), p);
      };
      auto noiseVoice = [&](int filter, double cut, double res, double dec, double vol)
      {
         Params nn = F({ { "volume", vol }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 0.0 },
                         { "decay", dec }, { "sustain", 0.0 }, { "release", dec * 0.5 }, { "cutoff", cut }, { "resonance", res },
                         { "voices", 1.0 }, { "keyTrack", 0.0 } });
         nn["filterType"] = { 'i', std::to_string(filter) };
         return g.Add("Analog", dr.Range(1 << 30), nn);
      };
      auto toneVoice = [&](double drop, double dropMs, double decMs, int table, double cutoff)
      {
         Params k = F({ { "mix", 0.45 }, { "volume", 0.9 }, { "a.cutoff", cutoff }, { "a.resonance", 0.0 }, { "a.filterAmount", 0.0 },
                        { "a.ampAttack", 0.0 }, { "a.ampSustain", 0.0 }, { "a.pitchSustain", 0.0 }, { "a.pitchRelease", 20.0 },
                        { "a.ampRelease", 30.0 }, { "a.detune", 0.0 }, { "a.warpAmount", 0.0 }, { "fmDepth", 0.0 } });
         CleanWavetable(k);
         k["a.table"] = { 'i', std::to_string(table) };
         k["a.unison"] = { 'i', "1" };
         k["a.filterType"] = { 'i', "1" };
         k["a.octave"] = { 'i', "0" };
         k["a.pitchAmount"] = { 'f', Num(drop) };
         k["a.pitchDecay"] = { 'f', Num(dropMs) };
         k["a.ampDecay"] = { 'f', Num(decMs) };
         return g.Add("Wavetable", dr.Range(1 << 30), k);
      };
      if (*lanes[0] && dr.Uniform() < (part == Part::Kicks ? 0.95 : subPart ? 0.75 : 0.4))
      {
         int sq = 0;
         static const int kKinds[4] = { 1, 0, 1, 2 };
         const int kind = kKinds[dr.Range(4)]; // 0 punch, 1 808 (twice as likely), 2 thump
         is808Kick = (kind == 1);
         drumSeq(0, 29 + dr.Range(7), 90.0, sq); // 46-73 Hz: the kick's fundamental, audible on a small speaker too
         Params k = F({ { "mix", 0.45 }, { "volume", 0.9 }, { "a.position", dr.Uniform(0.0, 0.3) }, { "a.cutoff", 9000.0 }, { "a.resonance", 0.0 },
                        { "a.filterAmount", 0.0 }, { "a.ampAttack", 0.0 }, { "a.ampSustain", 0.0 }, { "a.pitchAttack", 0.0 },
                        { "a.pitchSustain", 0.0 }, { "a.pitchRelease", 20.0 }, { "a.ampRelease", 30.0 }, { "a.detune", 0.0 },
                        { "a.warpAmount", 0.0 }, { "fmDepth", 0.0 } });
         CleanWavetable(k);
         k["a.table"] = { 'i', kind == 2 ? "10" : "0" };
         k["a.unison"] = { 'i', "1" };
         k["a.filterType"] = { 'i', "1" };
         k["b.on"] = { 'b', "0" };
         k["a.octave"] = { 'i', "0" };
         k["a.pitchAmount"] = { 'f', Num(kind == 0 ? dr.Uniform(14.0, 26.0) : kind == 1 ? dr.Uniform(8.0, 16.0) : dr.Uniform(20.0, 36.0)) };
         k["a.pitchDecay"] = { 'f', Num(kind == 0 ? dr.Uniform(18.0, 40.0) : kind == 1 ? dr.Uniform(50.0, 110.0) : dr.Uniform(25.0, 60.0)) };
         k["a.ampDecay"] = { 'f', Num(kind == 0 ? dr.Uniform(200.0, 340.0) : kind == 1 ? dr.Uniform(600.0, 1100.0) : dr.Uniform(300.0, 520.0)) };
         const int sy = g.Add("Wavetable", dr.Range(1 << 30), k);
         g.Connect(sq, sy, true);
         force["lane0_volume"] = { 'f', "0" };
         synthDrums.push_back({ 0, sy, -3.0f + fullLowDb });
         if (dr.Uniform() < 0.45) // a 2-5 ms noise click on the front, 0.5-2.2 kHz: SubLab's attacks (item 16, click_measure.py)
         {
            const int ck = noiseVoice(2, dr.Uniform(500.0, 2200.0), 0.1, dr.Uniform(2.0, 5.0), 0.8);
            g.Connect(sq, ck, true);
            synthDrums.push_back({ 0, ck, -13.0f });
         }
      }
      if (*lanes[2] && dr.Uniform() < (part == Part::Hats ? 0.9 : subPart ? 0.75 : 0.35))
      {
         // A hat is mostly noise: band-passed hiss around 7-9 kHz with a short decay, and only a little ring of metal under it.
         int sq = 0;
         drumSeq(2, 84, 20.0, sq);
         const int hz = noiseVoice(8, dr.Uniform(6500.0, 9500.0), dr.Uniform(0.15, 0.4), dr.Uniform(22.0, 80.0), 0.8);
         g.Connect(sq, hz, true);
         force["lane2_volume"] = { 'f', "0" };
         synthDrums.push_back({ 2, hz, -7.0f });
         if (dr.Uniform() < 0.6)
         {
            Params h = F({ { "transient", dr.Uniform(0.5, 1.6) }, { "decay", dr.Uniform(0.02, 0.07) }, { "stiffness", dr.Uniform(0.8, 2.0) },
                           { "filterCutoff", dr.Uniform(7000.0, 16000.0) }, { "filterResonance", dr.Uniform(0.0, 0.3) },
                           { "volume", 0.7 }, { "width", dr.Uniform(0.2, 0.8) } });
            h["material"] = { 'i', std::to_string(dr.Range(8)) };
            const int sy = g.Add("Metallic", dr.Range(1 << 30), h);
            g.Connect(sq, sy, true);
            synthDrums.push_back({ 2, sy, -18.0f });
         }
      }
      // A snare, clap or toms from a synth, each about one take in three: noise
      // through a band for the crack, a dropped sine for the body.
      if (*lanes[1] && dr.Uniform() < (part == Part::Snares ? 0.9 : subPart ? 0.75 : 0.33))
      {
         int sq = 0;
         drumSeq(1, 49 + dr.Range(7), 25.0, sq); // 140-210 Hz body, the snare's fundamental
         const int nz = noiseVoice(dr.Range(2) ? 6 : 2, dr.Uniform(1500.0, 4500.0), dr.Uniform(0.05, 0.3), dr.Uniform(60.0, 160.0), 0.8);
         g.Connect(sq, nz, true);
         const int bd = toneVoice(dr.Uniform(6.0, 12.0), dr.Uniform(15.0, 45.0), dr.Uniform(110.0, 220.0), dr.Range(2) ? 0 : 10, 4500.0);
         g.Connect(sq, bd, true);
         force["lane1_volume"] = { 'f', "0" };
         synthDrums.push_back({ 1, nz, -6.0f });
         synthDrums.push_back({ 1, bd, -3.0f });
      }
      if (*lanes[4] && dr.Uniform() < (subPart ? 0.75 : 0.33))
      {
         int sq = 0;
         drumSeq(4, 60, 20.0, sq);
         const int nz = noiseVoice(6, dr.Uniform(900.0, 2800.0), dr.Uniform(0.1, 0.6), dr.Uniform(90.0, 280.0), 0.9);
         g.Connect(sq, nz, true);
         // The 909 clap is one noise burst retriggered about four times, ~11 ms apart.
         const int bursts = g.Add("Delay", dr.Range(1 << 30), F({ { "mix", 0.5 }, { "timeMs", dr.Uniform(9.0, 16.0) }, { "feedback", dr.Uniform(0.3, 0.5) } }));
         g.Connect(nz, bursts, false);
         force["lane4_volume"] = { 'f', "0" };
         synthDrums.push_back({ 4, bursts, -4.0f });
      }
      // A struck head (owner's review, 2026-10-29: toms "sound like bass"; Farnell's Designing Sound and the membrane
      // modes): a body that drops in pitch as the head relaxes, the head's second mode about 1.59 times higher and
      // shorter, and the stick's noise on the front. Short and bright enough that it never rings as a bass note.
      auto headVoice = [&](double dropSemis, double dropMs, double decMs, double modeVol, double brightness)
      {
         Params k;
         CleanWavetable(k);
         k["volume"] = { 'f', "0.9" };
         k["mix"] = { 'f', "0.4" };
         Engine(k, 'a', 0, kSine, 1, 0.0, 0, 1.0);
         Engine(k, 'b', 0, kSine, 1, 0.0, 0, modeVol);
         k["b.semi"] = { 'i', "8" };
         k["b.fine"] = { 'f', "4" };
         for (char e : { 'a', 'b' })
         {
            const std::string p = std::string(1, e) + ".";
            k[p + "pitchAmount"] = { 'f', Num(std::round(dropSemis * 100.0) / 100.0) };
            k[p + "pitchDecay"] = { 'f', Num(std::round(dropMs)) };
            EngineFilter(k, e, 1, brightness, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
            EngineAmp(k, e, 0.0, e == 'a' ? decMs : decMs * 0.45, 0.0, 30.0);
         }
         return g.Add("Wavetable", dr.Range(1 << 30), k);
      };
      if (part != Part::Percussion && (*lanes[5] || *lanes[6]) && dr.Uniform() < (part == Part::Toms ? 1.0 : subPart ? 0.75 : 0.33))
      {
         // Floor 98-130 Hz, rack 165-233 Hz: tom pitch, an octave above where a bass plays.
         const int notes[2] = { 43 + dr.Range(6), 52 + dr.Range(7) };
         for (int ti = 0; ti < 2; ti++)
         {
            if (!*lanes[5 + ti])
               continue;
            int sq = 0;
            drumSeq(5 + ti, notes[ti], 70.0, sq);
            const int tm = headVoice(dr.Uniform(3.0, 7.0), dr.Uniform(60.0, 140.0), ti == 0 ? dr.Uniform(220.0, 380.0) : dr.Uniform(160.0, 300.0),
                                     dr.Uniform(0.25, 0.45), dr.Uniform(5000.0, 9000.0));
            g.Connect(sq, tm, true);
            const int st2 = noiseVoice(8, dr.Uniform(2500.0, 5000.0), 0.2, dr.Uniform(8.0, 18.0), 0.8); // the stick
            g.Connect(sq, st2, true);
            force["lane" + std::to_string(5 + ti) + "_volume"] = { 'f', "0" };
            synthDrums.push_back({ 5 + ti, tm, -4.0f });
            synthDrums.push_back({ 5 + ti, st2, -14.0f });
          }
      }
      if (part == Part::Percussion)
      {
         // Percussion is hand drums, shakers and struck wood or metal playing interlocking figures (MusicRadar's clave,
         // shaker and percussion guides; Production Expert): one drum pair, one continuous shaker or tambourine, one
         // accent instrument, each with its own pattern. All synthesised; the kit's samples are left silent.
         Rng pe(std::string("perc-ens:") + std::to_string(recipe.seed));
         for (int i = 4; i < 8; i++)
            force["lane" + std::to_string(i) + "_volume"] = { 'f', "0" };
         const std::string fam = st.family;
         const bool four = fam == "house" || fam == "techno" || fam == "trance" || fam == "hard";
         auto patSeq = [&](const std::string& pat, int note, double gate)
         {
            Params p = F({ { "rateBeats", 0.25 }, { "gatePercent", gate } });
            p["steps"] = { 'i', "16" };
            p["rateMode"] = { 'i', "0" };
            for (int k = 0; k < 16; k++)
            {
               const char c = pat[(size_t)k];
               const double v = c == 'x' ? pe.Uniform(0.85, 1.0) : c == 'o' ? pe.Uniform(0.35, 0.55) : 0.0;
               p["note" + std::to_string(k)] = { 'i', std::to_string(note) };
               p["en" + std::to_string(k)] = { 'b', v > 0.0 ? "1" : "0" };
               p["vel" + std::to_string(k)] = { 'f', Num(std::round(v * 100.0) / 100.0) };
            }
            return g.Add("Note Sequencer", pe.Range(1 << 30), p);
         };
         auto place = [&](int node, float gain, double pan)
         {
            if (std::abs(pan) > 0.01)
            {
               const int pn = g.Add("Mixer", pe.Range(1 << 30), F({ { "pan0", pan } }));
               g.Connect(node, pn, false);
               node = pn;
            }
            chans.push_back({ node, gain });
         };
         // 1. Hand drums: congas (low tone + high slap) or bongos (macho + hembra), tumbao- or martillo-like figures.
         {
            static const char* kDrumPat[][2] = {
               { "......x.....x.x.", "....x.......x..." }, // tumbao: open tones on the and of 2 and on 4, slaps on 2 and 4
               { "x.....x.x.....x.", "..x.x.....x.x..." },
               { "x..x..x...x.....", "....x.o.....x.o." },
               { "xooo.ooox.oo.ooo", "....x.......x..." }, // martillo: steady strokes, the open note on the beat
               { "x.....x...x.....", "...x..x....x..x." },
            };
            const bool bongo = pe.Uniform() < 0.35;
            const int pk = pe.Range(5);
            const int low = bongo ? 64 + pe.Range(4) : 52 + pe.Range(5), high = low + (bongo ? 5 : 7);
            for (int d = 0; d < 2; d++)
            {
               const int sq = patSeq(kDrumPat[pk][d], d == 0 ? low : high, 60.0);
               const int hv = headVoice(pe.Uniform(1.5, 3.5), pe.Uniform(15.0, 35.0), bongo ? pe.Uniform(90.0, 160.0) : pe.Uniform(140.0, 260.0),
                                        pe.Uniform(0.35, 0.6), 12000.0);
               g.Connect(sq, hv, true);
               const int slap = noiseVoice(8, pe.Uniform(3000.0, 6000.0), 0.3, pe.Uniform(6.0, 14.0), 0.8);
               g.Connect(sq, slap, true);
               // The drums sit under the shaker and accent, as in percussion loops (low-mid 0.80 against the library's 0.30).
               place(hv, -9.0f, d == 0 ? -0.35 : 0.35);
               place(slap, -15.0f, d == 0 ? -0.35 : 0.35);
            }
         }
         // 2. A shaker (soft attack, high hiss) or a tambourine (hiss plus jingles), on 16ths or offbeats.
         {
            static const char* kShake[] = { "xoxoxoxoxoxoxoxo", "oxoxoxoxoxoxoxox", "x.o.x.o.x.o.x.o.", "..x...x...x...x.", "xoox xoox xoox xoox" };
            std::string pat = four ? kShake[pe.Range(2) ? 3 : 1] : kShake[pe.Range(4)];
            pat.erase(std::remove(pat.begin(), pat.end(), ' '), pat.end());
            pat.resize(16, '.');
            const int sq = patSeq(pat, 84, 30.0);
            const bool tamb = pe.Uniform() < 0.35;
            Params nn = F({ { "volume", 0.8 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", pe.Uniform(4.0, 12.0) },
                            { "decay", tamb ? pe.Uniform(80.0, 140.0) : pe.Uniform(35.0, 70.0) }, { "sustain", 0.0 }, { "release", 30.0 },
                            { "cutoff", pe.Uniform(6000.0, 9000.0) }, { "resonance", 0.2 }, { "voices", 1.0 }, { "keyTrack", 0.0 } });
            nn["filterType"] = { 'i', "8" };
            const int sh = g.Add("Analog", pe.Range(1 << 30), nn);
            g.Connect(sq, sh, true);
            place(sh, tamb ? -9.0f : -7.0f, pe.Uniform(-0.5, 0.5));
            if (tamb)
            {
               Params h = F({ { "transient", 1.0 }, { "decay", pe.Uniform(0.08, 0.14) }, { "stiffness", 1.6 }, { "filterCutoff", 12000.0 },
                              { "filterResonance", 0.1 }, { "volume", 0.7 }, { "width", 0.6 } });
               h["material"] = { 'i', "2" };
               const int jg = g.Add("Metallic", pe.Range(1 << 30), h);
               g.Connect(sq, jg, true);
               place(jg, -17.0f, 0.3);
            }
         }
         // 3. An accent: clave (son or rumba), cowbell, woodblock or rim, the figure the others lock to.
         {
            static const char* kAccent[] = { "x..x..x...x.x...", "x..x...x..x.x...", "x..x..x...x..x..", "x.x.x.xx.x.x.xx.", "..x..x..x...x...", "...x..x....x..x." };
            const int kind = pe.Range(4); // 0 clave, 1 cowbell, 2 woodblock, 3 rim
            const std::string pat = kAccent[four ? 4 + pe.Range(2) : kind == 1 ? 3 : pe.Range(3)];
            Params k;
            CleanWavetable(k);
            k["volume"] = { 'f', "0.9" };
            int note = 96;
            double dec = 40.0;
            if (kind == 1) // the 808 cowbell: two squares a fifth apart, band-limited, ringing a little
            {
               Engine(k, 'a', 0, kSquare, 1, 0.0, 0, 1.0);
               Engine(k, 'b', 0, kSquare, 1, 0.0, 0, 0.8);
               k["b.semi"] = { 'i', "7" };
               k["mix"] = { 'f', "0.5" };
               note = 72 + pe.Range(3), dec = pe.Uniform(150.0, 280.0);
            }
            else
            {
               Engine(k, 'a', 0, kind == 3 ? 0.333 : kSine, 1, 0.0, 0, 1.0);
               k["mix"] = { 'f', "0" };
               note = kind == 0 ? 98 + pe.Range(3) : kind == 2 ? 84 + pe.Range(5) : 76 + pe.Range(4);
               dec = kind == 0 ? pe.Uniform(25.0, 45.0) : kind == 2 ? pe.Uniform(35.0, 70.0) : pe.Uniform(15.0, 30.0);
               k["a.pitchAmount"] = { 'f', Num(kind == 2 ? 2.0 : 1.0) };
               k["a.pitchDecay"] = { 'f', "8" };
            }
            for (char e : { 'a', 'b' })
            {
               EngineFilter(k, e, kind == 1 ? 7 : 0, kind == 1 ? 2600.0 : 12000.0, 0.3, 0.0, 0.0, 0.0, 0.0, 0.0);
               EngineAmp(k, e, 0.0, dec, 0.0, 20.0);
            }
            const int sq = patSeq(pat, note, 30.0);
            const int ac = g.Add("Wavetable", pe.Range(1 << 30), k);
            g.Connect(sq, ac, true);
            place(ac, kind == 1 ? -10.0f : -8.0f, pe.Uniform(-0.3, 0.3));
            if (kind == 3) // the rim's crack
            {
               const int ck = noiseVoice(8, 3500.0, 0.3, 10.0, 0.8);
               g.Connect(sq, ck, true);
               place(ck, -12.0f, 0.0);
            }
         }
      }
      drums = g.Add("Drum Sequencer", seed(), force);
      for (const Replace& r : synthDrums)
         if (r.lane == 0)
            duckKey = r.node; // the synth kick keys the pump when it replaces the sample
      if (full || drumPart)
      {
         chans.push_back({ chain(drums, 0), 0.0f });
         for (const Replace& r : synthDrums)
            chans.push_back({ r.node, r.gain });
      }
      layers++;
   }

   // Bass: the card's 16-step figure as scale degrees, one octave up from the bottom.
   std::string bass = std::string(st.bass) + std::string(16, '.');
   bass.resize(16);
   if (bass.find_first_not_of('.') != std::string::npos)
      bass = ProposeBass(criticRng, mine, bass, lanes[0]);
   if (bass.find_first_not_of('.') != std::string::npos)
   {
      Params p = F({ { "rateBeats", 0.25 }, { "gatePercent", myGate(55.0, 0.25) } });
      p["steps"] = { 'i', "16" };
      p["rateMode"] = { 'i', "0" };
      for (int k = 0; k < 16; k++)
      {
         p["note" + std::to_string(k)] = { 'i', std::to_string(degreeNote(bass[k] != '.' ? Degree(bass[k]) : 0, 1)) };
         p["en" + std::to_string(k)] = { 'b', bass[k] != '.' ? "1" : "0" };
         p["vel" + std::to_string(k)] = { 'f', "0.9" };
      }
      const int sq = g.Add("Note Sequencer", seed(), p);
      const std::string picked = pickVoice(st.synth);
      if (!picked.empty())
      {
         // A designed bass from its own stream (the card's pick is still drawn, so the parts after it keep their draws).
         Rng dv(std::string("voice:bass:") + std::to_string(recipe.seed));
         const double bp = dv.Uniform();
         const int sy = g.Add("Wavetable", seed(), bp < 0.5 ? BankVoice(BankRole::Bass, dv) : DesignVoice(bp < 0.85 ? Voice::SynthBass : Voice::ReeseBass, dv));
         g.Connect(sq, sy, true);
         const int fxChoices[] = { 0, 1, 1, 2 };
         int end = chain(sy, fxChoices[rng.Range(4)], true);
         if (wantBass && (is808Kick || st.sidechain) && drums != 0)
         {
            // Kick-Bass frequency slotting: duck the bass under an 808 sub or sidechain kick so 30-90 Hz stays clean.
            const int dy = g.Add("Dynamics", seed(), F({ { "sidechainExternal", 1.0 }, { "ratio", 4.5 }, { "threshold", -26.0 },
                                                          { "attack", 3.0 }, { "release", 110.0 } }));
            g.Connect(end, dy, false);
            g.Connect(duckKey != 0 ? duckKey : drums, dy, false);
            end = dy;
         }
         layers++;
         // A synth loop is the melodic layer: with the card's bass under it the Synths part measured as a bass (low
         // share 0.93 against the library's 0.08). The bass stays in the full mix only.
         if (full)
         {
            synths.push_back(sy);
            chans.push_back({ end, -4.0f + fullLowDb });
         }
      }
   }

   // A card with no motif (solo piano, a bass-and-drums card) has only its chords: they are its synths.
   if (part == Part::Synths && !st.hasMotif)
      wantChords = true;

   // Chords: a progression from the Chorder, through the card's note effects.
   bool chordFamily = false;
   for (const char* f : kChordFamilies)
      chordFamily = chordFamily || std::string(st.family) == f;
   const bool chordsInFull = st.hasChords || chordFamily || layers == 0;
   if (chordsInFull || wantChords)
   {
      const std::vector<std::string> sizes = Split(st.chordSizes);
      // Pads change chord at most once a bar.
      Params p = F({ { "rateBeats", pads ? std::max(4.0, (double)st.chordBeats) : (plucks && !pluckArp) ? pluckStab : st.chordBeats },
                     { "strumMs", st.chordStrum },
                     { "humanizeTimingMs", st.chordHuman }, { "humanizeVelocity", st.chordHuman } });
      p["root"] = { 'i', std::to_string(root) };
      p["scale"] = { 'i', std::to_string(out.scale) };
      p["chordSize"] = { 'i', sizes.empty() ? "3" : rng.Choice(sizes) };
      const SampleCriticData::StyleDirective& cDir = StyleDirectiveFor(st.id);
      const float calm = 1.0f - cDir.affect[1];
      if (p["chordSize"].second == "3" && (cDir.complexity > 0.62f || calm > 0.72f) &&
          criticRng.Uniform() < 0.35 * (cDir.complexity + calm))
         p["chordSize"] = { 'i', "4" }; // 7th chords for complex or calm styles
      if (mine && user.hasChords && rng.Uniform() < user.chordW)
      {
         // The chord sizes this player actually plays, by how often they play them.
         double r = rng.Uniform();
         for (int i = 0; i < 4; i++)
         {
            r -= user.chordSize[i];
            if (r <= 0.0)
            {
               p["chordSize"] = { 'i', std::to_string(i + 2) };
               break;
            }
         }
      }
      p["progSteps"] = { 'i', std::to_string(st.chordSteps) };
      if (plucks) // a progression from the note bank, on its own stream: pads and the card's draws stay put
      {
         Rng nb(std::string("notes:plucks:") + st.id + ":" + std::to_string(recipe.seed));
         if (nb.Uniform() < 0.6)
         {
            const int* pg = BankProg(out.scale, nb);
            p["progSteps"] = { 'i', "4" };
            for (int i = 0; i < 4; i++)
               p["progDeg" + std::to_string(i)] = { 'i', std::to_string(pg[i]) };
         }
      }
      const int ch = g.Add("Chorder", seed(), p);
      int notes = noteChain(ch, st.notefx);
      const std::string fam = st.family;
      // Plucks always run the chords through the arpeggiator; pads never do.
      if (((fam == "trance" || fam == "retro" || fam == "house") && rng.Uniform() < 0.6 && !pads) || (plucks && pluckArp))
      {
         Params a = F({ { "rateBeats", plucks ? pluckRate : 0.25 }, { "gatePercent", plucks ? pluckGate : 50.0 } });
         a["rateMode"] = { 'i', "0" };
         a["octaves"] = { 'i', plucks ? std::to_string(pluckOctaves) : "2" };
         a["mode"] = { 'i', plucks ? std::to_string(pluckMode) : "0" };
         const int ar = g.Add("Arpeggiator", seed(), a);
         g.Connect(notes, ar, true);
         notes = ar;
      }
      const std::string picked = pickVoice(st.synth);
      if (!picked.empty())
      {
         g.melodic = true;
         const int sy = (pads || plucks) ? g.Add(shapedVoice, seed(), shapedEnv) : g.Add(picked, seed());
         g.melodic = false;
         g.Connect(notes, sy, true);
         if (wantChords)
            synths.push_back(sy);
         if (pads && std::string(shapedVoice) == "Wavetable")
         {
            Rng drift(std::string("drift:") + std::to_string(recipe.seed));
            app::OfflineGraph::Motion m;
            m.node = sy;
            m.key = "a.position";
            const double mid = drift.Uniform(0.25, 0.75), span = drift.Uniform(0.1, 0.35);
            m.lo = (float)(mid - span * 0.5);
            m.hi = (float)(mid + span * 0.5);
            const double periods[] = { 16.0, 24.0, 32.0 };
            m.periodBeats = periods[drift.Range(3)];
            m.phase = drift.Uniform();
            out.motions.push_back(m);
         }
         int pad = chain(sy, 1 + rng.Range(2), !pads);
         if (st.sidechain && drums != 0)
         {
            // The kick ducks the chords: the pump that makes house breathe.
            const int dy = g.Add("Dynamics", seed(), F({ { "sidechainExternal", 1.0 }, { "ratio", 6.0 }, { "threshold", -28.0 },
                                                          { "attack", 5.0 }, { "release", 160.0 } }));
            g.Connect(pad, dy, false);
            g.Connect(duckKey != 0 ? duckKey : drums, dy, false);
            pad = dy;
         }
         if (wantChords)
            chans.push_back({ pad, st.gain });
      }
   }

   // Motif: eight steps asked, then answered with the last three changed.
   if (st.hasMotif && (full || wantMotif))
   {
      int deg = rng.Range(4);
      std::vector<std::pair<int, bool>> seq;
      for (int k = 0; k < 8; k++)
      {
         const int steps[] = { -2, -1, -1, 0, 1, 1, 2 };
         int move = steps[rng.Range(7)];
         double density = st.motifDensity;
         if (mine && user.hasPitch && rng.Uniform() < user.pitchW)
         {
            // The player's own melodic moves, drawn by how often they make them.
            double r = rng.Uniform();
            for (int i = 0; i < 6; i++)
            {
               r -= user.step[i];
               if (r <= 0.0)
               {
                  move = i - 2;
                  break;
               }
            }
         }
         if (mine && user.hasTiming)
            density += (0.25 + 0.75 * user.onset[(k * 2) % 16] - density) * user.timingW;
         deg = std::clamp(deg + move, -2, st.motifRange);
         seq.push_back({ deg, k % 4 == 0 || rng.Uniform() < std::clamp(density * (1.0 + 0.6 * recipe.energy), 0.05, 0.95) });
      }
      std::vector<std::pair<int, bool>> answer(seq.begin(), seq.begin() + 5);
      for (int k = 0; k < 3; k++)
      {
         const int steps[] = { -2, -1, 1, 2, 3 };
         deg = std::clamp(deg + steps[rng.Range(5)], -2, st.motifRange);
         answer.push_back({ deg, rng.Uniform() < 0.85 });
      }
      seq.insert(seq.end(), answer.begin(), answer.end());
      ProposeMotif(criticRng, st, mine, root, out.scale, seq, melNov);
      Rng nb(std::string("notes:motif:") + st.id + ":" + std::to_string(recipe.seed)); // its own stream: the card's draws stay put
      if (!mine && nb.Uniform() < 0.5) // a bar from the note library, folded into the motif's range
      {
         const auto line = BankLine(out.scale, false, nb);
         for (int k = 0; k < 16; k++)
         {
            int d = line[(size_t)k].first;
            while (d > std::max(4, st.motifRange + 2))
               d -= 7;
            seq[(size_t)k] = { d, line[(size_t)k].second > 0.0 };
         }
      }
      Params p = F({ { "rateBeats", st.motifRate }, { "gatePercent", myGate(st.motifGate, st.motifRate) } });
      p["steps"] = { 'i', "16" };
      p["rateMode"] = { 'i', "0" };
      // The card's motif octave sits over its bass in the full mix; played alone as the synths part it would be the
      // bass (owner's review, 2026-10-29: synths "not dark or muddy"), so it rises to octave 3: low share 0.22 -> 0.08, the
      // library's median (item 15). The synths gate's low floor was refitted with it, as it held the old dark sound.
      const int motifOct = part == Part::Synths ? std::max(st.motifOctave, 3) : st.motifOctave;
      for (int k = 0; k < 16; k++)
      {
         p["note" + std::to_string(k)] = { 'i', std::to_string(degreeNote(seq[k].first, motifOct)) };
         p["en" + std::to_string(k)] = { 'b', seq[k].second ? "1" : "0" };
         double v = 0.65 + 0.2 * rng.Uniform();
         if (mine && user.hasVelocity)
            v += (user.vel[(size_t)rng.Range((int)user.vel.size())] - v) * user.velW; // a velocity they have played
         p["vel" + std::to_string(k)] = { 'f', k % 4 == 0 ? "1" : Num(std::round(std::clamp(v, 0.2, 1.0) * 100.0) / 100.0) };
      }
      const int ms = g.Add("Note Sequencer", seed(), p);
      const int mn = noteChain(ms, st.motifNotefx);
      std::string synth = pick(st.motifSynth);
      if (synth.empty())
         synth = Split(st.motifSynth).empty() ? "" : Split(st.motifSynth)[0];
      if (!synth.empty() && TypeOf(synth) != nullptr)
      {
         Rng dv(std::string("voice:motif:") + std::to_string(recipe.seed));
         static const Voice kMotifVoice[] = { Voice::SynthPluck, Voice::SynthPluck, Voice::SawStack, Voice::SquareLead, Voice::FmStack, Voice::Supersaw };
         g.melodic = true;
         const int mv = dv.Range(10); // 0-5 a designed voice, 6-9 a Vital preset's
         const int sy = g.Add("Wavetable", seed(), mv < 6 ? DesignVoice(kMotifVoice[mv], dv) : BankVoice(mv < 8 ? BankRole::Synth : BankRole::Pluck, dv));
         g.melodic = false;
         g.Connect(mn, sy, true);
         synths.push_back(sy);
         int lead = chain(sy, 1, true);
         if (st.sidechain && drums != 0)
         {
            const int dy = g.Add("Dynamics", seed(), F({ { "sidechainExternal", 1.0 }, { "ratio", 4.0 }, { "threshold", -26.0 },
                                                          { "attack", 5.0 }, { "release", 140.0 } }));
            g.Connect(lead, dy, false);
            g.Connect(duckKey != 0 ? duckKey : drums, dy, false);
            lead = dy;
         }
         chans.push_back({ lead, st.motifGain });
      }
   }

   // Bass, fx and foley are built from their own stream and their own kits:
   // each take picks a kit (an instrument or a kind of sound), then draws the
   // kit's ranges and a rhythm. Nothing here moves the other parts' draws.
   const bool bassPart = part == Part::Bass, fxPart = part == Part::Fx, foleyPart = part == Part::Foley;
   const bool keysPart = part == Part::Keys, leadPart = part == Part::Leads, texPart = part == Part::Textures;
   const bool ownSpace = fxPart || foleyPart || keysPart || leadPart || texPart; // sets its own reverb
   double specialWet = 0.2, specialDecay = 1.6;
   if (bassPart || fxPart || foleyPart || keysPart || leadPart || texPart)
   {
      Rng sp(std::string("special:") + std::to_string(recipe.seed));
      const double beatMs = 60000.0 / out.bpm;
      const int loopBeats = recipe.bars * 4;
      const int preBeats = (recipe.bars == 1 ? 1 : 2) * 4;
      auto num = [](double v) { return Num(std::round(v * 1000.0) / 1000.0); };
      auto wt = [&](Params& k, int table, double pos, int filter, double cut, double res)
      {
         CleanWavetable(k);
         k["mix"] = { 'f', "0.45" }; // mix 1 is silent
         k["volume"] = { 'f', "0.9" };
         k["a.table"] = { 'i', std::to_string(table) };
         k["a.position"] = { 'f', num(pos) };
         k["a.filterType"] = { 'i', std::to_string(filter) };
         k["a.cutoff"] = { 'f', num(cut) };
         k["a.resonance"] = { 'f', num(res) };
         k["a.unison"] = { 'i', "1" };
         k["a.detune"] = { 'f', "0" };
         k["a.warpAmount"] = { 'f', "0" };
         k["fmDepth"] = { 'f', "0" };
         k["b.on"] = { 'b', "0" };
         k["a.octave"] = { 'i', "0" };
         k["a.ampAttack"] = { 'f', "1" };
         k["a.filterAmount"] = { 'f', "0" };
         k["a.pitchAmount"] = { 'f', "0" };
      };
      // Sixteen steps of notes: `vel[k] > 0` plays `note[k]` at that velocity.
      auto sequence = [&](const std::vector<double>& vel, const std::vector<int>& note, double rate, double gate, int steps)
      {
         Params q = F({ { "rateBeats", rate }, { "gatePercent", gate } });
         q["steps"] = { 'i', std::to_string(steps) };
         q["rateMode"] = { 'i', "0" };
         for (int k = 0; k < steps; k++)
         {
            q["note" + std::to_string(k)] = { 'i', std::to_string(note[(size_t)k]) };
            q["en" + std::to_string(k)] = { 'b', vel[(size_t)k] > 0.0 ? "1" : "0" };
            q["vel" + std::to_string(k)] = { 'f', num(std::max(0.0, vel[(size_t)k])) };
         }
         return g.Add("Note Sequencer", sp.Range(1 << 30), q);
      };
      auto sweep = [&](int node, const char* key, double lo, double hi, double periodBeats, double phase)
      {
         app::OfflineGraph::Motion m;
         m.node = node;
         m.key = key;
         m.lo = (float)lo;
         m.hi = (float)hi;
         m.periodBeats = periodBeats;
         m.phase = phase - std::floor(phase);
         out.motions.push_back(m);
      };

      const SampleCriticData::StyleDirective& dir = StyleDirectiveFor(st.id);

      if (bassPart)
      {
         // A bass is a sound and a way of playing it (owner's review, 2026-10-29: "rhythmic, melodic, long, wobbly,
         // funky"; Lunacy, eMastered and Beat Kitchen's bass guides). The card's learned weights pick the sound; the
         // sound picks the roles it is played in; each role writes its own line over a short progression.
         enum Role { kLong, kRolling, kMelodic, kFunk, kWobble, kTrap, kAcid };
         const int kind = PickDirectiveKind(sp, dir.bassW); // 0 sub, 1 808, 2 reese, 3 plucked, 4 fm, 5 acid
         static const int kRoles[6][4] = { { kLong, kLong, kRolling, kMelodic }, { kTrap, kTrap, kTrap, kLong },
                                           { kLong, kWobble, kWobble, kRolling }, { kFunk, kFunk, kMelodic, kRolling },
                                           { kWobble, kRolling, kFunk, kMelodic }, { kAcid, kAcid, kAcid, kRolling } };
         int role = kRoles[kind][sp.Range(4)];
         if (recipe.bassRole >= 0)
            role = std::clamp(recipe.bassRole, (int)kLong, (int)kAcid);
         // Progressions as scale degrees, one chord each: I-vi-IV-V, I-V-vi-IV, i-VII-VI-VII, i-iv-v-i, I-IV, i-VI ...
         static const int kProg[][4] = { { 0, 5, 3, 4 }, { 0, 4, 5, 3 }, { 0, 6, 5, 6 }, { 0, 3, 4, 0 },
                                         { 0, 0, 3, 4 }, { 0, 5, 0, 4 }, { 0, 3, 0, 3 }, { 0, 5, 6, 4 } };
         const int* prog = kProg[sp.Range(8)];
         Rng nb(std::string("notes:bass:") + st.id + ":" + std::to_string(recipe.seed)); // its own stream: every other draw stays put
         if (nb.Uniform() < 0.6)
            prog = BankProg(out.scale, nb);
         const int chords = loopBeats >= 8 ? 4 : 2;
         std::vector<double> vel(16, 0.0);
         std::vector<int> note(16, 0);
         int steps = 16;
         double rate = 0.25, gate = 70.0;
         auto hit = [&](int i, int deg, int oct, double v) {
            vel[(size_t)i] = std::clamp(v + sp.Uniform(-dir.velSpread, dir.velSpread) * 0.5, 0.2, 1.0);
            note[(size_t)i] = degreeNote(deg, oct);
         };
         switch (role)
         {
         case kLong: // one held note per chord
         case kWobble: // held notes; the movement is a synced sweep on the filter
            steps = role == kLong ? chords : std::min(16, loopBeats / 2);
            rate = (double)loopBeats / steps;
            gate = 96.0;
            for (int i = 0; i < steps; i++)
               hit(i, prog[(i * chords / steps) % 4], kind == 1 ? 0 : 1, i == 0 ? 1.0 : 0.85);
            break;
         case kRolling: // driving 16ths around the kick: offbeat 8ths, a gallop, or every 16th but the downbeat
         {
            static const char* kRoll[] = { "..x...x...x...x.", ".xxx.xxx.xxx.xxx", "x.xx.xxx.xx.x.xx", "..xx..xx..xx..xx", ".x.x.x.x.x.x.x.x" };
            const char* pat = kRoll[sp.Range(5)];
            gate = sp.Uniform(45.0, 70.0);
            const int turn = sp.Uniform() < 0.6 ? (sp.Range(2) ? 4 : 7) : 0; // the last beat moves to the fifth or octave
            for (int i = 0; i < 16; i++)
               if (pat[i] == 'x')
                  hit(i, i >= 12 ? turn : 0, 1, (i % 4 == 2) ? 0.95 : 0.7);
            break;
         }
         case kMelodic: // a walking line: the chord's root, its tones, and a step into the next chord
         {
            if (nb.Uniform() < 0.6) // or a bar a bass player in the note library plays
            {
               gate = sp.Uniform(55.0, 85.0);
               const auto line = BankLine(out.scale, true, nb);
               for (int i = 0; i < 16; i++)
                  if (line[(size_t)i].second > 0.0)
                     hit(i, line[(size_t)i].first % 14, 1, 0.45 + 0.55 * line[(size_t)i].second);
               break;
            }
            static const char* kWalk[] = { "x.x.x.xx", "x..x.xx.", "x.xxx.x.", "x..x..x.", "x.x..xx." };
            rate = loopBeats >= 8 ? 0.5 : 0.25;
            gate = sp.Uniform(65.0, 90.0);
            const int per = 16 / 2;
            for (int c = 0; c < 2; c++)
            {
               const char* pat = kWalk[sp.Range(5)];
               const int r0 = prog[(c * 2) % 4], next = prog[((c + 1) * 2) % 4];
               int last = -1;
               for (int j = 0; j < per; j++)
                  if (pat[j] == 'x')
                     last = j;
               for (int j = 0; j < per; j++)
                  if (pat[j] == 'x')
                  {
                     static const int kTone[] = { 0, 2, 4, 7, 4, 2 };
                     int d = j == 0 ? r0 : r0 + kTone[sp.Range(6)];
                     if (j == last && j != 0 && sp.Uniform() < 0.6)
                        d = next + (sp.Range(2) ? 1 : -1); // approach note
                     hit(c * per + j, d, 1, j == 0 ? 1.0 : 0.8);
                  }
            }
            break;
         }
         case kFunk: // root on the one, octave pops on the 16ths, ghost notes, the flat seventh and the fifth
         {
            static const char* kFunkPat[] = { "R..gO.R.g.R.O5.7", "R.gR..O.R.g.5.O.", "R..R.gO..R.gO.7.", "R.O.gR.O.R.gO.5g", "R...O.gRR.O.g5O." };
            std::string pat = kFunkPat[sp.Range(5)];
            for (int i = 1; i < 16; i++) // a little variation per take, never on the one
               if (sp.Uniform() < 0.12)
                  std::swap(pat[(size_t)i], pat[(size_t)(1 + sp.Range(15))]);
            gate = sp.Uniform(35.0, 55.0);
            for (int i = 0; i < 16; i++)
               switch (pat[(size_t)i])
               {
               case 'R': hit(i, 0, 1, i == 0 ? 1.0 : 0.85); break;
               case 'O': hit(i, 7, 1, 0.9); break;
               case '5': hit(i, 4, 1, 0.75); break;
               case '7': hit(i, 6, 1, 0.75); break;
               case 'g': hit(i, 0, 1, 0.28); break; // a muted ghost
               }
            break;
         }
         case kTrap: // 808: syncopated hits that ring, a slide up to the octave at the end
         {
            static const char* kTrapPat[] = { "x..x..x...x.x...", "x.....x.x..x....", "x..x......x..x..", "x......xx.....x.", "x..x...x..x....." };
            const char* pat = kTrapPat[sp.Range(5)];
            rate = loopBeats >= 8 && sp.Uniform() < 0.4 ? 0.5 : 0.25;
            gate = 95.0;
            int lastHit = 0;
            for (int i = 0; i < 16; i++)
               if (pat[i] == 'x')
                  lastHit = i;
            for (int i = 0; i < 16; i++)
               if (pat[i] == 'x')
                  hit(i, i == lastHit && sp.Uniform() < 0.5 ? (sp.Range(2) ? 7 : 4) : (i >= 8 ? prog[1] : 0), 0, i == 0 ? 1.0 : 0.9);
            break;
         }
         default: // acid: a busy 303 line, accents and slides, octave jumps
         {
            std::string raw(16, '.');
            const double density = sp.Uniform(0.55, 0.85);
            static const int kDeg[] = { 0, 0, 0, 0, 2, 3, 4, 6, 7 };
            for (int i = 0; i < 16; i++)
               if (i == 0 || sp.Uniform() < density)
                  raw[(size_t)i] = (char)('0' + std::clamp(kDeg[sp.Range(9)], 0, 7));
            raw = ProposeBass(criticRng, mine, raw, lanes[0]);
            gate = sp.Uniform(80.0, 100.0); // long gates are what lets a step slide into the next
            for (int i = 0; i < 16; i++)
               if (raw[(size_t)i] != '.')
                  hit(i, Degree(raw[(size_t)i]), sp.Uniform() < 0.3 ? 2 : 1, sp.Uniform() < 0.3 || i == 0 ? 1.0 : sp.Uniform(0.45, 0.65));
            break;
         }
         }

         // The sound. Cutoffs sit where the note's harmonics speak on a laptop (a bass that is only sub vanishes there),
         // and a filter envelope gives each note its bite.
         Params k;
         CleanWavetable(k);
         k["volume"] = { 'f', "0.9" };
         k["b.on"] = { 'b', "0" };
         int filter = 2; // LP24
         double cut = 1000.0, res = 0.1, amt = 1.0, fd = 250.0, fs = 0.3, aa = 1.0, ad = 400.0, as = 0.85, ar = 80.0;
         switch (kind)
         {
         case 0: // sub: a sine with a quiet triangle on top, so the note is heard and not only felt
            Engine(k, 'a', 0, kSine, 1, 0.0, 0, 1.0);
            Engine(k, 'b', 0, 0.333, 1, 0.0, 0, 0.35);
            k["mix"] = { 'f', "0.35" };
            cut = sp.Uniform(900.0, 1400.0), amt = 0.0, as = sp.Uniform(0.8, 1.0), ar = sp.Uniform(60.0, 160.0);
            break;
         case 1: // 808: a pitch-dropped boom that rings
            Engine(k, 'a', 0, sp.Uniform() < 0.5 ? kSine : 0.2, 1, 0.0, 0, 1.0);
            k["a.pitchAmount"] = { 'f', num(sp.Uniform(7.0, 14.0)) };
            k["a.pitchDecay"] = { 'f', num(sp.Uniform(30.0, 80.0)) };
            k["glide"] = { 'f', num(sp.Uniform(0.06, 0.12)) };
            cut = 2500.0, amt = 0.0, ad = sp.Uniform(600.0, 1400.0), as = 0.0, ar = 90.0;
            break;
         case 2: // reese: saws beating against each other over a clean sine on the note
            Engine(k, 'a', sp.Range(2) ? 24 : 0, sp.Uniform(0.55, 0.75), sp.Range(2) ? 2 : 3, sp.Uniform(16.0, 32.0), 0, 1.0);
            if (k["a.table"].second == "0")
               k["a.position"] = { 'f', num(kSaw) };
            k["a.stereoWidth"] = { 'f', "0.4" };
            Engine(k, 'b', 0, kSine, 1, 0.0, 0, 0.85); // an octave under a bass note is under 40 Hz
            k["mix"] = { 'f', "0.55" };
            cut = sp.Uniform(500.0, 1000.0), res = sp.Uniform(0.1, 0.25), amt = 1.0, fd = 400.0, fs = 0.4, ar = 120.0;
            break;
         case 3: // plucked or slap: the string's frames and a filter that snaps shut
            Engine(k, 'a', sp.Range(2) ? 54 : 50, sp.Uniform(0.1, 0.6), 1, 0.0, 0, 1.0);
            Engine(k, 'b', 0, kSine, 1, 0.0, 0, 0.8); // the body under the snap: bass loops are mostly fundamental
            k["mix"] = { 'f', "0.5" };
            cut = sp.Uniform(500.0, 900.0), amt = sp.Uniform(2.5, 3.5), fd = sp.Uniform(120.0, 260.0), fs = 0.0;
            ad = sp.Uniform(250.0, 500.0), as = 0.0, ar = 90.0;
            break;
         case 4: // fm: a bright, hollow growl that closes
            Engine(k, 'a', sp.Range(2) ? 12 : 32, sp.Uniform(0.1, 0.5), 1, 0.0, 0, 1.0);
            Engine(k, 'b', 0, kSine, 1, 0.0, 0, 0.8);
            k["mix"] = { 'f', "0.5" };
            cut = sp.Uniform(700.0, 1400.0), amt = 1.5, fd = 200.0, fs = 0.3, ad = sp.Uniform(250.0, 500.0), as = sp.Uniform(0.3, 0.6), ar = 100.0;
            break;
         default: // acid: a saw or square into a resonant ladder-like low-pass with a fast, deep sweep
            Engine(k, 'a', 0, sp.Uniform() < 0.6 ? kSaw : kSquare, 1, 0.0, 0, 1.0);
            Engine(k, 'b', 0, kSine, 1, 0.0, 0, 0.6);
            k["mix"] = { 'f', "0.4" };
            cut = sp.Uniform(300.0, 700.0), res = sp.Uniform(0.5, 0.75), amt = sp.Uniform(3.0, 4.5), fd = sp.Uniform(110.0, 240.0), fs = 0.05;
            ad = 300.0, as = 0.6, ar = 70.0;
            k["glide"] = { 'f', num(sp.Uniform(0.05, 0.07)) }; // the 303's slide is about 60 ms
            break;
         }
         if (k["b.on"].second == "0")
            k["mix"] = { 'f', "0" };
         for (char e : { 'a', 'b' })
         {
            EngineFilter(k, e, filter, cut, res, amt, 0.0, fd, fs, 80.0);
            EngineAmp(k, e, aa, ad, as, ar);
         }
         if (kind == 1) // the 808's sine takes no filter
            k["b.filterType"] = k["a.filterType"] = { 'i', "0" };
         const int sq = sequence(vel, note, rate, gate, steps);
         const int sy = g.Add("Wavetable", sp.Range(1 << 30), k);
         g.Connect(sq, sy, true);
         synths.push_back(sy);
         chans.push_back({ sy, 0.0f });
         if (role == kWobble)
         {
            // The wobble: the filter swept in time, a quarter, eighth, triplet or sixteenth.
            static const double kWob[] = { 1.0, 0.5, 0.5, 2.0 / 3.0, 0.25 };
            const double period = kWob[sp.Range(5)], lo = sp.Uniform(250.0, 450.0), hi = sp.Uniform(2500.0, 4500.0);
            sweep(sy, "a.cutoff", lo, hi, period, 0.0);
            if (k["b.on"].second == "1")
               sweep(sy, "b.cutoff", lo, hi, period, 0.0);
         }
         // The click: a separate short, high layer on each note's start (a sine pitched down from four octaves up in a
         // few ms, high-passed), as bass and kick designers layer their transients. It is what lets a bass cut through.
         // High-pass and decay sit SubLab's attack samples (item 16): 1-5 ms, centroid ~470 Hz, not the 2 kHz it had.
         if (sp.Uniform() < (kind == 1 ? 0.35 : kind == 0 ? 0.5 : 0.6))
         {
            Params c;
            CleanWavetable(c);
            c["volume"] = { 'f', "0.9" };
            c["mix"] = { 'f', "0" };
            c["glide"] = { 'f', "0" };
            Engine(c, 'a', 0, sp.Uniform() < 0.5 ? kSine : 0.333, 1, 0.0, 1, 1.0);
            c["a.pitchAmount"] = { 'f', num(sp.Uniform(30.0, 44.0)) };
            c["a.pitchDecay"] = { 'f', num(sp.Uniform(2.0, 6.0)) };
            EngineFilter(c, 'a', 4, sp.Uniform(250.0, 700.0), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0); // HP12
            EngineAmp(c, 'a', 0.0, sp.Uniform(3.0, 8.0), 0.0, 6.0);
            const int ck = g.Add("Wavetable", sp.Range(1 << 30), c);
            g.Connect(sq, ck, true);
            chans.push_back({ ck, (float)sp.Uniform(-12.0, -7.0) });
         }
      }
      else if (fxPart)
      {
         const int kind = sp.Range(7);
         const double period = 2.0 * loopBeats;
         // Phase that puts the preroll's end at the trough (rising) or the crest (falling) of the sweep.
         const double rise = 0.75 - preBeats / period, fall = 0.25 - preBeats / period;
         auto noiseVoice = [&](double attackMs, double releaseMs)
         {
            Params n = F({ { "volume", 0.8 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 },
                           { "attack", std::min(4000.0, attackMs) }, { "decay", 200.0 }, { "sustain", 1.0 }, { "release", releaseMs },
                           { "cutoff", 18000.0 }, { "resonance", 0.1 }, { "voices", 1.0 }, { "keyTrack", 0.0 } });
            n["filterType"] = { 'i', "0" };
            return g.Add("Analog", sp.Range(1 << 30), n);
         };
         auto hold = [&](double beats)
         {
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 60);
            vel[0] = 1.0;
            return sequence(vel, note, beats, 100.0, 1);
         };
         specialWet = sp.Uniform(0.25, 0.55);
         specialDecay = sp.Uniform(2.0, 6.0);
         if (kind <= 1) // riser / downlifter: noise through a band-pass that sweeps, over the whole loop
         {
            const bool up = kind == 0;
            const int nv = noiseVoice(up ? loopBeats * beatMs * 0.9 : 30.0, up ? 200.0 : loopBeats * beatMs * 0.5);
            g.Connect(hold(loopBeats), nv, true);
            Params f = F({ { "freq", 1000.0 }, { "q", sp.Uniform(1.5, 6.0 ) }, { "mix", 1.0 } });
            f["type"] = { 'f', "6" }; // effect params are floats: an "i" key is never read
            const int af = g.Add("Audio Filter", sp.Range(1 << 30), f);
            g.Connect(nv, af, false);
            sweep(af, "freq", sp.Uniform(200.0, 500.0), sp.Uniform(5000.0, 12000.0), period, up ? rise : fall);
            // A tonal line under the noise: a saw whose pitch the loop climbs.
            Params k;
            wt(k, 24, 0.6, 1, 2500, 0.0);
            k["a.unison"] = { 'i', "4" };
            k["a.detune"] = { 'f', "25" };
            k["a.ampAttack"] = { 'f', num(up ? loopBeats * beatMs * 0.8 : 20.0) };
            k["a.ampSustain"] = { 'f', "1" };
            k["a.ampRelease"] = { 'f', "300" };
            k["volume"] = { 'f', "0.3" };
            const int tone = g.Add("Wavetable", sp.Range(1 << 30), k);
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, degreeNote(sp.Range(3) * 2, 1));
            vel[0] = 1.0;
            g.Connect(sequence(vel, note, loopBeats, 100.0, 1), tone, true);
            sweep(tone, "a.cutoff", 300.0, 9000.0, period, up ? rise : fall);
            synths.push_back(tone);
            chans.push_back({ af, 0.0f });
            chans.push_back({ tone, -9.0f });
         }
         else if (kind == 2) // impact: a pitch-dropped boom with a noise burst, once at the start
         {
            Params k;
            wt(k, 10, 0.1, 1, 900, 0.0);
            k["a.pitchAmount"] = { 'f', num(sp.Uniform(18.0, 40.0)) };
            k["a.pitchDecay"] = { 'f', num(sp.Uniform(60.0, 250.0)) };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(1200.0, 3500.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "400" };
            const int boom = g.Add("Wavetable", sp.Range(1 << 30), k);
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 24 + sp.Range(8));
            vel[0] = 1.0;
            g.Connect(sequence(vel, note, loopBeats, 30.0, 1), boom, true);
            Params nn = F({ { "volume", 0.7 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 1.0 },
                            { "decay", sp.Uniform(250.0, 1200.0) }, { "sustain", 0.0 }, { "release", 300.0 }, { "cutoff", sp.Uniform(1500.0, 9000.0) },
                            { "voices", 1.0 }, { "keyTrack", 0.0 } });
            nn["filterType"] = { 'i', "1" };
            const int burst = g.Add("Analog", sp.Range(1 << 30), nn);
            g.Connect(sequence(vel, std::vector<int>(16, 60), loopBeats, 30.0, 1), burst, true);
            chans.push_back({ boom, 0.0f });
            chans.push_back({ burst, -6.0f });
         }
         else if (kind == 3) // whoosh: a pass-by, noise swept up and down over about a second, panned across
         {
            const double secs = sp.Uniform(0.5, 1.2);
            const double beats = secs * 1000.0 / beatMs;
            const int nv = noiseVoice(secs * 450.0, secs * 500.0);
            // The pass-by lands at the start of the loop so the tail rings into the next.
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 60);
            vel[0] = 1.0;
            g.Connect(sequence(vel, note, loopBeats, std::clamp(secs * 1000.0 / (loopBeats * beatMs) * 100.0, 5.0, 100.0), 1), nv, true);
            Params f = F({ { "freq", 800.0 }, { "q", sp.Uniform(0.8, 2.5) }, { "mix", 1.0 } });
            f["type"] = { 'f', "6" }; // effect params are floats: an "i" key is never read
            const int af = g.Add("Audio Filter", sp.Range(1 << 30), f);
            g.Connect(nv, af, false);
            const double lo = sp.Uniform(250.0, 600.0), hi = sp.Uniform(4000.0, 9000.0);
            sweep(af, "freq", lo, hi, std::max(0.25, beats * 2.0), 0.75 - preBeats / std::max(0.25, beats * 2.0));
            const int pn = g.Add("Stereo", sp.Range(1 << 30), F({ { "width", 1.0 } }));
            g.Connect(af, pn, false);
            sweep(pn, "pan", -0.9, 0.9, std::max(0.25, beats * 2.0), 0.75 - preBeats / std::max(0.25, beats * 2.0));
            chans.push_back({ pn, 0.0f });
         }
         else if (kind == 4) // zaps: falling or rising lasers, a few to a bar
         {
            Params k;
            wt(k, sp.Range(2) ? 12 : 37, sp.Uniform(0.1, 0.8), 1, 8000, 0.0);
            k["a.pitchAmount"] = { 'f', num(sp.Uniform(0.0, 1.0) < 0.5 ? sp.Uniform(-36.0, -14.0) : sp.Uniform(14.0, 30.0)) };
            k["a.pitchDecay"] = { 'f', num(sp.Uniform(80.0, 300.0)) };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(150.0, 500.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "80" };
            const int zp = g.Add("Wavetable", sp.Range(1 << 30), k);
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 72);
            for (int i = 0; i < 16; i++)
               if (sp.Uniform() < 0.2)
               {
                  vel[(size_t)i] = sp.Uniform(0.5, 1.0);
                  note[(size_t)i] = 60 + sp.Range(24);
               }
            vel[0] = 1.0;
            g.Connect(sequence(vel, note, 0.5, 25.0, 16), zp, true);
            chans.push_back({ zp, 0.0f });
         }
         else if (kind == 6) // endless riser: three octave-spaced voices climbing, fading in low and out high
         {
            for (int v = 0; v < 3; v++)
            {
               Params k;
               wt(k, v == 0 ? 24 : 0, 0.5, 1, 3500, 0.0);
               k["a.unison"] = { 'i', "2" };
               k["a.detune"] = { 'f', "10" };
               k["a.ampAttack"] = { 'f', "40" };
               k["a.ampSustain"] = { 'f', "1" };
               k["a.ampRelease"] = { 'f', "120" };
               k["glide"] = { 'f', "0.25" };
               k["volume"] = { 'f', "0.4" };
               const int sv = g.Add("Wavetable", sp.Range(1 << 30), k);
               std::vector<double> vel(16, 0.0), note0(0);
               std::vector<int> note(16, 48);
               const int steps = 8;
               for (int i = 0; i < steps; i++)
               {
                  vel[(size_t)i] = 1.0;
                  // Each voice climbs one octave over the loop, starting a third of the way further on.
                  note[(size_t)i] = 40 + ((i * 12 / steps + v * 12 / 3 * 3) % 36);
               }
               g.Connect(sequence(vel, note, loopBeats / (double)steps, 100.0, steps), sv, true);
               // Loudest mid-climb, silent at the edges: the ear loses the seam.
               sweep(sv, "volume", 0.02, 0.45, loopBeats * 1.0, 0.25 + v / 3.0 - preBeats / (double)loopBeats);
               synths.push_back(sv);
               chans.push_back({ sv, -4.0f });
            }
         }
         else // sparkle: high metallic ticks scattered, bell-like, over a long tail
         {
            Params m = F({ { "transient", sp.Uniform(0.3, 1.2) }, { "decay", sp.Uniform(0.6, 2.5) }, { "stiffness", sp.Uniform(0.4, 1.6) },
                           { "filterCutoff", sp.Uniform(5000.0, 15000.0) }, { "volume", 0.8 }, { "width", 0.8 } });
            m["material"] = { 'i', std::to_string(sp.Range(8)) };
            const int mt = g.Add("Metallic", sp.Range(1 << 30), m);
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 84);
            for (int i = 0; i < 16; i++)
               if (sp.Uniform() < 0.3)
               {
                  vel[(size_t)i] = sp.Uniform(0.4, 1.0);
                  note[(size_t)i] = 72 + sp.Range(24);
               }
            vel[0] = 1.0;
            g.Connect(sequence(vel, note, 0.5, 30.0, 16), mt, true);
            chans.push_back({ mt, 0.0f });
         }
      }
      else if (keysPart || leadPart)
      {
         Params k;
         if (keysPart)
         {
            // Chords struck on an instrument: a tine piano, organ, mallet keys, a clav or a plucked-string piano.
            int kind = PickDirectiveKind(sp, dir.keysW);
            // A named mood picks the instrument too (its own stream, so the notes do not move): bright tines,
            // mallets and clav for happy; clav and organ for tense; felt piano and pipes for sad; felt, vibes
            // and tines for calm.
            if (g.Quadrant() != Graph::kNoQuad)
            {
               static const int kByQuad[5][3] = { { 0, 0, 0 }, { 0, 3, 4 }, { 4, 1, 4 }, { 2, 5, 2 }, { 2, 3, 0 } };
               Rng vr(std::string("affect-voice:") + std::to_string(recipe.seed));
               kind = kByQuad[g.Quadrant()][vr.Range(3)];
            }
            double rate = sp.Uniform() < 0.5 ? 2.0 : 4.0, strum = sp.Uniform(4.0, 28.0);
            switch (kind)
            {
            case 0: // electric piano: tine table, bell-like top, long decay
               wt(k, 17, sp.Uniform(0.0, 0.5), 1, sp.Uniform(3000.0, 6500.0), 0.0);
               k["a.ampDecay"] = { 'f', num(sp.Uniform(900.0, 1800.0)) };
               k["a.ampSustain"] = { 'f', num(sp.Uniform(0.0, 0.2)) };
               k["a.ampRelease"] = { 'f', "350" };
               break;
            case 1: // organ: drawbars, no decay, sits on the key
               wt(k, 14, sp.Uniform(0.0, 0.6), 1, sp.Uniform(3500.0, 7000.0), 0.0);
               k["a.ampAttack"] = { 'f', "6" };
               k["a.ampSustain"] = { 'f', "1" };
               k["a.ampRelease"] = { 'f', "90" };
               break;
            case 2: // felt piano: a plucked string with a soft filter on the strike
               wt(k, 50, sp.Uniform(0.05, 0.4), 2, sp.Uniform(1200.0, 2600.0), 0.0);
               k["a.filterAmount"] = { 'f', num(sp.Uniform(1.5, 2.8)) };
               k["a.filterDecay"] = { 'f', num(sp.Uniform(250.0, 600.0)) };
               k["a.filterSustain"] = { 'f', "0" };
               k["a.ampDecay"] = { 'f', num(sp.Uniform(1100.0, 2400.0)) };
               k["a.ampSustain"] = { 'f', "0" };
               k["a.ampRelease"] = { 'f', "300" };
               break;
            case 3: // vibes: struck bars that ring
               wt(k, 49, sp.Uniform(0.0, 0.5), 1, sp.Uniform(4000.0, 8000.0), 0.0);
               k["a.ampDecay"] = { 'f', num(sp.Uniform(1500.0, 2800.0)) };
               k["a.ampSustain"] = { 'f', "0" };
               k["a.ampRelease"] = { 'f', "400" };
               break;
            case 4: // clav: a short, hard, nasal chord stab
               wt(k, 50, sp.Uniform(0.5, 0.85), 1, sp.Uniform(1500.0, 3200.0), 0.15);
               k["a.filterAmount"] = { 'f', num(sp.Uniform(1.5, 2.5)) };
               k["a.filterDecay"] = { 'f', num(sp.Uniform(60.0, 140.0)) };
               k["a.filterSustain"] = { 'f', "0" };
               k["a.ampDecay"] = { 'f', num(sp.Uniform(140.0, 320.0)) };
               k["a.ampSustain"] = { 'f', "0" };
               k["a.ampRelease"] = { 'f', "60" };
               rate = sp.Uniform() < 0.5 ? 1.0 : 0.5;
               strum = sp.Uniform(0.0, 8.0);
               break;
            default: // pipes: a church organ, slow to speak
               wt(k, 29, sp.Uniform(0.0, 0.6), 1, sp.Uniform(2500.0, 5500.0), 0.0);
               k["a.ampAttack"] = { 'f', num(sp.Uniform(40.0, 160.0)) };
               k["a.ampSustain"] = { 'f', "1" };
               k["a.ampRelease"] = { 'f', num(sp.Uniform(300.0, 900.0)) };
               rate = 4.0;
               break;
            }
            k["a.octave"] = { 'i', "0" };
            k["a.unison"] = { 'i', std::to_string(1 + sp.Range(2)) };
            k["a.detune"] = { 'f', num(sp.Uniform(2.0, 9.0)) };
            // How the keys are played (owner's review, 2026-10-29: "note effects, chord progressions, melody, arps"):
            // held chords through a progression, a comped rhythm that re-strikes and pushes the change onto an offbeat,
            // an arpeggio over the progression, or held chords with a top line. Organs and pipes hold; a clav comps.
            enum { kHeld, kComp, kArp, kTopLine };
            int play = sp.Range(4);
            if ((kind == 1 || kind == 5) && play == kArp)
               play = kTopLine;
            if (kind == 4 && play == kHeld)
               play = kComp;
            if (recipe.keysPlay >= 0)
               play = std::clamp(recipe.keysPlay, (int)kHeld, (int)kTopLine);
            // Degrees per chord: I-V-vi-IV, I-vi-IV-V, ii-V-I-I, I-IV-V-IV, vi-IV-I-V, i-VII-VI-v, I-iii-IV-V, IV-V-I-vi.
            static const int kKeysProg[][4] = { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 1, 4, 0, 0 }, { 0, 3, 4, 3 },
                                                { 5, 3, 0, 4 }, { 0, 6, 5, 4 }, { 0, 2, 3, 4 }, { 3, 4, 0, 5 } };
            const int pi = sp.Range(8);
            const int* pk = kKeysProg[pi];
            Rng nb(std::string("notes:keys:") + st.id + ":" + std::to_string(recipe.seed)); // its own stream: every other draw stays put
            if (nb.Uniform() < 0.6)
               pk = BankProg(out.scale, nb);
            const int nChords = loopBeats >= 8 ? 4 : 2;
            std::vector<int> cells;
            if (play == kComp)
            {
               // Eight cells: a strikes the first chord, b the second, - lets it ring.
               static const char* kCompPat[] = { "a--a-b-b", "a-a--b-b", "a--a--b-", "a-ab--b-", "a--ab-b-", "a-a-b-bb" };
               const char* pat = kCompPat[sp.Range(6)];
               rate = loopBeats >= 8 ? 1.0 : 0.5;
               for (int i = 0; i < 8; i++)
                  cells.push_back(pat[i] == 'a' ? pk[0] : pat[i] == 'b' ? pk[1] : -1);
               strum = std::min(strum, 10.0);
            }
            else
            {
               rate = (double)loopBeats / nChords;
               for (int i = 0; i < nChords; i++)
                  cells.push_back(pk[i]);
            }
            Params p = F({ { "rateBeats", rate }, { "strumMs", strum }, { "humanizeTimingMs", sp.Uniform(3.0, 14.0) }, { "humanizeVelocity", sp.Uniform(5.0, 20.0) } });
            p["root"] = { 'i', std::to_string(root) };
            p["scale"] = { 'i', std::to_string(out.scale) };
            p["chordSize"] = { 'i', pi == 2 ? "4" : std::to_string(3 + sp.Range(2)) }; // the ii-V-I wants its sevenths
            if (recipe.chordSize > 0)
               p["chordSize"] = { 'i', std::to_string(std::clamp(recipe.chordSize, 2, 6)) };
            p["mode"] = { 'i', "1" };
            p["progSteps"] = { 'i', std::to_string(cells.size()) };
            for (size_t i = 0; i < cells.size(); i++)
               p["progDeg" + std::to_string(i)] = { 'i', std::to_string(cells[i]) };
            const int ch = g.Add("Chorder", sp.Range(1 << 30), p);
            int notes = ch;
            if (play == kArp)
            {
               static const int kArpMode[] = { 0, 2, 2, 6, 11, 13, 1, 8 }; // up, up-down, random, spread, stairs, down, repeat
               Params a = F({ { "rateBeats", sp.Uniform() < 0.7 ? 0.25 : 0.5 }, { "gatePercent", sp.Uniform(45.0, 85.0) } });
               a["rateMode"] = { 'i', "0" };
               a["octaves"] = { 'i', std::to_string(1 + sp.Range(2)) };
               a["mode"] = { 'i', std::to_string(kArpMode[sp.Range(8)]) };
               if (recipe.arpOctaves > 0)
                  a["octaves"] = { 'i', std::to_string(std::clamp(recipe.arpOctaves, 1, 4)) };
               if (recipe.arpMode >= 0)
                  a["mode"] = { 'i', std::to_string(recipe.arpMode) };
               notes = g.Add("Arpeggiator", sp.Range(1 << 30), a);
               g.Connect(ch, notes, true);
            }
            g.melodic = true;
            const int sy = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.melodic = false;
            g.Connect(notes, sy, true);
            synths.push_back(sy);
            chans.push_back({ sy, play == kTopLine ? -2.0f : 0.0f });
            if (play == kTopLine)
            {
               // The right hand: a line above the chords that lands on each chord's tones on the beat.
               std::vector<double> vel(16, 0.0);
               std::vector<int> note(16, 0);
               const double lineRate = loopBeats >= 8 ? 0.5 : 0.25;
               const int per = 16 / nChords;
               int d = 4;
               for (int i = 0; i < 16; i++)
               {
                  const int c = pk[std::min(nChords - 1, i / per)];
                  const bool beat = i % 2 == 0;
                  if (!(i == 0 || sp.Uniform() < (beat ? 0.7 : 0.3)))
                     continue;
                  if (beat || i % per == 0)
                  {
                     static const int kTone[] = { 0, 2, 4, 7 };
                     const int t = c + kTone[sp.Range(4)];
                     d = std::abs(t - d) <= 4 ? t : t + (t < d ? 7 : -7); // the nearest chord tone, not a leap
                  }
                  else
                     d += sp.Range(2) ? 1 : -1;
                  d = std::clamp(d, -2, 11);
                  vel[(size_t)i] = std::clamp((beat ? 0.8 : 0.65) + sp.Uniform(-0.1, 0.1), 0.3, 1.0);
                  note[(size_t)i] = degreeNote(d, 3);
               }
               const int lq = sequence(vel, note, lineRate, sp.Uniform(70.0, 95.0), 16);
               g.melodic = true;
               const int ly = g.Add("Wavetable", sp.Range(1 << 30), k);
               g.melodic = false;
               g.Connect(lq, ly, true);
               synths.push_back(ly);
               chans.push_back({ ly, 0.0f });
            }
            specialWet = sp.Uniform(0.1, 0.3);
            specialDecay = sp.Uniform(1.0, 2.4);
         }
         else
         {
            // One massive voice playing a line: stacked unison engines (owner: "leads should sound massive").
            const int kind = PickDirectiveKind(sp, dir.leadW);
            static const Voice kLeadVoice[] = { Voice::BrassStack, Voice::SawStack, Voice::Supersaw, Voice::SquareLead, Voice::Supersaw, Voice::FmStack };
            Rng bv(std::string("vital:lead:") + std::to_string(recipe.seed)); // its own stream: the line's draws stay put
            k = DesignVoice(kLeadVoice[std::clamp(kind, 0, 5)], sp);
            if (kind != 4 && bv.Uniform() < 0.5)
               k = BankVoice(BankRole::Lead, bv);
            if (kind == 4) // vowel: a sung stack on top of the supersaw
            {
               Engine(k, 'a', 47, sp.Uniform(0.0, 0.9), 3, sp.Uniform(10.0, 18.0), 0, 1.0);
               k["a.ampAttack"] = { 'f', num(sp.Uniform(20.0, 60.0)) };
            }
            k["glide"] = { 'f', num(sp.Uniform(0.0, 0.06)) };
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 0);
            // Energy fills or thins the line (the draw stays, so the line is the same one, busier or sparser).
            const double density = std::clamp(sp.Uniform(0.35, 0.7) * (1.0 + 0.6 * recipe.energy), 0.12, 0.95);
            static const int kMove[] = { -2, -1, -1, 0, 1, 1, 2, 3 };
            int d = sp.Range(4);
            std::vector<std::pair<int, bool>> leadSeq;
            leadSeq.reserve(16);
            for (int i = 0; i < 16; i++)
            {
               const bool on = (i == 0 || sp.Uniform() < density * (i % 4 == 0 ? 1.4 : 0.8));
               if (on)
                  d = std::clamp(d + kMove[sp.Range(8)], -2, 7);
               leadSeq.push_back({ d, on });
            }
            ProposeMotif(criticRng, st, mine, root, out.scale, leadSeq, melNov);
            // Call and response: the second half opens like the first and then lands somewhere new, on the root or fifth.
            for (int i = 8; i < 13; i++)
               leadSeq[(size_t)i] = leadSeq[(size_t)i - 8];
            for (int i = 15; i >= 13; i--)
               if (leadSeq[(size_t)i].second || i == 13)
               {
                  leadSeq[(size_t)i] = { sp.Uniform() < 0.6 ? 0 : 4, true };
                  break;
               }
            // A named mood leans the line onto the chord: the beats land on root, third or fifth and the
            // line opens on the third, the note that says major or minor.
            if (g.Quadrant() != Graph::kNoQuad)
            {
               bool first = true;
               for (int i = 0; i < 16; i++)
                  if (leadSeq[(size_t)i].second && (i % 4 == 0 || first))
                  {
                     int& dg = leadSeq[(size_t)i].first;
                     const int m = ((dg % 7) + 7) % 7;
                     if (first)
                        dg += 2 - m;
                     else if (m == 1 || m == 3 || m == 5)
                        dg -= 1;
                     else if (m == 6)
                        dg += 1;
                     dg = std::clamp(dg, -2, 7);
                     first = false;
                  }
            }
            Rng nb(std::string("notes:lead:") + st.id + ":" + std::to_string(recipe.seed));
            if (!mine && nb.Uniform() < 0.5) // a bar a player in the note library plays, in place of the drawn line
            {
               const auto line = BankLine(out.scale, false, nb);
               for (int i = 0; i < 16; i++)
                  leadSeq[(size_t)i] = { line[(size_t)i].first > 9 ? line[(size_t)i].first - 7 : line[(size_t)i].first, line[(size_t)i].second > 0.0 };
            }
            for (int i = 0; i < 16; i++)
               if (leadSeq[(size_t)i].second)
               {
                  vel[(size_t)i] = std::clamp((i % 4 == 0 ? dir.velMean + 0.15 : dir.velMean) + sp.Uniform(-dir.velSpread, dir.velSpread), 0.3, 1.0);
                  note[(size_t)i] = degreeNote(leadSeq[(size_t)i].first, 3);
               }
            const double leadRate = sp.Uniform() < 0.3 || recipe.energy < -0.35f ? 0.5 : 0.25;
            const double leadGate = sp.Uniform(60.0, 100.0) * (recipe.energy > 0.35f ? 0.7 : 1.0);
            const int sq = sequence(vel, note, leadRate, leadGate, 16);
            g.melodic = true;
            const int sy = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.melodic = false;
            g.Connect(sq, sy, true);
            int end = sy;
            if (sp.Uniform() < 0.85) // a dotted echo is how a lead gets its space
            {
               const int dl = g.Add("Delay", sp.Range(1 << 30), F({ { "mix", sp.Uniform(0.18, 0.3) }, { "timeMs", 0.75 * beatMs }, { "feedback", sp.Uniform(25.0, 45.0) }, { "tone", -0.1 },
                                                                    { "sync", 0.0 }, { "wow", 0.0 }, { "flutter", 0.0 }, { "saturation", 0.0 }, { "diffusion", 0.0 } }));
               g.Connect(end, dl, false);
               end = dl;
            }
            synths.push_back(sy);
            chans.push_back({ end, 0.0f });
            specialWet = sp.Uniform(0.1, 0.3);
            specialDecay = sp.Uniform(1.0, 2.2);
         }
      }
      else if (texPart)
      {
         // Sound with no pulse: it holds, drifts and breathes across the loop.
         // Kinds 6-8 are nature foley merged into textures (owner, 2026-10-30: birds, water, leaves, wind; wind is kind 4).
         const int kind = sp.Range(9);
         const double cyc = sp.Uniform() < 0.5 ? (double)loopBeats : loopBeats / 2.0; // a whole number of cycles per loop, so it loops
         specialWet = sp.Uniform(0.3, 0.6);
         specialDecay = sp.Uniform(3.0, 6.0);
         auto bed = [&](int table, int filter, double cutLo, double cutHi, double res, int octave)
         {
            Params k;
            wt(k, table, sp.Uniform(0.0, 1.0), filter, sp.Uniform(cutLo, cutHi), res);
            k["a.unison"] = { 'i', std::to_string(2 + sp.Range(3)) };
            k["a.detune"] = { 'f', num(sp.Uniform(8.0, 40.0)) };
            k["a.stereoWidth"] = { 'f', "1" };
            k["a.ampAttack"] = { 'f', num(std::min(3500.0, loopBeats * beatMs * 0.5)) };
            k["a.ampSustain"] = { 'f', "1" };
            k["a.ampRelease"] = { 'f', "2500" };
            k["a.octave"] = { 'i', std::to_string(octave) };
            const int tx = g.Add("Wavetable", sp.Range(1 << 30), k);
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, degreeNote(sp.Range(5), 1));
            vel[0] = 1.0;
            g.Connect(sequence(vel, note, loopBeats, 100.0, 1), tx, true);
            sweep(tx, "a.cutoff", cutLo * 0.6, cutHi * 1.5, cyc, sp.Uniform(0.0, 1.0));
            sweep(tx, "a.position", 0.1, 0.9, cyc, sp.Uniform(0.0, 1.0));
            synths.push_back(tx);
            return tx;
         };
         // Owner's review, 2026-10-29: textures "mostly sound like grumbly bass" (low share 0.113 vs the library's pad
         // loops 0.042), so the beds sit an octave up and the rumble became an ocean (item 18).
         if (kind == 0) // drone: dark, wide, slow
            chans.push_back({ bed(sp.Range(2) ? 56 : 59, 2, 450.0, 1600.0, sp.Uniform(0.1, 0.35), 1), 0.0f });
         else if (kind == 1) // vowel bed: a held, breathy formant
            chans.push_back({ bed(sp.Range(2) ? 46 : 43, 1, 1500.0, 4500.0, 0.0, 0), 0.0f });
         else if (kind == 2) // shimmer: high, bright, glassy
            chans.push_back({ bed(sp.Range(2) ? 62 : 63, 1, 3000.0, 8000.0, 0.0, 1), 0.0f });
         else if (kind == 3) // ocean: low-passed noise whose cutoff swells and falls once or twice a loop, like waves
         {
            Params n = F({ { "volume", 0.8 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 1500.0 },
                           { "decay", 200.0 }, { "sustain", 1.0 }, { "release", 2000.0 }, { "cutoff", 900.0 }, { "resonance", 0.0 },
                           { "voices", 1.0 }, { "keyTrack", 0.0 } });
            n["filterType"] = { 'i', "2" };
            const int nv = g.Add("Analog", sp.Range(1 << 30), n);
            std::vector<double> vel(16, 0.0);
            vel[0] = 1.0;
            g.Connect(sequence(vel, std::vector<int>(16, 60), loopBeats, 100.0, 1), nv, true);
            sweep(nv, "cutoff", sp.Uniform(250.0, 450.0), sp.Uniform(1800.0, 3500.0), cyc, sp.Uniform(0.0, 1.0));
            chans.push_back({ nv, -2.0f });
         }
         else if (kind == 4) // wind: noise through a band-pass that wanders
         {
            Params n = F({ { "volume", 0.8 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 1200.0 },
                           { "decay", 200.0 }, { "sustain", 1.0 }, { "release", 1800.0 }, { "cutoff", 18000.0 }, { "voices", 1.0 }, { "keyTrack", 0.0 } });
            n["filterType"] = { 'i', "0" };
            const int nv = g.Add("Analog", sp.Range(1 << 30), n);
            std::vector<double> vel(16, 0.0);
            vel[0] = 1.0;
            g.Connect(sequence(vel, std::vector<int>(16, 60), loopBeats, 100.0, 1), nv, true);
            Params f = F({ { "freq", 700.0 }, { "q", sp.Uniform(0.8, 3.0) }, { "mix", 1.0 } });
            f["type"] = { 'f', "6" }; // effect params are floats: an "i" key is never read
            const int af = g.Add("Audio Filter", sp.Range(1 << 30), f);
            g.Connect(nv, af, false);
            sweep(af, "freq", sp.Uniform(250.0, 500.0), sp.Uniform(1800.0, 4000.0), cyc, sp.Uniform(0.0, 1.0));
            chans.push_back({ af, 0.0f });
         }
         else if (kind >= 6) // nature: events in phrases (Farnell, Designing Sound) over a quiet bed of moving air
         {
            // A bed of low-passed noise whose cutoff drifts, under every nature kind.
            Params n = F({ { "volume", 0.7 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 1500.0 },
                           { "decay", 200.0 }, { "sustain", 1.0 }, { "release", 2000.0 }, { "cutoff", 1200.0 }, { "resonance", 0.0 },
                           { "voices", 1.0 }, { "keyTrack", 0.0 } });
            n["filterType"] = { 'i', "2" };
            const int air = g.Add("Analog", sp.Range(1 << 30), n);
            std::vector<double> held(16, 0.0);
            held[0] = 1.0;
            g.Connect(sequence(held, std::vector<int>(16, 60), loopBeats, 100.0, 1), air, true);
            sweep(air, "cutoff", sp.Uniform(500.0, 900.0), sp.Uniform(1800.0, 3200.0), cyc, sp.Uniform(0.0, 1.0));
            chans.push_back({ air, kind == 7 ? -6.0f : -12.0f });
            // Phrases: two to five events close together, then a gap.
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 96);
            for (int b = sp.Range(3); b < 16; b += 4 + sp.Range(5))
               for (int j = 0; j < 2 + sp.Range(4) && b + j < 16; j++)
                  vel[(size_t)(b + j)] = sp.Uniform(0.4, 1.0);
            vel[(size_t)sp.Range(4)] = 0.9;
            if (kind == 6) // birds: sine chirps, 2-5 kHz, a fast glide up or down in each
            {
               for (int i = 0; i < 16; i++)
                  note[(size_t)i] = 96 + sp.Range(10);
               Params k;
               wt(k, 0, 0.0, 0, 18000, 0.0);
               k["a.pitchAmount"] = { 'f', num(sp.Uniform() < 0.5 ? sp.Uniform(-14.0, -5.0) : sp.Uniform(5.0, 14.0)) };
               k["a.pitchDecay"] = { 'f', num(sp.Uniform(20.0, 70.0)) };
               k["a.ampDecay"] = { 'f', num(sp.Uniform(30.0, 90.0)) };
               k["a.ampSustain"] = { 'f', "0" };
               k["a.ampRelease"] = { 'f', "20" };
               const int bird = g.Add("Wavetable", sp.Range(1 << 30), k);
               const int src = sequence(vel, note, 0.25, 30.0, 16);
               const int bh = g.Add("Humanizer", sp.Range(1 << 30), F({ { "timingMs", sp.Uniform(10.0, 35.0) }, { "velocityPct", 30.0 } }));
               g.Connect(src, bh, true);
               g.Connect(bh, bird, true);
               chans.push_back({ bird, -6.0f });
            }
            else if (kind == 7) // water: bubbles, sines that rise as they form (Minnaert), dense over the stream's hiss
            {
               for (int i = 0; i < 16; i++)
                  if (vel[(size_t)i] == 0.0 && sp.Uniform() < 0.5)
                     vel[(size_t)i] = sp.Uniform(0.2, 0.6);
               for (int i = 0; i < 16; i++)
                  note[(size_t)i] = 79 + sp.Range(20);
               Params k;
               wt(k, 0, 0.0, 0, 18000, 0.0);
               k["a.pitchAmount"] = { 'f', num(sp.Uniform(-18.0, -6.0)) };
               k["a.pitchDecay"] = { 'f', num(sp.Uniform(10.0, 40.0)) };
               k["a.ampDecay"] = { 'f', num(sp.Uniform(20.0, 70.0)) };
               k["a.ampSustain"] = { 'f', "0" };
               k["a.ampRelease"] = { 'f', "20" };
               const int drop = g.Add("Wavetable", sp.Range(1 << 30), k);
               const int src = sequence(vel, note, 0.125, 30.0, 16);
               const int wh = g.Add("Humanizer", sp.Range(1 << 30), F({ { "timingMs", sp.Uniform(15.0, 40.0) }, { "velocityPct", 40.0 } }));
               g.Connect(src, wh, true);
               g.Connect(wh, drop, true);
               chans.push_back({ drop, -8.0f });
            }
            else // leaves: short band-passed noise crackles in gusts, 2.5-7 kHz
            {
               Params nn = F({ { "volume", 0.8 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 0.0 },
                               { "decay", sp.Uniform(15.0, 60.0) }, { "sustain", 0.0 }, { "release", 20.0 }, { "cutoff", sp.Uniform(2500.0, 7000.0) },
                               { "resonance", 0.15 }, { "voices", 4.0 }, { "keyTrack", 0.0 } });
               nn["filterType"] = { 'i', "8" };
               const int leaf = g.Add("Analog", sp.Range(1 << 30), nn);
               const int src = sequence(vel, note, 0.125, 30.0, 16);
               const int lh = g.Add("Humanizer", sp.Range(1 << 30), F({ { "timingMs", sp.Uniform(10.0, 35.0) }, { "velocityPct", 30.0 } }));
               g.Connect(src, lh, true);
               g.Connect(lh, leaf, true);
               chans.push_back({ leaf, -4.0f });
            }
            specialWet = sp.Uniform(0.2, 0.45);
         }
         else // grain: scattered glassy specks over a quiet bed
         {
            Params k;
            wt(k, 58, sp.Uniform(0.0, 1.0), 1, sp.Uniform(5000.0, 12000.0), 0.0);
            k["a.ampDecay"] = { 'f', num(sp.Uniform(60.0, 240.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "60" };
            const int gr = g.Add("Wavetable", sp.Range(1 << 30), k);
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 72);
            for (int i = 0; i < 16; i++)
               if (sp.Uniform() < 0.35)
               {
                  vel[(size_t)i] = sp.Uniform(0.25, 0.9);
                  note[(size_t)i] = 66 + sp.Range(24);
               }
            vel[(size_t)sp.Range(4)] = 0.8;
            g.Connect(sequence(vel, note, 0.5, 25.0, 16), gr, true);
            sweep(gr, "a.position", 0.0, 1.0, cyc, sp.Uniform(0.0, 1.0));
            chans.push_back({ gr, 0.0f });
            chans.push_back({ bed(sp.Range(2) ? 56 : 59, 2, 400.0, 1400.0, 0.1, 1), -9.0f });
            specialWet = sp.Uniform(0.45, 0.7);
         }
      }
      else // foley: taps, steps, knocks, drips and rustles, placed by hand-like irregular timing
      {
         const int kind = sp.Range(10);
         specialWet = sp.Uniform(0.08, 0.3);
         specialDecay = sp.Uniform(0.6, 1.8);
         std::vector<double> vel(16, 0.0);
         std::vector<int> note(16, 60);
         const double density = kind == 5 ? sp.Uniform(0.5, 0.9) : kind == 2 ? sp.Uniform(0.2, 0.4) : sp.Uniform(0.15, 0.5);
         for (int i = 0; i < 16; i++)
            if (sp.Uniform() < density)
            {
               vel[(size_t)i] = sp.Uniform(0.35, 1.0);
               note[(size_t)i] = 52 + sp.Range(14);
            }
         // Three ways to place the hits: scattered, in bursts with gaps, or walking evenly.
         const int layout = sp.Range(3);
         if (layout == 1)
         {
            std::fill(vel.begin(), vel.end(), 0.0);
            for (int b = sp.Range(4); b < 16; b += 5 + sp.Range(4))
               for (int j = 0; j < 2 + sp.Range(3) && b + j < 16; j++)
               {
                  vel[(size_t)(b + j)] = sp.Uniform(0.4, 1.0);
                  note[(size_t)(b + j)] = 52 + sp.Range(14);
               }
         }
         else if (layout == 2)
         {
            std::fill(vel.begin(), vel.end(), 0.0);
            const int gap = 2 + sp.Range(3);
            for (int i = sp.Range(gap); i < 16; i += gap)
               vel[(size_t)i] = sp.Uniform(0.55, 1.0);
         }
         vel[(size_t)sp.Range(4)] = 1.0;
         const double rate = kind == 5 || kind == 7 || kind == 9 ? 0.25 : 0.5;
         int src = sequence(vel, note, rate, 20.0, 16);
         src = [&]()
         {
            const int hu = g.Add("Humanizer", sp.Range(1 << 30), F({ { "timingMs", sp.Uniform(15.0, 45.0) }, { "velocityPct", 25.0 } }));
            g.Connect(src, hu, true);
            return hu;
         }();
         auto noiseHit = [&](int filter, double cut, double res, double dec, double vol)
         {
            Params nn = F({ { "volume", vol }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 0.0 },
                            { "decay", dec }, { "sustain", 0.0 }, { "release", dec * 0.5 }, { "cutoff", cut }, { "resonance", res },
                            { "voices", 1.0 }, { "keyTrack", 0.0 } });
            nn["filterType"] = { 'i', std::to_string(filter) };
            return g.Add("Analog", sp.Range(1 << 30), nn);
         };
         int voice = 0;
         switch (kind)
         {
         case 0: // footsteps: a low thud under a soft filtered scuff
         {
            // Surface drives the scuff: dirt is dull, wood hollow, concrete short and bright, gravel long and crunchy.
            const int surface = sp.Range(4);
            const double cuts[4][2] = { { 250.0, 600.0 }, { 900.0, 2200.0 }, { 2500.0, 5000.0 }, { 4000.0, 9000.0 } };
            const double decs[4][2] = { { 90.0, 170.0 }, { 60.0, 130.0 }, { 30.0, 70.0 }, { 140.0, 280.0 } };
            voice = noiseHit(surface == 3 ? 2 : 1, sp.Uniform(cuts[surface][0], cuts[surface][1]), surface == 1 ? 0.35 : 0.1,
                             sp.Uniform(decs[surface][0], decs[surface][1]), 0.9);
            g.Connect(src, voice, true);
            Params k;
            wt(k, 10, 0.1, 1, 500, 0.0);
            k["a.pitchAmount"] = { 'f', "8" };
            k["a.pitchDecay"] = { 'f', "30" };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(80.0, 160.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "30" };
            k["a.octave"] = { 'i', "-1" };
            const int th = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.Connect(src, th, true);
            chans.push_back({ th, -3.0f });
            break;
         }
         case 1: // knocks: a struck wooden body
         {
            Params k;
            wt(k, sp.Range(2) ? 16 : 49, sp.Uniform(0.0, 0.5), 1, sp.Uniform(1500.0, 4000.0), 0.0);
            k["a.ampDecay"] = { 'f', num(sp.Uniform(40.0, 160.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "30" };
            k["a.octave"] = { 'i', std::to_string(-1 + sp.Range(2)) };
            voice = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.Connect(src, voice, true);
            break;
         }
         case 2: // drips: a sine that bends up fast, high, with a tail
         {
            Params k;
            wt(k, 0, 0.0, 0, 18000, 0.0);
            k["a.pitchAmount"] = { 'f', num(sp.Uniform(-12.0, -4.0)) };
            k["a.pitchDecay"] = { 'f', num(sp.Uniform(25.0, 70.0)) };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(70.0, 200.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "50" };
            k["a.octave"] = { 'i', "2" };
            voice = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.Connect(src, voice, true);
            specialWet = sp.Uniform(0.3, 0.55);
            break;
         }
         case 3: // clock ticks: two short metallic clicks
         {
            Params m = F({ { "transient", 1.6 }, { "decay", sp.Uniform(0.05, 0.14) }, { "stiffness", sp.Uniform(1.0, 2.0) },
                           { "filterCutoff", sp.Uniform(4000.0, 12000.0) }, { "volume", 0.8 }, { "width", 0.2 } });
            m["material"] = { 'i', std::to_string(sp.Range(8)) };
            voice = g.Add("Metallic", sp.Range(1 << 30), m);
            g.Connect(src, voice, true);
            break;
         }
         case 4: // paper and cloth: a high, breathy rustle
            voice = noiseHit(2, sp.Uniform(3000.0, 9000.0), 0.15, sp.Uniform(80.0, 260.0), 0.8);
            g.Connect(src, voice, true);
            break;
         case 5: // crackle: many tiny ticks, fire or vinyl
            voice = noiseHit(1, sp.Uniform(5000.0, 14000.0), 0.0, sp.Uniform(8.0, 25.0), 0.9);
            g.Connect(src, voice, true);
            break;
         case 6: // glass: bright clinks that ring
         {
            Params m = F({ { "transient", 1.2 }, { "decay", sp.Uniform(0.25, 0.7) }, { "stiffness", sp.Uniform(1.5, 2.0) },
                           { "filterCutoff", sp.Uniform(9000.0, 16000.0) }, { "volume", 0.7 }, { "width", 0.5 } });
            m["material"] = { 'i', std::to_string(sp.Range(8)) };
            voice = g.Add("Metallic", sp.Range(1 << 30), m);
            g.Connect(src, voice, true);
            specialWet = sp.Uniform(0.2, 0.45);
            break;
         }
         case 7: // typing: tiny dry clicks, a hard and a soft one
            voice = noiseHit(2, sp.Uniform(2500.0, 7000.0), 0.3, sp.Uniform(6.0, 18.0), 0.9);
            g.Connect(src, voice, true);
            specialWet = sp.Uniform(0.03, 0.12);
            break;
         case 8: // bubbles: little sines that bend up
         {
            Params k;
            wt(k, 0, 0.0, 0, 18000, 0.0);
            k["a.pitchAmount"] = { 'f', num(sp.Uniform(5.0, 14.0)) };
            k["a.pitchDecay"] = { 'f', num(sp.Uniform(15.0, 45.0)) };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(40.0, 110.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "30" };
            k["a.octave"] = { 'i', std::to_string(1 + sp.Range(2)) };
            voice = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.Connect(src, voice, true);
            specialWet = sp.Uniform(0.15, 0.4);
            break;
         }
         default: // heartbeat or hand-on-table: soft double thumps
         {
            Params k;
            wt(k, 10, 0.0, 1, 400, 0.0);
            k["a.pitchAmount"] = { 'f', num(sp.Uniform(10.0, 20.0)) };
            k["a.pitchDecay"] = { 'f', num(sp.Uniform(40.0, 90.0)) };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(110.0, 240.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "60" };
            k["a.octave"] = { 'i', "-1" };
            voice = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.Connect(src, voice, true);
            break;
         }
         }
         chans.push_back({ voice, 0.0f });
      }
   }

   Params mp;
   mp["channels"] = { 'i', std::to_string(chans.size()) };
   for (size_t i = 0; i < chans.size(); i++)
      mp["gain" + std::to_string(i)] = { 'f', Num(chans[i].second + st.trim) };
   const int mx = g.Add("Mixer", 0, mp);
   for (const auto& ch : chans)
      g.Connect(ch.first, mx, false);
   int end = mx;
   const bool dryDrums = part == Part::Kicks || part == Part::Snares || part == Part::Toms;
   if ((!ban.count("Reverb") || pads || ownSpace) && !bassPart && !dryDrums)
   {
      const bool wide = std::string(st.family) == "ambient" || pads || fxPart || texPart;
      if (pads)
      {
         Rng cr(std::string("padchorus:") + std::to_string(recipe.seed));
         const int ch = g.Add("Chorus", seed(), F({ { "mix", cr.Uniform(0.25, 0.5) }, { "delay", cr.Uniform(8.0, 18.0) }, { "depth", cr.Uniform(3.0, 8.0) },
                                                    { "rate", cr.Uniform(0.15, 0.6) }, { "spread", cr.Uniform(0.5, 1.0) } }));
         g.Connect(end, ch, false);
         end = ch;
      }
      if (pads && padDelay)
      {
         const int dl = g.Add("Delay", seed(), F({ { "mix", padDelayMix }, { "timeMs", padDelayBeats * 60000.0 / out.bpm }, { "feedback", padDelayFb },
                                                   { "tone", -0.4 } }));
         g.Connect(end, dl, false);
         end = dl;
      }
      const int rv = g.Add("Reverb", seed(), F({ { "mix", pads ? padWet : ownSpace ? specialWet : wide ? 0.45 : drumPart ? 0.05 : 0.15 }, { "decay", pads ? padDecay : ownSpace ? specialDecay : wide ? 3.0 : drumPart ? 0.5 : 1.2 } }));
      g.Connect(end, rv, false);
      end = rv;
   }
   // The master's floor: nothing under 30 Hz. A voice an octave down under a low note plays near 20 Hz, which no
   // speaker gives back; left in, it is half the loop's power and the loudness match turns everything audible down.
   {
      // Every knob pinned: Add() draws the ones left out, and a drawn envAmount sweeps the cutoff into the loop.
      Params hp = F({ { "freq", 30.0 }, { "q", 0.707 }, { "mix", 1.0 }, { "gain", 0.0 }, { "envAmount", 0.0 }, { "drive", 0.0 } });
      hp["type"] = { 'f', "4" }; // AudioFilterDsp::kHP24; an "i" key is never read (effect params are floats)
      const int hpf = g.Add("Audio Filter", 0, hp);
      g.Connect(end, hpf, false);
      end = hpf;
   }
   const int lm = g.Add("Limiter", 0);
   g.Connect(end, lm, false);

   // Motion: round(mod * 3) timbre params on the synths, each swept around
   // the middle of its range, as styled.py's LFO cables do.
   std::vector<std::pair<int, const GenRow*>> targets;
   for (int sy : synths)
      for (const GenRow& row : kGenRows)
         if (row.moves && g.types[sy] == row.type)
            targets.push_back({ sy, &row });
   for (size_t i = targets.size(); i > 1; i--)
      std::swap(targets[i - 1], targets[(size_t)rng.Range((int)i)]);
   const int k = (int)std::lround(st.mod * 3.0f);
   for (int i = 0; i < k && i < (int)targets.size(); i++)
   {
      const GenRow& row = *targets[i].second;
      const float depth = 0.1f + 0.5f * st.mod * (float)rng.Uniform();
      // A sweep around the middle of a knob's whole range overrides a designed voice (a bass's 1 kHz cutoff swept
      // to 9 kHz, drive swept to half): only the parts with no designed voice keep it (pads, textures, fx, foley).
      // The draws stay, so nothing after moves.
      const bool keep = pads || part == Part::Textures || part == Part::Fx || part == Part::Foley;
      app::OfflineGraph::Motion m;
      m.node = targets[i].first;
      m.key = row.key;
      m.lo = row.lo + (row.hi - row.lo) * (0.5f - 0.5f * depth);
      m.hi = row.lo + (row.hi - row.lo) * (0.5f + 0.5f * depth);
      const double periods[] = { 4.0, 8.0, 16.0 };
      m.periodBeats = periods[rng.Range(3)];
      m.phase = rng.Uniform();
      if (keep)
         out.motions.push_back(m);
   }

   char head[96];
   snprintf(head, sizeof(head), "transport %g 4 4 %d %d", out.bpm, root, out.scale);
   g.lines.insert(g.lines.begin() + 2, head);
   g.lines.insert(g.lines.begin() + 3, std::string("# style: ") + st.id + " seed " + std::to_string(recipe.seed) + " part " + PartName(part));
   out.patch = g.Text();
   out.tap = lm;
   if (mine)
   {
      const SampleCriticData::StyleDirective& dir = StyleDirectiveFor(st.id);
      float phi[UserStyle::kFeatureDim] = {};
      int kicks = 0, hats = 0, bassHits = 0;
      for (int s = 0; s < 16; s++)
      {
         if (*lanes[0] && lanes[0][s] != '.') kicks++;
         if (*lanes[2] && lanes[2][s] != '.') hats++;
         if (s < (int)bass.size() && bass[(size_t)s] != '.') bassHits++;
      }
      phi[0] = (float)kicks / 8.0f;
      phi[1] = (float)hats / 16.0f;
      phi[2] = drumSync;
      phi[3] = st.swing * 3.0f;
      phi[4] = st.groove;
      phi[5] = drumNov;
      phi[6] = st.motifDensity;
      phi[7] = (float)st.motifRange / 8.0f;
      phi[8] = melNov;
      phi[9] = (float)bassHits / 16.0f;
      // Perceptual pillars + valence/arousal + style descriptors
      const float tension = std::clamp(2.0f * dir.affect[1] * (1.0f - dir.affect[0]), 0.0f, 1.0f); // tense quadrant
      phi[10] = 0.5f * (dir.pillars[0] + dir.pocket);     // danceability & pocket
      phi[11] = 0.5f * (dir.pillars[1] + dir.affect[1]);  // energy & arousal
      phi[12] = 0.5f * (dir.pillars[2] + dir.affect[0]);  // valence
      phi[13] = 0.5f * (dir.pillars[3] + dir.complexity); // brightness & complexity
      phi[14] = 0.5f * (dir.pillars[4] + tension);        // contrast & tension
      phi[15] = 1.0f;
      UserStyle::NoteFeatures(phi);
   }
   return out;
}

bool JudgeMelody(const std::vector<int>& pitches, int root, int scale, float& score)
{
   (void)scale; // the corpus model is in pitch classes from the root; the scale is the caller's snap
   score = 0.0f;
   return CriticJudgePitches(pitches, root, score);
}

std::vector<std::string> PoolTypes()
{
   std::set<std::string> fixed = { "Drum Sequencer", "Note Sequencer", "Chorder", "Arpeggiator", "Mixer", "Limiter" };
   std::vector<std::string> out;
   for (const GenType& t : kGenTypes)
      if (!fixed.count(t.name))
         out.push_back(t.name);
   return out;
}

Compiled Probe(const std::string& type)
{
   Compiled out;
   const GenType* t = TypeOf(type);
   if (t == nullptr)
   {
      out.error = "unknown type " + type;
      return out;
   }
   Graph g;
   Params p = F({ { "rateBeats", 0.5 }, { "gatePercent", 70.0 } });
   p["steps"] = { 'i', "4" };
   p["rateMode"] = { 'i', "0" };
   for (int k = 0; k < 4; k++)
   {
      p["note" + std::to_string(k)] = { 'i', std::to_string(k % 2 == 0 ? 48 : 55) };
      p["en" + std::to_string(k)] = { 'b', "1" };
      p["vel" + std::to_string(k)] = { 'f', "0.9" };
   }
   const int sq = g.Add("Note Sequencer", 0, p);
   const std::string cat = t->category;
   if (cat == "Synths")
   {
      out.tap = g.Add(type, 0);
      g.Connect(sq, out.tap, true);
   }
   else if (cat == "Notes")
   {
      const int nf = g.Add(type, 0, NoteFx(type));
      g.Connect(sq, nf, true);
      out.tap = g.Add("Analog", 0);
      g.Connect(nf, out.tap, true);
   }
   else
   {
      const int sy = g.Add("Analog", 0);
      g.Connect(sq, sy, true);
      out.tap = g.Add(type, 0);
      g.Connect(sy, out.tap, false);
   }
   out.bpm = 120.0f;
   g.lines.insert(g.lines.begin() + 2, "transport 120 4 4 0 0");
   out.patch = g.Text();
   return out;
}
}
