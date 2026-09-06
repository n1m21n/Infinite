#pragma once

#include <map>
#include <utility>
#include <vector>

// Tracks which params are currently part of a "gesture recording": the user
// holding Shift while dragging a knob or slider. Shift is the sole trigger -
// there is no pattern-matching here, just "is Shift down while this widget is
// being moved". A widget joins the session the instant it is shift-dragged
// and stays flagged (and drawn red - see ModKnob/ModSlider) for the rest of
// the session even after the user moves on to a different control, so
// several params tweaked in the same Shift-held stretch read as one group.
// The session ends - and every flag drops - the moment Shift is released.
class GestureRecorder
{
public:
   static GestureRecorder& Instance();

   using Key = std::pair<int, int>; // nodeIndex, paramIndex

   // Call once per frame, before any param widgets draw (see main.cpp, right
   // alongside gParamPinScreenList.clear()). Ends the session the instant
   // Shift is no longer held.
   void BeginFrame(bool shiftHeld);

   // Call from a param widget's post-draw check, only while Shift is held and
   // the widget is actively being dragged (ImGui::IsItemActive()) - adds this
   // param to the current session and appends this sample to its trace.
   void NotifyMovement(int nodeIndex, int paramIndex, float value, double nowSec);

   // Whether this param should render its "recording" (red) visual state
   // right now - true for the rest of the session once touched, per the
   // class comment above.
   bool IsRecording(int nodeIndex, int paramIndex) const;

private:
   struct Sample
   {
      float value;
      double timeSec;
   };

   bool mShiftHeld = false;
   std::map<Key, std::vector<Sample>> mSession;
};
