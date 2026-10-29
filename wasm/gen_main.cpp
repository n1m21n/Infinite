// infinite_gen_wasm entry: one call renders a Generate Samples loop the way GenerateSamplesNode does.
// The redraw rule (seed + attempt * 7919, up to 3) and Master() mirror src/app/graph/GenerateSamplesNode.cpp; the parity test keeps them honest.
#include "app/graph/GenerateSamplesNode.h"  // PrerollBars
#include "app/graph/GenerateSamplesCritic.inc"
#include "app/graph/OfflineGraph.h"
#include "app/graph/SampleStyles.h"
#include "audio/MusicTime.h"
#include "core/AudioSummary.h"
#include "core/Patch.h"
#include "nodes/DrumSequencerNode.h"

#include <emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

void RegisterGenNodes();

namespace
{
   constexpr double kRate = 48000.0;
   constexpr int kMaxRedraws = 3;
   constexpr double kTargetLufs = -14.0;
   constexpr double kCeilingDb = -1.0;
   constexpr double kSilentLufs = -50.0;

   std::vector<float> gPcm; // interleaved
   std::string gLabel;
   std::string gPatch; // the graph that made the loop, as an openable .inf
   std::string gStyleId;
   float gBpm = 0.0f;
   int gAttempt = 0;
   bool gReady = false;
   // The studio panel's dots and edits, set before a run; all zero is the plain generator.
   float gTone = 0.0f, gEnergy = 0.0f, gSpace = 0.0f, gMood = 0.0f, gVariation = 0.5f, gBpmSet = 0.0f;
   bool gPinned = false; // the seed is the one a first take landed on: no redraw to another seed

   // lenient: the studio's shaped renders keep the sample the player is shaping; only silence or
   // broken audio is refused, never a timbre the taste gate would redraw.
   bool Master(int part, const float* l, const float* r, int frames, std::vector<float>& outL, std::vector<float>& outR, bool lenient = false)
   {
      AudioSummary::Analyzer an(kRate);
      an.Push(l, r, frames);
      const AudioSummary::Result s = an.Finish();
      const double rms = std::sqrt(0.5 * (s.rms[0] * s.rms[0] + s.rms[1] * s.rms[1]));
      const bool specOk = SampleCriticData::VerifyAudioSpectrum(part, s.bandPower, s.samplePeak, rms, s.silenceRatio, s.correlation);
      if (s.nonFiniteFrames != 0 || !(s.integratedLufs > kSilentLufs) || (!lenient && ((rms > 0.0 && s.samplePeak / rms < 1.2) || !specOk)))
         return false;
      double gain = std::pow(10.0, (kTargetLufs - s.integratedLufs) / 20.0);
      const double ceiling = std::pow(10.0, kCeilingDb / 20.0);
      if (s.truePeak * gain > ceiling)
         gain = ceiling / s.truePeak;
      outL.assign(l, l + frames);
      outR.assign(r, r + frames);
      for (int i = 0; i < frames; i++)
      {
         outL[i] *= (float)gain;
         outR[i] *= (float)gain;
      }
      return true;
   }

   std::string Label(const SampleStyles::Recipe& r, int root, int scale)
   {
      static const char* kRoots[12] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
      using P = SampleStyles::Part;
      std::string s = SampleStyles::PartName(r.part);
      s += " " + std::to_string(r.seed);
      if (r.part != P::Drums && r.part != P::Percussion && r.part != P::Fx && r.part != P::Foley && r.part != P::Kicks &&
          r.part != P::Snares && r.part != P::Hats && r.part != P::Toms)
         s += std::string(", ") + kRoots[((root % 12) + 12) % 12] + " " + MusicTime::ScaleTable(scale).name;
      return s;
   }
}

namespace
{
   // The compiled graph ends at a tap; a download needs credit and an Audio Out so it opens playing.
   std::string OpenablePatch(const SampleStyles::Compiled& c)
   {
      int last = 0;
      for (size_t at = c.patch.find("\nnode "); at != std::string::npos; at = c.patch.find("\nnode ", at + 1))
         last = std::max(last, atoi(c.patch.c_str() + at + 6));
      std::string out = c.patch;
      // The browser keeps the kit at /kit; on a desktop the same files live in the app's drumkits/infinite-basic folder,
      // which the drum sequencer finds again from any such path when the patch opens.
      for (size_t at = out.find("/kit/"); at != std::string::npos; at = out.find("/kit/", at + 1))
         out.replace(at, 5, "/Applications/Infinite.app/Contents/Resources/drumkits/infinite-basic/");
      const size_t nl = out.find("\n", out.find("\ntransport") == std::string::npos ? 0 : out.find("\ntransport") + 1);
      out.insert(nl == std::string::npos ? out.size() : nl + 1,
                 "# Made with the free sample generator in Infinite, by Naman Soni (n1m21n): https://infinitedevices.org/samples/\n# Free for commercial use if credited: Sample from Infinite by Naman Soni (n1m21n), infinitedevices.org.\n");
      out += "node " + std::to_string(last + 1) + " Utility Audio Out\nend\naud " + std::to_string(last + 1) + " 0 " + std::to_string(c.tap) + "\n";
      return out;
   }
}

