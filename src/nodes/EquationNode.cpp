#include "EquationNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"

namespace
{
   const std::vector<EquationNode::Preset> kPresets = {
      { "Sine (Pure)", "sin(2*pi*x)", EquationDsp::kDomainZeroToOne, 0.5f, 0.5f, 0.0f, 0.0f },
      { "Additive Harmonics", "sin(2*pi*x) + a*sin(4*pi*x) + b*sin(6*pi*x) + c*sin(8*pi*x)", EquationDsp::kDomainZeroToOne, 0.5f, 0.25f, 0.125f, 0.0f },
      { "Chebyshev T3/T4/T5", "4*x^3 - 3*x + a*(8*x^4 - 8*x^2 + 1) + b*(16*x^5 - 20*x^3 + 5*x)", EquationDsp::kDomainNegOneToOne, 0.5f, 0.25f, 0.0f, 0.0f },
      { "Phase Distortion / FM", "sin(2*pi*x + a*sin(2*pi*(1+b*4)*x))", EquationDsp::kDomainZeroToOne, 0.75f, 0.5f, 0.0f, 0.0f },
      { "Sinc Pulse (Dirichlet)", "sin(pi*(1+a*15)*x) / (pi*(1+a*15)*x + 0.0001)", EquationDsp::kDomainNegOneToOne, 0.5f, 0.0f, 0.0f, 0.0f },
      { "Exponential Chirp", "exp(-a*6*x) * sin(2*pi*(1+b*8)*x)", EquationDsp::kDomainZeroToOne, 0.5f, 0.5f, 0.0f, 0.0f },
      { "Weierstrass Fractal", "sin(2*pi*x) + a*sin(6*pi*x) + a^2*sin(18*pi*x) + a^3*sin(54*pi*x)", EquationDsp::kDomainZeroToOne, 0.5f, 0.0f, 0.0f, 0.0f },
      { "Tanh Soft Saturator", "tanh((1 + a*12) * sin(2*pi*x))", EquationDsp::kDomainZeroToOne, 0.5f, 0.0f, 0.0f, 0.0f },
      { "Pulse Width Morph", "if(x < a, 1, -1)", EquationDsp::kDomainZeroToOne, 0.5f, 0.0f, 0.0f, 0.0f },
      { "Formant Vowel Peak", "sin(2*pi*x) * exp(-a*25 * (x - b)^2)", EquationDsp::kDomainZeroToOne, 0.5f, 0.5f, 0.0f, 0.0f },
      { "Fold / Tangent Warp", "tan(x * pi * a * 0.96)", EquationDsp::kDomainNegOneToOne, 0.75f, 0.0f, 0.0f, 0.0f },
      { "Sawtooth Morph", "(x - floor(x)) * 2 - 1 + a * sin(2*pi*b*x)", EquationDsp::kDomainZeroToOne, 0.0f, 1.0f, 0.0f, 0.0f }
   };

   const std::vector<std::string> kDomainNames = {
      "[0, 1] Normalized", "[-pi, pi] Angular", "[-1, 1] Bipolar"
   };

   const std::vector<std::string> kFilterNames = {
      "Bypass", "Lowpass 12dB", "Lowpass 24dB", "Highpass 12dB", "Bandpass"
   };

   enum SmoothedParam
   {
      kParamFrequency = 0,
      kParamVolume,
      kParamPan,
      kParamGlide,
      kParamDetune,
      kParamStereoWidth,
      kParamCutoff,
      kParamResonance,
      kParamDrive,
      kParamFine,
      kNumSmoothedParams
   };

   constexpr float kVoicePhaseSeed[EquationNode::kMaxUnison] = {
      0.000000f, 0.618034f, 0.236068f, 0.854102f,
      0.472136f, 0.090170f, 0.708204f, 0.326238f
   };

   inline float MidiNoteToHz(int midiNote, float fineCents = 0.0f)
   {
      return 440.0f * powf(2.0f, (float)(midiNote - 69 + fineCents * 0.01f) / 12.0f);
   }
}

