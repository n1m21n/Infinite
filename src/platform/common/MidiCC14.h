#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>

// 14-bit MIDI CC pairing, shared by the CoreMIDI / WinMM / ALSA backends.
//
// Pioneer DDJ controllers (FLX4 included) send each knob and fader as a pair:
// the coarse byte on CC n (0-31) followed within a millisecond or two by the
// fine byte on CC n+32. Read naively, the fine byte looks like an independent
// controller sweeping 0..127 across every single coarse step, so anything
// bound to it (or MIDI Learn grabbing it) wiggles wildly. Pair them here and
// publish one 14-bit value under the coarse controller number.
//
// Not thread-safe by itself: callers hold their MidiState mutex.
namespace MidiCC14
{
   struct Event
   {
      int controller = 0;    // controller number to publish under
      float value01 = 0.0f;
   };

   inline int64_t NowMs()
   {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
   }

   struct Tracker
   {
      static constexpr int64_t kPairWindowMs = 25;

      struct Slot
      {
         int msb = 0;
         int lsb = 0;
         int64_t msbTimeMs = -1000000;
         bool pair14 = false; // this coarse CC has been seen with a fine partner
      };

      // key = (device, channel, coarse cc) packed by the caller-independent hash below
      std::unordered_map<uint64_t, Slot> slots;

      static uint64_t Key(uint64_t device, int channel, int cc)
      {
         return (device * 1315423911ull) ^ ((uint64_t)(channel & 0xF) << 8) ^ (uint64_t)(cc & 0x7F);
      }

      void Clear() { slots.clear(); }

      static float Combine(int msb, int lsb)
      {
         return (float)((msb << 7) | lsb) / 16383.0f;
      }

      Event OnCC(uint64_t device, int channel, int cc, int value7, int64_t nowMs)
      {
         Event e;
         e.controller = cc;
         e.value01 = (float)value7 / 127.0f;
         if (cc >= 0 && cc < 32)
         {
            Slot& s = slots[Key(device, channel, cc)];
            if (s.pair14)
            {
               // The fine byte for this coarse step is still in flight. Until
               // it lands, sit on the edge of the new step nearest the old
               // value so the stream stays monotonic instead of jumping a
               // whole coarse step and back.
               if (value7 > s.msb)
                  s.lsb = 0;
               else if (value7 < s.msb)
                  s.lsb = 127;
            }
            s.msb = value7;
            s.msbTimeMs = nowMs;
            if (s.pair14)
               e.value01 = Combine(s.msb, s.lsb);
         }
         else if (cc >= 32 && cc < 64)
         {
            auto it = slots.find(Key(device, channel, cc - 32));
            if (it != slots.end())
            {
               Slot& s = it->second;
               if (s.pair14 || nowMs - s.msbTimeMs <= kPairWindowMs)
               {
                  s.pair14 = true;
                  s.lsb = value7;
                  e.controller = cc - 32;
                  e.value01 = Combine(s.msb, s.lsb);
                  return e;
               }
            }
         }
         return e;
      }

      // A patch saved against the fine controller (e.g. MIDI Learn grabbed it
      // before pairing existed) follows the paired coarse controller instead.
      int Resolve(uint64_t device, int channel, int controller) const
      {
         if (controller >= 32 && controller < 64)
         {
            auto it = slots.find(Key(device, channel, controller - 32));
            if (it != slots.end() && it->second.pair14)
               return controller - 32;
         }
         return controller;
      }
   };
}
