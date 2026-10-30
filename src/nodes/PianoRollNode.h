#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

namespace Arrange { struct NoteSchedule; }
class AudioPianoRollNode;

// The canvas end of a Piano Roll page (docs/plans/piano-roll/README.md §6):
// plays the notes of the MIDI lane linked to it, on the transport's beat
// timeline. The lane lives in the arrangement, not in the node; the app
// flattens it into a NoteSchedule and hands it over whenever it changes, so
// this node only ever plays what it was last given. An optional note input
// passes through (thru) and merges with the lane's notes.
class PianoRollNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PianoRollNode(); }
   PianoRollNode();
   ~PianoRollNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }

   int transpose = 0;           // semitones, on top of each clip's own transpose
   float velocity = 1.0f;       // scales every played velocity
   bool useGlobalScale = false; // snap played notes to the Transport key/scale
   bool thru = true;            // pass the note input through

   NoteCable noteInput;

   // Main thread: replaces what the node plays. Notes held across the swap
   // keep sounding if the new schedule still has them, and end at once if
   // it does not (deleted, or shortened past the playhead).
   void SetSchedule(std::unique_ptr<Arrange::NoteSchedule> s);
   const Arrange::NoteSchedule* Schedule() const { return mCurrent; }
   // The linked lane's name, for the body ("" = no lane linked).
   std::string laneName;
   uint64_t laneId = 0;
   uint64_t scheduleKey = 0; // the publisher's change key for the current schedule
   // Main-thread readout of the playhead in timeline beats, -1 when stopped.
   double PlayheadBeats() const;
   // Main thread: sounds a key now, transport or not (the panel's audition).
   void Audition(int pitch, float velocity, bool on);

private:
   void RetireSchedules();

   std::unique_ptr<AudioPianoRollNode> mAudioNode;
   // Every schedule handed over and not yet known to be out of the audio
   // thread's hands. The last one is current.
   std::vector<std::unique_ptr<Arrange::NoteSchedule>> mSchedules;
   const Arrange::NoteSchedule* mCurrent = nullptr;
   uint64_t mNextGen = 1;
   long long mBlocksSeen = -1;     // audio block counter at the last check
   long long mBlocksStillMs = 0;   // when that counter last moved
   int mLastCookFrame = -1;
};
