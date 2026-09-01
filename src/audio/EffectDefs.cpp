#include "EffectDefs.h"

#include <cstdio>

#include "dsp/AudioFilterKernel.h"
#include "dsp/EqKernel.h"
#include "dsp/DynamicsKernel.h"
#include "dsp/LimiterKernel.h"
#include "dsp/DelayKernel.h"
#include "dsp/ReverbKernel.h"
#include "dsp/DriveKernel.h"
#include "dsp/StereoKernel.h"
#include "dsp/PitchShiftKernel.h"
#include "dsp/ChorusKernel.h"
#include "dsp/FlangerKernel.h"
#include "dsp/PhaserKernel.h"
#include "dsp/BitcrushKernel.h"
#include "dsp/TransientShaperKernel.h"
#include "dsp/StutterKernel.h"
#include "dsp/RingModKernel.h"
#include "dsp/FrequencyShifterKernel.h"
#include "dsp/TremoloKernel.h"
#include "dsp/FormantFilterKernel.h"
#include "dsp/WavetableShaperKernel.h"
#include "dsp/ResonatorBankKernel.h"
#include "dsp/CycleShaperKernel.h"
#include "dsp/SpecBlurKernel.h"
#include "MusicTime.h"

namespace
{
   std::vector<EffectDef> BuildEffectDefs()
   {
      std::vector<EffectDef> defs;

      // ---------------------------------------------------------- Audio Filter
      // One filter, one type/freq/Q/gain - deliberately simpler than
      // docs/plans/audio/P3c-P3a2-design.md §1.1's original 4-band design,
      // which this supersedes (see STATUS.md).
      {
         EffectDef def;
         def.name = "Audio Filter";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kFilterResponse;
         def.params.push_back({ "type", 0.0f, (float)(AudioFilterDsp::kNumFilterTypes - 1),
                                 (float)AudioFilterDsp::kLP24 });
         def.params.push_back({ "freq", 20.0f, 20000.0f, 1000.0f });
         def.params.push_back({ "q", 0.1f, 18.0f, 0.707f });
         // Gain only audibly matters on a shelf/peak type - its prereq pins
         // `type` to peak so the sweep can observe it in isolation.
         def.params.push_back({ "gain", -24.0f, 24.0f, 0.0f, false,
                                 { { "type", (float)AudioFilterDsp::kPeak } } });
         def.params.push_back({ "outputGainDb", -24.0f, 12.0f, 0.0f });
         // Bipolar octaves of cutoff shift, driven by an internal free-
         // running sine LFO (not an audio-rate sidechain input any more -
         // see the removed "cutoff mod" pin below) - the classic auto-wah/
         // filter-sweep control. 0 = off, the kernel's unmodified fast path.
         def.params.push_back({ "envAmount", -1.0f, 1.0f, 0.0f });
         // sync/rateDiv/rate: appended after the original six params rather
         // than inserted among them, so an existing saved patch's indices
         // for type/freq/q/gain/outputGainDb/envAmount are untouched -
         // params are name-keyed (AudioEffectNode::VisitParams,
         // ParamIndex(name)) so this is about draw-order/ordinal stability
         // for modulation bindings, not save/load itself. Same sync/rateDiv/
         // rate triplet Chorus/Flanger/Phaser/Tremolo already declare -
         // rateDiv deliberately gets no {sync, 1.0f} prerequisite, matching
         // their same known AUDIOPARAMSWEEPTEST blind spot (see
         // audio-param-sweep-expected.txt).
         // sync and rate only reach the output through the LFO's cutoff-shift
         // path, which the kernel's fast path (ProcessBlock's `!envActive`
         // check) skips entirely when envAmount is at its own default of 0 -
         // same gated-by-sibling-default class as `gain` above, so both get
         // an explicit prerequisite pinning envAmount away from 0 for the
         // sweep to observe them in isolation.
         def.params.push_back({ "sync", 0.0f, 1.0f, 0.0f, false, { { "envAmount", 1.0f } } });
         def.params.push_back(
            { "rateDiv", 0.0f, (float)(MusicTime::kNumRateDivisions - 1), (float)MusicTime::kQuarter });
         def.params.push_back(
            { "rate", 0.02f, 5.0f, 0.5f, false, { { "sync", 0.0f }, { "envAmount", 1.0f } } });
         // The "cutoff mod" audio-rate sidechain input (modAmount + a second
         // audio pin) is removed per the redesign - envAmount now drives the
         // internal LFO above instead of an external modulator or an
         // envelope follower. An existing patch with a cable plugged into
         // that pin silently loses it on load; no compat shim, by design.
         def.makeKernel = []() { return std::make_unique<AudioFilterKernel>(); };
         defs.push_back(std::move(def));
      }

      // ------------------------------------------------------------------- EQ
      // Five fixed bands, always present, each individually enable-able -
      // docs/plans/audio/eq-node-prompt.md. Deliberately its own node, not a
      // re-expansion of Audio Filter above (which was cut down to one band on
      // purpose - see its comment) and deliberately not table-driven per band
      // (5 bands x 5 params, flat names, no nesting - EffectParamDef itself
      // has no nesting concept).
      {
         EffectDef def;
         def.name = "EQ";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kEqCurve;

         struct BandDefault { int type; float freq; float q; };
         static const BandDefault kBandDefaults[5] = {
            { EqDsp::kLowShelf, 80.0f, 0.707f },
            { EqDsp::kPeak, 250.0f, 1.0f },
            { EqDsp::kPeak, 1000.0f, 1.0f },
            { EqDsp::kPeak, 4000.0f, 1.0f },
            { EqDsp::kHighShelf, 10000.0f, 0.707f },
         };

         for (int b = 0; b < 5; b++)
         {
            auto bandParamName = [b](const char* suffix) {
               char buf[32];
               snprintf(buf, sizeof(buf), "band%d%s", b + 1, suffix);
               return std::string(buf);
            };
            const std::string bandType = bandParamName("Type");
            const std::string bandFreq = bandParamName("Freq");
            const std::string bandQ = bandParamName("Q");
            const std::string bandGain = bandParamName("Gain");
            const std::string bandOn = bandParamName("On");

            // band5's default corner (10 kHz, high shelf) is 5+ octaves from
            // the sweep rig's fixed 300 Hz probe tone (FillDriveTone) - a
            // lowpass alternate there leaves 300 Hz untouched (still in the
            // passband), so the generic candidate sequence never lands on an
            // observable type within its try budget. An explicit hp 12
            // candidate cuts everything below its corner, including a probe
            // far below it, regardless of where that corner starts.
            std::vector<float> typeCandidates;
            if (b == 4)
               typeCandidates = { (float)EqDsp::kHp12 };
            def.params.push_back({ bandType, 0.0f, (float)(EqDsp::kNumBandTypes - 1),
                                    (float)kBandDefaults[b].type, false, {}, typeCandidates });
            // freq/q are silent no-ops at the spawn default (gain 0 dB, a
            // peak/shelf at unity) - prereqs pin gain audible and the band on
            // so the sweep observes them in isolation, exactly like Audio
            // Filter's gain prereq above. An explicit 300 Hz test candidate
            // (the probe tone's own frequency) replaces the generic sequence,
            // which for band4 (default 4 kHz) happens to land on near-zero
            // freq alternates that produce a near-unity filter no probe tone
            // can distinguish from the default - see band5's type comment
            // above for the same "candidate landed somewhere unobservable"
            // shape of bug.
            def.params.push_back({ bandFreq, 20.0f, 20000.0f, kBandDefaults[b].freq, false,
                                    { { bandGain, 12.0f }, { bandOn, 1.0f } }, { 300.0f } });
            def.params.push_back({ bandQ, 0.1f, 18.0f, kBandDefaults[b].q, false,
                                    { { bandGain, 12.0f }, { bandOn, 1.0f } } });
            // gain/on's own prereqs additionally pin freq near the probe tone
            // (1000 Hz) - band5's default corner (10 kHz) is otherwise too
            // far from the fixed 300 Hz probe for any gain/on change there to
            // move the rendered signature at all (a high shelf leaves
            // everything below its corner ~flat regardless of gain).
            def.params.push_back({ bandGain, -24.0f, 24.0f, 0.0f, false,
                                    { { bandOn, 1.0f }, { bandFreq, 1000.0f } } });
            // On/off is itself a silent no-op at 0 dB gain (a shelf/peak at
            // unity gain is an identity filter either way) - same trap as
            // freq/q, so it also needs gain pinned nonzero to be observable.
            def.params.push_back({ bandOn, 0.0f, 1.0f, 1.0f, false,
                                    { { bandGain, 12.0f }, { bandFreq, 1000.0f } } });
         }

         def.params.push_back({ "outputGainDb", -24.0f, 12.0f, 0.0f });
         // UI-only: which band the Tier 1 knob row currently follows. No
         // audio-thread effect by design - the sweep reports it pass-with-a-
         // note rather than FAIL (EffectParamDef::uiOnly's whole purpose).
         def.params.push_back({ "selectedBand", 0.0f, 4.0f, 0.0f, true });

         def.makeKernel = []() { return std::make_unique<EqKernel>(); };
         defs.push_back(std::move(def));
      }

      // -------------------------------------------------------------- Dynamics
      // Cut down to KHS Audio Compressor's control surface - Threshold,
      // Ratio, Attack, Release, Makeup, a Peak/RMS switch, a Sidechain
      // on/off switch, a meter - per
      // .claude/skills/new-audio-node/SKILL.md's minimalism rule. An earlier
      // version had 4 selectable modes (compress/limit/gate/expand) and 17
      // params; see DynamicsKernel.h's class comment. This is a compressor
      // only now - no limit/gate/expand, no knee/lookahead/hold/range/
      // stereo-link/auto-release controls.
      {
         EffectDef def;
         def.name = "Dynamics";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kDynamicsTransfer;
         def.hasSidechain = true;

         def.params.push_back({ "threshold", -60.0f, 0.0f, -18.0f });
         def.params.push_back({ "ratio", 1.0f, 20.0f, 4.0f });
         def.params.push_back({ "attack", 0.05f, 200.0f, 10.0f });
         def.params.push_back({ "release", 5.0f, 2000.0f, 100.0f });
         def.params.push_back({ "makeup", -12.0f, 24.0f, 0.0f });
         def.params.push_back({ "detectorRms", 0.0f, 1.0f, 0.0f }); // 0 = peak, 1 = RMS
         // Sidechain HP/audition are gone with the rest of Tier 2 - external
         // sidechain now just feeds the detector raw, unfiltered. That
         // simplicity is exactly why `sidechainExternal` is a confirmed (by
         // hand) sweep blind spot: AUDIOPARAMSWEEPTEST's generic rig wires
         // the identical drive tone to every audio input slot a candidate
         // has, main and sidechain alike (see BuildRig's comment in
         // main.cpp), so toggling which pin the detector reads from can
         // never change the signature when both pins carry byte-identical
         // content by construction - there's no HP filter left on the
         // sidechain path to differentiate them the way there was before
         // this file's minimalism pass. `ProcessBlock`'s
         // `useSidechain ? sidechain : in` ternary is the entire
         // implementation; correct by inspection.
         def.params.push_back({ "sidechainExternal", 0.0f, 1.0f, 0.0f });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         // mix is AudioEffectNode's universal field, not a second param here.

         def.makeKernel = []() { return std::make_unique<DynamicsKernel>(); };
         defs.push_back(std::move(def));
      }

      // ------------------------------------------------------------------ Limiter
      // The Tier-1-only mode Dynamics' old `limit` selector used to cover,
      // now its own right-sized node instead of a mode buried in a
      // compressor: Threshold, Release, In gain, Out gain, plus a live
      // gain-reduction meter. Always fully wet (`mix` stays at its 1.0
      // default and is never drawn) - a limiter is never partially applied.
      {
         EffectDef def;
         def.name = "Limiter";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kLimiterMeter;
         def.defaultMix = 1.0f;

         // audio-node-sweep's fixed probe tone sits at ~-8 dBFS peak; the
         // generic small-number candidate sequence (0, 4, -4, 2) never drops
         // low enough to actually engage a limiter whose default threshold
         // is -1 dB, so threshold gets explicit below-the-probe
         // testCandidates (confirmed fix - the sweep passes with these).
         def.params.push_back(
            { "threshold", -30.0f, 0.0f, -1.0f, false, {}, { -20.0f, -30.0f, -10.0f, -15.0f } });
         // `release` is a confirmed (by hand) sweep blind spot, the same
         // documented class as Dynamics' `sidechainExternal` above: the
         // sweep's probe is two channels in phase quadrature (L=sin, R=cos),
         // so the stereo-linked per-sample level max(|L|,|R|) ripples with a
         // ~40-sample period - but this kernel's peak detector takes the max
         // over a full lookahead window (~72 samples at the sweep's 48kHz),
         // wider than that ripple period, so the windowed level lands on
         // exactly 1.0 (thus a fixed target gain) for every sample once
         // warmed up, with nothing left for the release coefficient to
         // chase. Confirmed by direct simulation of the kernel's own math
         // against the sweep's exact probe and block timing: no threshold
         // value produces an observable release difference against this
         // specific two-channel test signal. A real (non-quadrature, non-
         // perfectly-periodic) signal has genuine amplitude variation across
         // a 72-sample span, where release is very much audible.
         def.params.push_back({ "release", 5.0f, 1000.0f, 100.0f });
         def.params.push_back({ "inGain", -12.0f, 24.0f, 0.0f });
         def.params.push_back({ "outGain", -24.0f, 12.0f, 0.0f });

         def.makeKernel = []() { return std::make_unique<LimiterKernel>(); };
         defs.push_back(std::move(def));
      }

      // ----------------------------------------------------------------- Delay
      // Cut down to KHS Audio Delay's control surface - Time, Tone,
      // Feedback, Pan, Duck, a Bounce on/off switch, Mix - per
      // .claude/skills/new-audio-node/SKILL.md's minimalism rule. An earlier
      // version had 4 selectable modes and 39 params; see DelayKernel.h's
      // class comment.
      {
         EffectDef def;
         def.name = "Delay";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kDelayTaps;
         def.defaultMix = 0.3f; // 0.3 wet, not AudioEffectNode's usual fully-wet default

         // feedback/tone/pan/ducking are gated behind {sync, 0} -
         // AUDIOPARAMSWEEPTEST's rig only warms up ~70ms before probing, and
         // the musical default delay time (rateDiv=1/8 @ 120bpm = 250ms) is
         // longer than that whole window, so nothing downstream of the base
         // tap would ever be reachable in time. Forcing free-ms mode with a
         // short `timeMs` (see below) makes each of them independently
         // observable without changing the musical default anyone actually
         // hears. `sync`/`rateDiv` can't gate around their own effect this
         // way, so they stay confirmed (by hand) sweep blind spots - hand-
         // verified via DSPTEST's "delay impulse timing" fixture
         // (RunDelayFixture, main.cpp), which drives both through 3
         // division x tempo combinations with sync=1 and lands the tap at
         // the exact expected sample every time.
         static const std::vector<EffectParamPrereq> kShortPathPrereq = { { "sync", 0.0f } };

         def.params.push_back({ "bounce", 0.0f, 1.0f, 0.0f, false, kShortPathPrereq });
         def.params.push_back({ "sync", 0.0f, 1.0f, 1.0f });
         def.params.push_back(
            { "rateDiv", 0.0f, (float)(MusicTime::kNumRateDivisions - 1), (float)MusicTime::kEighth });
         // Only audible while sync is off. Deliberately far from the
         // rateDiv default's 250ms (see kShortPathPrereq above) so toggling
         // `sync` itself is observable, and short enough (3ms = 132 samples)
         // to sit under the sweep's one-more-256-sample-block probe window -
         // feedback/tone only shape what gets WRITTEN into the delay line,
         // so their effect can't reach the READ tap until at least one full
         // delay-line length has elapsed.
         def.params.push_back({ "timeMs", 1.0f, 2000.0f, 3.0f, false, { { "sync", 0.0f } } });
         def.params.push_back({ "feedback", 0.0f, 110.0f, 35.0f, false, kShortPathPrereq });
         def.params.push_back({ "tone", -1.0f, 1.0f, 0.0f, false, kShortPathPrereq });
         def.params.push_back({ "pan", -1.0f, 1.0f, 0.0f, false, kShortPathPrereq });
         def.params.push_back({ "ducking", 0.0f, 1.0f, 0.0f, false, kShortPathPrereq });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f, false, kShortPathPrereq });
         // mix is AudioEffectNode's universal field (defaultMix above), not
         // a table row.

