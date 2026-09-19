#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

// Param roles for the prediction modulator's cold-start rungs 2b and 2d (docs/plans/prediction README §6):
// "cutoff is cutoff". A hand-kept alias table keyed by (node category, normalised name), so `freq` on a
// filter is a cutoff and `freq` on an oscillator is a pitch, and `amount` on a 2D node is nothing at all.
// Unknown names get no role. Built from the labels src/main.cpp and src/nodes actually use.
namespace ParamRoles
{
   // The physical unit a role's dwell histogram is kept in. Pooling in fader position would be wrong:
   // 0.5 is a different Hz on a 20 Hz - 20 kHz knob and a 50 Hz - 5 kHz one.
   enum class Unit : uint8_t
   {
      Pos,      // fader position 0..1 (roles without a natural unit, and size/xf.pos: see the note below)
      Log2Hz,   // log2 of a frequency in Hz
      Db,       // decibels (a linear 0..1 volume is converted; a knob whose range is already dB is used as is)
      CentsFine, // a fine-tune offset in cents, +-100 (own axis: the wide one has 75 cents per bin)
      Cents,    // pitch offset in cents (a semitone knob is multiplied by 100)
      LogSec,   // log2 of a time in seconds
      Log2Val,  // log2 of a rate-like value
      Hue       // fader position, circular
   };

   struct Range { float lo, hi; };
   inline Range UnitRange(Unit u)
   {
      switch (u)
      {
      case Unit::Log2Hz: return { 3.0f, 15.0f };
      case Unit::Db: return { -72.0f, 12.0f };
      case Unit::Cents: return { -2400.0f, 2400.0f };
      case Unit::CentsFine: return { -100.0f, 100.0f };
      case Unit::LogSec: return { -10.0f, 5.0f };
      case Unit::Log2Val: return { -8.0f, 8.0f };
      default: return { 0.0f, 1.0f };
      }
   }

   // Node categories, as registered with REGISTER_NODE, as a bit set.
   enum Cat : uint32_t
   {
      kSynths = 1u << 0, kAudioFx = 1u << 1, kNotes = 1u << 2, kMod = 1u << 3,
      k3D = 1u << 4, kComp = 1u << 5, kSource = 1u << 6, kEffects = 1u << 7, kUtility = 1u << 8, kMacros = 1u << 9,
      kAudio = kSynths | kAudioFx,
      kVisual = k3D | kComp | kSource | kEffects | kUtility,
      kAny = 0xFFFFFFFFu
   };
   inline uint32_t CatBit(std::string_view c)
   {
      if (c == "Synths") return kSynths;
      if (c == "AudioEffects") return kAudioFx;
      if (c == "Notes") return kNotes;
      if (c == "Modulators") return kMod;
      if (c == "3D") return k3D;
      if (c == "Compositing") return kComp;
      if (c == "Source") return kSource;
      if (c == "Effects") return kEffects;
      if (c == "Utility") return kUtility;
      if (c == "Macros") return kMacros;
      return 0;
   }

   struct Role
   {
      const char* role = nullptr;   // e.g. "filter.cutoff"; nullptr = no role
      const char* family = nullptr; // e.g. "filter"
      Unit unit = Unit::Pos;
      bool any() const { return role != nullptr; }
   };

   struct Row { uint32_t cats; const char* alias; const char* role; const char* family; Unit unit; };

