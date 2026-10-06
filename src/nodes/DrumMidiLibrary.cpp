#include "DrumMidiLibrary.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "core/MidiFile.h"
#include "json.hpp"
#include "platform/SettingsPaths.h"

namespace DrumMidi
{
int GmDrumLane(int key)
{
   switch (key)
   {
      case 35: case 36: return 0;                       // kicks
      case 38: case 40: return 1;                       // snares
      case 42: case 44: return 2;                       // closed / pedal hat
      case 46: return 3;                                // open hat
      case 37: case 39: case 75: return 4;              // side stick, clap, claves
      case 41: case 43: case 45:                        // low / floor / low-mid toms
      case 61: case 64: case 66: return 5;              // low bongo, low conga, low timbale
      case 47: case 48: case 50:                        // mid / high toms
      case 60: case 62: case 63: case 65: return 6;     // high bongo, high congas, high timbale
      default: return (key >= 27 && key <= 87) ? 7 : -1; // cymbals, bells, the rest of GM2's map
   }
}

float CellVelocity(int velocity)
{
   return velocity >= 100 ? 1.0f : std::clamp(velocity / 127.0f, 0.05f, 0.95f);
}

int Pattern::BarStart(int bar) const
{
   int s = 0;
   for (int b = 0; b < bar && b < (int)barSteps.size(); b++)
      s += barSteps[b];
   return s;
}

namespace
{
   // Bars longer than this are not kept: a whole song is a few hundred bars,
   // and the browser only ever uses the first three distinct chunks.
   constexpr int kMaxBars = 2048;

   struct Meter
   {
      double beat;
      int num, den;
   };

