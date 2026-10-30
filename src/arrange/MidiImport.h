#pragma once
// A parsed .mid onto a MIDI lane (docs/plans/piano-roll/README.md §4.3, §3.2):
// one clip holding every note of every track, on the beat grid the file
// already maps to. Pure model code, so the checks run headless.
#include "ArrangeModel.h"
#include "../audio/MidiFile.h"

namespace Arrange
{
   // Places a clip at `at` on `laneId`, whole bars long (at least one), and
   // fills it with the song's notes. A note left open at the end of the file
   // ends there. Notes past kMaxNotesPerLane are dropped. `loop` makes the
   // clip loop its own length. Returns the clip id, 0 on failure (not a MIDI
   // lane, or a song with no notes).
   uint64_t ImportSong(Model& m, uint64_t laneId, const MidiFile::Song& song, Tick at, bool loop = false);
}
