#include "SliceSet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <sstream>

namespace
{
   // %.9g round-trips a float exactly, and the same float always prints the same way.
   std::string F(float v)
   {
      char b[32];
      snprintf(b, sizeof(b), "%.9g", (double)v);
      return b;
   }

   // Names are free text on a space-separated line: escape the separators.
   std::string Escape(const std::string& s)
   {
      std::string o;
      for (char c : s)
      {
         if (c == ' ' || c == '%' || c == '=' || c == '\n' || c == '\r' || c == '\t')
         {
            char b[4];
            snprintf(b, sizeof(b), "%%%02X", (unsigned char)c);
            o += b;
         }
         else
            o += c;
      }
      return o;
   }

   std::string Unescape(const std::string& s)
   {
      std::string o;
      for (size_t i = 0; i < s.size(); i++)
      {
         if (s[i] == '%' && i + 2 < s.size())
         {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
         }
         else
            o += s[i];
      }
      return o;
   }

   // One field per param: its key, a writer that says whether it differs from the default, and a reader.
   struct Field
   {
      const char* key;
      float SliceParams::*f;
      int SliceParams::*i;
      bool SliceParams::*b;
   };

   const Field kFields[] = {
      { "g", &SliceParams::gainDb, nullptr, nullptr },
      { "pan", &SliceParams::pan, nullptr, nullptr },
      { "p", &SliceParams::pitchSt, nullptr, nullptr },
      { "ft", &SliceParams::fineCt, nullptr, nullptr },
      { "st", nullptr, &SliceParams::stretchMode, nullptr },
      { "len", &SliceParams::lengthRatio, nullptr, nullptr },
      { "fit", nullptr, nullptr, &SliceParams::fitToGrid },
      { "ca", &SliceParams::compAmount, nullptr, nullptr },
      { "ct", &SliceParams::compThreshDb, nullptr, nullptr },
      { "cr", &SliceParams::compRatio, nullptr, nullptr },
      { "cat", &SliceParams::compAttackMs, nullptr, nullptr },
      { "crl", &SliceParams::compReleaseMs, nullptr, nullptr },
      { "cm", &SliceParams::compMakeupDb, nullptr, nullptr },
      { "tr", &SliceParams::transient, nullptr, nullptr },
      { "dc", &SliceParams::decay, nullptr, nullptr },
      { "fi", &SliceParams::fadeInMs, nullptr, nullptr },
      { "fo", &SliceParams::fadeOutMs, nullptr, nullptr },
      { "fc", &SliceParams::fadeCurve, nullptr, nullptr },
      { "flt", nullptr, &SliceParams::filterMode, nullptr },
      { "cut", &SliceParams::cutoffHz, nullptr, nullptr },
      { "res", &SliceParams::res, nullptr, nullptr },
      { "rev", nullptr, nullptr, &SliceParams::reverse },
      { "loop", nullptr, &SliceParams::loopMode, nullptr },
      { "ls", &SliceParams::loopStart, nullptr, nullptr },
      { "le", &SliceParams::loopEnd, nullptr, nullptr },
      { "mute", nullptr, nullptr, &SliceParams::mute },
      { "solo", nullptr, nullptr, &SliceParams::solo },
      { "ch", nullptr, &SliceParams::choke, nullptr },
   };

   // key=value tokens of one line, after its leading tag.
   std::vector<std::pair<std::string, std::string>> Tokens(const std::string& line)
   {
      std::vector<std::pair<std::string, std::string>> out;
      std::istringstream in(line);
      std::string tok;
      in >> tok; // the tag
      while (in >> tok)
      {
         const size_t eq = tok.find('=');
         if (eq != std::string::npos)
            out.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
      }
      return out;
   }
}

bool SliceParams::operator==(const SliceParams& o) const
{
   for (const Field& f : kFields)
   {
      if (f.f && this->*f.f != o.*f.f)
         return false;
      if (f.i && this->*f.i != o.*f.i)
         return false;
      if (f.b && this->*f.b != o.*f.b)
         return false;
   }
   return true;
}

