#include "MpcNode.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/NoteEventQueue.h"
#include "audio/PassFade.h"
#include "audio/SampleSlot.h"
#include "core/AudioDecodeCache.h"
#include "core/Transport.h"
#include "platform/Platform.h"

namespace
{
   constexpr int kReleaseFrames = 96; // ~2 ms fade when a gate/loop stops
   constexpr int kDeclickFrames = 64; // ~1.3 ms: smooths a voice that is cut and restarted
   constexpr int kCmdCapacity = 256;
   constexpr int kMaxEvents = 128;

   struct PadCommand
   {
      int pad = 0;
      bool down = false;
      float velocity = 1.0f;
   };

   struct PadEventAt
   {
      int offset = 0;
      int pad = 0;
      bool down = false;
      float velocity = 1.0f;
      bool retrig = false; // synced loop: restart the pass on the grid line
   };
}

class AudioMpcNode : public AudioNode
{
public:
   AudioMpcNode()
   {
      // Sized here as well as in PrepareToPlay: a graph rebuild can run a block
      // on a node before its PrepareToPlay.
      mPadOut.assign((size_t)MpcNode::kPads * 2 * kAudioMaxBlockFrames, 0.0f);
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         mMode[p].store(MpcNode::kOneShot, std::memory_order_relaxed);
         mVolume[p].store(0.8f, std::memory_order_relaxed);
         mPitch[p].store(0.0f, std::memory_order_relaxed);
         mPan[p].store(0.0f, std::memory_order_relaxed);
         mSpeed[p].store(1.0f, std::memory_order_relaxed);
         mFine[p].store(0.0f, std::memory_order_relaxed);
         mFadeIn[p].store(3.0f, std::memory_order_relaxed);
         mFadeOut[p].store(3.0f, std::memory_order_relaxed);
         mSync[p].store(MpcNode::kFree, std::memory_order_relaxed);
         mDiv[p].store((int)MusicTime::kQuarter, std::memory_order_relaxed);
      }
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
      mPadOut.assign((size_t)MpcNode::kPads * 2 * kAudioMaxBlockFrames, 0.0f);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   // ---- main thread -----------------------------------------------------
   void PushBuffer(int pad, Platform::SampleBuffer* buffer) { mSlots[pad].Push(buffer); }
   void DrainRetired()
   {
      for (auto& slot : mSlots)
         slot.DrainRetired();
   }

   void PushEvent(int pad, bool down, float velocity)
   {
      const uint32_t tail = mCmdTail.load(std::memory_order_relaxed);
      const uint32_t next = (tail + 1) % kCmdCapacity;
      if (next == mCmdHead.load(std::memory_order_acquire))
         return;
      mCmds[tail] = { pad, down, velocity };
      mCmdTail.store(next, std::memory_order_release);
   }

