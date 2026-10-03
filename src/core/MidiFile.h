#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Turbo 0.48: Standard MIDI File reader (format 0 and 1, PPQ division).
// Self-contained on purpose (no JUCE): the MIDI File node and any future
// importer (Arrangement Timeline MIDI clips) share it, and it can be built
// and tested on any host compiler.
//
// Robustness contract: Parse never reads outside the buffer it was given.
// A truncated or garbled track stops at the first byte that cannot be
// decoded and keeps what came before it (reported in `warning`); only a
// missing/invalid header or an SMPTE division fails the whole parse.
namespace MidiFile
{
   enum EventKind : uint8_t
   {
      kNoteOff = 0,
      kNoteOn,          // velocity 0 is already turned into kNoteOff
      kPolyPressure,
      kControlChange,
      kProgramChange,
      kChannelPressure,
      kPitchBend,       // value = 14-bit bend, 8192 = centre
      kTempo,           // value = microseconds per quarter note
      kTimeSignature,   // data1 = numerator, data2 = denominator (already 2^dd)
      kEndOfTrack,
      kTrackName,       // text in Track::name
   };

   struct Event
   {
      uint64_t tick = 0;
      uint16_t track = 0;   // 0-based chunk index
      uint8_t kind = 0;     // EventKind
      uint8_t channel = 0;  // 0..15, channel events only
      uint8_t data1 = 0;    // note / controller / program
      uint8_t data2 = 0;    // velocity / value
      uint32_t value = 0;   // tempo, pitch bend
   };

   // A note-on paired with its note-off (first-in, first-out per track,
   // channel and key). A note still open at the end of its track ends there.
   struct Note
   {
      uint64_t startTick = 0;
      uint64_t endTick = 0;
      uint16_t track = 0;   // 0-based
      uint8_t channel = 0;  // 0..15
      uint8_t key = 0;
      uint8_t velocity = 0; // 1..127
   };

   struct Track
   {
      std::string name;
      int noteCount = 0;
      int eventCount = 0;
      uint16_t channelMask = 0; // bit c = channel c carries notes
      uint64_t endTick = 0;
   };

   struct TempoChange
   {
      uint64_t tick = 0;
      double bpm = 120.0;
   };

   struct Data
   {
      int format = 0;
      int ppq = 480;
      std::vector<Track> tracks;
      std::vector<Event> events;         // every track, in file order per track
      std::vector<Note> notes;           // sorted by startTick, then key
      std::vector<TempoChange> tempos;   // sorted by tick, may be empty
      int timeSigNum = 4;                // first time signature (4/4 if none)
      int timeSigDen = 4;
      uint64_t lengthTicks = 0;          // latest end of track / note end
      std::string warning;               // non-fatal problems, empty if none

      double FirstTempo() const { return tempos.empty() ? 120.0 : tempos.front().bpm; }
      double TicksToBeats(uint64_t tick) const { return (double)tick / (double)(ppq > 0 ? ppq : 480); }
      double LengthBeats() const { return TicksToBeats(lengthTicks); }
   };

   // Parses an in-memory SMF (also accepts the RIFF "RMID" wrapper).
   // Returns false with `error` set when nothing usable could be read.
   bool Parse(const uint8_t* bytes, size_t size, Data& out, std::string& error);

   // Reads a file (UTF-8 path) and parses it. Files over 64 MB are refused.
   bool Load(const std::string& utf8Path, Data& out, std::string& error);

   const char* KindName(uint8_t kind);
}
