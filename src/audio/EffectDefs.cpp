#include "EffectDefs.h"

#include <cstdio>

#include "dsp/AudioFilterKernel.h"
#include "dsp/DynamicsKernel.h"
#include "dsp/DelayKernel.h"
#include "dsp/ReverbKernel.h"
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
         def.makeKernel = []() { return std::make_unique<AudioFilterKernel>(); };
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
         // mix is AudioEffectNode's universal field, not a second param here.

         def.makeKernel = []() { return std::make_unique<DynamicsKernel>(); };
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
         // mix is AudioEffectNode's universal field (defaultMix above), not
         // a table row.

         def.makeKernel = []() { return std::make_unique<DelayKernel>(); };
         defs.push_back(std::move(def));
      }

      // ---------------------------------------------------------------- Reverb
      // Algorithmic only - the design doc's `engine` dropdown (algorithmic /
      // convolution) and its whole Tier 2 table (diffusion, mod, early/late
      // balance, tone shaping, width, freeze, and convolution's own IR-file
      // sub-panel) are cut per .claude/skills/new-audio-node/SKILL.md's
      // minimalism rule - see ReverbKernel.h's class comment. Tier 1 only:
      // size, decay, damping, predelay - 4 params + the universal mix.
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
         def.params.push_back({ "damping", 0.0f, 1.0f, 0.4f });
         def.params.push_back({ "predelay", 0.0f, 500.0f, 20.0f });
         // mix is AudioEffectNode's universal field (defaultMix above), not
         // a table row.

         def.makeKernel = []() { return std::make_unique<ReverbKernel>(); };
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
   return nullptr;
}
