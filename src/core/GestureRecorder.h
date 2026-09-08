#pragma once

#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

// Tracks which params are currently being gesture-recorded, two ways:
//
//  1. Holding Shift while dragging a knob or slider - the original path.
//     Every param touched while Shift is held joins ONE shared session, and
//     releasing Shift ends all of them at once (see BeginFrame).
//  2. "Start Recording" from a param's right-click menu (main.cpp) - arms
//     exactly that one (nodeIndex, paramIndex), no Shift needed. It disarms
//     itself the instant that control's drag ends (see
//     MaybeFinishArmedRecording) - there is no separate "stop" action.
//
// Either path feeds the same per-param trace and produces the same looping
// Playback once finished (see GetPlaybackValue) - only how the session
// starts/ends differs.
class GestureRecorder
{
public:
   static GestureRecorder& Instance();

   using Key = std::pair<int, int>; // nodeIndex, paramIndex

   // Call once per frame, before any param widgets draw (see main.cpp, right
   // alongside gParamPinScreenList.clear()). Ends the Shift-held session the
   // instant Shift is no longer held, turning every trace recorded during it
   // into a looping playback. Armed (menu-started) recordings are unaffected
   // by this - they end on their own via MaybeFinishArmedRecording.
   void BeginFrame(bool shiftHeld, double nowSec);

   // Arms exactly this one param for recording, independent of Shift. The
   // very next drag on it joins the session; releasing that drag finishes it
   // (see MaybeFinishArmedRecording) with no further action needed. Drops
   // any stale unfinished trace and any currently-looping playback for this
   // key first, so arming always starts clean.
   void ArmParam(int nodeIndex, int paramIndex);
   bool IsArmed(int nodeIndex, int paramIndex) const { return mArmedParams.count(Key(nodeIndex, paramIndex)) > 0; }

   // Cancels an armed-but-not-yet-moved (or mid-drag) recording without
   // turning whatever was captured into a playback loop - the menu's escape
   // hatch if "Start Recording" was armed by mistake.
   void CancelArm(int nodeIndex, int paramIndex);

   // Call from a param widget's post-draw check, while the widget is
   // actively being dragged (ImGui::IsItemActive()) and either Shift is held
   // or this param is armed (see IsArmed) - adds this param to the current
   // session and appends this sample to its trace. Also cancels any playback
   // already looping for this param, so re-recording a param that's
   // currently replaying re-records it instead of fighting the old loop.
   //
   // `isNewGrab` marks the first sample of a fresh grab (the caller's own
   // ImGui::IsItemActivated() this frame) as opposed to a sample mid an
   // already-active drag. A session that is one continuous drag never sets
   // this past its very first sample, and plays back as smooth interpolation
   // like before. A session built from several separate grabs - shift-click
   // a value, release, shift-click another, release, i.e. "checkpoints" -
   // sets it on each grab's first sample, and GetPlaybackValue then holds
   // each checkpoint's value and jumps straight to the next rather than
   // sliding between them.
   void NotifyMovement(int nodeIndex, int paramIndex, float value, double nowSec, bool isNewGrab = false);

   // Call from a param widget's post-draw check once per frame (safe to call
   // unconditionally - it no-ops unless this param is currently armed). When
   // the widget's drag has just ended (ImGui::IsItemDeactivated()), finishes
   // an armed recording: turns its trace into a looping playback (if it has
   // at least two samples) and disarms. This is what lets "Start Recording"
   // stop itself the moment the user lets go, with no separate stop step.
   void MaybeFinishArmedRecording(int nodeIndex, int paramIndex, double nowSec);

   // Whether this param should render its "recording" (red) visual state
   // right now - true the instant it's armed (even before the first sample,
   // so the menu action reads as having taken effect immediately), true for
   // the rest of a Shift session once touched, and true again for as long as
   // its recorded trace is looping back into it (see GetPlaybackValue) once
   // the session ends.
   bool IsRecording(int nodeIndex, int paramIndex) const;

   // The user grabbed this control directly (not a shift-drag/armed
   // recording) - stop replaying its recorded trace and let them drive it
   // manually again. Also the "Unbind"/"Stop Recording" action from the
   // param's right-click menu.
   void StopPlayback(int nodeIndex, int paramIndex);