         def.makeKernel = []() { return std::make_unique<DelayKernel>(); };
         defs.push_back(std::move(def));
      }

      // ---------------------------------------------------------------- Reverb
      // Algorithmic only - the design doc's `engine` dropdown (algorithmic /
      // convolution) and its whole Tier 2 table (diffusion, mod, early/late
      // balance, tone shaping, freeze, and convolution's own IR-file
      // sub-panel) are cut per .claude/skills/new-audio-node/SKILL.md's
      // minimalism rule - see ReverbKernel.h's class comment. Tier 1: size,
      // decay, damping, predelay, width - 5 params + the universal mix.
      {
         EffectDef def;
         def.name = "Reverb";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kReverbDecay;
         def.defaultMix = 0.25f; // §1.4's stat example is 25% wet

         def.params.push_back({ "size", 0.0f, 1.0f, 0.5f });
         // decay/damping/predelay are confirmed (by hand) AUDIOPARAMSWEEPTEST
         // blind spots, structurally, not from a missing prerequisite: all
         // three only change what the FDN's 8 lines *write*, and a line's
         // own read is always its own activeLen samples behind its write
         // (~600-1100 samples at this param's default `size`, see
         // ReverbKernel.h's class comment) - `size` passes because it moves
         // the read position directly and so is audible on the very next
         // sample, but the sweep's post-alteration measurement window (one
         // 256-sample block, `TestOneParamWithValue` in main.cpp) is shorter
         // than any line's own loop length, so a change to decay/damping/
         // predelay can never reach a read in time to move the sweep's
         // signature - the same class of blind spot as Delay's sync/rateDiv,
         // just from the read/write asymmetry inherent to a feedback delay
         // network instead of from tempo-sync math. Hand-verified via
         // DSPTEST's RunReverbFixture (main.cpp), which measures RT60,
         // predelay landing and the damping-shortens-decay effect directly
         // and passes for all three.
         def.params.push_back({ "decay", 0.1f, 20.0f, 2.0f });
         def.params.push_back({ "damping", 0.0f, 1.0f, 0.155f });
         def.params.push_back({ "predelay", 0.0f, 500.0f, 20.0f });
         // Stereo width: blends the FDN's even/odd-line L/R split back toward
         // mono. Default 1.0 reproduces the fixed 0.6 cross-mix the kernel
         // used before this param existed (see ReverbKernel.cpp), so adding
         // it doesn't change any existing patch's sound.
         def.params.push_back({ "width", 0.0f, 1.0f, 1.0f });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         // mix is AudioEffectNode's universal field (defaultMix above), not
         // a table row.

         def.makeKernel = []() { return std::make_unique<ReverbKernel>(); };
         defs.push_back(std::move(def));
      }

      // ----------------------------------------------------------------- Drive
      // One saturator (tanh + bias), not the design doc's 6-mode curve
      // dropdown - a mode selector with more than two choices is itself the
      // smell .claude/skills/new-audio-node/SKILL.md's minimalism rule calls
      // out, and tanh's own character sweeps from warm to fuzzy across the
      // drive knob's own range. See DriveKernel.h's class comment.
      {
         EffectDef def;
         def.name = "Drive";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kDriveCurve;

         def.params.push_back({ "drive", 0.0f, 40.0f, 6.0f });
         // bias only audibly matters once drive pushes the shaper into its
         // nonlinear region - near-zero drive is close enough to linear
         // that an offset barely warps the curve at all, but there's no
         // clean binary gate for "audibly matters" the way Audio Filter's
         // gain has (peak/shelf only) - left ungated, matches Reverb's
         // `width` in staying a plain always-live param.
         def.params.push_back({ "bias", -1.0f, 1.0f, 0.0f });
         def.params.push_back({ "tone", -1.0f, 1.0f, 0.0f });
         // color: 0 = pure tanh (warm), 1 = blended toward a harder arctan
         // saturator (aggressive) - see DriveDsp::RawShape. Default 0 keeps
         // every existing patch's sound identical to before this param
         // existed.
         def.params.push_back({ "color", 0.0f, 1.0f, 0.0f });
         def.params.push_back({ "output", -24.0f, 12.0f, 0.0f });
         // mix is AudioEffectNode's universal field, not a table row.

         def.makeKernel = []() { return std::make_unique<DriveKernel>(); };
         defs.push_back(std::move(def));
      }

      // ---------------------------------------------------------------- Stereo
      // Replaces stereo/panning/mono. Cut from the design doc's `mode`
      // dropdown (stereo/mono/mid-side/swap) down to one always-on M/S
      // width control - width=0 already gives mono, matches the KHS Audio
      // Stereo module's three knobs. See StereoKernel.h's class comment.
      {
         EffectDef def;
         def.name = "Stereo";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kStereoGoniometer;
         // `width` is a confirmed (by hand) AUDIOPARAMSWEEPTEST blind spot,
         // structural like Dynamics' `sidechainExternal`: the sweep's
         // generic rig (BuildRig/FillDriveTone, main.cpp) writes the
         // identical tone to both channels of every input slot, so
         // side = 0.5*(L-R) is already exactly zero before `width` ever
         // scales it - no prerequisite can fix an L==R rig. Hand-verified:
         // width=0 collapses a real stereo source to mono, width=2 widens
         // it, by inspection of the mid/side matrix in ProcessBlock.
         def.params.push_back({ "width", 0.0f, 2.0f, 1.0f });
         def.params.push_back({ "pan", -1.0f, 1.0f, 0.0f });
         def.params.push_back({ "bassMono", 0.0f, 500.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<StereoKernel>(); };
         defs.push_back(std::move(def));
      }

      // ---------------------------------------------------------- Pitch Shifter
      // The two-tap crossfaded delay-line pitch shifter (see
      // PitchShiftKernel.h). `pitch`/`grain` matches the KHS Audio Pitch
      // Shifter module's big display + Grain Size knob; `jitter`/
      // `correlate` from the reference image are cut per
      // .claude/skills/new-audio-node/SKILL.md's minimalism rule.
      {
         EffectDef def;
         def.name = "Pitch Shifter";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kPitchShiftDisplay;
         def.params.push_back({ "pitch", -24.0f, 24.0f, 0.0f });
         def.params.push_back({ "grain", 10.0f, 250.0f, 80.0f });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f, false, { { "pitch", 7.0f } } });
         def.makeKernel = []() { return std::make_unique<PitchShiftKernel>(); };
         defs.push_back(std::move(def));
      }

      // ---------------------------------------------------------------- Chorus
      // Matches the KHS Audio Chorus module's Delay/Spread/Taps/Depth/Rate/
      // Mix control set, plus Feedback (BLEASS Chorus's own control set adds
      // this too) and a sync-to-tempo mode for rate, mirroring Delay/
      // Stutter's own sync/rateDiv pair exactly (same MusicTime table, same
      // free-Hz fallback). See ChorusKernel.h's class comment.
      {
         EffectDef def;
         def.name = "Chorus";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kChorusScatter;
         def.defaultMix = 0.5f;
         def.params.push_back({ "delay", 1.0f, 30.0f, 7.0f });
         def.params.push_back({ "spread", 0.0f, 1.0f, 0.5f });
         def.params.push_back({ "taps", 2.0f, 3.0f, 2.0f });
         def.params.push_back({ "depth", 0.0f, 15.0f, 5.0f });
         def.params.push_back({ "feedback", 0.0f, 0.9f, 0.0f });
         def.params.push_back({ "sync", 0.0f, 1.0f, 0.0f });
         def.params.push_back(
            { "rateDiv", 0.0f, (float)(MusicTime::kNumRateDivisions - 1), (float)MusicTime::kQuarter });
         // `rate` (free Hz) is only read while sync is off - same
         // prerequisite-gating precedent as Delay/Stutter's own free-time
         // param, for the same reason (AUDIOPARAMSWEEPTEST's short probe
         // window). `sync`/`rateDiv` stay confirmed (by hand) blind spots,
         // same as Delay's.
         def.params.push_back({ "rate", 0.02f, 5.0f, 0.5f, false, { { "sync", 0.0f } } });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<ChorusKernel>(); };
         defs.push_back(std::move(def));
      }

      // --------------------------------------------------------------- Flanger
      // Matches the KHS Audio Flanger module's Delay/Depth/Rate/Mix knobs,
      // plus Feedback (the control that actually makes it flange rather
      // than chorus) in place of Scroll/Offset/Motion, cut per
      // .claude/skills/new-audio-node/SKILL.md's minimalism rule, and the
      // same sync-to-tempo rate mode Chorus/Delay/Stutter share. See
      // FlangerKernel.h's class comment.
      {
         EffectDef def;
         def.name = "Flanger";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kFlangerScatter;
         def.defaultMix = 0.5f;
         def.params.push_back({ "delay", 0.2f, 15.0f, 3.0f });
         def.params.push_back({ "depth", 0.0f, 10.0f, 4.0f });
         def.params.push_back({ "feedback", -0.95f, 0.95f, 0.5f });
         def.params.push_back({ "sync", 0.0f, 1.0f, 0.0f });
         def.params.push_back(
            { "rateDiv", 0.0f, (float)(MusicTime::kNumRateDivisions - 1), (float)MusicTime::kQuarter });
         def.params.push_back({ "rate", 0.02f, 5.0f, 0.2f, false, { { "sync", 0.0f } } });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<FlangerKernel>(); };
         defs.push_back(std::move(def));
      }

      // ---------------------------------------------------------------- Phaser
      // Matches the KHS Audio Phaser module's Cutoff/Rate/Depth/Order/
      // Spread/Mix control set, plus the same sync-to-tempo rate mode
      // Chorus/Flanger/Delay/Stutter share. See PhaserKernel.h's class
      // comment.
      {
         EffectDef def;
         def.name = "Phaser";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kPhaserScatter;
         def.defaultMix = 0.5f;
         def.params.push_back({ "cutoff", 100.0f, 4000.0f, 800.0f });
         def.params.push_back({ "depth", 0.0f, 1.0f, 0.7f });
         def.params.push_back({ "order", 2.0f, (float)PhaserKernel::kMaxStages, 4.0f });
         def.params.push_back({ "spread", 0.0f, 1.0f, 0.5f });
         def.params.push_back({ "sync", 0.0f, 1.0f, 0.0f });
         def.params.push_back(
            { "rateDiv", 0.0f, (float)(MusicTime::kNumRateDivisions - 1), (float)MusicTime::kQuarter });
         def.params.push_back({ "rate", 0.02f, 5.0f, 0.3f, false, { { "sync", 0.0f } } });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<PhaserKernel>(); };
         defs.push_back(std::move(def));
      }

      // -------------------------------------------------------------- Bitcrush
      // Cut from the design doc's Quantize/Bits/Dither/ADC-Q/DAC-Q panel
      // down to rate + bits + mix per
      // .claude/skills/new-audio-node/SKILL.md's minimalism rule. See
      // BitcrushKernel.h's class comment.
      {
         EffectDef def;
         def.name = "Bitcrush";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kBitcrushWave;
         def.params.push_back({ "rate", 200.0f, 44100.0f, 6000.0f });
         def.params.push_back({ "bits", 1.0f, 16.0f, 8.0f });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<BitcrushKernel>(); };
         defs.push_back(std::move(def));
      }

      // ------------------------------------------------------- Transient Shaper
      // Matches the KHS Audio Transient Shaper module's own two knobs
      // (Attack, Sustain) - `pump`/`sidechain` from the reference image are
      // cut per .claude/skills/new-audio-node/SKILL.md's minimalism rule.
      // See TransientShaperKernel.h's class comment.
      {
         EffectDef def;
         def.name = "Transient Shaper";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kTransientEnvelope;
         def.params.push_back({ "attack", -24.0f, 24.0f, 0.0f });
         def.params.push_back({ "sustain", -24.0f, 24.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<TransientShaperKernel>(); };
         defs.push_back(std::move(def));
      }

      // --------------------------------------------------------------- Stutter
      // Tempo-synced (or free-ms) beat-repeat/glitch loop - see
      // StutterKernel.h's class comment. `sync`/`rateDiv`/`timeMs` mirror
      // Delay's own tempo-sync controls exactly (same MusicTime table),
      // including the same blind-spot mitigation: `timeMs` defaults short
      // (3ms, gated behind `sync=0`) purely so AUDIOPARAMSWEEPTEST's short
      // warmup window can reach a full record+loop cycle at all - the
      // musical default anyone actually hears is `sync=1` @ quarter notes,
      // unaffected by this. `steps`/`gateMask` are additionally gated behind
      // `sync=0` for the same reason: at the musical default's ~500ms
      // period, the sweep's probe block never leaves the record phase (a
      // straight passthrough regardless of either value), so neither is
      // observable without also forcing the short free-ms path.
      // `sync`/`rateDiv` themselves stay confirmed (by hand) blind spots,
      // the same way Delay's own `sync`/`rateDiv` do - they can't be gated
      // around their own effect.
      //
      // `steps` is a discrete repeat count (one square of the UI's gate grid
      // per repeat) rather than a free-floating grain fraction - see
      // StutterKernel.h. `gateMask` is that grid's on/off state, one bit per
      // repeat up to StutterKernel::kMaxGateSteps; stored as a plain float
      // (exact for any int up to 2^24) rather than a dedicated bitmask param
      // type, since EffectDef's param table only speaks float.
      {
         EffectDef def;
         def.name = "Stutter";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kStutterGrid;
         static const std::vector<EffectParamPrereq> kStutterShortPathPrereq = { { "sync", 0.0f } };
         def.params.push_back({ "sync", 0.0f, 1.0f, 1.0f });
         def.params.push_back(
            { "rateDiv", 0.0f, (float)(MusicTime::kNumRateDivisions - 1), (float)MusicTime::kQuarter });
         def.params.push_back({ "timeMs", 1.0f, 2000.0f, 3.0f, false, kStutterShortPathPrereq });
         def.params.push_back({ "steps", 2.0f, 16.0f, 8.0f, false, kStutterShortPathPrereq });
         def.params.push_back({ "gateMask", 0.0f, 65535.0f, 65535.0f, false, kStutterShortPathPrereq });
         def.makeKernel = []() { return std::make_unique<StutterKernel>(); };
         defs.push_back(std::move(def));
      }

      // --------------------------------------------------------------- Ring Mod
      // Multiplies by a band-limited oscillator - see RingModKernel.h's
      // class comment. `waveform` is the modulator's identity, matching
      // Audio Filter's `type` dropdown carve-out, not a processing mode.
      {
         EffectDef def;
         def.name = "Ring Mod";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kRingModWave;
         def.params.push_back({ "freq", 1.0f, 5000.0f, 220.0f });
         def.params.push_back({ "waveform", 0.0f, (float)DspMath::kWaveSquare, (float)DspMath::kWaveSine });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<RingModKernel>(); };
         defs.push_back(std::move(def));
      }

      // ----------------------------------------------------- Frequency Shifter
      // Single-sideband frequency shifter: moves every partial by the same
      // number of Hz (signed shift), creating inharmonicity, with soft-clipped
      // feedback for barber-pole spirals and spread for stereo width.
      {
         EffectDef def;
         def.name = "Frequency Shifter";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kFrequencyShiftSpectrum;
         def.defaultMix = 0.5f;
         def.params.push_back({ "shift", -10000.0f, 10000.0f, 0.0f });
         def.params.push_back({ "feedback", 0.0f, 0.95f, 0.0f });
         def.params.push_back({ "spread", 0.0f, 100.0f, 0.0f });
         def.params.push_back({ "range", 0.0f, 1.0f, 0.0f, true /* uiOnly */ });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f, false, { { "shift", 200.0f } } });
         def.makeKernel = []() { return std::make_unique<FrequencyShifterKernel>(); };
         defs.push_back(std::move(def));
      }

      // -------------------------------------------------------------- Tremolo
      // A unipolar gain envelope traced by a band-limited LFO - NOT ring
      // modulation (Ring Mod above is a bipolar audio-rate carrier that
      // replaces the input's frequencies; Tremolo's sub-audio LFO only scales
      // the input's existing level). See TremoloKernel.h's class comment.
      // `sync`/`rateDiv`/`rate` mirror Chorus/Flanger/Phaser's own
      // sync-to-tempo pair exactly (same MusicTime table): `rate` is gated
      // behind `sync == 0` so the sweep can observe it in isolation; `sync`/
      // `rateDiv` themselves stay confirmed (by hand) blind spots, the same
      // way those effects' do - they can't be gated around their own effect.
      {
         EffectDef def;
         def.name = "Tremolo";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kTremoloWave;
         def.defaultMix = 1.0f;
         def.params.push_back({ "depth", 0.0f, 1.0f, 0.6f });
         def.params.push_back({ "shape", 0.0f, 3.0f, 0.0f });
         def.params.push_back({ "stereoPhase", 0.0f, 180.0f, 0.0f });
         def.params.push_back({ "sync", 0.0f, 1.0f, 0.0f });
         def.params.push_back(
            { "rateDiv", 0.0f, (float)(MusicTime::kNumRateDivisions - 1), (float)MusicTime::kEighth });
         def.params.push_back({ "rate", 0.05f, 20.0f, 5.0f, false, { { "sync", 0.0f } } });
         def.makeKernel = []() { return std::make_unique<TremoloKernel>(); };
         defs.push_back(std::move(def));
      }

      // --------------------------------------------------------------- Formant
      // Three parallel bandpass resonators tuned to a vowel's formants,
      // morphed continuously A-E-I-O-U - see FormantFilterKernel.h's class
      // comment.
      {
         EffectDef def;
         def.name = "Formant Filter";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kFormantVowel;
         def.params.push_back({ "vowel", 0.0f, 4.0f, 0.0f });
         def.params.push_back({ "q", 2.0f, 30.0f, 10.0f });
         def.makeKernel = []() { return std::make_unique<FormantFilterKernel>(); };
         defs.push_back(std::move(def));
      }

      {
         EffectDef def;
         def.name = "Wavetable Shaper";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kWavetableShaperCurve;
         def.params.push_back({ "table", 0.0f, (float)(Wavetable::NumTables() - 1), 0.0f });
         def.params.push_back({ "position", 0.0f, 1.0f, 0.0f });
         def.params.push_back({ "drive", 0.0f, 24.0f, 0.0f });
         def.params.push_back({ "bias", -1.0f, 1.0f, 0.0f });
         // At the spawn default (table 0 "Basic Shapes", position 0), frame 0
         // is a pure sine - a single harmonic, so every mip level reads back
         // bit-identical and `smooth` has nothing to remove. Not a dropped
         // mailbox push (DSPTEST's RunWavetableShaperFixture confirms smooth
         // strictly cuts HF energy once a harmonically rich frame is in
         // play) - a gated-by-another-param blind spot, the same class as
         // Audio Filter's gain/Dynamics' ratio above. position=0.5 lands on
         // table 0's triangle/saw blend, which has plenty of harmonic
         // content for smooth to act on.
         static const std::vector<EffectParamPrereq> kSmoothPrereq = { { "position", 0.5f } };
         def.params.push_back({ "smooth", 0.0f, 1.0f, 0.0f, false, kSmoothPrereq });
         def.params.push_back({ "output", -24.0f, 12.0f, 0.0f });
         // Appended after the existing params, never inserted - see
         // WavetableShaperKernel.h's class comment. At the default 0.0 both
         // channels read the same frame, so this is a no-op for every patch
         // saved before it existed.
         def.params.push_back({ "stereo", 0.0f, 1.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<WavetableShaperKernel>(); };
         defs.push_back(std::move(def));
      }

      // -------------------------------------------------------- Resonator Bank
      // A bank of up to 16 parallel bandpass resonators (DspMath::TptSvf).
      {
         EffectDef def;
         def.name = "Resonator Bank";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kResonatorBankSpectrum;
         def.defaultMix = 0.5f;   // parallel resonance, not a full replacement
         def.params.push_back({ "rootFreq",  20.0f, 2000.0f, 110.0f });
         def.params.push_back({ "structure",  0.0f,    3.0f,   0.0f });
         def.params.push_back({ "poles",      1.0f,   16.0f,   8.0f });
         def.params.push_back({ "decay",      0.05f,  10.0f,   2.5f });
         def.params.push_back({ "scatter",    0.0f,    1.0f,   0.0f });
         def.params.push_back({ "spread",     0.0f,    1.0f,   0.7f });
         def.params.push_back({ "analog",     0.0f,    1.0f,   0.0f });
         // Appended after the existing params - never inserted, or every saved
         // patch's param indices for this node would silently shift.
         def.params.push_back({ "damp",       0.0f,    1.0f,   0.0f });
         def.makeKernel = []() { return std::make_unique<ResonatorBankKernel>(); };
         defs.push_back(std::move(def));
      }

      // ---------------------------------------------------------- Cycle Shaper
      {
         EffectDef def;
         def.name = "Cycle Shaper";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kCycleShaperWave;
         def.defaultMix = 1.0f;
         def.params.push_back({ "waveform",   0.0f,   2.0f,   0.0f });
         def.params.push_back({ "threshold", -60.0f,   0.0f, -36.0f, false, {}, { 0.0f, -3.0f, -6.0f, -60.0f } });
         def.params.push_back({ "smooth",     0.0f,  32.0f,   8.0f });
         def.params.push_back({ "analog",     0.0f,   1.0f,   0.0f });
         def.makeKernel = []() { return std::make_unique<CycleShaperKernel>(); };
         defs.push_back(std::move(def));
      }

      // -------------------------------------------------------------- Spec Blur
      {
         EffectDef def;
         def.name = "Spec Blur";
         def.category = "AudioEffects";
         def.bodyWidth = 440.0f;
         def.visualizerId = EffectVisualizerId::kSpecBlurSpectrum;
         def.defaultMix = 1.0f;
         def.params.push_back({ "blurTime", 10.0f, 5000.0f, 300.0f });
         def.params.push_back({ "tilt", -1.0f, 1.0f, 0.0f, false, { { "blurTime", 1000.0f } } });
         def.params.push_back({ "diffusion", 0.0f, 1.0f, 0.25f });
         def.params.push_back({ "freeze", 0.0f, 1.0f, 0.0f });
         def.params.push_back({ "analog", 0.0f, 1.0f, 0.0f });
         def.makeKernel = []() { return std::make_unique<SpecBlurKernel>(); };
         defs.push_back(std::move(def));
      }

      return defs;
   }
}