   struct Bar
   {
      double start, len; // beats
      int num, den;
      bool partial;      // cut short by a meter change
   };
}

bool Analyze(const MidiFile::Data& data, Pattern& out, std::string& error)
{
   out = Pattern();
   // GM drums live on channel 10; files that put them there may also carry
   // bass or chords on other channels, so only channel 10 is read when it
   // has notes. Pattern packs that write drums on channel 1 read everything.
   bool hasCh10 = false;
   for (const MidiFile::Note& nt : data.notes)
      hasCh10 = hasCh10 || nt.channel == 9;
   out.allChannels = !hasCh10 && !data.notes.empty();

   struct Hit
   {
      double beat;
      int lane;
      int velocity;
   };
   std::vector<Hit> hits;
   for (const MidiFile::Note& nt : data.notes)
   {
      if (hasCh10 && nt.channel != 9)
      {
         out.otherChannel++;
         continue;
      }
      const int lane = GmDrumLane(nt.key);
      if (lane < 0)
      {
         out.skipped++;
         continue;
      }
      hits.push_back({ data.TicksToBeats(nt.startTick), lane, (int)nt.velocity });
   }
   out.bpm = data.FirstTempo();
   out.tempoChanges = std::max(0, (int)data.tempos.size() - 1);
   if (hits.empty())
   {
      error = data.notes.empty() ? "the file has no notes" : "no GM drum notes";
      return false;
   }
   out.notes = (int)hits.size();

   // Grid: 16ths, with swing estimated from how late the odd 16ths sit
   // (the node delays odd steps by swing * 0.5 step). Falls back to 16th
   // triplets when those fit clearly better (a shuffle written as triplets).
   double swingSum = 0.0;
   int swingCount = 0;
   for (const Hit& h : hits)
   {
      const double p = h.beat / 0.25;
      const double k = std::floor(p + 0.5);
      if (((long long)k & 1) != 0)
      {
         swingSum += p - k;
         swingCount++;
      }
   }
   const double meanLate = swingCount > 0 ? swingSum / swingCount : 0.0;
   const float estSwing = meanLate > 0.04 ? (float)std::clamp(meanLate * 2.0, 0.0, 1.0) : 0.0f;
   double errSwung = 0.0, errTrip = 0.0;
   for (const Hit& h : hits)
   {
      const double p = h.beat / 0.25;
      double best = 1.0;
      for (double k = std::floor(p) - 1.0; k <= std::floor(p) + 1.0; k += 1.0)
      {
         const double mark = k + ((((long long)k) & 1) != 0 ? estSwing * 0.5 : 0.0);
         best = std::min(best, std::fabs(p - mark));
      }
      errSwung += best * 0.25;                       // in beats
      const double q = h.beat * 6.0;                 // 16th-triplet steps
      errTrip += std::fabs(q - std::floor(q + 0.5)) / 6.0;
   }
   errSwung /= (double)hits.size();
   errTrip /= (double)hits.size();
   out.triplet = errSwung > 0.02 && errTrip < errSwung * 0.5;
   const double stepBeats = out.triplet ? 0.25 * 2.0 / 3.0 : 0.25;
   out.swing = out.triplet ? 0.0f : estSwing;

   // Snap every hit to its step (a swung odd 16th snaps back to its odd step).
   std::vector<long long> stepOf(hits.size());
   for (size_t i = 0; i < hits.size(); i++)
   {
      const double p = hits[i].beat / stepBeats;
      long long k = (long long)std::floor(p + 0.5);
      if (!out.triplet && out.swing > 0.0f)
      {
         // Pick the nearest swung landmark rather than the nearest straight one.
         double best = 1e9;
         for (long long c = (long long)std::floor(p) - 1; c <= (long long)std::floor(p) + 1; c++)
         {
            const double mark = (double)c + ((c & 1) != 0 ? out.swing * 0.5 : 0.0);
            if (std::fabs(p - mark) < best)
            {
               best = std::fabs(p - mark);
               k = c;
            }
         }
      }
      k = std::max(0LL, k);
      stepOf[i] = k;
   }

   // Time signature map. Before 0.50's browser the first signature was used
   // for the whole file; walking the map keeps bars right across meter
   // changes and finds a pickup bar (a short first bar before the main
   // meter starts). An event within a 32nd of the start counts as the start.
   std::vector<Meter> meters;
   for (const MidiFile::Event& e : data.events)
      if (e.kind == MidiFile::kTimeSignature)
      {
         double beat = data.TicksToBeats(e.tick);
         if (beat < 0.125)
            beat = 0.0;
         meters.push_back({ beat, std::max(1, (int)e.data1), std::max(1, (int)e.data2) });
      }
   std::stable_sort(meters.begin(), meters.end(), [](const Meter& a, const Meter& b) { return a.beat < b.beat; });
   // Same position: the last one wins (format 1 files repeat it per track).
   std::vector<Meter> map;
   for (const Meter& m : meters)
   {
      if (!map.empty() && std::fabs(map.back().beat - m.beat) < 1e-9)
         map.back() = m;
      else if (map.empty() || map.back().num != m.num || map.back().den != m.den)
         map.push_back(m);
   }
   if (map.empty())
      map.push_back({ 0.0, std::max(1, data.timeSigNum), std::max(1, data.timeSigDen) });
   map.front().beat = 0.0; // the region before the first signature uses it too

   // The extent comes from where the notes were played, not where they
   // snap: a late last 32nd that rounds onto the next downbeat must not add
   // a bar of its own (it wraps to the loop start below).
   double lastHitBeat = 0.0;
   for (const Hit& h : hits)
      lastHitBeat = std::max(lastHitBeat, h.beat + 1e-6);
   const double fileBeats = data.LengthBeats();
   const double endBeat = std::max(lastHitBeat, fileBeats);
   std::vector<Bar> bars;
   size_t mi = 0;
   for (double start = 0.0; start < endBeat - 1e-6 && (int)bars.size() < kMaxBars * 4;)
   {
      while (mi + 1 < map.size() && map[mi + 1].beat <= start + 1e-6)
         mi++;
      const Meter& m = map[mi];
      double len = std::max(0.125, m.num * 4.0 / m.den);
      bool partial = false;
      if (mi + 1 < map.size() && map[mi + 1].beat < start + len - 1e-6)
      {
         len = map[mi + 1].beat - start;
         partial = true;
      }
      bars.push_back({ start, len, m.num, m.den, partial });
      start += len;
   }
   if (bars.empty())
      bars.push_back({ 0.0, 4.0, 4, 4, false });

   // Hits per bar (by snapped step), to find the leading empty bars, the
   // pickup and an empty last bar.
   auto barOfStep = [&](long long step) {
      const double beat = (double)step * stepBeats + 1e-6;
      int lo = 0, hi = (int)bars.size() - 1;
      while (lo < hi)
      {
         const int mid = (lo + hi + 1) / 2;
         if (bars[mid].start <= beat)
            lo = mid;
         else
            hi = mid - 1;
      }
      return lo;
   };
   std::vector<int> barHits(bars.size(), 0);
   std::vector<int> barOf(hits.size());
   for (size_t i = 0; i < hits.size(); i++)
   {
      barOf[i] = barOfStep(stepOf[i]);
      barHits[barOf[i]]++;
   }
   // The file length rounds to the nearest bar: an empty last bar stays
   // (a rest bar) only when the file covers at least half of it.
   while (bars.size() > 1 && barHits.back() == 0 && fileBeats - bars.back().start < bars.back().len * 0.5)
   {
      bars.pop_back();
      barHits.pop_back();
   }
   int first = 0;
   while (first < (int)bars.size() && barHits[first] == 0)
      first++;
   out.leadBars = first;
   // A pickup: a first bar cut short by the meter change into the main
   // meter (the anacrusis of a song), with full bars after it. It would loop
   // as a broken bar, so it is dropped (and reported).
   // Also a lone short bar in its own meter (a 1/4 bar before the 4/4).
   const bool pickup = first + 1 < (int)bars.size() &&
                       (bars[first].partial || (bars[first].len < bars[first + 1].len - 1e-6 &&
                                                (bars[first].num != bars[first + 1].num || bars[first].den != bars[first + 1].den)));
   if (pickup)
   {
      out.pickupNotes = barHits[first];
      first++;
   }
   int last = std::min((int)bars.size(), first + kMaxBars);

   // Bars to steps. Bar lines on a 16th (or 16th triplet) boundary for every
   // x/4 and x/8 meter; anything finer rounds to the nearest step.
   std::vector<int> barStartStep;
   for (int b = first; b <= last; b++)
   {
      const double beat = b < (int)bars.size() ? bars[b].start : bars.back().start + bars.back().len;
      barStartStep.push_back((int)std::lround(beat / stepBeats));
   }
   for (int b = 0; b + 1 < (int)barStartStep.size(); b++)
      out.barSteps.push_back(std::max(1, barStartStep[b + 1] - barStartStep[b]));
   const int origin = barStartStep.front();
   out.cells.assign((size_t)out.BarStart(out.Bars()), std::array<float, kLanes>{});
   for (size_t i = 0; i < hits.size(); i++)
   {
      if (barOf[i] < first || barOf[i] >= last)
         continue;
      long long s = stepOf[i] - origin;
      if (s >= (long long)out.cells.size() && barOf[i] == last - 1)
         s %= (long long)out.cells.size(); // snapped past the end of the last bar: the loop start
      if (s < 0 || s >= (long long)out.cells.size())
         continue;
      float& cell = out.cells[(size_t)s][hits[i].lane];
      if (cell == 0.0f)
         out.placed++;
      else
         out.merged++;
      cell = std::max(cell, CellVelocity(hits[i].velocity));
      out.lanesUsed |= 1u << hits[i].lane;
   }

   // Meter description.
   std::vector<std::pair<int, int>> seen;
   for (int b = first; b < last; b++)
   {
      const std::pair<int, int> m(bars[b].num, bars[b].den);
      if (std::find(seen.begin(), seen.end(), m) == seen.end())
         seen.push_back(m);
   }
   for (size_t i = 0; i < seen.size() && i < 4; i++)
      out.meter += (i > 0 ? " + " : "") + std::to_string(seen[i].first) + "/" + std::to_string(seen[i].second);
   return true;
}

namespace
{
   int StepsOf(const Pattern& p, int firstBar, int bars)
   {
      int s = 0;
      for (int b = firstBar; b < firstBar + bars && b < p.Bars(); b++)
         s += p.barSteps[b];
      return s;
   }