void CopyChangedFields(const SliceParams& before, const SliceParams& after, SliceParams& dst)
{
   for (const Field& f : kFields)
   {
      if (f.f && before.*f.f != after.*f.f)
         dst.*f.f = after.*f.f;
      if (f.i && before.*f.i != after.*f.i)
         dst.*f.i = after.*f.i;
      if (f.b && before.*f.b != after.*f.b)
         dst.*f.b = after.*f.b;
   }
}

bool SampleWide::operator==(const SampleWide& o) const
{
   return pitch == o.pitch && fine == o.fine && speed == o.speed && fadeInMs == o.fadeInMs && fadeOutMs == o.fadeOutMs &&
          gainDb == o.gainDb && loop == o.loop && reverse == o.reverse && pingPong == o.pingPong;
}

const std::vector<const char*>& SliceFilterNames()
{
   static const std::vector<const char*> k = { "off", "lp 12", "lp 24", "hp 12", "hp 24", "bp 12" };
   return k;
}

const char* SliceStretchName(int mode)
{
   static const char* const k[] = { "off", "repitch", "beats", "tonal" };
   return k[std::clamp(mode, 0, 3)];
}

int SliceSet::IndexAt(int64_t frame) const
{
   if (slices.empty())
      return -1;
   const auto it = std::upper_bound(slices.begin(), slices.end(), frame, [](int64_t f, const Slice& s) { return f < s.start; });
   return std::max(0, (int)(it - slices.begin()) - 1);
}

int SliceSet::IndexOfId(uint16_t id) const
{
   for (int i = 0; i < Count(); i++)
      if (slices[(size_t)i].id == id)
         return i;
   return -1;
}

bool SliceSet::AnySolo() const
{
   for (const Slice& s : slices)
      if (s.p.solo)
         return true;
   return false;
}

void SliceSet::Reset(int64_t from, int64_t to)
{
   slices.clear();
   Slice s;
   s.id = nextId++;
   s.start = from;
   slices.push_back(s);
   endFrame = std::max(from + 1, to);
   mHandEdited = false;
}

int SliceSet::SplitAt(int64_t frame, int64_t minGap)
{
   if (slices.empty() || Count() >= kMaxSlices || frame <= StartFrame() || frame >= endFrame)
      return -1;
   const int i = IndexAt(frame);
   if (frame - slices[(size_t)i].start < minGap || EndOf(i) - frame < minGap)
      return -1;
   Slice s = slices[(size_t)i];
   s.id = nextId++;
   s.start = frame;
   s.name.clear();
   s.note = -1;
   slices.insert(slices.begin() + i + 1, s);
   mHandEdited = true;
   return i + 1;
}

bool SliceSet::Join(int i)
{
   if (i < 1 || i >= Count())
      return false;
   slices.erase(slices.begin() + i);
   mHandEdited = true;
   return true;
}

void SliceSet::Move(int i, int64_t frame, int64_t minGap)
{
   if (i < 0 || i >= Count())
      return;
   const int64_t lo = i > 0 ? slices[(size_t)i - 1].start + minGap : 0;
   const int64_t hi = EndOf(i) - minGap;
   slices[(size_t)i].start = std::clamp(frame, lo, std::max(lo, hi));
   mHandEdited = true;
}

void SliceSet::MoveEnd(int64_t frame, int64_t minGap)
{
   if (slices.empty())
      return;
   endFrame = std::max(slices.back().start + minGap, frame);
}

void SliceSet::Fill(const std::vector<int64_t>& starts, int64_t to)
{
   slices.clear();
   for (int64_t st : starts)
   {
      Slice s;
      s.id = nextId++;
      s.start = st;
      slices.push_back(s);
   }
   endFrame = to;
   mHandEdited = false;
}

