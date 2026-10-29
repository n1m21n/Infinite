// Notes for the Piano Roll from the Generate Samples engine. See NoteGen.h.
#include "app/graph/NoteGen.h"

#include "app/graph/OfflineGraph.h"
#include "app/graph/SampleStyles.h"
#include "audio/MusicTime.h"
#include "core/Patch.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace NoteGen
{
namespace
{
   using Arrange::kPPQ;
   using Arrange::Note;
   using Arrange::Tick;
   using SampleStyles::Part;

   constexpr double kRate = 48000.0;
   constexpr int kMaxRedraws = 3;
   constexpr uint32_t kRedrawStep = 7919u; // the Generate Samples node's redraw step
   constexpr Tick kSixteenth = kPPQ / 4;
   constexpr Tick kDrumHit = kPPQ / 8; // drum notes are fixed-length hits

   const char* const kTypeNames[(int)Type::Count] = { "chords", "comp", "pads", "strum", "arp", "bass", "melody", "drums" };
   const char* const kFormNames[(int)Form::Count] = { "none", "aaba", "abab", "aaab" };

   // Drum Sequencer lanes in SampleStyles' kit order (kick, snare, closed hat,
   // open hat, clap, low tom, high tom, bell) as General MIDI drum notes.
   constexpr int kGmDrum[8] = { 36, 38, 42, 46, 39, 45, 50, 56 };

   // A deterministic stream for the post-passes (density): the same on every
   // platform, unlike the standard distributions.
   struct Hash
   {
      uint64_t s;
      double Uniform()
      {
         s += 0x9E3779B97F4A7C15ull;
         uint64_t z = s;
         z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
         z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
         return (double)((z ^ (z >> 31)) >> 11) * (1.0 / 9007199254740992.0);
      }
   };

   // Creativity 0 .. 0.3 .. 1 onto the Recipe's variation 0 .. 0.5 .. 1:
   // the default creativity is the card as Generate Samples plays it.
   float VariationOf(float c)
   {
      c = std::clamp(c, 0.0f, 1.0f);
      return c <= 0.3f ? c / 0.3f * 0.5f : 0.5f + (c - 0.3f) / 0.7f * 0.5f;
   }

   SampleStyles::Recipe RecipeOf(const GenSpec& spec, uint32_t seed)
   {
      SampleStyles::Recipe r;
      r.style = spec.style;
      r.seed = seed;
      r.variation = VariationOf(spec.creativity);
      r.bars = std::clamp(spec.bars, 1, 8);
      r.key = spec.key;
      r.scale = spec.scale;
      r.personal = spec.personal;
      r.energy = std::clamp(spec.energy, -1.0f, 1.0f);
      r.mood = std::clamp(spec.mood, -1.0f, 1.0f);
      r.chordSize = spec.chordSize;
      r.arpMode = spec.arpMode;
      r.arpOctaves = spec.arpOctaves;
      r.bassRole = spec.bassRole;
      switch (spec.type)
      {
      case Type::Chords: r.part = Part::Keys; r.keysPlay = 0; break;
      case Type::Comp:   r.part = Part::Keys; r.keysPlay = 1; break;
      case Type::Arp:    r.part = Part::Keys; r.keysPlay = 2; break;
      case Type::Pads:   r.part = Part::Pads; break;
      case Type::Strum:  r.part = Part::Plucks; break;
      case Type::Bass:   r.part = Part::Bass; break;
      case Type::Melody: r.part = Part::Leads; break;
      case Type::Drums:  r.part = Part::Drums; break;
      default: break;
      }
      return r;
   }

   // A saved param's text by its key; the record's keys carry the type tag ("f lane0_step3").
   const std::string* ParamText(const Patch::NodeRecord& rec, const std::string& key)
   {
      for (const auto& [k, v] : rec.params)
         if (k.size() == key.size() + 2 && k[1] == ' ' && k.compare(2, std::string::npos, key) == 0)
            return &v;
      return nullptr;
   }
   int ParamInt(const Patch::NodeRecord& rec, const std::string& key, int fallback)
   {
      const std::string* v = ParamText(rec, key);
      return v != nullptr ? std::atoi(v->c_str()) : fallback;
   }
   float ParamFloat(const Patch::NodeRecord& rec, const std::string& key, float fallback)
   {
      const std::string* v = ParamText(rec, key);
      return v != nullptr ? (float)std::atof(v->c_str()) : fallback;
   }

   // The Drum Sequencer is an audio source, not a note source: its pattern
   // is read from the compiled params, which hold the whole groove even for
   // lanes a synth drum replaced (those are only turned down).
   void DrumNotes(const Patch::Data& data, double beats, std::vector<app::OfflineGraph::CapturedNote>& out)
   {
      for (const Patch::NodeRecord& rec : data.nodes)
      {
         if (rec.typeName != "Drum Sequencer")
            continue;
         const int steps = std::clamp(ParamInt(rec, "steps", 16), 1, 32);
         const int rate = std::clamp(ParamInt(rec, "rate", (int)MusicTime::kSixteenth), 0, (int)MusicTime::kNumRateDivisions - 1);
         const double stepBeats = std::max(1e-3, MusicTime::BeatsFor((MusicTime::RateDivision)rate));
         const float swing = std::clamp(ParamFloat(rec, "swing", 0.0f), 0.0f, 1.0f);
         const int total = (int)std::floor(beats / stepBeats + 1e-6);
         for (int lane = 0; lane < 8; lane++)
            for (int k = 0; k < total; k++)
            {
               const int step = k % steps;
               const float v = ParamFloat(rec, "lane" + std::to_string(lane) + "_step" + std::to_string(step), 0.0f);
               if (v <= 0.0f)
                  continue;
               app::OfflineGraph::CapturedNote c;
               c.startBeats = ((double)k + (step % 2 == 1 ? swing * 0.5 : 0.0)) * stepBeats; // the sequencer's own swing
               c.lengthBeats = Arrange::TicksToBeats(kDrumHit);
               c.pitch = kGmDrum[lane];
               c.velocity = std::clamp(v, 0.0f, 1.0f);
               c.voice = rec.index;
               out.push_back(c);
            }
      }
   }

   // One draw: compile, render the notes, keep the part's voice.
   bool Draw(const GenSpec& spec, uint32_t seed, std::vector<Note>& notes, int& root, int& scale, float& bpm, std::string& error)
   {
      const SampleStyles::Recipe recipe = RecipeOf(spec, seed);
      const SampleStyles::Compiled c = SampleStyles::Compile(recipe);
      if (!c.error.empty())
      {
         error = c.error;
         return false;
      }
      root = c.root;
      scale = c.scale;
      bpm = c.bpm;
      Patch::Data data;
      std::string err;
      if (!Patch::ReadText(c.patch, data, err))
      {
         error = "compiled patch did not read: " + err;
         return false;
      }
      const double beats = 4.0 * recipe.bars;
      std::vector<app::OfflineGraph::CapturedNote> captured;
      if (spec.type == Type::Drums)
         DrumNotes(data, beats, captured);
      else
      {
         app::OfflineGraph graph;
         if (!graph.Build(data, c.tap) || !graph.Prepare(kRate))
         {
            error = "note graph did not build: " + graph.Error();
            return false;
         }
         graph.RenderNotes(beats, captured);
         // The part's voice is the one that took the most notes: a part plays
         // one instrument; a second voice (a top line) is not this type.
         std::map<int, int> perVoice;
         for (const auto& n : captured)
            perVoice[n.voice]++;
         int keep = -1, most = 0;
         for (const auto& [voice, count] : perVoice)
            if (count > most)
            {
               most = count;
               keep = voice;
            }
         captured.erase(std::remove_if(captured.begin(), captured.end(), [keep](const auto& n) { return n.voice != keep; }), captured.end());
      }
      notes.clear();
      const Tick span = Arrange::BeatsToTicks(beats);
      for (const auto& cn : captured)
      {
         Note n;
         n.start = std::clamp<Tick>(Arrange::BeatsToTicks(cn.startBeats), 0, span - 1);
         n.length = std::clamp<Tick>(Arrange::BeatsToTicks(cn.lengthBeats), 1, span - n.start);
         n.pitch = (uint8_t)std::clamp(cn.pitch, 0, 127);
         n.vel = (uint8_t)std::clamp((int)std::lround(cn.velocity * 127.0f), 1, 127);
         notes.push_back(n);
      }
      return true;
   }

   void SortNotes(std::vector<Note>& notes)
   {
      std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
         return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
      });
   }

   // A single line's pitches for the melody critic: the top note at each onset.
   std::vector<int> LineOf(const std::vector<Note>& notes)
   {
      std::vector<int> line;
      for (size_t i = 0; i < notes.size(); i++)
      {
         if (!line.empty() && i > 0 && notes[i].start == notes[i - 1].start)
            line.back() = std::max(line.back(), (int)notes[i].pitch);
         else
            line.push_back(notes[i].pitch);
      }
      return line;
   }

   // Overlapping notes of one pitch end where the next begins, so the roll
   // never holds two copies of a key (a held chord tone struck again).
   void Unoverlap(std::vector<Note>& notes)
   {
      SortNotes(notes);
      std::map<int, size_t> last;
      std::vector<Note> out;
      for (const Note& n : notes)
      {
         auto it = last.find(n.pitch);
         if (it != last.end())
         {
            Note& prev = out[it->second];
            if (prev.start == n.start)
            {
               prev.vel = std::max(prev.vel, n.vel);
               prev.length = std::max(prev.length, n.length);
               continue;
            }
            if (prev.End() > n.start)
               prev.length = n.start - prev.start;
         }
         last[n.pitch] = out.size();
         out.push_back(n);
      }
      notes.swap(out);
   }

   void PostPass(const GenSpec& spec, uint32_t seed, int root, int scale, std::vector<Note>& notes)
   {
      const bool drums = spec.type == Type::Drums;
      // The page's key: generation lands in it whatever the card drew.
      if (!drums && spec.key >= 0 && spec.scale >= 0)
         for (Note& n : notes)
            n.pitch = (uint8_t)std::clamp(MusicTime::SnapToScale(n.pitch, spec.key, spec.scale, MusicTime::kSnapNearest), 0, 127);
      // Range: whole octaves only, so the line keeps its shape and its key.
      // Without one, the piano's 21..108: a card's Note Echo can climb an
      // octave a repeat past anything a keyboard plays.
      const bool ranged = spec.rangeLo >= 0 && spec.rangeHi >= spec.rangeLo + 11;
      const int lo = ranged ? spec.rangeLo : 21, hi = ranged ? spec.rangeHi : 108;
      if (!drums)
         for (Note& n : notes)
         {
            int p = n.pitch;
            while (p < lo)
               p += 12;
            while (p > hi)
               p -= 12;
            n.pitch = (uint8_t)std::clamp(p, 0, 127);
         }
      // Density thins notes off the beat; the downbeats always stay.
      if (spec.density >= 0.0f && spec.density < 1.0f)
      {
         Hash h { (uint64_t)seed * 0x2545F4914F6CDD1Dull + 17 };
         std::vector<Note> kept;
         for (const Note& n : notes)
         {
            const bool beat = n.start % kPPQ == 0;
            if (beat || h.Uniform() < spec.density)
               kept.push_back(n);
         }
         notes.swap(kept);
      }
      if (spec.gate > 0.0f && !drums)
      {
         const float g = std::clamp(spec.gate, 0.1f, 1.0f);
         for (Note& n : notes)
            n.length = std::max<Tick>(1, (Tick)std::llround((double)n.length * g));
      }
      if (spec.swing > 0.0f)
      {
         const Tick late = (Tick)std::llround(std::clamp(spec.swing, 0.0f, 0.75f) * (double)kSixteenth);
         for (Note& n : notes)
            if (n.start % kSixteenth == 0 && (n.start / kSixteenth) % 2 == 1)
               n.start += late;
      }
      SortNotes(notes);
      // Locks against the notes being regenerated.
      if (!spec.source.empty() && !notes.empty() && (spec.lockRhythm || spec.lockPitch))
      {
         std::vector<Note> src = spec.source;
         SortNotes(src);
         if (spec.lockRhythm)
         {
            std::vector<Note> out = src;
            for (size_t i = 0; i < out.size(); i++)
               out[i].pitch = notes[i % notes.size()].pitch;
            notes.swap(out);
         }
         else
            for (size_t i = 0; i < notes.size(); i++)
               notes[i].pitch = src[i % src.size()].pitch;
      }
      (void)root;
      (void)scale;
      if (!drums)
         Unoverlap(notes);
   }

   // Form: A is the take, B the next seed's take; AAAB's B is a variation.
   const char* FormLetters(Form f)
   {
      switch (f)
      {
      case Form::AABA: return "AABA";
      case Form::ABAB: return "ABAB";
      case Form::AAAB: return "AAAB";
      default: return "A";
      }
   }
}

