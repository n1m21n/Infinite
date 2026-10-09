#pragma once

// The only legal way for the audio thread to send MIDI to a device (docs/plans/midi-out D5).
//
// OS MIDI calls allocate and take locks, so the audio thread cannot make them. It pushes
// {bytes, deliver time} into a lock-free single-producer ring here; a thread owned by this
// sink drains the ring and calls Platform::MidiOutSend (or, in tests, a fake). One sink per
// MIDI Out node, so each node has its own device, ring and thread.
//
// Threads:
//   main      Open / OpenTest / Close / Error / IsOpen
//   audio     Push (one producer only)
//   sink's own thread    drains, never touches the audio thread's data

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace Platform { struct MidiOutHandle; }

class MidiOutSink
{
public:
   struct Msg
   {
      double at = 0.0; // Platform::MidiOutNowSeconds() timeline
      unsigned char bytes[3] = { 0, 0, 0 };
      unsigned char len = 0;
   };

   MidiOutSink() = default;
   ~MidiOutSink() { Close(); }
   MidiOutSink(const MidiOutSink&) = delete;
   MidiOutSink& operator=(const MidiOutSink&) = delete;

   // Main thread. Closes whatever was open, opens the named destination and starts the
   // drain thread. False with Error() set when the device is gone or busy. The name is
   // kept either way so a later Open(sameName) after replugging works (D10).
   bool Open(const std::string& deviceName, std::string& outError);

   // Main thread. Same ring and thread with a fake in place of the OS, for the headless
   // byte-stream tests: no hardware in CI.
   void OpenTest(std::function<void(const Msg&)> sink);

   // Main thread. Delivers what is already queued, stops the thread (joinable() is the
   // predicate, never a running flag) and closes the device. Once the audio thread has
   // stopped calling Push, the caller may push a last batch (note-offs) before Close.
   void Close();

   bool IsOpen() const { return mOpen.load(std::memory_order_acquire); }

   // Audio thread, single producer. False (and counted) when the ring is full or nothing is
   // open; the caller must not retry. Lock-free, no allocation.
   bool Push(const unsigned char* bytes, int len, double atSeconds);

   // Messages handed to the OS or the test sink since Open; the node's activity LED.
   unsigned long long SentCount() const { return mSent.load(std::memory_order_relaxed); }
   unsigned long long DroppedCount() const { return mDropped.load(std::memory_order_relaxed); }

private:
   void Run();
   void Start();

   static constexpr unsigned kCapacity = 1024; // power of two; ~1 s of dense CC traffic
   Msg mRing[kCapacity];
   std::atomic<unsigned> mHead { 0 }; // written by the producer
   std::atomic<unsigned> mTail { 0 }; // written by the drain thread

   Platform::MidiOutHandle* mHandle = nullptr;
   std::function<void(const Msg&)> mTestSink;
   std::thread mThread;
   std::atomic<bool> mStop { false };
   std::atomic<bool> mOpen { false };
   std::atomic<unsigned long long> mSent { 0 };
   std::atomic<unsigned long long> mDropped { 0 };
};
