#include "FieldSynthNode.h"

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/NoteEventQueue.h"
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
   constexpr int kMaxVoices = 16;
   constexpr float kOutClamp = 4.0f; // ~12dB headroom

   inline float MidiNoteToHz(int midiNote)
   {
      return 440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f);
   }
}

// ------------------------------------------------------------- audio thread
class AudioFieldSynthNode : public AudioNode
{
public:
   AudioFieldSynthNode() : mVoices(kMaxVoices)
   {
      std::memset(mStateCur, 0, sizeof(mStateCur));
      std::memset(mStateNext, 0, sizeof(mStateNext));
      std::memset(mGateHeld, 0, sizeof(mGateHeld));
      std::memset(mDelayBuffer, 0, sizeof(mDelayBuffer));
      std::memset(mDelaySwapBuffer, 0, sizeof(mDelaySwapBuffer));
      std::memset(mDelayCursors, 0, sizeof(mDelayCursors));
      std::memset(mDelaySwapCursors, 0, sizeof(mDelaySwapCursors));
      std::memset(mTableBuffer, 0, sizeof(mTableBuffer));
      std::memset(mTableSwapBuffer, 0, sizeof(mTableSwapBuffer));
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mVoices.SetSampleRate(sampleRate);
      // Fast attack/release on the voice envelope to prevent click artifacts
      // on note trigger/release; kernel state cells can implement bespoke ADSR
      // or filter dynamics.
      mVoices.SetADSR(2.0f, 0.0f, 1.0f, 30.0f);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void PushProgram(Field::SampleProgram* prog) { mProgramSlot.Push(prog); }
   void DrainRetired() { mProgramSlot.DrainRetired(); }
   void PushParam(int mailboxId, float value) { mMailbox.Push(mailboxId, value); }
   void SetMaxVoices(int n) { mNumVoicesInUse = std::clamp(n, 1, kMaxVoices); }

   uint64_t FaultCount() const { return mFaultCount.load(std::memory_order_relaxed); }
   bool ReadRmsLatest(float& out) { return mMeter.ReadLatest(out); }
   MeterRing& ScopeRing() { return mScopeRing; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      if (mProgramSlot.SwapIn())
      {
         Field::SampleProgram* fresh = mProgramSlot.Active();
         for (int v = 0; v < kMaxVoices; v++)
         {
            float oldCur[Field::kSampleMaxStateCells];
            std::memcpy(oldCur, mStateCur[v], sizeof(oldCur));
            const int n = (int)fresh->state.size();
            for (int i = 0; i < n; i++)
            {
               const int from = fresh->state[i].transplantFromIndex;
               const float val = (from >= 0 && from < Field::kSampleMaxStateCells)
                  ? oldCur[from] : fresh->state[i].initialValue;
               mStateCur[v][i] = val;
               mStateNext[v][i] = val;
            }
            for (int i = n; i < Field::kSampleMaxStateCells; i++)
            {
               mStateCur[v][i] = 0.0f;
               mStateNext[v][i] = 0.0f;
            }
         }

         // Delay line transplant: preserve live ring buffer contents and
         // cursor position per-voice across hot reloads when delay length matches.
         for (int v = 0; v < kMaxVoices; v++)
         {
            std::memcpy(mDelaySwapBuffer, mDelayBuffer[v], sizeof(mDelaySwapBuffer));
            std::memcpy(mDelaySwapCursors, mDelayCursors[v], sizeof(mDelaySwapCursors));
            std::fill(mDelayBuffer[v], mDelayBuffer[v] + Field::kSampleMaxDelayCells, 0.0f);
            std::fill(mDelayCursors[v], mDelayCursors[v] + Field::kSampleMaxDelayLines, 0);

            for (const auto& dl : fresh->delays)
            {
               if (dl.transplantFromOffset >= 0 && dl.transplantFromLength == dl.length &&
                   dl.bufferOffset + dl.length <= Field::kSampleMaxDelayCells &&
                   dl.transplantFromOffset + dl.transplantFromLength <= Field::kSampleMaxDelayCells)
               {
                  std::memcpy(mDelayBuffer[v] + dl.bufferOffset,
                              mDelaySwapBuffer + dl.transplantFromOffset,
                              dl.length * sizeof(float));
                  if (dl.transplantFromCursor >= 0 && dl.transplantFromCursor < Field::kSampleMaxDelayLines &&
                      dl.cursorIndex >= 0 && dl.cursorIndex < Field::kSampleMaxDelayLines)
                  {
                     mDelayCursors[v][dl.cursorIndex] = mDelaySwapCursors[dl.transplantFromCursor];
                  }
               }
            }
         }

         // Table transplant (Step 24): preserve live table contents across hot reloads
         // per voice when table name and length match; otherwise initialize to initialValue.
         for (int v = 0; v < kMaxVoices; v++)
         {
            std::memcpy(mTableSwapBuffer, mTableBuffer[v], sizeof(mTableSwapBuffer));
            std::fill(mTableBuffer[v], mTableBuffer[v] + Field::kSampleMaxTableCells, 0.0f);

            for (const auto& tbl : fresh->tables)
            {
               if (tbl.bufferOffset + tbl.length <= Field::kSampleMaxTableCells)
               {
                  if (tbl.transplantFromOffset >= 0 && tbl.transplantFromLength == tbl.length &&
                      tbl.transplantFromOffset + tbl.transplantFromLength <= Field::kSampleMaxTableCells)
                  {
                     std::memcpy(mTableBuffer[v] + tbl.bufferOffset,
                                 mTableSwapBuffer + tbl.transplantFromOffset,
                                 tbl.length * sizeof(float));
                  }
                  else
                  {
                     std::fill(mTableBuffer[v] + tbl.bufferOffset,
                               mTableBuffer[v] + tbl.bufferOffset + tbl.length,
                               tbl.initialValue);
                  }
               }
            }
         }

         mActiveProgram = fresh;
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      if (mActiveProgram == nullptr || !mActiveProgram->valid)
         return;

      // Slot 0 is notes, slot 1 is audio in (if connected)
      const AudioBuffer* inBuf = (numInputs > 1) ? inputs[1] : nullptr;

      // Step 25 (OPEN-D): declared `input sample audio <name>` pins start at
      // slot 2 (right after "notes"/"in") - see
      // FieldSynthNode::AudioInputSlot()/NativeInputCount().
      const AudioBuffer* declaredBufs[Field::kSampleMaxDeclaredAudioInputs] = {};
      {
         int audioOrdinal = 0;
         for (const auto& d : mActiveProgram->declaredInputs)
         {
            if (d.typeName != "audio") continue;
            if (audioOrdinal >= Field::kSampleMaxDeclaredAudioInputs) break;
            const int slot = 2 + audioOrdinal;
            declaredBufs[audioOrdinal] = (slot < numInputs) ? inputs[slot] : nullptr;
            audioOrdinal++;
         }
      }
      float declaredInVals[Field::kSampleMaxDeclaredAudioInputs] = {};

      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (mNoteInbox != nullptr)
         numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);

      float paramVals[Field::kSampleMaxParams] = {};
      bool sawFault = false;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         // Step 26 (OPEN-D note history): noteOn is an edge, not a held
         // level like gate - true only for the one sample where a note-on
         // is registered below, reset every sample. notePitch/noteVel hold
         // the *last* registered note-on's values (a per-node snapshot,
         // shared by every voice, mirroring in/sr/n rather than the
         // per-voice freq/gate).
         bool noteOnEdge = false;
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            if (evts[evtIdx].isNoteOn)
            {
               const int v = mVoices.NoteOn(evts[evtIdx].note, evts[evtIdx].velocity, evts[evtIdx].voiceId);
               mVoiceId[v] = evts[evtIdx].voiceId;
               mGateHeld[v] = true;
               noteOnEdge = true;
               mLastNotePitchHz = MidiNoteToHz(evts[evtIdx].note);
               mLastNoteVel = evts[evtIdx].velocity;
               const int n = (int)mActiveProgram->state.size();
               for (int c = 0; c < n; c++)
               {
                  mStateCur[v][c] = mActiveProgram->state[c].initialValue;
                  mStateNext[v][c] = mActiveProgram->state[c].initialValue;
               }
               for (const auto& dl : mActiveProgram->delays)
               {
                  std::fill(mDelayBuffer[v] + dl.bufferOffset, mDelayBuffer[v] + dl.bufferOffset + dl.length, 0.0f);
                  mDelayCursors[v][dl.cursorIndex] = 0;
               }
            }
            else
            {
               mVoices.NoteOff(evts[evtIdx].voiceId);
               for (int gv = 0; gv < kMaxVoices; gv++)
               {
                  if (mVoiceId[gv] == evts[evtIdx].voiceId)
                     mGateHeld[gv] = false;
               }
            }
            evtIdx++;
         }

         const float inVal = (inBuf != nullptr && inBuf->numChannels > 0) ? inBuf->channels[0][i] : 0.0f;
         const float srVal = (float)mSampleRate;
         const float nVal = (float)mAbsSampleCounter;
         mAbsSampleCounter++;

         for (int d = 0; d < Field::kSampleMaxDeclaredAudioInputs; d++)
            declaredInVals[d] = (declaredBufs[d] != nullptr && declaredBufs[d]->numChannels > 0) ? declaredBufs[d]->channels[0][i] : 0.0f;

         const int numParams = (int)mActiveProgram->params.size();
         for (int p = 0; p < numParams; p++)
            paramVals[p] = mMailbox.SmoothedValue(mActiveProgram->params[p].mailboxId);

         float sampleAcc = 0.0f;
         float regs[Field::kSampleMaxRegs];

         for (int v = 0; v < mNumVoicesInUse; v++)
         {
            if (!mVoices.IsVoiceActive(v))
               continue;

            Field::SampleRuntimeInput rin;
            rin.in = inVal;
            rin.sr = srVal;
            rin.n = nVal;
            rin.freq = MidiNoteToHz(mVoices.NoteAt(v));
            rin.gate = mGateHeld[v] ? 1.0f : 0.0f;
            rin.noteOn = noteOnEdge ? 1.0f : 0.0f;
            rin.notePitch = mLastNotePitchHz;
            rin.noteVel = mLastNoteVel;
            rin.declaredIns = declaredInVals;
            rin.paramVals = paramVals;
            rin.stateCur = mStateCur[v];
            rin.stateNext = mStateNext[v];
            rin.delayBuf = mDelayBuffer[v];
            rin.delayCursors = mDelayCursors[v];
            rin.tableBuf = mTableBuffer[v];

            const float kernelOut = Field::RunSampleProgram(*mActiveProgram, rin, regs);
            const float env = mVoices.EnvelopeAt(v).Process();
            const float vel = mVoices.VelocityAt(v);
            sampleAcc += kernelOut * env * vel;

            std::memcpy(mStateCur[v], mStateNext[v], sizeof(mStateCur[v]));
         }

         if (std::isnan(sampleAcc) || std::isinf(sampleAcc))
         {
            sawFault = true;
            sampleAcc = 0.0f;
         }

         sampleAcc = std::clamp(sampleAcc, -kOutClamp, kOutClamp);

         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = sampleAcc;

         mBlockRmsAcc += sampleAcc * sampleAcc;
         mScopeRing.Write(&sampleAcc, 1);
      }

