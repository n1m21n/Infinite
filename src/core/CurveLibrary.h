#pragma once
#include "core/CurveEdit.h"
#include <cstdint>

// Main-thread, deterministic preset generation; independent of the panel and node.
namespace CurveLibrary
{
   struct Preset { const char* shelf; const char* name; };
   inline constexpr Preset presets[] = {
      {"Waves", "Sine"}, {"Waves", "Triangle"}, {"Waves", "Saw up"}, {"Waves", "Saw down"},
      {"Waves", "Square"}, {"Waves", "Pulse 25%"}, {"Waves", "Half-sine"}, {"Waves", "Exp rise"}, {"Waves", "Exp fall"},
      {"Steps & rhythm", "Gates 1/4"}, {"Steps & rhythm", "Gates 1/8"}, {"Steps & rhythm", "Gates 1/16"},
      {"Steps & rhythm", "Dotted 1/8"}, {"Steps & rhythm", "Triplet"}, {"Steps & rhythm", "Euclid"}, {"Steps & rhythm", "Trance gate"},
      {"Envelopes", "AR"}, {"Envelopes", "ADSR"}, {"Envelopes", "Swell"}, {"Envelopes", "Pluck"}, {"Envelopes", "Reverse swell"},
      {"Musical moves", "Riser"}, {"Musical moves", "Drop"}, {"Musical moves", "Sidechain pump"}, {"Musical moves", "Wobble"},
      {"Musical moves", "Filter sweep"}, {"Musical moves", "Tape stop"}, {"Musical moves", "Fade in"}, {"Musical moves", "Fade out"},
      {"Random", "Smooth random"}, {"Random", "Stepped S&H"}, {"Random", "Drift"}
   };
   inline constexpr int count = sizeof(presets) / sizeof(presets[0]);
   inline float Random(uint32_t seed, unsigned i)
   {
      uint32_t v = seed + i * 0x9e3779b9u;
      v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
      return float(v >> 8) / 16777215.0f;
   }
   inline CurveEdit::Points Make(int id, uint32_t seed = 1, int pulses = 5, int steps = 8)
   {
      using namespace CurveEdit;
      Points out;
      auto add = [&](float x, float y, int interp = ModShapes::kInterpLinear) {
         CurveEdit::Point p; p.x = x; p.y = std::clamp(y, 0.0f, 1.0f); p.interp = (uint8_t)interp; out.push_back(p);
      };
      if (id == 1) { add(0,0); add(.5f,1); add(1,0); return out; }
      if (id == 2 || id == 21 || id == 27) { add(0,0); add(1,1); return out; }
      if (id == 3 || id == 22 || id == 28) { add(0,1); add(1,0); return out; }
      if (id == 4 || id == 5) { add(0,1,0); add(id == 4 ? .5f : .25f,0,0); add(1,0,0); return out; }
      if (id >= 9 && id <= 15)
      {
         const int n = id == 9 ? 16 : id == 10 ? 32 : id == 11 ? 64 : id == 12 ? 22 : id == 13 ? 48 : id == 14 ? std::clamp(steps, 1, 64) : 16;
         for (int i = 0; i < n; ++i)
         {
            const bool on = id == 14 ? (i * std::clamp(pulses, 0, n)) % n < std::clamp(pulses, 0, n) : id == 15 ? ((0xb6adu >> (i % 16)) & 1) != 0 : true;
            add(float(i)/n, on ? 1.f : 0.f, 0);
            add((float(i)+.5f)/n, 0.f, 0);
         }
         add(1,0,0); return out;
      }
      if (id == 16) { add(0,0); add(.2f,1); add(1,0); return out; }
      if (id == 17) { add(0,0); add(.1f,1); add(.3f,.6f); add(.75f,.6f); add(1,0); return out; }
      if (id == 30) { for (int i=0;i<16;++i) add(float(i)/16,Random(seed,i),0); add(1,out.back().y,0); return out; }
      for (int i = 0; i <= 128; ++i)
      {
         const float x = float(i)/128.f;
         float y = x;
         switch(id)
         {
         case 0: y=.5f-.5f*std::cos(x*6.283185307f); break;
         case 6: case 18: y=std::sin(x*3.141592654f); break;
         case 7: y=std::expm1(5*x)/std::expm1(5.f); break;
         case 8: y=1-std::expm1(5*x)/std::expm1(5.f); break;
         case 19: y=x<.02f ? x/.02f : std::exp(-7*(x-.02f)); break;
         case 20: y=x<.95f ? std::pow(x/.95f,3.f) : (1-x)/.05f; break;
         case 23: y=1-std::exp(-6*x); break;
         case 24: y=.5f-.5f*std::cos(x*25.13274123f); break;
         case 25: y=x*x*(3-2*x); break;
         case 26: y=std::pow(1-x,3.f); break;
         case 29: case 31:
         {
            const float phase=x*(id==29 ? 8.f : 3.f); const unsigned k=(unsigned)phase;
            const float t=phase-k, u=t*t*(3-2*t);
            y=Random(seed,k)*(1-u)+Random(seed,k+1)*u;
            if(id==31) y=.25f+.5f*y;
            break;
         }
         default: break;
         }
         add(x,y);
      }
      return out;
   }
}
