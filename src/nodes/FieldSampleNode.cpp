#include "FieldSampleNode.h"

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "field/BackendRegister.h"
#include "field/ReduceOps.h"
#include "field/SampleProgram.h"
#include "field/SampleRuntime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace
{
   constexpr float kOutClamp = 4.0f; // ~12dB headroom, matches the rest of the audio graph's clamp convention
}

// ------------------------------------------------------------- audio thread
class AudioFieldSampleNode : public AudioNode
{
public:
   AudioFieldSampleNode()
   {
      std::memset(mStateCur, 0, sizeof(mStateCur));
      std::memset(mStateNext, 0, sizeof(mStateNext));
      std::fill(mDelayBuffer, mDelayBuffer + Field::kSampleMaxDelayCells, 0.0f);
      std::fill(mDelaySwapBuffer, mDelaySwapBuffer + Field::kSampleMaxDelayCells, 0.0f);
      std::fill(mDelayCursors, mDelayCursors + Field::kSampleMaxDelayLines, 0);
      std::fill(mDelaySwapCursors, mDelaySwapCursors + Field::kSampleMaxDelayLines, 0);
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
   }

   // Main thread only. Hands over a freshly compiled program; retired via
   // the existing SampleSlotT compile-swap channel, adopted at the top of
   // the next ProcessBlock - never mid-block.
   void PushProgram(Field::SampleProgram* prog) { mProgramSlot.Push(prog); }
   void DrainRetired() { mProgramSlot.DrainRetired(); }

   // Main thread only, called once per frame for every declared param.
   void PushParam(int mailboxId, float value) { mMailbox.Push(mailboxId, value); }

