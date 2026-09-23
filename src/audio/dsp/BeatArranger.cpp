#include "BeatArranger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace BeatArranger
{
   using DrumClassifier::DrumClass;

   namespace
   {
      // Bjorklund's algorithm (Toussaint, BRIDGES 2005): distributes k pulses
      // as evenly as possible across n steps.
      std::vector<bool> Euclidean(int k, int n)
      {
         std::vector<bool> out(std::max(0, n), false);
         if (n <= 0)
            return out;
         k = std::clamp(k, 0, n);
         if (k == 0)
            return out;

         std::vector<std::vector<bool>> groups;
         for (int i = 0; i < k; i++)
            groups.push_back({ true });
         std::vector<std::vector<bool>> remainder;
         for (int i = 0; i < n - k; i++)
            remainder.push_back({ false });

         while (remainder.size() > 1)
         {
            const size_t pairs = std::min(groups.size(), remainder.size());
            std::vector<std::vector<bool>> newGroups;
            for (size_t i = 0; i < pairs; i++)
            {
               std::vector<bool> merged = groups[i];
               merged.insert(merged.end(), remainder[i].begin(), remainder[i].end());
               newGroups.push_back(merged);
            }
            std::vector<std::vector<bool>> newRemainder;
            for (size_t i = pairs; i < groups.size(); i++)
               newRemainder.push_back(groups[i]);
            for (size_t i = pairs; i < remainder.size(); i++)
               newRemainder.push_back(remainder[i]);

            groups = newGroups;
            remainder = newRemainder;
            if (remainder.size() <= 1)
               break;
         }

         out.clear();
         for (auto& g : groups)
            out.insert(out.end(), g.begin(), g.end());
         for (auto& r : remainder)
            out.insert(out.end(), r.begin(), r.end());
         if ((int)out.size() != n)
            out.resize(n, false);
         return out;
      }

      // Fix A3: pitch range is keyed on the SLICE's own class, never the
      // role it was placed/promoted into - a kick slice promoted into a hat
      // role must still get the narrow kick-like range.
      bool IsLowRangeClass(DrumClass c) { return c == DrumClass::Kick || c == DrumClass::Bass; }

      bool IsTonalClass(DrumClass c)
      {
         return c == DrumClass::Synth || c == DrumClass::Piano || c == DrumClass::Tonal;
      }

      float PitchRandFor(DrumClass cls, Rng& rng)
      {
         if (IsTonalClass(cls))
         {
            // Musical-interval-snapped, packed as a fraction of a ±12
            // semitone max range (the node applies the class range at play
            // time, so this value is already normalised to that range).
            static const float kSnapSemis[] = { 0.0f, 3.0f, 5.0f, 7.0f, 12.0f, -3.0f, -5.0f, -7.0f, -12.0f };
            const int idx = std::min(8, (int)(rng.Next01() * 9.0f));
            return kSnapSemis[idx] / 12.0f;
         }
         // Drums: continuous -1..1, node scales by ±3 (kick/bass) or ±12
         // (everything else) semitones per IsLowRangeClass.
         return rng.NextBipolar();
      }

      const SliceInfo* PickWeighted(const std::vector<const SliceInfo*>& v, Rng& rng)
      {
         if (v.empty())
            return nullptr;
         float total = 0.0f;
         for (auto* s : v)
            total += std::max(0.05f, s->confidence);
         float r = rng.Next01() * total;
         for (auto* s : v)
         {
            const float w = std::max(0.05f, s->confidence);
            if (r < w)
               return s;
            r -= w;
         }
         return v.back();
      }
   } // namespace

   std::vector<ArrangedHit> Generate(const std::vector<SliceInfo>& slices, const ArrangeParams& params, uint32_t seed)
   {
      std::vector<ArrangedHit> hits;
      const int totalSteps = std::max(1, params.stepsPerBar * std::max(1, params.bars));
      if (slices.empty())
         return hits;

      Rng rng(seed);

      auto bucket = [&](DrumClass c)
      {
         std::vector<const SliceInfo*> v;
         for (auto& s : slices)
            if (s.cls == c)
               v.push_back(&s);
         return v;
      };

      auto kicks = bucket(DrumClass::Kick);
      auto basses = bucket(DrumClass::Bass);
      auto snares = bucket(DrumClass::Snare);
      auto claps = bucket(DrumClass::Clap);
      auto hatsClosed = bucket(DrumClass::HatClosed);
      auto hatsOpen = bucket(DrumClass::HatOpen);
      auto percs = bucket(DrumClass::Perc);
      auto synths = bucket(DrumClass::Synth);
      auto pianos = bucket(DrumClass::Piano);
      auto tonals = bucket(DrumClass::Tonal);

      // ---- Fix A1: feature-based single-slice promotion for missing
      // essential roles. No copying an entire fallback class's slice list -
      // if there's no good candidate, the role stays empty. ----
      const SliceInfo* promotedKick = nullptr;
      const SliceInfo* promotedHat = nullptr;
      const SliceInfo* promotedSnare = nullptr;

      if (kicks.empty())
      {
         float bestScore = 1e18f;
         for (auto& s : slices)
         {
            if (s.cls == DrumClass::HatClosed || s.cls == DrumClass::HatOpen)
               continue;
            const float score = s.centroid + s.decaySec * 20000.0f; // lowest centroid + shortest decay
            if (score < bestScore)
            {
               bestScore = score;
               promotedKick = &s;
            }
         }
      }
      if (hatsClosed.empty() && hatsOpen.empty())
      {
         float bestC = -1.0f;
         for (auto& s : slices)
         {
            if (s.centroid > bestC)
            {
               bestC = s.centroid;
               promotedHat = &s;
            }
         }
      }
      if (snares.empty() && claps.empty())
      {
         float bestScore = 1e18f;
         for (auto& s : slices)
         {
            const float score = std::fabs(s.centroid - 2000.0f); // mid-centroid
            if (score < bestScore)
            {
               bestScore = score;
               promotedSnare = &s;
            }
         }
      }

      // ---- Kick: Euclidean, ~1 pulse per 8 steps (quarter notes at 1/16 grid) ----
      std::vector<int> kickSteps;
      if (!kicks.empty() || promotedKick != nullptr)
      {
         const int k = std::max(1, totalSteps / 8);
         const auto pattern = Euclidean(k, totalSteps);
         for (int s = 0; s < totalSteps; s++)
         {
            if (!pattern[s])
               continue;
            const SliceInfo* pick = kicks.empty() ? promotedKick : PickWeighted(kicks, rng);
            if (pick == nullptr)
               continue;
            kickSteps.push_back(s);
            hits.push_back({ s, pick->slice, 0.85f + rng.Next01() * 0.15f, PitchRandFor(pick->cls, rng) });
         }
      }

      // ---- Bass: offsets derived from THIS generation's actual kick steps
      // (fix A4), not a hardcoded step list. ----
      if (!basses.empty() && !kickSteps.empty())
      {
         for (int ks : kickSteps)
         {
            if (rng.Next01() >= 0.6f)
               continue;
            const int offset = (rng.Next01() < 0.5f) ? 0 : std::max(1, totalSteps / 8);
            const int step = (ks + offset) % totalSteps;
            const SliceInfo* pick = PickWeighted(basses, rng);
            if (pick != nullptr)
               hits.push_back({ step, pick->slice, 0.75f + rng.Next01() * 0.2f, PitchRandFor(pick->cls, rng) });
         }
      }

      // ---- Snare/Clap: backbeat, one per half-bar. Fix A2: no
      // hasDistinctClap/hasDistinctSnare branch tangle, just one dedup pass
      // at the end. ----
      {
         const std::vector<const SliceInfo*>* group = nullptr;
         std::vector<const SliceInfo*> promotedGroup;
         if (!snares.empty())
            group = &snares;
         else if (!claps.empty())
            group = &claps;
         else if (promotedSnare != nullptr)
         {
            promotedGroup.push_back(promotedSnare);
            group = &promotedGroup;
         }

         if (group != nullptr)
         {
            for (int bar = 0; bar < params.bars; bar++)
            {
               const int step = bar * params.stepsPerBar + params.stepsPerBar / 2;
               if (step >= totalSteps)
                  continue;
               const SliceInfo* pick = PickWeighted(*group, rng);
               if (pick != nullptr)
                  hits.push_back({ step, pick->slice, 0.9f, PitchRandFor(pick->cls, rng) });
            }
         }
      }

      // ---- Closed hats: dense Euclidean fill ----
      if (!hatsClosed.empty() || promotedHat != nullptr)
      {
         const int k = std::max(2, totalSteps / 2);
         const auto pattern = Euclidean(k, totalSteps);
         for (int s = 0; s < totalSteps; s++)
         {
            if (!pattern[s])
               continue;
            const SliceInfo* pick = hatsClosed.empty() ? promotedHat : PickWeighted(hatsClosed, rng);
            if (pick != nullptr)
               hits.push_back({ s, pick->slice, 0.55f + rng.Next01() * 0.2f, PitchRandFor(pick->cls, rng) });
         }
      }

      // ---- Open hats: sparse ----
      if (!hatsOpen.empty())
      {
         const int k = std::max(1, totalSteps / 8);
         const auto pattern = Euclidean(k, totalSteps);
         for (int s = 0; s < totalSteps; s++)
         {
            if (!pattern[s])
               continue;
            const SliceInfo* pick = PickWeighted(hatsOpen, rng);
            if (pick != nullptr)
               hits.push_back({ s, pick->slice, 0.7f, PitchRandFor(pick->cls, rng) });
         }
      }

      // ---- Perc: fills gaps, sparse ----
      if (!percs.empty())
      {
         std::vector<bool> used(totalSteps, false);
         for (auto& h : hits)
            used[h.step] = true;
         for (int s = 0; s < totalSteps; s++)
         {
            if (used[s] || rng.Next01() >= 0.12f)
               continue;
            const SliceInfo* pick = PickWeighted(percs, rng);
            if (pick != nullptr)
            {
               hits.push_back({ s, pick->slice, 0.6f + rng.Next01() * 0.2f, PitchRandFor(pick->cls, rng) });
               used[s] = true;
            }
         }
      }

      // ---- Tonal roles: sparse 1-4 stabs per bar, musical-interval pitch ----
      {
         std::vector<const SliceInfo*> tonalPool;
         for (auto* s : synths)
            tonalPool.push_back(s);
         for (auto* s : pianos)
            tonalPool.push_back(s);
         for (auto* s : tonals)
            tonalPool.push_back(s);

         if (!tonalPool.empty())
         {
            for (int bar = 0; bar < params.bars; bar++)
            {
               const int perBar = 1 + (int)(rng.Next01() * 3.999f);
               for (int i = 0; i < perBar; i++)
               {
                  const int step = bar * params.stepsPerBar + (int)(rng.Next01() * params.stepsPerBar);
                  const SliceInfo* pick = PickWeighted(tonalPool, rng);
                  if (pick != nullptr)
                     hits.push_back({ step, pick->slice, 0.65f + rng.Next01() * 0.25f, PitchRandFor(pick->cls, rng) });
               }
            }
         }
      }

      // ---- Dedup: at most one hit per (step, slice) - fix A2 ----
      std::sort(hits.begin(), hits.end(), [](const ArrangedHit& a, const ArrangedHit& b)
      {
         if (a.step != b.step)
            return a.step < b.step;
         return a.slice < b.slice;
      });
      hits.erase(std::unique(hits.begin(), hits.end(), [](const ArrangedHit& a, const ArrangedHit& b)
      {
         return a.step == b.step && a.slice == b.slice;
      }),
                 hits.end());

      // ---- Fix A5: final ordering is (step, slice), no `sample` field exists anymore ----
      std::stable_sort(hits.begin(), hits.end(), [](const ArrangedHit& a, const ArrangedHit& b)
      {
         if (a.step != b.step)
            return a.step < b.step;
         return a.slice < b.slice;
      });

      return hits;
   }

   // Fix A6: drop the old `sample` field, use %.9g for exact float round-trip.
   std::string SerializeHits(const std::vector<ArrangedHit>& hits)
   {
      std::string out;
      char buf[128];
      for (const auto& h : hits)
      {
         snprintf(buf, sizeof(buf), "%d:%d:%.9g:%.9g;", h.step, h.slice, h.velocity, h.pitchRand);
         out += buf;
      }
      return out;
   }

   std::vector<ArrangedHit> DeserializeHits(const std::string& blob)
   {
      std::vector<ArrangedHit> hits;
      size_t pos = 0;
      while (pos < blob.size())
      {
         const size_t end = blob.find(';', pos);
         if (end == std::string::npos)
            break;
         const std::string token = blob.substr(pos, end - pos);
         pos = end + 1;

         ArrangedHit h;
         if (sscanf(token.c_str(), "%d:%d:%f:%f", &h.step, &h.slice, &h.velocity, &h.pitchRand) == 4)
            hits.push_back(h);
      }
      return hits;
   }
}
