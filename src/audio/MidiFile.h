#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A Standard MIDI File (format 0 or 1) reduced to what the MIDI File node
// plays: note-ons and note-offs placed on a beat timeline. Tempo and time
// signature meta events are ignored on purpose - the Infinite transport owns
// tempo, so the file's notes land on the same beats whatever bpm is set.
// SMPTE-division files (frames per second, not ticks per beat) are rejected.
namespace MidiFile
{
   struct Event
   {
      double beat = 0.0;
      uint8_t note = 0;
      uint8_t velocity = 0; // 0 for a note-off
      uint8_t track = 0;    // index among tracks that hold notes
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
}