   uint64_t FaultCount() const { return mFaultCount.load(std::memory_order_relaxed); }
   bool ReadRmsLatest(float& out) { return mMeter.ReadLatest(out); }
   MeterRing& ScopeRing() { return mScopeRing; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      // Adopt a newly compiled program, if any, at the top of the block -
      // never mid-block. Transplants per-voice state by (name,type) match,
      // resolved on the main thread at compile time (BackendRegister.cpp);
      // any cell with no match (freshly declared, or a type change) starts
      // at its declared initial value.
      if (mProgramSlot.SwapIn())
      {
         Field::SampleProgram* fresh = mProgramSlot.Active();
         float oldCur[Field::kSampleMaxStateCells];
         std::memcpy(oldCur, mStateCur, sizeof(oldCur));
         const int n = (int)fresh->state.size();
         for (int i = 0; i < n; i++)
         {
            const int from = fresh->state[i].transplantFromIndex;
            const float val = (from >= 0 && from < Field::kSampleMaxStateCells)
               ? oldCur[from] : fresh->state[i].initialValue;
            mStateCur[i] = val;
            mStateNext[i] = val;
         }
         for (int i = n; i < Field::kSampleMaxStateCells; i++)
         {
            mStateCur[i] = 0.0f;
            mStateNext[i] = 0.0f;
         }

         // Delay line transplant (Step 19): preserve live ring buffer contents and
         // cursor position across hot reloads when delay length matches.
         std::memcpy(mDelaySwapBuffer, mDelayBuffer, sizeof(mDelaySwapBuffer));
         std::memcpy(mDelaySwapCursors, mDelayCursors, sizeof(mDelaySwapCursors));
         std::fill(mDelayBuffer, mDelayBuffer + Field::kSampleMaxDelayCells, 0.0f);
         std::fill(mDelayCursors, mDelayCursors + Field::kSampleMaxDelayLines, 0);

         for (const auto& dl : fresh->delays)
         {
            if (dl.transplantFromOffset >= 0 && dl.transplantFromLength == dl.length &&
                dl.bufferOffset + dl.length <= Field::kSampleMaxDelayCells &&
                dl.transplantFromOffset + dl.transplantFromLength <= Field::kSampleMaxDelayCells)
            {
               std::memcpy(mDelayBuffer + dl.bufferOffset,
                           mDelaySwapBuffer + dl.transplantFromOffset,
                           dl.length * sizeof(float));
               if (dl.transplantFromCursor >= 0 && dl.transplantFromCursor < Field::kSampleMaxDelayLines &&
                   dl.cursorIndex >= 0 && dl.cursorIndex < Field::kSampleMaxDelayLines)
               {
                  mDelayCursors[dl.cursorIndex] = mDelaySwapCursors[dl.transplantFromCursor];
               }
            }
         }

         mActiveProgram = fresh;
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      if (mActiveProgram == nullptr || !mActiveProgram->valid)
         return;

      // "in" lives at slot 0 (FieldSampleNode.h's AudioInputSlot override) -
      // the note pin that used to occupy slot 0 is gone.
      const AudioBuffer* inBuf = (numInputs > 0) ? inputs[0] : nullptr;

      float paramVals[Field::kSampleMaxParams] = {};

      bool sawFault = false;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float inVal = (inBuf != nullptr && inBuf->numChannels > 0) ? inBuf->channels[0][i] : 0.0f;
         const float srVal = (float)mSampleRate;
         const float nVal = (float)mAbsSampleCounter;
         mAbsSampleCounter += 1.0;

         const int numParams = (int)mActiveProgram->params.size();
         for (int p = 0; p < numParams; p++)
            paramVals[p] = mMailbox.SmoothedValue(mActiveProgram->params[p].mailboxId);

         float regs[Field::kSampleMaxRegs];

         // Field Effect is a continuous audio effect, not a note-triggered
         // synth (the note pin was removed - see FieldSampleNode.h) - it runs
         // one always-on kernel instance (state bank 0) every sample, with no
         // voice allocation and no envelope gate. freq/gate stay at 0 since
         // there is no note to report.
         Field::SampleRuntimeInput rin;
         rin.in = inVal;
         rin.sr = srVal;
         rin.n = nVal;
         rin.freq = 0.0f;
         rin.gate = 0.0f;
         rin.paramVals = paramVals;
         rin.stateCur = mStateCur;
         rin.stateNext = mStateNext;
         rin.delayBuf = mDelayBuffer;
         rin.delayCursors = mDelayCursors;

         float sampleAcc = Field::RunSampleProgram(*mActiveProgram, rin, regs);
         std::memcpy(mStateCur, mStateNext, sizeof(mStateCur));

         if (std::isnan(sampleAcc) || std::isinf(sampleAcc))
         {
            sawFault = true;
            sampleAcc = 0.0f;
         }
         sampleAcc = std::clamp(sampleAcc, -kOutClamp, kOutClamp);

         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = sampleAcc;

         // Same pattern as WavetableSynthCore's mScopeRing - one sample per
         // Write() call, decimation and cadence are the UI reader's job.
         mScopeRing.Write(&sampleAcc, 1);
      }

      // Once-per-block NaN/inf sweep over the kernel's live state (not per
      // sample - that would be a real-time-hostile unbounded-looking cost on
      // every sample; a poisoned state cell is caught within one block either
      // way). A hit zeroes the block just rendered, resets state back to its
      // declared initial values, and bumps the fault counter rather than
      // silently propagating NaN forever.
      if (!sawFault)
      {
         const int n = (int)mActiveProgram->state.size();
         for (int c = 0; c < n; c++)
         {
            if (std::isnan(mStateCur[c]) || std::isinf(mStateCur[c]))
            {
               sawFault = true;
               break;
            }
         }
         if (!sawFault)
         {
            for (const auto& dl : mActiveProgram->delays)
            {
               const float* ptr = mDelayBuffer + dl.bufferOffset;
               for (int k = 0; k < dl.length; k++)
               {
                  if (std::isnan(ptr[k]) || std::isinf(ptr[k]))
                  {
                     sawFault = true;
                     break;
                  }
               }
               if (sawFault) break;
            }
         }
      }
      if (sawFault)
      {
         for (int ch = 0; ch < buffer.numChannels; ch++)
            std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);
         const int n = (int)mActiveProgram->state.size();
         for (int c = 0; c < n; c++)
         {
            mStateCur[c] = mActiveProgram->state[c].initialValue;
            mStateNext[c] = mActiveProgram->state[c].initialValue;
         }
         for (const auto& dl : mActiveProgram->delays)
         {
            std::fill(mDelayBuffer + dl.bufferOffset, mDelayBuffer + dl.bufferOffset + dl.length, 0.0f);
            mDelayCursors[dl.cursorIndex] = 0;
         }
         mFaultCount.fetch_add(1, std::memory_order_relaxed);
      }

