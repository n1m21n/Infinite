#include "VmpcNode.h"

#include "platform/OpenGLHeaders.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MediaExtensions.h"
#include "audio/NoteEventQueue.h"
#include "audio/SampleSlot.h"
#include "core/AudioTopologyRequest.h"

// Audio-thread half: forwards notes to the main thread, and plays the active
// pad's soundtrack (monophonic, like the picture). The main thread sends
// Play/Stop commands; per-pad range/speed/loop are plain atomics read every
// block, so trimming or changing speed while a clip plays is heard at once.
class AudioVmpcNode : public AudioNode
{
public:
   struct Event
   {
      int note = 0;
      float velocity = 0.0f;
      bool on = false;
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mDeviceRate.store(sampleRate > 0.0 ? sampleRate : 48000.0, std::memory_order_relaxed);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mInbox = inbox;
      mCursor = cursor;
   }

   // ---- main thread -----------------------------------------------------
   bool PopEvent(Event& out)
   {
      const uint32_t head = mHead.load(std::memory_order_relaxed);
      if (head == mTail.load(std::memory_order_acquire))
         return false;
      out = mEvents[head];
      mHead.store((head + 1) % kEventCapacity, std::memory_order_release);
      return true;
   }

   void PushBuffer(int pad, Platform::SampleBuffer* buffer) { mSlots[pad].Push(buffer); }
   void DrainRetired()
   {
      for (auto& s : mSlots)
         s.DrainRetired();
   }

   void SetPadParams(int pad, double fromSec, double toSec, float speed, bool loop)
   {
      mFrom[pad].store(fromSec, std::memory_order_relaxed);
      mTo[pad].store(toSec, std::memory_order_relaxed);
      mSpeed[pad].store(speed, std::memory_order_relaxed);
      mLoop[pad].store(loop, std::memory_order_relaxed);
   }
   void SetVolume(float v) { mVolume.store(v, std::memory_order_relaxed); }

   void Play(int pad, double startSec) { PushCmd({ pad, startSec }); }
   void Stop() { PushCmd({ -1, 0.0 }); }

   int PlayingPad() const { return mPubPad.load(std::memory_order_relaxed); }
   double PositionSeconds() const { return mPubPos.load(std::memory_order_relaxed); }
   uint64_t Blocks() const { return mBlocks.load(std::memory_order_relaxed); }
   uint32_t EndSerial() const { return mEndSerial.load(std::memory_order_relaxed); }