const std::vector<EquationNode::Preset>& EquationNode::Presets() { return kPresets; }

const std::vector<std::string>& EquationNode::PresetNames()
{
   static std::vector<std::string> sNames;
   if (sNames.empty())
   {
      for (const auto& p : kPresets)
         sNames.push_back(p.name);
   }
   return sNames;
}

const std::vector<std::string>& EquationNode::DomainNames() { return kDomainNames; }
const std::vector<std::string>& EquationNode::FilterTypeNames() { return kFilterNames; }

// ---------------------------------------------------------------------------
// AudioEquationNode: Audio Thread DSP Synthesizer
// ---------------------------------------------------------------------------
class AudioEquationNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = EquationNode::kMaxVoices;
   static constexpr int kMaxUnison = EquationNode::kMaxUnison;

   AudioEquationNode()
   {
      auto* initialBank = new EquationDsp::EquationBank();
      mBankSlot.Push(initialBank);
      mBankSlot.SwapIn();

      for (int v = 0; v < kMaxVoices; v++)
         mVoices[v].Reset(44100.0);
   }

   ~AudioEquationNode() override = default;

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void PrepareToPlay(double sampleRate, int maxBlockSize) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      for (int v = 0; v < kMaxVoices; v++)
         mVoices[v].Reset(mSampleRate);

      const int allocSize = std::max(maxBlockSize, 4096);
      mMonoScope.resize(allocSize, 0.0f);
   }

   void SwapBank(const EquationDsp::EquationBank& newBank)
   {
      mBankSlot.Push(new EquationDsp::EquationBank(newBank));
   }

   void DrainRetiredBanks()
   {
      mBankSlot.DrainRetired();
   }

   void PushParams(const EquationNode& n)
   {
      mUnison.store(std::clamp(n.unison, 1, kMaxUnison), std::memory_order_relaxed);
      mOctave.store(n.octave, std::memory_order_relaxed);
      mSemi.store(n.semi, std::memory_order_relaxed);
      mFilterType.store(n.filterType, std::memory_order_relaxed);
      mFilterAmount.store(n.filterAmount, std::memory_order_relaxed);

      mAmpAdsr[0].store(n.ampAttack, std::memory_order_relaxed);
      mAmpAdsr[1].store(n.ampDecay, std::memory_order_relaxed);
      mAmpAdsr[2].store(n.ampSustain, std::memory_order_relaxed);
      mAmpAdsr[3].store(n.ampRelease, std::memory_order_relaxed);

      mFilterAdsr[0].store(n.filterAttack, std::memory_order_relaxed);
      mFilterAdsr[1].store(n.filterDecay, std::memory_order_relaxed);
      mFilterAdsr[2].store(n.filterSustain, std::memory_order_relaxed);
      mFilterAdsr[3].store(n.filterRelease, std::memory_order_relaxed);

      const float values[kNumSmoothedParams] = {
         n.frequency,
         n.volume,
         n.pan,
         n.glide,
         n.detune,
         n.stereoWidth,
         n.cutoff,
         n.resonance,
         n.drive,
         n.fine
      };

      for (int i = 0; i < kNumSmoothedParams; i++)
         mMailbox.Push(i, values[i]);
   }

   int AllocateVoice()
   {
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (!mVoices[v].active)
            return v;
      }
      uint64_t oldestAge = UINT64_MAX;
      int oldestIdx = 0;
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (mVoices[v].age < oldestAge)
         {
            oldestAge = mVoices[v].age;
            oldestIdx = v;
         }
      }
      return oldestIdx;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      if (numFrames <= 0 || output.numChannels < 2)
         return;

      float* outL = output.channels[0];
      float* outR = output.channels[1];
      std::fill_n(outL, numFrames, 0.0f);
      std::fill_n(outR, numFrames, 0.0f);

      mBankSlot.SwapIn();
      const EquationDsp::EquationBank& bank = *mBankSlot.Active();

      const int unison = mUnison.load(std::memory_order_relaxed);
      const int octave = mOctave.load(std::memory_order_relaxed);
      const int semi = mSemi.load(std::memory_order_relaxed);
      const int filterType = mFilterType.load(std::memory_order_relaxed);
      const float filterAmount = mFilterAmount.load(std::memory_order_relaxed);

      const float ampA = mAmpAdsr[0].load(std::memory_order_relaxed);
      const float ampD = mAmpAdsr[1].load(std::memory_order_relaxed);
      const float ampS = mAmpAdsr[2].load(std::memory_order_relaxed);
      const float ampR = mAmpAdsr[3].load(std::memory_order_relaxed);

      const float fltA = mFilterAdsr[0].load(std::memory_order_relaxed);
      const float fltD = mFilterAdsr[1].load(std::memory_order_relaxed);
      const float fltS = mFilterAdsr[2].load(std::memory_order_relaxed);
      const float fltR = mFilterAdsr[3].load(std::memory_order_relaxed);

      if (mNoteInbox != nullptr)
      {
         NoteEvent evts[64];
         const int numEvts = mNoteInbox->Pop(mNoteCursor, evts, 64);
         for (int i = 0; i < numEvts; i++)
         {
            const auto& event = evts[i];
            if (event.isNoteOn && event.velocity > 0.0f)
            {
               const int voiceIdx = AllocateVoice();
               Voice& v = mVoices[voiceIdx];
               v.active = true;
               v.held = true;
               v.note = event.note;
               v.voiceId = event.voiceId;
               v.velocity = event.velocity;
               v.age = ++mAgeCounter;
               v.amp.SetADSR(ampA, ampD, ampS, ampR);
               v.amp.NoteOn();
               v.filt.SetADSR(fltA, fltD, fltS, fltR);
               v.filt.NoteOn();

               const float baseHz = MidiNoteToHz(event.note + octave * 12 + semi);
               v.targetFreq = baseHz;
               if (v.currentFreq <= 0.0f)
                  v.currentFreq = baseHz;
            }
            else if (!event.isNoteOn && !event.bendUpdate)
            {
               for (int v = 0; v < kMaxVoices; v++)
               {
                  if (mVoices[v].active && (mVoices[v].voiceId == event.voiceId || mVoices[v].note == event.note) && mVoices[v].held)
                  {
                     mVoices[v].held = false;
                     mVoices[v].amp.NoteOff();
                     mVoices[v].filt.NoteOff();
                  }
               }
            }
         }
      }

      int activeCount = 0;
      const bool isNoteDriven = (mNoteInbox != nullptr);
      if (mMonoScope.size() < (size_t)numFrames)
         mMonoScope.resize(numFrames, 0.0f);

      for (int i = 0; i < numFrames; i++)
      {
         const float baseFreq = mMailbox.SmoothedValue(kParamFrequency);
         const float volume = mMailbox.SmoothedValue(kParamVolume);
         const float pan = mMailbox.SmoothedValue(kParamPan);
         const float glide = mMailbox.SmoothedValue(kParamGlide);
         const float detune = mMailbox.SmoothedValue(kParamDetune);
         const float stereoWidth = mMailbox.SmoothedValue(kParamStereoWidth);
         const float cutoff = mMailbox.SmoothedValue(kParamCutoff);
         const float resonance = mMailbox.SmoothedValue(kParamResonance);
         const float drive = mMailbox.SmoothedValue(kParamDrive);
         const float fine = mMailbox.SmoothedValue(kParamFine);

         float frameSumL = 0.0f;
         float frameSumR = 0.0f;

         if (!isNoteDriven)
         {
            Voice& v = mVoices[0];
            v.active = true;
            v.held = true;
            const float targetHz = baseFreq * powf(2.0f, (float)(octave * 12 + semi) / 12.0f + fine * 0.01f / 12.0f);
            v.currentFreq = targetHz;

            float vOutL = 0.0f, vOutR = 0.0f;
            RenderVoice(v, bank, v.currentFreq, unison, detune, stereoWidth, filterType, cutoff, resonance, 0.0f, filterAmount, vOutL, vOutR);
            frameSumL += vOutL;
            frameSumR += vOutR;
         }
         else
         {
            for (int vIdx = 0; vIdx < kMaxVoices; vIdx++)
            {
               Voice& v = mVoices[vIdx];
               if (!v.active)
                  continue;

               const float ampEnv = v.amp.Process();
               const float filtEnv = v.filt.Process();

               if (!v.amp.IsActive())
               {
                  v.active = false;
                  continue;
               }

               if (glide > 0.001f)
               {
                  const float glideCoef = expf(-1.0f / (float)(mSampleRate * glide));
                  v.currentFreq = v.targetFreq + glideCoef * (v.currentFreq - v.targetFreq);
               }
               else
               {
                  v.currentFreq = v.targetFreq;
               }

               const float voiceHz = v.currentFreq * powf(2.0f, fine * 0.01f / 12.0f);
               float vOutL = 0.0f, vOutR = 0.0f;
               RenderVoice(v, bank, voiceHz, unison, detune, stereoWidth, filterType, cutoff, resonance, filtEnv, filterAmount, vOutL, vOutR);

               const float gain = ampEnv * v.velocity;
               frameSumL += vOutL * gain;
               frameSumR += vOutR * gain;
            }
         }

         if (drive > 0.01f)
         {
            const float driveGain = 1.0f + drive * 5.0f;
            frameSumL = tanhf(frameSumL * driveGain) / (1.0f + drive * 0.5f);
            frameSumR = tanhf(frameSumR * driveGain) / (1.0f + drive * 0.5f);
         }

         float panL, panR;
         DspMath::EqualPowerPan(pan, panL, panR);
         outL[i] = frameSumL * volume * panL;
         outR[i] = frameSumR * volume * panR;
         mMonoScope[i] = (outL[i] + outR[i]) * 0.5f;
      }

      mScopeRing.Write(mMonoScope.data(), numFrames);

      if (isNoteDriven)
      {
         for (int vIdx = 0; vIdx < kMaxVoices; vIdx++)
         {
            if (mVoices[vIdx].active)
               activeCount++;
         }
      }
      else
      {
         activeCount = 1;
      }
      mActiveVoices.store(activeCount, std::memory_order_relaxed);
   }

   MeterRing& ScopeRing() { return mScopeRing; }
   int ActiveVoices() const { return mActiveVoices.load(std::memory_order_relaxed); }

