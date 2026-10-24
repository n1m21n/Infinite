#include "VmpcNode.h"

#include "gl3.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "HapReader.h"
#include "Transport.h"
#include "VideoSourceNode.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MediaExtensions.h"
#include "audio/MusicTime.h"
#include "audio/NoteEventQueue.h"
#include "audio/SampleSlot.h"
#include "platform/Platform.h"

namespace
{
   constexpr int kEvtCapacity = 256;
   constexpr int kDecodeSet = 4;          // pads decoded per frame, at most
   constexpr double kSnapSeconds = 0.15;  // soundtrack drift beyond this snaps to the picture
   constexpr double kMinWindow = 0.02;    // a narrower start..end window falls back to the whole clip

   // GL names whose owner died (or cleared) with no context current; freed the
   // next time any VMPC has one.
   std::mutex gGraveMu;
   std::vector<unsigned int> gGrave;

   void ReleaseTex(unsigned int& t)
   {
      if (t == 0)
         return;
      if (glfwGetCurrentContext() != nullptr)
         glDeleteTextures(1, &t);
      else
      {
         std::lock_guard<std::mutex> lk(gGraveMu);
         gGrave.push_back(t);
      }
      t = 0;
   }

   void FlushGrave()
   {
      if (glfwGetCurrentContext() == nullptr)
         return;
      std::lock_guard<std::mutex> lk(gGraveMu);
      for (unsigned int& t : gGrave)
         glDeleteTextures(1, &t);
      gGrave.clear();
   }

   struct PadEvt
   {
      int pad = 0;
      bool down = false;
   };

   // top * wt + under * wb over black; the under clip is cover-fit to the top's aspect.
   const char* kCompositeFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTop;\n"
      "uniform sampler2D uUnder;\n"
      "uniform float uWt;\n"
      "uniform float uWb;\n"
      "uniform vec2 uUnderScale;\n"
      "void main()\n"
      "{\n"
      "   vec3 c = texture(uTop, vUv).rgb * uWt;\n"
      "   vec2 uu = (vUv - 0.5) * uUnderScale + 0.5;\n"
      "   c += texture(uUnder, uu).rgb * uWb;\n"
      "   fragColor = vec4(c, 1.0);\n"
      "}\n";
}

// ------------------------------------------------------------- audio thread
//
// Two jobs. It turns notes 36..51 into pad edges for the main thread (the pad
// engine lives there, next to the decoders), and it plays each pad's
// soundtrack at the gain the main thread publishes (the weight its picture is
// on screen with). The read cursor free-runs at the pad's speed and snaps to
// the published position on a new pass (serial) or when it drifts too far.
class AudioVmpcNode : public AudioNode
{
public:
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
   void Publish(int pad, double seconds, float rate, float gain, uint32_t serial)
   {
      mPos[pad].store(seconds, std::memory_order_relaxed);
      mRate[pad].store(rate, std::memory_order_relaxed);
      mGain[pad].store(gain, std::memory_order_relaxed);
      mSerial[pad].store(serial, std::memory_order_release);
   }
   bool PopEvent(PadEvt& out)
   {
      const uint32_t head = mEvtHead.load(std::memory_order_relaxed);
      if (head == mEvtTail.load(std::memory_order_acquire))
         return false;
      out = mEvts[head];
      mEvtHead.store((head + 1) % kEvtCapacity, std::memory_order_release);
      return true;
   }
   // Sweep hook: any note hits every pad.
   void SetTestAllPads(bool on) { mTestAllPads.store(on, std::memory_order_relaxed); }

   // ---- audio thread ----------------------------------------------------
   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      for (int ch = 0; ch < output.numChannels; ch++)
         std::fill(output.channels[ch], output.channels[ch] + output.numFrames, 0.0f);

      if (mNoteInbox != nullptr)
      {
         const bool all = mTestAllPads.load(std::memory_order_relaxed);
         NoteEvent notes[64];
         int n = 0;
         while ((n = mNoteInbox->Pop(mNoteCursor, notes, 64)) > 0)
         {
            for (int i = 0; i < n; i++)
            {
               if (notes[i].bendUpdate)
                  continue;
               if (all)
               {
                  for (int q = 0; q < VmpcNode::kPads; q++)
                     PushEvent(q, notes[i].isNoteOn);
                  continue;
               }
               const int pad = notes[i].note - VmpcNode::kFirstNote;
               if (pad >= 0 && pad < VmpcNode::kPads)
                  PushEvent(pad, notes[i].isNoteOn);
            }
         }
      }

