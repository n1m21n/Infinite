#pragma once

#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"
#include "audio/VoiceMod.h"
#include "core/AudioCable.h"
#include "core/CurveShape.h"
#include "core/ModShapes.h"
#include "core/Transport.h"

class AudioAudioToCVNode;

// Modulators produce a control value, not an image, so they report a zero output
// texture; the editor draws a value meter for them instead of a preview. All three
// are tempo-synced: rates are in beats, so changing the global BPM retimes them.

// Synced-rate period memo for LFO/Random: the MusicTime division search
// runs when the rate or the bar length changes, not on every Value01().
struct SyncedPeriodCache
{
   float rate = -1.0f;
   int sigNum = -1; // time signature a bar division was made with; < 0 = not a bar division
   int sigDen = -1;
   double period = 1.0;

   bool Hit(float rateBeats, const Transport& tr) const
   {
      return rate == rateBeats && (sigNum < 0 || (sigNum == tr.TimeSigNumerator() && sigDen == tr.TimeSigDenominator()));
   }
};

class LFONode : public INode, public IModulator, public VoiceMod::IVoiceModSource
{
public:
   static INode* Create();
   // S&H seeds come from their own counter, so spawning an LFO never moves
   // the seed the next Random gets (patches without a saved seed rely on it).
   static float NextSeed() { static int sSpawnCounter = 0; return (float)(++sSpawnCounter) * 5.1f + 0.37f; }
   static const std::vector<std::string>& ShapeNames();
   static const std::vector<std::string>& RateModeNames();
   static const std::vector<std::string>& PhaseModeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "trig"; }

   float Value01() override;

   // Restarts the cycle on a rising edge through 0.5 (Retrigger phase mode).
   IModulator* trigger = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &trigger : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   // Beats is the free float of every patch saved before rate modes existed;
   // Synced snaps rateBeats to the nearest MusicTime division and times it
   // exactly (triplets included), the note nodes' menu; Hz ignores tempo.
   enum RateMode { kRateBeats = 0, kRateSynced = 1, kRateHz = 2 };
   // Transport: the phase is a function of song position (Bitwig "Sync"),
   // so seeks and loops land where they would on a fresh play. Free: runs
   // on regardless of stop, seek and loop. Retrigger: free, restarted by
   // the trig input.
   enum PhaseMode { kPhaseTransport = 0, kPhaseFree = 1, kPhaseRetrigger = 2 };

   int shape = 0;          // index into ShapeNames()
   float rateBeats = 4.0f; // one full cycle every N beats (Beats, Synced)
   float phase = 0.0f;     // 0..1
   float low = 0.0f;
   float high = 1.0f;
   int rateMode = kRateSynced;
   float rateHz = 1.0f;
   int phaseMode = kPhaseTransport;
   float seed = 0.0f; // Sample & Hold sequence; each spawn gets its own
   // 0 = maths of patches saved before the modulation pass (S&H ignores
   // the seed, so every S&H LFO plays the same sequence).
   int maths = 1;
   // Per voice (docs/plans/modulation/per-voice.md): cabled straight into a
   // synth's voice param, runs once per voice from note-on on the audio thread.
   bool voice = false;
   bool VoiceSpec(VoiceMod::Spec& out) override;

   void BeforeLoadParams() override { rateMode = kRateBeats; maths = 0; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("shape", shape); v.Float("rateBeats", rateBeats);
      v.Float("phase", phase); v.Float("low", low); v.Float("high", high);
      v.Int("rateMode", rateMode); v.Float("rateHz", rateHz);
      v.Int("phaseMode", phaseMode); v.Float("seed", seed); v.Int("maths", maths);
      v.Bool("voice", voice);
   }

   // Length of one cycle in beats for Beats and Synced.
   double PeriodBeats() const;

private:
   double FreeCycles();
   double mFreeCycles = 0.0;
   double mFreeLast = -1.0;
   float mTrigLast = 0.0f;
   bool mFreeOffline = false;
   mutable SyncedPeriodCache mPeriodCache;
};