extern "C"
{
   EMSCRIPTEN_KEEPALIVE int gen_style_count() { return SampleStyles::Count(); }

   EMSCRIPTEN_KEEPALIVE const char* gen_style_id(int i)
   {
      gStyleId = SampleStyles::At(i).id;
      return gStyleId.c_str();
   }

   // tone, energy, space, mood: -1..1. variation 0..1. bpm <= 0: the card picks.
   // pinned: render this exact seed, gated only for silence (the studio, once a take exists).
   EMSCRIPTEN_KEEPALIVE void gen_set_shape(float tone, float energy, float space, float mood, float variation, float bpm, int pinned)
   {
      gTone = tone, gEnergy = energy, gSpace = space, gMood = mood, gVariation = variation, gBpmSet = bpm, gPinned = pinned != 0;
   }

   EMSCRIPTEN_KEEPALIVE int gen_scale_count() { return (int)MusicTime::ScaleTypeList().size(); }
   EMSCRIPTEN_KEEPALIVE const char* gen_scale_name(int i) { return MusicTime::ScaleTable(i).name; }

   // Returns the frame count (0: every attempt failed). key/scale < 0: the card picks.
   static int RunImpl(int styleIdx, int part, int seed, int bars, int key, int scale);
   EMSCRIPTEN_KEEPALIVE int gen_run(int styleIdx, int part, int seed, int bars, int key, int scale)
   {
      try
      {
         return RunImpl(styleIdx, part, seed, bars, key, scale);
      }
      catch (const std::exception& e)
      {
         gLabel = std::string("exception: ") + e.what();
         return 0;
      }
   }

   static int RunImpl(int styleIdx, int part, int seed, int bars, int key, int scale)
   {
      if (!gReady)
      {
         RegisterGenNodes();
         DrumSequencerNode::SetKitDir("/kit");
         gReady = true;
      }
      SampleStyles::Recipe rec;
      rec.style = SampleStyles::At(styleIdx % SampleStyles::Count()).id;
      rec.seed = (uint32_t)std::max(0, seed);
      rec.bars = bars <= 1 ? 1 : bars <= 2 ? 2 : bars <= 4 ? 4 : 8;
      rec.key = key;
      rec.scale = scale;
      rec.part = (SampleStyles::Part)std::clamp(part, 0, (int)SampleStyles::Part::Count - 1);
      rec.personal = false;
      rec.tone = gTone, rec.energy = gEnergy, rec.space = gSpace, rec.mood = gMood;
      rec.variation = gVariation;
      rec.bpm = gBpmSet;
      gPcm.clear();
      for (int attempt = 0; attempt <= (gPinned ? 0 : kMaxRedraws); attempt++)
      {
         SampleStyles::Recipe r = rec;
         r.seed += (uint32_t)attempt * 7919u;
         const SampleStyles::Compiled c = SampleStyles::Compile(r);
         if (!c.error.empty())
            return 0;
         Patch::Data data;
         std::string err;
         app::OfflineGraph g;
         if (!Patch::ReadText(c.patch, data, err) || !g.Build(data, c.tap))
            continue;
         for (const app::OfflineGraph::Motion& m : c.motions)
            g.AddMotion(m);
         if (!g.Prepare(kRate))
            continue;
         const int bar = (int)std::lround(4.0 * 60.0 / std::max(20.0f, c.bpm) * kRate);
         const int pre = bar * GenerateSamplesNode::PrerollBars(r.bars);
         const int frames = bar * r.bars;
         std::vector<float> left, right, l, rr;
         if (!g.Render(pre + frames, left, right))
            continue;
         if (!Master((int)r.part, left.data() + pre, right.data() + pre, frames, l, rr, gPinned))
            continue;
         gPcm.resize((size_t)frames * 2);
         for (int f = 0; f < frames; f++)
         {
            gPcm[(size_t)f * 2] = l[f];
            gPcm[(size_t)f * 2 + 1] = rr[f];
         }
         gLabel = Label(rec, c.root, c.scale);
         gPatch = OpenablePatch(c);
         gBpm = c.bpm;
         gAttempt = attempt;
         return frames;
      }
      return 0;
   }

   EMSCRIPTEN_KEEPALIVE const float* gen_pcm() { return gPcm.data(); }
   EMSCRIPTEN_KEEPALIVE const char* gen_label() { return gLabel.c_str(); }
   EMSCRIPTEN_KEEPALIVE const char* gen_patch() { return gPatch.c_str(); }
   EMSCRIPTEN_KEEPALIVE float gen_bpm() { return gBpm; }
   EMSCRIPTEN_KEEPALIVE int gen_attempt() { return gAttempt; }
}
