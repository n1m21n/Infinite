#include "WaveTerrainNode.h"

#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>
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
#include "audio/SynthModes.h"
#include "core/GLUtil.h"

namespace
{
   const std::vector<std::string> kOrbitNames = {
      "Circle / Ellipse", "Lissajous", "Lemniscate (8)", "Spiral", "Scanline"
   };

   const std::vector<std::string> kChannelNames = {
      "Luminance", "Red", "Green", "Blue", "Alpha", "Edge Magnitude"
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
      kParamPosition,
      kParamDetune,
      kParamStereoWidth,
      kParamCutoff,
      kParamResonance,
      kParamDrive,
      kParamFine,
      kNumSmoothedParams
   };

   constexpr float kVoicePhaseSeed[WaveTerrainNode::kMaxUnison] = {
      0.000000f, 0.618034f, 0.236068f, 0.854102f,
      0.472136f, 0.090170f, 0.708204f, 0.326238f
   };

   inline float MidiNoteToHz(int midiNote, float fineCents = 0.0f)
   {
      return 440.0f * powf(2.0f, (float)(midiNote - 69 + fineCents * 0.01f) / 12.0f);
   }

   // CPU-only procedural fallback for when there's no source texture to read
   // (no cable connected, or no GL context at all - the headless test/sweep
   // harness). Mirrors ImageSpectralSynthNode's SpectrogramMatrix::
   // GenerateDefaultPattern(): a flat single-value image would make the
   // terrain surface uniformly flat, so every orbit shape/param would sample
   // the same silence regardless of its own value - not just cosmetically
   // wrong, but exactly why headless orbit-param changes went unobserved on
   // a flat placeholder. A few overlapping rings give the surface actual
   // height variation so orbit shape/position genuinely matters.
   inline void FillProceduralDefault(std::vector<uint8_t>& pixels, int size)
   {
      pixels.resize((size_t)size * size * 4);
      for (int y = 0; y < size; y++)
      {
         for (int x = 0; x < size; x++)
         {
            const float u = ((float)x + 0.5f) / (float)size - 0.5f;
            const float v = ((float)y + 0.5f) / (float)size - 0.5f;
            const float r = sqrtf(u * u + v * v);
            const float ang = atan2f(v, u);
            const float val = 0.5f + 0.35f * sinf(r * 26.0f) + 0.15f * sinf(ang * 5.0f);
            const uint8_t lum = (uint8_t)std::clamp((int)(val * 255.0f), 0, 255);
            uint8_t* px = pixels.data() + ((size_t)y * size + x) * 4;
            px[0] = lum;
            px[1] = lum;
            px[2] = lum;
            px[3] = 255;
         }
      }
   }

   const char* kLineVertSrc =
      "#version 150\n"
      "in vec2 aPos;\n"
      "void main() {\n"
      "   gl_Position = vec4(aPos * 2.0 - 1.0, 0.0, 1.0);\n"
      "}\n";

   const char* kLineFragSrc =
      "#version 150\n"
      "out vec4 fragColor;\n"
      "uniform vec4 uColor;\n"
      "void main() {\n"
      "   fragColor = uColor;\n"
      "}\n";
}

const std::vector<std::string>& WaveTerrainNode::OrbitTypeNames() { return kOrbitNames; }
const std::vector<std::string>& WaveTerrainNode::ChannelModeNames() { return kChannelNames; }
const std::vector<std::string>& WaveTerrainNode::FilterTypeNames() { return kFilterNames; }

