#pragma once

#include <cmath>
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
// The session ends the moment Shift is released - and every param that
// recorded at least two samples starts looping its recorded trace back into
// the param (see GetPlaybackValue), until the user grabs that control again.
class GestureRecorder
{
public:
   static GestureRecorder& Instance();

   using Key = std::pair<int, int>; // nodeIndex, paramIndex

   // Call once per frame, before any param widgets draw (see main.cpp, right
   // alongside gParamPinScreenList.clear()). Ends the session the instant
   // Shift is no longer held, turning every recorded trace into a looping
   // playback.
   void BeginFrame(bool shiftHeld, double nowSec);

   // Call from a param widget's post-draw check, only while Shift is held and
   // the widget is actively being dragged (ImGui::IsItemActive()) - adds this
   // param to the current session and appends this sample to its trace. Also
   // cancels any playback already looping for this param, so re-shift-
   // dragging a param that's currently replaying re-records it instead of
   // fighting the old loop.
   //
   // `isNewGrab` marks the first sample of a fresh grab (the caller's own
   // ImGui::IsItemActivated() this frame) as opposed to a sample mid an
   // already-active drag. A shift-held session that is one continuous drag
   // never sets this past its very first sample, and plays back as smooth
   // interpolation like before. A session built from several separate
   // grabs - shift-click a value, release, shift-click another, release,
   // i.e. "checkpoints" - sets it on each grab's first sample, and
   // GetPlaybackValue then holds each checkpoint's value and jumps straight
   // to the next rather than sliding between them.
   void NotifyMovement(int nodeIndex, int paramIndex, float value, double nowSec, bool isNewGrab = false);

   // Whether this param should render its "recording" (red) visual state
   // right now - true for the rest of the session once touched (per the
   // class comment above), and true again for as long as its recorded trace
   // is looping back into it (see GetPlaybackValue) once the session ends.
   bool IsRecording(int nodeIndex, int paramIndex) const;

   // The user grabbed this control directly (not a shift-drag) - stop
   // replaying its recorded trace and let them drive it manually again.
   void StopPlayback(int nodeIndex, int paramIndex);

   // If this param has a finished recording looping, writes the interpolated
   // value it should hold at `nowSec` into `outValue` and returns true.
   // Callers only apply this when the param isn't otherwise driven (no wired
   // modulator, no expression) - same precedence rule as those two.
   bool GetPlaybackValue(int nodeIndex, int paramIndex, double nowSec, float& outValue) const;

private:
   struct Sample
   {
      float value;
      double timeSec;
      // True for the first sample of a fresh grab within a session - see
      // NotifyMovement's isNewGrab comment. Drives the hold-then-jump
      // playback shape in GetPlaybackValue.
      bool startsNewGrab = false;
   };

   struct Playback
   {
      std::vector<Sample> samples; // >= 2 entries, timeSec strictly increasing
      double startTime = 0.0;      // nowSec at which this loop began
   };

   bool mShiftHeld = false;
   std::map<Key, std::vector<Sample>> mSession;
   std::map<Key, Playback> mPlayback;
};
