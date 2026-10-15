// Windows MIDI output (Turbo 0.57, ported from upstream src/platform/win/MidiOutWin.cpp).
// WinMM, no new dependency.
//
// WinMM has no timestamped send: midiOutShortMsg plays now. So MidiOutSend waits for the
// deliver time itself, on the MIDI-out thread (never the audio thread), in 1 ms steps for at
// most kMaxWaitMs: about 1 ms of jitter. WinMM cannot publish a virtual source either, so
// MidiOutVirtualAvailable is false and the help points at loopMIDI.

#include "Platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <thread>

#pragma comment(lib, "winmm.lib")

namespace
{
   constexpr double kMaxWaitMs = 20.0;

   DWORD PackShort(const unsigned char* bytes, size_t len)
   {
      DWORD msg = bytes[0];
      if (len > 1) msg |= (DWORD)bytes[1] << 8;
      if (len > 2) msg |= (DWORD)bytes[2] << 16;
      return msg;
   }

   std::string WideToUtf8(const wchar_t* w)
   {
      if (w == nullptr || *w == 0)
         return std::string();
      const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
      if (n <= 1)
         return std::string();
      std::string out((size_t)n - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
      return out;
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
         names.push_back(WideToUtf8(caps.szPname));
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
         if (WideToUtf8(caps.szPname) != name)
            continue;
         HMIDIOUT out = nullptr;
         const MMRESULT res = midiOutOpen(&out, i, 0, 0, CALLBACK_NULL);
         if (res != MMSYSERR_NOERROR)
         {
            // MMSYSERR_ALLOCATED: another app holds the port exclusively (common with the
            // Microsoft GS Wavetable Synth and drivers that are not multi-client).
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
      if (!handle)
         return;
      if (handle->out)
      {
         midiOutReset(handle->out); // silences anything the device is still sounding
         midiOutClose(handle->out);
      }
      delete handle;
   }

   bool MidiOutSend(MidiOutHandle* handle, const unsigned char* bytes, size_t len, double deliverAtSeconds)
   {
      if (!handle || !handle->out || !bytes || len == 0 || len > 3)
         return false;
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
