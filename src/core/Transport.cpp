#include "Transport.h"

Transport& Transport::Instance()
{
   static Transport instance;
   return instance;
}

void Transport::Tick(float deltaSeconds)
{
   if (!mPlaying)
      return;

   // clamp so a stalled frame (window drag, file dialog) doesn't jump the clock
   if (deltaSeconds > 0.25f)
      deltaSeconds = 0.25f;

   mSeconds += deltaSeconds;
   mBeats += deltaSeconds * (mBpm / 60.0);
}
