#pragma once

#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"
#include "core/AudioCable.h"
#include "core/CurveShape.h"

class AudioAudioToCVNode;

// Modulators produce a control value, not an image, so they report a zero output
// texture; the editor draws a value meter for them instead of a preview. All three
// are tempo-synced: rates are in beats, so changing the global BPM retimes them.

class LFONode : public INode, public IModulator
{
public:
   static INode* Create() { return new LFONode(); }
   static const std::vector<std::string>& ShapeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   int shape = 0;          // index into ShapeNames()
   float rateBeats = 4.0f; // one full cycle every N beats
   float phase = 0.0f;     // 0..1
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("shape", shape); v.Float("rateBeats", rateBeats);
      v.Float("phase", phase); v.Float("low", low); v.Float("high", high);
   }
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

class RandomNode : public INode, public IModulator
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

   float rateBeats = 1.0f; // new target value every N beats
   float smooth = 0.5f;    // 0 = stepped, 1 = fully interpolated
   float low = 0.0f;
   float high = 1.0f;

   // Without this every Random node in a patch runs the identical sequence:
   // the value is a hash of the transport step, and the step is global. Each
   // instance gets a different default so two freshly spawned Random nodes are
   // uncorrelated, while the same seed still reproduces the same run exactly.
   float seed = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("rate", rateBeats); v.Float("smooth", smooth);
      v.Float("low", low); v.Float("high", high); v.Float("seed", seed);
   }

private:
   float ValueForStep(long long step) const;
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

   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f;
   float amount = 0.85f; // 0 = pass-through, close to 1 = heavy smoothing

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("amount", amount);
   }

private:
   float mLast = -1.0f;      // sentinel: not yet initialized
   double mLastBeats = -1.0; // beat at which mLast was last updated
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
// Output collapses toward 0.5 as the envelope level falls, the same
// convention ModDepthNode uses above (its comment explains why): at
// envLevel 0 the input has no say (holds at 0.5), at envLevel 1 the input
// passes through unchanged. This also means "nothing patched" holds steady
// at whatever constantIn settles to, rather than free-running a generator.
class EnvelopeNode : public INode, public IModulator
{
public:
   static INode* Create() { return new EnvelopeNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

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

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("attackMs", attackMs); v.Float("decayMs", decayMs);
      v.Float("sustainLevel", sustainLevel); v.Float("releaseMs", releaseMs);
      v.Float("constantIn", constantIn); v.Float("threshold", threshold);
   }

private:
   enum class Stage { Idle, Attack, Decay, Sustain, Release };
   Stage mStage = Stage::Idle;
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
   }

private:
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