// ---------------------------------------------------------------------------
// AudioWaveTerrainNode: Audio Thread DSP Processor
// ---------------------------------------------------------------------------
class AudioWaveTerrainNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = WaveTerrainNode::kMaxVoices;
   static constexpr int kMaxUnison = WaveTerrainNode::kMaxUnison;

   AudioWaveTerrainNode()
   {
      // Seed an initial bank (plain sine, same as before) and adopt it
      // immediately - the audio thread isn't running yet at construction
      // time, so it's safe to call SwapIn() directly here rather than
      // waiting for the first ProcessBlock, which keeps Active() non-null
      // from the very first call.
      auto* initialBank = new WaveTerrainDsp::BankData();
      for (int f = 0; f < WaveTerrainDsp::kFrames; f++)
      {
         for (int L = 0; L < WaveTerrainDsp::kMipLevels; L++)
         {
            float* dst = initialBank->data.data() + initialBank->Offset(f, L);
            for (int i = 0; i < WaveTerrainDsp::kFrameSize; i++)
               dst[i] = sinf(6.283185307f * (float)i / (float)WaveTerrainDsp::kFrameSize);
         }
      }
      mBankSlot.Push(initialBank);
      mBankSlot.SwapIn();

      for (int v = 0; v < kMaxVoices; v++)
         mVoices[v].Reset(44100.0);
   }

   ~AudioWaveTerrainNode() override = default;

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

   // Main thread only. Hands a freshly built bank over to the audio thread;
   // see SampleSlot.h. This replaces a hand-rolled two-slot double buffer
   // that had a real race: the audio thread binds `bank` once at the top of
   // ProcessBlock and holds that reference for the whole block, but with
   // only two slots a *second* main-thread swap inside that same block
   // would overwrite the very buffer being read. SampleSlotT's pending/
   // active/retire discipline (adopt only at the top of ProcessBlock, retire
   // instead of overwrite, free only after the audio thread has moved on)
   // is exactly the fix, reused rather than hand-rolled a third time.
   void SwapBank(const WaveTerrainDsp::BankData& newBank)
   {
      mBankSlot.Push(new WaveTerrainDsp::BankData(newBank));
   }

   // Main thread only, once per CookIfNeeded - frees whatever the audio
   // thread has since retired.
   void DrainRetiredBanks() { mBankSlot.DrainRetired(); }

   void PushParams(const WaveTerrainNode& n)
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
         n.position,
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

      // Adopt a freshly pushed bank only here, at the top of the block
      // (never mid-block) - see SampleSlot.h's contract.
      mBankSlot.SwapIn();
      const WaveTerrainDsp::BankData& bank = *mBankSlot.Active();

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
         const float position = mMailbox.SmoothedValue(kParamPosition);
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
            RenderVoice(v, bank, v.currentFreq, unison, position, detune, stereoWidth, filterType, cutoff, resonance, 0.0f, filterAmount, vOutL, vOutR);
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

               // `fine` (cents offset) is applied at render time only, kept
               // out of v.currentFreq itself, so the glide state above tracks
               // pure pitch and doesn't have to be re-derived if `fine`
               // changes mid-glide. The free-run branch above already applied
               // fine correctly; this is what made note-driven mode match it
               // (previously voiceHz was computed and never passed anywhere -
               // RenderVoice read v.currentFreq directly, so fine was a no-op
               // in note-driven mode).
               const float voiceHz = v.currentFreq * powf(2.0f, fine * 0.01f / 12.0f);
               float vOutL = 0.0f, vOutR = 0.0f;
               RenderVoice(v, bank, voiceHz, unison, position, detune, stereoWidth, filterType, cutoff, resonance, filtEnv, filterAmount, vOutL, vOutR);

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

      // Count active voices once per block, not once per sample: activeCount
      // used to be incremented inside the per-sample loop above, so the
      // "N voices" status line read ~numFrames times the true count.
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
      // filter[2] is L/R stage 1; filter2[2] is L/R stage 2, only run for
      // LP24 (a real 24dB/oct cascade, not the same TptSvf reused twice).
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

   void RenderVoice(Voice& v, const WaveTerrainDsp::BankData& bank, float baseFreq, int unison, float position,
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

         const float s = WaveTerrainDsp::SampleBank(bank, position, readPhase, inc);

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
            case 2: // LP24 - genuine two-stage cascade, not the same single
                    // TptSvf output reused (that was identical to LP12).
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
   ParamMailbox mMailbox;
   MeterRing mScopeRing;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;
   std::atomic<int> mActiveVoices{0};
   uint64_t mAgeCounter = 0;

   SampleSlotT<WaveTerrainDsp::BankData> mBankSlot;

   std::atomic<int> mUnison{1};
   std::atomic<int> mOctave{0};
   std::atomic<int> mSemi{0};
   std::atomic<int> mFilterType{1};
   std::atomic<float> mFilterAmount{0.0f};

   std::atomic<float> mAmpAdsr[4]{5.0f, 250.0f, 0.75f, 200.0f};
   std::atomic<float> mFilterAdsr[4]{5.0f, 300.0f, 0.4f, 250.0f};

   Voice mVoices[kMaxVoices];
   std::vector<float> mMonoScope;
};

