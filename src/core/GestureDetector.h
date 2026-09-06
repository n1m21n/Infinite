#pragma once

#include <deque>
#include <utility>
#include <vector>

// Detects the "manual modulation" gesture: while a user tweaks params in a
// patch, repeatedly alternating between the same small set of controls in a
// short window is itself a signal - they're already performing by hand a
// relationship the modulation system could automate. See the design brief
// this implements (gesture-suggested-modulation.md) for the full rationale.
//
// This class only detects the pattern and holds a single pending suggestion;
// it deliberately does NOT spawn nodes or call Modulation::Bind() itself,
// because both of those need SpawnNode()/PushUndoCheckpoint()/gNodes, which
// are private to main.cpp's anonymous namespace. main.cpp reads
// PendingSuggestion() to render the suggestion cable + checkmark, and calls
// Dismiss() once it has performed the real spawn+bind (see
// ConfirmGestureSuggestion in main.cpp) or once the suggestion is otherwise
// no longer wanted.
class GestureDetector
{
public:
   static GestureDetector& Instance();

   enum class Kind
   {
      SelfOscillation, // one param snapping between ~two values -> suggest a Pattern
      TwoParamLink,     // two params alternating -> suggest a direct link
      Fanout            // 3+ params cycling -> suggest one source, many links
   };

   struct Suggestion
   {
      Kind kind = Kind::TwoParamLink;
      int sourceNode = -1;
      int sourceParam = -1;
      // TwoParamLink: exactly one entry. Fanout: every other param in the
      // cycle. Unused for SelfOscillation.
      std::vector<std::pair<int, int>> destinations;
      // SelfOscillation only: the two observed value clusters and an
      // estimated step rate, used to pre-configure a spawned PatternNode.
      float clusterLow = 0.0f;
      float clusterHigh = 1.0f;
      float stepBeatsEstimate = 1.0f;
      double expiresAtSec = 0.0;
   };

   // Global on/off. While false, NotifyTouch() is a no-op and any pending
   // suggestion is dropped on the next PendingSuggestion() call.
   bool enabled = true;
   // Rolling window, in seconds, touches must fall within to count as part of
   // the same gesture. Doc default: 10-15s.
   double windowSeconds = 12.0;
   // Alternations required to trigger a suggestion. Internally this reads as
   // N = 2 * sensitivity recent touches needing to fit the pattern.
   int sensitivity = 3;

   // Call from the one place a param widget registers a user-initiated touch
   // (ModKnob/ModSlider's plain-interactive IsItemActivated() branches - see
   // main.cpp). `nowSec` is the caller's own clock (ImGui::GetTime()), passed
   // in rather than read here so detection stays independently testable.
   void NotifyTouch(int nodeIndex, int paramIndex, float value, double nowSec);

   // The suggestion to draw this frame, or nullptr if none is pending or the
   // pending one has expired (expiry, and the enabled/disabled check, are
   // both resolved here, not by the caller).
   const Suggestion* PendingSuggestion(double nowSec);

   // Drops the pending suggestion without acting on it - called after a real
   // confirm, or to discard it outright.
   void Dismiss();

private:
   struct TouchEvent
   {
      int nodeIndex;
      int paramIndex;
      float value;
      double timeSec;
   };
   using Key = std::pair<int, int>;

   void Evict(double nowSec);
   void Detect(double nowSec);
   bool DetectSelfOscillation(const std::vector<Key>& collapsed, double nowSec);
   bool DetectTwoParamLink(const std::vector<Key>& collapsed, double nowSec);
   bool DetectFanout(const std::vector<Key>& collapsed, double nowSec);

   std::deque<TouchEvent> mRecent;
   Suggestion mPending;
   bool mHasPending = false;
};