      for (int v = 0; v < mNumVoicesInUse && !sawFault; v++)
      {
         const int n = (int)mActiveProgram->state.size();
         for (int c = 0; c < n; c++)
         {
            if (std::isnan(mStateCur[v][c]) || std::isinf(mStateCur[v][c]))
            {
               sawFault = true;
               break;
            }
         }
         if (!sawFault)
         {
            for (const auto& dl : mActiveProgram->delays)
            {
               const float* ptr = mDelayBuffer[v] + dl.bufferOffset;
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
               const float* ptr = mTableBuffer[v] + tbl.bufferOffset;
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
         for (int v = 0; v < kMaxVoices; v++)
         {
            for (int c = 0; c < n; c++)
            {
               mStateCur[v][c] = mActiveProgram->state[c].initialValue;
               mStateNext[v][c] = mActiveProgram->state[c].initialValue;
            }
            for (const auto& dl : mActiveProgram->delays)
            {
               std::fill(mDelayBuffer[v] + dl.bufferOffset, mDelayBuffer[v] + dl.bufferOffset + dl.length, 0.0f);
               mDelayCursors[v][dl.cursorIndex] = 0;
            }
            for (const auto& tbl : mActiveProgram->tables)
            {
               std::fill(mTableBuffer[v] + tbl.bufferOffset, mTableBuffer[v] + tbl.bufferOffset + tbl.length, tbl.initialValue);
            }
         }
         mFaultCount.fetch_add(1, std::memory_order_relaxed);
      }

      mRmsSamplesAccum += buffer.numFrames;
      if (mRmsSamplesAccum >= (int)(mSampleRate * 0.05))
      {
         const float meanSquare = mBlockRmsAcc / (float)mRmsSamplesAccum;
         float v = std::sqrt(std::max(0.0f, meanSquare));
         mMeter.Write(&v, 1);
         mBlockRmsAcc = 0.0f;
         mRmsSamplesAccum = 0;
      }
   }

private:
   VoiceAllocator mVoices;
   int mVoiceId[kMaxVoices] = {};
   bool mGateHeld[kMaxVoices] = {};
   int mNumVoicesInUse = 8;

   ParamMailbox mMailbox;
   SampleSlotT<Field::SampleProgram> mProgramSlot;
   Field::SampleProgram* mActiveProgram = nullptr;

   float mStateCur[kMaxVoices][Field::kSampleMaxStateCells];
   float mStateNext[kMaxVoices][Field::kSampleMaxStateCells];

   float mDelayBuffer[kMaxVoices][Field::kSampleMaxDelayCells];
   float mDelaySwapBuffer[Field::kSampleMaxDelayCells];
   int mDelayCursors[kMaxVoices][Field::kSampleMaxDelayLines];
   int mDelaySwapCursors[Field::kSampleMaxDelayLines];

   // Step 24: Bounded state table storage per voice.
   float mTableBuffer[kMaxVoices][Field::kSampleMaxTableCells];
   float mTableSwapBuffer[Field::kSampleMaxTableCells];

   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   // Step 26 (OPEN-D note history): per-node snapshot of the most recent
   // note-on, shared across voices. Audio-thread-only, never read/written
   // from the main thread.
   float mLastNotePitchHz = 0.0f;
   float mLastNoteVel = 0.0f;

   double mSampleRate = 44100.0;
   uint64_t mAbsSampleCounter = 0;
   std::atomic<uint64_t> mFaultCount { 0 };

   MeterRing mMeter;
   float mBlockRmsAcc = 0.0f;
   int mRmsSamplesAccum = 0;

   MeterRing mScopeRing;
};

// -------------------------------------------------------------- presets
const std::vector<FieldSynthNode::Preset>& FieldSynthNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Dual Oscillator Pad",
        "param float detune = 1.008 [1.0, 1.03]\n"
        "param float cutoff = 2800.0 [200.0, 10000.0]\n"
        "param float res = 0.35 [0.0, 0.92]\n"
        "param float sub = 0.30 [0.0, 1.0]\n"
        "param float warmth = 0.40 [0.0, 1.0]\n"
        "param float attack = 0.05 [0.005, 0.5]\n"
        "state float p1 = 0\n"
        "state float p2 = 0\n"
        "state float pSub = 0\n"
        "state float env = 0\n"
        "state float lp = 0\n\n"
        "p1 = (p1 + freq / sr) % 1.0\n"
        "p2 = (p2 + (freq * detune) / sr) % 1.0\n"
        "pSub = (pSub + (freq * 0.5) / sr) % 1.0\n\n"
        "target = if(gate > 0.5, 1.0, 0.0)\n"
        "rate = if(target > env, clamp(1.0 / (sr * max(attack, 0.005)), 0.0001, 0.1), 0.0005)\n"
        "env = env + (target - env) * rate\n\n"
        "tone = sin(6.283185 * p1) * 0.45 + sin(6.283185 * p2) * 0.45 + sin(6.283185 * pSub) * sub\n"
        "drive = 1.0 + warmth * 1.5\n"
        "sat = (tone * drive) / (1.0 + abs(tone * drive * 0.5))\n\n"
        "f = clamp((cutoff / sr) * 3.14159, 0.001, 0.95)\n"
        "lp = lp + f * (sat - lp)\n"
        "filt = lp + (sat - lp) * (1.0 - res)\n"
        "out = filt * env\n" },
      { "Resonant String Pluck",
        "param float damp = 0.35 [0.05, 0.95]\n"
        "param float brightness = 2.5 [1.0, 8.0]\n"
        "param float decay = 1.8 [0.2, 5.0]\n"
        "param float body = 0.40 [0.0, 1.0]\n"
        "param float strike = 0.90 [0.1, 2.0]\n"
        "state float pInit = 0\n"
        "state float s1 = 0\n"
        "state float s2 = 0\n"
        "state float bodyPhase = 0\n\n"
        "exciter = if(gate > 0.5 && pInit < 1.0, strike, 0.0)\n"
        "pInit = if(gate > 0.5, pInit + 1.0, 0.0)\n\n"
        "w = 6.2831853 * clamp(freq, 20.0, sr * 0.45) / sr\n"
        "dRate = clamp(1.0 / (sr * max(decay, 0.1)), 0.00002, 0.01)\n"
        "dCoeff = clamp(1.0 - dRate - damp * 0.004, 0.95, 0.9999)\n\n"
        "cosW = cos(w)\n"
        "sinW = sin(w)\n"
        "next1 = (s1 * cosW - s2 * sinW + exciter) * dCoeff\n"
        "next2 = (s1 * sinW + s2 * cosW) * dCoeff\n\n"
        "s1 = next1 / (1.0 + abs(next1) * 0.02)\n"
        "s2 = next2 / (1.0 + abs(next2) * 0.02)\n\n"
        "stringSig = s1 * brightness\n"
        "bodyPhase = (bodyPhase + 110.0 / sr) % 1.0\n"
        "bodyRes = sin(6.283185 * bodyPhase) * abs(s1) * body\n\n"
        "out = (stringSig + bodyRes) * gate\n" },
      { "Subtractive Saw Lead",
        "param float cutoff = 2200.0 [100.0, 12000.0]\n"
        "param float res = 0.65 [0.0, 0.95]\n"
        "param float sub = 0.35 [0.0, 1.0]\n"
        "param float drive = 1.80 [1.0, 5.0]\n"
        "param float envAmount = 0.50 [0.0, 1.0]\n"
        "state float phase = 0\n"
        "state float subPhase = 0\n"
        "state float lp1 = 0\n"
        "state float lp2 = 0\n"
        "state float env = 0\n\n"
        "phase = (phase + freq / sr) % 1.0\n"
        "subPhase = (subPhase + (freq * 0.5) / sr) % 1.0\n"
        "rawSaw = phase * 2.0 - 1.0\n"
        "rawSub = subPhase * 2.0 - 1.0\n\n"
        "target = if(gate > 0.5, 1.0, 0.0)\n"
        "env = env * 0.999 + target * 0.001\n\n"
        "mixSig = (rawSaw + rawSub * sub) * drive\n"
        "sat = mixSig / (1.0 + abs(mixSig) * 0.4)\n\n"
        "effectiveCutoff = clamp(cutoff + env * envAmount * 6000.0, 60.0, sr * 0.40)\n"
        "f = clamp((effectiveCutoff / sr) * 2.5, 0.005, 0.42)\n"
        "fb = (lp2 * res * 3.5) / (1.0 + abs(lp2) * 1.5)\n"
        "u = sat - fb\n"
        "satU = u / (1.0 + abs(u) * 0.2)\n"
        "lp1 = lp1 + f * (satU - lp1)\n"
        "lp2 = lp2 + f * (lp1 - lp2)\n"
        "out = lp2 * env\n" },
      { "SuperSaw Stack",
        "param float detune = 0.012 [0.002, 0.03]\n"
        "param float spread = 0.70 [0.1, 1.0]\n"
        "param float cutoff = 3500.0 [200.0, 12000.0]\n"
        "param float sub = 0.40 [0.0, 1.0]\n"
        "param float mix = 0.85 [0.0, 1.0]\n"
        "state float p0 = 0\n"
        "state float p1 = 0\n"
        "state float p2 = 0\n"
        "state float pSub = 0\n"
        "state float lp = 0\n\n"
        "d1 = 1.0 + detune\n"
        "d2 = 1.0 - detune * 0.9\n"
        "p0 = (p0 + freq / sr) % 1.0\n"
        "p1 = (p1 + (freq * d1) / sr) % 1.0\n"
        "p2 = (p2 + (freq * d2) / sr) % 1.0\n"
        "pSub = (pSub + (freq * 0.5) / sr) % 1.0\n\n"
        "s0 = p0 * 2.0 - 1.0\n"
        "s1 = p1 * 2.0 - 1.0\n"
        "s2 = p2 * 2.0 - 1.0\n"
        "sSub = pSub * 2.0 - 1.0\n\n"
        "stacked = s0 * 0.4 + (s1 + s2) * 0.3 * spread + sSub * sub\n"
        "f = clamp((cutoff / sr) * 3.14159, 0.001, 0.95)\n"
        "lp = lp + f * (stacked - lp)\n"
        "filt = lp * mix + stacked * (1.0 - mix)\n"
        "out = filt * gate\n" },
      { "Analog 808 Bass Drum",
        "param float decay = 1.2 [0.1, 4.0]\n"
        "param float punch = 3.5 [1.0, 8.0]\n"
        "param float punchDecay = 0.04 [0.005, 0.2]\n"
        "param float drive = 1.8 [1.0, 5.0]\n"
        "param float clickAmt = 0.35 [0.0, 1.0]\n"
        "state float env = 0.0\n"
        "state float pEnv = 0.0\n"
        "state float phase = 0.0\n"
        "state float click = 0.0\n\n"
        "dCoeff = clamp(1.0 - 1.0 / (sr * max(decay, 0.05)), 0.99, 0.99999)\n"
        "pCoeff = clamp(1.0 - 1.0 / (sr * max(punchDecay, 0.002)), 0.9, 0.999)\n\n"
        "env = if(noteOn > 0.5, noteVel, env * dCoeff)\n"
        "pEnv = if(noteOn > 0.5, 1.0, pEnv * pCoeff)\n"
        "click = if(noteOn > 0.5, noteVel * clickAmt, click * 0.90)\n\n"
        "curFreq = notePitch * (1.0 + pEnv * (punch - 1.0))\n"
        "phase = (phase + curFreq / sr) % 1.0\n\n"
        "sine = sin(6.283185 * phase)\n"
        "sig = (sine + click) * env * drive\n"
        "sat = sig / (1.0 + abs(sig) * 0.5)\n\n"
        "out = sat\n" },
      { "West Coast Wavefolder",
        "param float fold = 2.8 [1.0, 6.0]\n"
        "param float decay = 0.8 [0.05, 3.0]\n"
        "param float asymmetry = 0.2 [0.0, 0.8]\n"
        "param float sub = 0.35 [0.0, 1.0]\n"
        "param float tone = 3500.0 [800.0, 12000.0]\n"
        "state float phase = 0.0\n"
        "state float subPhase = 0.0\n"
        "state float env = 0.0\n"
        "state float lp = 0.0\n\n"
        "dCoeff = clamp(1.0 - 1.0 / (sr * max(decay, 0.05)), 0.99, 0.99999)\n"
        "env = if(noteOn > 0.5, noteVel, env * dCoeff)\n\n"
        "phase = (phase + notePitch / sr) % 1.0\n"
        "subPhase = (subPhase + (notePitch * 0.5) / sr) % 1.0\n\n"
        "saw = phase * 2.0 - 1.0\n"
        "tri = 2.0 * abs(saw) - 1.0\n"
        "subOsc = sin(6.283185 * subPhase)\n\n"
        "dynFold = fold * (1.0 + env * 0.5)\n"
        "driveSig = (tri + asymmetry * 0.5) * dynFold\n"
        "folded = sin(3.14159 * driveSig)\n\n"
        "mixSig = folded * 0.7 + subOsc * sub\n\n"
        "fTone = clamp((tone / sr) * 3.14159, 0.005, 0.9)\n"
        "lp = lp + fTone * (mixSig - lp)\n\n"
        "out = lp * env\n" },
      { "Modal Percussion",
        "param float ratio2 = 2.76 [1.2, 7.0]\n"
        "param float ratio3 = 5.40 [2.0, 12.0]\n"
        "param float decay = 2.5 [0.2, 8.0]\n"
        "param float strike = 0.15 [0.01, 0.9]\n"
        "state float armed = 0.0\n"
        "state float mallet = 0.0\n"
        "state float a1 = 0.0\n"
        "state float a2 = 0.0\n"
        "state float b1 = 0.0\n"
        "state float b2 = 0.0\n"
        "state float c1 = 0.0\n"
        "state float c2 = 0.0\n\n"
        "hit = if(gate > 0.5 && armed < 0.5, 1.0, 0.0)\n"
        "armed = if(gate > 0.5, 1.0, 0.0)\n"
        "mallet = mallet + clamp(strike, 0.01, 0.9) * (hit * 8.0 - mallet)\n\n"
        "w1 = min(6.283185 * clamp(freq, 20.0, sr * 0.45) / sr, 2.9)\n"
        "w2 = min(w1 * ratio2, 2.9)\n"
        "w3 = min(w1 * ratio3, 2.9)\n\n"
        "dc1 = clamp(1.0 - 1.0 / (sr * max(decay, 0.2)), 0.9, 0.999999)\n"
        "dc2 = dc1 * dc1\n"
        "dc3 = dc2 * dc1\n\n"
        "cw1 = cos(w1)\n"
        "sw1 = sin(w1)\n"
        "na1 = (a1 * cw1 - a2 * sw1 + mallet) * dc1\n"
        "na2 = (a1 * sw1 + a2 * cw1) * dc1\n"
        "a1 = na1\n"
        "a2 = na2\n\n"
        "cw2 = cos(w2)\n"
        "sw2 = sin(w2)\n"
        "nb1 = (b1 * cw2 - b2 * sw2 + mallet * 0.6) * dc2\n"
        "nb2 = (b1 * sw2 + b2 * cw2) * dc2\n"
        "b1 = nb1\n"
        "b2 = nb2\n\n"
        "cw3 = cos(w3)\n"
        "sw3 = sin(w3)\n"
        "nc1 = (c1 * cw3 - c2 * sw3 + mallet * 0.35) * dc3\n"
        "nc2 = (c1 * sw3 + c2 * cw3) * dc3\n"
        "c1 = nc1\n"
        "c2 = nc2\n\n"
        "out = (a1 + b1 * 0.55 + c1 * 0.3) * 0.5\n" },
      { "Chaotic FM Voice",
        "param float chaosRate = 0.5 [0.05, 3.0]\n"
        "param float chaosAmt = 0.3 [0.0, 1.0]\n"
        "param float chaosR = 3.7 [3.5, 3.99]\n"
        "param float brightness = 1.5 [0.5, 4.0]\n"
        "param float decay = 1.2 [0.2, 4.0]\n"
        "state float x = 0.5\n"
        "state float phase = 0.0\n"
        "state float env = 0.0\n\n"
        "step = clamp(chaosRate * 2000.0 / sr, 0.0001, 0.5)\n"
        "x = x + (chaosR * x * (1.0 - x) - x) * step\n"
        "modAmt = (x - 0.5) * 2.0 * chaosAmt\n\n"
        "carrierFreq = freq * (1.0 + modAmt * brightness * 0.5)\n"
        "phase = (phase + carrierFreq / sr) % 1.0\n"
        "tone = sin(6.283185 * phase)\n\n"
        "target = if(gate > 0.5, 1.0, 0.0)\n"
        "rate = if(target > env, 0.01, clamp(1.0 / (sr * max(decay, 0.05)), 0.0001, 0.02))\n"
        "env = env + (target - env) * rate\n\n"
        "out = tone * env\n" }
   };
   return kPresets;
}

