// R-1 probe: load the spike AU the way Logic / GarageBand can, out of process
// (kAudioComponentInstantiation_LoadOutOfProcess -> sandboxed AUHostingService), render
// two seconds and check that the helper's signal (0.5*in + 440 Hz tone) comes back.
//
// usage: spike_auoop [in|out]   (default out)
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

static OSStatus InputCb(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32,
                        UInt32 frames, AudioBufferList* io) {
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
        float* p = static_cast<float*>(io->mBuffers[b].mData);
        for (UInt32 i = 0; i < frames; ++i)
            p[i] = 0.5f * float(std::sin(2.0 * M_PI * 220.0 * (ts->mSampleTime + i) / 48000.0));
    }
    return noErr;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        const bool oop = !(argc > 1 && std::string(argv[1]) == "in");
        AudioComponentDescription d{kAudioUnitType_Effect, 'Ispk', 'Infn', 0, 0};
        AudioComponent comp = AudioComponentFindNext(nullptr, &d);
        if (!comp) { printf("component not found\n"); return 1; }

        __block AudioUnit au = nullptr;
        __block OSStatus st = -1;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        AudioComponentInstantiate(comp,
                                  oop ? kAudioComponentInstantiation_LoadOutOfProcess
                                      : kAudioComponentInstantiation_LoadInProcess,
                                  ^(AudioComponentInstance inst, OSStatus e) {
                                      au = inst; st = e; dispatch_semaphore_signal(sem);
                                  });
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        if (st != noErr || !au) { printf("instantiate failed: %d\n", int(st)); return 1; }

        AudioStreamBasicDescription f{48000, kAudioFormatLinearPCM,
                                      kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
                                      4, 1, 4, 2, 32, 0};
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 maxFrames = 512;
        AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                             &maxFrames, sizeof maxFrames);
        AURenderCallbackStruct cb{InputCb, nullptr};
        AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        if ((st = AudioUnitInitialize(au)) != noErr) { printf("initialize failed: %d\n", int(st)); return 1; }

        Float64 latency = 0;
        UInt32 sz = sizeof latency;
        AudioUnitGetProperty(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &sz);

        usleep(500000);  // realtime render: give the helper time to attach
        std::vector<float> l(maxFrames), r(maxFrames), outL;
        AudioTimeStamp ts{};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        for (int blk = 0; blk < 48000 * 2 / int(maxFrames); ++blk) {
            struct { UInt32 n; AudioBuffer b[2]; } abl{2, {{1, maxFrames * 4, l.data()}, {1, maxFrames * 4, r.data()}}};
            AudioUnitRenderActionFlags flags = 0;
            st = AudioUnitRender(au, &flags, &ts, 0, maxFrames, reinterpret_cast<AudioBufferList*>(&abl));
            if (st != noErr) { printf("render failed: %d\n", int(st)); return 1; }
            outL.insert(outL.end(), l.begin(), l.end());
            ts.mSampleTime += maxFrames;
            usleep(useconds_t(1e6 * maxFrames / 48000));
        }
        // Second half: residual after removing 0.5*in(t-L) must be the helper's 0.05 tone
        // (its phase starts when the helper attaches, so only the amplitude is checked).
        const int L = int(latency * 48000 + 0.5);
        double res = 0, peak = 0;
        for (size_t t = outL.size() / 2; t < outL.size(); ++t) {
            const double dry = 0.5 * 0.5 * std::sin(2.0 * M_PI * 220.0 * (double(t) - L) / 48000.0);
            res = std::max(res, std::fabs(outL[t] - dry));
            peak = std::max(peak, double(std::fabs(outL[t])));
        }
        const bool ok = peak > 0.1 && std::fabs(res - 0.05) < 1e-3;
        printf("%s-of-process: latency %d samples, output peak %.3f, tone residual %.4f (want 0.05) -> %s\n",
               oop ? "out" : "in", L, peak, res, ok ? "PASS" : "FAIL");
        AudioUnitUninitialize(au);
        AudioComponentInstanceDispose(au);
        return ok ? 0 : 1;
    }
}
