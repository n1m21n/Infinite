#include "FieldSampleNode.h"

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
   constexpr int kMaxVoices = 16; // fixed cap; FieldSampleNode::maxVoices (UI) selects how many of these are used
   constexpr float kOutClamp = 4.0f; // ~12dB headroom, matches the rest of the audio graph's clamp convention

   // Same formula and naming convention as every other synth node's local
   // helper (OscillatorNode/ImageSpectralSynthNode/WaveTerrainNode/
   // EquationNode/MetallicNode all define their own copy rather than share
   // one from a header) - see WavetableSynthCore::NoteToHz for the
   // canonical form this mirrors.
   inline float MidiNoteToHz(int midiNote)
   {
      return 440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f);
   }
}

// ------------------------------------------------------------- audio thread
class AudioFieldSampleNode : public AudioNode
{
public:
   AudioFieldSampleNode() : mVoices(kMaxVoices)
   {
      std::memset(mStateCur, 0, sizeof(mStateCur));
      std::memset(mStateNext, 0, sizeof(mStateNext));
      std::memset(mGateHeld, 0, sizeof(mGateHeld));
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mVoices.SetSampleRate(sampleRate);
      // Fast fixed attack/release just enough to avoid a click on trigger/
      // release - the Field kernel's own 'state' cells are where any real
      // envelope shaping happens; this is not a musical parameter.
      mVoices.SetADSR(2.0f, 0.0f, 1.0f, 30.0f);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   // Main thread only. Hands over a freshly compiled program; retired via
   // the existing SampleSlotT compile-swap channel, adopted at the top of
   // the next ProcessBlock - never mid-block.
   void PushProgram(Field::SampleProgram* prog) { mProgramSlot.Push(prog); }
   void DrainRetired() { mProgramSlot.DrainRetired(); }

   // Main thread only, called once per frame for every declared param.
   void PushParam(int mailboxId, float value) { mMailbox.Push(mailboxId, value); }

   void SetMaxVoices(int n) { mNumVoicesInUse = std::clamp(n, 1, kMaxVoices); }

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
         mActiveProgram = fresh;
      }

      for (int ch = 0; ch < buffer.numChannels; ch++)
         std::fill(buffer.channels[ch], buffer.channels[ch] + buffer.numFrames, 0.0f);

      if (mActiveProgram == nullptr || !mActiveProgram->valid)
         return;

      // Slot 1, not 0 - slot 0 is the note pin's slot in the shared pin
      // index space (see FieldSampleNode.h's AudioInputSlot override).
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
               // Field 'gate': 1.0 for as long as this voice's note is
               // held, dropping to 0.0 the instant note-off arrives below -
               // independent of the amplitude envelope's own release tail
               // (EnvelopeAt(v), which keeps decaying after gate goes low).
               mGateHeld[v] = true;
               // Every voice assignment (idle or stolen) resets that voice's
               // Field 'state' cells to their declared initial values - a
               // new note is a fresh instance of the kernel's own per-voice
               // memory, unlike the envelope curve (which intentionally does
               // NOT reset on a same-note legato retrigger; see Envelope::
               // ResetLevel's comment). Field has no such legato exception.
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
               // mVoiceId[] is this class's own record of which voice index
               // currently holds which voiceId (kMaxVoices is small and
               // fixed, so this bounded scan is real-time safe) - matches
               // VoiceAllocator::NoteOff's own internal lookup without
               // needing a second accessor added to the shared class.
               for (int gv = 0; gv < kMaxVoices; gv++)
                  if (mVoiceId[gv] == evts[evtIdx].voiceId)
                     mGateHeld[gv] = false;
            }
            evtIdx++;
         }

         // Fetched once per sample, ABOVE the voice loop - fetching per
         // voice would corrupt the smoothing ramp's effective time constant
         // based on voice count (see SampleRuntime.h).
         const float inVal = (inBuf != nullptr && inBuf->numChannels > 0) ? inBuf->channels[0][i] : 0.0f;
         const float srVal = (float)mSampleRate;
         const float nVal = (float)mAbsSampleCounter;
         mAbsSampleCounter += 1.0;

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
            // Per-voice, unlike in/sr/n above - freq/gate are always
            // populated regardless of whether 'in' is connected, so a
            // kernel can be a self-contained generator (design-prompt-
            // sample-generator-mode.md). freq holds the voice's last
            // triggered note's frequency even after gate drops to 0 (no
            // reason to zero it - a released voice's kernel may still want
            // to know what it was playing during its own release tail).
            rin.freq = MidiNoteToHz(mVoices.NoteAt(v));
            rin.gate = mGateHeld[v] ? 1.0f : 0.0f;
            rin.paramVals = paramVals;
            rin.stateCur = mStateCur[v];
            rin.stateNext = mStateNext[v];

            const float kernelOut = Field::RunSampleProgram(*mActiveProgram, rin, regs);
            const float env = mVoices.EnvelopeAt(v).Process();
            sampleAcc += kernelOut * env;

            // Ping-pong the per-voice state bank for the next sample.
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

         // Same pattern as WavetableSynthCore's mScopeRing - one sample per
         // Write() call, decimation and cadence are the UI reader's job.
         mScopeRing.Write(&sampleAcc, 1);
      }

      // Once-per-block NaN/inf sweep over live voice state (not per sample -
      // that would be a real-time-hostile unbounded-looking cost on every
      // sample; a poisoned state cell is caught within one block either
      // way). A hit zeroes the block just rendered, resets every voice's
      // state to its declared initial values, and bumps the fault counter
      // rather than silently propagating NaN forever.
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
   VoiceAllocator mVoices;
   int mVoiceId[kMaxVoices] = {};
   bool mGateHeld[kMaxVoices] = {}; // Field 'gate': true from note-on until note-off, independent of envelope release
   int mNumVoicesInUse = kMaxVoices;

   ParamMailbox mMailbox;
   SampleSlotT<Field::SampleProgram> mProgramSlot;
   Field::SampleProgram* mActiveProgram = nullptr;

   float mStateCur[kMaxVoices][Field::kSampleMaxStateCells];
   float mStateNext[kMaxVoices][Field::kSampleMaxStateCells];

   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   MeterRing mMeter;
   MeterRing mScopeRing;
   std::atomic<uint64_t> mFaultCount { 0 };

   double mSampleRate = 44100.0;
   double mAbsSampleCounter = 0.0;
};

// Field build step 17 (.infdev device files) - see FieldElementNode's
// identical pair for the rationale. FieldSampleNode has no factory
// Presets() (§0.2 of the plan doc), so this is the only save/load path
// beyond the enclosing .inf patch.
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
   device.nodeSettings["maxVoices"] = (double)maxVoices;
   return device;
}

void FieldSampleNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   auto itV = device.nodeSettings.find("maxVoices");
   if (itV != device.nodeSettings.end())
      maxVoices = (int)itV->second;
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

   mAudioNode->SetMaxVoices(maxVoices);
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
   mAudioNode->SetMaxVoices(maxVoices);

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
   v.Int("maxVoices", maxVoices);
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