      const int numFrames = output.numFrames;
      const float k = 1.0f - std::exp(-1.0f / (0.01f * (float)mSampleRate)); // ~10 ms gain smoothing
      for (int p = 0; p < VmpcNode::kPads; p++)
      {
         if (mSlots[p].SwapIn())
         {
            mActive[p] = mSlots[p].Active();
            mHave[p] = false;
         }
         const Platform::SampleBuffer* buf = mActive[p];
         if (buf == nullptr || buf->numFrames <= 1 || buf->channels <= 0)
            continue;
         const float target = std::clamp(mGain[p].load(std::memory_order_relaxed), 0.0f, 1.0f);
         if (target <= 0.0f && mSmooth[p] < 1e-5f)
         {
            mSmooth[p] = 0.0f;
            mHave[p] = false;
            continue;
         }
         const double srcRate = buf->sampleRate > 0.0 ? buf->sampleRate : mSampleRate;
         const uint32_t serial = mSerial[p].load(std::memory_order_acquire);
         const double published = mPos[p].load(std::memory_order_relaxed) * srcRate;
         if (!mHave[p] || serial != mLastSerial[p] || std::fabs(published - mCursor[p]) > kSnapSeconds * srcRate)
            mCursor[p] = published;
         mHave[p] = true;
         mLastSerial[p] = serial;
         const double step = (double)mRate[p].load(std::memory_order_relaxed) * srcRate / mSampleRate;
         const int frames = buf->numFrames;
         const float* src0 = buf->channelData.data();
         const float* src1 = buf->channels > 1 ? src0 + frames : src0;
         double cur = mCursor[p];
         float g = mSmooth[p];
         for (int i = 0; i < numFrames; i++)
         {
            g += (target - g) * k;
            float l = 0.0f, r = 0.0f;
            if (cur >= 0.0 && cur < (double)(frames - 1))
            {
               const int idx = (int)cur;
               const float frac = (float)(cur - (double)idx);
               l = src0[idx] + (src0[idx + 1] - src0[idx]) * frac;
               r = src1[idx] + (src1[idx + 1] - src1[idx]) * frac;
            }
            if (output.numChannels > 0)
               output.channels[0][i] += l * g;
            if (output.numChannels > 1)
               output.channels[1][i] += r * g;
            for (int ch = 2; ch < output.numChannels; ch++)
               output.channels[ch][i] += l * g;
            cur += step;
         }
         mCursor[p] = cur;
         mSmooth[p] = g;
      }
   }

private:
   void PushEvent(int pad, bool down)
   {
      const uint32_t tail = mEvtTail.load(std::memory_order_relaxed);
      const uint32_t next = (tail + 1) % kEvtCapacity;
      if (next == mEvtHead.load(std::memory_order_acquire))
         return;
      mEvts[tail] = { pad, down };
      mEvtTail.store(next, std::memory_order_release);
   }

   double mSampleRate = 48000.0;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;
   std::atomic<bool> mTestAllPads { false };

   SampleSlot mSlots[VmpcNode::kPads];
   const Platform::SampleBuffer* mActive[VmpcNode::kPads] = {};
   double mCursor[VmpcNode::kPads] = {};
   float mSmooth[VmpcNode::kPads] = {};
   bool mHave[VmpcNode::kPads] = {};
   uint32_t mLastSerial[VmpcNode::kPads] = {};

   std::atomic<double> mPos[VmpcNode::kPads] = {};
   std::atomic<float> mRate[VmpcNode::kPads] = {};
   std::atomic<float> mGain[VmpcNode::kPads] = {};
   std::atomic<uint32_t> mSerial[VmpcNode::kPads] = {};

   PadEvt mEvts[kEvtCapacity];
   std::atomic<uint32_t> mEvtHead { 0 };
   std::atomic<uint32_t> mEvtTail { 0 };
};

// ------------------------------------------------------------------ VmpcNode

// One background thread, two kinds of job: a pad's filmstrip (eight exact
// seeks on its own handle) and its soundtrack decode. Thumbnails run first. A
// job carries the pad's generation; a pad that is reloaded or cleared bumps it,
// so stale work is skipped (or abandoned between frames) and its result is
// dropped. Results are published to the main thread through `done`; the audio
// thread only ever sees a finished buffer via SampleSlot's atomic swap.
struct VmpcNode::Worker
{
   struct Job
   {
      bool thumbs = true;
      int pad = 0;
      uint32_t gen = 0;
      std::string path;
      double duration = 0.0;
   };
   struct Result
   {
      bool thumbs = true;
      int pad = 0;
      uint32_t gen = 0;
      bool ok = false;
      std::vector<unsigned char> px;
      std::unique_ptr<Platform::SampleBuffer> buf;
   };

