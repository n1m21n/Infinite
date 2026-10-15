#pragma once

#include <string>
#include <vector>

// Turbo 0.57: the note pattern bank shared by the Pattern Player (plays a
// pattern over a chord) and the Note Looper (saves a take into the bank).
//
// A pattern is a loop of notes placed on a step grid. Most bundled patterns
// are chord-relative: a note says "the 2nd tone of the current chord, one
// octave up", so the same arpeggio or strum works on any chord, the way the
// pattern banks of groovebox firmwares do. Patterns read from .mid files are
// semitone-relative to the file's key (its key signature's tonic; the lowest
// note when it has none), transposed to the chord root.
//
// Bundled patterns are written for Infinite-Turbo (MIT); the user folder
// (%LOCALAPPDATA%\Infinite\NotePatterns, one subfolder per category) holds
// .mid files from anywhere, including takes saved by the Note Looper.
namespace NotePatterns
{
   enum Kind : unsigned char
   {
      kTone = 0,   // tone-th note of the chord (0-based; past the top wraps up an octave)
      kChord,      // every tone of the chord
      kBass,       // lowest tone, an octave down
      kRandom,     // a random chord tone
      kSemitone,   // `tone` semitones above the chord root (patterns from .mid)
   };

   struct PNote
   {
      float step = 0.0f;   // start, in steps from the loop start
      float len = 1.0f;    // length in steps (before the player's gate)
      int tone = 0;
      int oct = 0;
      float vel = 0.8f;    // 0..1
      Kind kind = kTone;
   };

   struct Pattern
   {
      std::string category;
      std::string name;
      std::string source;      // "built-in" or the .mid path
      int steps = 16;          // loop length in steps
      float stepBeats = 0.25f; // 1/16 by default
      std::vector<PNote> notes;
   };

   // Step tokens, separated by spaces (one token = one step):
   //   .        rest                 -   tie: extends the notes of the step before
   //   1..9     chord tone (1 = root; past the chord's size wraps up an octave)
   //   c        whole chord          b   bass (lowest tone, octave down)    r  random tone
   //   a+b      several at once (1+3, b+c)
   // Modifiers after an element: ' octave up, , octave down, ! accent, ? soft, ~ half length.
   // A token suffix *2 / *3 / *4 repeats the step's notes inside the step (ratchet).
   bool ParseTokens(const std::string& text, float stepBeats, Pattern& out, std::string& error);

   // The bundled bank (parsed once).
   const std::vector<Pattern>& BuiltIn();

   // %LOCALAPPDATA%\Infinite\NotePatterns, created when missing; "" if there is no data folder.
   std::string UserFolder();

   // Built-in plus every .mid under UserFolder() (category = first subfolder, or "My patterns").
   struct Library
   {
      std::vector<std::string> categories; // built-in categories first, in bank order
      std::vector<Pattern> patterns;       // grouped by category
      std::vector<int> InCategory(const std::string& category) const;
   };
   const Library& Current();
   void Rescan(); // main thread; re-reads the user folder

   // .mid helpers.
   struct MidiNote
   {
      double beat = 0.0;
      double dur = 0.25;
      int key = 60;
      int vel = 100;
   };
   bool LoadMidiPattern(const std::string& utf8Path, Pattern& out, std::string& error);
   // Writes a format 0 file (480 ppq) with a tempo and time signature.
   bool WriteMidi(const std::string& utf8Path, const std::vector<MidiNote>& notes, double bpm, int timeSigNum,
                  int timeSigDen, double lengthBeats, std::string& error);
}
