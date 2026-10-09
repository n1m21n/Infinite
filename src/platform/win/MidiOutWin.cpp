// Windows MIDI output (docs/plans/midi-out). WinMM, no new dependency.
//
// WinMM has no timestamped send: midiOutShortMsg plays now. So MidiOutSend waits for the
// deliver time itself, on the MIDI-out thread (never the audio thread), in 1 ms steps for at
// most kMaxWaitMs. That is the documented ~1 ms jitter (D6). WinMM also cannot publish a
// virtual source, so MidiOutVirtualAvailable is false and help points at loopMIDI (D8).
//
// Signatures come from ../Platform.h.

#include "../Platform.h"

#include "WinCommon.h"

#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <thread>

#pragma comment(lib, "winmm.lib")

namespace
{
   // A message further out than this is sent late rather than held: the sink only ever
   // schedules within about one audio block, so this is a ceiling, not a delay in use.
   constexpr double kMaxWaitMs = 20.0;

   DWORD PackShort(const unsigned char* bytes, size_t len)
   {
      DWORD msg = bytes[0];
      if (len > 1) msg |= (DWORD)bytes[1] << 8;
      if (len > 2) msg |= (DWORD)bytes[2] << 16;
      return msg;
   }
}

namespace Platform
{
   struct MidiOutHandle
   {
      HMIDIOUT out = nullptr;
   };

   double MidiOutNowSeconds()
   {
      using namespace std::chrono;
      return duration<double>(steady_clock::now().time_since_epoch()).count();
   }

   std::vector<std::string> MidiOutListDevices()
   {
      std::vector<std::string> names;
      const UINT count = midiOutGetNumDevs();
      for (UINT i = 0; i < count; i++)
      {
         MIDIOUTCAPSW caps {};
         if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;
         names.push_back(WinCommon::WideToUtf8(caps.szPname));
      }
      return names;
   }

   MidiOutHandle* MidiOutOpen(const std::string& name, std::string& outError)
   {
      outError.clear();
      const UINT count = midiOutGetNumDevs();
      for (UINT i = 0; i < count; i++)
      {
         MIDIOUTCAPSW caps {};
         if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;
         if (WinCommon::WideToUtf8(caps.szPname) != name)
            continue;

         HMIDIOUT out = nullptr;
         const MMRESULT res = midiOutOpen(&out, i, 0, 0, CALLBACK_NULL);
         if (res != MMSYSERR_NOERROR)
         {
            // MMSYSERR_ALLOCATED: another app holds the port exclusively. Common with the
            // Microsoft GS Wavetable Synth and with hardware drivers that are not multi-client.
            outError = res == MMSYSERR_ALLOCATED
                          ? "MIDI output \"" + name + "\" is in use by another program"
                          : "could not open MIDI output \"" + name + "\"";
            return nullptr;
         }
         MidiOutHandle* h = new MidiOutHandle();
         h->out = out;
         return h;
      }
      outError = "MIDI output \"" + name + "\" is not connected";
      return nullptr;
   }

   void MidiOutClose(MidiOutHandle* handle)
   {
      if (!handle) return;
      if (handle->out)
      {
         midiOutReset(handle->out); // silences anything the device is still sounding
         midiOutClose(handle->out);
      }
      delete handle;
   }

   bool MidiOutSend(MidiOutHandle* handle, const unsigned char* bytes, size_t len, double deliverAtSeconds)
   {
      if (!handle || !handle->out || !bytes || len == 0 || len > 3) return false;

      const double waitMs = (deliverAtSeconds - MidiOutNowSeconds()) * 1000.0;
      if (waitMs > 1.0)
         std::this_thread::sleep_for(std::chrono::microseconds((long long)(std::min(waitMs, kMaxWaitMs) * 1000.0)));

      return midiOutShortMsg(handle->out, PackShort(bytes, len)) == MMSYSERR_NOERROR;
   }

   bool MidiOutVirtualAvailable()
   {
      return false;
   }
}
