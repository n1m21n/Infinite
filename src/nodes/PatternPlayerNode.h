#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "core/INode.h"
#include "core/NoteCable.h"
#include "nodes/NotePatternLibrary.h"

// Turbo 0.57: Pattern Player. Plays a pattern from the note pattern bank
// (NotePatternLibrary.h) over a chord: the notes held on its input, or, with
// nothing played into it, a chord built from the global key and scale (pick
// the scale degree and chord size), so it runs on its own with play / stop.
// Locked to the transport; starts on the next bar.
//
// Two-object rule: this INode (main thread) owns an AudioPatternPlayerNode
// that schedules the notes on the audio thread; the pattern is handed over
// through a lock-free triple buffer, params through atomics.
class AudioPatternPlayerNode;

class PatternPlayerNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PatternPlayerNode(); }
   PatternPlayerNode();
   ~PatternPlayerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "chord" : nullptr; }
   AudioNode* GetAudioNode() override;
   // Runs with nothing wired into it (global-key mode).
   bool RequiresAudioProcessing() const override { return true; }

   enum Source { kAuto = 0, kInput, kGlobal };

   bool playing = false;
   bool latch = true;          // keeps the last chord after the keys are released
   bool syncBar = true;        // (re)starts on the next bar
   bool thru = false;          // also passes the input notes through
   int source = kAuto;
   int degree = 1;             // 1..7, chord built on this degree of the global scale
   int chordSize = 3;          // 3..5 notes (triad, 7th, 9th)
   int octave = 3;             // octave of the global chord root (3 = C3)
   int transpose = 0;          // semitones, -24..24
   int speed = 1;              // 0 = half, 1 = as written, 2 = double
   float gate = 0.85f;         // 0.05..1, times each note's length
   float swing = 0.0f;         // 0..1, delays every other step
   float velocity = 1.0f;      // 0..2
   int trimStart = 0;          // first step that plays
   int trimEnd = 0;            // one past the last step (0 = the pattern's end)
   bool fitScale = false;      // snaps .mid (semitone) notes to the global scale
   std::string category = "Arp";
   std::string patternName = "Up";
   NoteCable noteInput;

   // Main thread.
   int PatternIndex() const;               // index into NotePatterns::Current().patterns, -1 if missing
   void SelectPattern(int libraryIndex);
   void Step(int delta);                   // previous / next in the category
   void RandomInCategory();
   // Copies a .mid file into the bank (user folder, "Imported"), rescans and selects it.
   bool ImportMidiFile(const std::string& path, std::string& error);
   const NotePatterns::Pattern* CurrentPattern() const;

   // Readouts for the body.
   int PlayingStep() const;                // step under the playhead, -1 when idle
   int CurrentTones(int out[8]) const;     // the chord in use (MIDI keys)
   bool Running() const;

   static const char* SpeedName(int speed);

private:
   std::unique_ptr<AudioPatternPlayerNode> mAudioNode;
   int mLastCookFrame = -1;
   std::string mPushedKey;                 // category/name last handed to the audio half
   unsigned mPushedGen = 0;
   void PushPattern();
};
