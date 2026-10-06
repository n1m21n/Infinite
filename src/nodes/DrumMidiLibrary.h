#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace MidiFile
{
   struct Data;
}

// Turbo 0.50: drum patterns from the user's own MIDI files, for the Drum
// Sequencer. Two halves, both free of JUCE / ImGui so they build and test on
// any host compiler:
//
//  - Analyze: a Standard MIDI File's drum notes onto the 8 lanes and a 16th
//    (or 16th-triplet) step grid, bar by bar through the time signature map,
//    and SplitParts, which picks the A / B / C chunks of a long file.
//  - The folder library: %LOCALAPPDATA%\Infinite\DrumPatterns scanned
//    recursively on a background thread (index.json aware), read by the UI
//    and MCP through an immutable snapshot.
//
// Nothing here ships pattern data: the files stay in the user's folder.
namespace DrumMidi
{
   constexpr int kLanes = 8;

   // General MIDI percussion key -> lane (0 kick, 1 snare, 2 closed hat,
   // 3 open hat, 4 clap / rim / clave, 5 low tom / conga, 6 high tom /
   // conga, 7 bell / ride / cymbals), -1 outside the drum map.
   int GmDrumLane(int key);

   // Velocity 0..127 -> cell value: >= 100 is an accent (1.0).
   float CellVelocity(int velocity);

   // A whole file on the step grid, from its first bar holding a drum note.
   struct Pattern
   {
      bool triplet = false;   // 16th triplets instead of 16ths
      float swing = 0.0f;     // 0..1, the node's swing (odd 16ths late by swing * 0.5 step)
      std::vector<int> barSteps;                  // steps of each bar kept
      std::vector<std::array<float, kLanes>> cells; // one per step, 0 = off
      std::string meter;      // "4/4", or "4/4 + 7/8" when it changes
      double bpm = 0.0;       // first tempo of the file
      int tempoChanges = 0;   // tempo events after the first
      int notes = 0;          // drum notes read
      int placed = 0;         // distinct (step, lane) cells set
      int merged = 0;         // notes landing on a cell already set (flams, rolls, two keys on one lane)
      int skipped = 0;        // notes outside the GM drum map
      int otherChannel = 0;   // notes ignored because channel 10 has the drums
      int leadBars = 0;       // empty bars dropped before the first note (count-in)
      int pickupNotes = 0;    // notes of a dropped pickup (anacrusis) bar
      unsigned lanesUsed = 0; // bit per lane
      bool allChannels = false; // no channel 10 notes: every channel read

      int Bars() const { return (int)barSteps.size(); }
      int Steps() const { return (int)cells.size(); }
      int BarStart(int bar) const; // first step of `bar`
   };

   // False with `error` set when the file has no usable drum notes.
   bool Analyze(const MidiFile::Data& data, Pattern& out, std::string& error);

   // One part: `bars` bars from `firstBar`.
   struct PartSpan
   {
      int firstBar = 0;
      int bars = 0;
      int steps = 0;
      bool copyOfA = false; // the file is too short for its own chunk
   };

   // A = the first chunk of whole bars that fits maxSteps (the whole file
   // when it fits). For a longer file the chunk is a power of two of bars
   // (8 bars of 4/4 16ths at 128 steps) and B / C are the next chunks that
   // differ from the ones already used (a repeated chunk is skipped, so a
   // long arrangement gives three distinct sections). With no such chunk B
   // and C are copies of A.
   void SplitParts(const Pattern& p, int maxSteps, PartSpan out[3]);

   // "bars 1-8", "bar 17", "= A".
   std::string SpanLabel(const PartSpan& s);

   // ---------------------------------------------------------- the folder
   struct Entry
   {
      std::string path;   // UTF-8, absolute
      std::string title;  // index.json title, else the cleaned file name
      std::string group;  // index.json style, else the subfolder name
      int bpm = 0;        // index.json bpm, else the file's first tempo (0 unknown)
      int bars = 0;       // bars the analysis found (0 not read / unreadable)
      int indexBars = 0;  // index.json "compassos" / "bars" (0 none)
      bool empty = false; // read, but no drum notes
      bool unreadable = false; // not a MIDI file (or over 4 MB)
   };

   struct Library
   {
      std::string root;
      std::vector<std::string> groups; // sorted, case-insensitive
      std::vector<Entry> entries;      // by group, then title
      std::vector<std::string> keys;   // normalised path per entry, for Find
      std::string error;               // folder missing / unreadable
      bool scanned = false;            // false until the first scan finished
      bool scanning = false;
      bool truncated = false;          // more files than the scan keeps
      unsigned generation = 0;

      std::vector<int> InGroup(const std::string& group) const;
      int GroupIndex(const std::string& group) const;
      int Find(const std::string& path) const; // entry index by path, -1
   };

   // "001_breakbeat_Ogugua_s_solo.mid" -> "breakbeat Ogugua s solo".
   std::string TitleFromFileName(const std::string& fileName);

   // Synchronous scan (the background thread runs this). parseFiles also
   // reads every file for its bar count, tempo and emptiness.
   Library Scan(const std::string& root, bool parseFiles);

   // %LOCALAPPDATA%\Infinite\DrumPatterns, created when missing; "" when
   // there is no user data folder.
   std::string DefaultFolder();

   // Starts a background scan of DefaultFolder() (queued when one runs).
   void RequestScan();
   // Starts the first scan if none ran yet.
   void EnsureScanned();
   // The latest finished scan (never null; empty before the first one).
   std::shared_ptr<const Library> Current();
}
