#include "PartialsNode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "audio/dsp/MolderDsp.h"
#include "core/AudioDecodeCache.h"
#include "platform/Platform.h"

namespace
{
   constexpr int kMaxPartials = PartialsModel::kMaxPartials;
   static_assert(kMaxPartials == MolderDsp::kMaxPartials, "the model is a straight copy of the analysis");

   enum SmoothedParam
   {
      kParamPitch = 0,
      kParamFormant,
      kParamPartials,
      kParamMorph,
      kParamSpeed,
      kParamNoise,
      kParamVolume,
   };

   // Partial frequencies and levels are recomputed this often and ramped
   // linearly in between: 0.7 ms at 48 kHz, far finer than the 5 ms
   // analysis hop they interpolate.
   constexpr int kChunk = 32;

   // Partials fade out between these fractions of the output rate, so
   // nothing above Nyquist is ever synthesised and nothing pops at the edge.
   constexpr float kAliasFadeStart = 0.42f;
   constexpr float kAliasFadeEnd = 0.47f;

   // Cosine over one turn, linearly interpolated (about -118 dB of error).
   struct CosTable
   {
      static constexpr int kSize = 2048;
      float v[kSize + 1];
      CosTable()
      {
         for (int i = 0; i <= kSize; i++)
            v[i] = (float)std::cos(2.0 * M_PI * (double)i / kSize);
      }
      static const CosTable& Instance()
      {
         static const CosTable s;
         return s;
      }
      float operator()(float turns) const // turns in [0, 1)
      {
         const float x = turns * (float)kSize;
         const int i = std::min((int)x, kSize - 1);
         const float f = x - (float)i;
         return v[i] + f * (v[i + 1] - v[i]);
      }
   };

   // One model's partial levels and frequency ratios at normalised time u.
   void FrameAt(const PartialsModel& m, float u, float* amp, float* ratio)
   {
      const float pos = std::clamp(u * (float)m.sourceLen / (float)std::max(1, m.hop), 0.0f,
                                   (float)std::max(0, m.numFrames - 1));
      const int f0i = (int)pos;
      const int f1i = std::min(f0i + 1, m.numFrames - 1);
      const float fr = pos - (float)f0i;
      const float* a0 = m.amp.data() + (size_t)f0i * kMaxPartials;
      const float* a1 = m.amp.data() + (size_t)f1i * kMaxPartials;
      const float* r0 = m.ratio.data() + (size_t)f0i * kMaxPartials;
      const float* r1 = m.ratio.data() + (size_t)f1i * kMaxPartials;
      for (int h = 0; h < kMaxPartials; h++)
      {
         if (h < m.numPartials)
         {
            amp[h] = a0[h] + (a1[h] - a0[h]) * fr;
            ratio[h] = r0[h] + (r1[h] - r0[h]) * fr;
         }
         else
         {
            amp[h] = 0.0f;
            ratio[h] = (float)(h + 1);
         }
      }
   }

   float ResidualAt(const PartialsModel& m, float u)
   {
      if (m.residual.empty())
         return 0.0f;
      const float pos = std::clamp(u, 0.0f, 1.0f) * (float)(m.residual.size() - 1);
      const int i0 = (int)pos;
      const int i1 = std::min(i0 + 1, (int)m.residual.size() - 1);
      return m.residual[(size_t)i0] + (m.residual[(size_t)i1] - m.residual[(size_t)i0]) * (pos - (float)i0);
   }

   struct PartialsVoice
   {
      bool active = false;
      bool ended = false;
      int note = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      float bend = 0.0f;
      uint64_t age = 0;
      float u = 0.0f; // 0..1 through the (morphed) sample
      int chunkLeft = 0;
      float phase[kMaxPartials] = {}; // turns
      float inc[kMaxPartials] = {};
      float dInc[kMaxPartials] = {};
      float amp[kMaxPartials] = {};
      float dAmp[kMaxPartials] = {};
      Envelope env;
   };
}