private:
   struct Voice
   {
      bool active = false;
      bool held = false;
      int note = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      float currentFreq = 220.0f;
      float targetFreq = 220.0f;
      uint64_t age = 0;
      double phase[kMaxUnison] = {};
      Envelope amp;
      Envelope filt;
      DspMath::TptSvf filter[2];
      DspMath::TptSvf filter2[2];

      void Reset(double sampleRate)
      {
         active = false;
         held = false;
         note = -1;
         voiceId = 0;
         velocity = 0.0f;
         currentFreq = 220.0f;
         targetFreq = 220.0f;
         age = 0;
         for (int u = 0; u < kMaxUnison; u++)
            phase[u] = 0.0;
         amp.SetSampleRate(sampleRate);
         filt.SetSampleRate(sampleRate);
         for (int c = 0; c < 2; c++)
         {
            filter[c].SetSampleRate(sampleRate);
            filter[c].Reset();
            filter2[c].SetSampleRate(sampleRate);
            filter2[c].Reset();
         }
      }
   };

   void RenderVoice(Voice& v, const EquationDsp::EquationBank& bank, float baseFreq, int unison,
                    float detuneCents, float stereoWidth, int filterType, float cutoffHz,
                    float resonance, float filtEnv, float filterAmount, float& outL, float& outR)
   {
      outL = 0.0f;
      outR = 0.0f;
      if (baseFreq <= 0.0f)
         return;

      float sumL = 0.0f;
      float sumR = 0.0f;

      for (int u = 0; u < unison; u++)
      {
         const float spread = (unison > 1)
            ? detuneCents * 0.5f * (2.0f * (float)u / (float)(unison - 1) - 1.0f)
            : 0.0f;
         const float voiceFreq = baseFreq * powf(2.0f, spread / 1200.0f);
         const double inc = (double)voiceFreq / mSampleRate;

         const double randOffset = (double)kVoicePhaseSeed[u] * 0.5;
         const double readPhase = v.phase[u] + randOffset;

         const float s = EquationDsp::SampleBank(bank, readPhase, inc);

         v.phase[u] += inc;
         v.phase[u] -= floor(v.phase[u]);

         const float spreadPan = (unison > 1)
            ? stereoWidth * (2.0f * (float)u / (float)(unison - 1) - 1.0f)
            : 0.0f;
         float panL, panR;
         DspMath::EqualPowerPan(std::clamp(spreadPan, -1.0f, 1.0f), panL, panR);
         sumL += s * panL;
         sumR += s * panR;
      }

      const float norm = 1.0f / sqrtf((float)unison);
      sumL *= norm;
      sumR *= norm;

      if (filterType > 0)
      {
         const float modulatedCutoff = cutoffHz * exp2f(filterAmount * filtEnv);
         const float clampedHz = std::clamp(modulatedCutoff, 20.0f, 20000.0f);
         const float q = 0.707f + resonance * 9.0f;

         for (int c = 0; c < 2; c++)
         {
            v.filter[c].SetCutoff(clampedHz, q);
            if (filterType == 2) // LP24
               v.filter2[c].SetCutoff(clampedHz, q);
         }

         auto resL = v.filter[0].Process(sumL);
         auto resR = v.filter[1].Process(sumR);

         switch (filterType)
         {
            case 1: // LP12
               outL = resL.low;
               outR = resR.low;
               break;
            case 2: // LP24
            {
               auto resL2 = v.filter2[0].Process(resL.low);
               auto resR2 = v.filter2[1].Process(resR.low);
               outL = resL2.low;
               outR = resR2.low;
               break;
            }
            case 3: // HP12
               outL = resL.high;
               outR = resR.high;
               break;
            case 4: // BP
               outL = resL.band;
               outR = resR.band;
               break;
            default:
               outL = sumL;
               outR = sumR;
               break;
         }
      }
      else
      {
         outL = sumL;
         outR = sumR;
      }
   }

   double mSampleRate = 44100.0;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = 0;

   ParamMailbox mMailbox;
   SampleSlotT<EquationDsp::EquationBank> mBankSlot;
   MeterRing mScopeRing;
   std::vector<float> mMonoScope;

   std::atomic<int> mUnison{1};
   std::atomic<int> mOctave{0};
   std::atomic<int> mSemi{0};
   std::atomic<int> mFilterType{1};
   std::atomic<float> mFilterAmount{0.0f};

   std::atomic<float> mAmpAdsr[4]{ 5.0f, 250.0f, 0.75f, 200.0f };
   std::atomic<float> mFilterAdsr[4]{ 5.0f, 300.0f, 0.4f, 250.0f };

   Voice mVoices[kMaxVoices];
   uint64_t mAgeCounter = 0;
   std::atomic<int> mActiveVoices{0};
};

