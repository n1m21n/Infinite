#include "NotePatternLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

#include "core/MidiFile.h"
#include "platform/SettingsPaths.h"

namespace NotePatterns
{
namespace
{
   struct Def
   {
      const char* category;
      const char* name;
      float stepBeats;
      const char* tokens;
   };

   constexpr float k16 = 0.25f;
   constexpr float k8 = 0.5f;
   constexpr float k4 = 1.0f;
   constexpr float k8t = 1.0f / 3.0f;
   constexpr float k16t = 1.0f / 6.0f;

   // Written for Infinite-Turbo (MIT). Generic figures: arpeggios, keyboard and
   // guitar accompaniments, strums, bass lines and sequencer riffs as players
   // use them everywhere; nothing here is copied from a recording or a product.
   const Def kDefs[] = {
      // ---- Arp ------------------------------------------------------------
      { "Arp", "Up", k16, "1 2 3 4" },
      { "Arp", "Up 2 octaves", k16, "1 2 3 4 5 6 7 8" },
      { "Arp", "Down", k16, "4 3 2 1" },
      { "Arp", "Up down", k16, "1 2 3 4 3 2" },
      { "Arp", "Down up", k16, "4 3 2 1 2 3" },
      { "Arp", "Converge", k16, "1 4 2 3" },
      { "Arp", "Pinky", k16, "1 4 2 4 3 4" },
      { "Arp", "Thumb", k16, "1 2 1 3 1 4 1 3" },
      { "Arp", "Octave jumps", k16, "1 1' 2 2' 3 3' 2 2'" },
      { "Arp", "Broken 8ths", k8, "1 3 2 4" },
      { "Arp", "Triplets", k8t, "1 2 3 4 3 2" },
      { "Arp", "Sixteenth triplets", k16t, "1 2 3 2 3 4 3 4 5 4 3 2" },
      { "Arp", "Random", k16, "r r r r r r r r" },
      { "Arp", "Accent 3 over 4", k16, "1! 2 3 1'! 2' 3' 1''! 3' 2'! 3 2 1! 3 2 1'! 3" },
      { "Arp", "Ratchet up", k16, "1 2*2 3 4*2 3 2*3 1 2" },

      // ---- Piano ------------------------------------------------------------
      { "Piano", "Alberti", k8, "1 3 2 3" },
      { "Piano", "Alberti 16ths", k16, "1 3 2 3 1 3 2 3" },
      { "Piano", "Waltz", k4, "b c? c?" },
      { "Piano", "Stride", k8, "b! c 3, c b! c 1, c" },
      { "Piano", "Ballad", k8, "b 2 3 1' 3 2 1 2" },
      { "Piano", "Broken ballad", k8, "b 3 1' 2' 3' 2' 1' 3" },
      { "Piano", "Gospel pushes", k16, "c! . . c . . c! . c . . c . . c? ." },
      { "Piano", "Montuno", k8, "1+1' . 3 1+1' . 2 3 . 1+1' . 3 . 2 1+1' . 3" },
      { "Piano", "Block chords 8ths", k8, "c! c c c c! c c c" },
      { "Piano", "Rolled chord", k16t, "1 2 3 4 - - - - - - - -" },

      // ---- Guitar -----------------------------------------------------------
      { "Guitar", "Travis picking", k8, "b 2' 3, 3' b 2' 3, 3'" },
      { "Guitar", "Folk arpeggio", k8, "b 3 1' 2' 3' 2' 1' 3" },
      { "Guitar", "Reggae skank", k16, ". . . . c~! . . . . . . . c~! . . ." },
      { "Guitar", "Funk 16ths", k16, "c~! . c~ c~ . c~ . c~! c~ . c~ . c~! c~ . c~" },
      { "Guitar", "Bossa comp", k16,
        "b! . c . . c b . c . . c b . c . b! . c . . c b . c . . c . c . ." },
      { "Guitar", "Samba picking", k16, "b c' c b, c c' b c" },
      { "Guitar", "Highlife", k16, "b . 3 2' . 1' 3 . b . 3 2' . 1' 3 ." },
      { "Guitar", "Afro picking", k16, "1' . 2' 1' . 3' . 1' 2' . 1' . 3' 1' . 2'" },
      { "Guitar", "Arpeggio picking", k16, "b 2 3 2' 3' 2' 3 2" },

      // ---- Strum ------------------------------------------------------------
      { "Strum", "Straight 8ths", k8, "c! c c c c! c c c" },
      { "Strum", "Rock", k8, "c! . c . c! c . c" },
      { "Strum", "Ballad", k8, "c! . . c . c c ." },
      { "Strum", "Shuffle", k8t, "c! . c c! . c" },
      { "Strum", "Waltz", k4, "b! c c" },
      { "Strum", "Island", k8, "c . c c . c c c" },
      { "Strum", "Sixteenth push", k16, "c! . . c . . c . . . c! . . c . ." },
      { "Strum", "Swing chop", k8t, ". . c~! . . c~" },

      // ---- Bass -------------------------------------------------------------
      { "Bass", "Root fifth", k4, "b 3, b 3," },
      { "Bass", "Octave disco", k8, "b 1 b 1" },
      { "Bass", "Driving 8ths", k8, "b! b b b b! b b b" },
      { "Bass", "Acid", k16, "b! b' - b 3, b'! b . b b'! 3 - b 2' b b'" },
      { "Bass", "Walking", k4, "1, 2, 3, 2," },
      { "Bass", "One drop", k16, ". . . . . . . . b! - . . 3, - . ." },
      { "Bass", "Funk slap", k16, "b! . b' . . b b' . b! . . b b' . 3, ." },
      { "Bass", "Baiao", k16, "b! . . b . . b . b! . . b . . b ." },
      { "Bass", "Dub", k8, "b! - - . 3, . b . " },
      { "Bass", "Synth pulse", k16, "b b b b b b b b b b b b b b b b" },

      // ---- Sequencer --------------------------------------------------------
      { "Sequencer", "Trance gate", k16, "c! c c c! c c c! c c! c c c! c c c! c" },
      { "Sequencer", "Offbeat stabs", k16, ". . c~! . . . c~ . . . c~! . . . c~ ." },
      { "Sequencer", "Pulse chords", k8, "c c c c c c c c" },
      { "Sequencer", "Berlin school", k16, "1, 1 3 1 2 1 3 1' 1, 1 3 1 2 1 4 3" },
      { "Sequencer", "303 climb", k16, "1 1 2 1 3 1 4 1 3 1 2 1 1' 1 2 1" },
      { "Sequencer", "Arp and stab", k16, "1 2 3 c~! 1 2 3 . 1 2 3 c~! 2 3 4 ." },
      { "Sequencer", "Euclid 5 of 8", k16, "1! . 2 3 . 1 2 . 1! . 2 3 . 1 2 ." },
      { "Sequencer", "Polymeter 5", k16, "1 3 2 4 3" },

      // ---- Ambient ----------------------------------------------------------
      { "Ambient", "Slow arp", k4, "1 3 2 4" },
      { "Ambient", "Bell drops", k8, "4 . . 3 . . 2 . . . 1 . . . . ." },
      { "Ambient", "Pad pulses", k8, "c? - - - c? - - -" },
      { "Ambient", "Whole chord", k4, "c - - -" },
      { "Ambient", "Rising mist", k8t, "1? 2? 3? 4? 5? 6? 5? 4? 3? 2? 1? ." },
      { "Ambient", "Glass", k16, "4' . . . 3' . . 2' . . . . 1' . . ." },
   };

}

bool ParseTokens(const std::string& text, float stepBeats, Pattern& out, std::string& error)
{
   out.notes.clear();
   out.stepBeats = stepBeats > 0.0f ? stepBeats : 0.25f;
   error.clear();
   std::vector<std::string> tokens;
   {
      std::string cur;
      for (char ch : text)
      {
         if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
         {
            if (!cur.empty())
               tokens.push_back(cur);
            cur.clear();
         }
         else
            cur += ch;
      }
      if (!cur.empty())
         tokens.push_back(cur);
   }
   if (tokens.empty())
   {
      error = "empty pattern";
      return false;
   }
   std::vector<int> lastStepNotes; // indices into out.notes started on the last non-tie step
   for (int s = 0; s < (int)tokens.size(); s++)
   {
      std::string tok = tokens[s];
      if (tok == ".")
      {
         lastStepNotes.clear();
         continue;
      }
      if (tok == "-")
      {
         for (int i : lastStepNotes)
            out.notes[i].len += 1.0f;
         continue;
      }
      int ratchet = 1;
      const size_t star = tok.find('*');
      if (star != std::string::npos)
      {
         ratchet = std::clamp(atoi(tok.c_str() + star + 1), 1, 8);
         tok = tok.substr(0, star);
      }
      std::vector<PNote> stepNotes;
      size_t pos = 0;
      while (pos < tok.size())
      {
         size_t plus = tok.find('+', pos);
         if (plus == std::string::npos)
            plus = tok.size();
         const std::string el = tok.substr(pos, plus - pos);
         pos = plus + 1;
         if (el.empty())
            continue;
         PNote n;
         size_t i = 0;
         if (el[0] == 'c')
         {
            n.kind = kChord;
            i = 1;
         }
         else if (el[0] == 'b')
         {
            n.kind = kBass;
            i = 1;
         }
         else if (el[0] == 'r')
         {
            n.kind = kRandom;
            i = 1;
         }
         else if (el[0] >= '0' && el[0] <= '9')
         {
            int v = 0;
            while (i < el.size() && el[i] >= '0' && el[i] <= '9')
               v = v * 10 + (el[i++] - '0');
            n.kind = kTone;
            n.tone = std::max(0, v - 1);
         }
         else
         {
            error = "unknown step \"" + tokens[s] + "\"";
            return false;
         }
         for (; i < el.size(); i++)
         {
            switch (el[i])
            {
            case '\'': n.oct++; break;
            case ',': n.oct--; break;
            case '!': n.vel = 1.0f; break;
            case '?': n.vel = 0.55f; break;
            case '~': n.len *= 0.5f; break;
            default:
               error = "unknown modifier in \"" + tokens[s] + "\"";
               return false;
            }
         }
         stepNotes.push_back(n);
      }
      lastStepNotes.clear();
      for (int r = 0; r < ratchet; r++)
         for (PNote n : stepNotes)
         {
            n.step = (float)s + (float)r / (float)ratchet;
            if (ratchet > 1)
               n.len = std::min(n.len, 1.0f) / (float)ratchet;
            out.notes.push_back(n);
            if (ratchet == 1)
               lastStepNotes.push_back((int)out.notes.size() - 1);
         }
   }
   out.steps = (int)tokens.size();
   return true;
}

const std::vector<Pattern>& BuiltIn()
{
   static std::vector<Pattern> bank;
   if (bank.empty())
   {
      for (const Def& d : kDefs)
      {
         Pattern p;
         std::string err;
         if (!ParseTokens(d.tokens, d.stepBeats, p, err))
         {
            fprintf(stderr, "NotePatterns: %s / %s: %s\n", d.category, d.name, err.c_str());
            continue;
         }
         p.category = d.category;
         p.name = d.name;
         p.source = "built-in";
         bank.push_back(std::move(p));
      }
   }
   return bank;
}

std::string UserFolder()
{
   const std::string root = InfiniteSettingsDirectory();
   if (root.empty())
      return std::string();
   const std::filesystem::path dir = std::filesystem::u8path(root) / "NotePatterns";
   std::error_code ec;
   std::filesystem::create_directories(dir, ec);
   return ec ? std::string() : dir.u8string();
}

std::vector<int> Library::InCategory(const std::string& category) const
{
   std::vector<int> out;
   for (int i = 0; i < (int)patterns.size(); i++)
      if (patterns[i].category == category)
         out.push_back(i);
   return out;
}

namespace
{
   Library& Lib()
   {
      static Library lib;
      return lib;
   }
   bool g_scanned = false;
}

void Rescan()
{
   Library lib;
   for (const Pattern& p : BuiltIn())
   {
      if (std::find(lib.categories.begin(), lib.categories.end(), p.category) == lib.categories.end())
         lib.categories.push_back(p.category);
      lib.patterns.push_back(p);
   }
   const std::string folder = UserFolder();
   if (!folder.empty())
   {
      std::map<std::string, std::vector<Pattern>> user;
      std::error_code ec;
      const std::filesystem::path root = std::filesystem::u8path(folder);
      int count = 0;
      for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
           !ec && it != std::filesystem::recursive_directory_iterator() && count < 2000; it.increment(ec))
      {
         if (!it->is_regular_file(ec))
            continue;
         std::string ext = it->path().extension().u8string();
         std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         if (ext != ".mid" && ext != ".midi")
            continue;
         Pattern p;
         std::string err;
         if (!LoadMidiPattern(it->path().u8string(), p, err))
            continue;
         const std::filesystem::path rel = std::filesystem::relative(it->path(), root, ec);
         p.category = (!ec && rel.has_parent_path() && !rel.parent_path().empty())
                         ? rel.begin()->u8string()
                         : std::string("My patterns");
         p.name = it->path().stem().u8string();
         user[p.category].push_back(std::move(p));
         count++;
      }
      for (auto& kv : user)
      {
         std::sort(kv.second.begin(), kv.second.end(),
                   [](const Pattern& a, const Pattern& b) { return a.name < b.name; });
         if (std::find(lib.categories.begin(), lib.categories.end(), kv.first) == lib.categories.end())
            lib.categories.push_back(kv.first);
         for (Pattern& p : kv.second)
            lib.patterns.push_back(std::move(p));
      }
   }
   Lib() = std::move(lib);
   g_scanned = true;
}