const std::vector<std::string>& FieldSynthNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
   {
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   }
   return kNames;
}

void FieldSynthNode::LoadPreset(int index)
{
   const auto& presets = Presets();
   if (index >= 0 && index < (int)presets.size())
   {
      presetIndex = index;
      code = presets[index].code;
      Apply();
   }
}

Field::DeviceFile FieldSynthNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "synth";
   device.code = code;
   for (const auto& p : mParamTable.Params())
   {
      if (p.isDeclared)
         device.params[p.name] = p.value;
   }
   device.nodeSettings["maxVoices"] = (double)maxVoices;
   device.nodeSettings["exposeRmsOutput"] = exposeRmsOutput ? 1.0 : 0.0;
   return device;
}

void FieldSynthNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   auto itV = device.nodeSettings.find("maxVoices");
   if (itV != device.nodeSettings.end())
      maxVoices = (int)itV->second;
   auto itRms = device.nodeSettings.find("exposeRmsOutput");
   if (itRms != device.nodeSettings.end())
      exposeRmsOutput = (itRms->second > 0.5);
   Apply();
   for (const auto& kv : device.params)
   {
      Field::ParamEntry* p = mParamTable.Find(kv.first);
      if (p != nullptr)
         p->value = kv.second;
   }
}

// -------------------------------------------------------------- main thread
FieldSynthNode::FieldSynthNode()
   : mAudioNode(std::make_unique<AudioFieldSynthNode>())
{
   mRmsOutput.owner = this;
   const auto& presets = Presets();
   if (!presets.empty())
      code = presets[0].code;
   else
      code = "out = sin(6.283185 * ((n * freq) / sr)) * gate\n";
   maxVoices = 8;
   Apply();
}

