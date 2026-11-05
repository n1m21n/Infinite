#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A Standard MIDI File (format 0, 1 or 2; plain or RIFF/RMID-wrapped) reduced
// to what the MIDI File node plays: note-ons and note-offs placed on a beat
// timeline (ticks / division). The file's own tempo and tempo changes are
// ignored - it is just notes on a beat grid, played at the project bpm times
// the node's speed. SMPTE-division files are absolute time, so they map at a
// fixed 2 beats per second (120 bpm reference). Format 2 (independent
// sequences) is played as parallel tracks. Pitch bend, CCs and program
// changes are not played - this node is a note source.
namespace MidiFile
{
   struct Event
   {
      double beat = 0.0;
      uint8_t note = 0;
      uint8_t velocity = 0; // 0 for a note-off
      uint8_t track = 0;    // index among tracks that hold notes
      uint8_t channel = 0;  // 0-based; 9 is General MIDI percussion
      bool on = false;
   };

   struct Song
   {
      std::vector<Event> events; // sorted by beat, note-offs before note-ons at the same beat
      double lengthBeats = 0.0;  // beat of the last event
      int trackCount = 0;        // tracks that hold at least one note
      int noteCount = 0;
      uint8_t lowNote = 127, highNote = 0;
   };

   // Parses `size` bytes. On failure returns false and fills `error`.
   bool Parse(const uint8_t* data, size_t size, Song& out, std::string& error);
   bool Load(const std::string& path, Song& out, std::string& error);

   // A note to write: start and length in beats.
   struct Note
   {
      double beat = 0.0, length = 0.25;
      uint8_t pitch = 60, velocity = 100, channel = 0;
   };
   // A format-0 file at 480 ticks per beat, with `bpm` as its tempo. Parse reads it back.
   std::vector<uint8_t> Encode(const std::vector<Note>& notes, double bpm);
   bool Save(const std::string& path, const std::vector<Note>& notes, double bpm, std::string& error);
}
