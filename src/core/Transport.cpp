#include "Transport.h"

Transport& Transport::Instance()
{
   static Transport instance;
   return instance;
}

void Transport::Tick(float deltaSeconds)
{
   // clamp so a stalled frame (window drag, file dialog) doesn't jump the clock
   if (deltaSeconds > 0.25f)
      deltaSeconds = 0.25f;

   // External clock freshness ages regardless of play state - a MIDI clock
   // source dropping out (deck unplugged, pause on the mixer) should still be
   // detected while the transport itself is paused.
   if (mExternalSync && mExternalClockFresh)
   {
      mExternalClockAge += deltaSeconds;
      if (mExternalClockAge > kExternalClockTimeoutSeconds)
         mExternalClockFresh = false;
   }

   if (!mPlaying)
      return;

   mSeconds += deltaSeconds;
   mBeats += deltaSeconds * (mBpm / 60.0);
}