// A fixed value, as a modulator.
//
// Exists because Math has no way to express "multiply this LFO by 0.5" - both
// its inputs are modulator pins, so a literal needs a node to come from. Macro
// Knob is close but is meant to be performed; this is meant to be set once and
// left, and it reads in the destination's own units rather than 0..1.
class ConstantNode : public INode, public IModulator
{
public:
   static INode* Create() { return new ConstantNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override
   {
      // Clamped, since every modulator is contractually 0..1 and a destination
      // maps that onto its own range.
      return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
   }

   float value = 0.5f;

   void VisitParams(ParamVisitor& v) override { v.Float("value", value); }
};

class RandomNode : public INode, public IModulator, public VoiceMod::IVoiceModSource
{
public:
   // Each spawned Random starts on a different seed. Two of them dropped on the
   // canvas should not march in lockstep, which is what a shared default does.
   static float NextSeed() { static int sSpawnCounter = 0; return (float)(++sSpawnCounter) * 7.3f; }

   static INode* Create()
   {
      auto* node = new RandomNode();
      node->seed = NextSeed();
      return node;
   }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   float rateBeats = 1.0f; // new target value every N beats (Beats, Synced)
   float smooth = 0.5f;    // 0 = stepped, 1 = fully interpolated
   float low = 0.0f;
   float high = 1.0f;

   // Without this every Random node in a patch runs the identical sequence:
   // the value is a hash of the transport step, and the step is global. Each
   // instance gets a different default so two freshly spawned Random nodes are
   // uncorrelated, while the same seed still reproduces the same run exactly.
   float seed = 0.0f;

   int rateMode = LFONode::kRateSynced; // LFONode::RateMode
   float rateHz = 1.0f;
   // 0 = maths of patches saved before the modulation pass: the seed is
   // salted at 1/1013 resolution, so seeds closer than that collide.
   int maths = 1;
   // Per voice (docs/plans/modulation/per-voice.md): cabled straight into a
   // synth's voice param, runs once per voice from note-on on the audio thread.
   bool voice = false;
   bool VoiceSpec(VoiceMod::Spec& out) override;

   void BeforeLoadParams() override { rateMode = LFONode::kRateBeats; maths = 0; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("rate", rateBeats); v.Float("smooth", smooth);
      v.Float("low", low); v.Float("high", high); v.Float("seed", seed);
      v.Int("rateMode", rateMode); v.Float("rateHz", rateHz); v.Int("maths", maths);
      v.Bool("voice", voice);
   }

private:
   float ValueForStep(long long step, uint64_t salt) const; // salt: SeedSalt(seed, 2), maths 1 only
   SyncedPeriodCache mPeriodCache;
};

// A step sequencer: the value walks through up to sixteen steps and repeats.
class PatternNode : public INode, public IModulator
{
public:
   static const int kSteps = 16;

   static INode* Create() { return new PatternNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   int CurrentStep() const { return mCurrentStep; }

   // How many steps are actually live this cycle: `length` when fitToBar is
   // off (manual, as before), or the transport's time signature times
   // fitDivision when it's on. Value01() and the step-grid UI both read this
   // so the sounding pattern and what's drawn always agree.
   int EffectiveLength() const;

   float steps[kSteps] = {
      0.0f, 0.15f, 0.35f, 0.6f, 1.0f, 0.6f, 0.35f, 0.15f,
      0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f
   };
   int length = 8;         // how many steps are used before looping, when fitToBar is off
   float stepBeats = 1.0f; // one step every N beats
   bool smoothSteps = false;
   float low = 0.0f;
   float high = 1.0f;
   bool bipolar = false;  // display/drag convention only - steps[] always stores 0..1
   bool fitToBar = false; // when on, the live step count tracks the transport's time signature
   int fitDivision = 1;   // steps per beat when fitToBar is on (1, 2, or 4)

   void VisitParams(ParamVisitor& v) override
   {
      static const char* kStepKeys[kSteps] = {
         "step0", "step1", "step2", "step3", "step4", "step5", "step6", "step7",
         "step8", "step9", "step10", "step11", "step12", "step13", "step14", "step15"
      };
      for (int i = 0; i < kSteps; i++)
         v.Float(kStepKeys[i], steps[i]);
      v.Int("length", length); v.Float("stepBeats", stepBeats);
      v.Bool("smoothSteps", smoothSteps);
      v.Float("low", low); v.Float("high", high);
      v.Bool("bipolar", bipolar);
      v.Bool("fitToBar", fitToBar); v.Int("fitDivision", fitDivision);
   }

private:
   int mCurrentStep = 0;
};

// Combines two other modulators with an arithmetic operation.
class MathNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MathNode(); }
   static const std::vector<std::string>& OpNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   // The editor patches other modulators in here; either may be null, in which
   // case the corresponding constant is used instead.
   IModulator* inputA = nullptr;
   IModulator* inputB = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &inputA : (slot == 1 ? &inputB : nullptr); }
   int ModulatorInputCount() const override { return 2; }

   int op = 0;
   float constantA = 0.5f; // used when nothing is patched into A
   float constantB = 0.5f;
   float gain = 1.0f;
   float offset = 0.0f;
   bool clampOutput = true;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("op", op); v.Float("constantA", constantA); v.Float("constantB", constantB);
      v.Float("gain", gain); v.Float("offset", offset); v.Bool("clampOutput", clampOutput);
   }
};