PartialsModel* PartialsModel::Build(const std::vector<float>& mono, double sr, const std::atomic<bool>* abort)
{
   if (mono.empty() || sr <= 0.0)
      return nullptr;
   MolderDsp::Analysis a;
   MolderDsp::Analyze(mono.data(), (int)mono.size(), sr, a, abort);
   if (!a.valid || a.numFrames <= 0)
      return nullptr;

   auto* m = new PartialsModel();
   m->sourceSR = a.sourceSR;
   m->sourceLen = a.sourceLen;
   m->hop = a.hop;
   m->numFrames = a.numFrames;
   m->numPartials = std::clamp(a.numPartials, 0, kMaxPartials);
   m->f0 = a.globalF0 > 1.0f ? a.globalF0 : 220.0f;
   m->amp = a.partialAmp;
   m->ratio.resize(m->amp.size());
   for (int k = 0; k < a.numFrames; k++)
   {
      const float frameF0 = a.frameF0[(size_t)k] > 1.0f ? a.frameF0[(size_t)k] : m->f0;
      for (int h = 0; h < kMaxPartials; h++)
      {
         const size_t i = (size_t)k * kMaxPartials + (size_t)h;
         // An untracked partial keeps its harmonic place, so the spectral
         // envelope still has a point there.
         const float f = a.partialFreq[i] > 0.0f ? a.partialFreq[i] : frameF0 * (float)(h + 1);
         m->ratio[i] = f / m->f0;
      }
   }
   m->residual.resize(a.residualSteady.size());
   for (size_t i = 0; i < m->residual.size(); i++)
      m->residual[i] = a.residualSteady[i] + (i < a.residualTransient.size() ? a.residualTransient[i] : 0.0f);
   return m;
}