   void PushParams(const MpcNode& n)
   {
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         mMode[p].store(std::clamp(n.padMode[p], 0, 2), std::memory_order_relaxed);
         mVolume[p].store(std::clamp(n.padVolume[p], 0.0f, 1.0f), std::memory_order_relaxed);
         mPitch[p].store(std::clamp(n.padPitch[p], -24.0f, 24.0f), std::memory_order_relaxed);
         mPan[p].store(std::clamp(n.padPan[p], -1.0f, 1.0f), std::memory_order_relaxed);
         mSpeed[p].store(std::clamp(n.padSpeed[p], -2.0f, 2.0f), std::memory_order_relaxed);
         mFine[p].store(std::clamp(n.padFine[p], -50.0f, 50.0f), std::memory_order_relaxed);
         mFadeIn[p].store(std::clamp(n.padFadeIn[p], 0.0f, 250.0f), std::memory_order_relaxed);
         mFadeOut[p].store(std::clamp(n.padFadeOut[p], 0.0f, 250.0f), std::memory_order_relaxed);
         mSync[p].store(std::clamp(n.padSync[p], 0, 1), std::memory_order_relaxed);
         mDiv[p].store(std::clamp(n.padDiv[p], 0, (int)MusicTime::kNumRateDivisions - 1), std::memory_order_relaxed);
      }
   }
   // Sweep hook: any note starts every pad, so all 16 pads' params are audible
   // through the one held note the sweep drives.
   void SetTestAllPads(bool on) { mTestAllPads.store(on, std::memory_order_relaxed); }

   unsigned int PlayingMask() const { return mPlayingMask.load(std::memory_order_relaxed); }
   float Peak() const { return mPeak.exchange(0.0f, std::memory_order_relaxed); }

   // ---- audio thread ----------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = std::min(output.numFrames, kAudioMaxBlockFrames);
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         if (mSlots[p].SwapIn())
         {
            mActive[p] = mSlots[p].Active();
            mVoice[p] = Voice();
            mLoopOn[p] = false;
            mPending[p].active = false;
         }
      }

      // This block's raw events: UI/CV at offset 0, notes at their own frame
      // offsets. Kept sorted by offset (insertion sort, tiny N). Synced pads'
      // hits are then moved onto the grid (Schedule below) into `events`.
      PadEventAt raw[kMaxEvents];
      int rawCount = 0;
      auto addRaw = [&](const PadEventAt& e)
      {
         if (rawCount >= kMaxEvents)
            return;
         int k = rawCount++;
         while (k > 0 && raw[k - 1].offset > e.offset)
         {
            raw[k] = raw[k - 1];
            k--;
         }
         raw[k] = e;
      };
      for (;;)
      {
         const uint32_t head = mCmdHead.load(std::memory_order_relaxed);
         if (head == mCmdTail.load(std::memory_order_acquire))
            break;
         const PadCommand c = mCmds[head];
         mCmdHead.store((head + 1) % kCmdCapacity, std::memory_order_release);
         addRaw({ 0, c.pad, c.down, c.velocity });
      }
      if (mNoteInbox != nullptr)
      {
         const int base = MpcNode::kFirstNote;
         const bool all = mTestAllPads.load(std::memory_order_relaxed);
         NoteEvent notes[64];
         int n = 0;
         while ((n = mNoteInbox->Pop(mNoteCursor, notes, 64)) > 0)
         {
            for (int i = 0; i < n; i++)
            {
               if (notes[i].bendUpdate)
                  continue;
               const int off = std::clamp(notes[i].frameOffset, 0, std::max(0, numFrames - 1));
               if (all)
               {
                  for (int q = 0; q < MpcNode::kPads; q++)
                     addRaw({ off, q, notes[i].isNoteOn, notes[i].velocity });
                  continue;
               }
               const int pad = notes[i].note - base;
               if (pad < 0 || pad >= MpcNode::kPads)
                  continue;
               addRaw({ off, pad, notes[i].isNoteOn, notes[i].velocity });
            }
         }
      }

      for (int p = 0; p < MpcNode::kPads; p++)
      {
         std::fill(PadWrite(p, 0), PadWrite(p, 0) + numFrames, 0.0f);
         std::fill(PadWrite(p, 1), PadWrite(p, 1) + numFrames, 0.0f);
      }

      PadEventAt events[kMaxEvents];
      int eventCount = 0;
      Schedule(raw, rawCount, numFrames, events, eventCount);

      int cursor = 0;
      for (int e = 0; e < eventCount; e++)
      {
         const int at = std::clamp(events[e].offset, cursor, numFrames);
         Render(cursor, at);
         Apply(events[e]);
         cursor = at;
      }
      Render(cursor, numFrames);

      float peak = 0.0f;
      for (int i = 0; i < numFrames; i++)
      {
         float l = 0.0f, r = 0.0f;
         for (int p = 0; p < MpcNode::kPads; p++)
         {
            l += PadWrite(p, 0)[i];
            r += PadWrite(p, 1)[i];
         }
         if (output.numChannels > 0)
            output.channels[0][i] = l;
         if (output.numChannels > 1)
            output.channels[1][i] = r;
         for (int ch = 2; ch < output.numChannels; ch++)
            output.channels[ch][i] = l;
         peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
      }
      for (int ch = 0; ch < output.numChannels; ch++)
         for (int i = numFrames; i < output.numFrames; i++)
            output.channels[ch][i] = 0.0f;

      unsigned int mask = 0;
      for (int p = 0; p < MpcNode::kPads; p++)
         if (mVoice[p].active)
            mask |= 1u << p;
      mPlayingMask.store(mask, std::memory_order_relaxed);
      float prev = mPeak.load(std::memory_order_relaxed);
      if (peak > prev)
         mPeak.store(peak, std::memory_order_relaxed);
   }