const Library& Current()
{
   if (!g_scanned)
      Rescan();
   return Lib();
}

bool LoadMidiPattern(const std::string& utf8Path, Pattern& out, std::string& error)
{
   MidiFile::Data data;
   if (!MidiFile::Load(utf8Path, data, error))
      return false;
   if (data.notes.empty())
   {
      error = "no notes";
      return false;
   }
   int lowest = 127;
   for (const MidiFile::Note& n : data.notes)
      lowest = std::min(lowest, (int)n.key);
   // The reference lands on the chord root. With a key signature in the file
   // it is the key's tonic (the highest tonic at or below the lowest note), so
   // a melody keeps its place in the key; without one, the lowest note.
   int ref = lowest;
   if (data.hasKeySig)
   {
      int tonic = ((data.keySigSharps * 7) % 12 + 12) % 12; // major key with that signature
      if (data.keySigMinor)
         tonic = (tonic + 9) % 12;                           // its relative minor
      ref = lowest - (((lowest - tonic) % 12) + 12) % 12;
   }
   out.notes.clear();
   out.stepBeats = 0.25f;
   double lastEnd = 0.0;
   for (const MidiFile::Note& n : data.notes)
   {
      const double b = data.TicksToBeats(n.startTick);
      const double e = data.TicksToBeats(n.endTick);
      PNote p;
      p.kind = kSemitone;
      p.tone = (int)n.key - ref;
      p.step = (float)(b / 0.25);
      p.len = (float)std::max(0.05, (e - b) / 0.25);
      p.vel = std::clamp((float)n.velocity / 127.0f, 0.05f, 1.0f);
      out.notes.push_back(p);
      lastEnd = std::max(lastEnd, e);
      if (out.notes.size() >= 1024)
         break;
   }
   const double bpb = std::max(1.0, 4.0 * (double)data.timeSigNum / (double)std::max(1, data.timeSigDen));
   double lenBeats = std::max(data.LengthBeats(), lastEnd);
   lenBeats = std::max(bpb, std::ceil(lenBeats / bpb - 1e-6) * bpb);
   out.steps = std::clamp((int)std::lround(lenBeats / 0.25), 1, 1024);
   out.source = utf8Path;
   return true;
}

