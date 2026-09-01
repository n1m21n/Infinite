#pragma once

#include <string>
#include <vector>

// The mode tables the Wavetable node's two engines are configured with:
// which warp is applied to the table read, and which filter follows it.
//
// They live here, in one header shared by the DSP and the UI, for the reason
// every enum-plus-name-list eventually needs to: a dropdown built from one
// list and a switch written against another drift apart silently, and the
// symptom (a mode that selects the wrong algorithm) looks like a DSP bug
// rather than a list bug. Name and behaviour are declared side by side, and
// `kNumWarpModes` / `kNumFilterTypes` are derived from the arrays so adding a
// mode cannot leave either side behind.
namespace SynthModes
{
   // ------------------------------------------------------------------ warp
   // Warp is the single "make this table sound like something else" control.
   // The modes fall into three families, and which family a mode is in
   // decides *where* in the read it acts:
   //
   //   phase-domain, cross-modulated (FM, PD)  - offset the read phase by the
   //       other engine's output before the table lookup;
   //   amplitude-domain, cross-modulated (AM, RM) - scale the sample after it;
   //   self-contained shapers (everything else) - a fixed function of phase or
   //       of the sample, driven only by the warp amount.
   //
   // The four cross-modulated modes are the sketch's "from wavetable B". They
   // read the *other* engine, which is a cycle: A modulating B while B
   // modulates A has no defined evaluation order. It is broken the way
   // feedback FM always is, with a one-sample delay - each engine reads the
   // other's previous sample - which is stable, costs one float of state, and
   // is inaudible at any sample rate this runs at.
   //
   // When the other engine is switched off there is nothing to read, so those
   // modes fall back to an internal sine operator running at
   // `frequency * warpRatio`. Without the fallback, selecting "FM (b)" on a
   // patch whose engine B is off does nothing at all, which reads as the mode
   // being broken rather than as a missing source.
   enum WarpMode
   {
      kWarpOff = 0,
      kWarpFM,        // phase modulation by the other engine
      kWarpAM,        // unipolar amplitude modulation
      kWarpRM,        // bipolar ring modulation
      kWarpPD,        // phase distortion, bend amount driven by the other engine
      kWarpSync,      // hard sync: the read phase runs fast and wraps early
      kWarpBendPlus,  // asymmetric phase bend, forward
      kWarpBendMinus, // asymmetric phase bend, backward
      kWarpAsymPlus,  // power-law phase skew, forward
      kWarpAsymMinus, // power-law phase skew, backward
      kWarpFlip,      // crossfade toward the inverted sample
      kWarpMirror,    // fold the cycle back on itself at its midpoint
      kWarpQuantize,  // step the phase, so the cycle reads as a staircase
      kWarpRectify,   // crossfade toward the rectified sample
      kWarpOddOnly,   // keep the odd harmonics
      kWarpEvenOnly,  // keep the even harmonics
      kNumWarpModes
   };

   // The four cross-modulated modes name their source, and the source is
   // always the *other* engine - so engine A's list reads "fm (b)" and engine
   // B's reads "fm (a)". One shared list labelled "(b)" was wrong on engine B
   // in the only way that matters: it told you the modulator was itself.
   inline const char* const* WarpNames(int engine)
   {
      static const char* const kNamesA[kNumWarpModes] = {
         "off",     "fm (b)",   "am (b)",   "rm (b)",   "pd (b)",   "hard sync",
         "bend +",  "bend -",   "asym +",   "asym -",   "flip",     "mirror",
         "quantize","rectify",  "odd only", "even only"
      };
      static const char* const kNamesB[kNumWarpModes] = {
         "off",     "fm (a)",   "am (a)",   "rm (a)",   "pd (a)",   "hard sync",
         "bend +",  "bend -",   "asym +",   "asym -",   "flip",     "mirror",
         "quantize","rectify",  "odd only", "even only"
      };
      return engine == 1 ? kNamesB : kNamesA;
   }

   inline const char* WarpName(int mode, int engine = 0)
   {
      const char* const* names = WarpNames(engine);
      return (mode >= 0 && mode < kNumWarpModes) ? names[mode] : names[0];
   }

   // True for the four modes that read the other engine (and therefore fall
   // back to the internal operator when it is off). These are also the only
   // modes where `warpRatio` means anything besides sync's own ratio, which is
   // why the UI greys the ratio slider outside this set plus hard sync.
   inline bool WarpIsCrossModulated(int mode)
   {
      return mode == kWarpFM || mode == kWarpAM || mode == kWarpRM || mode == kWarpPD;
   }

   inline bool WarpUsesRatio(int mode)
   {
      return WarpIsCrossModulated(mode) || mode == kWarpSync;
   }

   // ---------------------------------------------------------------- filter
   // One filter per engine, so the two halves of a patch can be shaped
   // independently - which is the whole point of having two engines rather
   // than one engine and a filter node downstream of the mix.
   //
   // Slope is spelled in the type rather than as a separate "poles" control
   // because that is how it is labelled on hardware and in every plugin's
   // filter menu ("LP 24" is one choice, not two), and because not every
   // shape has every slope: a 36 dB bandpass is six poles of skirt around a
   // band that was already narrow, which is a resonant sine with extra steps.
   //
   // Every slope is built from cascaded 12 dB TPT state-variable stages
   // (DspMath::TptSvf) at the same cutoff and Q. Cascading identical stages
   // does pull the -3 dB point down relative to a true Butterworth alignment
   // of the same order; that is the standard analogue-style ladder behaviour
   // and it is what makes a 24 dB setting sound darker than a 12 dB one at the
   // same knob position, rather than merely steeper.
   enum FilterShape
   {
      kShapeLow = 0,
      kShapeHigh,
      kShapeBand,
      kShapeNotch
   };