private:
   struct Voice
   {
      bool active = false;
      bool looping = false;
      double pos = 0.0;
      bool reverse = false; // direction the pass was started in
      float gain = 1.0f;
      int release = -1; // >= 0: frames left in the stop fade
      // Declick: the last sample this voice emitted, and the residual left when
      // it was cut and restarted (a retrigger); the residual fades to zero over
      // kDeclickFrames so a restart never steps.
      float lastL = 0.0f, lastR = 0.0f;
      float resL = 0.0f, resR = 0.0f;
      int resN = 0;
   };

   // A synced hit that has been latched and is waiting for its grid line.
   struct Pending
   {
      bool active = false;
      double tgt = 0.0; // absolute beat of the line it fires on
      float vel = 1.0f;
      int fire = -1; // frame in the current block, or -1 (a later block)
   };

   // ---- transport (audio thread) ----------------------------------------
   bool Syncing(int p) const
   {
      return mPlaying && mBpf > 0.0 && mSync[p].load(std::memory_order_relaxed) == MpcNode::kSynced;
   }
   double Grid(int p) const
   {
      const int d = std::clamp(mDiv[p].load(std::memory_order_relaxed), 0, (int)MusicTime::kNumRateDivisions - 1);
      return std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)d));
   }
   // Frame of this block a grid line at beat `tgt` falls on: 0 when it is already
   // behind the block start, -1 when it is in a later block.
   int FireFrame(double tgt, int numFrames) const
   {
      const double f = std::ceil((tgt - mBlockStart) / mBpf - 1e-6);
      if (f < 0.0)
         return 0;
      return f < (double)numFrames ? (int)f : -1;
   }

   // Turns this block's raw pad events into the events Render/Apply run:
   // a synced pad's hit is latched to the next grid line of the transport and
   // released into the list at that exact frame; loop pads get a re-trigger on
   // every line. Free pads pass straight through.
   void Schedule(const PadEventAt* raw, int rawCount, int numFrames, PadEventAt* out, int& outCount)
   {
      outCount = 0;
      auto push = [&](const PadEventAt& e)
      {
         if (outCount >= kMaxEvents)
            return;
         int k = outCount++;
         while (k > 0 && out[k - 1].offset > e.offset)
         {
            out[k] = out[k - 1];
            k--;
         }
         out[k] = e;
      };
      Transport& tr = Transport::Instance();
      mPlaying = tr.IsPlaying();
      mBpf = std::max(1.0f, tr.Tempo()) / 60.0 / std::max(1.0, mSampleRate);
      mBlockStart = tr.BlockStartBeats();

      int startedAt[MpcNode::kPads];
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         startedAt[p] = -1;
         const int mode = mMode[p].load(std::memory_order_relaxed);
         if (mLoopOn[p] && mode != MpcNode::kLoopToggle)
         {
            mLoopOn[p] = false; // the mode moved away from loop: stop it
            Release(p);
         }
         else if (mLoopOn[p] && !mPlaying && !mVoice[p].active &&
                  mSync[p].load(std::memory_order_relaxed) == MpcNode::kSynced)
            Start(p, mVel[p], true); // transport stopped: a synced loop plays as a plain loop
         Pending& pd = mPending[p];
         if (!pd.active)
            continue;
         if (!Syncing(p) || pd.tgt < mBlockStart - 1e-6)
            pd.fire = 0; // transport stopped / mode freed / position jumped past it: fire now
         else
         {
            const double g = Grid(p);
            if (pd.tgt > mBlockStart + g + 1e-6) // position jumped back: re-latch
               pd.tgt = std::ceil(mBlockStart / g - 1e-6) * g;
            pd.fire = FireFrame(pd.tgt, numFrames);
         }
      }

      for (int i = 0; i < rawCount; i++)
      {
         const PadEventAt& e = raw[i];
         const int p = e.pad;
         if (p < 0 || p >= MpcNode::kPads)
            continue;
         Pending& pd = mPending[p];
         if (!e.down)
         {
            // Gate mode: a release before the line cancels the latched hit (a
            // one shot or loop hit is a trigger, its release means nothing).
            if (pd.active && mMode[p].load(std::memory_order_relaxed) == MpcNode::kGate &&
                (pd.fire < 0 || pd.fire > e.offset))
               pd.active = false;
            push(e);
            continue;
         }
         if (!Syncing(p))
         {
            pd.active = false;
            push(e);
            continue;
         }
         if (pd.active)
         {
            pd.vel = e.velocity; // already waiting: keep the earlier line
            continue;
         }
         const double g = Grid(p);
         const double beat = mBlockStart + (double)e.offset * mBpf;
         pd.active = true;
         pd.tgt = std::ceil(beat / g - 1e-6) * g;
         pd.vel = e.velocity;
         pd.fire = FireFrame(pd.tgt, numFrames);
         if (pd.fire >= 0)
            pd.fire = std::max(pd.fire, e.offset);
      }
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         Pending& pd = mPending[p];
         if (pd.active && pd.fire >= 0)
         {
            push({ pd.fire, p, true, pd.vel });
            startedAt[p] = pd.fire;
            pd.active = false;
         }
      }
      // Rate-locked loops: a re-trigger on every line while the pad is on.
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         if (!Syncing(p) || mMode[p].load(std::memory_order_relaxed) != MpcNode::kLoopToggle)
            continue;
         if (!mLoopOn[p] && startedAt[p] < 0)
            continue;
         const double g = Grid(p);
         double n = std::ceil(mBlockStart / g - 1e-6);
         for (int guard = 0; guard < 64; guard++, n += 1.0)
         {
            int f = (int)std::ceil((n * g - mBlockStart) / mBpf - 1e-6);
            if (f < 0)
               f = 0;
            if (f >= numFrames)
               break;
            if (f == startedAt[p])
               continue;
            PadEventAt r;
            r.offset = f;
            r.pad = p;
            r.retrig = true;
            push(r);
         }
      }
   }

   float* PadWrite(int pad, int ch)
   {
      return mPadOut.data() + ((size_t)pad * 2 + (size_t)ch) * kAudioMaxBlockFrames;
   }

   void Start(int p, float velocity, bool looping)
   {
      Voice& v = mVoice[p];
      // A restart of a sounding voice would step; carry its last sample into a
      // short decaying residual instead.
      const bool cut = v.active;
      v.resL = cut ? v.lastL : 0.0f;
      v.resR = cut ? v.lastR : 0.0f;
      v.resN = cut ? kDeclickFrames : 0;
      mVel[p] = velocity;
      v.active = mActive[p] != nullptr && mActive[p]->numFrames > 1;
      v.looping = looping;
      const float speed = mSpeed[p].load(std::memory_order_relaxed);
      v.reverse = speed < 0.0f;
      v.pos = (v.reverse && mActive[p] != nullptr) ? (double)(mActive[p]->numFrames - 1) - 1e-3 : 0.0;
      v.release = -1;
      v.gain = std::clamp(velocity, 0.0f, 1.0f);
   }

   void Release(int p)
   {
      if (mVoice[p].active && mVoice[p].release < 0)
         mVoice[p].release = kReleaseFrames;
   }

   void Apply(const PadEventAt& e)
   {
      const int p = e.pad;
      if (p < 0 || p >= MpcNode::kPads)
         return;
      const int mode = mMode[p].load(std::memory_order_relaxed);
      if (e.retrig)
      {
         if (mLoopOn[p] && mode == MpcNode::kLoopToggle)
            Start(p, mVel[p], false);
         return;
      }
      if (mode == MpcNode::kOneShot)
      {
         if (e.down)
            Start(p, e.velocity, false);
      }
      else if (mode == MpcNode::kGate)
      {
         if (e.down)
            Start(p, e.velocity, false);
         else
            Release(p);
      }
      else // loop toggle
      {
         if (!e.down)
            return;
         if (mLoopOn[p])
         {
            mLoopOn[p] = false;
            Release(p);
         }
         else
         {
            mLoopOn[p] = true;
            // Synced: one pass per division (Schedule re-triggers on each line).
            Start(p, e.velocity, !Syncing(p));
         }
      }
   }

   void Render(int from, int to)
   {
      if (to <= from)
         return;
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         Voice& v = mVoice[p];
         const Platform::SampleBuffer* buf = mActive[p];
         if (!v.active || buf == nullptr || buf->numFrames <= 1 || buf->channels <= 0)
         {
            v.active = false;
            continue;
         }
         const int frames = buf->numFrames;
         const float* src0 = buf->channelData.data();
         const float* src1 = buf->channels > 1 ? src0 + frames : src0;
         const double srcRate = buf->sampleRate > 0.0 ? buf->sampleRate : mSampleRate;
         const float speed = mSpeed[p].load(std::memory_order_relaxed);
         const double semis = (double)mPitch[p].load(std::memory_order_relaxed) +
                              (double)mFine[p].load(std::memory_order_relaxed) / 100.0;
         const double rate = (srcRate / mSampleRate) * std::pow(2.0, semis / 12.0) * (double)speed;
         const double step = rate;
         const float dirSign = step < 0.0 ? -1.0f : 1.0f;
         const double lastPos = (double)(frames - 1);
         float panL = 1.0f, panR = 1.0f;
         DspMath::EqualPowerPan(mPan[p].load(std::memory_order_relaxed), panL, panR);
         const float vol = mVolume[p].load(std::memory_order_relaxed) * v.gain;
         panL *= vol * (float)M_SQRT2;
         panR *= vol * (float)M_SQRT2;
         float* outL = PadWrite(p, 0);
         float* outR = PadWrite(p, 1);
         const float fadeInMs = mFadeIn[p].load(std::memory_order_relaxed);
         const float fadeOutMs = mFadeOut[p].load(std::memory_order_relaxed);
         for (int i = from; i < to; i++)
         {
            if (v.pos >= lastPos || v.pos < 0.0)
            {
               if (v.looping)
               {
                  v.pos = std::fmod(v.pos, lastPos);
                  if (v.pos < 0.0)
                     v.pos += lastPos;
               }
               else
               {
                  v.active = false;
                  break;
               }
            }
            const int idx = std::clamp((int)v.pos, 0, frames - 2);
            const float frac = (float)(v.pos - (double)idx);
            const float a = src0[idx] + (src0[idx + 1] - src0[idx]) * frac;
            const float b = src1[idx] + (src1[idx + 1] - src1[idx]) * frac;
            float env = PassFade::Gain(v.pos, 0.0, lastPos, dirSign, fadeInMs, fadeOutMs,
                                       (float)std::fabs(step), mSampleRate);
            if (v.release >= 0)
            {
               env *= (float)v.release / (float)kReleaseFrames;
               if (--v.release < 0)
               {
                  v.active = false;
                  v.pos = 0.0;
                  break;
               }
            }
            float sL = a * panL * env;
            float sR = b * panR * env;
            if (v.resN > 0)
            {
               const float k = (float)v.resN / (float)kDeclickFrames;
               sL += v.resL * k;
               sR += v.resR * k;
               v.resN--;
            }
            v.lastL = sL;
            v.lastR = sR;
            outL[i] += sL;
            outR[i] += sR;
            v.pos += step;
         }
      }
   }

   double mSampleRate = 48000.0;
   SampleSlot mSlots[MpcNode::kPads];
   const Platform::SampleBuffer* mActive[MpcNode::kPads] = {};
   Voice mVoice[MpcNode::kPads];
   std::vector<float> mPadOut;
   bool mLoopOn[MpcNode::kPads] = {}; // loop-mode toggle state (audio thread)
   float mVel[MpcNode::kPads] = {};
   Pending mPending[MpcNode::kPads];
   bool mPlaying = false; // transport state and grid clock, refreshed per block
   double mBpf = 0.0;        // beats per frame
   double mBlockStart = 0.0; // beat position of the block's first frame

   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   PadCommand mCmds[kCmdCapacity];
   std::atomic<uint32_t> mCmdHead { 0 };
   std::atomic<uint32_t> mCmdTail { 0 };

   std::atomic<int> mMode[MpcNode::kPads];
   std::atomic<float> mVolume[MpcNode::kPads];
   std::atomic<float> mPitch[MpcNode::kPads];
   std::atomic<float> mPan[MpcNode::kPads];
   std::atomic<float> mSpeed[MpcNode::kPads];
   std::atomic<float> mFine[MpcNode::kPads];
   std::atomic<float> mFadeIn[MpcNode::kPads];
   std::atomic<float> mFadeOut[MpcNode::kPads];
   std::atomic<int> mSync[MpcNode::kPads];
   std::atomic<int> mDiv[MpcNode::kPads];
   std::atomic<bool> mTestAllPads { false };

   std::atomic<unsigned int> mPlayingMask { 0 };
   mutable std::atomic<float> mPeak { 0.0f };
};

