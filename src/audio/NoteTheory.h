#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "MusicTime.h"

// Small, allocation-free music-theory helpers shared by the generative note nodes (Random Note
// Generator, Chorder). Everything here is safe on the audio thread: fixed stack arrays, no locks.
// Pure functions of their arguments plus a caller-owned state struct, so a headless test can drive
// them without an engine (INFINITE_PREDMIDITEST).
namespace NoteTheory
{
   constexpr int kMaxScaleNotes = 128;

   // Every in-scale MIDI note in [lo, hi], ascending. Returns the count written.
   inline int CollectScaleNotes(int root, int scale, int lo, int hi, int* out, int maxOut)
   {
      if (lo > hi)
         std::swap(lo, hi);
      int n = 0;
      for (int note = std::max(0, lo); note <= std::min(127, hi) && n < maxOut; note++)
         if (MusicTime::ScaleContainsPitchClass(scale, ((note - root) % 12 + 12) % 12))
            out[n++] = note;
      return n;
   }

   // Tonal stability of a pitch class relative to the root (Krumhansl-style ranking, flattened to
   // three tiers): tonic and fifth are most stable, the third next, everything else passing tone.
   inline float Stability(int pcRelativeToRoot)
   {
      switch (pcRelativeToRoot)
      {
         case 0: return 1.0f;
         case 7: return 0.85f;
         case 3:
         case 4: return 0.7f;
         default: return 0.35f;
      }
   }

   // ---------------------------------------------------------------- melody

   // State of the melodic random walk. All fields are the previous step's outcome.
   struct MelodyState
   {
      int prevNote = -1;
      int prevDir = 0;   // -1 / 0 / +1
      int prevLeap = 0;  // semitones moved by the previous step
      int phraseStep = 0;
   };

   constexpr int kPhraseLength = 8;

   // Picks the next note of a constrained melodic walk over `notes[0..n)` (ascending, in scale, in
   // range). The distribution is the product of four musical pressures rather than a uniform
   // semitone jump snapped to the scale (which over-weights notes next to scale gaps):
   //   proximity   - small steps are far likelier than leaps (Gaussian in semitones, width ~ wander)
   //   gap fill    - after a leap of a fourth or more, continuing the same way is discouraged and
   //                 turning back is encouraged (Meyer 1956, Narmour's implication-realisation)
   //   stability   - chord tones and the tonic are favoured, sharply on strong beats
   //   centre pull - mean reversion toward the middle of the range keeps the line from drifting
   // Phrase end (every kPhraseLength steps) triples the tonic's weight, so lines cadence home.
   // `metric` is 0 (off-beat) .. 1 (downbeat). `u01` is a uniform draw in [0, 1).
   inline int PickMelodyNote(MelodyState& st, const int* notes, int n, int root, int wander, float metric, float u01)
   {
      if (n <= 0)
         return -1;
      const bool first = st.prevNote < 0;
      const int prev = first ? notes[n / 2] : st.prevNote;
      const float centre = 0.5f * (float)(notes[0] + notes[n - 1]);
      const float halfSpan = std::max(3.0f, 0.5f * (float)(notes[n - 1] - notes[0]));
      const float sigma = std::max(1.5f, 0.6f * (float)std::max(1, wander));
      const bool phraseEnd = st.phraseStep % kPhraseLength == kPhraseLength - 1;
      const float stabPow = 1.0f + 1.5f * metric;

      float w[kMaxScaleNotes];
      float total = 0.0f;
      for (int i = 0; i < n; i++)
      {
         const int note = notes[i];
         const int ds = note - prev;
         const float a = (float)std::abs(ds);
         float wt = std::exp(-0.5f * (a * a) / (sigma * sigma));
         if (ds == 0)
            wt *= 0.3f; // repeats are allowed, not favoured
         if (st.prevLeap >= 5 && st.prevDir != 0 && ds != 0)
            wt *= ((ds > 0) == (st.prevDir > 0)) ? 0.3f : 1.8f; // gap fill
         const int rel = ((note - root) % 12 + 12) % 12;
         wt *= 0.25f + 0.75f * std::pow(Stability(rel), stabPow);
         if (phraseEnd && rel == 0)
            wt *= 3.0f;
         const float c = (float)note - centre;
         wt *= 0.35f + 0.65f * std::exp(-0.5f * (c * c) / (halfSpan * halfSpan * 0.64f));
         w[i] = wt;
         total += wt;
      }
      if (total <= 0.0f)
         return notes[n / 2];

      float r = std::clamp(u01, 0.0f, 0.99999f) * total;
      int pick = n - 1;
      for (int i = 0; i < n; i++)
      {
         r -= w[i];
         if (r < 0.0f)
         {
            pick = i;
            break;
         }
      }
      const int note = notes[pick];
      const int d = note - prev;
      st.prevDir = d > 0 ? 1 : (d < 0 ? -1 : 0);
      st.prevLeap = std::abs(d);
      st.prevNote = note;
      st.phraseStep++;
      return note;
   }

