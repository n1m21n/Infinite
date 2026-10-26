// Music style cards compiled into a patch. See SampleStyles.h; the card format
// is documented in tools/catalog/styles_music.json's _doc.
#include "app/graph/SampleStyles.h"

#include "app/graph/UserStyle.h"
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
#include "app/graph/GenerateSamplesCritic.inc"

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
      const float emoIntel  = dir.emotions[3]; // Intelligence / Complexity
      const float emoGroove = dir.emotions[7]; // Groove / Hypnosis
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
      const bool fourFloor = std::string(st.family) == "house" || std::string(st.family) == "techno" ||
                             std::string(st.family) == "trance" || std::string(st.family) == "hard";
      uint16_t bestM[8];
      for (int l = 0; l < 8; l++) bestM[l] = baseM[l];
      float bestScore = -1.0f, bestSync = 0.2f;
      CriticJudgeDrum(baseM, baseM, ambient, bestScore, bestSync);

      for (int cand = 0; cand < 8; cand++)
      {
         uint16_t m[8];
         for (int l = 0; l < 8; l++) m[l] = baseM[l];
         const int mode = (emoIntel > 0.65f && cr.Uniform() < 0.45) ? 1 : cr.Range(3);
         const int cl = cr.Range(32);
         if (mode == 0)
         {
            // Cross-cluster hybrid: keep genre kick anchor, blend hats/percussion from learned cluster + syncopated kick pickup
            if (!fourFloor && (SampleCriticData::kDrumClusters[cl][0] & 1u))
               m[0] = SampleCriticData::kDrumClusters[cl][0];
            else if (fourFloor && cr.Uniform() < 0.65)
               m[0] = (uint16_t)(0x1111u | (1u << (cr.Range(2) ? 14 : 15)) | (cr.Uniform() < 0.4 ? (1u << 6) : 0u));
            m[2] = SampleCriticData::kDrumClusters[cl][2] ? SampleCriticData::kDrumClusters[cl][2] : EuclideanMask(cr.Int(5, 11), cr.Range(4));
            if (cr.Uniform() < 0.5)
               m[5] = (uint16_t)(SampleCriticData::kDrumClusters[cl][5] & 0x6c6cu);
         }
         else if (mode == 1)
         {
            // Euclidean polyrhythmic hats + syncopated ghost snare/percussion
            m[2] = EuclideanMask(cr.Int(5, 13), cr.Range(4));
            if (m[3] == 0 && cr.Uniform() < 0.6)
               m[3] = 0x4444u;
            if (m[1] != 0)
               m[1] |= (uint16_t)(1u << (cr.Range(2) ? 7 : 15));
            if (cr.Uniform() < 0.45 + 0.25 * emoIntel)
               m[5] = EuclideanMask(cr.Int(2, 5), cr.Range(8));
         }
         else
         {
            // Pocket mutation: displace 1-3 offbeat hits while preserving Beat 1 and backbeats
            for (int flips = 0; flips < 3; flips++)
            {
               const int l = cr.Range(6);
               const int s = 1 + cr.Range(15);
               if ((l == 0 || l == 1) && (s == 0 || s == 4 || s == 8 || s == 12) && fourFloor)
                  continue;
               m[l] ^= (uint16_t)(1u << s);
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
      const float emoHappy = dir.emotions[0]; // Valence / Happiness
      const float emoSad   = dir.emotions[1]; // Sadness / Melancholy
      const float emoIntel = dir.emotions[3]; // Intelligence / Complexity
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
   out.root = root0;
   out.scale = scale0;
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
         out.bpm = (float)feel.Uniform(f.lo, f.hi);
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

   int drums = 0, duckKey = 0;
   bool is808Kick = false;
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
         force[lane + "_volume"] = { 'f', Num(i == 2 ? 0.55 : part == Part::Percussion && i == 5 ? 0.6 : 0.85) };
         const std::string pat = lanes[i];
         const bool ghostLane = i == 1 || i == 2 || i == 3 || i == 4;
         for (int k = 0; k < 32; k++)
         {
            const char c = pat.empty() ? '.' : pat[(size_t)(k % 16)];
            double v = c == 'x' ? 1.0 : c == 'o' ? 0.45 : 0.0;
            if (v > 0.0)
               v = std::clamp(v + rng.Uniform(-0.12, 0.12) * st.groove, 0.2, 1.0);
            if (v > 0.0 && mine && user.hasVelocity)
               v = std::clamp(v * (1.0 + (std::clamp(user.velMean / 0.75, 0.6, 1.15) - 1.0) * user.velW), 0.2, 1.0); // as hard as they play
            else if (!pat.empty() && ghostLane && k % 2 == 1 && rng.Uniform() < 0.22 * st.groove * (1.0 + 2.0 * (mine && user.hasTiming ? user.offGrid * user.timingW : 0.0)))
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
         synthDrums.push_back({ 0, sy, -3.0f });
         if (dr.Uniform() < 0.45) // a 5-15 ms noise click on the front
         {
            const int ck = noiseVoice(2, dr.Uniform(3000.0, 8000.0), 0.1, dr.Uniform(5.0, 15.0), 0.8);
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
      if ((*lanes[5] || *lanes[6]) && dr.Uniform() < (part == Part::Toms ? 1.0 : subPart ? 0.75 : 0.33))
      {
         const bool perc = part == Part::Percussion; // percussion keeps its brighter toms
         const int notes[2] = { perc ? 40 + dr.Range(8) : 37 + dr.Range(8), perc ? 52 + dr.Range(8) : 47 + dr.Range(8) }; // toms: floor 75-140 Hz, rack 125-230 Hz
         for (int ti = 0; ti < 2; ti++)
         {
            if (!*lanes[5 + ti])
               continue;
            int sq = 0;
            drumSeq(5 + ti, notes[ti], 70.0, sq);
            const int tm = toneVoice(perc ? dr.Uniform(8.0, 22.0) : dr.Uniform(4.0, 9.0), perc ? dr.Uniform(40.0, 110.0) : dr.Uniform(35.0, 80.0),
                                   perc ? dr.Uniform(180.0, 460.0) : dr.Uniform(300.0, 600.0), perc && dr.Range(3) == 0 ? 10 : 0, perc ? 9000.0 : 1500.0);
            g.Connect(sq, tm, true);
            force["lane" + std::to_string(5 + ti) + "_volume"] = { 'f', "0" };
            synthDrums.push_back({ 5 + ti, tm, -5.0f });
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
         const int sy = g.Add(picked, seed());
         g.Connect(sq, sy, true);
         const int fxChoices[] = { 0, 1, 1, 2 };
         int end = chain(sy, fxChoices[rng.Range(4)]);
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
      Params p = F({ { "rateBeats", pads ? std::max(4.0, (double)st.chordBeats) : (plucks && !pluckArp) ? pluckStab : st.chordBeats },
                     { "strumMs", st.chordStrum },
                     { "humanizeTimingMs", st.chordHuman }, { "humanizeVelocity", st.chordHuman } });
      p["root"] = { 'i', std::to_string(root) };
      p["scale"] = { 'i', std::to_string(out.scale) };
      p["chordSize"] = { 'i', sizes.empty() ? "3" : rng.Choice(sizes) };
      const SampleCriticData::StyleDirective& cDir = StyleDirectiveFor(st.id);
      if (p["chordSize"].second == "3" && (cDir.emotions[3] > 0.62f || cDir.emotions[5] > 0.72f) &&
          criticRng.Uniform() < 0.35 * (cDir.emotions[3] + cDir.emotions[5]))
         p["chordSize"] = { 'i', "4" }; // Emotion network: 7th chord extension for intelligence/nostalgia
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
         const int sy = (pads || plucks) ? g.Add(shapedVoice, seed(), shapedEnv) : g.Add(picked, seed());
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
         int pad = chain(sy, 1 + rng.Range(2));
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
         seq.push_back({ deg, k % 4 == 0 || rng.Uniform() < density });
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
      Params p = F({ { "rateBeats", st.motifRate }, { "gatePercent", myGate(st.motifGate, st.motifRate) } });
      p["steps"] = { 'i', "16" };
      p["rateMode"] = { 'i', "0" };
      for (int k = 0; k < 16; k++)
      {
         p["note" + std::to_string(k)] = { 'i', std::to_string(degreeNote(seq[k].first, st.motifOctave)) };
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
         const int sy = g.Add(synth, seed());
         g.Connect(mn, sy, true);
         synths.push_back(sy);
         int lead = chain(sy, 1);
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
         // A figure of one to eight hits in sixteen steps, on the root, its fifth and its neighbours.
         const double density = sp.Uniform(0.15, 0.65);
         std::vector<double> vel(16, 0.0);
         std::vector<int> note(16, 0), deg16(16, 0);
         static const int kDeg[] = { 0, 0, 0, 0, 2, 3, 4, 4, 5, 6, 7 };
         static const int kOct[] = { 0, 0, 0, 1 };
         std::string rawBass(16, '.');
         int prev = 0;
         for (int k = 0; k < 16; k++)
         {
            const bool strong = k % 4 == 0;
            if (sp.Uniform() < density * (strong ? 1.8 : 0.8) || k == 0)
            {
               prev = sp.Uniform() < 0.55 ? prev : kDeg[sp.Range(11)];
               rawBass[(size_t)k] = (char)('0' + std::clamp(prev, 0, 7));
            }
         }
         rawBass = ProposeBass(criticRng, mine, rawBass, lanes[0]);
         for (int k = 0; k < 16; k++)
         {
            if (rawBass[(size_t)k] != '.')
            {
               const bool strong = k % 4 == 0;
               vel[(size_t)k] = std::clamp((strong ? dir.velMean + 0.15 : dir.velMean) + sp.Uniform(-dir.velSpread, dir.velSpread), 0.3, 1.0);
               deg16[(size_t)k] = Degree(rawBass[(size_t)k]);
               note[(size_t)k] = degreeNote(deg16[(size_t)k], 1 + kOct[sp.Range(4)]);
            }
         }
         const double rate = sp.Range(5) == 0 ? 0.5 : 0.25;
         const int kind = PickDirectiveKind(sp, dir.bassW);
         Params k;
         double gate = 70.0;
         if (kind == 1) // an 808 is tuned to the key: the root sits at 30-60 Hz, never above about 100
            for (int i = 0; i < 16; i++)
               note[(size_t)i] = degreeNote(deg16[(size_t)i], 0);
         if (kind == 5) // acid: a few accented steps, the rest softer; the 303's accent lifts level and sweep together
            for (int i = 0; i < 16; i++)
               if (vel[(size_t)i] > 0.0)
                  vel[(size_t)i] = sp.Uniform() < 0.3 || i == 0 ? 1.0 : sp.Uniform(0.45, 0.65);
         switch (kind)
         {
         case 0: // sub: a clean low sine, long notes
            wt(k, 10, sp.Uniform(0.0, 0.3), 1, 700, 0.0);
            k["a.ampDecay"] = { 'f', "400" };
            k["a.ampSustain"] = { 'f', num(sp.Uniform(0.7, 1.0)) };
            k["a.ampRelease"] = { 'f', num(sp.Uniform(60.0, 220.0)) };
            gate = sp.Uniform(60.0, 95.0);
            break;
         case 1: // 808: a pitch-dropped boom that rings
            wt(k, sp.Range(2) ? 10 : 0, 0.1, 1, 1200, 0.0);
            k["a.pitchAmount"] = { 'f', num(sp.Uniform(6.0, 18.0)) };
            k["a.pitchDecay"] = { 'f', num(sp.Uniform(40.0, 120.0)) };
            k["a.pitchSustain"] = { 'f', "0" };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(500.0, 1400.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "80" };
            k["glide"] = { 'f', num(sp.Uniform(0.0, 0.12)) };
            gate = 50.0;
            break;
         case 2: // reese: detuned saws through a slow low-pass
            wt(k, sp.Range(2) ? 24 : 0, sp.Uniform(0.4, 0.8), 2, sp.Uniform(250.0, 700.0), sp.Uniform(0.05, 0.3));
            k["a.unison"] = { 'i', std::to_string(2 + sp.Range(2)) };
            k["a.detune"] = { 'f', num(sp.Uniform(20.0, 60.0)) };
            k["a.stereoWidth"] = { 'f', "0.3" };
            k["a.ampSustain"] = { 'f', "0.9" };
            k["a.ampRelease"] = { 'f', "120" };
            break;
         case 3: // plucked: the string or slap, a filter snap
            wt(k, sp.Range(2) ? 54 : 50, sp.Uniform(0.1, 0.7), 2, sp.Uniform(250.0, 600.0), sp.Uniform(0.0, 0.2));
            k["a.filterAmount"] = { 'f', num(sp.Uniform(2.5, 4.0)) };
            k["a.filterDecay"] = { 'f', num(sp.Uniform(120.0, 300.0)) };
            k["a.filterSustain"] = { 'f', "0" };
            k["a.ampDecay"] = { 'f', num(sp.Uniform(250.0, 600.0)) };
            k["a.ampSustain"] = { 'f', "0" };
            k["a.ampRelease"] = { 'f', "90" };
            break;
         case 4: // fm: a bright, hollow growl that closes
            wt(k, sp.Range(2) ? 12 : 32, sp.Uniform(0.1, 0.7), 1, sp.Uniform(600.0, 1600.0), 0.1);
            k["a.ampDecay"] = { 'f', num(sp.Uniform(250.0, 500.0)) };
            k["a.ampSustain"] = { 'f', num(sp.Uniform(0.2, 0.5)) };
            k["a.ampRelease"] = { 'f', "100" };
            break;
         default: // acid: a squelching, resonant saw with a fast sweep
            wt(k, sp.Range(2) ? 35 : 4, sp.Uniform(0.2, 0.7), 2, sp.Uniform(250.0, 600.0), sp.Uniform(0.45, 0.75));
            k["a.filterAmount"] = { 'f', num(sp.Uniform(3.0, 5.0)) };
            k["a.filterDecay"] = { 'f', num(sp.Uniform(110.0, 260.0)) };
            k["a.filterSustain"] = { 'f', "0.05" };
            k["a.ampDecay"] = { 'f', "300" };
            k["a.ampSustain"] = { 'f', "0.6" };
            k["a.ampRelease"] = { 'f', "70" };
            k["glide"] = { 'f', num(sp.Uniform(0.05, 0.07)) }; // the 303's slide is about 60 ms
            gate = sp.Uniform(85.0, 110.0);                    // long gates are what lets a step slide into the next
            break;
         }
         const int sq = sequence(vel, note, rate, gate, 16);
         const int sy = g.Add("Wavetable", sp.Range(1 << 30), k);
         g.Connect(sq, sy, true);
         int end = sy;
         if (sp.Uniform() < 0.5)
         {
            const int dr2 = g.Add("Drive", sp.Range(1 << 30), F({ { "mix", sp.Uniform(0.3, 0.8) }, { "drive", sp.Uniform(4.0, 16.0) }, { "output", -4.0 } }));
            g.Connect(end, dr2, false);
            end = dr2;
         }
         synths.push_back(sy);
         chans.push_back({ end, 0.0f });
         if (kind == 2)
         {
            // A clean sine an octave under the beating saws, as the reese recipes layer it.
            Params sb;
            wt(sb, 10, 0.0, 1, 600, 0.0);
            sb["a.ampSustain"] = { 'f', "0.9" };
            sb["a.ampRelease"] = { 'f', "120" };
            sb["a.octave"] = { 'i', "-1" };
            const int sub = g.Add("Wavetable", sp.Range(1 << 30), sb);
            g.Connect(sq, sub, true);
            chans.push_back({ sub, -6.0f });
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
            f["type"] = { 'i', "6" };
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
            f["type"] = { 'i', "6" };
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
            const int kind = PickDirectiveKind(sp, dir.keysW);
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
            Params p = F({ { "rateBeats", rate }, { "strumMs", strum }, { "humanizeTimingMs", sp.Uniform(3.0, 14.0) }, { "humanizeVelocity", sp.Uniform(5.0, 20.0) } });
            p["root"] = { 'i', std::to_string(root) };
            p["scale"] = { 'i', std::to_string(out.scale) };
            p["chordSize"] = { 'i', std::to_string(3 + sp.Range(2)) };
            p["progSteps"] = { 'i', std::to_string(2 + sp.Range(3)) };
            const int ch = g.Add("Chorder", sp.Range(1 << 30), p);
            const int sy = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.Connect(ch, sy, true);
            synths.push_back(sy);
            chans.push_back({ sy, 0.0f });
            specialWet = sp.Uniform(0.1, 0.3);
            specialDecay = sp.Uniform(1.0, 2.4);
         }
         else
         {
            // One voice playing a line: stepwise, with rests and a few leaps.
            const int kind = PickDirectiveKind(sp, dir.leadW);
            switch (kind)
            {
            case 0: // brass
               wt(k, 53, sp.Uniform(0.2, 0.8), 1, sp.Uniform(2200.0, 5000.0), 0.0);
               k["a.filterAmount"] = { 'f', num(sp.Uniform(1.0, 2.0)) };
               k["a.filterDecay"] = { 'f', "250" };
               k["a.filterSustain"] = { 'f', "0.5" };
               k["a.ampAttack"] = { 'f', num(sp.Uniform(15.0, 60.0)) };
               break;
            case 1: // reed
               wt(k, 52, sp.Uniform(0.1, 0.7), 1, sp.Uniform(2500.0, 6000.0), 0.0);
               k["a.ampAttack"] = { 'f', num(sp.Uniform(10.0, 40.0)) };
               break;
            case 2: // detuned saw lead
               wt(k, 24, sp.Uniform(0.4, 0.85), 2, sp.Uniform(2000.0, 5500.0), sp.Uniform(0.0, 0.25));
               k["a.unison"] = { 'i', std::to_string(2 + sp.Range(3)) };
               k["a.detune"] = { 'f', num(sp.Uniform(8.0, 22.0)) };
               k["a.stereoWidth"] = { 'f', "0.7" };
               break;
            case 3: // pulse
               wt(k, 4, sp.Uniform(0.15, 0.7), 1, sp.Uniform(2500.0, 6000.0), 0.1);
               break;
            case 4: // vowel: a sung line
               wt(k, 47, sp.Uniform(0.0, 0.9), 1, sp.Uniform(3500.0, 8000.0), 0.0);
               k["a.ampAttack"] = { 'f', num(sp.Uniform(30.0, 90.0)) };
               break;
            default: // fm lead: bright and glassy
               wt(k, sp.Range(2) ? 12 : 32, sp.Uniform(0.1, 0.6), 1, sp.Uniform(4500.0, 9000.0), 0.0);
               break;
            }
            k["a.ampDecay"] = { 'f', "300" };
            k["a.ampSustain"] = { 'f', num(sp.Uniform(0.6, 0.95)) };
            k["a.ampRelease"] = { 'f', num(sp.Uniform(80.0, 260.0)) };
            k["glide"] = { 'f', num(sp.Uniform(0.0, 0.14)) };
            k["a.octave"] = { 'i', std::to_string(sp.Range(2)) };
            std::vector<double> vel(16, 0.0);
            std::vector<int> note(16, 0);
            const double density = sp.Uniform(0.35, 0.7);
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
            for (int i = 0; i < 16; i++)
               if (leadSeq[(size_t)i].second)
               {
                  vel[(size_t)i] = std::clamp((i % 4 == 0 ? dir.velMean + 0.15 : dir.velMean) + sp.Uniform(-dir.velSpread, dir.velSpread), 0.3, 1.0);
                  note[(size_t)i] = degreeNote(leadSeq[(size_t)i].first, 2);
               }
            const int sq = sequence(vel, note, sp.Uniform() < 0.3 ? 0.5 : 0.25, sp.Uniform(60.0, 100.0), 16);
            const int sy = g.Add("Wavetable", sp.Range(1 << 30), k);
            g.Connect(sq, sy, true);
            int end = sy;
            if (sp.Uniform() < 0.7) // a dotted echo is how a lead gets its space
            {
               const int dl = g.Add("Delay", sp.Range(1 << 30), F({ { "mix", sp.Uniform(0.15, 0.35) }, { "timeMs", 0.75 * beatMs }, { "feedback", sp.Uniform(25.0, 50.0) }, { "tone", -0.3 } }));
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
         const int kind = sp.Range(6);
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
         if (kind == 0) // drone: dark, wide, slow
            chans.push_back({ bed(sp.Range(2) ? 56 : 59, 2, 300.0, 1200.0, sp.Uniform(0.1, 0.35), 0), 0.0f });
         else if (kind == 1) // vowel bed: a held, breathy formant
            chans.push_back({ bed(sp.Range(2) ? 46 : 43, 1, 1500.0, 4500.0, 0.0, 0), 0.0f });
         else if (kind == 2) // shimmer: high, bright, glassy
            chans.push_back({ bed(sp.Range(2) ? 62 : 63, 1, 3000.0, 8000.0, 0.0, 1), 0.0f });
         else if (kind == 3) // rumble: a low sine under dark noise
         {
            chans.push_back({ bed(10, 1, 200.0, 500.0, 0.0, -1), 0.0f });
            Params n = F({ { "volume", 0.6 }, { "osc1Vol", 0.0 }, { "osc2Vol", 0.0 }, { "sub", 0.0 }, { "noise", 1.0 }, { "attack", 1500.0 },
                           { "decay", 200.0 }, { "sustain", 1.0 }, { "release", 2000.0 }, { "cutoff", sp.Uniform(120.0, 320.0) }, { "voices", 1.0 }, { "keyTrack", 0.0 } });
            n["filterType"] = { 'i', "1" };
            const int nv = g.Add("Analog", sp.Range(1 << 30), n);
            std::vector<double> vel(16, 0.0);
            vel[0] = 1.0;
            g.Connect(sequence(vel, std::vector<int>(16, 36), loopBeats, 100.0, 1), nv, true);
            chans.push_back({ nv, -3.0f });
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
            f["type"] = { 'i', "6" };
            const int af = g.Add("Audio Filter", sp.Range(1 << 30), f);
            g.Connect(nv, af, false);
            sweep(af, "freq", sp.Uniform(250.0, 500.0), sp.Uniform(1800.0, 4000.0), cyc, sp.Uniform(0.0, 1.0));
            chans.push_back({ af, 0.0f });
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
            chans.push_back({ bed(sp.Range(2) ? 56 : 59, 2, 250.0, 900.0, 0.1, 0), -9.0f });
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
      // Perceptual Pillars + 8-Node Connected Emotion Network activations
      phi[10] = 0.5f * (dir.pillars[0] + dir.emotions[7]); // Danceability & Groove Hypnosis
      phi[11] = 0.5f * (dir.pillars[1] + dir.emotions[2]); // Energy & Drive
      phi[12] = 0.5f * (dir.pillars[2] + dir.emotions[0] - 0.5f * dir.emotions[1] + 0.25f); // Valence / Happiness vs Melancholy
      phi[13] = 0.5f * (dir.pillars[3] + dir.emotions[3]); // Brightness & Intelligence/Complexity
      phi[14] = 0.5f * (dir.pillars[4] + dir.emotions[4] - 0.3f * dir.emotions[6] + 0.15f); // Contrast & Tension vs Serenity
      phi[15] = 1.0f;
      UserStyle::NoteFeatures(phi);
   }
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