// ------------------------------------------------------------------ MpcNode
MpcNode::MpcNode() : mAudioNode(std::make_unique<AudioMpcNode>())
{
   for (int p = 0; p < kPads; p++)
   {
      padMode[p] = kOneShot;
      padVolume[p] = 0.8f;
      padPitch[p] = 0.0f;
      padPan[p] = 0.0f;
      padSpeed[p] = 1.0f;
      padFine[p] = 0.0f;
      padFadeIn[p] = 3.0f;
      padFadeOut[p] = 3.0f;
      padSync[p] = kFree;
      padDiv[p] = (int)MusicTime::kQuarter;
   }
}

MpcNode::~MpcNode() = default;

AudioNode* MpcNode::GetAudioNode()
{
   return mAudioNode.get();
}

void MpcNode::SetPadHeld(int pad, bool held, float velocity)
{
   pad = Clamp(pad);
   if (mHeld[pad] == held)
      return;
   mHeld[pad] = held;
   mAudioNode->PushEvent(pad, held, velocity);
}

bool MpcNode::LoadPad(int pad, const std::string& path)
{
   pad = Clamp(pad);
   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (path.empty() || !AudioDecodeCache::DecodeCached(path, *decoded, error))
   {
      delete decoded;
      return false;
   }
   const size_t slash = path.find_last_of("/\\");
   padName[pad] = slash == std::string::npos ? path : path.substr(slash + 1);
   padPath[pad] = path;

   // Decimated min/max waveform for the pad's tooltip/preview strip.
   padWaveCount[pad] = std::min(kWaveCache, decoded->numFrames);
   if (padWaveCount[pad] > 0)
   {
      const int per = std::max(1, decoded->numFrames / padWaveCount[pad]);
      for (int b = 0; b < padWaveCount[pad]; b++)
      {
         float mn = 0.0f, mx = 0.0f;
         const int start = b * per;
         const int end = std::min(decoded->numFrames, start + per);
         for (int i = start; i < end; i++)
         {
            mn = std::min(mn, decoded->channelData[(size_t)i]);
            mx = std::max(mx, decoded->channelData[(size_t)i]);
         }
         padWaveMin[pad][b] = mn;
         padWaveMax[pad][b] = mx;
      }
   }
   mAudioNode->PushBuffer(pad, decoded);
   return true;
}