      // Sample -> frame reduce publish, via the existing MeterRing (no new
      // cross-thread channel - see Field step-08-notes.md §4). reduce.rms's
      // argument is required to be the bare 'in' signal (BackendRegister.cpp),
      // so this reduces the raw block input directly rather than anything
      // routed through the register machine.
      if (mActiveProgram->hasReduceRms && inBuf != nullptr && inBuf->numChannels > 0 && buffer.numFrames > 0)
      {
         const double rms = Field::ReduceRmsBandLimited(inBuf->channels[0], (size_t)buffer.numFrames,
                                                          mActiveProgram->reduceLoHz, mActiveProgram->reduceHiHz,
                                                          mSampleRate);
         float v = (float)rms;
         mMeter.Write(&v, 1);
      }
   }

private:
   ParamMailbox mMailbox;
   SampleSlotT<Field::SampleProgram> mProgramSlot;
   Field::SampleProgram* mActiveProgram = nullptr;

   float mStateCur[Field::kSampleMaxStateCells];
   float mStateNext[Field::kSampleMaxStateCells];

   // Step 19: Ring-buffer delay line storage and write cursors.
   // Audio runtime state - not persisted in patches (resets on patch load).
   float mDelayBuffer[Field::kSampleMaxDelayCells];
   float mDelaySwapBuffer[Field::kSampleMaxDelayCells];
   int mDelayCursors[Field::kSampleMaxDelayLines];
   int mDelaySwapCursors[Field::kSampleMaxDelayLines];

   MeterRing mMeter;
   MeterRing mScopeRing;
   std::atomic<uint64_t> mFaultCount { 0 };

   double mSampleRate = 44100.0;
   double mAbsSampleCounter = 0.0;
};