   // ---- audio thread ----------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int frames = output.numFrames;
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + frames, 0.0f);

      // Notes -> main thread.
      if (mInbox != nullptr)
      {
         NoteEvent notes[64];
         int n = 0;
         while ((n = mInbox->Pop(mCursor, notes, 64)) > 0)
         {
            for (int i = 0; i < n; i++)
            {
               if (notes[i].bendUpdate)
                  continue;
               const uint32_t tail = mTail.load(std::memory_order_relaxed);
               const uint32_t next = (tail + 1) % kEventCapacity;
               if (next == mHead.load(std::memory_order_acquire))
                  break; // full: the main thread is stalled
               mEvents[tail] = { notes[i].note, notes[i].velocity, notes[i].isNoteOn };
               mTail.store(next, std::memory_order_release);
            }
         }
      }

      for (int p = 0; p < VmpcNode::kPads; p++)
         if (mSlots[p].SwapIn() && p == mPad)
            mPad = -1; // the buffer under the playhead was replaced

      // Commands (the last one wins).
      for (;;)
      {
         const uint32_t head = mCmdHead.load(std::memory_order_relaxed);
         if (head == mCmdTail.load(std::memory_order_acquire))
            break;
         const Cmd c = mCmds[head];
         mCmdHead.store((head + 1) % kCmdCapacity, std::memory_order_release);
         if (c.pad >= 0 && c.pad < VmpcNode::kPads && mSlots[c.pad].Active() != nullptr &&
             mSlots[c.pad].Active()->numFrames > 0)
         {
            const Platform::SampleBuffer* b = mSlots[c.pad].Active();
            mPad = c.pad;
            mPosFrames = c.startSec * b->sampleRate;
            mFade = 0.0f; // fade in
            mStopping = false;
         }
         else if (mPad >= 0)
            mStopping = true; // fade out, then stop
      }

      const Platform::SampleBuffer* b = (mPad >= 0) ? mSlots[mPad].Active() : nullptr;
      if (b == nullptr || b->numFrames <= 0 || b->channels <= 0 || b->sampleRate <= 0.0)
      {
         mPad = -1;
         Publish();
         return;
      }

      const double devRate = mDeviceRate.load(std::memory_order_relaxed);
      const double speed = std::clamp((double)mSpeed[mPad].load(std::memory_order_relaxed), -4.0, 4.0);
      const double step = speed * b->sampleRate / std::max(1.0, devRate);
      const double last = (double)(b->numFrames - 1);
      double from = std::clamp(mFrom[mPad].load(std::memory_order_relaxed) * b->sampleRate, 0.0, last);
      double to = std::clamp(mTo[mPad].load(std::memory_order_relaxed) * b->sampleRate, from + 1.0, last + 1.0);
      const bool loop = mLoop[mPad].load(std::memory_order_relaxed);
      const float volume = mVolume.load(std::memory_order_relaxed);
      const float fadeStep = 1.0f / 96.0f;

      for (int i = 0; i < frames; i++)
      {
         if (mStopping)
         {
            mFade -= fadeStep;
            if (mFade <= 0.0f)
            {
               mPad = -1;
               break;
            }
         }
         else if (mFade < 1.0f)
            mFade = std::min(1.0f, mFade + fadeStep);

         if (mPosFrames < from || mPosFrames >= to)
         {
            if (loop && std::fabs(step) > 1e-9)
               mPosFrames = step >= 0.0 ? from : std::max(from, to - 1.0);
            else
            {
               mPad = -1;
               mEndSerial.fetch_add(1, std::memory_order_relaxed);
               break;
            }
         }
         const int i0 = std::clamp((int)mPosFrames, 0, b->numFrames - 1);
         const int i1 = std::min(i0 + 1, b->numFrames - 1);
         const float frac = (float)(mPosFrames - std::floor(mPosFrames));
         const float g = volume * mFade;
         for (int ch = 0; ch < output.numChannels; ch++)
         {
            const int src = std::min(ch, b->channels - 1);
            const float* d = b->channelData.data() + (size_t)src * (size_t)b->numFrames;
            output.channels[ch][i] = (d[i0] + (d[i1] - d[i0]) * frac) * g;
         }
         mPosFrames += step;
      }
      mPubPos.store(mPad >= 0 ? std::clamp(mPosFrames, from, to) / b->sampleRate : 0.0, std::memory_order_relaxed);
      Publish();
   }

private:
   struct Cmd
   {
      int pad = -1;
      double startSec = 0.0;
   };
   static constexpr uint32_t kEventCapacity = 256;
   static constexpr uint32_t kCmdCapacity = 32;

   void PushCmd(const Cmd& c)
   {
      const uint32_t tail = mCmdTail.load(std::memory_order_relaxed);
      const uint32_t next = (tail + 1) % kCmdCapacity;
      if (next == mCmdHead.load(std::memory_order_acquire))
         return;
      mCmds[tail] = c;
      mCmdTail.store(next, std::memory_order_release);
   }

   void Publish()
   {
      mPubPad.store(mPad, std::memory_order_relaxed);
      mBlocks.fetch_add(1, std::memory_order_relaxed);
   }

   NoteEventQueue* mInbox = nullptr;
   int mCursor = -1;
   Event mEvents[kEventCapacity];
   std::atomic<uint32_t> mHead { 0 };
   std::atomic<uint32_t> mTail { 0 };

   Cmd mCmds[kCmdCapacity];
   std::atomic<uint32_t> mCmdHead { 0 };
   std::atomic<uint32_t> mCmdTail { 0 };

   SampleSlot mSlots[VmpcNode::kPads];
   std::atomic<double> mFrom[VmpcNode::kPads] {};
   std::atomic<double> mTo[VmpcNode::kPads] {};
   std::atomic<float> mSpeed[VmpcNode::kPads] {};
   std::atomic<bool> mLoop[VmpcNode::kPads] {};
   std::atomic<float> mVolume { 1.0f };
   std::atomic<double> mDeviceRate { 48000.0 };

   // audio-thread state
   int mPad = -1;
   double mPosFrames = 0.0;
   float mFade = 0.0f;
   bool mStopping = false;

   std::atomic<int> mPubPad { -1 };
   std::atomic<double> mPubPos { 0.0 };
   std::atomic<uint64_t> mBlocks { 0 };
   std::atomic<uint32_t> mEndSerial { 0 };
};