   // At least 90% of the hits shared (velocity ignored) over `steps` steps.
   bool SameSteps(const Pattern& p, int sa, int sb, int steps)
   {
      int inter = 0, uni = 0;
      for (int s = 0; s < steps; s++)
         for (int l = 0; l < kLanes; l++)
         {
            const bool x = p.cells[(size_t)(sa + s)][l] > 0.0f;
            const bool y = p.cells[(size_t)(sb + s)][l] > 0.0f;
            inter += (x && y) ? 1 : 0;
            uni += (x || y) ? 1 : 0;
         }
      return uni == 0 || inter * 10 >= uni * 9;
   }

   // True when chunk `b` repeats chunk `a`: the same groove at the same
   // length, or (a short tail chunk) the same as some bar-aligned stretch of
   // `a`, e.g. a 17th bar that repeats the 16th.
   bool SameChunk(const Pattern& p, const PartSpan& a, const PartSpan& b)
   {
      if (b.steps > a.steps)
         return false;
      const int sb = p.BarStart(b.firstBar);
      for (int k = 0; k + b.bars <= a.bars; k++)
      {
         if (StepsOf(p, a.firstBar + k, b.bars) != b.steps)
            continue;
         if (SameSteps(p, p.BarStart(a.firstBar + k), sb, b.steps))
            return true;
         if (b.steps == a.steps)
            break;
      }
      return false;
   }
}

void SplitParts(const Pattern& p, int maxSteps, PartSpan out[3])
{
   maxSteps = std::max(1, maxSteps);
   for (int i = 0; i < 3; i++)
      out[i] = PartSpan();
   const int bars = p.Bars();
   if (bars <= 0)
      return;
   if (p.Steps() <= maxSteps)
   {
      out[0] = { 0, bars, p.Steps(), false };
      out[1] = out[2] = { 0, bars, p.Steps(), true };
      return;
   }
   // Chunk length: the bars that fit from the start, down to a power of two
   // so a chunk is a musical phrase (8 bars of 4/4, 4 of 16th triplets).
   int fit = 0;
   while (fit < bars && StepsOf(p, 0, fit + 1) <= maxSteps)
      fit++;
   int chunk = 1;
   while (chunk * 2 <= fit)
      chunk *= 2;
   std::vector<PartSpan> chunks;
   for (int b = 0; b < bars;)
   {
      int n = std::min(chunk, bars - b);
      while (n > 1 && StepsOf(p, b, n) > maxSteps)
         n--;
      // A single bar longer than maxSteps (a 9/4 bar of triplets): cut.
      chunks.push_back({ b, n, std::min(maxSteps, StepsOf(p, b, n)), false });
      b += n;
   }
   out[0] = chunks[0];
   std::vector<int> used = { 0 };
   auto pick = [&](int after) {
      for (int pass = 0; pass < 2; pass++) // full-length chunks first, then a short tail
         for (int c = after + 1; c < (int)chunks.size(); c++)
         {
            if (pass == 0 && chunks[c].bars != chunk)
               continue;
            bool differs = true;
            for (int u : used)
               differs = differs && !SameChunk(p, chunks[u], chunks[c]);
            if (differs)
               return c;
         }
      return -1;
   };
   const int b = pick(0);
   const int c = b >= 0 ? (used.push_back(b), pick(b)) : -1;
   PartSpan copy = out[0];
   copy.copyOfA = true;
   out[1] = b >= 0 ? chunks[b] : copy;
   out[2] = c >= 0 ? chunks[c] : copy;
}

std::string SpanLabel(const PartSpan& s)
{
   if (s.copyOfA)
      return "= A";
   if (s.bars <= 1)
      return "bar " + std::to_string(s.firstBar + 1);
   return "bars " + std::to_string(s.firstBar + 1) + "-" + std::to_string(s.firstBar + s.bars);
}

// ---------------------------------------------------------------- library
namespace
{
   std::string Lower(std::string s)
   {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
   }