const std::vector<FieldSampleNode::Preset>& FieldSampleNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      // Real Schroeder-style diffuser: 4 series allpass stages with actual
      // multi-sample delay lines (7/11/13/17 samples, shift-register state
      // cells - Field's sample domain has no ring-buffer primitive, only
      // scalar `state` cells, capped at Field::kSampleMaxStateCells = 64 per
      // kernel) plus a global feedback path (fbstate/fb) that recirculates
      // the diffused signal back through the whole chain, so energy actually
      // decays over multiple passes instead of a single-sample smear. The
      // old version used 1-sample "delay" registers per stage, which is a
      // pure phase-only allpass with no audible diffusion at all - this is
      // why it didn't sound like reverb. Ceiling: 64 cells is ~1.45ms at
      // 44.1kHz, so this reads as a short metallic/spring-verb character,
      // not a spacious hall - a real hall tail needs a proper delay-line
      // (ring buffer) primitive, which the compiler doesn't have yet.
      { "Reverb",
        "param float size = 0.75 [0.1, 0.95]\n"
        "param float mix = 0.45 [0.0, 1.0]\n"
        "state float fbstate = 0\n"
        "state float d1_1 = 0\n"
        "state float d1_2 = 0\n"
        "state float d1_3 = 0\n"
        "state float d1_4 = 0\n"
        "state float d1_5 = 0\n"
        "state float d1_6 = 0\n"
        "state float d1_7 = 0\n"
        "state float d2_1 = 0\n"
        "state float d2_2 = 0\n"
        "state float d2_3 = 0\n"
        "state float d2_4 = 0\n"
        "state float d2_5 = 0\n"
        "state float d2_6 = 0\n"
        "state float d2_7 = 0\n"
        "state float d2_8 = 0\n"
        "state float d2_9 = 0\n"
        "state float d2_10 = 0\n"
        "state float d2_11 = 0\n"
        "state float d3_1 = 0\n"
        "state float d3_2 = 0\n"
        "state float d3_3 = 0\n"
        "state float d3_4 = 0\n"
        "state float d3_5 = 0\n"
        "state float d3_6 = 0\n"
        "state float d3_7 = 0\n"
        "state float d3_8 = 0\n"
        "state float d3_9 = 0\n"
        "state float d3_10 = 0\n"
        "state float d3_11 = 0\n"
        "state float d3_12 = 0\n"
        "state float d3_13 = 0\n"
        "state float d4_1 = 0\n"
        "state float d4_2 = 0\n"
        "state float d4_3 = 0\n"
        "state float d4_4 = 0\n"
        "state float d4_5 = 0\n"
        "state float d4_6 = 0\n"
        "state float d4_7 = 0\n"
        "state float d4_8 = 0\n"
        "state float d4_9 = 0\n"
        "state float d4_10 = 0\n"
        "state float d4_11 = 0\n"
        "state float d4_12 = 0\n"
        "state float d4_13 = 0\n"
        "state float d4_14 = 0\n"
        "state float d4_15 = 0\n"
        "state float d4_16 = 0\n"
        "state float d4_17 = 0\n"
        "\n"
        "g = 0.35 + size * 0.25\n"
        "fb = 0.2 + size * 0.5\n"
        "x0 = in + fbstate * fb\n"
        "tap1 = d1_7\n"
        "out1 = -(x0) * g + tap1\n"
        "din1 = (x0) + out1 * g\n"
        "d1_7 = d1_6\n"
        "d1_6 = d1_5\n"
        "d1_5 = d1_4\n"
        "d1_4 = d1_3\n"
        "d1_3 = d1_2\n"
        "d1_2 = d1_1\n"
        "d1_1 = din1\n"
        "\n"
        "tap2 = d2_11\n"
        "out2 = -(out1) * g + tap2\n"
        "din2 = (out1) + out2 * g\n"
        "d2_11 = d2_10\n"
        "d2_10 = d2_9\n"
        "d2_9 = d2_8\n"
        "d2_8 = d2_7\n"
        "d2_7 = d2_6\n"
        "d2_6 = d2_5\n"
        "d2_5 = d2_4\n"
        "d2_4 = d2_3\n"
        "d2_3 = d2_2\n"
        "d2_2 = d2_1\n"
        "d2_1 = din2\n"
        "\n"
        "tap3 = d3_13\n"
        "out3 = -(out2) * g + tap3\n"
        "din3 = (out2) + out3 * g\n"
        "d3_13 = d3_12\n"
        "d3_12 = d3_11\n"
        "d3_11 = d3_10\n"
        "d3_10 = d3_9\n"
        "d3_9 = d3_8\n"
        "d3_8 = d3_7\n"
        "d3_7 = d3_6\n"
        "d3_6 = d3_5\n"
        "d3_5 = d3_4\n"
        "d3_4 = d3_3\n"
        "d3_3 = d3_2\n"
        "d3_2 = d3_1\n"
        "d3_1 = din3\n"
        "\n"
        "tap4 = d4_17\n"
        "out4 = -(out3) * g + tap4\n"
        "din4 = (out3) + out4 * g\n"
        "d4_17 = d4_16\n"
        "d4_16 = d4_15\n"
        "d4_15 = d4_14\n"
        "d4_14 = d4_13\n"
        "d4_13 = d4_12\n"
        "d4_12 = d4_11\n"
        "d4_11 = d4_10\n"
        "d4_10 = d4_9\n"
        "d4_9 = d4_8\n"
        "d4_8 = d4_7\n"
        "d4_7 = d4_6\n"
        "d4_6 = d4_5\n"
        "d4_5 = d4_4\n"
        "d4_4 = d4_3\n"
        "d4_3 = d4_2\n"
        "d4_2 = d4_1\n"
        "d4_1 = din4\n"
        "\n"
        "wet = out4\n"
        "fbstate = wet\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      // Slapback / Echo delay using the first-class delay(x, samples) ring-buffer
      // intrinsic (Step 19). 4410 samples @ 44.1kHz is a crisp 100ms echo tap
      // with low-pass damping in the feedback recirculation loop.
      { "Delay",
        "param float feedback = 0.55 [0.0, 0.95]\n"
        "param float damp = 0.25 [0.0, 0.9]\n"
        "param float mix = 0.5 [0.0, 1.0]\n"
        "state float lp = 0\n"
        "state float fb = 0\n"
        "d = delay(in + fb * feedback, 4410)\n"
        "lp = lp * damp + d * (1.0 - damp)\n"
        "fb = lp\n"
        "wet = d\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      { "Soft Saturation",
        "param float drive = 2.5 [1.0, 8.0]\n"
        "param float mix = 1.0 [0.0, 1.0]\n"
        "x = in * drive\n"
        "sat = x / (1.0 + abs(x))\n"
        "out = in * (1.0 - mix) + sat * mix\n" },
      { "NLMS Feedback Killer",
        "param float mu = 0.3 [0.001, 1.9]\n"
        "param float mode = 0.0 [0.0, 1.0]\n"
        "state float x1 = 0\n"
        "state float x2 = 0\n"
        "state float x3 = 0\n"
        "state float x4 = 0\n"
        "state float w1 = 0\n"
        "state float w2 = 0\n"
        "state float w3 = 0\n"
        "state float w4 = 0\n"
        "state float pw = 1\n\n"
        "y = w1 * x1 + w2 * x2 + w3 * x3 + w4 * x4\n"
        "e = in - y\n"
        "pw = pw + (x1 * x1 + x2 * x2 + x3 * x3 + x4 * x4 - pw) * 0.001\n"
        "g = mu * e / (0.000001 + pw)\n"
        "w1 = w1 + g * x1\n"
        "w2 = w2 + g * x2\n"
        "w3 = w3 + g * x3\n"
        "w4 = w4 + g * x4\n"
        "x4 = x3\n"
        "x3 = x2\n"
        "x2 = x1\n"
        "x1 = in\n\n"
        "out = e + (y - e) * mode\n" },
      { "Chamberlin Multi-Tap SVF",
        "param float cutoff = 1200.0 [40.0, 8000.0]\n"
        "param float q = 0.707 [0.05, 1.0]\n"
        "param float mixLp = 1.0 [0.0, 1.0]\n"
        "param float mixBp = 0.0 [0.0, 1.0]\n"
        "param float mixHp = 0.0 [0.0, 1.0]\n\n"
        "state float lp = 0\n"
        "state float bp = 0\n\n"
        "f = 2.0 * sin(3.141592 * cutoff / sr)\n"
        "hp = in - lp - q * bp\n"
        "bp = bp + f * hp\n"
        "lp = lp + f * bp\n\n"
        "out = lp * mixLp + bp * mixBp + hp * mixHp\n" },
      { "Bass Ribbon Driver",
        "param float boost = 1.5 [0.5, 4.0]\n"
        "output frame float bass = reduce.rms(in, 20.0, 160.0)\n"
        "state float env = 0\n"
        "env = env * 0.995 + abs(in) * 0.005\n"
        "out = in * boost\n" },
      { "Boundary Chime Resonator",
        "param float pitch = 880.0 [110.0, 3520.0]\n"
        "param float damp = 0.9992 [0.99, 0.9999]\n"
        "state float env = 0\n"
        "state float phase = 0\n"
        "env = env * damp + abs(in) * 0.2\n"
        "phase = (phase + pitch / sr) % 1.0\n"
        "out = sin(6.283185 * phase) * env\n" },
      { "Chladni Resonant Tone",
        "param float freq1 = 220.0 [55.0, 1760.0]\n"
        "param float ratio = 1.67 [1.0, 4.0]\n"
        "state float p1 = 0\n"
        "state float p2 = 0\n"
        "p1 = (p1 + freq1 / sr) % 1.0\n"
        "p2 = (p2 + (freq1 * ratio) / sr) % 1.0\n"
        "out = (sin(6.283185 * p1) + sin(6.283185 * p2) * 0.5) * 0.4\n" }
   };
   return kPresets;
}

