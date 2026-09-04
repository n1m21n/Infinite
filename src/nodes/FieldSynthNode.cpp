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
      std::fill(mDelayBuffer, mDelayBuffer + Field::kSampleMaxDelayCells, 0.0f);
      std::fill(mDelaySwapBuffer, mDelaySwapBuffer + Field::kSampleMaxDelayCells, 0.0f);
      std::fill(mDelayCursors, mDelayCursors + Field::kSampleMaxDelayLines, 0);
      std::fill(mDelaySwapCursors, mDelaySwapCursors + Field::kSampleMaxDelayLines, 0);
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

         // Delay line transplant
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

      // Slot 0 is notes, slot 1 is audio in (if connected)
      const AudioBuffer* inBuf = (numInputs > 1) ? inputs[1] : nullptr;

      NoteEvent evts[64];
      int numEvts = 0;
      int evtIdx = 0;
      if (mNoteInbox != nullptr)
         numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);

      float paramVals[Field::kSampleMaxParams] = {};
      bool sawFault = false;

      for (int i = 0; i < buffer.numFrames; i++)
      {
         while (evtIdx < numEvts && evts[evtIdx].frameOffset <= i)
         {
            if (evts[evtIdx].isNoteOn)
            {
               const int v = mVoices.NoteOn(evts[evtIdx].note, evts[evtIdx].velocity, evts[evtIdx].voiceId);
               mVoiceId[v] = evts[evtIdx].voiceId;
               mGateHeld[v] = true;
               const int n = (int)mActiveProgram->state.size();
               for (int c = 0; c < n; c++)
               {
                  mStateCur[v][c] = mActiveProgram->state[c].initialValue;
                  mStateNext[v][c] = mActiveProgram->state[c].initialValue;
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
            rin.paramVals = paramVals;
            rin.stateCur = mStateCur[v];
            rin.stateNext = mStateNext[v];
            rin.delayBuf = mDelayBuffer;
            rin.delayCursors = mDelayCursors;

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

   float mDelayBuffer[Field::kSampleMaxDelayCells];
   float mDelaySwapBuffer[Field::kSampleMaxDelayCells];
   int mDelayCursors[Field::kSampleMaxDelayLines];
   int mDelaySwapCursors[Field::kSampleMaxDelayLines];

   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

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
      { "Polyphonic FM Bell",
        "param float modRatio = 2.76 [0.5, 8.0]\n"
        "param float modIndex = 3.5 [0.0, 10.0]\n"
        "state float pCarrier = 0\n"
        "state float pMod = 0\n\n"
        "pMod = (pMod + (freq * modRatio) / sr) % 1.0\n"
        "modSig = sin(6.283185 * pMod) * modIndex\n\n"
        "pCarrier = (pCarrier + (freq / sr) * (1.0 + modSig)) % 1.0\n"
        "out = sin(6.283185 * pCarrier) * gate\n" },
      { "Subtractive Saw Lead",
        "param float cutoff = 1500.0 [100.0, 10000.0]\n"
        "param float res = 0.5 [0.0, 0.95]\n"
        "state float phase = 0\n"
        "state float lp = 0\n\n"
        "phase = (phase + freq / sr) % 1.0\n"
        "rawSaw = phase * 2.0 - 1.0\n"
        "f = clamp((cutoff / sr) * 3.14159, 0.001, 0.99)\n"
        "lp = lp + f * (rawSaw - lp)\n"
        "out = (lp + (rawSaw - lp) * (1.0 - res)) * gate\n" },
      { "Karplus-Strong Pluck",
        "param float damp = 0.45 [0.05, 0.95]\n"
        "state float pInit = 0\n"
        "state float lastSample = 0\n\n"
        "period = clamp(floor(sr / max(20.0, freq)), 4.0, 2000.0)\n"
        "exciter = if(gate > 0.5 && pInit < 1.0, 0.8, 0.0)\n"
        "pInit = if(gate > 0.5, pInit + 1.0, 0.0)\n"
        "d = delay(exciter + lastSample * (1.0 - damp * 0.05), period)\n"
        "lastSample = d\n"
        "out = d\n" },
      { "Dual Oscillator Pad",
        "param float detune = 1.006 [1.0, 1.03]\n"
        "state float p1 = 0\n"
        "state float p2 = 0\n"
        "state float env = 0\n\n"
        "p1 = (p1 + freq / sr) % 1.0\n"
        "p2 = (p2 + (freq * detune) / sr) % 1.0\n"
        "target = if(gate > 0.5, 1.0, 0.0)\n"
        "env = env * 0.999 + target * 0.001\n"
        "tone = sin(6.283185 * p1) * 0.5 + sin(6.283185 * p2) * 0.5\n"
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
