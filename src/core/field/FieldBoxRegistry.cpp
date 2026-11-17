#include "FieldBoxRegistry.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Field
{
   namespace
   {
      std::string ToLower(const std::string& s)
      {
         std::string out = s;
         for (char& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
         return out;
      }

      bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower)
      {
         if (needleLower.empty())
            return true;
         return ToLower(haystack).find(needleLower) != std::string::npos;
      }

      PinSpec In(const char* name, ValueKind kind = ValueKind::Number, double def = 0.0)
      {
         return PinSpec{ name, kind, def };
      }

      PinSpec Out(const char* name, ValueKind kind = ValueKind::Number)
      {
         return PinSpec{ name, kind, 0.0 };
      }
   }

   const char* BoxFamilyName(BoxFamily family)
   {
      switch (family)
      {
         case BoxFamily::Sources:       return "Sources";
         case BoxFamily::Arithmetic:    return "Arithmetic";
         case BoxFamily::Range:         return "Range";
         case BoxFamily::Waves:         return "Waves";
         case BoxFamily::Logic:         return "Logic";
         case BoxFamily::TimeMemory:    return "Time & Memory";
         case BoxFamily::Random:        return "Random";
         case BoxFamily::Geometry:      return "Geometry";
         case BoxFamily::Music:         return "Music";
         case BoxFamily::InletsOutlets: return "Inlets / Outlets";
         default:                       return "Unknown";
      }
   }

   const char* ValueKindGlyph(ValueKind kind)
   {
      switch (kind)
      {
         case ValueKind::Pulse:    return "\xE2\x97\x8F"; // ●
         case ValueKind::Bool:     return "\xE2\x97\x89"; // ◉
         case ValueKind::Note:     return "\xE2\x99\xAA"; // ♪
         case ValueKind::PerPoint:
         case ValueKind::Vec3:     return "\xE2\x9C\xA6"; // ✦
         case ValueKind::Vec2:
         case ValueKind::Number:
         case ValueKind::Any:
         default:                  return "\xE2\x94\x80"; // ─
      }
   }

   const std::vector<BoxSpec>& AllBoxSpecs()
   {
      static const std::vector<BoxSpec> kSpecs = [] {
         std::vector<BoxSpec> s;
         s.reserve(96);

         // 1. Sources
         s.push_back({ "number", BoxFamily::Sources, "number", "0", { "number", "float", "const", "val" }, {}, { Out("out", ValueKind::Number) }, HostAll, false });
         s.push_back({ "knob", BoxFamily::Sources, "knob", "param", { "knob", "param", "slider" }, {}, { Out("out", ValueKind::Number) }, HostAll, false });
         s.push_back({ "time", BoxFamily::Sources, "time", "t", { "time", "t", "seconds" }, {}, { Out("t", ValueKind::Number) }, HostGeometry | HostPixel | HostAudio, false });
         s.push_back({ "dt", BoxFamily::Sources, "delta time", "dt", { "dt", "delta" }, {}, { Out("dt", ValueKind::Number) }, HostGeometry | HostPixel, false });
         s.push_back({ "age", BoxFamily::Sources, "age", "age", { "age", "frames" }, {}, { Out("age", ValueKind::Number) }, HostGeometry | HostPixel, false });
         s.push_back({ "beat", BoxFamily::Sources, "beat", "beat", { "beat", "transport" }, {}, { Out("beat", ValueKind::Number) }, HostNotes, false });
         s.push_back({ "index", BoxFamily::Sources, "index", "i", { "index", "i" }, {}, { Out("i", ValueKind::PerPoint) }, HostGeometry, false });
         s.push_back({ "count", BoxFamily::Sources, "count", "count", { "count", "total" }, {}, { Out("count", ValueKind::Number) }, HostGeometry, false });
         s.push_back({ "position", BoxFamily::Sources, "position", "P", { "position", "P", "pos" }, {}, { Out("P", ValueKind::Vec3) }, HostGeometry, false });
         s.push_back({ "normal", BoxFamily::Sources, "normal", "N", { "normal", "N" }, {}, { Out("N", ValueKind::Vec3) }, HostGeometry, false });
         s.push_back({ "color_in", BoxFamily::Sources, "colour", "Cd", { "colour", "color", "Cd" }, {}, { Out("Cd", ValueKind::Vec3) }, HostGeometry, false });
         s.push_back({ "uv", BoxFamily::Sources, "pixel position", "uv", { "pixel position", "uv" }, {}, { Out("uv", ValueKind::Vec2) }, HostGeometry | HostPixel, false });
         s.push_back({ "audio_in", BoxFamily::Sources, "audio in", "in", { "audio in", "in" }, {}, { Out("in", ValueKind::Number) }, HostAudio, false });
         s.push_back({ "sr", BoxFamily::Sources, "sample rate", "sr", { "sample rate", "sr" }, {}, { Out("sr", ValueKind::Number) }, HostAudio | HostNotes, false });
         s.push_back({ "freq", BoxFamily::Sources, "pitch", "freq", { "pitch", "freq" }, {}, { Out("freq", ValueKind::Number) }, HostAudio, false });
         s.push_back({ "gate", BoxFamily::Sources, "held", "gate", { "held", "gate" }, {}, { Out("gate", ValueKind::Bool) }, HostAudio, false });
         s.push_back({ "noteOn", BoxFamily::Sources, "note start", "noteOn", { "note start", "noteOn" }, {}, { Out("noteOn", ValueKind::Pulse) }, HostNotes | HostAudio, false });
         s.push_back({ "noteNum", BoxFamily::Sources, "note number", "noteNum", { "note number", "noteNum" }, {}, { Out("noteNum", ValueKind::Note) }, HostNotes, false });
         s.push_back({ "noteVel", BoxFamily::Sources, "velocity", "noteVel", { "velocity", "noteVel", "vel" }, {}, { Out("noteVel", ValueKind::Number) }, HostNotes | HostAudio, false });
         s.push_back({ "attrib", BoxFamily::Sources, "per point", "attrib", { "per point", "attrib" }, { In("init", ValueKind::Any, 0.0) }, { Out("val", ValueKind::PerPoint) }, HostGeometry, false });

         // 2. Arithmetic
         s.push_back({ "add", BoxFamily::Arithmetic, "+", "+", { "+", "add", "plus" }, { In("a"), In("b") }, { Out("out") }, HostAll, false });
         s.push_back({ "sub", BoxFamily::Arithmetic, "\xE2\x88\x92", "-", { "-", "sub", "minus" }, { In("a"), In("b") }, { Out("out") }, HostAll, false });
         s.push_back({ "mul", BoxFamily::Arithmetic, "\xC3\x97", "*", { "*", "mul", "times" }, { In("a"), In("b", ValueKind::Number, 1.0) }, { Out("out") }, HostAll, false });
         s.push_back({ "div", BoxFamily::Arithmetic, "\xC3\xB7", "/", { "/", "div", "divide" }, { In("a"), In("b", ValueKind::Number, 1.0) }, { Out("out") }, HostAll, false });
         s.push_back({ "neg", BoxFamily::Arithmetic, "negate", "-x", { "neg", "negate" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "mod", BoxFamily::Arithmetic, "cycle", "%", { "cycle", "mod", "%" }, { In("a"), In("b", ValueKind::Number, 1.0) }, { Out("out") }, HostAll, false });
         s.push_back({ "pow", BoxFamily::Arithmetic, "power", "pow", { "power", "pow", "^" }, { In("base"), In("exp", ValueKind::Number, 2.0) }, { Out("out") }, HostAll, false });
         s.push_back({ "abs", BoxFamily::Arithmetic, "absolute", "abs", { "absolute", "abs" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "sign", BoxFamily::Arithmetic, "sign", "sign", { "sign" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "floor", BoxFamily::Arithmetic, "round down", "floor", { "round down", "floor" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "ceil", BoxFamily::Arithmetic, "round up", "ceil", { "round up", "ceil" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "round", BoxFamily::Arithmetic, "round", "round", { "round" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "fract", BoxFamily::Arithmetic, "decimal", "fract", { "decimal", "fract" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "sqrt", BoxFamily::Arithmetic, "square root", "sqrt", { "square root", "sqrt" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "exp", BoxFamily::Arithmetic, "exp", "exp", { "exp" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "log", BoxFamily::Arithmetic, "log", "log", { "log" }, { In("x", ValueKind::Number, 1.0) }, { Out("out") }, HostAll, false });
         s.push_back({ "min", BoxFamily::Arithmetic, "min", "min", { "min" }, { In("a"), In("b") }, { Out("out") }, HostAll, false });
         s.push_back({ "max", BoxFamily::Arithmetic, "max", "max", { "max" }, { In("a"), In("b") }, { Out("out") }, HostAll, false });

         // 3. Range
         s.push_back({ "clamp", BoxFamily::Range, "limit", "clamp", { "limit", "clamp" }, { In("x"), In("lo", ValueKind::Number, 0.0), In("hi", ValueKind::Number, 1.0) }, { Out("out") }, HostAll, false });
         s.push_back({ "mix", BoxFamily::Range, "blend", "mix", { "blend", "mix", "lerp" }, { In("a"), In("b"), In("t", ValueKind::Number, 0.5) }, { Out("out") }, HostAll, false });
         s.push_back({ "smoothstep", BoxFamily::Range, "ease", "smoothstep", { "ease", "smoothstep" }, { In("edge0", ValueKind::Number, 0.0), In("edge1", ValueKind::Number, 1.0), In("x") }, { Out("out") }, HostGeometry | HostPixel, false });
         s.push_back({ "step", BoxFamily::Range, "above", "step", { "above", "step" }, { In("edge", ValueKind::Number, 0.5), In("x") }, { Out("out", ValueKind::Bool) }, HostGeometry | HostPixel, false });
         s.push_back({ "rescale", BoxFamily::Range, "rescale", "rescale", { "rescale", "map", "range" }, { In("x"), In("inMin", ValueKind::Number, 0.0), In("inMax", ValueKind::Number, 1.0), In("outMin", ValueKind::Number, 0.0), In("outMax", ValueKind::Number, 1.0) }, { Out("out") }, HostAll, true });
         s.push_back({ "wrap", BoxFamily::Range, "wrap", "wrap", { "wrap", "loop" }, { In("x"), In("lo", ValueKind::Number, 0.0), In("hi", ValueKind::Number, 1.0) }, { Out("out") }, HostAll, true });

         // 4. Waves
         s.push_back({ "sin", BoxFamily::Waves, "sine", "sin", { "sine", "sin" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "cos", BoxFamily::Waves, "cosine", "cos", { "cosine", "cos" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "tan", BoxFamily::Waves, "tan", "tan", { "tan" }, { In("x") }, { Out("out") }, HostAll, false });
         s.push_back({ "atan2", BoxFamily::Waves, "angle", "atan2", { "angle", "atan2" }, { In("y"), In("x") }, { Out("out") }, HostGeometry | HostPixel, false });

         // 5. Logic
         s.push_back({ "lt", BoxFamily::Logic, "<", "<", { "<", "lt", "less" }, { In("a"), In("b") }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "le", BoxFamily::Logic, "\xE2\x89\xA4", "<=", { "<=", "le" }, { In("a"), In("b") }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "gt", BoxFamily::Logic, ">", ">", { ">", "gt", "greater" }, { In("a"), In("b") }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "ge", BoxFamily::Logic, "\xE2\x89\xA5", ">=", { ">=", "ge" }, { In("a"), In("b") }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "eq", BoxFamily::Logic, "=", "==", { "==", "=", "eq", "equal" }, { In("a"), In("b") }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "ne", BoxFamily::Logic, "\xE2\x89\xA0", "!=", { "!=", "ne", "not equal" }, { In("a"), In("b") }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "and", BoxFamily::Logic, "both", "&&", { "both", "&&", "and" }, { In("a", ValueKind::Bool), In("b", ValueKind::Bool) }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "or", BoxFamily::Logic, "any", "||", { "any", "||", "or" }, { In("a", ValueKind::Bool), In("b", ValueKind::Bool) }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "not", BoxFamily::Logic, "flip", "!", { "flip", "!", "not" }, { In("x", ValueKind::Bool) }, { Out("out", ValueKind::Bool) }, HostAll, false });
         s.push_back({ "if", BoxFamily::Logic, "choose", "if", { "choose", "if", "select" }, { In("cond", ValueKind::Bool), In("then"), In("else") }, { Out("out") }, HostAll, false });
         s.push_back({ "xor", BoxFamily::Logic, "either", "xor", { "either", "xor", "!=" }, { In("a", ValueKind::Bool), In("b", ValueKind::Bool) }, { Out("out", ValueKind::Bool) }, HostAll, true });
         s.push_back({ "nand", BoxFamily::Logic, "not both", "nand", { "not both", "nand" }, { In("a", ValueKind::Bool), In("b", ValueKind::Bool) }, { Out("out", ValueKind::Bool) }, HostAll, true });
         s.push_back({ "nor", BoxFamily::Logic, "neither", "nor", { "neither", "nor" }, { In("a", ValueKind::Bool), In("b", ValueKind::Bool) }, { Out("out", ValueKind::Bool) }, HostAll, true });
         s.push_back({ "xnor", BoxFamily::Logic, "same", "xnor", { "same", "xnor" }, { In("a", ValueKind::Bool), In("b", ValueKind::Bool) }, { Out("out", ValueKind::Bool) }, HostAll, true });

         // 6. Time & memory
         s.push_back({ "tick", BoxFamily::TimeMemory, "every", "tick", { "every", "tick" }, { In("div", ValueKind::Number, 0.25) }, { Out("pulse", ValueKind::Pulse) }, HostNotes, false });
         s.push_back({ "previous", BoxFamily::TimeMemory, "previous", "state", { "previous", "state" }, { In("next"), In("init") }, { Out("cur") }, HostAll, false });

         // 7. Random
         s.push_back({ "rand", BoxFamily::Random, "random", "rand", { "random", "rand" }, {}, { Out("out") }, HostAll, false });
         s.push_back({ "noise", BoxFamily::Random, "smooth random", "noise", { "smooth random", "noise" }, { In("x", ValueKind::Any) }, { Out("out") }, HostGeometry | HostPixel, false });
         s.push_back({ "sh", BoxFamily::Random, "stepped random", "sh", { "stepped random", "sh" }, { In("x") }, { Out("out") }, HostAll, false });

         // 8. Geometry
         s.push_back({ "vec3", BoxFamily::Geometry, "point", "vec3", { "point", "vec3" }, { In("x"), In("y"), In("z") }, { Out("vec", ValueKind::Vec3) }, HostGeometry | HostPixel, false });
         s.push_back({ "vec2", BoxFamily::Geometry, "point 2D", "vec2", { "point 2D", "vec2" }, { In("x"), In("y") }, { Out("vec", ValueKind::Vec2) }, HostGeometry | HostPixel, false });
         s.push_back({ "swizzle", BoxFamily::Geometry, "x y z", "swizzle", { "x y z", "swizzle", "split" }, { In("v", ValueKind::Vec3) }, { Out("x"), Out("y"), Out("z") }, HostGeometry | HostPixel, false });
         s.push_back({ "length", BoxFamily::Geometry, "length", "length", { "length", "len" }, { In("v", ValueKind::Vec3) }, { Out("len") }, HostGeometry | HostPixel, false });
         s.push_back({ "normalize", BoxFamily::Geometry, "direction", "normalize", { "direction", "normalize" }, { In("v", ValueKind::Vec3) }, { Out("dir", ValueKind::Vec3) }, HostGeometry | HostPixel, false });
         s.push_back({ "distance", BoxFamily::Geometry, "distance", "distance", { "distance", "dist" }, { In("a", ValueKind::Vec3), In("b", ValueKind::Vec3) }, { Out("dist") }, HostGeometry | HostPixel, false });
         s.push_back({ "dot", BoxFamily::Geometry, "alignment", "dot", { "alignment", "dot" }, { In("a", ValueKind::Vec3), In("b", ValueKind::Vec3) }, { Out("out") }, HostGeometry | HostPixel, false });
         s.push_back({ "cross", BoxFamily::Geometry, "perpendicular", "cross", { "perpendicular", "cross" }, { In("a", ValueKind::Vec3), In("b", ValueKind::Vec3) }, { Out("out", ValueKind::Vec3) }, HostGeometry | HostPixel, false });
         s.push_back({ "neighbour", BoxFamily::Geometry, "neighbour", ".at", { "neighbour", "neighbor", ".at" }, { In("idx") }, { Out("val", ValueKind::Any) }, HostElement, false });
         s.push_back({ "rotate", BoxFamily::Geometry, "rotate", "rotate", { "rotate", "rot" }, { In("v", ValueKind::Vec3), In("angle") }, { Out("out", ValueKind::Vec3) }, HostGeometry, true });
         s.push_back({ "circle", BoxFamily::Geometry, "circle", "circle", { "circle" }, { In("angle"), In("radius", ValueKind::Number, 1.0) }, { Out("pos", ValueKind::Vec3) }, HostGeometry, true });
         s.push_back({ "spiral", BoxFamily::Geometry, "spiral", "spiral", { "spiral" }, { In("u"), In("turns", ValueKind::Number, 4.0), In("radius", ValueKind::Number, 1.0) }, { Out("pos", ValueKind::Vec3) }, HostGeometry, true });
         s.push_back({ "reduce", BoxFamily::Geometry, "reduce", "reduce", { "reduce", "reduce.max", "reduce.min", "reduce.mean", "reduce.sum" }, { In("x", ValueKind::PerPoint) }, { Out("out") }, HostElement, false });

         // 9. Music
         s.push_back({ "deg", BoxFamily::Music, "scale note", "deg", { "scale note", "deg" }, { In("degree") }, { Out("note", ValueKind::Note) }, HostNotes, false });
         s.push_back({ "note_to_hz", BoxFamily::Music, "note\xE2\x86\x92Hz", "noteToHz", { "note->Hz", "mtof" }, { In("note", ValueKind::Note) }, { Out("hz") }, HostNotes | HostAudio, true });
         s.push_back({ "hz_to_note", BoxFamily::Music, "Hz\xE2\x86\x92note", "hzToNote", { "Hz->note", "ftom" }, { In("hz") }, { Out("note", ValueKind::Note) }, HostNotes | HostAudio, true });
         s.push_back({ "octave", BoxFamily::Music, "octave", "octave", { "octave" }, { In("note", ValueKind::Note), In("oct") }, { Out("note", ValueKind::Note) }, HostNotes, true });
         s.push_back({ "euclid", BoxFamily::Music, "spread hits", "euclid", { "spread hits", "euclid" }, { In("step"), In("hits", ValueKind::Number, 5.0), In("steps", ValueKind::Number, 16.0) }, { Out("hit", ValueKind::Bool) }, HostNotes, true });

         // 10. Inlets / outlets
         s.push_back({ "play_note", BoxFamily::InletsOutlets, "play note", "note", { "play note", "note" }, { In("when", ValueKind::Pulse, 1.0), In("note", ValueKind::Note, 60.0), In("vel", ValueKind::Number, 0.8), In("len", ValueKind::Number, 0.25) }, {}, HostNotes, false });
         s.push_back({ "position_out", BoxFamily::InletsOutlets, "position", "P", { "position out", "P" }, { In("x"), In("y"), In("z"), In("xyz", ValueKind::Vec3) }, {}, HostGeometry, false });
         s.push_back({ "normal_out", BoxFamily::InletsOutlets, "normal", "N", { "normal out", "N" }, { In("xyz", ValueKind::Vec3) }, {}, HostGeometry, false });
         s.push_back({ "color_out", BoxFamily::InletsOutlets, "colour", "Cd", { "colour out", "color out", "Cd" }, { In("rgb", ValueKind::Vec3) }, {}, HostGeometry | HostPixel, false });
         s.push_back({ "value_out", BoxFamily::InletsOutlets, "value out", "publish", { "value out", "publish", "out" }, { In("val") }, {}, HostAll, false });
         s.push_back({ "pulse_out", BoxFamily::InletsOutlets, "pulse out", "publish", { "pulse out", "publish" }, { In("pulse", ValueKind::Pulse) }, {}, HostAll, false });
         s.push_back({ "code", BoxFamily::InletsOutlets, "code", "code", { "code" }, {}, {}, HostAll, false });

         return s;
      }();
      return kSpecs;
   }

   const BoxSpec* FindBoxSpec(const std::string& id)
   {
      static const std::unordered_map<std::string, const BoxSpec*> kById = [] {
         std::unordered_map<std::string, const BoxSpec*> m;
         for (const BoxSpec& spec : AllBoxSpecs())
            m[spec.id] = &spec;
         return m;
      }();
      auto it = kById.find(id);
      return it != kById.end() ? it->second : nullptr;
   }

   std::vector<const BoxSpec*> FindBoxSpecsByQuery(const std::string& query, uint32_t hostFilter)
   {
      const std::string q = ToLower(query);
      std::vector<const BoxSpec*> results;
      for (const BoxSpec& spec : AllBoxSpecs())
      {
         if (hostFilter != 0 && (spec.hostMask & hostFilter) == 0)
            continue;
         bool matched = ContainsCaseInsensitive(spec.id, q)
                     || ContainsCaseInsensitive(spec.label, q)
                     || ContainsCaseInsensitive(spec.fieldWord, q);
         if (!matched)
         {
            for (const std::string& alias : spec.aliases)
            {
               if (ContainsCaseInsensitive(alias, q))
               {
                  matched = true;
                  break;
               }
            }
         }
         if (matched)
            results.push_back(&spec);
      }
      return results;
   }
}