// ------------------------------------------------------------- audio thread
class AudioPartialsNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = PartialsNode::kMaxVoices;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      for (auto& v : mVoices)
         v.env.SetSampleRate(mSampleRate);
      Reset();
   }

   void Reset() override
   {
      for (auto& v : mVoices)
      {
         v.active = false;
         v.env.NoteOff();
         v.env.ResetLevel();
      }
      mNextAge = 1;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void SetClipPitchOverride(float semitones) override { mClipPitch.store(semitones, std::memory_order_relaxed); }

   SampleSlotT<PartialsModel>& Slot(int which) { return mSlots[which]; }
   int ActiveVoices() const { return mActiveVoiceCount.load(std::memory_order_relaxed); }

   void PushParams(const PartialsNode& n)
   {
      mMailbox.Push(kParamPitch, n.pitch);
      mMailbox.Push(kParamFormant, n.formant);
      mMailbox.Push(kParamPartials, n.partials);
      mMailbox.Push(kParamMorph, n.morph);
      mMailbox.Push(kParamSpeed, n.speed);
      mMailbox.Push(kParamNoise, n.noise);
      mMailbox.Push(kParamVolume, n.volume);
      mRelease.store(n.release, std::memory_order_relaxed);
      mLoop.store(n.loop, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      if (output.numChannels <= 0 || output.numFrames <= 0)
         return;
      float* outL = output.channels[0];
      float* outR = (output.numChannels > 1 && output.channels[1]) ? output.channels[1] : nullptr;
      for (int ch = 2; ch < output.numChannels; ch++)
         if (output.channels[ch])
            std::fill_n(output.channels[ch], output.numFrames, 0.0f);

      // A new sample changes what every voice's phase and level mean.
      const bool swappedA = mSlots[0].SwapIn();
      const bool swappedB = mSlots[1].SwapIn();
      if (swappedA || swappedB)
         for (auto& v : mVoices)
            v.active = false;
      const PartialsModel* a = mSlots[0].Active();
      const PartialsModel* b = mSlots[1].Active();

      NoteEvent evts[64];
      const int numEvts = mNoteInbox ? mNoteInbox->Pop(mNoteCursor, evts, 64) : 0;
      int nextEvt = 0;

      const float release = mRelease.load(std::memory_order_relaxed);
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const float clipPitch = mClipPitch.load(std::memory_order_relaxed);
      const CosTable& cosT = CosTable::Instance();

      for (int i = 0; i < output.numFrames; i++)
      {
         while (nextEvt < numEvts && evts[nextEvt].frameOffset <= i)
            HandleEvent(evts[nextEvt++], release, a);

         const float pitch = mMailbox.SmoothedValue(kParamPitch);
         const float formant = mMailbox.SmoothedValue(kParamFormant);
         const float partials = mMailbox.SmoothedValue(kParamPartials);
         const float morph = b ? std::clamp(mMailbox.SmoothedValue(kParamMorph), 0.0f, 1.0f) : 0.0f;
         const float speed = std::clamp(mMailbox.SmoothedValue(kParamSpeed), 0.1f, 4.0f);
         const float noise = std::max(0.0f, mMailbox.SmoothedValue(kParamNoise));
         const float volume = mMailbox.SmoothedValue(kParamVolume);

         float y = 0.0f;
         if (a)
         {
            const float durA = a->DurationSec();
            const float dur = b ? durA + (b->DurationSec() - durA) * morph : durA;
            const float du = dur > 0.0f ? speed / ((float)mSampleRate * dur) : 1.0f;
            for (auto& v : mVoices)
            {
               if (!v.active)
                  continue;
               if (v.chunkLeft <= 0)
               {
                  PlanChunk(v, *a, b, morph, pitch + clipPitch, formant, partials);
                  v.chunkLeft = kChunk;
               }
               v.chunkLeft--;

               float s = 0.0f;
               for (int h = 0; h < kMaxPartials; h++)
               {
                  if (v.amp[h] == 0.0f && v.dAmp[h] == 0.0f)
                     continue;
                  s += v.amp[h] * cosT(v.phase[h]);
                  v.phase[h] += v.inc[h];
                  if (v.phase[h] >= 1.0f)
                     v.phase[h] -= 1.0f;
                  v.inc[h] += v.dInc[h];
                  v.amp[h] += v.dAmp[h];
               }
               float res = ResidualAt(*a, v.u);
               if (b)
                  res += (ResidualAt(*b, v.u) - res) * morph;
               s += res * noise;
               y += s * v.env.Process() * v.velocity;

               v.u += du;
               if (v.u >= 1.0f)
               {
                  if (loop)
                     v.u -= std::floor(v.u);
                  else
                  {
                     v.u = 1.0f;
                     if (!v.ended)
                     {
                        v.ended = true;
                        v.env.NoteOff();
                     }
                  }
               }
               if (!v.env.IsActive())
                  v.active = false;
            }
         }

         y *= volume;
         if (!std::isfinite(y))
            y = 0.0f;
         y = std::clamp(y, -4.0f, 4.0f);
         outL[i] = y;
         if (outR)
            outR[i] = y;
      }
      while (nextEvt < numEvts)
         HandleEvent(evts[nextEvt++], release, a);

      int count = 0;
      for (const auto& v : mVoices)
         count += v.active ? 1 : 0;
      mActiveVoiceCount.store(count, std::memory_order_relaxed);
   }

private:
   // Sets every partial's frequency and level ramp for the next chunk.
   void PlanChunk(PartialsVoice& v, const PartialsModel& a, const PartialsModel* b, float morph, float pitch,
                  float formant, float partials)
   {
      float srcAmp[kMaxPartials], ratio[kMaxPartials];
      FrameAt(a, v.u, srcAmp, ratio);
      float f0 = a.f0;
      if (b && morph > 0.0f)
      {
         float ampB[kMaxPartials], ratioB[kMaxPartials];
         FrameAt(*b, v.u, ampB, ratioB);
         for (int h = 0; h < kMaxPartials; h++)
         {
            srcAmp[h] += (ampB[h] - srcAmp[h]) * morph;
            ratio[h] += (ratioB[h] - ratio[h]) * morph;
         }
         f0 = a.f0 * std::pow(b->f0 / a.f0, morph);
      }

      // Where each partial sat in the (morphed) source: the spectral
      // envelope's sample points.
      float srcFreq[kMaxPartials];
      for (int h = 0; h < kMaxPartials; h++)
         srcFreq[h] = std::max(1.0f, ratio[h] * f0);

      const float noteHz = 440.0f * std::exp2(((float)v.note - 69.0f + v.bend + pitch) / 12.0f);
      const float formantRatio = std::exp2(formant / 12.0f);
      const float sr = (float)mSampleRate;
      const float invChunk = 1.0f / (float)kChunk;
      int j = 0;
      for (int h = 0; h < kMaxPartials; h++)
      {
         const float freq = ratio[h] * noteHz;
         // The level the source's envelope had at this frequency (shifted
         // by formant), not this partial's own: transposing keeps the
         // formants where they were. At the root the two are the same.
         const float q = freq / formantRatio;
         float level;
         if (q <= srcFreq[0])
            level = srcAmp[0];
         else if (q >= srcFreq[kMaxPartials - 1])
         {
            const float k = srcFreq[kMaxPartials - 1] / q;
            level = srcAmp[kMaxPartials - 1] * k * k; // -12 dB per octave past the last partial
         }
         else
         {
            while (j + 1 < kMaxPartials - 1 && srcFreq[j + 1] <= q)
               j++;
            while (j > 0 && srcFreq[j] > q)
               j--;
            const float span = srcFreq[j + 1] - srcFreq[j];
            const float t = span > 1e-6f ? std::clamp((q - srcFreq[j]) / span, 0.0f, 1.0f) : 0.0f;
            level = srcAmp[j] + (srcAmp[j + 1] - srcAmp[j]) * t;
         }
         level *= std::clamp(partials - (float)h, 0.0f, 1.0f);
         level *= std::clamp((kAliasFadeEnd * sr - freq) / ((kAliasFadeEnd - kAliasFadeStart) * sr), 0.0f, 1.0f);
         if (level < 1e-7f)
            level = 0.0f;

         const float targetInc = std::min(freq / sr, 0.49f);
         if (v.amp[h] == 0.0f && v.dAmp[h] == 0.0f)
            v.inc[h] = targetInc; // silent partial: no glide into its first chunk
         v.dInc[h] = (targetInc - v.inc[h]) * invChunk;
         v.dAmp[h] = (level - v.amp[h]) * invChunk;
         if (level == 0.0f && v.amp[h] == 0.0f)
            v.dAmp[h] = 0.0f;
      }
   }

   void HandleEvent(const NoteEvent& e, float release, const PartialsModel* a)
   {
      if (e.bendUpdate)
      {
         for (auto& v : mVoices)
            if (v.active && v.voiceId == e.voiceId)
               v.bend = e.bendSemitones;
         return;
      }
      if (e.isNoteOn && e.velocity > 0.0f)
      {
         if (a)
            NoteOn(e, release);
         return;
      }
      for (auto& v : mVoices)
         if (v.active && v.note == e.note && (v.voiceId == e.voiceId || e.voiceId == 0))
         {
            v.env.SetADSR(2.0f, 1.0f, 1.0f, release);
            v.env.NoteOff();
         }
   }

   void NoteOn(const NoteEvent& e, float release)
   {
      // Same note retriggers its voice, else a free one, else the oldest.
      int slot = -1;
      for (int k = 0; k < kMaxVoices && slot < 0; k++)
         if (mVoices[k].active && mVoices[k].note == e.note)
            slot = k;
      for (int k = 0; k < kMaxVoices && slot < 0; k++)
         if (!mVoices[k].active)
            slot = k;
      if (slot < 0)
      {
         uint64_t oldest = UINT64_MAX;
         for (int k = 0; k < kMaxVoices; k++)
            if (mVoices[k].age < oldest)
            {
               oldest = mVoices[k].age;
               slot = k;
            }
      }
      PartialsVoice& v = mVoices[slot];
      // Every note starts the sample from the top, from silence: a
      // resynthesised attack is the sample's own, so it is never smeared
      // into whatever the voice was playing.
      v.active = true;
      v.ended = false;
      v.note = e.note;
      v.voiceId = e.voiceId;
      v.velocity = std::clamp(e.velocity, 0.0f, 1.0f);
      v.bend = e.bendSemitones;
      v.age = mNextAge++;
      v.u = 0.0f;
      v.chunkLeft = 0;
      for (int h = 0; h < kMaxPartials; h++)
      {
         v.phase[h] = 0.0f;
         v.amp[h] = v.dAmp[h] = 0.0f;
         v.inc[h] = v.dInc[h] = 0.0f;
      }
      v.env.SetADSR(2.0f, 1.0f, 1.0f, release);
      v.env.ResetLevel();
      v.env.NoteOn();
   }

   double mSampleRate = 44100.0;
   PartialsVoice mVoices[kMaxVoices];
   uint64_t mNextAge = 1;
   SampleSlotT<PartialsModel> mSlots[2];

   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   std::atomic<float> mRelease{ 300.0f };
   std::atomic<bool> mLoop{ false };
   std::atomic<float> mClipPitch{ 0.0f };
   std::atomic<int> mActiveVoiceCount{ 0 };
};

// -------------------------------------------------------------- main thread
PartialsNode::PartialsNode() = default;

PartialsNode::~PartialsNode()
{
   for (auto& l : mLoaders)
   {
      l.abort.store(true, std::memory_order_release);
      if (l.thread.joinable())
         l.thread.join();
      delete l.result;
   }
}

AudioNode* PartialsNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPartialsNode>();
   return mAudioNode.get();
}

int PartialsNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

bool PartialsNode::IsAnalyzing() const
{
   return mLoaders[0].working.load(std::memory_order_relaxed) || mLoaders[1].working.load(std::memory_order_relaxed);
}

void PartialsNode::LoadMono(int which, std::vector<float> mono, double sr)
{
   Loader& l = mLoaders[which];
   // A second load while one is analysing waits for it: loads are rare
   // and a sample is seconds long at most, so this never stalls long.
   l.abort.store(true, std::memory_order_release);
   if (l.thread.joinable())
      l.thread.join();
   l.abort.store(false, std::memory_order_release);
   delete l.result;
   l.result = nullptr;
   l.ready.store(false, std::memory_order_relaxed);
   l.working.store(true, std::memory_order_release);
   mStatus = "analyzing...";
   auto analyse = [&l, mono = std::move(mono), sr]() {
      PartialsModel* m = PartialsModel::Build(mono, sr, &l.abort);
      if (l.abort.load(std::memory_order_relaxed))
      {
         delete m;
         l.working.store(false, std::memory_order_release);
         return;
      }
      l.result = m;
      l.ready.store(true, std::memory_order_release);
      l.working.store(false, std::memory_order_release);
   };
#ifdef __EMSCRIPTEN__
   analyse(); // the website build has no threads: analyse inline
#else
   l.thread = std::thread(std::move(analyse));
#endif
}