const std::vector<std::string>& FieldSampleNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
   {
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   }
   return kNames;
}

void FieldSampleNode::LoadPreset(int index)
{
   const auto& presets = Presets();
   if (index >= 0 && index < (int)presets.size())
   {
      presetIndex = index;
      code = presets[index].code;
      Apply();
   }
}

// Field build step 17 (.infdev device files) - see FieldElementNode's
// identical pair for the rationale.
Field::DeviceFile FieldSampleNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "sample";
   device.code = code;
   for (const auto& p : mParamTable.Params())
   {
      if (p.isDeclared)
         device.params[p.name] = p.value;
   }
   return device;
}

void FieldSampleNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   Apply();
   for (const auto& kv : device.params)
   {
      Field::ParamEntry* p = mParamTable.Find(kv.first);
      if (p != nullptr)
         p->value = kv.second;
   }
}

// -------------------------------------------------------------- main thread
FieldSampleNode::FieldSampleNode()
   : mAudioNode(std::make_unique<AudioFieldSampleNode>())
{
   mRmsOutput.owner = this;
   Apply();
}

FieldSampleNode::~FieldSampleNode() = default;

AudioNode* FieldSampleNode::GetAudioNode() { return mAudioNode.get(); }

uint64_t FieldSampleNode::FaultCount() const { return mAudioNode->FaultCount(); }
bool FieldSampleNode::ReadRmsLatest(float& out) { return mAudioNode->ReadRmsLatest(out); }
int FieldSampleNode::ReadScope(float* out, int capacity) { return mAudioNode->ScopeRing().Read(out, capacity); }

