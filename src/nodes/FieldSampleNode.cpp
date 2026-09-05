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
      std::fill(mTableBuffer, mTableBuffer + Field::kSampleMaxTableCells, 0.0f);
      std::fill(mTableSwapBuffer, mTableSwapBuffer + Field::kSampleMaxTableCells, 0.0f);
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

         // Table transplant (Step 24): preserve live table contents across hot reloads
         // when table name and length match; otherwise initialize to initialValue.
         std::memcpy(mTableSwapBuffer, mTableBuffer, sizeof(mTableSwapBuffer));
         std::fill(mTableBuffer, mTableBuffer + Field::kSampleMaxTableCells, 0.0f);

         for (const auto& tbl : fresh->tables)
         {
            if (tbl.bufferOffset + tbl.length <= Field::kSampleMaxTableCells)
            {
               if (tbl.transplantFromOffset >= 0 && tbl.transplantFromLength == tbl.length &&
                   tbl.transplantFromOffset + tbl.transplantFromLength <= Field::kSampleMaxTableCells)
               {
                  std::memcpy(mTableBuffer + tbl.bufferOffset,
                              mTableSwapBuffer + tbl.transplantFromOffset,
                              tbl.length * sizeof(float));
               }
               else
               {
                  std::fill(mTableBuffer + tbl.bufferOffset,
                            mTableBuffer + tbl.bufferOffset + tbl.length,
                            tbl.initialValue);
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

      // Step 25 (OPEN-D): declared `input sample audio <name>` pins start at
      // slot 1 (right after "in"), one AudioBuffer* per declared-audio-input
      // ordinal - see FieldSampleNode::AudioInputSlot()/NativeInputCount().
      const AudioBuffer* declaredBufs[Field::kSampleMaxDeclaredAudioInputs] = {};
      {
         int audioOrdinal = 0;
         for (const auto& d : mActiveProgram->declaredInputs)
         {
            if (d.typeName != "audio") continue;
            if (audioOrdinal >= Field::kSampleMaxDeclaredAudioInputs) break;
            const int slot = 1 + audioOrdinal;
            declaredBufs[audioOrdinal] = (slot < numInputs) ? inputs[slot] : nullptr;
            audioOrdinal++;
         }
      }

      float paramVals[Field::kSampleMaxParams] = {};
      float declaredInVals[Field::kSampleMaxDeclaredAudioInputs] = {};

      bool sawFault = false;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         const float inVal = (inBuf != nullptr && inBuf->numChannels > 0) ? inBuf->channels[0][i] : 0.0f;
         const float srVal = (float)mSampleRate;
         const float nVal = (float)mAbsSampleCounter;
         mAbsSampleCounter += 1.0;

         for (int d = 0; d < Field::kSampleMaxDeclaredAudioInputs; d++)
            declaredInVals[d] = (declaredBufs[d] != nullptr && declaredBufs[d]->numChannels > 0) ? declaredBufs[d]->channels[0][i] : 0.0f;

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
         rin.declaredIns = declaredInVals;
         rin.paramVals = paramVals;
         rin.stateCur = mStateCur;
         rin.stateNext = mStateNext;
         rin.delayBuf = mDelayBuffer;
         rin.delayCursors = mDelayCursors;
         rin.tableBuf = mTableBuffer;

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
         if (!sawFault)
         {
            for (const auto& tbl : mActiveProgram->tables)
            {
               const float* ptr = mTableBuffer + tbl.bufferOffset;
               for (int k = 0; k < tbl.length; k++)
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
         for (const auto& tbl : mActiveProgram->tables)
         {
            std::fill(mTableBuffer + tbl.bufferOffset, mTableBuffer + tbl.bufferOffset + tbl.length, tbl.initialValue);
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

   // Step 24: Bounded state table storage.
   float mTableBuffer[Field::kSampleMaxTableCells];
   float mTableSwapBuffer[Field::kSampleMaxTableCells];

   MeterRing mMeter;
   MeterRing mScopeRing;
   std::atomic<uint64_t> mFaultCount { 0 };

   double mSampleRate = 44100.0;
   double mAbsSampleCounter = 0.0;
};

// -------------------------------------------------------------- presets
const std::vector<FieldSampleNode::Preset>& FieldSampleNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Tube Overdrive",
        "param float drive = 3.0 [1.0, 10.0]\n"
        "param float tone = 4000.0 [400.0, 12000.0]\n"
        "param float bias = 0.20 [0.0, 0.8]\n"
        "param float mix = 1.0 [0.0, 1.0]\n"
        "state float lp = 0\n\n"
        "x = in * drive + bias\n"
        "sat = x / (1.0 + abs(x)) - bias * 0.5\n\n"
        "f = clamp((tone / sr) * 3.14159, 0.001, 0.95)\n"
        "lp = lp + f * (sat - lp)\n\n"
        "wet = lp\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      { "Bitcrusher & Decimator",
        "param float bits = 6.0 [2.0, 16.0]\n"
        "param float downsample = 8.0 [1.0, 32.0]\n"
        "param float drive = 1.5 [1.0, 6.0]\n"
        "param float mix = 0.85 [0.0, 1.0]\n"
        "state float phase = 0\n"
        "state float held = 0\n\n"
        "phase = phase + 1.0\n"
        "trigger = if(phase >= downsample, 1.0, 0.0)\n"
        "phase = if(trigger > 0.5, 0.0, phase)\n\n"
        "steps = pow(2.0, floor(bits))\n"
        "driven = in * drive\n"
        "crushed = floor(driven * steps + 0.5) / steps\n"
        "held = if(trigger > 0.5, crushed, held)\n\n"
        "wet = held\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      { "Moog 4-Pole Ladder Filter",
        "param float cutoff = 2200.0 [80.0, 14000.0]\n"
        "param float res = 0.70 [0.0, 0.98]\n"
        "param float drive = 1.50 [1.0, 4.0]\n"
        "param float mix = 1.0 [0.0, 1.0]\n"
        "state float s1 = 0\n"
        "state float s2 = 0\n"
        "state float s3 = 0\n"
        "state float s4 = 0\n\n"
        "f = clamp((cutoff / sr) * 2.2, 0.005, 0.42)\n"
        "feedback = (s4 * res * 3.6) / (1.0 + abs(s4) * 0.8)\n"
        "u = (in * drive) - feedback\n"
        "sat = u / (1.0 + abs(u) * 0.3)\n\n"
        "s1 = s1 + f * (sat - s1)\n"
        "s2 = s2 + f * (s1 - s2)\n"
        "s3 = s3 + f * (s2 - s3)\n"
        "s4 = s4 + f * (s3 - s4)\n\n"
        "wet = s4\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      { "Dynamic Auto-Wah",
        "param float sens = 2.2 [0.5, 5.0]\n"
        "param float baseFreq = 350.0 [100.0, 1500.0]\n"
        "param float sweep = 2800.0 [200.0, 6000.0]\n"
        "param float q = 0.75 [0.1, 0.95]\n"
        "param float mix = 0.90 [0.0, 1.0]\n"
        "state float env = 0\n"
        "state float bp = 0\n"
        "state float lp = 0\n\n"
        "sigAbs = abs(in) * sens\n"
        "env = if(sigAbs > env, env * 0.99 + sigAbs * 0.01, env * 0.9995)\n\n"
        "targetFreq = clamp(baseFreq + env * sweep, 80.0, sr * 0.35)\n"
        "f = clamp((targetFreq / sr) * 2.5, 0.005, 0.42)\n"
        "r = 1.0 - q * 0.90\n\n"
        "hp = in - lp - r * bp\n"
        "satHp = hp / (1.0 + abs(hp) * 0.1)\n"
        "bp = bp + f * satHp\n"
        "lp = lp + f * bp\n\n"
        "wet = bp * 2.0\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      { "Ring Modulator",
        "param float carrierFreq = 280.0 [20.0, 2500.0]\n"
        "param float drive = 1.50 [1.0, 4.0]\n"
        "param float mix = 0.75 [0.0, 1.0]\n"
        "state float phase = 0\n\n"
        "phase = (phase + carrierFreq / sr) % 1.0\n"
        "carrier = sin(6.283185 * phase)\n\n"
        "modSig = (in * drive) * carrier\n"
        "wet = modSig / (1.0 + abs(modSig) * 0.2)\n\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      { "Soft Saturation",
        "param float drive = 2.5 [1.0, 8.0]\n"
        "param float mix = 1.0 [0.0, 1.0]\n"
        "x = in * drive\n"
        "sat = x / (1.0 + abs(x))\n"
        "out = in * (1.0 - mix) + sat * mix\n" },
      { "Analog Tape Saturator",
        "param float drive = 2.5 [1.0, 8.0]\n"
        "param float warmth = 0.5 [0.0, 1.0]\n"
        "param float tone = 3500.0 [500.0, 10000.0]\n"
        "param float mix = 1.0 [0.0, 1.0]\n"
        "state float lp = 0\n"
        "state float hp = 0\n\n"
        "fHp = clamp((80.0 / sr) * 3.14159, 0.001, 0.1)\n"
        "hp = hp + fHp * (in - hp)\n"
        "cleanIn = in - hp\n\n"
        "x = cleanIn * drive\n"
        "sat = (x + x * x * warmth * 0.2) / (1.0 + abs(x))\n\n"
        "fTone = clamp((tone / sr) * 2.5, 0.005, 0.45)\n"
        "lp = lp + fTone * (sat - lp)\n\n"
        "wet = lp\n"
        "out = in * (1.0 - mix) + wet * mix\n" },
      { "Chamberlin Multi-Tap SVF",
        "param float cutoff = 1200.0 [40.0, 8000.0]\n"
        "param float q = 0.707 [0.05, 1.0]\n"
        "param float mixLp = 1.0 [0.0, 1.0]\n"
        "param float mixBp = 0.0 [0.0, 1.0]\n"
        "param float mixHp = 0.0 [0.0, 1.0]\n\n"
        "state float lp = 0\n"
        "state float bp = 0\n\n"
        "f = clamp(2.0 * sin(3.141592 * clamp(cutoff, 20.0, sr * 0.35) / sr), 0.005, 0.42)\n"
        "hp = in - lp - q * bp\n"
        "satHp = hp / (1.0 + abs(hp) * 0.1)\n"
        "bp = bp + f * satHp\n"
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
        "env = (env * clamp(damp, 0.90, 0.9999) + abs(in) * 0.2) / (1.0 + abs(env) * 0.05)\n"
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
   // Seed from the first factory preset rather than the bare passthrough
   // stub `code`'s member initializer defaults to: that stub declares no
   // `param float` lines, so a freshly spawned node compiled zero params
   // into GetParamTable() while presetIndex (also defaulted to 0) still
   // pointed the dropdown label at Presets()[0]'s name - the node looked
   // like it had that preset loaded but showed none of its sliders until
   // the user re-picked a preset through LoadPreset(), which does this
   // same code/Apply() pairing.
   const auto& presets = Presets();
   if (!presets.empty())
      code = presets[0].code;
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
      bool inOk = outOk && Field::ReconcileFieldPins(mInputPins, declIn, mNodeIndex, NativeInputCount(), pinNotice, pinRefusalMsg);
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