// ---------------------------------------------------------------------------
// EquationNode: Main Thread State & Graph Interface
// ---------------------------------------------------------------------------
EquationNode::EquationNode()
   : mAudioNode(std::make_unique<AudioEquationNode>())
{
   mPreviewCurve.resize(EquationDsp::kFrameSize, 0.0f);
   CompileEquation();
   RebuildBank();
}

EquationNode::~EquationNode() = default;

AudioNode* EquationNode::GetAudioNode()
{
   return mAudioNode.get();
}

int EquationNode::ReadScope(float* out, int capacity)
{
   if (!mAudioNode || !out || capacity <= 0)
      return 0;
   return mAudioNode->ScopeRing().Read(out, capacity);
}

int EquationNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void EquationNode::LoadPreset(int index)
{
   if (index < 0 || index >= (int)kPresets.size())
      return;
   const auto& p = kPresets[index];
   presetIndex = index;
   formula = p.formula;
   domainMode = p.domainMode;
   knobA = p.a;
   knobB = p.b;
   knobC = p.c;
   knobD = p.d;
   CompileEquation();
   RebuildBank();
}

bool EquationNode::CompileEquation()
{
   EquationDsp::AstNodePtr newAst;
   std::string err;
   if (EquationDsp::Parser::Parse(formula, newAst, err))
   {
      mAst = newAst;
      mLastError.clear();
      return true;
   }
   mLastError = err;
   return false;
}