bool FieldSampleNode::Apply()
{
   pinRefusal.clear();
   // (name,type) state transplant (BackendRegister.h / §5.9) is resolved
   // against mLastCompiled - the main thread's own retained copy of the last
   // successfully compiled program, not the live audio-thread one (reading
   // that back across threads from Apply() would not be real-time-safe).
   auto newProgram = std::make_unique<Field::SampleProgram>();
   Field::FieldError err;
   const Field::SampleProgram* prevForTransplant = mLastCompiled.valid ? &mLastCompiled : nullptr;
   if (!Field::CompileSampleProgram(code, prevForTransplant, *newProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   // Dynamic pins, Phase 2b (build step 13, §5.1): reconcile the declared
   // output/input pin tables against this compile's SampleProgram - unlike
   // Element/Pixel, the sample backend (BackendRegister.cpp) already fully
   // populates declaredOutputs/declaredInputs on the compiled program
   // itself, so no local-IR-only step is needed. Must run before any other
   // live state is mutated so a refusal here leaves Apply() a no-op.
   {
      std::vector<Field::DeclaredPin> declOut, declIn;
      for (const auto& d : newProgram->declaredOutputs)
         declOut.push_back({ d.name, d.typeName, d.domainName, true });
      for (const auto& d : newProgram->declaredInputs)
         declIn.push_back({ d.name, d.typeName, d.domainName, false });

      std::string pinNotice, pinRefusalMsg;
      bool outOk = Field::ReconcileFieldPins(mOutputPins, declOut, mNodeIndex, NativeOutputCount(), pinNotice, pinRefusalMsg);
      bool inOk = outOk && Field::ReconcileFieldPins(mInputPins, declIn, mNodeIndex, /*nativeCount=*/2, pinNotice, pinRefusalMsg);
      if (!outOk || !inOk)
      {
         mLastError = pinRefusalMsg;
         pinRefusal = pinRefusalMsg;
         return false;
      }
      if (!pinNotice.empty())
         mNotice = pinNotice;
   }

   std::vector<Field::DeclaredParam> declared;
   declared.reserve(newProgram->params.size());
   for (const auto& p : newProgram->params)
   {
      Field::DeclaredParam dp;
      dp.name = p.name;
      dp.typeName = "float";
      dp.defaultValue = p.defaultValue;
      dp.minValue = p.minValue;
      dp.maxValue = p.maxValue;
      declared.push_back(dp);
   }
   mParamTable.Reconcile(declared, mNodeIndex, mNotice);
   mCompiledParams = newProgram->params;
   mLastCompiled = *newProgram; // retained copy for the next Apply()'s transplant resolution

   mAudioNode->PushProgram(newProgram.release());
   mLastError.clear();
   return true;
}

void FieldSampleNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   mAudioNode->DrainRetired();

   // Push every declared param's current (UI-editable) value to the audio
   // thread's mailbox every frame - the mailbox is "latest value wins", so
   // this is cheap and keeps modulated/animated params live. Looked up by
   // name through ParamTable (the stable, persisted identity) and pushed at
   // the mailboxId the *currently compiled* program assigned it - the two
   // are recomputed independently (ParamTable::Reconcile can reorder/append
   // relative to declaration order; mailboxId is always dense declaration
   // order - see SampleParamSlot's comment), so never assume they match.
   for (const auto& p : mCompiledParams)
   {
      if (const Field::ParamEntry* entry = mParamTable.Find(p.name))
         mAudioNode->PushParam(p.mailboxId, entry->value);
   }
}

void FieldSampleNode::VisitParams(ParamVisitor& v)
{
   v.Text("code", code);
   v.Bool("exposeRmsOutput", exposeRmsOutput);
   mParamTable.VisitParams(v);
   // Dynamic pins, Phase 2b (build step 13, §5.1 step 8) - see
   // FieldElementNode::VisitParams's identical block for the rationale.
   std::string outPins = mOutputPins.SerializePinMap();
   std::string inPins = mInputPins.SerializePinMap();
   v.Text("__outputPins", outPins);
   v.Text("__inputPins", inPins);
   mOutputPins.DeserializePinMap(outPins);
   mInputPins.DeserializePinMap(inPins);
}