// Outputs 1 or 0 based on a comparison between two other modulators.
class CompareNode : public INode, public IModulator
{
public:
   static INode* Create() { return new CompareNode(); }
   static const std::vector<std::string>& OpNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   IModulator* inputA = nullptr;
   IModulator* inputB = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &inputA : (slot == 1 ? &inputB : nullptr); }
   int ModulatorInputCount() const override { return 2; }

   int op = 0;
   float constantA = 0.5f; // used when nothing is patched into A
   float constantB = 0.5f;
   float tolerance = 0.001f; // equality/inequality treat |A-B| <= tolerance as equal

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("op", op); v.Float("constantA", constantA); v.Float("constantB", constantB);
      v.Float("tolerance", tolerance);
   }
};

// Remaps a modulator from one range to another.
class RangeToRangeNode : public INode, public IModulator
{
public:
   static INode* Create() { return new RangeToRangeNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f; // used when nothing is patched
   float inLow = 0.0f, inHigh = 1.0f;
   float outLow = 0.0f, outHigh = 1.0f;
   bool clampOutput = true;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("inLow", inLow); v.Float("inHigh", inHigh);
      v.Float("outLow", outLow); v.Float("outHigh", outHigh); v.Bool("clampOutput", clampOutput);
   }
};

// Exponential moving average over another modulator, to damp jittery sources.
class SmoothNode : public INode, public IModulator
{
public:
   static INode* Create() { return new SmoothNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f;
   float amount = 0.85f; // 0 = pass-through, close to 1 = heavy smoothing

   // mode defaults to Per frame (0), the behaviour of every patch saved before
   // the field existed; Time and Spring are opt-in. Time and Spring read the
   // transport's seconds, so the response is the same at 30 and 144 fps.
   // With maths 1, Per frame reads `amount` as the time constant it gives at
   // 60 fps, so it too is the same at every frame rate, and all three modes
   // treat the input as a straight line between calls (exact one-pole and
   // closed-form spring) instead of a value held for the whole step.
   enum Mode { kPerFrame = 0, kTime = 1, kSpring = 2 };
   int mode = kPerFrame;
   float timeConstant = 0.25f; // seconds to cover ~63% of a step (Time mode)
   float frequency = 3.0f;     // Hz, spring stiffness (Spring mode)
   float damping = 0.5f;       // 1 = critically damped, below 1 overshoots

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("amount", amount);
      v.Int("mode", mode); v.Float("timeConstant", timeConstant);
      v.Float("frequency", frequency); v.Float("damping", damping);
      v.Int("maths", maths);
   }

   int maths = 1; // 0 = per-frame step and held input, as saved before the modulation pass
   void BeforeLoadParams() override { maths = 0; }

   // Per frame `amount` as a time constant: the tau whose one-pole step at
   // 60 fps is the old per-frame coefficient.
   static double AmountTau(float amount);

private:
   float ValueExact(float target);
   float mTauAmount = -1.0f; // AmountTau memo
   double mTau = 0.0;
   bool mInit = false;
   float mLast = 0.0f;
   float mVel = 0.0f;
   float mPrevIn = 0.0f;
   double mLastBeats = -1.0;   // beat at which mLast was last updated (Per frame)
   double mLastSeconds = -1.0; // transport second at which mLast was last updated
};