   std::atomic<uint32_t> gen[kPads];
   std::mutex mu;
   std::condition_variable cv;
   std::deque<Job> jobs;
   std::vector<Result> done;
   bool stop = false;
   std::atomic<bool> stopFlag { false };
   std::thread th;

   Worker()
   {
      for (auto& g : gen)
         g.store(0, std::memory_order_relaxed);
   }
   ~Worker() { Stop(); }

   void Stop()
   {
      {
         std::lock_guard<std::mutex> lk(mu);
         stop = true;
         jobs.clear();
      }
      stopFlag.store(true, std::memory_order_release);
      cv.notify_all();
      if (th.joinable())
         th.join();
   }

   void Enqueue(Job&& j)
   {
      {
         std::lock_guard<std::mutex> lk(mu);
         if (stop)
            return;
         if (j.thumbs)
         {
            auto it = std::find_if(jobs.begin(), jobs.end(), [](const Job& o) { return !o.thumbs; });
            jobs.insert(it, std::move(j));
         }
         else
            jobs.push_back(std::move(j));
         if (!th.joinable())
            th = std::thread([this]() { Run(); });
      }
      cv.notify_one();
   }

   bool Stale(int pad, uint32_t g) const
   {
      return stopFlag.load(std::memory_order_acquire) || gen[pad].load(std::memory_order_acquire) != g;
   }

   void Run()
   {
      for (;;)
      {
         Job job;
         {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [this]() { return stop || !jobs.empty(); });
            if (stop)
               return;
            job = std::move(jobs.front());
            jobs.pop_front();
         }
         if (Stale(job.pad, job.gen))
            continue;
         Result r;
         r.thumbs = job.thumbs;
         r.pad = job.pad;
         r.gen = job.gen;
         if (job.thumbs)
            r.ok = BuildAtlas(job, r.px);
         else
         {
            r.buf = std::make_unique<Platform::SampleBuffer>();
            std::string error;
            r.ok = Platform::DecodeVideoAudioTrackToBuffer(job.path, *r.buf, error); // no audio track: a normal case
            if (!r.ok)
               r.buf.reset();
         }
         std::lock_guard<std::mutex> lk(mu);
         if (!stop)
            done.push_back(std::move(r));
      }
   }

   // Eight frames across the clip, cover-cropped to 16:9 and nearest sampled
   // into one atlas. Rows stay in the decoder's order.
   bool BuildAtlas(const Job& job, std::vector<unsigned char>& out)
   {
      std::string error;
      Platform::VideoHandle* h = Platform::VideoOpen(job.path, error);
      if (h == nullptr)
         return false;
      bool ok = false;
      const int w = Platform::VideoWidth(h), hgt = Platform::VideoHeight(h);
      const double dur = Platform::VideoDuration(h) > 0.0 ? Platform::VideoDuration(h) : job.duration;
      if (w > 0 && hgt > 0 && dur > 0.0)
      {
         const int aw = kThumbW * kThumbs;
         std::vector<unsigned char> atlas((size_t)aw * kThumbH * 4, 0);
         double cw = (double)w, ch = (double)hgt;
         const double ta = (double)kThumbW / (double)kThumbH;
         if (cw / ch > ta)
            cw = ch * ta;
         else
            ch = cw / ta;
         const double cx = ((double)w - cw) * 0.5, cy = ((double)hgt - ch) * 0.5;
         std::vector<unsigned char> px;
         for (int i = 0; i < kThumbs; i++)
         {
            if (Stale(job.pad, job.gen))
               break;
            const double t = ((double)i + 0.5) / (double)kThumbs * dur;
            if (!Platform::VideoFrameAtExact(h, t, px) || px.size() < (size_t)w * hgt * 4)
               continue;
            for (int y = 0; y < kThumbH; y++)
            {
               const int sy = std::clamp((int)(cy + ((double)y + 0.5) / kThumbH * ch), 0, hgt - 1);
               for (int x = 0; x < kThumbW; x++)
               {
                  const int sx = std::clamp((int)(cx + ((double)x + 0.5) / kThumbW * cw), 0, w - 1);
                  const unsigned char* sp = &px[((size_t)sy * w + sx) * 4];
                  unsigned char* d = &atlas[((size_t)y * aw + (size_t)(i * kThumbW + x)) * 4];
                  d[0] = sp[0];
                  d[1] = sp[1];
                  d[2] = sp[2];
                  d[3] = 255;
               }
            }
         }
         if (!Stale(job.pad, job.gen))
         {
            out.swap(atlas);
            ok = true;
         }
      }
      Platform::VideoClose(h);
      return ok;
   }
};