namespace
{
   void PutVarLen(std::vector<unsigned char>& v, uint32_t value)
   {
      unsigned char buf[5];
      int n = 0;
      buf[n++] = (unsigned char)(value & 0x7F);
      while ((value >>= 7) != 0)
         buf[n++] = (unsigned char)((value & 0x7F) | 0x80);
      while (n > 0)
         v.push_back(buf[--n]);
   }
   void Put32(std::vector<unsigned char>& v, uint32_t x)
   {
      v.push_back((unsigned char)(x >> 24));
      v.push_back((unsigned char)(x >> 16));
      v.push_back((unsigned char)(x >> 8));
      v.push_back((unsigned char)x);
   }
}

bool WriteMidi(const std::string& utf8Path, const std::vector<MidiNote>& notes, double bpm, int timeSigNum,
               int timeSigDen, double lengthBeats, std::string& error)
{
   constexpr int kPpq = 480;
   struct Ev
   {
      uint32_t tick;
      int order; // note-offs before note-ons on the same tick
      unsigned char b0, b1, b2;
   };
   std::vector<Ev> evs;
   for (const MidiNote& n : notes)
   {
      const uint32_t on = (uint32_t)std::max(0.0, std::round(n.beat * kPpq));
      const uint32_t off = std::max(on + 1, (uint32_t)std::max(0.0, std::round((n.beat + n.dur) * kPpq)));
      const unsigned char key = (unsigned char)std::clamp(n.key, 0, 127);
      const unsigned char vel = (unsigned char)std::clamp(n.vel, 1, 127);
      evs.push_back({ on, 1, 0x90, key, vel });
      evs.push_back({ off, 0, 0x80, key, 0 });
   }
   std::stable_sort(evs.begin(), evs.end(),
                    [](const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
   std::vector<unsigned char> trk;
   // tempo
   const uint32_t usPerQ = (uint32_t)std::lround(60000000.0 / std::max(1.0, bpm));
   PutVarLen(trk, 0);
   trk.insert(trk.end(), { 0xFF, 0x51, 0x03, (unsigned char)(usPerQ >> 16), (unsigned char)(usPerQ >> 8), (unsigned char)usPerQ });
   // time signature
   int dd = 0;
   for (int d = std::max(1, timeSigDen); d > 1; d >>= 1)
      dd++;
   PutVarLen(trk, 0);
   trk.insert(trk.end(), { 0xFF, 0x58, 0x04, (unsigned char)std::clamp(timeSigNum, 1, 32), (unsigned char)dd, 24, 8 });
   uint32_t last = 0;
   for (const Ev& e : evs)
   {
      PutVarLen(trk, e.tick - last);
      last = e.tick;
      trk.push_back(e.b0);
      trk.push_back(e.b1);
      trk.push_back(e.b2);
   }
   const uint32_t endTick = std::max(last, (uint32_t)std::lround(std::max(0.0, lengthBeats) * kPpq));
   PutVarLen(trk, endTick - last);
   trk.insert(trk.end(), { 0xFF, 0x2F, 0x00 });

   std::vector<unsigned char> file = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                                       (unsigned char)(kPpq >> 8), (unsigned char)(kPpq & 0xFF), 'M', 'T', 'r', 'k' };
   Put32(file, (uint32_t)trk.size());
   file.insert(file.end(), trk.begin(), trk.end());
   std::ofstream f(std::filesystem::u8path(utf8Path), std::ios::binary);
   if (!f)
   {
      error = "could not write " + utf8Path;
      return false;
   }
   f.write((const char*)file.data(), (std::streamsize)file.size());
   return (bool)f;
}
}