   // Path key for matching index.json entries against the files found:
   // normalised, forward slashes, lower case (Windows paths ignore case).
   std::string PathKey(const std::filesystem::path& p)
   {
      return Lower(p.lexically_normal().generic_u8string());
   }

   // Curly quotes and dashes to ASCII: the UI font has no glyphs for them.
   std::string Asciify(const std::string& s)
   {
      std::string out;
      for (size_t i = 0; i < s.size(); i++)
      {
         const unsigned char c = (unsigned char)s[i];
         if (c == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80)
         {
            const unsigned char d = (unsigned char)s[i + 2];
            if (d == 0x98 || d == 0x99) { out += '\''; i += 2; continue; }
            if (d == 0x9C || d == 0x9D) { out += '"'; i += 2; continue; }
            if (d == 0x93 || d == 0x94) { out += '-'; i += 2; continue; }
         }
         if (c >= 32)
            out += (char)c;
      }
      return out;
   }

   std::string Trim(std::string s)
   {
      while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
         s.pop_back();
      size_t a = 0;
      while (a < s.size() && (s[a] == ' ' || s[a] == '\t'))
         a++;
      return s.substr(a);
   }

   struct IndexMeta
   {
      std::string title, style;
      int bpm = 0, bars = 0;
   };

   std::string JsonText(const nlohmann::json& o, const char* a, const char* b)
   {
      for (const char* k : { a, b })
         if (o.contains(k) && o[k].is_string())
            return Trim(Asciify(o[k].get<std::string>()));
      return std::string();
   }

