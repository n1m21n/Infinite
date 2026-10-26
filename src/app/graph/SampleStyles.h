#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/graph/OfflineGraph.h"

// The music style cards (tools/catalog/styles_music.json), compiled into a
// patch the Generate Samples node renders. A port of tools/universe/styled.py
// build(): the same card fields mean the same thing, so a style sounds like
// itself in the app and in the corpus tools. Deterministic in the recipe, not
// bit-exact with the Python (its own random stream).
namespace SampleStyles
{
   struct Style
   {
      const char* id;
      const char* name;   // lowercase display name
      const char* family; // dropdown group
   };
   int Count();
   Style At(int i);
   int Find(const std::string& id); // -1 when unknown

   // Which part of the style a sample is: the whole mix, or one layer of it.
   // A part is drawn from the same seed as the full mix, so "drums" of a seed
   // is the drum layer of that seed's full loop (percussion and fx, which the
   // full mix may not have, are made from the card's own figures; pads and
   // plucks are the card's chords with the voice's envelope set for them).
   enum class Part
   {
      Full,       // "all": the whole mix
      Synths,     // bass and the motif line on the card's voices
      Pads,       // the chords held long on a slow-swelling voice
      Plucks,     // the chords arpeggiated on a short, plucked voice
      Percussion, // clap, toms, bell
      Drums,      // kick, snare, hats
      Count
   };
   const char* PartName(Part p); // lowercase: "all", "synths", ...

   struct Recipe
   {
      std::string style = "deep_house";
      uint32_t seed = 1;
      float variation = 0.5f; // 0 = node defaults, 1 = twice the corpus's spread
      int bars = 2;
      // <= 0: the card picks (its tempo band, a random root). Otherwise these.
      float bpm = 0.0f;
      int key = -1;
      int scale = -1; // MusicTime scale; < 0: the card picks
      Part part = Part::Full;
   };

   struct Compiled
   {
      std::string patch;   // .inf text: the generated graph, readable as a patch
      int tap = 0;         // node index whose output is the sample
      float bpm = 120.0f;
      int root = 0;
      int scale = 0;
      std::vector<app::OfflineGraph::Motion> motions;
      std::string error;   // non-empty: unknown style
   };
   Compiled Compile(const Recipe& r);

   // Every type a style can place as a synth, effect or note effect.
   std::vector<std::string> PoolTypes();
   // The smallest patch that sounds `type` at its defaults: a steady note
   // sequence into it (a synth), through it into a synth (a note effect), or
   // a synth through it (an effect). The worker self-test renders each.
   Compiled Probe(const std::string& type);
}