VmpcNode::VmpcNode() : mAudioNode(std::make_unique<AudioVmpcNode>()), mWorker(std::make_unique<Worker>())
{
   for (int p = 0; p < kPads; p++)
   {
      padMode[p] = kOneShot;
      padSync[p] = kFree;
      padDiv[p] = (int)MusicTime::kQuarter;
      for (int k = 0; k < kNumPadParams; k++)
         *PadParamPtr(p, k) = Info(k).def;
   }
}

VmpcNode::~VmpcNode()
{
   // Cancel and join first: a result must not outlive the buffers it would be handed to.
   mWorker->Stop();
   for (unsigned int& t : mThumbTex)
      ReleaseTex(t); // deferred when no context is current
   ReleaseTex(mBlackTex);
   if (glfwGetCurrentContext() != nullptr)
   {
      if (mProg != 0)
         glDeleteProgram(mProg);
      GLUtil::DestroyFbo(mOut);
   }
}

AudioNode* VmpcNode::GetAudioNode()
{
   return mAudioNode.get();
}

float VmpcNode::PadCursor(int pad) const
{
   pad = Clamp(pad);
   if (!mRun[pad].playing || mDuration[pad] <= 0.0)
      return -1.0f;
   return (float)std::clamp(mRun[pad].pos / mDuration[pad], 0.0, 1.0);
}

void VmpcNode::SetPadHeld(int pad, bool held, float /*velocity*/)
{
   pad = Clamp(pad);
   if (mHeld[pad] == held)
      return;
   mHeld[pad] = held;
   HandleEdge(pad, held);
}

bool VmpcNode::LoadPad(int pad, const std::string& path)
{
   pad = Clamp(pad);
   if (path.empty())
      return false;
   auto player = std::make_unique<VideoSourceNode>();
   player->audioEnabled = false;
   player->loop = true;
   bool hap = false;
   if (Hap::Reader::Probe(path))
   {
      if (!player->OpenFromHandle(path, nullptr, nullptr))
         return false;
      hap = true;
   }
   else
   {
      std::string error;
      Platform::VideoHandle* handle = Platform::VideoOpen(path, error);
      if (handle == nullptr || !player->OpenFromHandle(path, handle, nullptr))
         return false;
   }
   if (player->Duration() <= 0.0)
      return false;

   mPlayer[pad] = std::move(player);
   mDuration[pad] = mPlayer[pad]->Duration();
   mHap[pad] = hap;
   padPath[pad] = path;
   const size_t slash = path.find_last_of("/\\");
   padName[pad] = slash == std::string::npos ? path : path.substr(slash + 1);
   const uint64_t order = mRun[pad].order;
   mRun[pad] = PadRun();
   mRun[pad].order = order;
   mWorker->gen[pad].fetch_add(1, std::memory_order_acq_rel); // abandons the old clip's jobs
   QueueThumbs(pad);
   // The old clip's soundtrack is gone; the new one decodes when the toggle wants it.
   mSoundDecoded[pad] = false;
   mSoundSkipped[pad] = false;
   mSoundBytes[pad] = 0;
   mAudioNode->PushBuffer(pad, new Platform::SampleBuffer());
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
         ext.erase(0, 1);
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
         loaded++;
   return loaded;
}

void VmpcNode::ClearPad(int pad)
{
   pad = Clamp(pad);
   mPlayer[pad].reset();
   mDuration[pad] = 0.0;
   mHap[pad] = false;
   padPath[pad].clear();
   padName[pad].clear();
   mWorker->gen[pad].fetch_add(1, std::memory_order_acq_rel);
   mThumbPx[pad].clear();
   mThumbDirty[pad] = false;
   mThumbLoading[pad] = false;
   ReleaseTex(mThumbTex[pad]);
   mRun[pad] = PadRun();
   mSoundDecoded[pad] = false;
   mSoundSkipped[pad] = false;
   mSoundBytes[pad] = 0;
   mAudioNode->PushBuffer(pad, new Platform::SampleBuffer());
   // Memory came back: pads that were over budget get another go.
   for (int q = 0; q < kPads; q++)
      if (mSoundSkipped[q])
      {
         mSoundSkipped[q] = false;
         mSoundDecoded[q] = false;
      }
}

void VmpcNode::ReloadFromPaths()
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

// The filmstrip is built on the worker (a HAP clip has no platform handle to
// read stills from, so it has none). Until it lands the pad shows a loading state.
void VmpcNode::QueueThumbs(int pad)
{
   mThumbPx[pad].clear();
   mThumbDirty[pad] = true;
   mThumbLoading[pad] = false;
   if (mHap[pad])
      return;
   Worker::Job j;
   j.thumbs = true;
   j.pad = pad;
   j.gen = mWorker->gen[pad].load(std::memory_order_acquire);
   j.path = padPath[pad];
   j.duration = mDuration[pad];
   mThumbLoading[pad] = true;
   mWorker->Enqueue(std::move(j));
}

