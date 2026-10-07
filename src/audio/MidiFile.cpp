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
         uint8_t note, velocity, track;
         bool on;
      };
   }

   bool Parse(const uint8_t* data, size_t size, Song& out, std::string& error)
   {
      out = Song();
      Reader r { data, data + size };
      if (size < 14 || data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
      {
         error = "not a MIDI file";
         return false;
      }
      r.Skip(4);
      const uint32_t headerLen = r.U32();
      const uint32_t format = r.U16();
      const uint32_t numTracks = r.U16();
      const uint32_t division = r.U16();
      if (headerLen < 6) { error = "bad MIDI header"; return false; }
      r.Skip(headerLen - 6);
      if (format > 1) { error = "MIDI format 2 is not supported"; return false; }
      if (division == 0 || (division & 0x8000)) { error = "SMPTE-timed MIDI files are not supported"; return false; }

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
               raw.push_back({ tick, (uint8_t)(d1 & 0x7f), on ? (uint8_t)(d2 & 0x7f) : (uint8_t)0, trackIdx, on });
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
      out.events.reserve(raw.size());
      for (const RawEvent& e : raw)
      {
         Event ev;
         ev.beat = (double)e.tick / (double)division;
         ev.note = e.note;
         ev.velocity = e.velocity;
         ev.track = e.track;
         ev.on = e.on;
         out.events.push_back(ev);
         out.lengthBeats = std::max(out.lengthBeats, ev.beat);
         if (e.on)
         {
            out.noteCount++;
            out.lowNote = std::min(out.lowNote, e.note);
            out.highNote = std::max(out.highNote, e.note);
         }
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