FieldSynthNode::~FieldSynthNode() = default;

AudioNode* FieldSynthNode::GetAudioNode() { return mAudioNode.get(); }
uint64_t FieldSynthNode::FaultCount() const { return mAudioNode->FaultCount(); }
bool FieldSynthNode::ReadRmsLatest(float& out) { return mAudioNode->ReadRmsLatest(out); }
int FieldSynthNode::ReadScope(float* out, int capacity) { return mAudioNode->ScopeRing().Read(out, capacity); }

bool FieldSynthNode::Apply()
{
   pinRefusal.clear();
   auto newProgram = std::make_unique<Field::SampleProgram>();
   Field::FieldError err;
   const Field::SampleProgram* prevForTransplant = mLastCompiled.valid ? &mLastCompiled : nullptr;
   if (!Field::CompileSampleProgram(code, prevForTransplant, *newProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   {
      std::vector<Field::DeclaredPin> declOut, declIn;
      for (const auto& d : newProgram->declaredOutputs)
         declOut.push_back({ d.name, d.typeName, d.domainName, true });
      for (const auto& d : newProgram->declaredInputs)
         declIn.push_back({ d.name, d.typeName, d.domainName, false });

      std::string pinNotice, pinRefusalMsg;
      bool outOk = Field::ReconcileFieldPins(mOutputPins, declOut, mNodeIndex, NativeOutputCount(), pinNotice, pinRefusalMsg);
      // Native inputs count is 2 (notes at slot 0, audio in at slot 1)
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
   mLastCompiled = *newProgram;

   mAudioNode->SetMaxVoices(maxVoices);
   mAudioNode->PushProgram(newProgram.release());
   mLastError.clear();
   return true;
}

void FieldSynthNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   mAudioNode->DrainRetired();
   mAudioNode->SetMaxVoices(maxVoices);

   for (const auto& p : mCompiledParams)
   {
      if (const Field::ParamEntry* entry = mParamTable.Find(p.name))
         mAudioNode->PushParam(p.mailboxId, entry->value);
   }
}

void FieldSynthNode::VisitParams(ParamVisitor& v)
{
   v.Text("code", code);
   v.Int("maxVoices", maxVoices);
   v.Bool("exposeRmsOutput", exposeRmsOutput);
   mParamTable.VisitParams(v);

   std::string outPins = mOutputPins.SerializePinMap();
   std::string inPins = mInputPins.SerializePinMap();
   v.Text("__outputPins", outPins);
   v.Text("__inputPins", inPins);
   mOutputPins.DeserializePinMap(outPins);
   mInputPins.DeserializePinMap(inPins);
}