void SliceSet::FromOnsets(const std::vector<int>& onsets, const std::vector<float>& strengths, int64_t from, int64_t to, int maxSlices)
{
   maxSlices = std::clamp(maxSlices, 1, kMaxSlices);
   std::vector<int> order(onsets.size());
   std::iota(order.begin(), order.end(), 0);
   // Strongest first (the forced onset at 0 carries a huge strength), then back in time order.
   std::stable_sort(order.begin(), order.end(), [&](int a, int b)
                    { return (a < (int)strengths.size() ? strengths[(size_t)a] : 0.0f) > (b < (int)strengths.size() ? strengths[(size_t)b] : 0.0f); });
   if ((int)order.size() > maxSlices)
      order.resize((size_t)maxSlices);
   std::vector<int64_t> starts;
   for (int k : order)
   {
      const int64_t f = from + onsets[(size_t)k];
      if (f >= from && f < to)
         starts.push_back(f);
   }
   starts.push_back(from);
   std::sort(starts.begin(), starts.end());
   starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
   Fill(starts, to);
}

void SliceSet::FromGrid(double framesPerStep, int64_t from, int64_t to)
{
   std::vector<int64_t> starts;
   framesPerStep = std::max(1.0, framesPerStep);
   for (int k = 0; k < kMaxSlices; k++)
   {
      const int64_t f = from + (int64_t)std::llround(framesPerStep * k);
      if (f >= to)
         break;
      starts.push_back(f);
   }
   Fill(starts, to);
}

void SliceSet::FromEqual(int n, int64_t from, int64_t to)
{
   n = std::clamp(n, 1, kMaxSlices);
   std::vector<int64_t> starts;
   for (int k = 0; k < n; k++)
      starts.push_back(from + (to - from) * k / n);
   starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
   Fill(starts, to);
}

std::string SliceSet::Serialize() const
{
   static const SliceParams kDef;
   static const SampleWide kWide;
   std::string o = "sliceset 1 end=" + std::to_string(endFrame) + " next=" + std::to_string(nextId);
   if (sampleBpm != 0.0f)
      o += " bpm=" + F(sampleBpm);
   if (xfadeMs != 2.0f)
      o += " xf=" + F(xfadeMs);
   if (mHandEdited)
      o += " hand=1";
   o += "\n";
   std::string w;
   const auto wf = [&](const char* k, float v, float d) { if (v != d) w += std::string(" ") + k + "=" + F(v); };
   const auto wb = [&](const char* k, bool v, bool d) { if (v != d) w += std::string(" ") + k + "=" + (v ? "1" : "0"); };
   wf("p", wide.pitch, kWide.pitch);
   wf("ft", wide.fine, kWide.fine);
   wf("sp", wide.speed, kWide.speed);
   wf("fi", wide.fadeInMs, kWide.fadeInMs);
   wf("fo", wide.fadeOutMs, kWide.fadeOutMs);
   wf("g", wide.gainDb, kWide.gainDb);
   wb("loop", wide.loop, kWide.loop);
   wb("rev", wide.reverse, kWide.reverse);
   wb("pp", wide.pingPong, kWide.pingPong);
   if (!w.empty())
      o += "sample" + w + "\n";
   for (const Slice& s : slices)
   {
      o += "slice id=" + std::to_string(s.id) + " s=" + std::to_string(s.start);
      if (s.colour >= 0)
         o += " col=" + std::to_string(s.colour);
      if (s.note >= 0)
         o += " n=" + std::to_string(s.note);
      if (!s.name.empty())
         o += " name=" + Escape(s.name);
      for (const Field& f : kFields)
      {
         if (f.f && s.p.*f.f != kDef.*f.f)
            o += std::string(" ") + f.key + "=" + F(s.p.*f.f);
         if (f.i && s.p.*f.i != kDef.*f.i)
            o += std::string(" ") + f.key + "=" + std::to_string(s.p.*f.i);
         if (f.b && s.p.*f.b != kDef.*f.b)
            o += std::string(" ") + f.key + "=" + (s.p.*f.b ? "1" : "0");
      }
      o += "\n";
   }
   return o;
}

