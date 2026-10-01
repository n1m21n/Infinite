#pragma once

// Film events -> note schedule for --audio-summary --notes (R471 7.5).
// GL- and audio-engine-free: reads the two JSON files and produces a flat,
// time-sorted list of note hits that main.cpp injects at sample offsets.

#include <string>
#include <vector>

namespace Headless
{
   struct NoteHit
   {
      double t = 0.0;      // note-on time, seconds on the film clock
      double length = 0.0; // seconds until the matching note-off
      int pitch = 60;
      float velocity = 1.0f;
      std::string node;    // id word or index, as written in the map
   };

   struct NoteSchedule
   {
      std::vector<NoteHit> hits;                 // sorted by t
      std::vector<std::string> unmappedTypes;    // event types with no map entry, ignored
      std::vector<std::string> panIgnoredNodes;  // maps that asked for pan_from; notes carry no pan
      std::vector<std::string> nodes;            // distinct target node refs, first-use order
   };

   // False with `error` set when a file is missing or not the expected shape.
   bool LoadNoteSchedule(const std::string& eventsPath, const std::string& mapPath, NoteSchedule& out, std::string& error);
}
