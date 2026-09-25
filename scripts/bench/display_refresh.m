// What each display really refreshes at, independent of Infinite: the mode's
// rate, NSScreen.maximumFramesPerSecond, CVDisplayLink's nominal and actual
// output period, and CVDisplayLink ticks counted over 2 s. Block 2 step 2
// (docs/plans/perf/README.md open item 5) used it to rule out "the panel
// runs at 120 Hz" as the reason a vsync=1 canvas runs at 8.33 ms.
//
//   clang -fobjc-arc -framework Cocoa -framework CoreVideo \
//      -Wno-deprecated-declarations scripts/bench/display_refresh.m -o /tmp/display_refresh
//   /tmp/display_refresh
#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#include <stdatomic.h>

static _Atomic int gTicks = 0;

static CVReturn Tick(CVDisplayLinkRef link, const CVTimeStamp* now, const CVTimeStamp* out, CVOptionFlags in, CVOptionFlags* flags, void* ctx)
{
   atomic_fetch_add(&gTicks, 1);
   return kCVReturnSuccess;
}

int main()
{
   @autoreleasepool
   {
      for (NSScreen* screen in [NSScreen screens])
      {
         const CGDirectDisplayID d = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
         CGDisplayModeRef mode = CGDisplayCopyDisplayMode(d);
         printf("display %u builtin=%d mode_hz=%.3f max_fps=%ld\n", d, CGDisplayIsBuiltin(d),
                CGDisplayModeGetRefreshRate(mode), (long)screen.maximumFramesPerSecond);
         CGDisplayModeRelease(mode);
         CVDisplayLinkRef link = NULL;
         if (CVDisplayLinkCreateWithCGDisplay(d, &link) != kCVReturnSuccess)
            continue;
         const CVTime nominal = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
         CVDisplayLinkSetOutputCallback(link, Tick, NULL);
         CVDisplayLinkStart(link);
         usleep(1500000);
         const int t0 = gTicks;
         usleep(2000000);
         const int t1 = gTicks;
         printf("  nominal_period_ms=%.4f actual_period_ms=%.4f ticks_per_s=%.1f\n",
                1000.0 * (double)nominal.timeValue / (double)nominal.timeScale,
                1000.0 * CVDisplayLinkGetActualOutputVideoRefreshPeriod(link), (t1 - t0) / 2.0);
         CVDisplayLinkStop(link);
         CVDisplayLinkRelease(link);
      }
   }
   return 0;
}
