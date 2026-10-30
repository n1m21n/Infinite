// Headless checks for the Piano Roll's note model (docs/plans/piano-roll/README.md §5, §10 Block A):
// every note op keeps the invariants, bumps revision once, and trim/split/duplicate move content without
// rewriting notes. Exit code 0 when all pass.
#include "ArrangeModel.h"
#include "NoteSchedule.h"
#include "MidiImport.h"

#include <cstdio>
#include <string>

using namespace Arrange;

namespace
{
   int gFails = 0;
   void Check(bool ok, const char* what)
   {
      printf("%s %s\n", ok ? "ok  " : "FAIL", what);
      if (!ok)
         gFails++;
   }

   void CheckValid(const Model& m, const char* what)
   {
      std::string why;
      const bool ok = Validate(m, &why);
      if (!ok)
         printf("     %s\n", why.c_str());
      Check(ok, what);
   }

   Note N(Tick start, Tick length, int pitch, int vel = 100)
   {
      Note n;
      n.start = start;
      n.length = length;
      n.pitch = (uint8_t)pitch;
      n.vel = (uint8_t)vel;
      return n;
   }

   // A model with one MIDI lane holding one 4-bar clip; returns the clip id.
   uint64_t OneClip(Model& m, uint64_t* outLane = nullptr)
   {
      const uint64_t lane = AddLane(m, kLaneMidi);
      Clip c;
      c.start = 0;
      c.length = 16 * kPPQ;
      uint64_t id = 0;
      PlaceOverwrite(m, lane, c, &id);
      if (outLane)
         *outLane = lane;
      return id;
   }

   const Clip& ClipOf(Model& m, uint64_t id) { return *FindClipOrSlot(m, id); }
}