   int JsonInt(const nlohmann::json& o, const char* a, const char* b)
   {
      for (const char* k : { a, b })
         if (o.contains(k))
         {
            if (o[k].is_number())
               return (int)std::lround(std::clamp(o[k].get<double>(), 0.0, 100000.0));
            if (o[k].is_string())
               return std::clamp(std::atoi(o[k].get<std::string>().c_str()), 0, 100000);
         }
      return 0;
   }

   // index.json: an array of objects, or an object holding one ("patterns",
   // "files", "items"). Each names its file relative to the index folder.
   void ReadIndex(const std::filesystem::path& indexPath, std::map<std::string, IndexMeta>& out)
   {
      std::error_code ec;
      const uintmax_t size = std::filesystem::file_size(indexPath, ec);
      if (ec || size > 8u * 1024u * 1024u)
         return;
      std::ifstream f(indexPath, std::ios::binary);
      if (!f)
         return;
      const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
      if (j.is_discarded())
         return;
      const nlohmann::json* arr = j.is_array() ? &j : nullptr;
      if (arr == nullptr && j.is_object())
         for (const char* k : { "patterns", "files", "items" })
            if (j.contains(k) && j[k].is_array())
            {
               arr = &j[k];
               break;
            }
      if (arr == nullptr)
         return;
      const std::filesystem::path dir = indexPath.parent_path();
      for (const nlohmann::json& o : *arr)
      {
         if (!o.is_object())
            continue;
         std::string file = JsonText(o, "arquivo", "file");
         if (file.empty())
            file = JsonText(o, "path", "arquivo");
         if (file.empty())
            continue;
         std::replace(file.begin(), file.end(), '\\', '/');
         IndexMeta m;
         m.title = JsonText(o, "titulo", "title");
         m.style = JsonText(o, "estilo", "style");
         m.bpm = JsonInt(o, "bpm", "tempo");
         m.bars = JsonInt(o, "compassos", "bars");
         out[PathKey(dir / std::filesystem::u8path(file))] = m;
      }
   }