   // If this param has a finished recording looping, writes the interpolated
   // value it should hold at `nowSec` into `outValue` and returns true.
   // Callers only apply this when the param isn't otherwise driven (no wired
   // modulator, no expression) - same precedence rule as those two. Honors
   // this playback's speed multiplier and, if set, remaps the recorded
   // trace's own [min,max] onto a custom [rangeLo,rangeHi] - see
   // SetPlaybackSpeed/SetPlaybackRange.
   bool GetPlaybackValue(int nodeIndex, int paramIndex, double nowSec, float& outValue) const;

   // Playback rate multiplier for a looping recording - 1.0 is the recorded
   // speed, >1 replays faster, <1 slower. No-op if nothing is looping there.
   void SetPlaybackSpeed(int nodeIndex, int paramIndex, float speed);
   float PlaybackSpeedFor(int nodeIndex, int paramIndex) const;

   // Remaps a looping recording's own [recorded min, recorded max] onto
   // [lo, hi] before it's written back into the param - the same "Range"
   // concept a modulator binding has (Modulation::SetRange), applied to a
   // recorded trace instead of a live modulator signal. Cleared (falls back
   // to the raw recorded values) by ClearPlaybackRange. No-op if nothing is
   // looping there.
   void SetPlaybackRange(int nodeIndex, int paramIndex, float lo, float hi);
   void ClearPlaybackRange(int nodeIndex, int paramIndex);
   // Returns false (leaving lo/hi untouched) if no override is set.
   bool PlaybackRangeFor(int nodeIndex, int paramIndex, float& lo, float& hi) const;

   // Everything this node recorded (armed, mid-session, or looping) goes
   // away with it. Mirrors Modulation::UnbindAllFor / PaletteBinding::
   // UnbindAllFor at the node delete site - without it the (nodeIndex,
   // paramIndex) keys outlive the node, and since indices are handed out by
   // a running counter and reused across an Undo's respawn, a stale key can
   // silently start driving a completely unrelated param on whatever node
   // next lands on that index.
   void ClearForNode(int nodeIndex);

   // Drops every recording and any in-progress session. Called when the
   // whole graph goes away (NewPatch), for the same reason
   // Modulation::Clear() is - node indices restart from 1, so a leftover
   // recording would re-attach to whichever node lands on its index next.
   void Clear();

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
      float speed = 1.0f;          // see SetPlaybackSpeed
      bool hasRangeOverride = false;
      float rangeLo = 0.0f, rangeHi = 0.0f;    // see SetPlaybackRange
      float recordedMin = 0.0f, recordedMax = 0.0f; // min/max across samples, computed once at finalize
   };

   using PlaybackMap = std::map<Key, Playback>;

   // Undo/Redo support. A recording is session state, not patch content: it
   // is not in Patch::Data and never reaches a saved file. But undo still has
   // to make one appear and disappear at the right point in history - the
   // same way a typed expression does - so the undo stack snapshots this map
   // alongside the graph and hands it back through Restore(), keyed through
   // ApplyPatchData's old-index -> new-index remap. This mirrors
   // RemapViewportPanelNodes in main.cpp exactly.
   //
   // Any in-progress session (Shift still held, or a param still armed) is
   // deliberately NOT part of the snapshot: a half-drawn gesture is not a
   // state anyone can return to.
   const PlaybackMap& Playbacks() const { return mPlayback; }

   // Replaces every recording with `playbacks`, restarting each loop from
   // `nowSec`. Restarting rather than preserving startTime is the point: the
   // recorded timestamps come from a clock that has kept running since the
   // snapshot, so keeping the old startTime would drop the loop in at an
   // arbitrary phase. Also clears any in-progress session, since the graph
   // underneath it has just been rebuilt.
   void Restore(PlaybackMap playbacks, double nowSec);

   // Installs one recording, leaving every other param's alone. Used when a
   // node is duplicated or pasted: the copy inherits the original's loop,
   // speed and range override included, and starts in phase with it rather
   // than restarting from wherever the paste happened to land.
   void SetPlayback(int nodeIndex, int paramIndex, Playback playback);

private:
   // Turns an in-progress trace at `key` into a looping Playback (if it has
   // at least two samples) and always erases it from mSession - shared by
   // BeginFrame's Shift-release path and MaybeFinishArmedRecording.
   void FinalizeSession(const Key& key, double nowSec);

   bool mShiftHeld = false;
   std::set<Key> mArmedParams;
   std::map<Key, std::vector<Sample>> mSession;
   std::map<Key, Playback> mPlayback;
};
