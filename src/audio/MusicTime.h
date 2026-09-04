#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "core/Transport.h"

// The musical pulse and the musical scale, in one header shared by DSP and
// UI - for the reason src/audio/SynthModes.h states about its own tables: a
// dropdown built from one list and a switch written against another drift
// apart silently, and the symptom (a rate or a scale that resolves to the
// wrong thing) looks like a DSP bug rather than a list bug. Every rhythmic
// node in P3c/P3a-part-2 reads RateDivision/BeatsFor from here; every note
// node reads the scale table/SnapToScale/DegreeToNote from here.
namespace MusicTime
{
   // Index order IS the dropdown order, slowest to fastest - see
   // docs/plans/audio/P3c-P3a2-design.md §0.1.
   enum RateDivision
   {
      k4Bars = 0,
      k2Bars,
      k1Bar,
      kHalf,
      kHalfDot,
      kHalfTrip,
      kQuarter,
      kQuarterDot,
      kQuarterTrip,
      kEighth,
      kEighthDot,
      kEighthTrip,
      kSixteenth,
      kSixteenthDot,
      kSixteenthTrip,
      kThirtySecond,
      kThirtySecondDot,
      kThirtySecondTrip,
      kSixtyFourth,
      kNumRateDivisions
   };

   // Reads the live time signature (§0.2) - a bar is not a fixed 4 beats.
   inline double BarsToBeats(double bars)
   {
      return bars * Transport::Instance().BeatsPerBar();
   }

   // The division's length in beats, the unit Transport::Beats() already
   // speaks. Dotted = x1.5, triplet = x2/3 - both computed straight off the
   // even divisions rather than tabulated separately, so the arithmetic
   // between a division and its dotted/triplet variants can never drift.
   inline double BeatsFor(RateDivision d)
   {
      switch (d)
      {
         case k4Bars: return BarsToBeats(4.0);
         case k2Bars: return BarsToBeats(2.0);
         case k1Bar: return BarsToBeats(1.0);
         case kHalf: return 2.0;
         case kHalfDot: return 2.0 * 1.5;
         case kHalfTrip: return 2.0 * 2.0 / 3.0;
         case kQuarter: return 1.0;
         case kQuarterDot: return 1.0 * 1.5;
         case kQuarterTrip: return 1.0 * 2.0 / 3.0;
         case kEighth: return 0.5;
         case kEighthDot: return 0.5 * 1.5;
         case kEighthTrip: return 0.5 * 2.0 / 3.0;
         case kSixteenth: return 0.25;
         case kSixteenthDot: return 0.25 * 1.5;
         case kSixteenthTrip: return 0.25 * 2.0 / 3.0;
         case kThirtySecond: return 0.125;
         case kThirtySecondDot: return 0.125 * 1.5;
         case kThirtySecondTrip: return 0.125 * 2.0 / 3.0;
         case kSixtyFourth: return 0.0625;
         default: return 1.0;
      }
   }

   // Hz for one full cycle at rate division `d`, at `bpm` - the inverse of
   // BeatsFor's period, used by Chorus/Flanger/Phaser's sync-to-tempo rate
   // mode so their LFO can lock to the same rate-division table Delay/Stutter
   // already use for their tempo-synced time, rather than a separate Hz
   // conversion living in each kernel.
   inline double HzForRateDivision(RateDivision d, double bpm)
   {
      const double periodSeconds = BeatsFor(d) * 60.0 / std::max(1.0, bpm);
      return periodSeconds > 0.0 ? 1.0 / periodSeconds : 0.0;
   }

   // Labels every DAW uses, in the same slowest-to-fastest order as the enum.
   inline const char* const* RateDivisionNames()
   {
      static const char* const kNames[kNumRateDivisions] = {
         "4 bars", "2 bars", "1 bar",
         "1/2", "1/2.", "1/2T",
         "1/4", "1/4.", "1/4T",
         "1/8", "1/8.", "1/8T",
         "1/16", "1/16.", "1/16T",
         "1/32", "1/32.", "1/32T",
         "1/64"
      };
      return kNames;
   }

   inline const char* RateDivisionName(int d)
   {
      return (d >= 0 && d < kNumRateDivisions) ? RateDivisionNames()[d] : RateDivisionNames()[kSixteenth];
   }

   // std::vector<std::string> because that is what AudioKnobRow::Dropdown
   // takes (see SynthModes.h's WarpModeList for the same pattern). Built once,
   // read-only afterwards.
   inline const std::vector<std::string>& RateDivisionList()
   {
      static std::vector<std::string> list;
      if (list.empty())
         for (int i = 0; i < kNumRateDivisions; i++)
            list.push_back(RateDivisionNames()[i]);
      return list;
   }

   // Finds the closest RateDivision for an arbitrary beat length.
   inline RateDivision NearestRateDivision(double beats)
   {
      RateDivision best = kSixteenth;
      double bestDiff = 1e9;
      for (int i = 0; i < kNumRateDivisions; i++)
      {
         const double d = std::abs(BeatsFor((RateDivision)i) - beats);
         if (d < bestDiff) { bestDiff = d; best = (RateDivision)i; }
      }
      return best;
   }

   // Shared "quantize to grid" list used by Quantizer and Note Capturer:
   // index 0 is "Off", index 1..kNumRateDivisions corresponds to RateDivision (index - 1).
   inline const std::vector<std::string>& QuantizeGridList()
   {
      static std::vector<std::string> list;
      if (list.empty())
      {
         list.push_back("Off");
         for (int i = 0; i < kNumRateDivisions; i++)
            list.push_back(RateDivisionNames()[i]);
      }
      return list;
   }

