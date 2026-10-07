#include "MidiFile.h"

#include <algorithm>
#include <cstdio>

namespace MidiFile
{
   namespace
   {
      struct Reader
      {
         const uint8_t* p;
         const uint8_t* end;
         bool ok = true;

         size_t Left() const { return (size_t)(end - p); }
         uint8_t U8()
         {
            if (p >= end) { ok = false; return 0; }
            return *p++;
         }
         uint32_t U16() { const uint32_t a = U8(); return (a << 8) | U8(); }
         uint32_t U32() { const uint32_t a = U16(); return (a << 16) | U16(); }
         uint32_t VarLen()
         {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++)
            {
               const uint8_t b = U8();
               v = (v << 7) | (b & 0x7f);
               if (!(b & 0x80))
                  break;
            }
            return v;
         }
         void Skip(size_t n)
         {
            if (n > Left()) { p = end; ok = false; return; }
            p += n;
         }
      };

      struct RawEvent
      {
         uint64_t tick;
         uint8_t note, velocity, track, channel;
         bool on;
      };

      struct Tempo { uint64_t tick; uint32_t usPerQN; };
   }

   bool Parse(const uint8_t* data, size_t size, Song& out, std::string& error)
   {
      out = Song();
      // RMID files wrap the SMF in a RIFF chunk, and some exporters leave junk before the header:
      // start at the first "MThd".
      size_t start = size;
      for (size_t i = 0; i + 14 <= size; i++)
         if (data[i] == 'M' && data[i + 1] == 'T' && data[i + 2] == 'h' && data[i + 3] == 'd')
         {
            start = i;
            break;
         }
      if (start == size)
      {
         error = "not a MIDI file";
         return false;
      }
      data += start;
      size -= start;
      Reader r { data, data + size };
      r.Skip(4);
      const uint32_t headerLen = r.U32();
      const uint32_t format = r.U16();
      const uint32_t numTracks = r.U16();
      const uint32_t division = r.U16();
      if (headerLen < 6) { error = "bad MIDI header"; return false; }
      r.Skip(headerLen - 6);
      if (format > 2) { error = "unknown MIDI format"; return false; }
      // Format 2 is independent sequences; they are played as parallel tracks like format 1.
      const bool smpte = (division & 0x8000) != 0;
      double ticksPerBeat = (double)division;
      if (smpte)
      {
         const int fps = 256 - (int)(division >> 8); // high byte is the negated frame rate
         const int sub = (int)(division & 0xFF);
         if (fps <= 0 || sub <= 0) { error = "bad SMPTE division"; return false; }
         ticksPerBeat = (double)fps * (double)sub / 2.0; // 2 beats per second
      }
      else if (division == 0) { error = "bad MIDI division"; return false; }
      std::vector<Tempo> tempos;

      std::vector<RawEvent> raw;
      int noteTracks = 0;
      for (uint32_t t = 0; t < numTracks && r.Left() >= 8; t++)
      {
         const uint8_t* tag = r.p;
         r.Skip(4);
         const uint32_t len = r.U32();
         if (!r.ok) break;
         const size_t take = std::min<size_t>(len, r.Left());
         Reader tr { r.p, r.p + take };
         r.Skip(take);
         if (!(tag[0] == 'M' && tag[1] == 'T' && tag[2] == 'r' && tag[3] == 'k'))
            continue;

         const size_t before = raw.size();
         uint64_t tick = 0;
         uint8_t status = 0;
         const uint8_t trackIdx = (uint8_t)std::min(noteTracks, 255);
         while (tr.Left() > 0 && tr.ok)
         {
            tick += tr.VarLen();
            uint8_t b = tr.U8();
            if (!tr.ok)
               break;
            if (b == 0xFF)
            {
               const uint8_t type = tr.U8();
               const uint32_t mlen = tr.VarLen();
               if (type == 0x51 && mlen == 3 && tr.Left() >= 3)
               {
                  const uint32_t us = ((uint32_t)tr.p[0] << 16) | ((uint32_t)tr.p[1] << 8) | tr.p[2];
                  if (us > 0)
                     tempos.push_back({ tick, us });
               }
               tr.Skip(mlen);
               if (type == 0x2F)
                  break;
               continue;
            }
            if (b == 0xF0 || b == 0xF7)
            {
               tr.Skip(tr.VarLen());
               continue;
            }
            uint8_t d1;
            if (b & 0x80)
            {
               status = b;
               d1 = tr.U8();
            }
            else
               d1 = b; // running status
            const uint8_t kind = status & 0xF0;
            if (kind == 0xC0 || kind == 0xD0)
               continue;
            const uint8_t d2 = tr.U8();
            if (!tr.ok)
               break;
            if (kind == 0x90 || kind == 0x80)
            {
               const bool on = kind == 0x90 && d2 > 0;
               raw.push_back({ tick, (uint8_t)(d1 & 0x7f), on ? (uint8_t)(d2 & 0x7f) : (uint8_t)0, trackIdx, (uint8_t)(status & 0x0F), on });
            }
         }
         if (raw.size() > before)
            noteTracks++;
      }
      if (raw.empty())
      {
         error = "no notes in file";
         return false;
      }

      // Offs before ons at the same tick so a re-struck note releases first.
      std::stable_sort(raw.begin(), raw.end(), [](const RawEvent& a, const RawEvent& b) {
         if (a.tick != b.tick) return a.tick < b.tick;
         return !a.on && b.on;
      });
      // Tempo map -> relative beat position (see MidiFile.h). SMPTE has no tempo map.
      std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
      if (smpte)
         tempos.clear();
      const double base = tempos.empty() || tempos.front().tick > 0 ? 500000.0 : (double)tempos.front().usPerQN;
      for (const Tempo& t : tempos)
         if ((double)t.usPerQN != base)
            out.hasTempoChanges = true;
      auto tempoBeatAt = [&](uint64_t tick) {
         double beats = 0.0;
         uint64_t prev = 0;
         double cur = base; // tempo in force before the first change
         for (const Tempo& t : tempos)
         {
            if (t.tick >= tick)
               break;
            beats += (double)(t.tick - prev) / ticksPerBeat * (cur / base);
            prev = t.tick;
            cur = (double)t.usPerQN;
         }
         return beats + (double)(tick - prev) / ticksPerBeat * (cur / base);
      };

      out.events.reserve(raw.size() + 16);
      // Notes still sounding at the end of the file get an off there, so a missing note-off never sticks.
      std::vector<Event> open;
      for (const RawEvent& e : raw)
      {
         Event ev;
         ev.beat = (double)e.tick / ticksPerBeat;
         ev.tempoBeat = tempoBeatAt(e.tick);
         ev.note = e.note;
         ev.velocity = e.velocity;
         ev.track = e.track;
         ev.channel = e.channel;
         ev.on = e.on;
         out.events.push_back(ev);
         out.lengthBeats = std::max(out.lengthBeats, ev.beat);
         out.lengthTempoBeats = std::max(out.lengthTempoBeats, ev.tempoBeat);
         if (e.on)
         {
            out.noteCount++;
            out.lowNote = std::min(out.lowNote, e.note);
            out.highNote = std::max(out.highNote, e.note);
            open.push_back(ev);
         }
         else
            for (size_t i = 0; i < open.size(); i++)
               if (open[i].note == e.note && open[i].track == e.track && open[i].channel == e.channel)
               {
                  open.erase(open.begin() + (long)i);
                  break;
               }
      }
      for (Event off : open)
      {
         off.on = false;
         off.velocity = 0;
         off.beat = out.lengthBeats;
         off.tempoBeat = out.lengthTempoBeats;
         out.events.push_back(off);
      }
      out.trackCount = noteTracks;
      return true;
   }

   bool Load(const std::string& path, Song& out, std::string& error)
   {
      FILE* f = fopen(path.c_str(), "rb");
      if (!f)
      {
         error = "cannot open file";
         return false;
      }
      std::vector<uint8_t> bytes;
      uint8_t buf[65536];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
      {
         bytes.insert(bytes.end(), buf, buf + n);
         if (bytes.size() > (64u << 20))
            break;
      }
      fclose(f);
      return Parse(bytes.data(), bytes.size(), out, error);
   }
}
