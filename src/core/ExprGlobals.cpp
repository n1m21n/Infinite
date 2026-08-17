#include "ExprGlobals.h"

#include <cctype>

#include "Expression.h"

namespace ExprGlobals
{
namespace
{
   std::vector<Global> sGlobals;
   std::map<std::string, float> sValues;
}

std::vector<Global>& All() { return sGlobals; }

bool IsValidName(const std::string& name, std::string& outError)
{
   if (name.empty())
   {
      outError = "name is empty";
      return false;
   }
   if (isdigit((unsigned char)name[0]) != 0)
   {
      outError = "name cannot start with a digit";
      return false;
   }
   for (char c : name)
   {
      if (isalnum((unsigned char)c) == 0 && c != '_')
      {
         outError = "name may only contain letters, digits and _";
         return false;
      }
   }
   if (name == "t" || name == "pi" || name == "lo" || name == "hi")
   {
      outError = "'" + name + "' is already bound by the evaluator";
      return false;
   }
   return true;
}

void EvaluateAll(double t)
{
   sValues.clear();
   for (Global& g : sGlobals)
   {
      std::string nameError;
      if (g.name.empty() || !IsValidName(g.name, nameError))
      {
         g.error = nameError.empty() ? "name is empty" : nameError;
         continue;
      }
      if (g.expr.empty())
      {
         // An empty expression is a blank row mid-edit, not a failure - it
         // still publishes its last value so downstream parameters don't jump
         // while the field is being retyped.
         g.error.clear();
         sValues[g.name] = g.value;
         continue;
      }

      float result = 0.0f;
      std::string error;
      // `sValues` at this point holds exactly the globals declared above this
      // one, which is what makes the ordering rule in the header hold: a
      // global referring to one below it fails with "unknown identifier"
      // rather than silently reading a stale value from the previous frame.
      if (Expression::Evaluate(g.expr, t, &sValues, nullptr, result, error))
      {
         g.value = result;
         g.error.clear();
      }
      else
      {
         g.error = error;
      }
      sValues[g.name] = g.value;
   }
}

const std::map<std::string, float>& Values() { return sValues; }

const std::vector<Preset>& Presets()
{
   static const std::vector<Preset> sPresets = {
      // --- Random & Noise ---
      { "Random & Noise", "rand", "rand(0, 1, 2)", "Smooth continuous organic random wander (0..1, speed 2)" },
      { "Random & Noise", "rand_wide", "(sin(t * 1.5) + sin(t * 2.427) + sin(t * 4.077)) / 6.0 + 0.5", "Multi-frequency sine random wander (0..1)" },
      { "Random & Noise", "rand_glide", "lerp(sh(1, 0, 2), sh(1, 0, 2), smoothstep(0, 1, mod(t * 2, 1)))", "Smooth gliding random walk between targets" },
      { "Random & Noise", "sh", "sh(0, 1, 4)", "Sample & Hold stepped random jumps (rate 4 Hz)" },
      { "Random & Noise", "chaos", "abs(mod(sin(t * 1.7) * 43.12 + cos(t * 2.3) * 17.54, 1.0))", "Chaotic non-linear oscillator (0..1)" },

      // --- Rhythm & Beats (120 BPM base: t * 2 = 1 beat) ---
      { "Rhythm & Beats", "beat", "mod(t * 2, 1) < 0.5", "Square tempo pulse (120 BPM, 50% duty)" },
      { "Rhythm & Beats", "beat_saw", "mod(t * 2, 1)", "Sawtooth ramp per beat (0..1)" },
      { "Rhythm & Beats", "beat_pulse", "smoothstep(0.0, 0.04, mod(t * 2, 1)) * (1.0 - smoothstep(0.04, 0.35, mod(t * 2, 1)))", "Snappy decay envelope per beat" },
      { "Rhythm & Beats", "measure", "mod(t * 0.5, 1)", "4-beat / 1-bar saw ramp (0..1)" },
      { "Rhythm & Beats", "euclid_3_8", "mod(floor(t * 4) * 3, 8) < 3", "Euclidean 3-in-8 rhythmic trigger" },
      { "Rhythm & Beats", "triplet", "mod(t * 3, 1) < 0.5", "8th note triplet gate" },

      // --- Motion & LFOs ---
      { "Motion & LFOs", "slow_drift", "sin(t * 0.2) * 0.5 + 0.5", "Slow ambient sine wave (0..1, period ~30s)" },
      { "Motion & LFOs", "fast_lfo", "sin(t * 8) * 0.5 + 0.5", "Fast sine LFO (0..1, rate ~1.3 Hz)" },
      { "Motion & LFOs", "wobble", "sin(t * 4 + sin(t * 1.5) * 2) * 0.5 + 0.5", "Frequency-modulated wobble LFO (0..1)" },
      { "Motion & LFOs", "bounce", "abs(sin(t * 3))", "Bouncing gravity curve (0..1)" },
      { "Motion & LFOs", "strobe", "mod(t * 16, 1) < 0.2", "Rapid strobe / glitch trigger" }
   };
   return sPresets;
}

void Clear()
{
   sGlobals.clear();
   sValues.clear();
}
}