// Records whatever modulator is patched into "in" (or its own `in` knob when
// nothing is), then plays it back on a loop at an adjustable speed. Record
// samples the input against the transport's beat clock, so a take is a
// fixed-length gesture in beats: it retimes with global BPM, pauses with the
// transport, and `speed` scales the playback rate (1 = as recorded, 2 = twice
// as fast). Stopping a take starts it looping straight away; low/high map the
// take onto an output range. While recording,
// the live input passes through so you can hear what you are capturing.
class CVRecorderNode : public INode, public IModulator
{
public:
   static INode* Create() { return new CVRecorderNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f; // recorded when nothing is patched into "in"
   float speed = 1.0f;      // playback rate multiplier
   float low = 0.0f;        // output range the recorded 0..1 is mapped onto
   float high = 1.0f;
   bool playing = false;

   static constexpr int kSamplesPerBeat = 16;
   static constexpr int kMaxSamples = 1024; // 64 beats

   void VisitParams(ParamVisitor& v) override;

   // Main-thread controls (the UI buttons).
   void StartRecording();
   void StopRecording();
   void StartPlayback();
   void StopPlayback();
   void Clear();

   bool IsRecording() const { return mRecording; }
   int SampleCount() const { return (int)mData.size(); }
   const std::vector<unsigned char>& Samples() const { return mData; }
   double LengthBeats() const { return (double)mData.size() / kSamplesPerBeat; }
   // 0..1 position within the take (for the playhead), -1 when not playing.
   float PlayheadNorm() const { return mPlaying() ? mPlayNorm : -1.0f; }

private:
   bool mPlaying() const { return playing && !mRecording && !mData.empty(); }
   float Sample(double pos) const;

   std::vector<unsigned char> mData;
   bool mRecording = false;
   double mRecStart = 0.0;
   double mPhase = 0.0;  // playhead in take samples, integrated per tick
   bool mAnchorSet = false;
   double mLastBeats = -1.0;
   float mLastOut = 0.0f;
   float mPlayNorm = 0.0f;
   std::string mEncoded; // last string handed to / read from the patch
};

// Scales how much of a modulator's swing reaches its destination, without
// needing to know the destination's own range or resting value: a modulation
// binding always overrides the destination outright (see Modulation.h), so
// "depth" can't mean "add a fraction of this on top of the knob" the way a
// hardware synth's env-depth knob does - the knob's own position is gone the
// moment something is patched in. Collapsing toward 0.5 instead achieves the
// same practical effect: at depth 0 the destination sits at its own
// mid-range value (the modulator has no say), and at depth 1 the source
// passes through unchanged. Negative depth inverts the source, so one knob
// covers "how much" and "which direction" both.
class ModDepthNode : public INode, public IModulator
{
public:
   static INode* Create() { return new ModDepthNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f;
   float depth = 1.0f; // -1..1: 0 = no effect (holds at 0.5), 1 = full swing, negative = inverted

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("depth", depth);
   }
};

// An ADSR contour applied to a continuous modulation signal, rather than a
// note-triggered generator - see docs/plans/modulators/03-envelope-to-shaper.md.
// A mod cable has no note-on/note-off, so the gate is derived from the input
// crossing `threshold`: rising through it starts attack->decay->sustain,
// falling back below it starts release. Patch an LFO in and the LFO
// retriggers the envelope once per cycle, each swing shaped by the ADSR.
//
// output kOutAdsr (new nodes): the level itself, 0..1. output kOutCentred
// (patches saved before it existed, via BeforeLoadParams): the output
// collapses toward 0.5 as the envelope level falls, the same
// convention ModDepthNode uses above (its comment explains why): at
// envLevel 0 the input has no say (holds at 0.5), at envLevel 1 the input
// passes through unchanged. This also means "nothing patched" holds steady
// at whatever constantIn settles to, rather than free-running a generator.
class EnvelopeNode : public INode, public IModulator, public VoiceMod::IVoiceModSource
{
public:
   static INode* Create() { return new EnvelopeNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   // Keys unchanged from the note-triggered Envelope this replaced, so A/D/S/R
   // on an already-saved patch survives the load untouched.
   float attackMs = 10.0f;
   float decayMs = 200.0f;
   float sustainLevel = 0.6f;
   float releaseMs = 400.0f;
   float constantIn = 0.5f; // used when nothing is patched into "in"
   float threshold = 0.5f;  // rising edge above this (on the raw 0..1 input) = gate on
   // Hold at the peak after attack, and each moving stage's bend
   // (ModShapes::MsegSegment: 0 straight, positive fast first). New nodes
   // get a falling decay and release, as an analog envelope does; patches
   // saved before these existed load straight (BeforeLoadParams).
   float holdMs = 0.0f;
   float attackCurve = 0.0f;
   float decayCurve = 0.5f;
   float releaseCurve = 0.5f;
   // Per voice (docs/plans/modulation/per-voice.md): cabled straight into an
   // Analog or Wavetable knob, each voice runs its own envelope, gated by its
   // own note-on and note-off. Only the ADSR output goes per voice (centred
   // needs the shared input).
   bool voice = false;
   bool VoiceSpec(VoiceMod::Spec& out) override;
   ModShapes::Ahdsr Shape() const;

   // ADSR: the output is the envelope level, 0 at rest, the standard ADSR.
   // Centred: 0.5 + (in - 0.5) * level, the output of every patch saved
   // before this existed (rest 0.5, a closed gate dips below it).
   enum Output { kOutCentred = 0, kOutAdsr = 1 };
   int output = kOutAdsr;
   // 1 = stage edges at their exact times and the gate edge placed where
   // the input crosses the threshold between calls; 0 = edges found once
   // per call, as saved before the modulation pass.
   int maths = 1;

   void BeforeLoadParams() override { output = kOutCentred; maths = 0; decayCurve = 0.0f; releaseCurve = 0.0f; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("attackMs", attackMs); v.Float("decayMs", decayMs);
      v.Float("sustainLevel", sustainLevel); v.Float("releaseMs", releaseMs);
      v.Float("constantIn", constantIn); v.Float("threshold", threshold);
      v.Int("output", output); v.Int("maths", maths);
      v.Float("holdMs", holdMs); v.Float("attackCurve", attackCurve);
      v.Float("decayCurve", decayCurve); v.Float("releaseCurve", releaseCurve);
      v.Bool("voice", voice);
   }

private:
   float LevelExact(double seconds, float in01);
   float LevelAt(double seconds);
   float mPrevIn = 0.0f;
   double mPrevSeconds = -1.0;
   int mStage = ModShapes::kEnvIdle;
   float mLevel = 0.0f;
   float mStageStartLevel = 0.0f;
   double mStageStartSeconds = -1.0;
   bool mGateOpen = false;
};

// Quantizes a continuous modulation signal to semitone steps over a chosen
// range, turning a smooth LFO into a stepped/arpeggio-like contour or a
// Random node into notes of a scale instead of microtonal drift. Still
// speaks the same 0..1 contract as every other modulator (Modulation.h) -
// it just restricts *where within* 0..1 the value is allowed to land, so the
// span maps onto whole semitones. See docs/plans/modulators/02-cv-to-pitch.md.
//
// glideMs smooths the quantized output itself (portamento), so with glide
// > 0 the output passes through non-quantized values in transit - that is
// intentional; quantization always happens before the glide filter, never
// after.
class CVToPitchNode : public INode, public IModulator
{
public:
   static INode* Create() { return new CVToPitchNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f; // used when nothing is patched
   int rangeLow = -12;      // semitone mapped to input 0.0
   int rangeHigh = 12;      // semitone mapped to input 1.0; clamped so high > low
   int scale = 13;          // MusicTime::kChromatic = every semitone = "off", NoteFilterNode's convention
   int root = 0;            // 0 = C .. 11 = B; only meaningful when scale != kChromatic
   float glideMs = 0.0f;    // 0..2000, one-pole smoothing toward the new quantized step; 0 = hard steps

   // Main-thread readout for the body's semitone display - the quantized
   // target semitone, not the (possibly still-gliding) output value.
   int LastSemitone() const { return mLastSemitone; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Int("rangeLow", rangeLow); v.Int("rangeHigh", rangeHigh);
      v.Int("scale", scale); v.Int("root", root); v.Float("glideMs", glideMs);
      v.Int("maths", maths);
   }

   // 1 = glide follows the input as a straight line between calls, gliding
   // to each semitone from the moment the line crosses into it; 0 = the
   // quantized value is held for the whole step, as saved before the
   // modulation pass.
   int maths = 1;
   void BeforeLoadParams() override { maths = 0; }

private:
   float TargetFor(float v01, int* semitoneOut) const;
   float mPrevIn = -1.0f;
   int mLastSemitone = 0;
   float mGlided = -1.0f;      // sentinel: not yet initialized
   double mLastSeconds = -1.0; // transport seconds at which mGlided was last updated
};

// A stable junction point for a modulator cable, mirroring Null and Null 3D:
// zero-cost pass-through, useful for branching one modulator to several
// destinations or as a placeholder while rewiring. Existing as its own node
// (rather than telling people to just look at the source) matters because the
// value meter is drawn per-node - dropping one mid-chain lets you read a
// modulator's current value at that specific point in a longer pipeline.
class NullModulatorNode : public INode, public IModulator
{
public:
   static INode* Create() { return new NullModulatorNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override { return input ? input->Value01() : constantIn; }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f; // used when nothing is patched

   void VisitParams(ParamVisitor& v) override { v.Float("constantIn", constantIn); }
};

// Mirrors a modulator around a low/high pivot. Not a flat 1-v, so it does the
// right thing fed something already outside 0..1 (e.g. an unclamped Math output).
class InvertNode : public INode, public IModulator
{
public:
   static INode* Create() { return new InvertNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f;
   float low = 0.0f, high = 1.0f; // default 0..1 gives classic 1-v

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("low", low); v.Float("high", high);
   }
};

// Remaps a modulator through a user-drawn transfer curve - exponential
// response on a filter sweep, an S-curve to make an LFO snappier at the
// extremes, a stepped staircase, an inverted ramp: all things a slider can't
// express. Shares its point-list/spline evaluator (CurveShape) with
// CurvesNode's Photoshop-style image tone curve; see
// docs/plans/modulators/04-curve-shaper.md.
//
// The input is clamped to 0..1 for the curve lookup only (the curve is only
// defined there); mix interpolates against the unclamped input, so an
// out-of-range source degrades sensibly rather than snapping.
class ModCurveNode : public INode, public IModulator
{
public:
   static INode* Create() { return new ModCurveNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   CurveShape curve;
   float constantIn = 0.5f;
   float mix = 1.0f; // 0 = curve bypassed (straight passthrough), 1 = fully applied

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn);
      v.Float("mix", mix);
      std::string encoded = curve.Encode();
      v.Text("curve", encoded);
      curve.Decode(encoded);
   }
};

// A drawn shape played over time: breakpoints, each segment with its own
// bend, run once through per `rate` (beats, a synced division, or Hz).
//
// Nothing patched to trig: the shape follows the transport, so it sits at the
// same place after a seek or a loop. Loop off repeats the whole shape; loop on
// plays in from the start, then repeats the part between loop start and end.
//
// trig patched: a rising edge through 0.5 starts the shape from its
// beginning (retrigger). Loop off plays it once and holds the last value.
// Loop on repeats start..end while trig stays high and, when it drops, plays
// on from there to the end of the shape (the release). Loop start = loop end
// is a sustain point.
class MSEGNode : public INode, public IModulator, public VoiceMod::IVoiceModSource
{
public:
   static INode* Create() { return new MSEGNode(); }
   static constexpr int kMaxPoints = 32;