const char* TypeName(Type t)
{
   const int i = (int)t;
   return i >= 0 && i < (int)Type::Count ? kTypeNames[i] : kTypeNames[0];
}

bool TypeFromName(const std::string& name, Type& out)
{
   for (int i = 0; i < (int)Type::Count; i++)
      if (name == kTypeNames[i])
      {
         out = (Type)i;
         return true;
      }
   return false;
}

const char* FormName(Form f)
{
   const int i = (int)f;
   return i >= 0 && i < (int)Form::Count ? kFormNames[i] : kFormNames[0];
}

Take Generate(const GenSpec& specIn)
{
   GenSpec spec = specIn;
   spec.bars = std::clamp(spec.bars, 1, 8);
   Take take;
   take.seed = spec.seed;

   // The critic gate: a melody that fails the corpus floor is redrawn (the
   // Generate Samples node's redraw step), up to three times; the best one
   // judged is kept if none passes. Every other type passes when it has notes.
   std::vector<Note> best;
   float bestScore = -1.0f;
   uint32_t bestSeed = spec.seed;
   int root = 0, scale = 0;
   float bpm = 120.0f;
   for (int attempt = 0; attempt <= kMaxRedraws; attempt++)
   {
      const uint32_t seed = spec.seed + (uint32_t)attempt * kRedrawStep;
      std::vector<Note> notes;
      int r = 0, s = 0;
      float b = 120.0f;
      if (!Draw(spec, seed, notes, r, s, b, take.error))
         return take;
      PostPass(spec, seed, r, s, notes);
      take.redraws = attempt;
      float score = notes.empty() ? -1.0f : 0.0f;
      bool pass = !notes.empty();
      if (spec.type == Type::Melody && pass)
      {
         score = -1.0f;
         pass = SampleStyles::JudgeMelody(LineOf(notes), spec.key >= 0 ? spec.key : r, spec.scale >= 0 ? spec.scale : s, score);
         if (!pass)
            score = -0.5f; // judged and failed: still better than nothing
      }
      if (best.empty() || score > bestScore)
      {
         best = notes;
         bestScore = score;
         bestSeed = seed;
         root = r;
         scale = s;
         bpm = b;
      }
      if (pass)
         break;
   }
   if (best.empty())
   {
      take.error = "no notes";
      return take;
   }

   const Tick bar = Arrange::kTicksPerBar * spec.bars;
   std::vector<Note> out;
   if (spec.form == Form::None)
      out = best;
   else
   {
      // B: the next seed with the same settings, so it is the same style,
      // key and part, drawn again.
      GenSpec bSpec = spec;
      bSpec.seed = bestSeed + 1;
      bSpec.form = Form::None;
      bSpec.lockRhythm = bSpec.lockPitch = false;
      bSpec.source.clear();
      std::vector<Note> b;
      int r = 0, s = 0;
      float bb = 120.0f;
      if (Draw(bSpec, bSpec.seed, b, r, s, bb, take.error))
         PostPass(bSpec, bSpec.seed, r, s, b);
      if (b.empty())
         b = best;
      // Pitches of B follow A's key, so a card that draws a new root per seed stays in one key.
      if (spec.key < 0 && spec.type != Type::Drums)
         for (Note& n : b)
            n.pitch = (uint8_t)std::clamp(MusicTime::SnapToScale(n.pitch, root, scale, MusicTime::kSnapNearest), 0, 127);
      const char* letters = FormLetters(spec.form);
      for (int i = 0; letters[i] != 0; i++)
         for (Note n : letters[i] == 'A' ? best : b)
         {
            n.start += bar * i;
            out.push_back(n);
         }
   }
   SortNotes(out);
   uint32_t id = 1;
   for (Note& n : out)
      n.id = id++;
   take.notes = std::move(out);
   take.length = bar * (spec.form == Form::None ? 1 : 4);
   take.seed = bestSeed;
   take.criticScore = spec.type == Type::Melody ? bestScore : -1.0f;
   take.root = spec.key >= 0 ? spec.key : root;
   take.scale = spec.scale >= 0 ? spec.scale : scale;
   take.bpm = bpm;
   take.error.clear();
   return take;
}

