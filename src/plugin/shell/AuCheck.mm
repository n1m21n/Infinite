// au_check (docs/plans/vst-au/README.md step 1.6): load the installed "Infinite FX" / "Infinite
// Instrument" AUs the way Logic does, in process or out of process (AUHostingService), against
// a real engine, and check the audio.
//
// usage: au_check [in|out]
//   in   needs $INFINITE_PLUGIN_ENGINE (inherited by the AU, which runs in this process)
//   out  the hosting service sees no environment: the engine must sit in the installed bundle at
//        Contents/Resources/plugin.vst3/Contents/Helpers/Infinite Engine.app (the packaged layout)
//
// Checks: FX offline render equals the input delayed by the reported latency (exact, random
// slice sizes), FX realtime the same once the engine is up, Instrument note-on sounds and
// note-off releases, no engine process left after dispose.
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
   constexpr double kRate = 48000.0;
   constexpr UInt32 kMaxFrames = 512;
   int failures = 0;

   void Report(bool ok, const char* what, const std::string& detail)
   {
      printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
      if (!ok)
         ++failures;
   }

   float Sine(double t) { return 0.5f * float(std::sin(2.0 * M_PI * 220.0 * t / kRate)); }

   OSStatus InputCb(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 frames,
                    AudioBufferList* io)
   {
      for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
      {
         float* p = static_cast<float*>(io->mBuffers[b].mData);
         for (UInt32 i = 0; i < frames; ++i)
            p[i] = Sine(ts->mSampleTime + i);
      }
      return noErr;
   }

   OSStatus SilentCb(void*, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*, UInt32, UInt32,
                     AudioBufferList* io)
   {
      for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
         memset(io->mBuffers[b].mData, 0, io->mBuffers[b].mDataByteSize);
      *flags |= kAudioUnitRenderAction_OutputIsSilence;
      return noErr;
   }

   int EngineCount()
   {
      FILE* f = popen("pgrep -f -- '--plugin-engine' | wc -l", "r");
      int n = -1;
      if (f)
      {
         if (fscanf(f, "%d", &n) != 1)
            n = -1;
         pclose(f);
      }
      return n;
   }

   AudioUnit Open(OSType type, OSType sub, bool oop)
   {
      AudioComponentDescription d{type, sub, 'Infn', 0, 0};
      AudioComponent comp = AudioComponentFindNext(nullptr, &d);
      if (!comp)
         return nullptr;
      __block AudioUnit au = nullptr;
      __block OSStatus st = -1;
      dispatch_semaphore_t sem = dispatch_semaphore_create(0);
      AudioComponentInstantiate(comp,
                                oop ? kAudioComponentInstantiation_LoadOutOfProcess
                                    : kAudioComponentInstantiation_LoadInProcess,
                                ^(AudioComponentInstance inst, OSStatus e) {
                                   au = inst;
                                   st = e;
                                   dispatch_semaphore_signal(sem);
                                });
      dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
      return st == noErr ? au : nullptr;
   }

   bool Setup(AudioUnit au, bool offline, AURenderCallback input)
   {
      AudioStreamBasicDescription f{kRate, kAudioFormatLinearPCM,
                                    kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
                                    4, 1, 4, 2, 32, 0};
      AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
      AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
      UInt32 maxFrames = kMaxFrames;
      AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames,
                           sizeof maxFrames);
      UInt32 off = offline ? 1 : 0;
      AudioUnitSetProperty(au, kAudioUnitProperty_OfflineRender, kAudioUnitScope_Global, 0, &off, sizeof off);
      AURenderCallbackStruct cb{input, nullptr};
      AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
      return AudioUnitInitialize(au) == noErr;
   }

   int Latency(AudioUnit au)
   {
      Float64 latency = 0;
      UInt32 sz = sizeof latency;
      AudioUnitGetProperty(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &sz);
      return int(latency * kRate + 0.5);
   }

   // Renders `total` frames in slices (random sizes when `randomSlices`), left channel out.
   bool Render(AudioUnit au, int total, bool randomSlices, bool realtime, std::vector<float>& out,
               const std::function<void(double)>& beforeSlice = nullptr)
   {
      std::mt19937 rng(7);
      std::vector<float> l(kMaxFrames), r(kMaxFrames);
      AudioTimeStamp ts{};
      ts.mFlags = kAudioTimeStampSampleTimeValid;
      while (int(out.size()) < total)
      {
         UInt32 n = randomSlices ? UInt32(1 + rng() % kMaxFrames) : kMaxFrames;
         n = std::min<UInt32>(n, UInt32(total - int(out.size())));
         if (beforeSlice)
            beforeSlice(ts.mSampleTime);
         struct
         {
            UInt32 n;
            AudioBuffer b[2];
         } abl{2, {{1, n * 4, l.data()}, {1, n * 4, r.data()}}};
         AudioUnitRenderActionFlags flags = 0;
         if (AudioUnitRender(au, &flags, &ts, 0, n, reinterpret_cast<AudioBufferList*>(&abl)) != noErr)
            return false;
         out.insert(out.end(), l.begin(), l.begin() + n);
         ts.mSampleTime += n;
         if (realtime)
            usleep(useconds_t(1e6 * n / kRate));
      }
      return true;
   }

   void Close(AudioUnit au)
   {
      AudioUnitUninitialize(au);
      AudioComponentInstanceDispose(au);
   }

   // FX build, starting patch Audio In -> Audio Out: out[t] == in[t - L].
   void CheckFx(bool oop, bool offline)
   {
      const std::string what = std::string("FX ") + (offline ? "offline" : "realtime");
      AudioUnit au = Open(kAudioUnitType_MusicEffect, 'InFx', oop);
      if (!au)
         return Report(false, what.c_str(), "component not found / instantiate failed");
      if (!Setup(au, offline, InputCb))
      {
         Close(au);
         return Report(false, what.c_str(), "initialize failed");
      }
      const int L = Latency(au);
      std::vector<float> out;
      const int total = int(kRate * 3);
      const bool rendered = Render(au, total, offline, !offline, out);
      // Offline: every sample counts (the shell waits for the engine). Realtime: the last second,
      // after the engine is up.
      const int from = offline ? 0 : total - int(kRate);
      double err = 0, peak = 0;
      for (int t = from; t < int(out.size()); ++t)
      {
         const double want = t >= L ? Sine(t - L) : 0.0;
         err = std::max(err, std::fabs(out[t] - want));
         peak = std::max(peak, double(std::fabs(out[t])));
      }
      int firstSound = -1;
      for (int t = 0; t < int(out.size()) && firstSound < 0; ++t)
         if (out[t] != 0.0f)
            firstSound = t;
      Close(au);
      char buf[200];
      snprintf(buf, sizeof buf, "latency %d, peak %.3f, max error %.2g, first sound at %d%s", L, peak, err,
               firstSound, rendered ? "" : ", render error");
      Report(rendered && peak > 0.4 && err == 0.0, what.c_str(), buf);
   }

   // Instrument build, starting patch MIDI Notes -> Analog -> Audio Out.
   void CheckInstrument(bool oop)
   {
      AudioUnit au = Open(kAudioUnitType_MusicDevice, 'InIn', oop);
      if (!au)
         return Report(false, "Instrument note", "component not found / instantiate failed");
      if (!Setup(au, true, SilentCb))
      {
         Close(au);
         return Report(false, "Instrument note", "initialize failed");
      }
      const int on = int(kRate * 0.5), offAt = int(kRate * 1.5), total = int(kRate * 3);
      bool sentOn = false, sentOff = false;
      std::vector<float> out;
      const bool rendered = Render(au, total, true, false, out, [&](double t) {
         if (!sentOn && t >= on)
         {
            MusicDeviceMIDIEvent(au, 0x90, 60, 100, 0);
            sentOn = true;
         }
         if (!sentOff && t >= offAt)
         {
            MusicDeviceMIDIEvent(au, 0x80, 60, 0, 0);
            sentOff = true;
         }
      });
      Close(au);
      auto rms = [&](int a, int b) {
         double s = 0;
         for (int t = a; t < b && t < int(out.size()); ++t)
            s += double(out[t]) * out[t];
         return std::sqrt(s / std::max(1, b - a));
      };
      const double before = rms(0, on), held = rms(on + int(kRate * 0.1), offAt),
                   after = rms(total - int(kRate * 0.5), total);
      char buf[160];
      snprintf(buf, sizeof buf, "rms before %.4f, held %.3f, released %.4f", before, held, after);
      Report(rendered && before < 1e-4 && held > 0.05 && after < held * 0.1, "Instrument note", buf);
   }
}

int main(int argc, char** argv)
{
   @autoreleasepool
   {
      const bool oop = argc > 1 && std::string(argv[1]) == "out";
      if (!oop && !getenv("INFINITE_PLUGIN_ENGINE"))
         printf("note: INFINITE_PLUGIN_ENGINE not set; the AU falls back to the bundle / installed app\n");
      printf("au_check, %s\n", oop ? "out of process" : "in process");
      const int enginesBefore = EngineCount();
      CheckFx(oop, true);
      CheckFx(oop, false);
      CheckInstrument(oop);
      int left = 0;
      for (int i = 0; i < 50 && (left = EngineCount() - enginesBefore) > 0; ++i)
         usleep(100000);
      Report(left <= 0, "no engine left", std::to_string(std::max(0, left)) + " running");
      printf(failures ? "FAILED (%d)\n" : "ALL PASS\n", failures);
      return failures ? 1 : 0;
   }
}