const std::vector<EffectDef>& GetEffectDefs()
{
   static const std::vector<EffectDef> defs = BuildEffectDefs();
   return defs;
}

namespace
{
   // Sweep-only prerequisite/candidate declarations for nodes that are NOT
   // AudioEffectNode instances (Note Filter, and any future note-only node) -
   // kept out of BuildEffectDefs() because every entry there gets registered
   // as a spawnable AudioEffectNode (see main.cpp's GetEffectDefs() loop),
   // which these node types must never be.
   const std::vector<std::pair<std::string, EffectParamDef>>& ExtraParamDefs()
   {
      static const std::vector<std::pair<std::string, EffectParamDef>> extra = {
         // root only affects anything once a non-chromatic scale is chosen -
         // chromatic (Note Filter's default) contains every pitch class, so
         // SnapToScale is a no-op regardless of root.
         { "Note Filter", { "root", 0.0f, 11.0f, 0.0f, false, { { "scale", 0.0f /* MusicTime::kMajor */ } } } },
         // rangeLow's meaningful span (0..127) is wider than the sweep's
         // generic int candidates reach relative to the rig's fixed probe
         // note (69) - none of them push rangeLow above it, so the note
         // never actually gets blocked and no difference is ever observed.
         { "Note Filter", { "rangeLow", 0.0f, 127.0f, 0.0f, false, {}, { 90.0f } } },
      };
      return extra;
   }
}

const EffectParamDef* FindEffectParamDef(const std::string& nodeName, const std::string& paramName)
{
   for (const EffectDef& def : GetEffectDefs())
   {
      if (def.name != nodeName)
         continue;
      for (const EffectParamDef& p : def.params)
         if (p.name == paramName)
            return &p;
      return nullptr;
   }
   for (const auto& entry : ExtraParamDefs())
      if (entry.first == nodeName && entry.second.name == paramName)
         return &entry.second;
   return nullptr;
}