int main()
{
   // --- lanes ------------------------------------------------------------
   {
      Model m;
      const uint64_t lane = AddLane(m, kLaneMidi);
      Check(lane != 0 && FindLane(m, lane)->type == kLaneMidi, "AddLane makes a MIDI lane");
      Check(FindLane(m, lane)->name.rfind("MIDI", 0) == 0, "MIDI lane is named MIDI");
      CheckValid(m, "empty MIDI lane validates");
   }

   // --- add / edit / delete ------------------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      const uint64_t rev = m.revision;
      std::vector<uint32_t> ids;
      Check(AddNotes(m, clip, { N(kPPQ, kPPQ, 64), N(0, kPPQ, 60), N(0, kPPQ, 67) }, &ids), "AddNotes");
      Check(m.revision == rev + 1, "AddNotes bumps revision once");
      const Clip& c = ClipOf(m, clip);
      Check(ids.size() == 3 && ids[0] != ids[1] && ids[1] != ids[2], "AddNotes mints fresh ids");
      Check(c.notes.size() == 3 && c.notes[0].pitch == 60 && c.notes[1].pitch == 67 && c.notes[2].pitch == 64,
            "notes sorted by start then pitch");
      CheckValid(m, "after AddNotes");

      Note e = *FindNote(c, ids[0]);
      e.vel = 20;
      e.chance = 0.5f;
      e.bend = { { 0, 0.0f }, { kPPQ / 2, 1.0f } };
      Check(EditNotes(m, clip, { e }), "EditNotes");
      Check(FindNote(ClipOf(m, clip), ids[0])->vel == 20 && FindNote(ClipOf(m, clip), ids[0])->bend.size() == 2,
            "EditNotes keeps the edit and expression");
      Check(!EditNotes(m, clip, { e }), "EditNotes with no change is refused");

      Check(DeleteNotes(m, clip, { ids[1] }) && ClipOf(m, clip).notes.size() == 2, "DeleteNotes");
      Check(!DeleteNotes(m, clip, { 999 }), "DeleteNotes of an unknown id is refused");
      CheckValid(m, "after edit and delete");
   }

   // --- overlaps -----------------------------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      std::vector<uint32_t> ids;
      AddNotes(m, clip, { N(0, 4 * kPPQ, 60) }, &ids);
      AddNotes(m, clip, { N(kPPQ, kPPQ, 60) });
      const Clip& c = ClipOf(m, clip);
      Check(c.notes.size() == 2 && c.notes[0].End() <= c.notes[1].start, "same-pitch overlap cut, later wins");
      CheckValid(m, "after overlap");
   }

   // --- move ---------------------------------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      std::vector<uint32_t> ids;
      AddNotes(m, clip, { N(kPPQ, kPPQ, 60), N(2 * kPPQ, kPPQ, 64) }, &ids);
      Check(MoveNotes(m, clip, ids, -5 * kPPQ, 0), "MoveNotes left past 0 clamps");
      Check(ClipOf(m, clip).notes[0].start == 0 && ClipOf(m, clip).notes[1].start == kPPQ,
            "block keeps its shape when clamped");
      Check(MoveNotes(m, clip, ids, 0, 100) && ClipOf(m, clip).notes[1].pitch == 127 &&
            ClipOf(m, clip).notes[0].pitch == 123, "pitch clamp keeps the interval");
      // C major mask: C D E F G A B.
      const uint16_t cMajor = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11);
      Model m2;
      const uint64_t clip2 = OneClip(m2);
      std::vector<uint32_t> ids2;
      AddNotes(m2, clip2, { N(0, kPPQ, 60) }, &ids2);
      MoveNotes(m2, clip2, ids2, 0, 1, cMajor);
      Check(ClipOf(m2, clip2).notes[0].pitch == 62, "scale snap: C+1 lands on D");
      CheckValid(m, "after move");
   }

   // --- split / join -------------------------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      std::vector<uint32_t> ids;
      Note n = N(0, 2 * kPPQ, 60);
      n.pressure = { { 0, 0.2f }, { kPPQ + 10, 0.9f } };
      AddNotes(m, clip, { n }, &ids);
      Check(SplitNotes(m, clip, ids, kPPQ), "SplitNotes");
      const Clip& c = ClipOf(m, clip);
      Check(c.notes.size() == 2 && c.notes[0].length == kPPQ && c.notes[1].start == kPPQ, "split halves");
      Check(c.notes[0].pressure.size() == 1 && c.notes[1].pressure.size() == 1 && c.notes[1].pressure[0].at == 10,
            "split re-bases the right half's expression");
      Check(JoinNotes(m, clip, { c.notes[0].id, c.notes[1].id }) && ClipOf(m, clip).notes.size() == 1 &&
            ClipOf(m, clip).notes[0].length == 2 * kPPQ, "JoinNotes puts it back");
      CheckValid(m, "after split and join");
   }

   // --- quantize -----------------------------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      std::vector<uint32_t> ids;
      AddNotes(m, clip, { N(kPPQ / 4 + 30, 100, 60), N(kPPQ - 20, 100, 62) }, &ids);
      QuantizeSpec q;
      q.grid = kPPQ / 4;
      Check(QuantizeNotes(m, clip, ids, q), "QuantizeNotes");
      Check(ClipOf(m, clip).notes[0].start == kPPQ / 4 && ClipOf(m, clip).notes[1].start == kPPQ,
            "full strength lands on the grid");
      Check(ClipOf(m, clip).notes[0].length == 100, "lengths kept when ends are off");
      CheckValid(m, "after quantize");
   }

   // --- clip ops move content, not notes ------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      AddNotes(m, clip, { N(0, kPPQ, 60), N(8 * kPPQ, kPPQ, 62) });
      const std::vector<Note> before = ClipOf(m, clip).notes;
      Check(TrimEdge(m, clip, 0, 2 * kPPQ), "trim the left edge");
      Check(ClipOf(m, clip).contentOffset == 2 * kPPQ && ClipOf(m, clip).notes == before,
            "trim advances contentOffset, notes untouched");
      uint64_t right = 0;
      Check(Split(m, clip, 6 * kPPQ, &right), "split the clip");
      Check(ClipOf(m, right).contentOffset == 6 * kPPQ && ClipOf(m, right).notes == before,
            "right piece shows the later content");
      Check(MidiContentTick(ClipOf(m, right), 2 * kPPQ) == 8 * kPPQ, "content tick through offset");
      std::vector<uint64_t> made;
      Check(DuplicateBlock(m, { right }, &made) && made.size() == 1 && ClipOf(m, made[0]).notes == before,
            "duplicate copies notes");
      CheckValid(m, "after clip ops");
   }

   // --- loops ---------------------------------------------------------------
   {
      Clip c;
      c.length = 16 * kPPQ;
      c.midiLoop = true;
      c.loopStart = 0;
      c.loopLength = 4 * kPPQ;
      Check(MidiContentTick(c, 5 * kPPQ) == kPPQ, "loop wraps content");
      c.contentOffset = 2 * kPPQ;
      Check(MidiContentTick(c, 3 * kPPQ) == kPPQ, "offset then loop");
   }

   // --- links ---------------------------------------------------------------
   {
      Model m;
      uint64_t laneA = 0;
      OneClip(m, &laneA);
      const uint64_t laneB = AddLane(m, kLaneMidi);
      const uint64_t audio = AddLane(m, kLaneAudio);
      Check(LinkLane(m, laneA, 42), "link lane to node");
      Check(LinkLane(m, laneB, 42) && FindLane(m, laneA)->noteNodeUid == 0 && FindLane(m, laneB)->noteNodeUid == 42,
            "relinking a node moves it, one lane per node");
      Check(!LinkLane(m, audio, 7), "audio lanes cannot link");
      uint64_t dup = 0;
      Check(DuplicateLane(m, laneB, &dup) && FindLane(m, dup)->noteNodeUid == 0, "duplicated lane comes out unlinked");
      CheckValid(m, "after links");

      Model bad = m;
      FindLane(bad, laneA)->noteNodeUid = 42;
      Check(!Validate(bad), "Validate rejects a node linked twice");
      Normalize(bad);
      CheckValid(bad, "Normalize repairs a double link");
   }

   // --- normalize repairs notes ---------------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      Clip* c = FindClipOrSlot(m, clip);
      c->notes = { N(kPPQ, kPPQ, 60), N(0, kPPQ, 60), N(0, kPPQ, 61) };
      c->notes[0].id = c->notes[1].id = 3;
      c->notes[2].id = 0;
      c->nextNoteId = 1;
      Check(!Validate(m), "Validate rejects duplicate note ids");
      Normalize(m);
      CheckValid(m, "Normalize repairs notes");

      Model mixed;
      const uint64_t audio = AddLane(mixed, kLaneAudio);
      Clip ac;
      ac.length = kPPQ;
      ac.notes = { N(0, 10, 60) };
      uint64_t aid = 0;
      PlaceOverwrite(mixed, audio, ac, &aid);
      Normalize(mixed);
      Check(FindClipOrSlot(mixed, aid)->notes.empty(), "notes on an audio lane are dropped");
   }

   // --- cap -----------------------------------------------------------------
   {
      Model m;
      const uint64_t clip = OneClip(m);
      std::vector<Note> many;
      for (int i = 0; i < kMaxNotesPerLane; i++)
         many.push_back(N((Tick)i * 4, 2, 60 + (i % 12)));
      Check(AddNotes(m, clip, many), "AddNotes up to the cap");
      Check(!AddNotes(m, clip, { N(0, 1, 100) }), "AddNotes past the cap is refused");
      Check(!DuplicateBlock(m, { clip }), "duplicate past the cap is refused");
   }

   // --- schedule: what the audio thread plays ------------------------------
   {
      Model m;
      uint64_t lane = 0;
      const uint64_t clip = OneClip(m, &lane);
      Note bent = N(kPPQ, kPPQ, 62);
      bent.bend = { { 0, 0.0f }, { kPPQ, 2.0f } };
      Note muted = N(2 * kPPQ, kPPQ, 64);
      muted.mute = true;
      AddNotes(m, clip, { N(0, kPPQ, 60), bent, muted });
      Clip* c = FindClipOrSlot(m, clip);
      c->start = 4 * kPPQ;
      c->length = 8 * kPPQ;
      c->midiLoop = true;
      c->loopStart = 0;
      c->loopLength = 2 * kPPQ;
      c->transpose = 12;
      NoteSchedule s;
      BuildNoteSchedule(*FindLane(m, lane), s);
      Check(s.notes.size() == 8, "loop unrolls 4 passes of 2 notes, muted note skipped");
      Check(s.notes[0].on == 4.0 && s.notes[0].off == 5.0 && s.notes[0].pitch == 72, "clip start and transpose");
      Check(s.notes[7].on == 11.0 && s.notes[7].off == 12.0, "last pass ends at the clip edge");
      Check(s.notes[0].key != s.notes[2].key, "each loop pass gets its own chance key");
      const SchedNote& b = s.notes[1];
      Check(b.exprN[0] == 2 && SampleExpr(&s.exprs[b.expr[0]], b.exprN[0], 0.5) == 1.0f, "bend curve sampled mid-note");

      c->transpose = 0;
      c->midiLoop = false;
      c->notes[0].length = 20 * kPPQ;
      BuildNoteSchedule(*FindLane(m, lane), s);
      Check(s.notes.size() == 2 && s.notes[0].off == 12.0, "a note past the clip edge is cut there");

      c->swing = 1.0f;
      c->notes[1].start = kPPQ / 4;
      BuildNoteSchedule(*FindLane(m, lane), s);
      Check(s.notes[1].on == 4.0 + 1.0 / 3.0, "full swing moves the off 16th to the triplet");

      FindLane(m, lane)->mute = true;
      BuildNoteSchedule(*FindLane(m, lane), s);
      Check(s.notes.empty(), "a muted lane schedules nothing");

      int plays = 0;
      for (uint32_t pass = 0; pass < 1000; pass++)
         plays += ChancePlays(12345u, pass, 0.25f) ? 1 : 0;
      Check(plays > 180 && plays < 320, "chance 0.25 plays about a quarter of passes");
      Check(ChancePlays(7u, 3u, 0.5f) == ChancePlays(7u, 3u, 0.5f), "chance roll repeats");
   }

   // --- .mid import ----------------------------------------------------------
   {
      auto ev = [](double beat, int note, int vel, int track, bool on) {
         MidiFile::Event e;
         e.beat = beat;
         e.note = (uint8_t)note;
         e.velocity = (uint8_t)vel;
         e.track = (uint8_t)track;
         e.on = on;
         return e;
      };
      MidiFile::Song song;
      song.events = { ev(0, 60, 90, 0, true), ev(0, 36, 120, 1, true), ev(0.5, 36, 0, 1, false),
                      ev(1, 60, 0, 0, false), ev(4.5, 67, 70, 0, true) }; // the G is never closed
      song.lengthBeats = 5.0;
      song.noteCount = 3;
      Model m;
      const uint64_t lane = AddLane(m, kLaneMidi);
      const uint64_t clip = ImportSong(m, lane, song, 4 * kPPQ, true);
      Check(clip != 0, "ImportSong places a clip");
      const Clip& c = ClipOf(m, clip);
      Check(c.start == 4 * kPPQ && c.length == 8 * kPPQ && c.midiLoop && c.loopLength == 8 * kPPQ,
            "whole bars, looping its own length");
      Check(c.notes.size() == 3 && c.notes[0].pitch == 36 && c.notes[0].length == kPPQ / 2 && c.notes[0].vel == 120 &&
            c.notes[1].pitch == 60 && c.notes[1].length == kPPQ, "notes paired by pitch and track");
      Check(c.notes[2].pitch == 67 && c.notes[2].End() == 5 * kPPQ, "an open note ends at the file end");
      CheckValid(m, "after import");
      const uint64_t audio = AddLane(m, kLaneAudio);
      Check(ImportSong(m, audio, song, 0) == 0, "import refuses an audio lane");
   }

   printf(gFails == 0 ? "pianoroll-check: all passed\n" : "pianoroll-check: %d failed\n", gFails);
   return gFails == 0 ? 0 : 1;
}
