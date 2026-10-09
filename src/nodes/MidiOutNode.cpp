#include "MidiOutNode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"
#include "platform/Platform.h"

// ------------------------------------------------------------------ audio half
class AudioMidiOutNode : public AudioNode
{
public:
   explicit AudioMidiOutNode(MidiOutSink& sink) : mSink(sink)
   {
      for (auto& c : mHeldChannel) c = kNone;
   }

   // The owner is going away. Audio no longer calls ProcessBlock, so this thread is the only
   // producer: close every note still sounding (D9) before the sink's Close delivers them.
   ~AudioMidiOutNode() override { ReleaseAll(Platform::MidiOutNowSeconds()); }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override { mSampleRate = sampleRate; }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      NoteEvent evts[64];
      const double now = Platform::MidiOutNowSeconds();

      // Transport stop: let go of everything that was sounding (D9).
      const bool playing = Transport::Instance().IsPlaying();
      if (mWasPlaying && !playing)
         ReleaseAll(now);
      mWasPlaying = playing;

      const int panicSeq = mPanicSeq.load(std::memory_order_acquire);
      if (panicSeq != mSeenPanic)
      {
         mSeenPanic = panicSeq;
         SendPanic(now);
      }
      if (mFlushRequest.exchange(false, std::memory_order_acq_rel))
         ReleaseAll(now);

      // Always drain, even with nothing open: a cursor nobody reads makes the producer's ring
      // skip forward later, and a held-note table must not outlive its device.
      int n;
      do
      {
         n = mInbox != nullptr ? mInbox->Pop(mNoteCursor, evts, 64) : 0;
         if (!mSink.IsOpen())
         {
            ForgetHeld();
            continue;
         }
         const int ch = std::clamp(mChannel.load(std::memory_order_relaxed), 1, 16) - 1;
         const double sr = mSampleRate > 0.0 ? mSampleRate : 44100.0;
         for (int i = 0; i < n; i++)
         {
            const NoteEvent& e = evts[i];
            if (e.bendUpdate)
               continue; // pitch bend out is D11, out of scope for v1
            const int note = std::clamp(e.note, 0, 127);
            const double at = now + (double)std::max(0, e.frameOffset) / sr;
            if (e.isNoteOn)
            {
               // A retrigger of a sounding pitch: close the old one first, or the hardware
               // stacks two voices and the single note-off below only ends one.
               if (mHeldChannel[note] != kNone)
                  Send(0x80 | mHeldChannel[note], note, 0, at);
               else
                  mHeldCount.fetch_add(1, std::memory_order_relaxed);
               const int vel = std::clamp((int)std::lround(e.velocity * 127.0f), 1, 127);
               Send(0x90 | ch, note, vel, at);
               mHeldChannel[note] = (uint8_t)ch; // note-off goes to the channel its on used
            }
            else if (mHeldChannel[note] != kNone)
            {
               Send(0x80 | mHeldChannel[note], note, 0, at);
               mHeldChannel[note] = kNone;
               mHeldCount.fetch_sub(1, std::memory_order_relaxed);
            }
         }
      } while (n == 64);
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override { mInbox = inbox; mNoteCursor = cursor; }

   // Main thread only.
   void SetChannel(int ch1to16) { mChannel.store(ch1to16, std::memory_order_relaxed); }
   void RequestPanic() { mPanicSeq.fetch_add(1, std::memory_order_release); }
   void RequestFlush() { mFlushRequest.store(true, std::memory_order_release); }
   int HeldCount() const { return mHeldCount.load(std::memory_order_relaxed); }

private:
   static constexpr uint8_t kNone = 0xFF;

   void Send(int status, int d1, int d2, double at)
   {
      const unsigned char b[3] = { (unsigned char)status, (unsigned char)d1, (unsigned char)d2 };
      mSink.Push(b, 3, at);
   }

   void ForgetHeld()
   {
      for (auto& c : mHeldChannel) c = kNone;
      mHeldCount.store(0, std::memory_order_relaxed);
   }

   // Note-off for every held note, on the channel it started on.
   void ReleaseAll(double at)
   {
      if (mSink.IsOpen())
         for (int note = 0; note < 128; note++)
            if (mHeldChannel[note] != kNone)
               Send(0x80 | mHeldChannel[note], note, 0, at);
      ForgetHeld();
   }

   // CC 123 (all notes off) and CC 120 (all sound off) on all 16 channels: the standard
   // panic, for a synth stuck on a note this node never tracked (D9).
   void SendPanic(double at)
   {
      ForgetHeld();
      if (!mSink.IsOpen())
         return;
      for (int ch = 0; ch < 16; ch++)
      {
         Send(0xB0 | ch, 123, 0, at);
         Send(0xB0 | ch, 120, 0, at);
      }
   }

   MidiOutSink& mSink;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;
   double mSampleRate = 44100.0;
   bool mWasPlaying = false;
   int mSeenPanic = 0;
   uint8_t mHeldChannel[128];
   std::atomic<int> mChannel { 1 };
   std::atomic<int> mPanicSeq { 0 };
   std::atomic<bool> mFlushRequest { false };
   std::atomic<int> mHeldCount { 0 };
};

// ------------------------------------------------------------------ main-thread half
MidiOutNode::MidiOutNode() = default;
MidiOutNode::~MidiOutNode() = default; // members unwind in the order the header documents

AudioMidiOutNode& MidiOutNode::Audio()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMidiOutNode>(mSink);
   return *mAudioNode;
}

AudioNode* MidiOutNode::AudioNodeForNotePorts()
{
   return &Audio();
}

void MidiOutNode::VisitParams(ParamVisitor& v)
{
   v.Text("device", device);
   v.Int("channel", channel);
}

void MidiOutNode::Panic()
{
   Audio().RequestPanic();
}

void MidiOutNode::RefreshDeviceList()
{
   mDevices = Platform::MidiOutListDevices();
}

void MidiOutNode::UseTestSink(std::function<void(const MidiOutSink::Msg&)> fn)
{
   mUsingTestSink = true;
   mOpenedDevice.clear();
   mDeviceError.clear();
   mSink.OpenTest(std::move(fn));
}

// Opens, switches or retries the destination. Runs on the main thread from CookIfNeeded.
void MidiOutNode::ServiceDevice()
{
   if (mUsingTestSink)
      return;

   if (device == mOpenedDevice && (mSink.IsOpen() || device.empty()))
   {
      mSwitchWait = 0;
      return;
   }

   // Changing away from an open device: release what is sounding on the old one first, and give
   // the audio thread a short window to do it (about half a second of cooks). If the engine is
   // not running there is nothing sounding that we could still end, so we just move on.
   if (mSink.IsOpen() && Audio().HeldCount() > 0 && mSwitchWait < 30)
   {
      if (mSwitchWait++ == 0)
         Audio().RequestFlush();
      return;
   }
   mSwitchWait = 0;

   if (device.empty())
   {
      mSink.Close();
      mOpenedDevice.clear();
      mDeviceError.clear();
      return;
   }
   if (mRetryIn > 0 && device == mOpenedDevice)
   {
      mRetryIn--;
      return;
   }

   std::string err;
   mOpenedDevice = device; // remember the attempt so the retry timer, not this branch, paces it
   if (mSink.Open(device, err))
   {
      mDeviceError.clear();
      mRetryIn = 0;
   }
   else
   {
      mDeviceError = err;
      mRetryIn = 60; // about a second; the device may simply not be plugged in yet
   }
}

void MidiOutNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   channel = std::clamp(channel, 1, 16);
   ServiceDevice();
   Audio().SetChannel(channel);
}
