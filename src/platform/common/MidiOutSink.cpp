#include "MidiOutSink.h"

#include "../Platform.h"

#include <chrono>

bool MidiOutSink::Open(const std::string& deviceName, std::string& outError)
{
   Close();
   Platform::MidiOutHandle* h = Platform::MidiOutOpen(deviceName, outError);
   if (!h)
      return false;
   mHandle = h;
   Start();
   return true;
}

void MidiOutSink::OpenTest(std::function<void(const Msg&)> sink)
{
   Close();
   mTestSink = std::move(sink);
   Start();
}

void MidiOutSink::Start()
{
   mHead.store(0, std::memory_order_relaxed);
   mTail.store(0, std::memory_order_relaxed);
   mSent.store(0, std::memory_order_relaxed);
   mDropped.store(0, std::memory_order_relaxed);
   mStop.store(false, std::memory_order_release);
   mOpen.store(true, std::memory_order_release);
   mThread = std::thread([this] { Run(); });
}

void MidiOutSink::Close()
{
   // Stop accepting first; what is already queued is still delivered (see Run).
   mOpen.store(false, std::memory_order_release);
   mStop.store(true, std::memory_order_release);
   if (mThread.joinable())
      mThread.join();
   if (mHandle)
   {
      Platform::MidiOutClose(mHandle);
      mHandle = nullptr;
   }
   mTestSink = nullptr;
}

bool MidiOutSink::Push(const unsigned char* bytes, int len, double atSeconds)
{
   if (!mOpen.load(std::memory_order_acquire) || len < 1 || len > 3)
      return false;
   const unsigned head = mHead.load(std::memory_order_relaxed);
   const unsigned tail = mTail.load(std::memory_order_acquire);
   if (head - tail >= kCapacity)
   {
      mDropped.fetch_add(1, std::memory_order_relaxed);
      return false;
   }
   Msg& m = mRing[head & (kCapacity - 1)];
   m.at = atSeconds;
   m.len = (unsigned char)len;
   for (int i = 0; i < len; i++)
      m.bytes[i] = bytes[i];
   mHead.store(head + 1, std::memory_order_release);
   return true;
}

void MidiOutSink::Run()
{
   while (true)
   {
      const unsigned tail = mTail.load(std::memory_order_relaxed);
      if (tail != mHead.load(std::memory_order_acquire))
      {
         const Msg m = mRing[tail & (kCapacity - 1)];
         // Delivered even while stopping: Close() must flush the note-offs the owner pushed
         // just before it (D9), so the thread only returns once the ring is empty.
         if (mTestSink)
            mTestSink(m);
         else
            Platform::MidiOutSend(mHandle, m.bytes, m.len, m.at);
         mSent.fetch_add(1, std::memory_order_relaxed);
         mTail.store(tail + 1, std::memory_order_release);
         continue;
      }
      if (mStop.load(std::memory_order_acquire))
         return;
      // The 1 ms poll the plan names. Polling keeps the audio thread's Push free of any
      // syscall; a condition variable's notify would not be.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
}