   inline double QuantizeGridBeats(int gridIndex)
   {
      if (gridIndex <= 0 || gridIndex > kNumRateDivisions)
         return 0.0;
      return BeatsFor((RateDivision)(gridIndex - 1));
   }

   inline const char* QuantizeGridName(int gridIndex)
   {
      if (gridIndex <= 0 || gridIndex > kNumRateDivisions)
         return "Off";
      return RateDivisionName(gridIndex - 1);
   }

   // ------------------------------------------------------------------ scale
   // Name and interval set declared side by side, same reasoning as the rate
   // table above. Intervals are semitone offsets from the root within one
   // octave, ascending.
   enum ScaleType
   {
      kMajor = 0,
      kNaturalMinor,
      kHarmonicMinor,
      kMelodicMinor,
      kDorian,
      kPhrygian,
      kLydian,
      kMixolydian,
      kLocrian,
      kMajorPentatonic,
      kMinorPentatonic,
      kBlues,
      kWholeTone,
      kChromatic,
      kNumScaleTypes
   };

   enum SnapDir
   {
      kSnapUp = 0,
      kSnapDown,
      kSnapNearest
   };

   struct ScaleDef
   {
      const char* name;
      int count;
      int intervals[12];
   };

   inline const ScaleDef& ScaleTable(int scale)
   {
      static const ScaleDef kScales[kNumScaleTypes] = {
         { "major",            7,  { 0, 2, 4, 5, 7, 9, 11 } },
         { "natural minor",    7,  { 0, 2, 3, 5, 7, 8, 10 } },
         { "harmonic minor",   7,  { 0, 2, 3, 5, 7, 8, 11 } },
         { "melodic minor",    7,  { 0, 2, 3, 5, 7, 9, 11 } },
         { "dorian",           7,  { 0, 2, 3, 5, 7, 9, 10 } },
         { "phrygian",         7,  { 0, 1, 3, 5, 7, 8, 10 } },
         { "lydian",           7,  { 0, 2, 4, 6, 7, 9, 11 } },
         { "mixolydian",       7,  { 0, 2, 4, 5, 7, 9, 10 } },
         { "locrian",          7,  { 0, 1, 3, 5, 6, 8, 10 } },
         { "major pentatonic", 5,  { 0, 2, 4, 7, 9 } },
         { "minor pentatonic", 5,  { 0, 3, 5, 7, 10 } },
         { "blues",            6,  { 0, 3, 5, 6, 7, 10 } },
         { "whole tone",       6,  { 0, 2, 4, 6, 8, 10 } },
         { "chromatic",        12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
      };
      const int safe = (scale >= 0 && scale < kNumScaleTypes) ? scale : kMajor;
      return kScales[safe];
   }

   inline const std::vector<std::string>& ScaleTypeList()
   {
      static std::vector<std::string> list;
      if (list.empty())
         for (int i = 0; i < kNumScaleTypes; i++)
            list.push_back(ScaleTable(i).name);
      return list;
   }

   inline bool ScaleContainsPitchClass(int scale, int pc)
   {
      const ScaleDef& def = ScaleTable(scale);
      for (int i = 0; i < def.count; i++)
         if (def.intervals[i] == pc)
            return true;
      return false;
   }

   // Moves `midiNote` to the nearest note that is in `scale` relative to
   // `root` (0 = C .. 11 = B), in the direction `dir` asks for. A note
   // already in the scale is returned unchanged.
   inline int SnapToScale(int midiNote, int root, int scale, SnapDir dir)
   {
      const int pc = ((midiNote - root) % 12 + 12) % 12;
      if (ScaleContainsPitchClass(scale, pc))
         return midiNote;
      for (int delta = 1; delta <= 12; delta++)
      {
         const int upPc = (pc + delta) % 12;
         const int downPc = ((pc - delta) % 12 + 12) % 12;
         const bool upOk = (dir != kSnapDown) && ScaleContainsPitchClass(scale, upPc);
         const bool downOk = (dir != kSnapUp) && ScaleContainsPitchClass(scale, downPc);
         if (dir == kSnapNearest)
         {
            // Equidistant: up wins, an arbitrary but deterministic tie-break.
            if (upOk) return midiNote + delta;
            if (downOk) return midiNote - delta;
         }
         else if (upOk) return midiNote + delta;
         else if (downOk) return midiNote - delta;
      }
      return midiNote; // unreachable for any real scale table (chromatic always matches at delta 0)
   }

   // Floor division (rounds toward -infinity), needed below because C++'s
   // `/` truncates toward zero - `-1 / 7 == 0` would put degree -1 in octave
   // 0 at scale index -1 instead of octave -1 at index (count - 1).
   inline int FloorDiv(int a, int b)
   {
      int q = a / b;
      const int r = a % b;
      if (r != 0 && ((r < 0) != (b < 0)))
         q--;
      return q;
   }

   // Lets a sequencer store scale degrees rather than semitones, so changing
   // the key or scale transposes the whole patch in key. `degree` may be
   // negative or larger than the scale's step count; it wraps into further
   // octaves either way.
   inline int DegreeToNote(int degree, int octave, int root, int scale)
   {
      const ScaleDef& def = ScaleTable(scale);
      const int n = def.count > 0 ? def.count : 1;
      const int extraOct = FloorDiv(degree, n);
      const int idx = degree - extraOct * n;
      return root + 12 * (octave + extraOct) + def.intervals[idx];
   }
}
