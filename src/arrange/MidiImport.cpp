#include "MidiImport.h"

#include <algorithm>
#include <cmath>

namespace Arrange
{
uint64_t ImportSong(Model& m, uint64_t laneId, const MidiFile::Song& song, Tick at, bool loop)
{
   const Lane* lane = FindLane(m, laneId);
   if (lane == nullptr || lane->type != kLaneMidi || song.noteCount <= 0)
      return 0;
   auto tickOf = [](double beat) { return (Tick)std::llround(std::max(0.0, beat) * (double)kPPQ); };

   // Pair each on with the first open on of the same note and track. A
   // second on before the off is a fresh note; the lane's same-pitch rule
   // cuts the first one where it starts.
   struct Open { double beat; uint8_t vel; uint8_t track; };
   std::vector<Open> open[128];
   std::vector<Note> notes;
   notes.reserve((size_t)song.noteCount);
   auto close = [&](uint8_t pitch, const Open& o, double offBeat) {
      Note n;
      n.start = tickOf(o.beat);
      n.length = std::max<Tick>(1, tickOf(offBeat) - n.start);
      n.pitch = pitch;
      n.vel = (uint8_t)std::clamp((int)o.vel, 1, 127);
      notes.push_back(n);
   };
   for (const MidiFile::Event& e : song.events)
   {
      std::vector<Open>& q = open[e.note & 127];
      if (e.on)
         q.push_back({ e.beat, e.velocity, e.track });
      else
         for (size_t i = 0; i < q.size(); i++)
            if (q[i].track == e.track)
            {
               close(e.note & 127, q[i], e.beat);
               q.erase(q.begin() + (long)i);
               break;
            }
   }
   for (int p = 0; p < 128; p++)
      for (const Open& o : open[p])
         close((uint8_t)p, o, song.lengthBeats);
   std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
   const size_t room = (size_t)std::max(0, kMaxNotesPerLane - LaneNoteCount(*lane));
   if (notes.size() > room)
      notes.resize(room);
   if (notes.empty())
      return 0;

   Tick end = 0;
   for (const Note& n : notes)
      end = std::max(end, n.End());
   const Tick bar = 4 * kPPQ;
   Clip c;
   c.start = std::max<Tick>(0, at);
   c.length = std::max(bar, (end + bar - 1) / bar * bar);
   if (loop)
   {
      c.midiLoop = true;
      c.loopStart = 0;
      c.loopLength = c.length;
   }
   uint64_t id = 0;
   if (!PlaceOverwrite(m, laneId, c, &id) || id == 0)
      return 0;
   AddNotes(m, id, notes);
   return id;
}
}