   using Point = ModShapes::MsegPoint; // x, y (0..1) and the bend of the segment that leaves it

   MSEGNode() { Reset(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "trig"; }

   float Value01() override;

   IModulator* trigger = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &trigger : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   // Sorted by x; the first point is pinned to x = 0 and the last to x = 1.
   std::vector<Point> points;
   int rateMode = LFONode::kRateSynced;
   float rateBeats = 4.0f; // the whole shape takes this many beats (Beats, Synced)
   float rateHz = 1.0f;    // times through per second (Hz)
   bool loop = false;
   float loopStart = 0.25f; // 0..1 along the shape
   float loopEnd = 0.75f;
   // Per voice (docs/plans/modulation/per-voice.md): cabled straight into a
   // synth's voice param, the note is its trig, once per voice on the audio thread.
   bool voice = false;
   bool VoiceSpec(VoiceMod::Spec& out) override;

   void Reset();
   int AddPoint(float x, float y); // index of the new point, or -1 at kMaxPoints
   void MovePoint(int index, float x, float y);
   void RemovePoint(int index); // the two end points stay
   // Shape value at x (0..1).
   float Evaluate(float x) const;
   // Value of one segment at t (0..1) between y0 and y1 for a bend of curve.
   static float Segment(float y0, float y1, float curve, float t);
   // Where along the shape (0..1) a run that started `elapsed` shape-lengths
   // ago is, while trig is held (or nothing is patched).
   float PositionHeld(double elapsed, bool patched) const;
   // Position (0..1) at the last Value01(), for the playhead.
   float Position() const { return mShownX; }

   // Round-tripped as one "x,y,curve;..." string.
   std::string Encode() const;
   void Decode(const std::string& s);

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("rateMode", rateMode); v.Float("rateBeats", rateBeats); v.Float("rateHz", rateHz);
      v.Bool("loop", loop); v.Float("loopStart", loopStart); v.Float("loopEnd", loopEnd);
      v.Bool("voice", voice);
      std::string encoded = Encode();
      v.Text("points", encoded);
      Decode(encoded);
   }

private:
   float Advance();
   mutable SyncedPeriodCache mPeriodCache;
   // trig patched: the run, advanced once per clock value.
   double mClockLast = -1.0;
   double mX = 1.0;        // position along the shape; starts at rest on the last value
   bool mReleased = true;  // the run has left the loop
   float mTrigLast = 0.0f;
   float mShownX = 0.0f;
};

// A value per note, by its place among the notes held when it started: cabled
// into an Analog or Wavetable knob it spreads a chord's voices across low..high
// (cutoff, pan, detune...). Always per voice (docs/plans/modulation/per-voice.md);
// into anything that has no voices it reads as the first note's value.
class VoiceSpreadNode : public INode, public IModulator, public VoiceMod::IVoiceModSource
{
public:
   static INode* Create() { return new VoiceSpreadNode(); }
   static const std::vector<std::string>& ModeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;
   bool VoiceSpec(VoiceMod::Spec& out) override;