void EquationNode::RebuildBank()
{
   if (!mAst)
   {
      if (!CompileEquation() && !mAst)
      {
         // Create fallback sine AST if none exists
         EquationDsp::Parser::Parse("sin(2*pi*x)", mAst, mLastError);
      }
   }

   if (mAst)
   {
      EquationDsp::EquationBank bank;
      EquationDsp::BuildBankFromAst(bank, *mAst, domainMode, knobA, knobB, knobC, knobD, 0.0f);
      mPreviewCurve = bank.previewCurve;
      if (mAudioNode)
         mAudioNode->SwapBank(bank);
   }
}

void EquationNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   // Check if preset or formula or parameter knobs changed
   const bool formulaChanged = (formula != mLastFormula);
   const bool domainChanged = (domainMode != mLastDomainMode);
   const bool knobsChanged = (knobA != mLastKnobA || knobB != mLastKnobB || knobC != mLastKnobC || knobD != mLastKnobD);

   if (formulaChanged || domainChanged || knobsChanged)
   {
      if (formulaChanged)
         CompileEquation();

      RebuildBank();

      mLastFormula = formula;
      mLastDomainMode = domainMode;
      mLastKnobA = knobA;
      mLastKnobB = knobB;
      mLastKnobC = knobC;
      mLastKnobD = knobD;
   }

   if (mAudioNode)
   {
      mAudioNode->DrainRetiredBanks();
      mAudioNode->PushParams(*this);

      // Decimated scope read (up to 30 Hz refresh)
      float tempScope[kScopeCapacity];
      const int read = mAudioNode->ScopeRing().Read(tempScope, kScopeCapacity);
      if (read > 0)
      {
         const int toCopy = std::min(read, kScopeCapacity);
         std::copy_n(tempScope, toCopy, scopeCache);
         scopeCacheCount = toCopy;
      }
   }
}

void EquationNode::VisitParams(ParamVisitor& v)
{
   v.Text("formula", formula);
   v.Int("preset", presetIndex);
   v.Int("domainMode", domainMode);

   v.Float("knobA", knobA);
   v.Float("knobB", knobB);
   v.Float("knobC", knobC);
   v.Float("knobD", knobD);

   v.Float("volume", volume);
   v.Float("pan", pan);
   v.Float("frequency", frequency);
   v.Int("octave", octave);
   v.Int("semi", semi);
   v.Float("fine", fine);
   v.Float("glide", glide);

   v.Int("unison", unison);
   v.Float("detune", detune);
   v.Float("stereoWidth", stereoWidth);

   v.Float("ampAttack", ampAttack);
   v.Float("ampDecay", ampDecay);
   v.Float("ampSustain", ampSustain);
   v.Float("ampRelease", ampRelease);

   v.Int("filterType", filterType);
   v.Float("cutoff", cutoff);
   v.Float("resonance", resonance);
   v.Float("filterAmount", filterAmount);
   v.Float("filterAttack", filterAttack);
   v.Float("filterDecay", filterDecay);
   v.Float("filterSustain", filterSustain);
   v.Float("filterRelease", filterRelease);

   v.Float("drive", drive);
}
