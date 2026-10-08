#include "platform/HeadTracker.h"

#import <CoreMotion/CoreMotion.h>
#import <QuartzCore/QuartzCore.h>
#include <atomic>
#include <cmath>

namespace HeadTracker
{
   namespace
   {
      CMHeadphoneMotionManager* gManager = nil;
      int gRefs = 0;
      std::atomic<float> gRawYaw { 0.0f };   // degrees, CoreMotion sign (+ = left)
      std::atomic<double> gLastSample { 0.0 };
      std::atomic<bool> gRecenter { true };
      float gZero = 0.0f;
      const char* gStatus = "Head tracking off";

      double Now() { return CACurrentMediaTime(); }
      float Wrap(float d)
      {
         d = std::fmod(d + 180.0f, 360.0f);
         if (d < 0.0f)
            d += 360.0f;
         return d - 180.0f;
      }
   }

   bool Supported(Source source)
   {
      if (source == kOff)
         return true;
      if (source != kHeadphones)
         return false;
      if (@available(macOS 14.0, *))
         return true;
      return false;
   }

   void Start(Source source)
   {
      if (source != kHeadphones || !Supported(source))
         return;
      if (gRefs++ > 0)
         return;
      if (@available(macOS 14.0, *))
      {
         gManager = [[CMHeadphoneMotionManager alloc] init];
         if (!gManager.deviceMotionAvailable)
         {
            gStatus = "No headphone motion: connect AirPods Pro/Max or similar";
            return;
         }
         if (CMHeadphoneMotionManager.authorizationStatus == CMAuthorizationStatusDenied ||
             CMHeadphoneMotionManager.authorizationStatus == CMAuthorizationStatusRestricted)
         {
            gStatus = "Motion access denied: allow it in System Settings > Privacy";
            return;
         }
         gStatus = "Looking for AirPods...";
         gRecenter.store(true);
         NSOperationQueue* q = [[NSOperationQueue alloc] init];
         q.maxConcurrentOperationCount = 1;
         [gManager startDeviceMotionUpdatesToQueue:q
                                       withHandler:^(CMDeviceMotion* motion, NSError* error) {
                                          if (motion == nil)
                                             return;
                                          gRawYaw.store((float)(motion.attitude.yaw * 57.29577951308232),
                                                        std::memory_order_relaxed);
                                          gLastSample.store(Now(), std::memory_order_relaxed);
                                       }];
      }
   }

   void Stop(Source source)
   {
      if (source != kHeadphones || gRefs == 0)
         return;
      if (--gRefs > 0)
         return;
      if (@available(macOS 14.0, *))
         [gManager stopDeviceMotionUpdates];
      gManager = nil;
      gLastSample.store(0.0);
      gStatus = "Head tracking off";
   }

   bool ReadYaw(float& yawDegrees)
   {
      if (gRefs == 0)
         return false;
      const double age = Now() - gLastSample.load(std::memory_order_relaxed);
      if (gLastSample.load(std::memory_order_relaxed) <= 0.0 || age > 1.0)
      {
         if (gManager != nil && gStatus[0] != 'N' && gStatus[0] != 'M')
            gStatus = "AirPods not sending motion (in your ears?)";
         return false;
      }
      const float raw = gRawYaw.load(std::memory_order_relaxed);
      if (gRecenter.exchange(false))
         gZero = raw;
      gStatus = "AirPods connected";
      yawDegrees = Wrap(-(raw - gZero)); // CoreMotion + = left; ours + = right
      return true;
   }

   void Recenter() { gRecenter.store(true); }
   const char* Status() { return gStatus; }
}
