// Music style cards compiled into a patch. See SampleStyles.h; the card format
// is documented in tools/catalog/styles_music.json's _doc.
#include "app/graph/SampleStyles.h"

#include "audio/MusicTime.h"
#include "nodes/DrumSequencerNode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace
{
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
}

namespace SampleStyles
{
const char* PartName(Part p)
{
   static const char* const kNames[] = { "all", "synths", "pads", "plucks", "percussion", "drums" };
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
   out.bpm = recipe.bpm > 0.0f ? recipe.bpm : (float)cardBpm;
   out.root = recipe.key >= 0 ? recipe.key % 12 : cardRoot;
   out.scale = recipe.scale >= 0 ? recipe.scale : scale;
   const int root = out.root;
   const Part part = recipe.part;
   const bool full = part == Part::Full;
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
   if (pads || plucks)
   {
      const bool analog = shapeRng.Range(2) == 0;
      shapedVoice = analog ? "Analog" : "Wavetable";
      const char* const keys[4] = { analog ? "attack" : "a.ampAttack", analog ? "decay" : "a.ampDecay", analog ? "sustain" : "a.ampSustain",
                                    analog ? "release" : "a.ampRelease" };
      const double env[4] = { pads ? shapeRng.Uniform(300.0, 900.0) : shapeRng.Uniform(1.0, 5.0),
                              pads ? shapeRng.Uniform(800.0, 1600.0) : shapeRng.Uniform(120.0, 350.0),
                              pads ? shapeRng.Uniform(0.7, 0.9) : 0.0,
                              pads ? shapeRng.Uniform(1500.0, 2500.0) : shapeRng.Uniform(80.0, 200.0) };
      for (int i = 0; i < 4; i++)
         shapedEnv[keys[i]] = { 'f', Num(std::round(env[i] * 1000.0) / 1000.0) };
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
   auto seed = [&]() { return rng.Range(1 << 30); };

   Graph g;
   g.variation = recipe.variation;
   std::vector<std::pair<int, float>> chans; // (node, gain dB)
   std::vector<int> synths;
   int layers = 0; // layers the full mix has, whatever the part keeps

   auto chain = [&](int src, int fxN)
   {
      int end = src;
      for (int i = 0; i < fxN; i++)
      {
         const std::string n = pick(st.fx);
         if (n.empty())
            break;
         const int id = g.Add(n, seed());
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
   auto degreeNote = [&](int deg, int octave) { return MusicTime::DegreeToNote(deg, octave + 1, root, out.scale); };

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
   const bool drumPart = part == Part::Drums || part == Part::Percussion;
   auto none = [&](int from) { return !*lanes[from] && !*lanes[from + 1] && !*lanes[from + 2] && !*lanes[from + 3]; };
   if (drumPart && none(0) && (part == Part::Drums || none(4)))
   {
      lanes[0] = "x...x...x...x...";
      lanes[1] = "....x.......x...";
      lanes[2] = "..x...x...x...x.";
      hasDrums = true;
   }
   if (part == Part::Percussion && none(4))
   {
      lanes[4] = lanes[1];
      lanes[5] = lanes[0];
      lanes[6] = lanes[3];
      lanes[7] = lanes[2];
   }
   if (part == Part::Drums)
      lanes[4] = lanes[5] = lanes[6] = lanes[7] = "";
   if (part == Part::Percussion)
      lanes[0] = lanes[1] = lanes[2] = lanes[3] = "";
   int drums = 0;
   if (hasDrums)
   {
      Params force = F({ { "swing", st.swing }, { "volume", 0.9 } });
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
      for (int i = 0; i < 8; i++)
      {
         const std::string lane = "lane" + std::to_string(i);
         force[lane + "_path"] = { 's', DrumSequencerNode::KitDir() + "/" + kKitFiles[i] + ".wav" };
         force[lane + "_volume"] = { 'f', Num(i == 2 ? 0.55 : 0.85) };
         const std::string pat = lanes[i];
         const bool ghostLane = i == 1 || i == 2 || i == 3 || i == 4;
         for (int k = 0; k < 32; k++)
         {
            const char c = pat.empty() ? '.' : pat[(size_t)(k % 16)];
            double v = c == 'x' ? 1.0 : c == 'o' ? 0.45 : 0.0;
            if (v > 0.0)
               v = std::clamp(v + rng.Uniform(-0.12, 0.12) * st.groove, 0.2, 1.0);
            else if (!pat.empty() && ghostLane && k % 2 == 1 && rng.Uniform() < 0.22 * st.groove)
               v = 0.18 + 0.1 * rng.Uniform();
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
      drums = g.Add("Drum Sequencer", seed(), force);
      if (full || drumPart)
         chans.push_back({ chain(drums, 0), 0.0f });
      layers++;
   }

   // Bass: the card's 16-step figure as scale degrees, one octave up from the bottom.
   std::string bass = std::string(st.bass) + std::string(16, '.');
   bass.resize(16);
   if (bass.find_first_not_of('.') != std::string::npos)
   {
      Params p = F({ { "rateBeats", 0.25 }, { "gatePercent", 55.0 } });
      p["steps"] = { 'i', "16" };
      p["rateMode"] = { 'i', "0" };
      for (int k = 0; k < 16; k++)
      {
         p["note" + std::to_string(k)] = { 'i', std::to_string(degreeNote(bass[k] != '.' ? Degree(bass[k]) : 0, 1)) };
         p["en" + std::to_string(k)] = { 'b', bass[k] != '.' ? "1" : "0" };
         p["vel" + std::to_string(k)] = { 'f', "0.9" };
      }
      const int sq = g.Add("Note Sequencer", seed(), p);
      const std::string picked = pick(st.synth);
      if (!picked.empty())
      {
         const int sy = g.Add(picked, seed());
         g.Connect(sq, sy, true);
         const int fxChoices[] = { 0, 1, 1, 2 };
         const int end = chain(sy, fxChoices[rng.Range(4)]);
         layers++;
         if (wantBass)
         {
            synths.push_back(sy);
            chans.push_back({ end, -4.0f });
         }
      }
   }

   // A card with no bass or motif (solo piano) has only its chords: they are its synths.
   if (part == Part::Synths && synths.empty() && !st.hasMotif)
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
      Params p = F({ { "rateBeats", pads ? std::max(4.0, (double)st.chordBeats) : st.chordBeats }, { "strumMs", st.chordStrum },
                     { "humanizeTimingMs", st.chordHuman }, { "humanizeVelocity", st.chordHuman } });
      p["root"] = { 'i', std::to_string(root) };
      p["scale"] = { 'i', std::to_string(out.scale) };
      p["chordSize"] = { 'i', sizes.empty() ? "3" : rng.Choice(sizes) };
      p["progSteps"] = { 'i', std::to_string(st.chordSteps) };
      const int ch = g.Add("Chorder", seed(), p);
      int notes = noteChain(ch, st.notefx);
      const std::string fam = st.family;
      // Plucks always run the chords through the arpeggiator; pads never do.
      if (((fam == "trance" || fam == "retro" || fam == "house") && rng.Uniform() < 0.6 && !pads) || plucks)
      {
         Params a = F({ { "rateBeats", 0.25 } });
         a["rateMode"] = { 'i', "0" };
         a["octaves"] = { 'i', "2" };
         a["mode"] = { 'i', "0" };
         const int ar = g.Add("Arpeggiator", seed(), a);
         g.Connect(notes, ar, true);
         notes = ar;
      }
      const std::string picked = pick(st.synth);
      if (!picked.empty())
      {
         const int sy = (pads || plucks) ? g.Add(shapedVoice, seed(), shapedEnv) : g.Add(picked, seed());
         g.Connect(notes, sy, true);
         if (wantChords)
            synths.push_back(sy);
         int pad = chain(sy, 1 + rng.Range(2));
         if (st.sidechain && drums != 0)
         {
            // The kick ducks the chords: the pump that makes house breathe.
            const int dy = g.Add("Dynamics", seed(), F({ { "sidechainExternal", 1.0 }, { "ratio", 6.0 }, { "threshold", -28.0 },
                                                          { "attack", 5.0 }, { "release", 160.0 } }));
            g.Connect(pad, dy, false);
            g.Connect(drums, dy, false);
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
         deg = std::clamp(deg + steps[rng.Range(7)], -2, st.motifRange);
         seq.push_back({ deg, k % 4 == 0 || rng.Uniform() < st.motifDensity });
      }
      std::vector<std::pair<int, bool>> answer(seq.begin(), seq.begin() + 5);
      for (int k = 0; k < 3; k++)
      {
         const int steps[] = { -2, -1, 1, 2, 3 };
         deg = std::clamp(deg + steps[rng.Range(5)], -2, st.motifRange);
         answer.push_back({ deg, rng.Uniform() < 0.85 });
      }
      seq.insert(seq.end(), answer.begin(), answer.end());
      Params p = F({ { "rateBeats", st.motifRate }, { "gatePercent", st.motifGate } });
      p["steps"] = { 'i', "16" };
      p["rateMode"] = { 'i', "0" };
      for (int k = 0; k < 16; k++)
      {
         p["note" + std::to_string(k)] = { 'i', std::to_string(degreeNote(seq[k].first, st.motifOctave)) };
         p["en" + std::to_string(k)] = { 'b', seq[k].second ? "1" : "0" };
         p["vel" + std::to_string(k)] = { 'f', k % 4 == 0 ? "1" : Num(std::round((0.65 + 0.2 * rng.Uniform()) * 100.0) / 100.0) };
      }
      const int ms = g.Add("Note Sequencer", seed(), p);
      const int mn = noteChain(ms, st.motifNotefx);
      std::string synth = pick(st.motifSynth);
      if (synth.empty())
         synth = Split(st.motifSynth).empty() ? "" : Split(st.motifSynth)[0];
      if (!synth.empty() && TypeOf(synth) != nullptr)
      {
         const int sy = g.Add(synth, seed());
         g.Connect(mn, sy, true);
         synths.push_back(sy);
         int lead = chain(sy, 1);
         if (st.sidechain && drums != 0)
         {
            const int dy = g.Add("Dynamics", seed(), F({ { "sidechainExternal", 1.0 }, { "ratio", 4.0 }, { "threshold", -26.0 },
                                                          { "attack", 5.0 }, { "release", 140.0 } }));
            g.Connect(lead, dy, false);
            g.Connect(drums, dy, false);
            lead = dy;
         }
         chans.push_back({ lead, st.motifGain });
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
   if (!ban.count("Reverb") || pads)
   {
      const bool wide = std::string(st.family) == "ambient" || pads;
      const int rv = g.Add("Reverb", seed(), F({ { "mix", wide ? 0.45 : 0.15 }, { "decay", wide ? 3.0 : 1.2 } }));
      g.Connect(end, rv, false);
      end = rv;
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
      app::OfflineGraph::Motion m;
      m.node = targets[i].first;
      m.key = row.key;
      m.lo = row.lo + (row.hi - row.lo) * (0.5f - 0.5f * depth);
      m.hi = row.lo + (row.hi - row.lo) * (0.5f + 0.5f * depth);
      const double periods[] = { 4.0, 8.0, 16.0 };
      m.periodBeats = periods[rng.Range(3)];
      m.phase = rng.Uniform();
      out.motions.push_back(m);
   }

   char head[96];
   snprintf(head, sizeof(head), "transport %g 4 4 %d %d", out.bpm, root, out.scale);
   g.lines.insert(g.lines.begin() + 2, head);
   g.lines.insert(g.lines.begin() + 3, std::string("# style: ") + st.id + " seed " + std::to_string(recipe.seed) + " part " + PartName(part));
   out.patch = g.Text();
   out.tap = lm;
   return out;
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