bool SliceSet::Parse(const std::string& text, SliceSet& out)
{
   out = SliceSet();
   std::istringstream in(text);
   std::string line;
   bool header = false;
   while (std::getline(in, line))
   {
      const std::string tag = line.substr(0, line.find(' '));
      if (tag == "sliceset")
      {
         std::istringstream h(line);
         std::string t;
         int version = 0;
         h >> t >> version;
         if (version < 1)
            return false;
         header = true;
         for (const auto& [k, v] : Tokens(line))
         {
            if (k == "end")
               out.endFrame = std::strtoll(v.c_str(), nullptr, 10);
            else if (k == "next")
               out.nextId = (uint16_t)std::clamp(atoi(v.c_str()), 1, 65535);
            else if (k == "bpm")
               out.sampleBpm = std::strtof(v.c_str(), nullptr);
            else if (k == "xf")
               out.xfadeMs = std::clamp(std::strtof(v.c_str(), nullptr), 0.0f, 20.0f);
            else if (k == "hand")
               out.mHandEdited = v == "1";
         }
      }
      else if (tag == "sample")
      {
         SampleWide& w = out.wide;
         for (const auto& [k, v] : Tokens(line))
         {
            const float f = std::strtof(v.c_str(), nullptr);
            if (k == "p") w.pitch = f;
            else if (k == "ft") w.fine = f;
            else if (k == "sp") w.speed = std::clamp(f, 0.25f, 4.0f);
            else if (k == "fi") w.fadeInMs = f;
            else if (k == "fo") w.fadeOutMs = f;
            else if (k == "g") w.gainDb = f;
            else if (k == "loop") w.loop = v == "1";
            else if (k == "rev") w.reverse = v == "1";
            else if (k == "pp") w.pingPong = v == "1";
         }
      }
      else if (tag == "slice")
      {
         if ((int)out.slices.size() >= kMaxSlices)
            continue;
         Slice s;
         for (const auto& [k, v] : Tokens(line))
         {
            if (k == "id")
               s.id = (uint16_t)std::clamp(atoi(v.c_str()), 0, 65535);
            else if (k == "s")
               s.start = std::strtoll(v.c_str(), nullptr, 10);
            else if (k == "col")
               s.colour = atoi(v.c_str());
            else if (k == "n")
               s.note = std::clamp(atoi(v.c_str()), 0, 127);
            else if (k == "name")
               s.name = Unescape(v);
            else
               for (const Field& f : kFields)
                  if (k == f.key)
                  {
                     if (f.f)
                        s.p.*f.f = std::strtof(v.c_str(), nullptr);
                     if (f.i)
                        s.p.*f.i = atoi(v.c_str());
                     if (f.b)
                        s.p.*f.b = v == "1";
                  }
         }
         out.slices.push_back(s);
      }
   }
   if (!header || out.slices.empty())
      return false;
   // A hand-edited file: keep starts ascending and inside the sample, ids unique and below nextId.
   std::stable_sort(out.slices.begin(), out.slices.end(), [](const Slice& a, const Slice& b) { return a.start < b.start; });
   for (Slice& s : out.slices)
      if (s.id == 0 || s.id >= out.nextId)
         s.id = out.nextId++;
   out.endFrame = std::max(out.endFrame, out.slices.back().start + 1);
   return true;
}

bool SliceSet::operator==(const SliceSet& o) const
{
   if (slices.size() != o.slices.size() || endFrame != o.endFrame || sampleBpm != o.sampleBpm || xfadeMs != o.xfadeMs ||
       nextId != o.nextId || !(wide == o.wide) || mHandEdited != o.mHandEdited)
      return false;
   for (size_t i = 0; i < slices.size(); i++)
   {
      const Slice &a = slices[i], &b = o.slices[i];
      if (a.id != b.id || a.start != b.start || a.name != b.name || a.colour != b.colour || a.note != b.note || a.p != b.p)
         return false;
   }
   return true;
}

int64_t SnapToZeroCrossing(const float* l, const float* r, int64_t frames, int64_t frame, int window)
{
   if (l == nullptr || frames < 2)
      return frame;
   const auto mono = [&](int64_t i) { return r != nullptr ? 0.5f * (l[i] + r[i]) : l[i]; };
   for (int d = 0; d <= window; d++)
      for (int sgn : { -1, 1 })
      {
         const int64_t i = frame + sgn * d;
         if (i < 1 || i >= frames)
            continue;
         const float a = mono(i - 1), b = mono(i);
         if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f))
            return std::fabs(a) < std::fabs(b) ? i - 1 : i;
      }
   return frame;
}