bool PartialsNode::LoadFile(int which, const std::string& path)
{
   if (path.empty())
      return false;
   Platform::SampleBuffer decoded;
   std::string error;
   if (!AudioDecodeCache::DecodeCached(path, decoded, error))
   {
      mStatus = error.empty() ? "failed to load" : error;
      return false;
   }
   std::vector<float> mono((size_t)decoded.numFrames, 0.0f);
   const int channels = std::max(1, decoded.channels);
   for (int ch = 0; ch < channels; ++ch)
   {
      const float* chData = decoded.channelData.data() + (size_t)ch * decoded.numFrames;
      for (int i = 0; i < decoded.numFrames; ++i)
         mono[(size_t)i] += chData[i] / (float)channels;
   }
   (which == 0 ? pathA : pathB) = path;
   const size_t slash = path.find_last_of("/\\");
   mLoaders[which].name = slash == std::string::npos ? path : path.substr(slash + 1);
   LoadMono(which, std::move(mono), decoded.sampleRate);
   return true;
}

void PartialsNode::ReloadFromPath()
{
   if (!pathA.empty())
      LoadFile(0, pathA);
   if (!pathB.empty())
      LoadFile(1, pathB);
}

void PartialsNode::WaitIdle()
{
   for (int i = 0; i < 20000 && IsAnalyzing(); i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   JoinIfDone(0);
   JoinIfDone(1);
}

void PartialsNode::RebuildBars(const PartialsModel& m)
{
   // The loudest frame stands for the sample on the card.
   int best = 0;
   float bestSum = -1.0f;
   for (int k = 0; k < m.numFrames; k++)
   {
      float sum = 0.0f;
      for (int h = 0; h < m.numPartials; h++)
         sum += m.amp[(size_t)k * kMaxPartials + (size_t)h];
      if (sum > bestSum)
      {
         bestSum = sum;
         best = k;
      }
   }
   barCount = m.numPartials;
   for (int h = 0; h < kBarCount; h++)
      bars[h] = h < m.numPartials ? m.amp[(size_t)best * kMaxPartials + (size_t)h] : 0.0f;
}

void PartialsNode::JoinIfDone(int which)
{
   Loader& l = mLoaders[which];
   if (!l.ready.load(std::memory_order_acquire))
      return;
   l.ready.store(false, std::memory_order_relaxed);
   if (l.thread.joinable())
      l.thread.join();
   PartialsModel* m = l.result;
   l.result = nullptr;
   if (m == nullptr)
   {
      mStatus = "analysis failed";
      return;
   }
   l.f0 = m->f0;
   if (which == 0)
      RebuildBars(*m);
   char stat[96];
   snprintf(stat, sizeof(stat), "f0 %.1f Hz  -  %.2fs", m->f0, m->DurationSec());
   mStatus = stat;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPartialsNode>();
   mAudioNode->Slot(which).Push(m);
}

void PartialsNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPartialsNode>();
   for (int w = 0; w < 2; w++)
   {
      mAudioNode->Slot(w).DrainRetired();
      JoinIfDone(w);
   }
   mAudioNode->PushParams(*this);
}