int MpcNode::LoadFolder(const std::string& folder)
{
   namespace fs = std::filesystem;
   static const char* kExts[] = { ".wav", ".wave", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".ogg", ".opus" };
   std::vector<std::string> files;
   std::error_code ec;
   for (fs::directory_iterator it(fs::u8path(folder), ec), end; it != end && !ec; it.increment(ec))
   {
      if (!it->is_regular_file(ec))
         continue;
      std::string ext = it->path().extension().u8string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      for (const char* e : kExts)
         if (ext == e)
         {
            files.push_back(it->path().u8string());
            break;
         }
   }
   std::sort(files.begin(), files.end());
   int loaded = 0;
   for (size_t i = 0; i < files.size() && loaded < kPads; i++)
      if (LoadPad(loaded, files[i]))
         loaded++;
   return loaded;
}

void MpcNode::ClearPad(int pad)
{
   pad = Clamp(pad);
   padPath[pad].clear();
   padName[pad].clear();
   padWaveCount[pad] = 0;
   // Push an empty buffer (a null push would leave the old one playing).
   mAudioNode->PushBuffer(pad, new Platform::SampleBuffer());
}

void MpcNode::ReloadFromPaths()
{
   for (int p = 0; p < kPads; p++)
   {
      if (padPath[p].empty())
         continue;
      const std::string path = padPath[p];
      if (!LoadPad(p, path))
      {
         padPath[p] = path; // keep the reference so a re-save doesn't lose it
         const size_t slash = path.find_last_of("/\\");
         padName[p] = slash == std::string::npos ? path : path.substr(slash + 1);
      }
   }
}