// ---------------------------------------------------------------------------
// WaveTerrainNode (Main Thread GUI / Graph Representation)
// ---------------------------------------------------------------------------
WaveTerrainNode::WaveTerrainNode()
{
   mAudioNode = std::make_unique<AudioWaveTerrainNode>();
}

WaveTerrainNode::~WaveTerrainNode()
{
   if (mPreviewTex != 0) { glDeleteTextures(1, &mPreviewTex); mPreviewTex = 0; }
   if (mFbo != 0) { glDeleteFramebuffers(1, &mFbo); mFbo = 0; }
}

AudioNode* WaveTerrainNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWaveTerrainNode>();
   return mAudioNode.get();
}

int WaveTerrainNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int WaveTerrainNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void WaveTerrainNode::EnsurePreviewResources(int size)
{
   if (mFbo != 0 && mPreviewSize == size)
      return;

   if (mPreviewTex != 0) glDeleteTextures(1, &mPreviewTex);
   if (mFbo != 0) glDeleteFramebuffers(1, &mFbo);

   glGenTextures(1, &mPreviewTex);
   glBindTexture(GL_TEXTURE_2D, mPreviewTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

   glGenFramebuffers(1, &mFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mPreviewTex, 0);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);

   mPreviewSize = size;
   mPixels.assign((size_t)size * size * 4, 0);
}

void WaveTerrainNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWaveTerrainNode>();

   mAudioNode->DrainRetiredBanks();

   if (fabsf(scanSpeed) > 0.001f)
   {
      mCurrentRotation += scanSpeed * 0.05f;
      if (mCurrentRotation > 360.0f) mCurrentRotation -= 360.0f;
      if (mCurrentRotation < 0.0f) mCurrentRotation += 360.0f;
   }

   RenderPreview(frameId);
   mAudioNode->PushParams(*this);
}

