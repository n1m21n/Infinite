// Windows implementation of the Platform facade's MIDI surface, replacing
// CoreMIDI:
//
//   - Every installed midiIn device is opened at MidiStart (matching the macOS
//     behavior of listening to every connected class-compliant source).
//   - CC/note values land in a mutex-guarded table keyed by
//     (device, channel, controller, isNote) - polled once a frame by the UI.
//   - The live note stream for synth nodes is its own lock-free ring written
//     on the WinMM callback thread and read from the audio thread with no
//     lock, exactly like the CoreMIDI reader did (monotonic write counter,
//     per-consumer cursors, skip-forward when a consumer falls behind).
//   - MIDI clock (0xF8) timing derives BPM from smoothed inter-pulse spacing;
//     Start/Stop reset the window so a restart doesn't average across the gap.
//
// Signatures come from ../Platform.h; MidiDeviceId maps to the WinMM device
// index plus one (0 stays "unbound").

#include "../Platform.h"

#include "WinCommon.h"

#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace
{
   using Clock = std::chrono::steady_clock;

   constexpr size_t kNoteRingCapacity = 4096; // power of two

   struct CcKey
   {
      unsigned int device;
      int channel;
      int controller;
      bool isNote;

      bool operator<(const CcKey& o) const
      {
         if (device != o.device) return device < o.device;
         if (channel != o.channel) return channel < o.channel;
         if (controller != o.controller) return controller < o.controller;
         return isNote < o.isNote;
      }
   };

   struct ChannelKey
   {
      unsigned int device;
      int channel;

      bool operator<(const ChannelKey& o) const
      {
         if (device != o.device) return device < o.device;
         return channel < o.channel;
      }
   };

   struct NoteRingSlot
   {
      // Plain storage: readers only touch slots the writer has already
      // published via the release-stored write counter, and a lapped reader
      // tolerates a torn slot by contract (see Platform.h).
      uint64_t seq = 0;
      Platform::MidiNoteMessage msg;
   };

   struct MidiState
   {
      std::mutex mutex;
      std::map<CcKey, float> values;
      std::map<CcKey, unsigned int> noteHits;
      std::map<ChannelKey, Platform::MidiLastNote> channelLast;
      Platform::MidiCCValue lastTouched;
      bool lastTouchedPending = false;
      std::vector<std::string> deviceNames;

      // Note ring - written on WinMM callback threads, read anywhere.
      //
      // MidiStart() opens EVERY installed device with CALLBACK_FUNCTION, and
      // WinMM delivers each open device's messages on its own thread. So this
      // is multi-producer, not the single-producer ring its load/store pair
      // was written for: two controllers could claim the same idx, write the
      // same slot, and lose one increment - dropped and torn notes exactly
      // when a performer has more than one controller plugged in.
      //
      // Producers are serialised on ringMutex rather than made lock-free:
      // note traffic is a few hundred messages a second at worst, and keeping
      // the single release-store on ringWrite preserves the publish barrier
      // the (single-consumer, lock-free) reader below already relies on.
      // This is deliberately NOT gState.mutex - the 0x80 Note Off path calls
      // PublishNote while already holding that one.
      std::mutex ringMutex;
      NoteRingSlot ring[kNoteRingCapacity];
      std::atomic<uint64_t> ringWrite{ 0 };
   };

   MidiState gState;
   std::atomic<bool> gRunning{ false };

   // Opened input handles, index = WinMM device id.
   std::vector<HMIDIIN> gHandles;

   // ---- MIDI clock ----------------------------------------------------------

   struct ClockTracker
   {
      std::mutex mutex;
      std::deque<double> pulseMs;       // steady-clock ms timestamps
      double bpm = 0.0f;
      Clock::time_point lastPulse{};
      bool haveLast = false;

      void Pulse()
      {
         const auto now = Clock::now();
         const double nowMs = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();

         std::lock_guard<std::mutex> lock(mutex);
         if (haveLast)
         {
            const double intervalMs =
               std::chrono::duration<double, std::milli>(now - lastPulse).count();
            // Plausible quarter-note-pulse spacing only (24 ppqn): 250 BPM ->
            // 10ms floor; 30 BPM -> 83ms ceiling. Filters stray garbage.
            if (intervalMs > 8.0 && intervalMs < 100.0)
            {
               pulseMs.push_back(nowMs);
               while (pulseMs.size() > 96)
                  pulseMs.pop_front();
               RecomputeBpmLocked();
            }
         }
         else
         {
            pulseMs.push_back(nowMs);
         }
         haveLast = true;
         lastPulse = now;
      }

      void Reset()
      {
         std::lock_guard<std::mutex> lock(mutex);
         pulseMs.clear();
         bpm = 0.0f;
         haveLast = false;
      }

      void RecomputeBpmLocked()
      {
         if (pulseMs.size() < 4)
            return;
         // Median-of-intervals: robust to a dropped or doubled pulse.
         std::vector<double> gaps;
         gaps.reserve(pulseMs.size());
         for (size_t i = 1; i < pulseMs.size(); i++)
         {
            const double g = pulseMs[i] - pulseMs[i - 1];
            if (g > 1.0)
               gaps.push_back(g);
         }
         if (gaps.empty())
            return;
         std::sort(gaps.begin(), gaps.end());
         const double median = gaps[gaps.size() / 2];
         bpm = (float)(60000.0 / (median * 24.0));
      }
   };

   ClockTracker gClock;

   void PublishNote(unsigned int deviceIdPlusOne, int channel, int note, float velocity01, bool isNoteOn)
   {
      std::lock_guard<std::mutex> lock(gState.ringMutex);
      const uint64_t idx = gState.ringWrite.load(std::memory_order_relaxed);
      NoteRingSlot& slot = gState.ring[idx % kNoteRingCapacity];
      slot.msg.device = deviceIdPlusOne;
      slot.msg.channel = channel;
      slot.msg.note = note;
      slot.msg.velocity01 = velocity01;
      slot.msg.isNoteOn = isNoteOn;
      slot.seq = idx + 1;
      gState.ringWrite.store(idx + 1, std::memory_order_release);
   }

   void HandleShortMessage(UINT deviceId, DWORD_PTR param)
   {
      const unsigned int dev = (unsigned int)deviceId + 1;
      const BYTE status = (BYTE)(param & 0xFF);

      // System Real-Time (0xF8..0xFF) and System Common (0xF0..0xF7) are NOT
      // channel messages: their low nibble is part of the message identity,
      // not a channel number. Masking with 0xF0 first can only ever yield
      // 0x80/0x90/0xA0/0xB0/0xC0/0xD0/0xE0/0xF0, so a switch on the masked
      // value can never reach 0xF8/0xFA/0xFC and every clock case below was
      // dead code - MidiClockIsPresent() was permanently false on Windows
      // while macOS (Platform.mm, which tests the raw status first) synced
      // fine. Dispatch on the full status byte before masking.
      if (status >= 0xF0)
      {
         switch (status)
         {
            case 0xF8: // Timing clock
               gClock.Pulse();
               break;
            case 0xFA: // Start
            case 0xFC: // Stop
               gClock.Reset();
               break;
            default:
               break;
         }
         return;
      }

      const BYTE type = status & 0xF0;
      const int channel = status & 0x0F;
      const BYTE d1 = (BYTE)((param >> 8) & 0xFF);
      const BYTE d2 = (BYTE)((param >> 16) & 0xFF);

      switch (type)
      {
         case 0x90: // Note On (velocity 0 == Note Off)
         {
            const bool realOn = d2 > 0;
            const float velocity01 = (float)d2 / 127.0f;

            {
               std::lock_guard<std::mutex> lock(gState.mutex);
               const CcKey key{ dev, channel, d1, true };
               gState.values[key] = velocity01;
               if (realOn)
               {
                  gState.noteHits[key] += 1;

                  Platform::MidiLastNote& last = gState.channelLast[{ dev, channel }];
                  last.note = d1;
                  last.velocity01 = velocity01;
                  last.hitSeq += 1;

                  gState.lastTouched.device = dev;
                  gState.lastTouched.channel = channel;
                  gState.lastTouched.controller = d1;
                  gState.lastTouched.isNote = true;
                  gState.lastTouched.value01 = velocity01;
                  gState.lastTouchedPending = true;
               }
            }
            PublishNote(dev, channel, d1, velocity01, realOn);
            break;
         }
         case 0x80: // Note Off
         {
            std::lock_guard<std::mutex> lock(gState.mutex);
            gState.values[{ dev, channel, d1, true }] = 0.0f;
            PublishNote(dev, channel, d1, 0.0f, false);
            break;
         }
         case 0xB0: // Control Change
         {
            const float value01 = (float)d2 / 127.0f;
            std::lock_guard<std::mutex> lock(gState.mutex);
            gState.values[{ dev, channel, d1, false }] = value01;
            gState.lastTouched.device = dev;
            gState.lastTouched.channel = channel;
            gState.lastTouched.controller = d1;
            gState.lastTouched.isNote = false;
            gState.lastTouched.value01 = value01;
            gState.lastTouchedPending = true;
            break;
         }
         default:
            break;
      }
   }

   void CALLBACK MidiInProc(HMIDIIN, UINT wMsg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
   {
      switch (wMsg)
      {
         case MIM_DATA:
            HandleShortMessage((UINT)instance, param1);
            break;
         default:
            break;
      }
   }
}

namespace Platform
{
   bool MidiStart(std::string& outError)
   {
      outError.clear();
      if (gRunning.load(std::memory_order_acquire))
         return true;

      MidiStop(); // close any stale handles first

      const UINT count = midiInGetNumDevs();
      {
         std::lock_guard<std::mutex> lock(gState.mutex);
         gState.deviceNames.clear();
      }

      if (count == 0)
      {
         // Not an error: the app runs fine without a controller attached and
         // MidiStart gets called on every launch.
         gRunning.store(true, std::memory_order_release);
         return true;
      }

      for (UINT i = 0; i < count; i++)
      {
         MIDIINCAPSW caps {};
         if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;

         HMIDIIN handle = nullptr;
         // CALLBACK_FUNCTION delivers on a WinMM-owned thread; `instance`
         // carries the device index through to MidiInProc.
         const MMRESULT res = midiInOpen(&handle, i, (DWORD_PTR)&MidiInProc,
                                         (DWORD_PTR)i, CALLBACK_FUNCTION);
         if (res != MMSYSERR_NOERROR || handle == nullptr)
            continue;

         if (midiInStart(handle) != MMSYSERR_NOERROR)
         {
            midiInClose(handle);
            continue;
         }

         gHandles.push_back(handle);
         std::lock_guard<std::mutex> lock(gState.mutex);
         gState.deviceNames.push_back(WinCommon::WideToUtf8(caps.szPname));
      }

      gRunning.store(true, std::memory_order_release);
      return true;
   }

   void MidiStop()
   {
      for (HMIDIIN handle : gHandles)
      {
         midiInStop(handle);
         midiInReset(handle);
         midiInClose(handle);
      }
      gHandles.clear();
      gRunning.store(false, std::memory_order_release);
   }

   bool MidiIsRunning()
   {
      return gRunning.load(std::memory_order_acquire);
   }

   std::string MidiDeviceSummary()
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      std::string summary;
      for (size_t i = 0; i < gState.deviceNames.size(); i++)
      {
         if (i > 0)
            summary += ", ";
         summary += gState.deviceNames[i];
      }
      return summary.empty() ? std::string("no inputs connected") : summary;
   }

   std::string MidiDeviceName(MidiDeviceId device)
   {
      if (device == 0 || device > gHandles.size())
         return {};
      std::lock_guard<std::mutex> lock(gState.mutex);
      return device <= gState.deviceNames.size() ? gState.deviceNames[device - 1] : std::string();
   }

   bool MidiRead(MidiDeviceId device, int channel, int controller, bool isNote, float& outValue01)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      const auto it = gState.values.find({ device, channel, controller, isNote });
      if (it == gState.values.end())
         return false;
      outValue01 = it->second;
      return true;
   }

   bool MidiPollLastTouched(MidiCCValue& outLast)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      if (!gState.lastTouchedPending)
         return false;
      outLast = gState.lastTouched;
      gState.lastTouchedPending = false;
      return true;
   }

   unsigned int MidiNoteHitCount(MidiDeviceId device, int channel, int note)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      const auto it = gState.noteHits.find({ device, channel, note, true });
      return it == gState.noteHits.end() ? 0u : it->second;
   }

   bool MidiChannelLastNote(MidiDeviceId device, int channel, MidiLastNote& out)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      const auto it = gState.channelLast.find({ device, channel });
      if (it == gState.channelLast.end())
         return false;
      out = it->second;
      return true;
   }

   int MidiReadNotesSince(unsigned long long& cursor, MidiNoteMessage* out, int maxCount)
   {
      if (out == nullptr || maxCount <= 0)
         return 0;

      const uint64_t w = gState.ringWrite.load(std::memory_order_acquire);
      if (cursor == 0)
         cursor = w; // fast-forward new consumers to the live edge
      if (w - cursor > kNoteRingCapacity)
         cursor = w - kNoteRingCapacity; // fell off the ring: drop what was missed

      int count = 0;
      while (cursor < w && count < maxCount)
      {
         const NoteRingSlot& slot = gState.ring[cursor % kNoteRingCapacity];
         out[count] = slot.msg;
         count++;
         cursor++;
      }
      return count;
   }

   unsigned long long MidiNoteStreamPosition()
   {
      return gState.ringWrite.load(std::memory_order_acquire);
   }

   bool MidiClockIsPresent()
   {
      std::lock_guard<std::mutex> lock(gClock.mutex);
      if (gClock.pulseMs.empty())
         return false;
      const double nowMs =
         std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
      return nowMs - gClock.pulseMs.back() < 2000.0;
   }

   float MidiClockBpm()
   {
      std::lock_guard<std::mutex> lock(gClock.mutex);
      return gClock.bpm;
   }
}
