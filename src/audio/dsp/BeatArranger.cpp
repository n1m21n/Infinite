#include "BeatArranger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace BeatArranger
{
   struct Rng
   {
      uint32_t state;
      explicit Rng(uint32_t s) : state(s == 0 ? 0x9e3779b9u : s) {}

      uint32_t Next()
      {
         state ^= state << 13;
         state ^= state >> 17;
         state ^= state << 5;
         return state;
      }

      float Float01()
      {
         return (Next() & 0x00ffffff) * (1.0f / 16777216.0f);
      }

      float Range(float a, float b)
      {
         return a + Float01() * (b - a);
      }

      int Int(int a, int b) // inclusive [a, b]
      {
         if (b <= a)
            return a;
         return a + (int)(Next() % (uint32_t)(b - a + 1));
      }

      bool Chance(float prob)
      {
         return Float01() < prob;
      }
   };

   static std::vector<bool> EuclideanRhythm(int k, int n, int rotation = 0)
   {
      std::vector<bool> pattern(n, false);
      if (k <= 0 || n <= 0)
         return pattern;
      if (k >= n)
      {
         std::fill(pattern.begin(), pattern.end(), true);
         return pattern;
      }
      for (int i = 0; i < k; i++)
      {
         const int step = (i * n) / k;
         int rotated = (step + rotation) % n;
         if (rotated < 0)
            rotated += n;
         pattern[rotated] = true;
      }
      return pattern;
   }

   static const SliceInfo* PickSlice(const std::vector<SliceInfo>& list, Rng& rng)
   {
      if (list.empty())
         return nullptr;
      if (list.size() == 1)
         return &list[0];

      // Confidence-weighted selection so higher-confidence slices repeat more
      float sumWeight = 0.0f;
      for (const auto& s : list)
         sumWeight += std::max(0.1f, s.confidence);

      const float r = rng.Range(0.0f, sumWeight);
      float accum = 0.0f;
      for (const auto& s : list)
      {
         accum += std::max(0.1f, s.confidence);
         if (accum >= r)
            return &s;
      }
      return &list.back();
   }

   std::vector<ArrangedHit> Arrange(const std::vector<SliceInfo>& pool,
                                    const ArrangeParams& p,
                                    uint32_t seed)
   {
      std::vector<ArrangedHit> hits;
      if (pool.empty())
         return hits;

      Rng rng(seed);

      const int bars = (p.bars == 1 || p.bars == 4) ? p.bars : 2;
      const int totalSteps = bars * 16;

      // Group pool by drum class
      std::vector<SliceInfo> byClass[DrumClassifier::kNumClasses];
      for (const auto& s : pool)
      {
         const int idx = std::clamp((int)s.cls, 0, DrumClassifier::kNumClasses - 1);
         byClass[idx].push_back(s);
      }

      // Missing-role promotion:
      // Always ensure the essential drum roles have candidate slices
      auto promoteBest = [&](const std::vector<DrumClassifier::DrumClass>& fallbackClasses,
                             DrumClassifier::DrumClass targetClass)
      {
         if (!byClass[(int)targetClass].empty())
            return;
         for (auto fb : fallbackClasses)
         {
            if (!byClass[(int)fb].empty())
            {
               byClass[(int)targetClass] = byClass[(int)fb];
               return;
            }
         }
         // Last resort: use entire pool
         byClass[(int)targetClass] = pool;
      };

      // Kick fallback: Bass -> Perc -> any
      promoteBest({ DrumClassifier::DrumClass::Bass, DrumClassifier::DrumClass::Perc },
                  DrumClassifier::DrumClass::Kick);
      // Snare fallback: Clap -> Perc -> any
      promoteBest({ DrumClassifier::DrumClass::Clap, DrumClassifier::DrumClass::Perc },
                  DrumClassifier::DrumClass::Snare);
      // Clap fallback: Snare -> Perc -> any
      promoteBest({ DrumClassifier::DrumClass::Snare, DrumClassifier::DrumClass::Perc },
                  DrumClassifier::DrumClass::Clap);
      // HatClosed fallback: HatOpen -> Perc -> any
      promoteBest({ DrumClassifier::DrumClass::HatOpen, DrumClassifier::DrumClass::Perc },
                  DrumClassifier::DrumClass::HatClosed);
      // HatOpen fallback: HatClosed -> Perc -> any
      promoteBest({ DrumClassifier::DrumClass::HatClosed, DrumClassifier::DrumClass::Perc },
                  DrumClassifier::DrumClass::HatOpen);
      // Bass fallback: Kick -> Perc -> any
      promoteBest({ DrumClassifier::DrumClass::Kick, DrumClassifier::DrumClass::Perc },
                  DrumClassifier::DrumClass::Bass);
      // Perc fallback: Snare -> any
      promoteBest({ DrumClassifier::DrumClass::Snare },
                  DrumClassifier::DrumClass::Perc);

      auto addHit = [&](int step, DrumClassifier::DrumClass cls, float vel)
      {
         const SliceInfo* slice = PickSlice(byClass[(int)cls], rng);
         if (slice == nullptr)
            return;

         ArrangedHit hit;
         hit.step = step;
         hit.sample = slice->sample;
         hit.slice = slice->slice;
         hit.velocity = std::clamp(vel, 0.05f, 1.0f);

         const float u = rng.Range(-1.0f, 1.0f);
         if (cls == DrumClassifier::DrumClass::Kick || cls == DrumClassifier::DrumClass::Bass)
            hit.pitchOffsetSemis = p.randPitch * (u * 3.0f); // Low end stays in tune
         else
            hit.pitchOffsetSemis = p.randPitch * (u * 12.0f);

         hits.push_back(hit);
      };

      // 1. Kick template: anchored on step 0 of each bar + seeded syncopations
      for (int b = 0; b < bars; b++)
      {
         const int base = b * 16;
         // Step 0 anchor: always present
         addHit(base + 0, DrumClassifier::DrumClass::Kick, 1.0f);

         // Syncopation variations
         if (rng.Chance(0.70f))
            addHit(base + 10, DrumClassifier::DrumClass::Kick, rng.Range(0.85f, 0.95f)); // beat 3&
         if (rng.Chance(0.40f))
            addHit(base + 6, DrumClassifier::DrumClass::Kick, rng.Range(0.75f, 0.90f));  // beat 2&
         if (rng.Chance(0.50f))
            addHit(base + 8, DrumClassifier::DrumClass::Kick, rng.Range(0.80f, 0.95f));  // beat 3
         if (rng.Chance(0.35f))
            addHit(base + 14, DrumClassifier::DrumClass::Kick, rng.Range(0.70f, 0.85f)); // beat 4&
      }

      // 2. Snare / Clap template: on the backbeat (steps 4 and 12)
      const bool hasDistinctClap = !byClass[(int)DrumClassifier::DrumClass::Clap].empty();
      const bool hasDistinctSnare = !byClass[(int)DrumClassifier::DrumClass::Snare].empty();

      for (int b = 0; b < bars; b++)
      {
         const int base = b * 16;
         const int s1 = base + 4;
         const int s2 = base + 12;

         if (hasDistinctSnare && hasDistinctClap)
         {
            const int mode = rng.Int(0, 2);
            if (mode == 0) // Snare on 4 & 12, clap layers on 12
            {
               addHit(s1, DrumClassifier::DrumClass::Snare, 0.95f);
               addHit(s2, DrumClassifier::DrumClass::Snare, 1.0f);
               addHit(s2, DrumClassifier::DrumClass::Clap, 0.70f);
            }
            else if (mode == 1) // Snare on 4, clap on 12
            {
               addHit(s1, DrumClassifier::DrumClass::Snare, 0.95f);
               addHit(s2, DrumClassifier::DrumClass::Clap, 0.95f);
            }
            else // Both layered
            {
               addHit(s1, DrumClassifier::DrumClass::Snare, 0.90f);
               addHit(s1, DrumClassifier::DrumClass::Clap, 0.65f);
               addHit(s2, DrumClassifier::DrumClass::Snare, 0.95f);
               addHit(s2, DrumClassifier::DrumClass::Clap, 0.75f);
            }
         }
         else if (hasDistinctSnare)
         {
            addHit(s1, DrumClassifier::DrumClass::Snare, 0.95f);
            addHit(s2, DrumClassifier::DrumClass::Snare, 1.0f);
         }
         else
         {
            addHit(s1, DrumClassifier::DrumClass::Clap, 0.95f);
            addHit(s2, DrumClassifier::DrumClass::Clap, 1.0f);
         }

         // Occasional ghost note on step 15 or 11
         if (rng.Chance(0.30f))
            addHit(base + (rng.Chance(0.5f) ? 15 : 11), DrumClassifier::DrumClass::Snare, 0.35f);
      }

      // 3. HatClosed & HatOpen templates:
      // HatOpen on off-beats, choking HatClosed on those steps
      std::vector<bool> hatOpenSteps(totalSteps, false);
      for (int b = 0; b < bars; b++)
      {
         const int base = b * 16;
         // Off-beat candidate steps: 2, 6, 10, 14
         static const int kOffbeats[4] = { 2, 6, 10, 14 };
         for (int ob : kOffbeats)
         {
            if (rng.Chance(0.28f))
            {
               const int step = base + ob;
               hatOpenSteps[step] = true;
               addHit(step, DrumClassifier::DrumClass::HatOpen, rng.Range(0.70f, 0.85f));
            }
         }
      }

      // HatClosed as Euclidean E(k, 16) with k in 6..12
      const int kClosed = rng.Int(6, 12);
      const int rotClosed = rng.Int(0, 3);
      for (int b = 0; b < bars; b++)
      {
         const int base = b * 16;
         const auto euc = EuclideanRhythm(kClosed, 16, rotClosed);
         for (int s = 0; s < 16; s++)
         {
            const int step = base + s;
            if (euc[s] && !hatOpenSteps[step])
            {
               const bool downbeat = (s % 4 == 0);
               const float vel = downbeat ? rng.Range(0.80f, 0.95f) : rng.Range(0.50f, 0.70f);
               addHit(step, DrumClassifier::DrumClass::HatClosed, vel);
            }
         }
      }

      // 4. Bass template: follows kick with offsets, never on snare step (4, 12)
      for (int b = 0; b < bars; b++)
      {
         const int base = b * 16;
         for (int s = 0; s < 16; s++)
         {
            if (s == 4 || s == 12)
               continue; // never on snare step

            // Bass note chances on step 2, 7, 10, 14
            if ((s == 2 && rng.Chance(0.6f)) ||
                (s == 7 && rng.Chance(0.4f)) ||
                (s == 10 && rng.Chance(0.5f)) ||
                (s == 14 && rng.Chance(0.5f)))
            {
               addHit(base + s, DrumClassifier::DrumClass::Bass, rng.Range(0.80f, 0.95f));
            }
         }
      }

      // 5. Perc template: sparse Euclidean filling gaps
      const int kPerc = rng.Int(3, 5);
      const int rotPerc = rng.Int(0, 15);
      for (int b = 0; b < bars; b++)
      {
         const int base = b * 16;
         const auto euc = EuclideanRhythm(kPerc, 16, rotPerc);
         for (int s = 0; s < 16; s++)
         {
            if (euc[s] && s != 4 && s != 12)
            {
               addHit(base + s, DrumClassifier::DrumClass::Perc, rng.Range(0.55f, 0.80f));
            }
         }
      }

      // Sort hits chronologically by step
      std::sort(hits.begin(), hits.end(), [](const ArrangedHit& a, const ArrangedHit& b) {
         if (a.step != b.step)
            return a.step < b.step;
         return a.sample < b.sample;
      });

      return hits;
   }

   std::string SerializeHits(const std::vector<ArrangedHit>& hits)
   {
      std::ostringstream os;
      for (size_t i = 0; i < hits.size(); i++)
      {
         if (i > 0)
            os << ' ';
         char buf[64];
         snprintf(buf, sizeof(buf), "%d:%d:%d:%.3f:%.2f",
                  hits[i].step, hits[i].sample, hits[i].slice,
                  hits[i].velocity, hits[i].pitchOffsetSemis);
         os << buf;
      }
      return os.str();
   }

   std::vector<ArrangedHit> DeserializeHits(const std::string& blob)
   {
      std::vector<ArrangedHit> hits;
      std::istringstream is(blob);
      std::string token;
      while (is >> token)
      {
         ArrangedHit hit;
         if (sscanf(token.c_str(), "%d:%d:%d:%f:%f",
                    &hit.step, &hit.sample, &hit.slice,
                    &hit.velocity, &hit.pitchOffsetSemis) == 5)
         {
            hits.push_back(hit);
         }
      }
      return hits;
   }
}