// Main thread, once per frame: adopt finished worker jobs. Results for a pad
// that was reloaded or cleared since are dropped.
void VmpcNode::PumpWorker()
{
   std::vector<Worker::Result> got;
   {
      std::lock_guard<std::mutex> lk(mWorker->mu);
      got.swap(mWorker->done);
   }
   for (Worker::Result& r : got)
   {
      if (r.gen != mWorker->gen[r.pad].load(std::memory_order_acquire))
         continue;
      if (r.thumbs)
      {
         mThumbLoading[r.pad] = false;
         if (r.ok)
         {
            mThumbPx[r.pad].swap(r.px);
            mThumbDirty[r.pad] = true;
         }
      }
      else if (r.ok && r.buf)
      {
         mSoundBytes[r.pad] = r.buf->channelData.size() * sizeof(float);
         mAudioNode->PushBuffer(r.pad, r.buf.release());
      }
      else
         mSoundBytes[r.pad] = 0;
   }
}

unsigned int VmpcNode::ThumbTexture(int pad)
{
   pad = Clamp(pad);
   FlushGrave();
   if (mThumbDirty[pad] && glfwGetCurrentContext() != nullptr)
   {
      mThumbDirty[pad] = false;
      if (mThumbPx[pad].empty())
         ReleaseTex(mThumbTex[pad]);
      else
      {
         if (mThumbTex[pad] == 0)
            glGenTextures(1, &mThumbTex[pad]);
         glBindTexture(GL_TEXTURE_2D, mThumbTex[pad]);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kThumbW * kThumbs, kThumbH, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      mThumbPx[pad].data());
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glBindTexture(GL_TEXTURE_2D, 0);
      }
   }
   return PadLoaded(pad) ? mThumbTex[pad] : 0;
}

// ---- pad engine (main thread) ----------------------------------------------

void VmpcNode::Window(int pad, double& s, double& e) const
{
   const double dur = mDuration[pad];
   s = std::clamp((double)padStart[pad], 0.0, 100.0) / 100.0 * dur;
   e = std::clamp((double)padEnd[pad], 0.0, 100.0) / 100.0 * dur;
   if (e < s)
      std::swap(s, e);
   if (e - s < kMinWindow)
   {
      s = 0.0;
      e = dur;
   }
   // The decoder wraps at the clip's end: stay a hair inside it.
   e = std::max(s, std::min(e, dur - 1e-3));
}

void VmpcNode::StartPass(int pad)
{
   double s, e;
   Window(pad, s, e);
   PadRun& r = mRun[pad];
   r.pos = padSpeed[pad] < 0.0f ? e : s;
   r.playing = true;
   r.relT = -1.0;
   r.holdEnd = false;
   r.serial++;
}

void VmpcNode::Fire(int pad)
{
   PadRun& r = mRun[pad];
   if (padMode[pad] == kLoopToggle)
   {
      if (r.loopOn)
      {
         r.loopOn = false;
         if (r.playing && r.relT < 0.0)
            r.relT = 0.0;
         return;
      }
      r.loopOn = true;
      Transport& tr = Transport::Instance();
      const double g = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(
                                         padDiv[pad], 0, (int)MusicTime::kNumRateDivisions - 1)));
      r.lastLine = (padSync[pad] == kSynced && tr.IsPlaying()) ? std::floor(tr.Beats() / g + 1e-6) : -1.0;
   }
   StartPass(pad);
   r.order = ++mOrder;
}

void VmpcNode::HandleEdge(int pad, bool down)
{
   pad = Clamp(pad);
   if (!PadLoaded(pad))
      return;
   PadRun& r = mRun[pad];
   Transport& tr = Transport::Instance();
   if (down)
   {
      if (padSync[pad] == kSynced && tr.IsPlaying())
      {
         if (!r.pending)
         {
            const double g = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(
                                               padDiv[pad], 0, (int)MusicTime::kNumRateDivisions - 1)));
            r.pending = true;
            r.pendingBeat = std::ceil(tr.Beats() / g - 1e-6) * g;
         }
         return;
      }
      Fire(pad);
      return;
   }
   if (padMode[pad] != kGate)
      return;
   if (r.pending)
      r.pending = false; // released before the line: the hit never happens
   else if (r.playing && r.relT < 0.0)
      r.relT = 0.0;
}

