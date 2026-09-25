#include "MpcNode.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/DspMath.h"
#include "audio/NoteEventQueue.h"
#include "audio/SampleSlot.h"
#include "platform/Platform.h"

namespace
{
   constexpr int kReleaseFrames = 96;   // ~2 ms fade when a gate/loop stops
   constexpr int kCmdCapacity = 256;

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
   };
}

class AudioMpcNode : public AudioNode
{
public:
   AudioMpcNode()
   {
      mPadOut.assign((size_t)MpcNode::kPads * 2 * kAudioMaxBlockFrames, 0.0f);
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         mMode[p].store(MpcNode::kOneShot, std::memory_order_relaxed);
         mVolume[p].store(0.8f, std::memory_order_relaxed);
         mPitch[p].store(0.0f, std::memory_order_relaxed);
         mPan[p].store(0.0f, std::memory_order_relaxed);
         mStart[p].store(0.0f, std::memory_order_relaxed);
         mEnd[p].store(1.0f, std::memory_order_relaxed);
      }
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
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
         mVolume[p].store(std::clamp(n.padVolume[p], 0.0f, 2.0f), std::memory_order_relaxed);
         mPitch[p].store(std::clamp(n.padPitch[p], -24.0f, 24.0f), std::memory_order_relaxed);
         mPan[p].store(std::clamp(n.padPan[p], -1.0f, 1.0f), std::memory_order_relaxed);
         const float s0 = std::clamp(n.padStart[p], 0.0f, 1.0f);
         mStart[p].store(s0, std::memory_order_relaxed);
         mEnd[p].store(std::clamp(n.padEnd[p], s0, 1.0f), std::memory_order_relaxed);
      }
      mMaster.store(std::clamp(n.volume, 0.0f, 2.0f), std::memory_order_relaxed);
      mBaseNote.store(std::clamp(n.baseNote, 0, 112), std::memory_order_relaxed);
      mVelocitySensitive.store(n.velocitySensitive, std::memory_order_relaxed);
   }

   unsigned int PlayingMask() const { return mPlayingMask.load(std::memory_order_relaxed); }
   float Peak() const { return mPeak.load(std::memory_order_relaxed); }

   // ---- MPC Out tap (audio thread, after this node's ProcessBlock) -------
   uint64_t BlockCounter() const { return mBlockCounter.load(std::memory_order_acquire); }
   const float* PadChannel(int pad, int ch) const
   {
      return mPadOut.data() + ((size_t)std::clamp(pad, 0, MpcNode::kPads - 1) * 2 + (size_t)(ch & 1)) *
                                 kAudioMaxBlockFrames;
   }
   int LastBlockFrames() const { return mLastFrames; }

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
         }
      }

      // Collect this block's events: UI/CV at offset 0, notes at their
      // own frame offsets. Kept sorted by offset (insertion sort, tiny N).
      PadEventAt events[128];
      int eventCount = 0;
      auto addEvent = [&](const PadEventAt& e)
      {
         if (eventCount >= 128)
            return;
         int k = eventCount++;
         while (k > 0 && events[k - 1].offset > e.offset)
         {
            events[k] = events[k - 1];
            k--;
         }
         events[k] = e;
      };
      for (;;)
      {
         const uint32_t head = mCmdHead.load(std::memory_order_relaxed);
         if (head == mCmdTail.load(std::memory_order_acquire))
            break;
         const PadCommand c = mCmds[head];
         mCmdHead.store((head + 1) % kCmdCapacity, std::memory_order_release);
         addEvent({ 0, c.pad, c.down, c.velocity });
      }
      if (mNoteInbox != nullptr)
      {
         const int base = mBaseNote.load(std::memory_order_relaxed);
         NoteEvent notes[64];
         int n = 0;
         while ((n = mNoteInbox->Pop(mNoteCursor, notes, 64)) > 0)
         {
            for (int i = 0; i < n; i++)
            {
               if (notes[i].bendUpdate)
                  continue;
               const int pad = notes[i].note - base;
               if (pad < 0 || pad >= MpcNode::kPads)
                  continue;
               addEvent({ std::clamp(notes[i].frameOffset, 0, std::max(0, numFrames - 1)), pad,
                          notes[i].isNoteOn, notes[i].velocity });
            }
         }
      }

      for (int p = 0; p < MpcNode::kPads; p++)
      {
         std::fill(PadWrite(p, 0), PadWrite(p, 0) + numFrames, 0.0f);
         std::fill(PadWrite(p, 1), PadWrite(p, 1) + numFrames, 0.0f);
      }

      int cursor = 0;
      for (int e = 0; e < eventCount; e++)
      {
         const int at = std::clamp(events[e].offset, cursor, numFrames);
         Render(cursor, at);
         Apply(events[e]);
         cursor = at;
      }
      Render(cursor, numFrames);

      // Master mix.
      const float master = mMaster.load(std::memory_order_relaxed);
      float peak = 0.0f;
      for (int i = 0; i < numFrames; i++)
      {
         float l = 0.0f, r = 0.0f;
         for (int p = 0; p < MpcNode::kPads; p++)
         {
            l += PadWrite(p, 0)[i];
            r += PadWrite(p, 1)[i];
         }
         l *= master;
         r *= master;
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
      mPeak.store(peak, std::memory_order_relaxed);
      mLastFrames = numFrames;
      mBlockCounter.fetch_add(1, std::memory_order_release);
   }