   // Metric strength of the step that starts at `beats`: 1 on a bar line, 0.6 on a beat, 0.3 on an
   // eighth, else 0. Works for any time signature via beatsPerBar.
   inline float MetricStrength(double beats, double beatsPerBar)
   {
      auto onGrid = [beats](double g) {
         const double q = beats / g;
         return std::abs(q - std::round(q)) < 1e-3;
      };
      if (onGrid(std::max(1.0, beatsPerBar)))
         return 1.0f;
      if (onGrid(1.0))
         return 0.6f;
      if (onGrid(0.5))
         return 0.3f;
      return 0.0f;
   }

   // ----------------------------------------------------------------- chords

   // Next chord root as a 0-based scale degree, from a functional-harmony transition table for
   // seven-note scales (tonic pulls to subdominant/dominant, dominant resolves home, leading tone
   // resolves up) and a tonic/dominant-weighted fallback for other scales. `u01` in [0, 1).
   inline int NextChordDegree(int prevDegree, int count, float u01)
   {
      count = std::max(1, count);
      float w[12] = {};
      if (count == 7)
      {
         static const float kT[7][7] = {
            { 0.05f, 0.10f, 0.05f, 0.30f, 0.30f, 0.20f, 0.00f }, // I
            { 0.05f, 0.00f, 0.05f, 0.10f, 0.60f, 0.05f, 0.15f }, // ii
            { 0.05f, 0.10f, 0.00f, 0.30f, 0.05f, 0.50f, 0.00f }, // iii
            { 0.20f, 0.15f, 0.00f, 0.02f, 0.50f, 0.08f, 0.05f }, // IV
            { 0.60f, 0.00f, 0.05f, 0.05f, 0.02f, 0.25f, 0.03f }, // V
            { 0.05f, 0.30f, 0.05f, 0.35f, 0.20f, 0.00f, 0.05f }, // vi
            { 0.70f, 0.00f, 0.15f, 0.00f, 0.05f, 0.10f, 0.00f }, // vii
         };
         const int p = ((prevDegree % 7) + 7) % 7;
         for (int i = 0; i < 7; i++)
            w[i] = kT[p][i];
      }
      else
      {
         const int p = ((prevDegree % count) + count) % count;
         const int dom = std::min(count - 1, (count * 4) / 7);
         for (int i = 0; i < count; i++)
            w[i] = (i == p ? 0.1f : 1.0f) * (i == 0 ? 2.5f : 1.0f) * (i == dom ? 2.0f : 1.0f);
      }
      float total = 0.0f;
      for (int i = 0; i < count; i++)
         total += w[i];
      float r = std::clamp(u01, 0.0f, 0.99999f) * total;
      for (int i = 0; i < count; i++)
      {
         r -= w[i];
         if (r < 0.0f)
            return i;
      }
      return 0;
   }

   constexpr int kMaxVoicing = 8;

   // Voices a chord whose tones are the scale degrees rootDegree + offsets[i] (ascending offsets)
   // by choosing the inversion and octave whose sorted notes move least from `prev` (smooth voice
   // leading), while staying in a comfortable register. With no previous chord it centres on
   // middle C. Writes `size` ascending notes to `out` and returns size.
   inline int VoiceDegrees(const int* offsets, int size, int rootDegree, int root, int scale, const int* prev, int prevN,
                           int* out)
   {
      size = std::clamp(size, 1, kMaxVoicing);
      int base[kMaxVoicing];
      for (int i = 0; i < size; i++)
         base[i] = MusicTime::DegreeToNote(rootDegree + offsets[i], 4, root, scale);

      float bestCost = 1e30f;
      int best[kMaxVoicing];
      for (int inv = 0; inv < size; inv++)
      {
         for (int oct = -1; oct <= 2; oct++)
         {
            int cand[kMaxVoicing];
            for (int i = 0; i < size; i++)
               cand[i] = base[i] + (i < inv ? 12 : 0) + 12 * oct;
            std::sort(cand, cand + size);
            float mean = 0.0f;
            for (int i = 0; i < size; i++)
               mean += (float)cand[i];
            mean /= (float)size;

            float cost = 0.3f * std::abs(mean - 60.0f);
            if (cand[0] < 36 || cand[size - 1] > 96)
               cost += 100.0f;
            if (prevN > 0)
            {
               const int m = std::min(size, prevN);
               for (int i = 0; i < m; i++)
                  cost += (float)std::abs(cand[size - 1 - i] - prev[prevN - 1 - i]);
            }
            if (cost < bestCost)
            {
               bestCost = cost;
               for (int i = 0; i < size; i++)
                  best[i] = cand[i];
            }
         }
      }
      for (int i = 0; i < size; i++)
         out[i] = std::clamp(best[i], 0, 127);
      return size;
   }