   enum FilterType
   {
      kFilterOff = 0,
      kFilterLP12, kFilterLP24, kFilterLP36,
      kFilterHP12, kFilterHP24, kFilterHP36,
      kFilterBP12, kFilterBP24,
      kFilterNotch12, kFilterNotch24,
      kNumFilterTypes
   };

   inline const char* const* FilterNames()
   {
      static const char* const kNames[kNumFilterTypes] = {
         "off",
         "lp 12", "lp 24", "lp 36",
         "hp 12", "hp 24", "hp 36",
         "bp 12", "bp 24",
         "notch 12", "notch 24"
      };
      return kNames;
   }

   inline const char* FilterName(int type)
   {
      return (type >= 0 && type < kNumFilterTypes) ? FilterNames()[type] : FilterNames()[0];
   }

   // Cascaded 12 dB/octave stages: 1, 2 or 3. Zero for "off", which is what
   // the render path tests rather than comparing against kFilterOff, so an
   // out-of-range saved index degrades to a bypass instead of an unfiltered
   // read of a stage that isn't there.
   inline int FilterStages(int type)
   {
      switch (type)
      {
         case kFilterLP12: case kFilterHP12: case kFilterBP12: case kFilterNotch12: return 1;
         case kFilterLP24: case kFilterHP24: case kFilterBP24: case kFilterNotch24: return 2;
         case kFilterLP36: case kFilterHP36: return 3;
         default: return 0;
      }
   }

   inline int FilterShapeOf(int type)
   {
      switch (type)
      {
         case kFilterHP12: case kFilterHP24: case kFilterHP36: return kShapeHigh;
         case kFilterBP12: case kFilterBP24: return kShapeBand;
         case kFilterNotch12: case kFilterNotch24: return kShapeNotch;
         default: return kShapeLow;
      }
   }

   // Maximum cascade depth any type asks for - the per-voice, per-engine,
   // per-channel filter state is sized from this.
   constexpr int kMaxFilterStages = 3;

   // ------------------------------------------------------------- UI tables
   // std::vector<std::string> because that is what AudioKnobRow::Dropdown
   // takes. Built once on first use; both are read-only afterwards.
   inline const std::vector<std::string>& WarpModeList(int engine)
   {
      static std::vector<std::string> lists[2];
      std::vector<std::string>& list = lists[engine == 1 ? 1 : 0];
      if (list.empty())
         for (int i = 0; i < kNumWarpModes; i++)
            list.push_back(WarpNames(engine)[i]);
      return list;
   }

   inline const std::vector<std::string>& FilterTypeList()
   {
      static std::vector<std::string> list;
      if (list.empty())
         for (int i = 0; i < kNumFilterTypes; i++)
            list.push_back(FilterNames()[i]);
      return list;
   }

   // For nodes whose DSP only ever implements a subset of the canonical
   // filter types: pass the enum values that subset actually uses, in the
   // node's own index order (which must stay whatever it already is - the
   // caller's saved patches store that index as an int), and get back the
   // canonical spelling for each. This is the only place a node-local filter
   // string is allowed to come from, so the four-different-spellings drift
   // that motivated this function cannot recur - a node can misname its own
   // subset only by passing the wrong enum value, which is reviewable at the
   // call site, not by hand-typing a string that silently diverges later.
   inline std::vector<std::string> FilterTypeSubset(std::initializer_list<FilterType> types)
   {
      std::vector<std::string> list;
      list.reserve(types.size());
      for (FilterType t : types)
         list.push_back(FilterName(t));
      return list;
   }

   // -------------------------------------------------------------- waveform
   // The same "same setting, different spelling per node" drift that
   // motivated the filter table above also happened to the basic oscillator
   // shapes: Wavetable capitalized them, Ring Mod and Tremolo lowercased
   // them, and Cycle Shaper reordered them and then spelled its own dropdown
   // differently from its own stat-line label. One canonical list here, in
   // canonical order and canonical (lowercase) spelling, same as
   // FilterNames() above.
   //
   // Every node keeps its own local index order - saved patches store that
   // index as an int - and gets its canonical strings via WaveformTypeSubset,
   // passing its own enum values in whatever order it already uses.
   enum WaveformType
   {
      kWaveSine = 0,
      kWaveTriangle,
      kWaveSaw,
      kWaveSquare,
      kWaveRampDown,
      kNumWaveformTypes
   };

   inline const char* const* WaveformNames()
   {
      static const char* const kNames[kNumWaveformTypes] = {
         "sine", "triangle", "saw", "square", "ramp down"
      };
      return kNames;
   }

   inline const char* WaveformName(int type)
   {
      return (type >= 0 && type < kNumWaveformTypes) ? WaveformNames()[type] : WaveformNames()[0];
   }

   // Same contract as FilterTypeSubset: pass this node's own enum values in
   // its own index order, get canonical spelling back, index order untouched.
   inline std::vector<std::string> WaveformTypeSubset(std::initializer_list<WaveformType> types)
   {
      std::vector<std::string> list;
      list.reserve(types.size());
      for (WaveformType t : types)
         list.push_back(WaveformName(t));
      return list;
   }
}