private:
   struct Voice
   {
      bool active = false;
      bool looping = false;
      double pos = 0.0;
      float gain = 1.0f;
      int release = -1; // >= 0: frames left in the stop fade
   };

   float* PadWrite(int pad, int ch)
   {
      return mPadOut.data() + ((size_t)pad * 2 + (size_t)ch) * kAudioMaxBlockFrames;
   }

   void Start(int p, float velocity, bool looping)
   {
      Voice& v = mVoice[p];
      v.active = mActive[p] != nullptr && mActive[p]->numFrames > 0;
      v.looping = looping;
      v.pos = 0.0;
      if (v.active)
         v.pos = (double)mStart[p].load(std::memory_order_relaxed) * (double)(mActive[p]->numFrames - 1);
      v.release = -1;
      v.gain = mVelocitySensitive.load(std::memory_order_relaxed) ? std::clamp(velocity, 0.0f, 1.0f) : 1.0f;
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
         if (mVoice[p].active && mVoice[p].release < 0)
            Release(p);
         else
            Start(p, e.velocity, true);
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
         // Trim: play [startFrame, endFrame); loops wrap inside that range.
         const double startFrame = (double)mStart[p].load(std::memory_order_relaxed) * (double)(frames - 1);
         const double endFrame = std::max(startFrame + 2.0,
                                          (double)mEnd[p].load(std::memory_order_relaxed) * (double)(frames - 1));
         const double rangeLen = std::max(1.0, endFrame - startFrame);
         const double srcRate = buf->sampleRate > 0.0 ? buf->sampleRate : mSampleRate;
         const double step = (srcRate / mSampleRate) *
                             std::pow(2.0, (double)mPitch[p].load(std::memory_order_relaxed) / 12.0);
         float panL = 1.0f, panR = 1.0f;
         DspMath::EqualPowerPan(mPan[p].load(std::memory_order_relaxed), panL, panR);
         const float vol = mVolume[p].load(std::memory_order_relaxed) * v.gain;
         panL *= vol * (float)M_SQRT2;
         panR *= vol * (float)M_SQRT2;
         float* outL = PadWrite(p, 0);
         float* outR = PadWrite(p, 1);
         for (int i = from; i < to; i++)
         {
            if (v.pos < startFrame)
               v.pos = startFrame;
            int idx = (int)v.pos;
            if (v.pos >= endFrame - 1.0 || idx >= frames - 1)
            {
               if (v.looping)
               {
                  v.pos = startFrame + std::fmod(v.pos - startFrame, rangeLen);
                  idx = std::clamp((int)v.pos, 0, frames - 2);
               }
               else
               {
                  v.active = false;
                  break;
               }
            }
            const float frac = (float)(v.pos - (double)idx);
            const float a = src0[idx] + (src0[idx + 1] - src0[idx]) * frac;
            const float b = src1[idx] + (src1[idx + 1] - src1[idx]) * frac;
            float env = 1.0f;
            if (v.release >= 0)
            {
               env = (float)v.release / (float)kReleaseFrames;
               if (--v.release < 0)
               {
                  v.active = false;
                  v.pos = 0.0;
                  break;
               }
            }
            outL[i] += a * panL * env;
            outR[i] += b * panR * env;
            v.pos += step;
         }
      }
   }

   double mSampleRate = 48000.0;
   SampleSlot mSlots[MpcNode::kPads];
   const Platform::SampleBuffer* mActive[MpcNode::kPads] = {};
   Voice mVoice[MpcNode::kPads];
   std::vector<float> mPadOut;
   int mLastFrames = 0;

   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;

   PadCommand mCmds[kCmdCapacity];
   std::atomic<uint32_t> mCmdHead { 0 };
   std::atomic<uint32_t> mCmdTail { 0 };

   std::atomic<int> mMode[MpcNode::kPads];
   std::atomic<float> mVolume[MpcNode::kPads];
   std::atomic<float> mPitch[MpcNode::kPads];
   std::atomic<float> mPan[MpcNode::kPads];
   std::atomic<float> mStart[MpcNode::kPads];
   std::atomic<float> mEnd[MpcNode::kPads];
   std::atomic<float> mMaster { 0.8f };
   std::atomic<int> mBaseNote { 36 };
   std::atomic<bool> mVelocitySensitive { true };

   std::atomic<unsigned int> mPlayingMask { 0 };
   std::atomic<float> mPeak { 0.0f };
   std::atomic<uint64_t> mBlockCounter { 0 };
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
      padStart[p] = 0.0f;
      padEnd[p] = 1.0f;
      padStatus[p] = "empty";
   }
}

MpcNode::~MpcNode() = default;

AudioNode* MpcNode::GetAudioNode()
{
   return mAudioNode.get();
}