void VmpcNode::Engine(double dt)
{
   Transport& tr = Transport::Instance();
   const bool trPlaying = tr.IsPlaying();
   const double beat = tr.Beats();

   for (int p = 0; p < kPads; p++)
   {
      PadRun& r = mRun[p];
      if (!PadLoaded(p))
      {
         r.playing = false;
         r.pending = false;
         r.loopOn = false;
         r.env = 0.0f;
         continue;
      }
      const bool synced = padSync[p] == kSynced;
      const double g = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(
                                         padDiv[p], 0, (int)MusicTime::kNumRateDivisions - 1)));
      if (r.pending && (!trPlaying || beat >= r.pendingBeat - 1e-6))
      {
         r.pending = false;
         Fire(p);
      }
      if (r.loopOn && padMode[p] != kLoopToggle)
      {
         r.loopOn = false; // the mode moved away from loop: let it go
         if (r.playing && r.relT < 0.0)
            r.relT = 0.0;
      }
      // A synced loop restarts its pass on every division while the transport runs.
      if (r.loopOn && synced && trPlaying)
      {
         const double line = std::floor(beat / g + 1e-6);
         if (r.lastLine >= 0.0 && line != r.lastLine)
            StartPass(p);
         r.lastLine = line;
      }
      else
         r.lastLine = -1.0;

      if (!r.playing)
         continue;
      double s, e;
      Window(p, s, e);
      const double sp = (double)padSpeed[p];
      r.pos += dt * sp;
      if ((sp >= 0.0 && r.pos >= e) || (sp < 0.0 && r.pos <= s))
      {
         if (r.loopOn && !(synced && trPlaying) && e - s > 1e-6)
         {
            const double range = e - s;
            r.pos = s + std::fmod(r.pos - s, range);
            if (r.pos < s)
               r.pos += range;
            r.serial++;
         }
         else if (r.loopOn && synced && trPlaying && r.relT < 0.0)
         {
            // A synced loop shorter than its division: hold the last frame (and
            // keep the pad on top) until the next line restarts it, instead of
            // flashing black every cycle.
            r.pos = e;
            r.holdEnd = true;
         }
         else
         {
            r.pos = std::clamp(r.pos, s, e);
            r.playing = false;
         }
      }
      const double fo = std::max(0.0, (double)padFadeOut[p] / 1000.0);
      float rel = 1.0f;
      if (r.playing && r.relT >= 0.0)
      {
         r.relT += dt;
         if (fo <= 0.0 || r.relT >= fo)
            r.playing = false;
         else
            rel = (float)(1.0 - r.relT / fo);
      }
      if (!r.playing)
         continue; // env keeps its last value: a held or outgoing frame shows as it ended
      const double a = std::fabs(sp);
      const double fi = std::max(0.0, (double)padFadeIn[p] / 1000.0);
      const double dIn = sp >= 0.0 ? r.pos - s : e - r.pos;
      const double dOut = sp >= 0.0 ? e - r.pos : r.pos - s;
      const bool held = r.holdEnd;
      const float eIn = (!held && fi > 0.0 && a > 1e-6) ? (float)std::clamp(dIn / (a * fi), 0.0, 1.0) : 1.0f;
      const float eOut = (!held && fo > 0.0 && a > 1e-6) ? (float)std::clamp(dOut / (a * fo), 0.0, 1.0) : 1.0f;
      r.env = std::clamp(padOpacity[p], 0.0f, 1.0f) * eIn * eOut * rel;
   }

   // ---- what is on screen ----
   if (mTop >= 0 && !PadLoaded(mTop))
   {
      mTop = -1;
      mHolding = false;
   }
   if (mUnder >= 0 && !PadLoaded(mUnder))
      mUnder = -1;
   int best = -1;
   for (int p = 0; p < kPads; p++)
      if (mRun[p].playing && (best < 0 || mRun[p].order > mRun[best].order))
         best = p;
   const bool instant = transition == kCut || transitionMs <= 0.0f;
   if (best >= 0)
   {
      if (best != mTop || mHolding)
      {
         if (best != mTop)
         {
            mUnder = mTop;
            mTop = best;
            mT = instant ? 1.0 : 0.0;
         }
         mHolding = false;
      }
   }
   else if (mTop >= 0)
   {
      if (holdLast)
         mHolding = true;
      else
      {
         mUnder = mTop;
         mTop = -1;
         mT = instant ? 1.0 : 0.0;
         mHolding = false;
      }
   }
   if (mT < 1.0)
      mT = std::min(1.0, mT + dt / std::max(1e-3, (double)transitionMs / 1000.0));
   if (mT >= 1.0)
      mUnder = -1;

   const float envTop = mTop >= 0 ? mRun[mTop].env : 0.0f;
   const float envUnder = mUnder >= 0 ? mRun[mUnder].env : 0.0f;
   const float t = (float)mT;
   if (mUnder < 0 || transition == kCut)
   {
      // No outgoing picture: a fresh top fades up from black (crossfade/dip), a cut is instant.
      mWt = envTop * (transition == kCut ? 1.0f : t);
      mWb = 0.0f;
   }
   else if (transition == kCrossfade)
   {
      mWt = t * envTop;
      mWb = (1.0f - t) * envUnder;
   }
   else // dip to black
   {
      mWt = t < 0.5f ? 0.0f : (2.0f * t - 1.0f) * envTop;
      mWb = t < 0.5f ? (1.0f - 2.0f * t) * envUnder : 0.0f;
   }
   if (mTop < 0)
      mWt = 0.0f;
}

void VmpcNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   mAudioNode->DrainRetired();
   FlushGrave();
   PumpWorker();

   PadEvt ev;
   while (mAudioNode->PopEvent(ev))
      HandleEdge(ev.pad, ev.down);

   // The transport's clock while it plays (pausing freezes the pads with
   // everything else), wall time while it is stopped (pads still play).
   Transport& tr = Transport::Instance();
   const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   const double trNow = tr.Seconds();
   double dt = 0.0;
   if (mTestDt >= 0.0)
      dt = mTestDt;
   else if (tr.IsPlaying())
      dt = mWasPlaying ? trNow - mLastTransport : 0.0;
   else
      dt = mLastWall > 0.0 ? wall - mLastWall : 0.0;
   dt = std::clamp(dt, 0.0, 0.25);
   mWasPlaying = tr.IsPlaying();
   mLastTransport = trNow;
   mLastWall = wall;

   Engine(dt);
   if (soundtrack)
      DecodeSoundtracks();
   for (int p = 0; p < kPads; p++)
   {
      float gain = 0.0f;
      if (soundtrack && mRun[p].playing)
         gain = mRun[p].holdEnd ? 0.0f : (p == mTop ? mWt : (p == mUnder ? mWb : 0.0f));
      mAudioNode->Publish(p, mRun[p].pos, padSpeed[p], gain, mRun[p].serial);
   }
   Composite(frameId);
}

// Soundtracks decode on the worker, one at a time, so turning the toggle on
// with 16 clips loaded never stalls a frame and never holds more than the
// budget (kSoundBudgetBytes across all pads: queued reservations plus decoded
// buffers). The platform decode is whole-track, so a clip is sized from its
// duration first (stereo 48 kHz floats as the upper bound); one that would not
// fit is left silent, and the card says so. Clearing a pad retries those.
void VmpcNode::DecodeSoundtracks()
{
   size_t used = 0;
   for (int p = 0; p < kPads; p++)
      used += mSoundBytes[p];
   for (int p = 0; p < kPads; p++)
   {
      if (mSoundDecoded[p] || !PadLoaded(p))
         continue;
      mSoundDecoded[p] = true;
      const size_t est = (size_t)(mDuration[p] * 48000.0) * 2u * sizeof(float);
      if (used + est > kSoundBudgetBytes)
      {
         mSoundSkipped[p] = true;
         continue;
      }
      mSoundBytes[p] = est;
      used += est;
      Worker::Job j;
      j.thumbs = false;
      j.pad = p;
      j.gen = mWorker->gen[p].load(std::memory_order_acquire);
      j.path = padPath[p];
      j.duration = mDuration[p];
      mWorker->Enqueue(std::move(j));
   }
}

unsigned int VmpcNode::GetOutputTexture()
{
   if (mOut.tex == 0 && glfwGetCurrentContext() != nullptr)
      Composite(mLastCookFrame);
   return mOut.tex;
}