std::string Serialize(const GenSpec& spec)
{
   const GenSpec d;
   std::ostringstream o;
   o << "type=" << TypeName(spec.type) << " style=" << spec.style << " seed=" << spec.seed << " bars=" << spec.bars;
   auto f = [&](const char* k, float v, float dv) {
      if (v != dv)
         o << " " << k << "=" << v;
   };
   auto i = [&](const char* k, int v, int dv) {
      if (v != dv)
         o << " " << k << "=" << v;
   };
   if (spec.form != Form::None)
      o << " form=" << FormName(spec.form);
   i("key", spec.key, d.key);
   i("scale", spec.scale, d.scale);
   f("c", spec.creativity, d.creativity);
   f("energy", spec.energy, d.energy);
   f("mood", spec.mood, d.mood);
   i("personal", spec.personal ? 1 : 0, 0);
   f("density", spec.density, d.density);
   f("swing", spec.swing, d.swing);
   f("gate", spec.gate, d.gate);
   i("lo", spec.rangeLo, d.rangeLo);
   i("hi", spec.rangeHi, d.rangeHi);
   i("chord", spec.chordSize, d.chordSize);
   i("arp", spec.arpMode, d.arpMode);
   i("oct", spec.arpOctaves, d.arpOctaves);
   i("role", spec.bassRole, d.bassRole);
   i("lockr", spec.lockRhythm ? 1 : 0, 0);
   i("lockp", spec.lockPitch ? 1 : 0, 0);
   return o.str();
}