bool MpcNode::PadPlaying(int pad) const
{
   return (mPlayingMask & (1u << Clamp(pad))) != 0;
}

void MpcNode::PadEvent(int pad, bool down, float velocity)
{
   mAudioNode->PushEvent(Clamp(pad), down, velocity);
}

bool MpcNode::LoadPad(int pad, const std::string& path)
{
   pad = Clamp(pad);
   auto* decoded = new Platform::SampleBuffer();
   std::string error;
   if (path.empty() || !Platform::DecodeAudioFileToBuffer(path, *decoded, error))
   {
      delete decoded;
      padStatus[pad] = error.empty() ? "failed to load" : error;
      return false;
   }
   const size_t slash = path.find_last_of("/\\");
   padName[pad] = slash == std::string::npos ? path : path.substr(slash + 1);
   padPath[pad] = path;
   padStatus[pad] = "loaded";

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
      {
         padStart[loaded] = 0.0f;
         padEnd[loaded] = 1.0f;
         loaded++;
      }
   return loaded;
}

void MpcNode::ClearPad(int pad)
{
   pad = Clamp(pad);
   padPath[pad].clear();
   padName[pad].clear();
   padStatus[pad] = "empty";
   padWaveCount[pad] = 0;
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
         padPath[p] = path; // keep the reference so a re-save doesn't lose it
   }
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
      snprintf(key, sizeof(key), "pad%d_start", p);
      v.Float(key, padStart[p]);
      snprintf(key, sizeof(key), "pad%d_end", p);
      v.Float(key, padEnd[p]);
   }
   v.Int("baseNote", baseNote);
   v.Float("volume", volume);
   v.Bool("velocitySensitive", velocitySensitive);
   v.Int("selectedPad", selectedPad);
}

// --------------------------------------------------------------- MPC Out
class AudioMpcOutNode : public AudioNode
{
public:
   void SetSource(AudioMpcNode* src) { mSource.store(src, std::memory_order_release); }
   void PushParams(int pad, float gainDb)
   {
      mPad.store(std::clamp(pad, 0, MpcNode::kPads - 1), std::memory_order_relaxed);
      mGain.store(DspMath::DbToLinear(std::clamp(gainDb, -60.0f, 12.0f)), std::memory_order_relaxed);
   }
   float Peak() const { return mPeak.load(std::memory_order_relaxed); }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* in = numInputs > 0 ? inputs[0] : nullptr;
      AudioMpcNode* src = mSource.load(std::memory_order_acquire);
      const float gain = mGain.load(std::memory_order_relaxed);
      float peak = 0.0f;
      if (in == nullptr)
      {
         Silence(output);
      }
      else if (src == nullptr)
      {
         // Not an MPC: plain pass-through.
         for (int ch = 0; ch < output.numChannels; ch++)
         {
            const float* s = in->channels[std::min(ch, in->numChannels - 1)];
            for (int i = 0; i < output.numFrames; i++)
            {
               output.channels[ch][i] = s[i] * gain;
               peak = std::max(peak, std::fabs(output.channels[ch][i]));
            }
         }
      }
      else
      {
         const uint64_t block = src->BlockCounter();
         if (block == mLastBlock || src->LastBlockFrames() < output.numFrames)
         {
            Silence(output); // the MPC did not render this block (bypassed)
         }
         else
         {
            const int pad = mPad.load(std::memory_order_relaxed);
            for (int ch = 0; ch < output.numChannels; ch++)
            {
               const float* s = src->PadChannel(pad, ch);
               for (int i = 0; i < output.numFrames; i++)
               {
                  output.channels[ch][i] = s[i] * gain;
                  peak = std::max(peak, std::fabs(output.channels[ch][i]));
               }
            }
         }
         mLastBlock = block;
      }
      mPeak.store(peak, std::memory_order_relaxed);
   }

private:
   static void Silence(AudioBuffer& output)
   {
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);
   }

   std::atomic<AudioMpcNode*> mSource { nullptr };
   std::atomic<int> mPad { 0 };
   std::atomic<float> mGain { 1.0f };
   std::atomic<float> mPeak { 0.0f };
   uint64_t mLastBlock = ~0ull;
};

MpcOutNode::MpcOutNode() : mAudioNode(std::make_unique<AudioMpcOutNode>()) {}
MpcOutNode::~MpcOutNode() = default;

AudioNode* MpcOutNode::GetAudioNode()
{
   return mAudioNode.get();
}

void MpcOutNode::ResolveAudioTaps()
{
   auto* mpc = dynamic_cast<MpcNode*>(input.GetSource());
   mConnectedToMpc = mpc != nullptr;
   mAudioNode->SetSource(mpc != nullptr ? mpc->AudioHalf() : nullptr);
}

void MpcOutNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   mAudioNode->PushParams(pad, gainDb);
   mLevel = mAudioNode->Peak();
}

void MpcOutNode::VisitParams(ParamVisitor& v)
{
   v.Int("pad", pad);
   v.Float("gainDb", gainDb);
}