void VmpcNode::Composite(int frameId)
{
   if (glfwGetCurrentContext() == nullptr)
      return;

   // Decode set: the two pictures on screen, then the most recent playing pads.
   int set[kDecodeSet];
   int count = 0;
   auto add = [&](int p)
   {
      if (p < 0 || count >= kDecodeSet || !mPlayer[p])
         return;
      for (int i = 0; i < count; i++)
         if (set[i] == p)
            return;
      set[count++] = p;
   };
   add(mTop);
   add(mUnder);
   for (int n = 0; n < kDecodeSet; n++)
   {
      int best = -1;
      for (int p = 0; p < kPads; p++)
      {
         bool taken = false;
         for (int i = 0; i < count; i++)
            taken = taken || set[i] == p;
         if (!taken && mRun[p].playing && mPlayer[p] && (best < 0 || mRun[p].order > mRun[best].order))
            best = p;
      }
      add(best);
   }
   for (int i = 0; i < count; i++)
   {
      const int p = set[i];
      VideoSourceNode* pl = mPlayer[p].get();
      // A stopped pad's picture is already the frame it stopped on.
      if ((!mRun[p].playing || mRun[p].holdEnd) && pl->FrameUpdateCount() > 0)
         continue;
      pl->SeekTo(std::clamp(mRun[p].pos, 0.0, std::max(0.0, mDuration[p] - 1e-3)));
      pl->CookIfNeeded(frameId);
   }

   if (mBlackTex == 0)
   {
      const unsigned char black[4] = { 0, 0, 0, 255 };
      glGenTextures(1, &mBlackTex);
      glBindTexture(GL_TEXTURE_2D, mBlackTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   auto picture = [&](int p, unsigned int& tex, int& w, int& h)
   {
      tex = mBlackTex;
      w = h = 0;
      if (p < 0 || !mPlayer[p] || mPlayer[p]->FrameUpdateCount() == 0)
         return; // nothing decoded yet: black, never the decoder's checker placeholder
      tex = mPlayer[p]->GetOutputTexture();
      w = mPlayer[p]->GetOutputWidth();
      h = mPlayer[p]->GetOutputHeight();
   };
   unsigned int topTex, underTex;
   int tw, th, uw, uh;
   picture(mTop, topTex, tw, th);
   picture(mUnder, underTex, uw, uh);
   int w = tw > 0 ? tw : (uw > 0 ? uw : (mOut.w > 0 ? mOut.w : 64));
   int h = tw > 0 ? th : (uw > 0 ? uh : (mOut.h > 0 ? mOut.h : 36));

   if (!mProgTried)
   {
      mProgTried = true;
      mProg = GLUtil::CompileProgram(kCompositeFrag);
   }
   if (mProg == 0 || !GLUtil::EnsureFbo(mOut, w, h, GL_RGBA8, "VMPC"))
      return;
   float sx = 1.0f, sy = 1.0f;
   if (tw > 0 && th > 0 && uw > 0 && uh > 0)
   {
      const float ta = (float)tw / (float)th, ua = (float)uw / (float)uh;
      if (ua > ta)
         sx = ta / ua;
      else
         sy = ua / ta;
   }
   const float wt = mWt, wb = mWb;
   GLUtil::RunShaderPass(mOut, mProg, [this, topTex, underTex, wt, wb, sx, sy]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, topTex);
      glUniform1i(glGetUniformLocation(mProg, "uTop"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, underTex);
      glUniform1i(glGetUniformLocation(mProg, "uUnder"), 1);
      glUniform1f(glGetUniformLocation(mProg, "uWt"), wt);
      glUniform1f(glGetUniformLocation(mProg, "uWb"), wb);
      glUniform2f(glGetUniformLocation(mProg, "uUnderScale"), sx, sy);
      glActiveTexture(GL_TEXTURE0);
   });
}

void VmpcNode::VisitParams(ParamVisitor& v)
{
   char key[32];
   static const char* kKeys[kNumPadParams] = { "opacity", "speed", "start", "end", "fadein", "fadeout" };
   for (int p = 0; p < kPads; p++)
   {
      snprintf(key, sizeof(key), "pad%d_path", p);
      v.Text(key, padPath[p]);
      snprintf(key, sizeof(key), "pad%d_mode", p);
      v.Int(key, padMode[p]);
      snprintf(key, sizeof(key), "pad%d_sync", p);
      v.Int(key, padSync[p]);
      snprintf(key, sizeof(key), "pad%d_div", p);
      v.Int(key, padDiv[p]);
      for (int k = 0; k < kNumPadParams; k++)
      {
         snprintf(key, sizeof(key), "pad%d_%s", p, kKeys[k]);
         v.Float(key, *PadParamPtr(p, k));
      }
   }
   v.Int("transition", transition);
   v.Float("transitionMs", transitionMs);
   v.Bool("holdLast", holdLast);
   v.Bool("soundtrack", soundtrack);
   v.Int("selectedPad", selectedPad);
}

void VmpcNode::SweepPrepare()
{
   // No clips headless: give every pad a one-second synthetic soundtrack and
   // a duration, so a note (any note, in this mode) plays every pad and the
   // soundtrack-facing params can be heard.
   mAudioNode->SetTestAllPads(true);
   soundtrack = true;
   for (int p = 0; p < kPads; p++)
   {
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
      mAudioNode->PushBuffer(p, buf);
      mDuration[p] = 1.0;
      mSoundDecoded[p] = true;
      if (padPath[p].empty())
         padPath[p] = "sweep";
   }
}