void PartialsNode::SweepPrepare()
{
   // A short, slightly noisy saw for the main sample (so formant, partials
   // and noise all have something to act on, and loop reaches the end at
   // speed 4) and an odd-harmonic square a fifth up as the morph target.
   const double sr = 48000.0;
   auto tone = [&](int len, float hz, bool oddOnly, float noiseAmp) {
      std::vector<float> x((size_t)len, 0.0f);
      uint32_t rng = 0x12345678u;
      for (int i = 0; i < len; i++)
      {
         float s = 0.0f;
         for (int h = 1; h <= 16; h++)
            if (!oddOnly || (h & 1))
               s += 0.4f / (float)h * std::sin(2.0f * (float)M_PI * hz * (float)h * (float)i / (float)sr);
         rng = rng * 1664525u + 1013904223u;
         s += noiseAmp * ((float)(rng >> 8) / 8388608.0f - 1.0f);
         x[(size_t)i] = s;
      }
      return x;
   };
   LoadMono(0, tone(12000, 220.0f, false, 0.03f), sr);
   LoadMono(1, tone(12000, 330.0f, true, 0.0f), sr);
   WaitIdle();
}

void PartialsNode::VisitParams(ParamVisitor& v)
{
   v.Text("path", pathA);
   v.Text("pathB", pathB);
   v.Float("pitch", pitch);
   v.Float("formant", formant);
   v.Float("partials", partials);
   v.Float("morph", morph);
   v.Float("speed", speed);
   v.Float("noise", noise);
   v.Float("release", release);
   v.Float("volume", volume);
   v.Bool("loop", loop);
}