   int mode = ModShapes::kSpreadLinear; // ModShapes::SpreadMode
   int count = 4;                       // notes before the spread repeats, 1..16
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("count", count); v.Float("low", low); v.Float("high", high);
   }
};

// MPE per-note pressure or slide (CC74) as a modulator: cabled into an Analog
// or Wavetable knob, each voice follows its own note's expression from MIDI
// Notes in MPE mode, smoothed over 5 ms so 7-bit steps do not zipper. Always
// per voice; into anything that has no voices it reads the last-moved
// channel.
class NoteExpressionNode : public INode, public IModulator, public VoiceMod::IVoiceModSource
{
public:
   static INode* Create() { return new NoteExpressionNode(); }
   static const std::vector<std::string>& SourceNames();
   static constexpr float kSmoothHz = 200.0f; // 1 / 5 ms

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;
   bool VoiceSpec(VoiceMod::Spec& out) override;

   int source = VoiceMod::kExprPressure; // VoiceMod::ExpressionSource
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("source", source); v.Float("low", low); v.Float("high", high);
   }
};

// Converts an incoming audio signal to a normalized 0..1 modulation signal
// using an envelope/amplitude follower with Peak or RMS detection and
// configurable attack and release smoothing.
class AudioToCVNode : public INode, public IModulator
{
public:
   static INode* Create() { return new AudioToCVNode(); }
   AudioToCVNode();
   ~AudioToCVNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }
   bool RequiresAudioProcessing() const override { return input.IsConnected(); }
   AudioNode* AudioNodeForNotePorts() override;

   float Value01() override;

   float gain = 1.0f;       // 0..5
   float attackMs = 10.0f;  // 0.1..500 ms
   float releaseMs = 100.0f;// 1..2000 ms
   int mode = 0;            // 0 = Peak, 1 = RMS
   float minVal = 0.0f;     // 0..1
   float maxVal = 1.0f;     // 0..1
   AudioCable input;

   float CurrentLevel() const;
   float RawEnvelope() const;

private:
   std::unique_ptr<AudioAudioToCVNode> mAudioNode;
   int mLastCookFrame = -1;
};