   bool IsMidiExt(const std::filesystem::path& p)
   {
      const std::string e = Lower(p.extension().u8string());
      return e == ".mid" || e == ".midi";
   }
}

std::string TitleFromFileName(const std::string& fileName)
{
   std::string s = fileName;
   const size_t slash = s.find_last_of("/\\");
   if (slash != std::string::npos)
      s = s.substr(slash + 1);
   const size_t dot = s.find_last_of('.');
   if (dot != std::string::npos && dot > 0)
      s = s.substr(0, dot);
   // A leading "NNN_" / "NNN - " number is just the pack's order.
   size_t i = 0;
   while (i < s.size() && std::isdigit((unsigned char)s[i]))
      i++;
   if (i > 0 && i < s.size() && (s[i] == '_' || s[i] == '-' || s[i] == ' ' || s[i] == '.'))
   {
      while (i < s.size() && (s[i] == '_' || s[i] == '-' || s[i] == ' ' || s[i] == '.'))
         i++;
      s = s.substr(i);
   }
   std::replace(s.begin(), s.end(), '_', ' ');
   std::string out;
   for (char c : s)
      if (!(c == ' ' && !out.empty() && out.back() == ' '))
         out += c;
   out = Trim(out);
   return out.empty() ? fileName : out;
}

std::vector<int> Library::InGroup(const std::string& group) const
{
   std::vector<int> out;
   for (int i = 0; i < (int)entries.size(); i++)
      if (entries[i].group == group)
         out.push_back(i);
   return out;
}

int Library::GroupIndex(const std::string& group) const
{
   for (int i = 0; i < (int)groups.size(); i++)
      if (groups[i] == group)
         return i;
   return -1;
}

int Library::Find(const std::string& path) const
{
   if (path.empty())
      return -1;
   const std::string key = PathKey(std::filesystem::u8path(path));
   for (int i = 0; i < (int)keys.size() && i < (int)entries.size(); i++)
      if (keys[i] == key)
         return i;
   return -1;
}

namespace
{
   // Set at exit so a scan of a big folder stops early and the static
   // Scanner's destructor does not hold up the app's exit.
   std::atomic<bool> gScanAbort { false };
}

Library Scan(const std::string& root, bool parseFiles)
{
   constexpr size_t kMaxFiles = 4000;
   Library lib;
   lib.root = root;
   lib.scanned = true;
   if (root.empty())
   {
      lib.error = "no user data folder";
      return lib;
   }
   namespace fs = std::filesystem;
   const fs::path rootPath = fs::u8path(root);
   std::error_code ec;
   if (!fs::is_directory(rootPath, ec))
   {
      lib.error = "folder not found: " + root;
      return lib;
   }
   std::vector<fs::path> files;
   std::map<std::string, IndexMeta> index;
   // increment(ec), not a range-for: operator++ throws on an I/O error.
   for (fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec), end;
        !ec && it != end && !gScanAbort.load(std::memory_order_relaxed); it.increment(ec))
   {
      if (it.depth() > 8)
      {
         it.disable_recursion_pending();
         continue;
      }
      std::error_code fileEc;
      if (!it->is_regular_file(fileEc))
         continue;
      const fs::path& p = it->path();
      if (Lower(p.filename().u8string()) == "index.json")
         ReadIndex(p, index);
      else if (IsMidiExt(p))
      {
         if (files.size() >= kMaxFiles)
         {
            lib.truncated = true;
            continue;
         }
         files.push_back(p);
      }
   }

