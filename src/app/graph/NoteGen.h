#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "arrange/ArrangeModel.h"

// Notes for the Piano Roll from the Generate Samples engine
// (docs/plans/piano-roll/README.md §12). A type picks a part of a style card
// and how it is played; SampleStyles::Compile builds the same private patch
// the Generate Samples node renders, and OfflineGraph::RenderNotes records
// the notes its voices receive instead of the sound. The same spec and seed
// give the same notes, here and as a Sample Studio take. Main thread; a
// 4-bar take is a few ms.
namespace NoteGen
{
   enum class Type
   {
      Chords, // block chords through a progression
      Comp,   // the progression re-struck on a rhythm
      Pads,   // long held chords
      Strum,  // broken chords: stabs or a plucked arpeggio
      Arp,    // the progression through an arpeggiator
      Bass,   // a bass line in one of seven roles
      Melody, // a lead line, judged by the melody critic
      Drums,  // GM drum notes from the style's grooves
      Count
   };
   const char* TypeName(Type t); // "chords", "comp", ...
   bool TypeFromName(const std::string& name, Type& out);

   // Repeats the take's bars as A and a second draw (seed + 1) as B.
   enum class Form { None, AABA, ABAB, AAAB, Count };
   const char* FormName(Form f); // "none", "aaba", ...

   struct GenSpec
   {
      Type type = Type::Chords;
      std::string style = "deep_house";
      uint32_t seed = 1;
      int bars = 4;            // 1, 2, 4, 8; with a form, the take is four times this
      Form form = Form::None;
      int key = -1, scale = -1; // the page's key and scale (Transport); < 0: the card picks
      float creativity = 0.3f; // 0..1: how far the draws stray from the card
      float energy = 0.0f, mood = 0.0f; // -1..1, the Recipe dots
      bool personal = false;
      // < 0 everywhere below: the card's own value.
      float density = -1.0f;   // 0..1: thins notes off the beat (never adds)
      float swing = -1.0f;     // 0..0.75: odd sixteenths late by this share of a sixteenth
      float gate = -1.0f;      // 0.1..1: note lengths scaled by this
      int rangeLo = -1, rangeHi = -1; // pitches folded by octaves into this range; unset: the piano's 21..108
      int chordSize = -1, arpMode = -1, arpOctaves = -1, bassRole = -1; // see SampleStyles::Recipe
      // Locks, against `source` (the notes being regenerated): rhythm keeps
      // its starts and lengths and takes the new pitches in order; pitch keeps
      // its pitch order on the new rhythm.
      bool lockRhythm = false, lockPitch = false;
      std::vector<Arrange::Note> source;
   };

   struct Take
   {
      std::vector<Arrange::Note> notes; // sorted by (start, pitch), ids from 1
      Arrange::Tick length = 0;         // the take's span in ticks
      float bpm = 120.0f;               // the card's tempo (notes are in ticks, so only informative)
      int root = 0, scale = 0;          // the key the notes are in
      uint32_t seed = 1;                // the seed that passed (spec seed, or a redraw)
      int redraws = 0;
      float criticScore = -1.0f;        // melody only: the critic's score, < 0 when not judged
      std::string error;                // non-empty: nothing generated
   };

   Take Generate(const GenSpec& spec);

   // "type=chords style=deep_house seed=42 bars=4 ..." with defaults left out,
   // and back. Unknown keys are ignored, so a newer spec loads in an older build.
   std::string Serialize(const GenSpec& spec);
   bool Parse(const std::string& text, GenSpec& out);

   // INFINITE_NOTEGENTEST (src/app/selftest/NoteGenTest.cpp): README §12.6's checks.
   bool RunSelfTest();
}
