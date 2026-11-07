// Kit coherence: which sounds go together, learned from real kits, packs, racks and sets (tools/universe/compat_model.py).
// Included inside SampleStyles.cpp's anonymous namespace. A sound is described the way sound_features.py measures one;
// a pair of sounds the way pairs.py does; each strong role pair has two gradient-boosted models: "run" picks the kit,
// "check" (fit on the other half of the packs) is the held-out score the self-test reports.
#include "app/graph/GenerateSamplesCompat.inc"

   struct CompatSound
   {
      bool pitched = false;
      double pitch = 0.0; // midi
      double attackMs = 0.0, decayMs = 0.0, centroidHz = 1000.0, crestDb = 12.0, noise = 0.1, low = 0.0, mid = 0.0, high = 0.0;
      float bands[32] = {};
   };

   // pairs.py vec(): the scale differences are taken in
   struct CompatVec
   {
      bool pitched;
      double s[9]; // pitch, attack, decay, centroid, crest, noise, low, mid, high
      const float* bands;
   };

   CompatVec CompatScale(const CompatSound& x)
   {
      return { x.pitched,
               { x.pitch, std::log2(1.0 + x.attackMs), std::log2(5.0 + x.decayMs), std::log2(std::max(20.0, x.centroidHz)), x.crestDb / 6.0,
                 std::log10(1e-4 + x.noise), x.low, x.mid, x.high },
               x.bands };
   }

   // pairs.py pair_features(), in its order
   void CompatFeatures(const CompatSound& sa, const CompatSound& sb, float* out)
   {
      const CompatVec a = CompatScale(sa), b = CompatScale(sb);
      int k = 0;
      for (int f = 0; f < 9; f++)
      {
         const double d = f == 0 ? (a.pitched && b.pitched ? (a.s[0] - b.s[0]) / 12.0 : 0.0) : a.s[f] - b.s[f];
         out[k++] = (float)d;
         out[k++] = (float)std::abs(d);
      }
      double ov = 0.0, lo = 0.0;
      for (int i = 0; i < 32; i++)
      {
         const double m = std::min(a.bands[i], b.bands[i]);
         ov += m;
         if (i < 11)
            lo += m;
      }
      const bool hp = a.pitched && b.pitched;
      double iv = 0.0;
      if (hp)
      {
         const int pc = (((int)std::lround(a.s[0] - b.s[0])) % 12 + 12) % 12;
         iv = std::min(pc, 12 - pc) / 6.0;
      }
      out[k++] = (float)ov, out[k++] = (float)lo, out[k++] = (float)iv, out[k++] = hp ? 1.0f : 0.0f;
      for (const CompatVec* v : { &a, &b })
      {
         out[k++] = (float)(v->pitched ? v->s[0] / 12.0 : 0.0);
         for (int f = 1; f < 9; f++)
            out[k++] = (float)v->s[f];
         for (int i = 0; i < 8; i++)
            out[k++] = v->bands[4 * i] + v->bands[4 * i + 1] + v->bands[4 * i + 2] + v->bands[4 * i + 3];
      }
   }

   double CompatEval(const compat::Model& m, const float* x)
   {
      double sum = 0.0;
      for (int t = 0; t < m.trees; t++)
      {
         int base = m.s[t], node = 0;
         while (m.f[base + node] >= 0)
            node = x[m.f[base + node]] <= m.t[base + node] ? m.l[base + node] : m.r[base + node];
         sum += m.v[base + node];
      }
      const double raw = m.offset + m.rate * sum;
      return 1.0 / (1.0 + std::exp(-raw));
   }

   const compat::Pair* CompatPair(const char* a, const char* b)
   {
      for (int i = 0; i < compat::kNumPairs; i++)
         if (std::strcmp(compat::kPairs[i].a, a) == 0 && std::strcmp(compat::kPairs[i].b, b) == 0)
            return &compat::kPairs[i];
      return nullptr;
   }

   // P(together) for two sounds in the role order of the table; -1 when the pair is weak (absent): the genre prior stands.
   double CompatScore(const char* ra, const CompatSound& a, const char* rb, const CompatSound& b, bool check)
   {
      const compat::Pair* p = CompatPair(ra, rb);
      bool swap = false;
      if (p == nullptr)
         p = CompatPair(rb, ra), swap = true;
      if (p == nullptr)
         return -1.0;
      float x[compat::kFeat];
      if (swap)
         CompatFeatures(b, a, x);
      else
         CompatFeatures(a, b, x);
      return CompatEval(check ? p->check : p->run, x);
   }

   // A bundled kit lane (0 kick, 1 snare, 2 closed hat) as the Drum Sequencer plays it: the measured grid, interpolated.
   CompatSound CompatLane(int lane, double semis, double decay, double transient)
   {
      using namespace compat;
      const float* P = lane == 0 ? kLane0Pitch : lane == 1 ? kLane1Pitch : kLane2Pitch;
      const float* B = lane == 0 ? kLane0Bands : lane == 1 ? kLane1Bands : kLane2Bands;
      const float* G = lane == 0 ? kLane0Grid : lane == 1 ? kLane1Grid : kLane2Grid;
      auto at = [](const float* grid, int n, double v, int& i0, double& w)
      {
         if (v <= grid[0])
         {
            i0 = 0, w = 0.0;
            return;
         }
         for (int i = 0; i + 1 < n; i++)
            if (v <= grid[i + 1])
            {
               i0 = i, w = (v - grid[i]) / (grid[i + 1] - grid[i]);
               return;
            }
         i0 = n - 2, w = 1.0;
      };
      const double s = std::clamp(semis, -12.0, 12.0) + 12.0;
      const int s0 = std::min(kSemis - 2, (int)std::floor(s));
      const double ws = s - s0;
      int d0, t0;
      double wd, wt;
      at(kDecayGrid, kDecays, std::clamp(decay, 0.0, 1.0), d0, wd);
      at(kTransGrid, kTrans, std::clamp(transient, -1.0, 1.0), t0, wt);
      double sc[kScal] = {};
      for (int a = 0; a < 2; a++)
         for (int b = 0; b < 2; b++)
            for (int c = 0; c < 2; c++)
            {
               const double w = (a ? ws : 1 - ws) * (b ? wd : 1 - wd) * (c ? wt : 1 - wt);
               const float* cell = G + (((s0 + a) * kDecays + (d0 + b)) * kTrans + (t0 + c)) * kScal;
               for (int k = 0; k < kScal; k++)
                  sc[k] += w * cell[k];
            }
      CompatSound o;
      o.pitched = P[s0] > -999.0f;
      o.pitch = P[s0] * (1 - ws) + P[s0 + 1] * ws;
      o.attackMs = sc[0], o.decayMs = sc[1], o.centroidHz = sc[2], o.crestDb = sc[3], o.noise = sc[4], o.low = sc[5], o.mid = sc[6], o.high = sc[7];
      for (int i = 0; i < 32; i++)
      {
         double v = 0.0;
         for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++)
               v += (a ? ws : 1 - ws) * (b ? wd : 1 - wd) * B[((s0 + a) * kDecays + (d0 + b)) * 32 + i];
         o.bands[i] = (float)v;
      }
      return o;
   }