bool Parse(const std::string& text, GenSpec& out)
{
   GenSpec spec;
   std::istringstream in(text);
   std::string tok;
   bool any = false;
   while (in >> tok)
   {
      const size_t eq = tok.find('=');
      if (eq == std::string::npos)
         continue;
      const std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
      const int iv = std::atoi(v.c_str());
      const float fv = (float)std::atof(v.c_str());
      any = true;
      if (k == "type")
      {
         if (!TypeFromName(v, spec.type))
            return false;
      }
      else if (k == "style") spec.style = v;
      else if (k == "seed") spec.seed = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
      else if (k == "bars") spec.bars = iv;
      else if (k == "form")
      {
         for (int f = 0; f < (int)Form::Count; f++)
            if (v == kFormNames[f])
               spec.form = (Form)f;
      }
      else if (k == "key") spec.key = iv;
      else if (k == "scale") spec.scale = iv;
      else if (k == "c") spec.creativity = fv;
      else if (k == "energy") spec.energy = fv;
      else if (k == "mood") spec.mood = fv;
      else if (k == "personal") spec.personal = iv != 0;
      else if (k == "density") spec.density = fv;
      else if (k == "swing") spec.swing = fv;
      else if (k == "gate") spec.gate = fv;
      else if (k == "lo") spec.rangeLo = iv;
      else if (k == "hi") spec.rangeHi = iv;
      else if (k == "chord") spec.chordSize = iv;
      else if (k == "arp") spec.arpMode = iv;
      else if (k == "oct") spec.arpOctaves = iv;
      else if (k == "role") spec.bassRole = iv;
      else if (k == "lockr") spec.lockRhythm = iv != 0;
      else if (k == "lockp") spec.lockPitch = iv != 0;
      // anything else: a newer setting, ignored
   }
   if (any)
      out = spec;
   return any;
}

} // namespace NoteGen