   std::map<std::string, std::string> groupSpelling; // lower -> first spelling
   for (const fs::path& p : files)
   {
      if (gScanAbort.load(std::memory_order_relaxed))
         break;
      Entry e;
      e.path = p.lexically_normal().u8string();
      auto it = index.find(PathKey(p));
      if (it != index.end())
      {
         e.title = it->second.title;
         e.group = it->second.style;
         e.bpm = it->second.bpm;
         e.indexBars = it->second.bars;
      }
      if (e.title.empty())
         e.title = TitleFromFileName(p.filename().u8string());
      if (e.group.empty())
      {
         const fs::path parent = p.parent_path();
         std::error_code eqEc;
         e.group = fs::equivalent(parent, rootPath, eqEc) ? std::string("misc") : parent.filename().u8string();
      }
      const std::string key = Lower(e.group);
      auto gs = groupSpelling.find(key);
      if (gs == groupSpelling.end())
         groupSpelling[key] = e.group;
      else
         e.group = gs->second;
      if (parseFiles)
      {
         std::error_code sizeEc;
         const uintmax_t size = fs::file_size(p, sizeEc);
         MidiFile::Data data;
         std::string err;
         if (!sizeEc && size <= 4u * 1024u * 1024u && MidiFile::Load(e.path, data, err))
         {
            Pattern pat;
            if (Analyze(data, pat, err))
               e.bars = pat.Bars();
            else
               e.empty = true;
            if (e.bpm <= 0 && !data.tempos.empty())
               e.bpm = (int)std::lround(data.FirstTempo());
         }
         else
            e.unreadable = true;
      }
      lib.entries.push_back(std::move(e));
   }
   std::stable_sort(lib.entries.begin(), lib.entries.end(), [](const Entry& a, const Entry& b) {
      const std::string ga = Lower(a.group), gb = Lower(b.group);
      if (ga != gb)
         return ga < gb;
      return Lower(a.path) < Lower(b.path); // the pack's own order (numbered files)
   });
   for (const Entry& e : lib.entries)
   {
      if (lib.groups.empty() || lib.groups.back() != e.group)
         lib.groups.push_back(e.group);
      lib.keys.push_back(PathKey(fs::u8path(e.path)));
   }
   return lib;
}

std::string DefaultFolder()
{
   const std::string root = InfiniteSettingsDirectory();
   if (root.empty())
      return std::string();
   const std::filesystem::path dir = std::filesystem::u8path(root) / "DrumPatterns";
   std::error_code ec;
   std::filesystem::create_directories(dir, ec);
   return ec ? std::string() : dir.u8string();
}

// ---------------------------------------------------------- background scan
namespace
{
   struct Scanner
   {
      std::mutex mutex;
      std::shared_ptr<const Library> current = std::make_shared<Library>();
      std::thread thread;
      bool running = false; // guarded by mutex
      bool pending = false; // a rescan asked for while one runs
      bool started = false;
      unsigned generation = 0;

      ~Scanner()
      {
         // Joining keeps the thread from touching this object after it is
         // gone; the abort flag makes a running scan return quickly.
         gScanAbort.store(true, std::memory_order_relaxed);
         if (thread.joinable())
            thread.join();
      }

      void Run()
      {
         for (;;)
         {
            // An exception escaping this thread would terminate the app
            // (a path the platform cannot convert, an allocation failure).
            Library lib;
            try
            {
               lib = Scan(DefaultFolder(), true);
            }
            catch (const std::exception& e)
            {
               lib = Library();
               lib.scanned = true;
               lib.error = std::string("scan failed: ") + e.what();
            }
            std::lock_guard<std::mutex> lock(mutex);
            lib.generation = ++generation;
            lib.scanning = pending;
            current = std::make_shared<Library>(std::move(lib));
            if (!pending)
            {
               running = false;
               return;
            }
            pending = false;
         }
      }

      void Request()
      {
         std::lock_guard<std::mutex> lock(mutex);
         started = true;
         if (running)
         {
            pending = true;
            return;
         }
         if (thread.joinable())
            thread.join(); // finished: running is false only after its last publish
         running = true;
         auto next = std::make_shared<Library>(*current);
         next->scanning = true;
         current = next;
         thread = std::thread([this] { Run(); });
      }
   };

   Scanner& TheScanner()
   {
      static Scanner s;
      return s;
   }
}

void RequestScan()
{
   TheScanner().Request();
}

void EnsureScanned()
{
   Scanner& s = TheScanner();
   bool started;
   {
      std::lock_guard<std::mutex> lock(s.mutex);
      started = s.started;
   }
   if (!started)
      s.Request();
}

std::shared_ptr<const Library> Current()
{
   Scanner& s = TheScanner();
   std::lock_guard<std::mutex> lock(s.mutex);
   return s.current;
}
}
