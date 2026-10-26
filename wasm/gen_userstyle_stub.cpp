// The website has no player profile: the generator runs with personal = false, so these are never consulted.
#include "app/graph/UserStyle.h"

namespace UserStyle
{
   const Profile& Get()
   {
      static const Profile kNone;
      return kNone;
   }
   bool DrawTempo(double, float&, float&) { return false; }
   bool DrawKnob(const char*, double, float&, float&) { return false; }
}