void WaveTerrainNode::RenderPreview(int /*frameId*/)
{
   // No GL context in headless test/sweep runs (no window created) - all GL
   // calls below (glGenTextures/glGenFramebuffers inside
   // EnsurePreviewResources, the texture blit/glReadPixels, and the
   // viewport/framebuffer binds) are unsafe without one. But the orbit/
   // texture-baking pipeline (BuildBankFromPixels -> SwapBank) is pure CPU
   // work over `mPixels` - it doesn't need GL at all, and it's the only path
   // that ever pushes orbit-shape param changes (centerX/radiusX/orbitType/
   // rotation/...) to the audio thread. Bailing out of the whole function
   // (as an earlier version of this fix did) made those params permanently
   // unreachable in headless mode - not just untested, genuinely dead - so
   // only the strictly-GL parts are skipped below; the CPU bank rebuild
   // always runs off of whatever's currently in mPixels.
   const int size = 128;
   const bool hasGL = glfwGetCurrentContext() != nullptr;

   if (hasGL)
      EnsurePreviewResources(size);
   else if (mPixels.size() != (size_t)size * size * 4)
      FillProceduralDefault(mPixels, size); // see comment on FillProceduralDefault: a flat placeholder would make every orbit param sample the same silence

   const unsigned int srcTex = (hasGL && mTextureInput.GetSource()) ? mTextureInput.GetSource()->GetOutputTexture() : 0;
   const unsigned long long currentTexRev = mTextureInput.GetSource() ? mTextureInput.GetSource()->TextureRevision() : 0;
   const float totalRot = rotation + mCurrentRotation;

   const bool texChanged = hasGL && ((currentTexRev != mLastTexRev) || (mPixels.empty()));
   // Discrete edits (a shape param actually dragged) rebuild immediately.
   // Rotation drift and a live video source's texture revision are
   // *continuous* modulation - scanSpeed alone bumps mCurrentRotation every
   // single CookIfNeeded, and a connected video source bumps its revision at
   // frame rate - neither needs a full FFT/mip-pyramid rebuild (8 forward +
   // 80 inverse 1024-point FFTs) plus a synchronous glReadPixels on every
   // single frame; those are rate-limited below instead.
   // `rotation` itself is an explicit user drag and stays immediate;
   // `mCurrentRotation` (scanSpeed's continuous drift, added into totalRot)
   // is what gets rate-limited below.
   const bool rotationParamChanged = (rotation != mLastRotationParam);
   const bool discreteChanged = (centerX != mLastCenterX) || (centerY != mLastCenterY) ||
                                (radiusX != mLastRadiusX) || (radiusY != mLastRadiusY) ||
                                (ratioA != mLastRatioA) || (ratioB != mLastRatioB) ||
                                (phaseOffset != mLastPhaseOffset) ||
                                (orbitType != mLastOrbitType) || (channel != mLastChannel) ||
                                rotationParamChanged;
   const bool rotDriftChanged = !rotationParamChanged && (std::fabs(totalRot - mLastTotalRot) > 0.001f);

   bool shouldRebuild = discreteChanged;
   if (!shouldRebuild && (rotDriftChanged || texChanged))
   {
      constexpr double kMinContinuousRebuildInterval = 1.0 / 15.0; // ~15Hz cap
      const auto now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - mLastContinuousRebuild).count();
      if (elapsed >= kMinContinuousRebuildInterval)
      {
         shouldRebuild = true;
         mLastContinuousRebuild = now;
      }
   }

   if (shouldRebuild)
   {
      GLint prevFbo = 0;
      GLint prevViewport[4] = { 0, 0, 0, 0 };
      if (hasGL)
      {
         glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
         glGetIntegerv(GL_VIEWPORT, prevViewport);
         glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
         glViewport(0, 0, size, size);
      }

      if (texChanged)
      {
         if (srcTex != 0)
         {
            GLUtil::Fbo wrapper;
            wrapper.fbo = mFbo;
            wrapper.tex = mPreviewTex;
            wrapper.w = size;
            wrapper.h = size;

            static unsigned int sBlitProg = 0;
            if (sBlitProg == 0)
            {
               const char* fs =
                  "#version 150\n"
                  "in vec2 vUv;\n"
                  "out vec4 fragColor;\n"
                  "uniform sampler2D uSrc;\n"
                  "void main() {\n"
                  "   fragColor = texture(uSrc, vUv);\n"
                  "}\n";
               sBlitProg = GLUtil::CompileProgram(fs);
            }

            GLUtil::RunShaderPass(wrapper, sBlitProg, [srcTex]() {
               glActiveTexture(GL_TEXTURE0);
               glBindTexture(GL_TEXTURE_2D, srcTex);
               glUniform1i(glGetUniformLocation(sBlitProg, "uSrc"), 0);
            });
         }
         else
         {
            glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
         }

         glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
         mLastTexRev = currentTexRev;
      }

      const float totalRotRad = totalRot * (3.14159265f / 180.0f);
      WaveTerrainDsp::BankData bank;
      WaveTerrainDsp::BuildBankFromPixels(bank, mPixels.data(), size, size,
                                         orbitType, channel,
                                         centerX, centerY, radiusX, radiusY,
                                         ratioA, ratioB, phaseOffset, totalRotRad);

      mAudioNode->SwapBank(bank);

      if (hasGL)
      {
         glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
         glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
      }

      mLastCenterX = centerX;
      mLastCenterY = centerY;
      mLastRadiusX = radiusX;
      mLastRadiusY = radiusY;
      mLastRatioA = ratioA;
      mLastRatioB = ratioB;
      mLastPhaseOffset = phaseOffset;
      mLastTotalRot = totalRot;
      mLastRotationParam = rotation;
      mLastOrbitType = orbitType;
      mLastChannel = channel;
   }
}

void WaveTerrainNode::VisitParams(ParamVisitor& v)
{
   v.Int("orbitType", orbitType);
   v.Int("channel", channel);
   v.Float("centerX", centerX);
   v.Float("centerY", centerY);
   v.Float("radiusX", radiusX);
   v.Float("radiusY", radiusY);
   v.Float("ratioA", ratioA);
   v.Float("ratioB", ratioB);
   v.Float("phaseOffset", phaseOffset);
   v.Float("rotation", rotation);
   v.Float("scanSpeed", scanSpeed);
   v.Float("position", position);

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