   // Aliases are lower-case with spaces, underscores and dashes removed. `size.*` and `xf.pos` are pooled in
   // fader position rather than as a fraction of the canvas: a node's canvas size is not reachable from
   // the stats layer, and the README allows the fader-position fallback.
   inline const Row* Table(int& n)
   {
      static const Row kRows[] = {
         // filter
         { kAudio | kNotes, "cutoff", "filter.cutoff", "filter", Unit::Log2Hz },
         { kAudio, "filterfreq", "filter.cutoff", "filter", Unit::Log2Hz },
         { kAudioFx, "freq", "filter.cutoff", "filter", Unit::Log2Hz },
         { kAudioFx, "frequency", "filter.cutoff", "filter", Unit::Log2Hz },
         { kAudio, "reso", "filter.resonance", "filter", Unit::Pos },
         { kAudio, "resonance", "filter.resonance", "filter", Unit::Pos },
         { kAudio, "q", "filter.resonance", "filter", Unit::Pos },
         // pitch
         { kSynths, "freq", "pitch.freq", "pitch", Unit::Log2Hz },
         { kSynths, "frequency", "pitch.freq", "pitch", Unit::Log2Hz },
         { kAudio | kNotes, "coarse", "pitch.coarse", "pitch", Unit::Cents },
         { kAudio | kNotes, "pitch", "pitch.coarse", "pitch", Unit::Cents },
         { kAudio | kNotes, "transpose", "pitch.coarse", "pitch", Unit::Cents },
         { kAudio | kNotes, "fine", "pitch.fine", "pitch", Unit::CentsFine },
         { kAudio | kNotes, "finetune", "pitch.fine", "pitch", Unit::CentsFine },
         { kAudio | kNotes, "detune", "pitch.detune", "pitch", Unit::CentsFine },
         // level and mix
         { kAudio, "gain", "level.gain", "level", Unit::Db },
         { kAudio, "volume", "level.gain", "level", Unit::Db },
         { kAudio, "level", "level.gain", "level", Unit::Db },
         { kAudio, "output", "level.gain", "level", Unit::Db },
         { kAny & ~kMod, "mix", "mix", "mix", Unit::Pos },
         { kAny & ~kMod, "drywet", "mix", "mix", Unit::Pos },
         { kAny & ~kMod, "wet", "mix", "mix", Unit::Pos },
         { kAudioFx, "amount", "mix", "mix", Unit::Pos },
         // envelope
         { kAudio | kMod, "attack", "env.attack", "env", Unit::LogSec },
         { kAudio | kMod, "decay", "env.decay", "env", Unit::LogSec },
         { kAudio | kMod, "sustain", "env.sustain", "env", Unit::Pos },
         { kAudio | kMod, "release", "env.release", "env", Unit::LogSec },
         // visual
         { kVisual, "opacity", "opacity", "opacity", Unit::Pos },
         { kVisual, "alpha", "opacity", "opacity", Unit::Pos },
         { kVisual, "bgopacity", "opacity", "opacity", Unit::Pos },
         { kVisual, "size", "size.size", "size", Unit::Pos },
         { kVisual, "pointsize", "size.size", "size", Unit::Pos },
         { kVisual, "scale", "size.scale", "size", Unit::Pos },
         { kVisual, "scalex", "size.scale", "size", Unit::Pos },
         { kVisual, "scaley", "size.scale", "size", Unit::Pos },
         { kVisual, "scalez", "size.scale", "size", Unit::Pos },
         { kVisual, "radius", "size.radius", "size", Unit::Pos },
         { kVisual, "width", "size.extent", "size", Unit::Pos },
         { kVisual, "height", "size.extent", "size", Unit::Pos },
         { kVisual, "rotation", "xf.rotation", "transform", Unit::Pos },
         { kVisual, "angle", "xf.rotation", "transform", Unit::Pos },
         { kVisual, "spin", "xf.rotation", "transform", Unit::Pos },
         { kVisual, "x", "xf.pos", "transform", Unit::Pos },
         { kVisual, "y", "xf.pos", "transform", Unit::Pos },
         { kVisual, "z", "xf.pos", "transform", Unit::Pos },
         { kVisual, "posx", "xf.pos", "transform", Unit::Pos },
         { kVisual, "posy", "xf.pos", "transform", Unit::Pos },
         { kVisual, "hue", "colour.hue", "colour", Unit::Hue },
         { kVisual, "brightness", "colour.brightness", "colour", Unit::Pos },
         { kVisual, "saturation", "colour.saturation", "colour", Unit::Pos },
         // time
         { kAny, "rate", "time.rate", "time", Unit::Log2Val },
         { kAny, "ratebeats", "time.rate", "time", Unit::Log2Val },
         { kVisual, "speed", "time.rate", "time", Unit::Log2Val },
      };
      n = (int)(sizeof(kRows) / sizeof(kRows[0]));
      return kRows;
   }

   inline std::string Normalise(std::string_view s)
   {
      std::string o;
      for (char c : s)
         if (c != ' ' && c != '_' && c != '-' && c != '/' && c != '(' && c != ')')
            o += (char)std::tolower((unsigned char)c);
      return o;
   }

   // The role of a param, or an empty Role.
   inline Role RoleFor(std::string_view category, std::string_view name)
   {
      const uint32_t cat = CatBit(category);
      if (cat == 0)
         return {};
      const std::string n = Normalise(name);
      int rows = 0;
      const Row* t = Table(rows);
      for (int i = 0; i < rows; i++)
         if ((t[i].cats & cat) != 0 && n == t[i].alias)
            return { t[i].role, t[i].family, t[i].unit };
      return {};
   }

   // The unit a whole family can pool its dwell landscape in: the roles' shared unit, or Pos when the
   // family mixes units (pitch, filter, env, colour). `shared` is false for the mixed ones: a mixed
   // family shares only speed, never a landscape.
   inline Unit FamilyUnit(std::string_view family, bool& shared)
   {
      int rows = 0;
      const Row* t = Table(rows);
      bool found = false, mixed = false;
      Unit u = Unit::Pos;
      for (int i = 0; i < rows; i++)
      {
         if (family != t[i].family)
            continue;
         if (found && t[i].unit != u)
            mixed = true;
         u = t[i].unit;
         found = true;
      }
      shared = found && !mixed;
      return mixed || !found ? Unit::Pos : u;
   }
}