VmpcNode::VmpcNode() : mAudio(std::make_unique<AudioVmpcNode>())
{
   for (int p = 0; p < kPads; p++)
   {
      padMode[p] = kOneShot;
      padStart[p] = 0.0f;
      padEnd[p] = 1.0f;
      padSpeed[p] = 1.0f;
   }
}

VmpcNode::~VmpcNode()
{
   for (Pad& pad : mPads)
      if (pad.video != nullptr)
         Platform::VideoClose(pad.video);
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
   if (mClearTex != 0)
      glDeleteTextures(1, &mClearTex);
}

AudioNode* VmpcNode::GetAudioNode()
{
   return mAudio.get();
}

bool VmpcNode::RequiresAudioProcessing() const
{
   for (const Pad& p : mPads)
      if (p.hasAudio)
         return true;
   return false;
}

void VmpcNode::EnsureTextures()
{
   if (mClearTex == 0)
   {
      const unsigned char px[4] = { 0, 0, 0, 0 };
      glGenTextures(1, &mClearTex);
      glBindTexture(GL_TEXTURE_2D, mClearTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   if (mTex == 0)
   {
      glGenTextures(1, &mTex);
      glBindTexture(GL_TEXTURE_2D, mTex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
}

unsigned int VmpcNode::GetOutputTexture()
{
   EnsureTextures();
   const bool show = mShowClip && (mPlaying || holdLastFrame);
   return show ? mTex : mClearTex;
}

bool VmpcNode::LoadPad(int pad, const std::string& path)
{
   pad = Clamp(pad);
   if (path.empty())
      return false;
   std::string error;
   Platform::VideoHandle* handle = Platform::VideoOpen(path, error);
   if (handle == nullptr)
   {
      mPads[pad].status = error.empty() ? "could not open video" : error;
      return false;
   }
   if (mActive == pad)
      StopPlayback();
   Pad& p = mPads[pad];
   if (p.video != nullptr)
      Platform::VideoClose(p.video);
   p.video = handle;
   p.duration = std::max(0.0, Platform::VideoDuration(handle));
   p.w = Platform::VideoWidth(handle);
   p.h = Platform::VideoHeight(handle);
   const size_t slash = path.find_last_of("/\\");
   p.name = slash == std::string::npos ? path : path.substr(slash + 1);
   p.status = "loaded";
   padPath[pad] = path;

   // The soundtrack, decoded once. A clip without an audio track just plays
   // silent (no error).
   auto* decoded = new Platform::SampleBuffer();
   std::string audioError;
   if (Platform::DecodeAudioFileToBuffer(path, *decoded, audioError) && decoded->numFrames > 0 &&
       decoded->channels > 0 && decoded->sampleRate > 0.0)
      p.hasAudio = true;
   else
   {
      delete decoded;
      decoded = new Platform::SampleBuffer();
      p.hasAudio = false;
   }
   mAudio->PushBuffer(pad, decoded);
   AudioTopologyRequest::Request();
   return true;
}

int VmpcNode::LoadFolder(const std::string& folder)
{
   namespace fs = std::filesystem;
   std::vector<std::string> files;
   std::error_code ec;
   for (fs::directory_iterator it(fs::u8path(folder), ec), end; it != end && !ec; it.increment(ec))
   {
      if (!it->is_regular_file(ec))
         continue;
      std::string ext = it->path().extension().u8string();
      if (!ext.empty() && ext[0] == '.')
         ext = ext.substr(1);
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      for (const std::string& e : MediaExtensions::Video())
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

void VmpcNode::ClearPad(int pad)
{
   pad = Clamp(pad);
   if (mActive == pad)
   {
      StopPlayback();
      mShowClip = false;
      mActive = -1;
   }
   Pad& p = mPads[pad];
   if (p.video != nullptr)
      Platform::VideoClose(p.video);
   p = Pad();
   padPath[pad].clear();
   mAudio->PushBuffer(pad, new Platform::SampleBuffer());
   AudioTopologyRequest::Request();
}

void VmpcNode::ReloadFromPaths()
{
   for (int p = 0; p < kPads; p++)
   {
      const std::string path = padPath[p];
      if (path.empty())
      {
         if (mPads[p].video != nullptr)
            ClearPad(p);
         continue;
      }
      if (!LoadPad(p, path))
         padPath[p] = path; // keep it, so saving again does not lose the reference
   }
}

void VmpcNode::RangeSeconds(int pad, double& from, double& to) const
{
   const double dur = mPads[Clamp(pad)].duration;
   const float s = std::clamp(padStart[pad], 0.0f, 1.0f);
   const float e = std::clamp(padEnd[pad], s, 1.0f);
   from = (double)s * dur;
   to = std::max(from + 0.001, (double)e * dur);
}

bool VmpcNode::AudioDriven() const
{
   return mAudioStarted && mActive >= 0 && mPads[mActive].hasAudio && playAudio;
}

void VmpcNode::StartPad(int pad)
{
   double from = 0.0, to = 0.0;
   RangeSeconds(pad, from, to);
   mActive = pad;
   mPlaying = true;
   // Keep showing the previous frame until the new clip delivers its first
   // one (no black flash between clips).
   mPos = padSpeed[pad] < 0.0f ? std::max(from, to - 0.001) : from;
   mHaveTick = false;
   mAudioStarted = false;
   if (mPads[pad].hasAudio && playAudio)
   {
      mAudio->SetPadParams(pad, from, to, padSpeed[pad], std::clamp(padMode[pad], 0, 2) != kOneShot);
      mAudio->Play(pad, mPos);
      mAudioStarted = true;
      mLastAudioBlocks = mAudio->Blocks();
      mLastEndSerial = mAudio->EndSerial();
   }
   else
      mAudio->Stop();
}

void VmpcNode::StopPlayback()
{
   mPlaying = false;
   mAudioStarted = false;
   mAudio->Stop();
   mRevision = NextTextureRevision();
}

void VmpcNode::PadEvent(int pad, bool down, float /*velocity*/)
{
   if (pad < 0 || pad >= kPads || !PadLoaded(pad))
      return;
   const int mode = std::clamp(padMode[pad], 0, 2);
   if (down)
   {
      if (mode == kLoopToggle && mPlaying && mActive == pad)
      {
         StopPlayback();
         return;
      }
      StartPad(pad);
   }
   else if (mode == kGate && mPlaying && mActive == pad)
      StopPlayback();
}

void VmpcNode::StopAll()
{
   StopPlayback();
}

float VmpcNode::ActivePosition01() const
{
   if (!mPlaying || mActive < 0)
      return 0.0f;
   const double dur = mPads[mActive].duration;
   return dur > 0.0 ? (float)std::clamp(mPos / dur, 0.0, 1.0) : 0.0f;
}

void VmpcNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   EnsureTextures();
   mAudio->DrainRetired();
   mAudio->SetVolume(std::clamp(audioVolume, 0.0f, 2.0f));

   // Notes from the audio thread.
   AudioVmpcNode::Event e;
   while (mAudio->PopEvent(e))
      PadEvent(e.note - baseNote, e.on, e.velocity);

   const auto now = std::chrono::steady_clock::now();
   double dt = 0.0;
   if (mHaveTick)
      dt = std::clamp(std::chrono::duration<double>(now - mLastTick).count(), 0.0, 0.25);
   mLastTick = now;
   mHaveTick = true;

   if (mAudioStarted && !playAudio)
   {
      // Audio switched off mid-take: silence it, the picture carries on
      // on the wall clock.
      mAudio->Stop();
      mAudioStarted = false;
   }

   if (!mPlaying || mActive < 0 || mPads[mActive].video == nullptr)
      return;

   const int pad = mActive;
   const int mode = std::clamp(padMode[pad], 0, 2);
   double from = 0.0, to = 0.0;
   RangeSeconds(pad, from, to);
   const double speed = std::clamp((double)padSpeed[pad], -4.0, 4.0);

   bool clocked = false;
   if (AudioDriven())
   {
      mAudio->SetPadParams(pad, from, to, padSpeed[pad], mode != kOneShot);
      // The soundtrack is the clock while the audio thread is running;
      // with no audio device the blocks stop advancing and the wall clock
      // takes over.
      const uint64_t blocks = mAudio->Blocks();
      if (mAudio->EndSerial() != mLastEndSerial && mAudio->PlayingPad() != pad)
      {
         StopPlayback();
         return;
      }
      if (blocks != mLastAudioBlocks && mAudio->PlayingPad() == pad)
      {
         mPos = mAudio->PositionSeconds();
         clocked = true;
      }
      mLastAudioBlocks = blocks;
   }
   if (!clocked)
   {
      mPos += dt * speed;
      if (mPos >= to || mPos < from)
      {
         if (mode == kOneShot)
         {
            StopPlayback();
            return;
         }
         const double len = std::max(0.001, to - from);
         if (speed >= 0.0)
            mPos = from + std::fmod(std::max(0.0, mPos - from), len);
         else
            mPos = to - std::fmod(std::max(0.0, to - mPos), len);
         mPos = std::clamp(mPos, from, std::max(from, to - 0.0005));
      }
   }

   Pad& p = mPads[pad];
   if (Platform::VideoFrameAt(p.video, mPos, mFrame) && !mFrame.empty())
   {
      const int w = Platform::VideoWidth(p.video);
      const int h = Platform::VideoHeight(p.video);
      if (w > 0 && h > 0 && mFrame.size() >= (size_t)w * (size_t)h * 3)
      {
         glBindTexture(GL_TEXTURE_2D, mTex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         if (w != mTexW || h != mTexH)
         {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGR, GL_UNSIGNED_BYTE, mFrame.data());
            mTexW = w;
            mTexH = h;
         }
         else
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, mFrame.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         mWidth = w;
         mHeight = h;
         mShowClip = true;
         mRevision = NextTextureRevision();
      }
   }
}

void VmpcNode::VisitParams(ParamVisitor& v)
{
   char key[32];
   for (int p = 0; p < kPads; p++)
   {
      snprintf(key, sizeof(key), "pad%d_path", p);
      v.Text(key, padPath[p]);
      snprintf(key, sizeof(key), "pad%d_mode", p);
      v.Int(key, padMode[p]);
      snprintf(key, sizeof(key), "pad%d_start", p);
      v.Float(key, padStart[p]);
      snprintf(key, sizeof(key), "pad%d_end", p);
      v.Float(key, padEnd[p]);
      snprintf(key, sizeof(key), "pad%d_speed", p);
      v.Float(key, padSpeed[p]);
   }
   v.Int("baseNote", baseNote);
   v.Bool("holdLastFrame", holdLastFrame);
   v.Bool("playAudio", playAudio);
   v.Float("audioVolume", audioVolume);
   v.Int("selectedPad", selectedPad);
}
