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
#include "core/AudioTopologyRequest.h"

// Audio-thread half: only drains the note inbox into a ring the main thread
// reads. Its audio output is silence (nothing downstream uses it).
class AudioVmpcNoteTap : public AudioNode
{
public:
   struct Event
   {
      int note = 0;
      float velocity = 0.0f;
      bool on = false;
   };

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mInbox = inbox;
      mCursor = cursor;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);
      if (mInbox == nullptr)
         return;
      NoteEvent notes[64];
      int n = 0;
      while ((n = mInbox->Pop(mCursor, notes, 64)) > 0)
      {
         for (int i = 0; i < n; i++)
         {
            if (notes[i].bendUpdate)
               continue;
            const uint32_t tail = mTail.load(std::memory_order_relaxed);
            const uint32_t next = (tail + 1) % kCapacity;
            if (next == mHead.load(std::memory_order_acquire))
               return; // full: the main thread is stalled, drop the rest
            mEvents[tail] = { notes[i].note, notes[i].velocity, notes[i].isNoteOn };
            mTail.store(next, std::memory_order_release);
         }
      }
   }

   // Main thread.
   bool Pop(Event& out)
   {
      const uint32_t head = mHead.load(std::memory_order_relaxed);
      if (head == mTail.load(std::memory_order_acquire))
         return false;
      out = mEvents[head];
      mHead.store((head + 1) % kCapacity, std::memory_order_release);
      return true;
   }

private:
   static constexpr uint32_t kCapacity = 256;
   NoteEventQueue* mInbox = nullptr;
   int mCursor = -1;
   Event mEvents[kCapacity];
   std::atomic<uint32_t> mHead { 0 };
   std::atomic<uint32_t> mTail { 0 };
};

VmpcNode::VmpcNode() : mNoteTap(std::make_unique<AudioVmpcNoteTap>())
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

AudioNode* VmpcNode::AudioNodeForNotePorts()
{
   return mNoteTap.get();
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
   {
      mPlaying = false;
      mShowClip = false;
      mRevision = NextTextureRevision();
   }
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
      mPlaying = false;
      mShowClip = false;
      mActive = -1;
      mRevision = NextTextureRevision();
   }
   Pad& p = mPads[pad];
   if (p.video != nullptr)
      Platform::VideoClose(p.video);
   p = Pad();
   padPath[pad].clear();
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
         mPlaying = false;
         mRevision = NextTextureRevision();
         return;
      }
      StartPad(pad);
   }
   else if (mode == kGate && mPlaying && mActive == pad)
   {
      mPlaying = false;
      mRevision = NextTextureRevision();
   }
}

void VmpcNode::StopAll()
{
   mPlaying = false;
   mRevision = NextTextureRevision();
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

   // Notes from the audio thread.
   AudioVmpcNoteTap::Event e;
   while (mNoteTap->Pop(e))
      PadEvent(e.note - baseNote, e.on, e.velocity);

   const auto now = std::chrono::steady_clock::now();
   double dt = 0.0;
   if (mHaveTick)
      dt = std::clamp(std::chrono::duration<double>(now - mLastTick).count(), 0.0, 0.25);
   mLastTick = now;
   mHaveTick = true;

   if (!mPlaying || mActive < 0 || mPads[mActive].video == nullptr)
      return;

   const int pad = mActive;
   const int mode = std::clamp(padMode[pad], 0, 2);
   double from = 0.0, to = 0.0;
   RangeSeconds(pad, from, to);
   const double speed = std::clamp((double)padSpeed[pad], -4.0, 4.0);
   mPos += dt * speed;
   if (mPos >= to || mPos < from)
   {
      if (mode == kOneShot)
      {
         mPlaying = false;
         mRevision = NextTextureRevision();
         return;
      }
      const double len = std::max(0.001, to - from);
      if (speed >= 0.0)
         mPos = from + std::fmod(std::max(0.0, mPos - from), len);
      else
         mPos = to - std::fmod(std::max(0.0, to - mPos), len);
      mPos = std::clamp(mPos, from, std::max(from, to - 0.0005));
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
   v.Int("selectedPad", selectedPad);
}