   // A stacked-thirds chord of `size` tones on scale degree `rootDegree`, voiced as VoiceDegrees.
   inline int VoiceChord(int rootDegree, int size, int root, int scale, const int* prev, int prevN, int* out)
   {
      size = std::clamp(size, 1, kMaxVoicing);
      int offsets[kMaxVoicing];
      for (int i = 0; i < size; i++)
         offsets[i] = 2 * i;
      return VoiceDegrees(offsets, size, rootDegree, root, scale, prev, prevN, out);
   }

   // Chord types a progression cell can ask for. kChordAuto is the Chorder's chord-size knob.
   enum ChordType { kChordAuto = 0, kChordSeventh, kChordSus2, kChordSus4, kNumChordTypes };

   // Scale-degree offsets for a chord type; returns the tone count.
   inline int ChordTypeOffsets(int type, int autoSize, int* out)
   {
      switch (type)
      {
         case kChordSeventh: out[0] = 0; out[1] = 2; out[2] = 4; out[3] = 6; return 4;
         case kChordSus2: out[0] = 0; out[1] = 1; out[2] = 4; return 3;
         case kChordSus4: out[0] = 0; out[1] = 3; out[2] = 4; return 3;
         default:
         {
            const int n = std::clamp(autoSize, 1, kMaxVoicing);
            for (int i = 0; i < n; i++)
               out[i] = 2 * i;
            return n;
         }
      }
   }

   // Roman numeral for scale degree `degree` (0-based) in `scale`: upper case for a major triad,
   // lower case for minor, lower case + "°" for diminished, upper case + "+" for augmented. Scales
   // with fewer than three tones per octave read as upper case.
   inline void RomanNumeral(int degree, int scale, char* out, size_t outSize)
   {
      static const char* const kUpper[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII" };
      static const char* const kLower[] = { "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii" };
      const int count = std::max(1, MusicTime::ScaleTable(scale).count);
      const int d = ((degree % count) + count) % count;
      const int r = MusicTime::DegreeToNote(d, 0, 0, scale);
      const int third = MusicTime::DegreeToNote(d + 2, 0, 0, scale) - r;
      const int fifth = MusicTime::DegreeToNote(d + 4, 0, 0, scale) - r;
      const bool minorThird = third == 3;
      const char* suffix = (minorThird && fifth == 6) ? "\xC2\xB0" : (!minorThird && fifth == 8) ? "+" : "";
      snprintf(out, outSize, "%s%s", (minorThird ? kLower : kUpper)[std::min(d, 11)], suffix);
   }

   // Well-known chord progressions as scale degrees (0 = I; -1 = hold the chord before), written
   // for Infinite. Every one is diatonic, so it follows whatever key and scale is set.
   struct Progression
   {
      const char* name;
      const char* category;
      int steps;
      int degrees[8];
      int types[8];
   };

   inline const Progression* Progressions(int& count)
   {
      static const Progression kList[] = {
         { "I V vi IV", "pop", 4, { 0, 4, 5, 3 }, {} },
         { "vi IV I V", "pop", 4, { 5, 3, 0, 4 }, {} },
         { "I vi IV V", "pop", 4, { 0, 5, 3, 4 }, {} },
         { "I IV vi V", "pop", 4, { 0, 3, 5, 4 }, {} },
         { "IV I V vi", "pop", 4, { 3, 0, 4, 5 }, {} },
         { "IV V iii vi", "pop", 4, { 3, 4, 2, 5 }, {} },
         { "I IV", "pop", 2, { 0, 3 }, {} },
         { "I V vi IV, 2 bars each", "pop", 8, { 0, -1, 4, -1, 5, -1, 3, -1 }, {} },
         { "ii V I", "jazz", 4, { 1, 4, 0, -1 }, { 1, 1, 1, 0 } },
         { "I vi ii V", "jazz", 4, { 0, 5, 1, 4 }, { 1, 1, 1, 1 } },
         { "iii vi ii V", "jazz", 4, { 2, 5, 1, 4 }, { 1, 1, 1, 1 } },
         { "I IV iii vi ii V I", "jazz", 8, { 0, 3, 2, 5, 1, 4, 0, -1 }, { 1, 1, 1, 1, 1, 1, 1, 0 } },
         { "I Isus4 IV Vsus2", "suspended", 4, { 0, 0, 3, 4 }, { 0, 3, 0, 2 } },
         { "Isus2 IV vi Vsus4", "suspended", 4, { 0, 3, 5, 4 }, { 2, 0, 0, 3 } },
         { "Canon", "classical", 8, { 0, 4, 5, 2, 3, 0, 3, 4 }, {} },
         { "Descending (I VII VI V)", "classical", 4, { 0, 6, 5, 4 }, {} },
         { "I IV V I", "classical", 4, { 0, 3, 4, 0 }, {} },
      };
      count = (int)(sizeof(kList) / sizeof(kList[0]));
      return kList;
   }
}
