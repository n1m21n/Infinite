#include "MidiFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace MidiFile
{
   namespace
   {
      constexpr size_t kMaxFileBytes = 64u * 1024u * 1024u;
      constexpr uint64_t kMaxTick = (uint64_t)1 << 40; // keeps tick sums far from overflow

      uint32_t Be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
      uint16_t Be16(const uint8_t* p) { return (uint16_t)(((uint32_t)p[0] << 8) | p[1]); }
      uint32_t Le32(const uint8_t* p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }

      // Bounds-checked cursor over one track chunk.
      struct Reader
      {
         const uint8_t* p;
         size_t size;
         size_t pos = 0;

         bool Byte(uint8_t& b)
         {
            if (pos >= size)
               return false;
            b = p[pos++];
            return true;
         }
         bool Peek(uint8_t& b) const
         {
            if (pos >= size)
               return false;
            b = p[pos];
            return true;
         }
         // MIDI variable-length quantity, at most 4 bytes (28 bits).
         bool Vlq(uint32_t& v)
         {
            v = 0;
            for (int i = 0; i < 4; i++)
            {
               uint8_t b;
               if (!Byte(b))
                  return false;
               v = (v << 7) | (b & 0x7Fu);
               if ((b & 0x80u) == 0)
                  return true;
            }
            return false; // a fifth continuation byte: garbage
         }
         bool Skip(uint32_t n)
         {
            if ((size_t)n > size - pos)
               return false;
            pos += n;
            return true;
         }
      };

      void AddWarning(Data& out, const std::string& w)
      {
         if (!out.warning.empty())
            out.warning += "; ";
         out.warning += w;
      }

      // Parses one MTrk body. Stops quietly at the first undecodable byte.
      void ParseTrack(const uint8_t* body, size_t len, uint16_t trackIndex, Data& out)
      {
         Track track;
         Reader r { body, len };
         uint64_t tick = 0;
         uint8_t running = 0; // 0 = no running status
         bool ended = false;
         const size_t firstEvent = out.events.size();

         auto push = [&](uint8_t kind, uint8_t ch, uint8_t d1, uint8_t d2, uint32_t value) {
            Event e;
            e.tick = tick;
            e.track = trackIndex;
            e.kind = kind;
            e.channel = ch;
            e.data1 = d1;
            e.data2 = d2;
            e.value = value;
            out.events.push_back(e);
         };

         while (!ended && r.pos < r.size)
         {
            uint32_t delta;
            if (!r.Vlq(delta))
            {
               AddWarning(out, "track " + std::to_string(trackIndex + 1) + " truncated");
               break;
            }
            tick = std::min(kMaxTick, tick + delta);

            uint8_t b;
            if (!r.Peek(b))
            {
               AddWarning(out, "track " + std::to_string(trackIndex + 1) + " truncated");
               break;
            }
            uint8_t status;
            if (b & 0x80u)
            {
               r.pos++;
               status = b;
            }
            else if (running != 0)
            {
               status = running; // running status: b is the first data byte
            }
            else
            {
               AddWarning(out, "track " + std::to_string(trackIndex + 1) + ": data byte without status");
               break;
            }

            if (status == 0xFF)
            {
               running = 0; // meta events cancel running status
               uint8_t type;
               uint32_t mlen;
               if (!r.Byte(type) || !r.Vlq(mlen) || (size_t)mlen > r.size - r.pos)
               {
                  AddWarning(out, "track " + std::to_string(trackIndex + 1) + ": truncated meta event");
                  break;
               }
               const uint8_t* m = r.p + r.pos;
               if (type == 0x51 && mlen >= 3)
               {
                  const uint32_t us = ((uint32_t)m[0] << 16) | ((uint32_t)m[1] << 8) | m[2];
                  if (us > 0)
                     push(kTempo, 0, 0, 0, us);
               }
               else if (type == 0x58 && mlen >= 2)
               {
                  const int den = m[1] <= 6 ? (1 << m[1]) : 4;
                  push(kTimeSignature, 0, std::max<uint8_t>(1, m[0]), (uint8_t)den, 0);
               }
               else if (type == 0x03 && track.name.empty())
               {
                  // Keep printable bytes only; names are shown in the UI.
                  for (uint32_t i = 0; i < mlen && track.name.size() < 64; i++)
                     if (m[i] >= 0x20 && m[i] != 0x7F)
                        track.name.push_back((char)m[i]);
                  push(kTrackName, 0, 0, 0, 0);
               }
               else if (type == 0x2F)
               {
                  push(kEndOfTrack, 0, 0, 0, 0);
                  ended = true;
               }
               r.pos += mlen;
               continue;
            }
            if (status == 0xF0 || status == 0xF7)
            {
               running = 0; // sysex: length-prefixed, skipped
               uint32_t slen;
               if (!r.Vlq(slen) || !r.Skip(slen))
               {
                  AddWarning(out, "track " + std::to_string(trackIndex + 1) + ": truncated sysex");
                  break;
               }
               continue;
            }
            if (status >= 0xF0)
            {
               // System common / real-time bytes have no business in an SMF.
               AddWarning(out, "track " + std::to_string(trackIndex + 1) + ": unexpected system byte");
               break;
            }

            running = status;
            const uint8_t hi = status & 0xF0u;
            const uint8_t ch = status & 0x0Fu;
            const int need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            uint8_t d1 = 0, d2 = 0;
            if (!r.Byte(d1) || (need == 2 && !r.Byte(d2)))
            {
               AddWarning(out, "track " + std::to_string(trackIndex + 1) + ": truncated event");
               break;
            }
            if ((d1 & 0x80u) || (d2 & 0x80u))
            {
               AddWarning(out, "track " + std::to_string(trackIndex + 1) + ": bad data byte");
               break;
            }
            switch (hi)
            {
               case 0x80: push(kNoteOff, ch, d1, d2, 0); break;
               case 0x90: push(d2 == 0 ? kNoteOff : kNoteOn, ch, d1, d2, 0); break;
               case 0xA0: push(kPolyPressure, ch, d1, d2, 0); break;
               case 0xB0: push(kControlChange, ch, d1, d2, 0); break;
               case 0xC0: push(kProgramChange, ch, d1, 0, 0); break;
               case 0xD0: push(kChannelPressure, ch, d1, 0, 0); break;
               default: push(kPitchBend, ch, d1, d2, (uint32_t)d1 | ((uint32_t)d2 << 7)); break;
            }
         }

         track.endTick = tick;
         track.eventCount = (int)(out.events.size() - firstEvent);

         // Pair note-ons with note-offs (FIFO per channel+key, so overlapping
         // same-pitch notes close in the order they opened).
         bool anyNoteOn = false;
         for (size_t i = firstEvent; i < out.events.size() && !anyNoteOn; i++)
            anyNoteOn = out.events[i].kind == kNoteOn;
         if (!anyNoteOn)
         {
            out.tracks.push_back(std::move(track));
            return;
         }
         std::vector<size_t> openNotes[16][128];
         for (size_t i = firstEvent; i < out.events.size(); i++)
         {
            const Event& e = out.events[i];
            if (e.kind == kNoteOn)
               openNotes[e.channel][e.data1 & 0x7F].push_back(i);
            else if (e.kind == kNoteOff)
            {
               std::vector<size_t>& q = openNotes[e.channel][e.data1 & 0x7F];
               if (q.empty())
                  continue; // stray note-off
               const Event& on = out.events[q.front()];
               q.erase(q.begin());
               Note n;
               n.startTick = on.tick;
               n.endTick = e.tick;
               n.track = trackIndex;
               n.channel = on.channel;
               n.key = on.data1;
               n.velocity = on.data2;
               out.notes.push_back(n);
               track.noteCount++;
               track.channelMask |= (uint16_t)(1u << on.channel);
            }
         }
         for (int c = 0; c < 16; c++)
            for (int k = 0; k < 128; k++)
               for (size_t idx : openNotes[c][k])
               {
                  const Event& on = out.events[idx];
                  Note n;
                  n.startTick = on.tick;
                  n.endTick = std::max(on.tick, tick);
                  n.track = trackIndex;
                  n.channel = on.channel;
                  n.key = on.data1;
                  n.velocity = on.data2;
                  out.notes.push_back(n);
                  track.noteCount++;
                  track.channelMask |= (uint16_t)(1u << on.channel);
               }

         out.tracks.push_back(std::move(track));
      }
   }

   bool Parse(const uint8_t* bytes, size_t size, Data& out, std::string& error)
   {
      out = Data();
      error.clear();

      // RIFF RMID wrapper: the SMF sits in the "data" chunk. Unwrapped in a
      // loop with a depth cap (Turbo 0.48), so a crafted file of nested RIFFs
      // cannot recurse without bound.
      constexpr int kMaxRiffDepth = 4;
      for (int depth = 0;; depth++)
      {
         if (bytes == nullptr || size < 14)
         {
            error = "not a MIDI file (too short)";
            return false;
         }
         if (!(std::equal(bytes, bytes + 4, "RIFF") && std::equal(bytes + 8, bytes + 12, "RMID")))
            break;
         if (depth >= kMaxRiffDepth)
         {
            error = "RIFF MIDI file nested too deeply";
            return false;
         }
         bool found = false;
         size_t pos = 12;
         while (pos + 8 <= size)
         {
            const uint32_t clen = Le32(bytes + pos + 4);
            if (std::equal(bytes + pos, bytes + pos + 4, "data"))
            {
               const size_t avail = size - (pos + 8);
               size = std::min<size_t>(clen, avail);
               bytes = bytes + pos + 8;
               found = true;
               break;
            }
            if ((size_t)clen > size - (pos + 8))
               break;
            pos += 8 + clen + (clen & 1u);
         }
         if (!found)
         {
            error = "RIFF MIDI file without a data chunk";
            return false;
         }
      }

      if (!std::equal(bytes, bytes + 4, "MThd"))
      {
         error = "not a MIDI file (no MThd header)";
         return false;
      }
      const uint32_t hlen = Be32(bytes + 4);
      if (hlen < 6 || (size_t)hlen > size - 8)
      {
         error = "bad MIDI header";
         return false;
      }
      out.format = Be16(bytes + 8);
      const int declaredTracks = Be16(bytes + 10);
      const uint16_t division = Be16(bytes + 12);
      if (division & 0x8000u)
      {
         error = "SMPTE time division is not supported (only ticks per quarter note)";
         return false;
      }
      if (division == 0)
      {
         error = "bad MIDI header (0 ticks per quarter note)";
         return false;
      }
      if (out.format > 2)
      {
         error = "unknown MIDI file format " + std::to_string(out.format);
         return false;
      }
      if (out.format == 2)
         AddWarning(out, "format 2 (independent sequences): tracks are played together");
      out.ppq = division;

      size_t pos = 8 + (size_t)hlen;
      int found = 0;
      while (pos + 8 <= size && found < 65535)
      {
         const uint8_t* chunk = bytes + pos;
         size_t clen = Be32(chunk + 4);
         const size_t avail = size - (pos + 8);
         bool truncated = false;
         if (clen > avail)
         {
            clen = avail;
            truncated = true;
         }
         if (std::equal(chunk, chunk + 4, "MTrk"))
         {
            if (truncated)
               AddWarning(out, "file truncated in track " + std::to_string(found + 1));
            ParseTrack(chunk + 8, clen, (uint16_t)found, out);
            found++;
         }
         // Unknown chunks are skipped, as the SMF spec asks.
         pos += 8 + clen;
      }
      if (found == 0)
      {
         error = "MIDI file has no tracks";
         return false;
      }
      if (found != declaredTracks)
         AddWarning(out, "header says " + std::to_string(declaredTracks) + " tracks, found " + std::to_string(found));

      // Tempo map, time signature and length across all tracks.
      for (const Event& e : out.events)
      {
         if (e.kind == kTempo)
            out.tempos.push_back({ e.tick, 60000000.0 / (double)e.value });
      }
      std::stable_sort(out.tempos.begin(), out.tempos.end(),
                       [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });
      uint64_t firstTsTick = ~(uint64_t)0;
      for (const Event& e : out.events)
         if (e.kind == kTimeSignature && e.tick < firstTsTick)
         {
            firstTsTick = e.tick;
            out.timeSigNum = e.data1;
            out.timeSigDen = e.data2;
         }
      for (const Track& t : out.tracks)
         out.lengthTicks = std::max(out.lengthTicks, t.endTick);
      for (const Note& n : out.notes)
         out.lengthTicks = std::max(out.lengthTicks, n.endTick);

      std::stable_sort(out.notes.begin(), out.notes.end(), [](const Note& a, const Note& b) {
         return a.startTick != b.startTick ? a.startTick < b.startTick : a.key < b.key;
      });
      return true;
   }

   bool Load(const std::string& utf8Path, Data& out, std::string& error)
   {
      out = Data();
      std::error_code ec;
      const std::filesystem::path p = std::filesystem::u8path(utf8Path);
      const uintmax_t sz = std::filesystem::file_size(p, ec);
      if (ec)
      {
         error = "cannot open file";
         return false;
      }
      if (sz > kMaxFileBytes)
      {
         error = "file too large for a MIDI file";
         return false;
      }
      std::ifstream f(p, std::ios::binary);
      if (!f)
      {
         error = "cannot open file";
         return false;
      }
      std::vector<uint8_t> bytes((size_t)sz);
      if (sz > 0 && !f.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)sz))
      {
         error = "cannot read file";
         return false;
      }
      return Parse(bytes.data(), bytes.size(), out, error);
   }

   const char* KindName(uint8_t kind)
   {
      static const char* const kNames[] = { "NoteOff", "NoteOn", "PolyPressure", "CC", "Program", "ChanPressure",
                                            "PitchBend", "Tempo", "TimeSig", "EndOfTrack", "TrackName" };
      return kind < sizeof(kNames) / sizeof(kNames[0]) ? kNames[kind] : "?";
   }
}