void MpcNode::PushParamsNow()
{
   mAudioNode->PushParams(*this);
}

void MpcNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   mAudioNode->DrainRetired();
   mAudioNode->PushParams(*this);
   mPlayingMask = mAudioNode->PlayingMask();
   mLevel = mAudioNode->Peak();
}

void MpcNode::VisitParams(ParamVisitor& v)
{
   char key[32];
   for (int p = 0; p < kPads; p++)
   {
      snprintf(key, sizeof(key), "pad%d_path", p);
      v.Text(key, padPath[p]);
      snprintf(key, sizeof(key), "pad%d_mode", p);
      v.Int(key, padMode[p]);
      snprintf(key, sizeof(key), "pad%d_volume", p);
      v.Float(key, padVolume[p]);
      snprintf(key, sizeof(key), "pad%d_pitch", p);
      v.Float(key, padPitch[p]);
      snprintf(key, sizeof(key), "pad%d_pan", p);
      v.Float(key, padPan[p]);
      snprintf(key, sizeof(key), "pad%d_speed", p);
      v.Float(key, padSpeed[p]);
      snprintf(key, sizeof(key), "pad%d_fine", p);
      v.Float(key, padFine[p]);
      snprintf(key, sizeof(key), "pad%d_fadein", p);
      v.Float(key, padFadeIn[p]);
      snprintf(key, sizeof(key), "pad%d_fadeout", p);
      v.Float(key, padFadeOut[p]);
      snprintf(key, sizeof(key), "pad%d_sync", p);
      v.Int(key, padSync[p]);
      snprintf(key, sizeof(key), "pad%d_div", p);
      v.Int(key, padDiv[p]);
   }
   v.Int("selectedPad", selectedPad);
}

void MpcNode::SweepPrepare()
{
   // The sweep drives one held note (69), outside the fixed 36..51 range. In
   // this mode any note starts every pad, each holding a synthetic tone, so all
   // 16 pads' params are observable.
   mAudioNode->SetTestAllPads(true);
   auto* buf = new Platform::SampleBuffer();
   buf->channels = 2;
   buf->numFrames = 48000;
   buf->sampleRate = 48000.0;
   buf->channelData.assign((size_t)buf->numFrames * 2, 0.0f);
   for (int i = 0; i < buf->numFrames; i++)
   {
      const float s = 0.5f * std::sin(2.0f * (float)M_PI * 220.0f * (float)i / 48000.0f);
      buf->channelData[(size_t)i] = s;
      buf->channelData[(size_t)buf->numFrames + (size_t)i] = s;
   }
   for (int p = 0; p < kPads; p++)
   {
      auto* copy = new Platform::SampleBuffer(*buf);
      mAudioNode->PushBuffer(p, copy);
   }
   delete buf;
}
