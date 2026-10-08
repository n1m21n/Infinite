#include "platform/HeadTracker.h"

// Windows and Linux have no headphone-motion API and the webcam fallback is not
// built yet: head tracking is reported as unsupported and the Spatial Mixer
// stays head-fixed there.
namespace HeadTracker
{
   bool Supported(Source source) { return source == kOff; }
   void Start(Source) {}
   void Stop(Source) {}
   bool ReadYaw(float&) { return false; }
   void Recenter() {}
   const char* Status() { return "Head tracking is not available on this system yet"; }
}
