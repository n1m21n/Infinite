// DSP self-test fixtures, part 1: gain..wavetable shaper (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
// ============================================================ INFINITE_DSPTEST
//
// Headless audio-engine smoke test: no GL/GLFW window, no real device - just
// AudioEngine::ProcessOffline() over a scratch buffer. Gated as an early exit
// before glfwInit() (see main() below) because it needs none of the GL/ImGui
// setup every other INFINITE_* fixture runs inside; this keeps it fast and
// honest about being a pure-DSP test, not a rendering test that happens to
// also touch audio.
//
// Hardcoded node chains (not real INode types - that's P2/P3). Two fixtures:
//   1) SineOscNode -> GainNode: session 1's original coverage (peak, zero-
//      crossing frequency, gain smoothing, meter ring).
//   2) SineOscNode -> SvfFilterNode: the plan's actual P1 exit criterion,
//      "osc->filter->out" - a TPT SVF lowpass driven through ParamMailbox,
//      swept below the 440 Hz tone mid-run to prove it actually filters
//      (a gain-only chain has no way to exercise this).
namespace DspTest
{
   class SineOscNode : public AudioNode
   {
   public:
      void PrepareToPlay(double sampleRate, int maxBlockSize) override
      {
         mPhaseStep = 2.0 * M_PI * 440.0 / sampleRate;
      }

      void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
      {
         for (int ch = 0; ch < buffer.numChannels; ++ch)
         {
            double phase = mPhase;
            for (int i = 0; i < buffer.numFrames; ++i)
            {
               buffer.channels[ch][i] = (float)sin(phase);
               phase += mPhaseStep;
            }
         }
         mPhase = fmod(mPhase + mPhaseStep * buffer.numFrames, 2.0 * M_PI);
      }

   private:
      double mPhase = 0.0;
      double mPhaseStep = 0.0;
   };

   class GainNode : public AudioNode
   {
   public:
      static constexpr int kGainParam = 0;

      void SetInitialGain(float g) { mInitialGain = g; }
      void PushGain(float g) { mMailbox.Push(kGainParam, g); }
      MeterRing& Meter() { return mMeter; }

      void PrepareToPlay(double sampleRate, int maxBlockSize) override
      {
         mMailbox.PrepareToPlay(sampleRate);
         mMailbox.SetImmediate(kGainParam, mInitialGain);
      }

      void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
      {
         const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
         float peak = 0.0f;
         for (int ch = 0; ch < buffer.numChannels; ++ch)
         {
            for (int i = 0; i < buffer.numFrames; ++i)
            {
               const float g = mMailbox.SmoothedValue(kGainParam);
               const float s = (in != nullptr) ? in->channels[ch][i] : 0.0f;
               const float v = s * g;
               buffer.channels[ch][i] = v;
               peak = std::max(peak, std::fabs(v));
            }
         }
         mMeter.Write(&peak, 1);
      }

   private:
      float mInitialGain = 1.0f;
      ParamMailbox mMailbox;
      MeterRing mMeter;
   };

   class SvfFilterNode : public AudioNode
   {
   public:
      static constexpr int kCutoffParam = 0;

      void SetInitialCutoff(float hz) { mInitialCutoff = hz; }
      void PushCutoff(float hz) { mMailbox.Push(kCutoffParam, hz); }
      MeterRing& Meter() { return mMeter; }

      void PrepareToPlay(double sampleRate, int maxBlockSize) override
      {
         for (auto& svf : mSvf)
         {
            svf.SetSampleRate(sampleRate);
            svf.Reset();
         }
         mMailbox.PrepareToPlay(sampleRate);
         mMailbox.SetImmediate(kCutoffParam, mInitialCutoff);
      }

      void Reset() override
      {
         for (auto& svf : mSvf)
            svf.Reset();
      }

      void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
      {
         const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
         float peak = 0.0f;
         const int numChannels = std::min(buffer.numChannels, kMaxChannels);
         for (int ch = 0; ch < numChannels; ++ch)
         {
            for (int i = 0; i < buffer.numFrames; ++i)
            {
               const float cutoff = mMailbox.SmoothedValue(kCutoffParam);
               mSvf[ch].SetCutoff(cutoff, 0.707f);
               const float s = (in != nullptr) ? in->channels[ch][i] : 0.0f;
               const float v = mSvf[ch].Process(s).low;
               buffer.channels[ch][i] = v;
               peak = std::max(peak, std::fabs(v));
            }
         }
         mMeter.Write(&peak, 1);
      }

   private:
      static constexpr int kMaxChannels = 8;
      float mInitialCutoff = 5000.0f;
      DspMath::TptSvf mSvf[kMaxChannels];
      ParamMailbox mMailbox;
      MeterRing mMeter;
   };

   // Test-only convenience: a strictly linear chain (each node's one input,
   // if any, is the previous node's output), expressed as the same
   // AudioTopology/AudioTopologyEntry the real editor-graph walker
   // (main.cpp's RebuildAudioTopology) builds - see
   // docs/plans/audio/audio-graph-semantics.md §3. Every node here is
   // treated as 0-or-1-input; that covers every DspTest fixture chain.
   AudioTopology BuildLinearTopology(std::vector<AudioNode*> chain)
   {
      AudioTopology topo;
      for (size_t i = 0; i < chain.size(); i++)
      {
         AudioTopologyEntry entry;
         entry.node = chain[i];
         if (i > 0)
         {
            entry.numInputs = 1;
            entry.inputBufferIndices[0] = (int)i - 1;
         }
         entry.outputBufferIndex = (int)i;
         topo.order.push_back(entry);
      }
      topo.numBuffers = (int)chain.size();
      if (!chain.empty())
         topo.terminalBufferIndices.push_back({ (int)chain.size() - 1, nullptr });
      return topo;
   }
}

bool RunGainFixture()
{
   using namespace DspTest;

   const double kSampleRate = 48000.0;
   const int kNumFrames = 512;
   const int kNumChannels = 2;
   const int kNumBlocks = 12; // ~128 ms total, well over a few 440 Hz cycles
   const int kChangeBlock = 10; // push a gain change right before this block

   SineOscNode osc;
   DspTest::GainNode gain; // qualified: ::GainNode (nodes/AudioNodes.h) now also in scope
   osc.PrepareToPlay(kSampleRate, kNumFrames);
   gain.SetInitialGain(0.5f);
   gain.PrepareToPlay(kSampleRate, kNumFrames);

   AudioEngine::Instance().SetTopology(DspTest::BuildLinearTopology({ &osc, &gain }));

   std::vector<float> chan0(kNumFrames), chan1(kNumFrames);
   float* chans[kNumChannels] = { chan0.data(), chan1.data() };
   AudioBuffer buffer;
   buffer.channels = chans;
   buffer.numChannels = kNumChannels;
   buffer.numFrames = kNumFrames;

   std::vector<float> allSamples; // channel 0, concatenated across every block
   allSamples.reserve((size_t)kNumFrames * kNumBlocks);

   float peakSteady = 0.0f;
   float lastSampleBeforeChange = 0.0f;
   float firstSampleAfterChange = 0.0f;

   bool all = true;

   for (int b = 0; b < kNumBlocks; ++b)
   {
      if (b == kChangeBlock)
         gain.PushGain(0.9f);

      AudioEngine::Instance().ProcessOffline(buffer);

      for (int i = 0; i < kNumFrames; ++i)
         allSamples.push_back(chan0[i]);

      if (b == 5)
      {
         for (int i = 0; i < kNumFrames; ++i)
            peakSteady = std::max(peakSteady, std::fabs(chan0[i]));
      }
      if (b == kChangeBlock - 1)
         lastSampleBeforeChange = chan0[kNumFrames - 1];
      if (b == kChangeBlock)
         firstSampleAfterChange = chan0[0];
   }

   // 1) Peak amplitude at steady state matches the configured gain (0.5).
   const bool peakOk = std::fabs(peakSteady - 0.5f) < 0.01f;
   printf("DSPTEST peak amplitude: expected 0.500  got %.4f  %s\n", peakSteady, peakOk ? "OK" : "FAIL");
   all &= peakOk;

   // 2) Zero-crossing rate over the pre-change samples matches 440 Hz.
   {
      const int numSamples = kChangeBlock * kNumFrames;
      int crossings = 0;
      for (int i = 1; i < numSamples; ++i)
      {
         if ((allSamples[i - 1] < 0.0f) != (allSamples[i] < 0.0f))
            crossings++;
      }
      const double totalTimeSec = (double)numSamples / kSampleRate;
      const double freqHz = crossings / 2.0 / totalTimeSec;
      const bool freqOk = std::fabs(freqHz - 440.0) < 5.0;
      printf("DSPTEST zero-crossing freq: expected 440.0 Hz  got %.2f Hz  %s\n", freqHz, freqOk ? "OK" : "FAIL");
      all &= freqOk;
   }

   // 3) Mid-run gain change ramps smoothly - the first sample of the block
   //    right after the change should differ only slightly from the last
   //    sample before it, not jump instantly toward the new gain.
   {
      const float delta = std::fabs(firstSampleAfterChange - lastSampleBeforeChange);
      const bool smoothOk = delta < 0.1f;
      printf("DSPTEST gain smoothing: |delta| = %.4f (instant jump would be ~0.4)  %s\n",
             delta, smoothOk ? "OK" : "FAIL");
      all &= smoothOk;
   }

   // 4) MeterRing: one peak value written per ProcessBlock call, drained here.
   {
      float meterValues[64] = {};
      const int meterCount = gain.Meter().Read(meterValues, 64);
      bool shapeOk = meterCount == kNumBlocks;
      for (int i = 0; i < meterCount; ++i)
         shapeOk &= (meterValues[i] >= 0.0f && meterValues[i] <= 1.0f);
      printf("DSPTEST meter ring: expected %d entries  got %d  %s\n", kNumBlocks, meterCount,
             shapeOk ? "OK" : "FAIL");
      all &= shapeOk;
   }

   return all;
}

bool RunFilterFixture()
{
   using namespace DspTest;

   const double kSampleRate = 48000.0;
   const int kNumFrames = 512;
   const int kNumChannels = 2;
   const int kNumBlocks = 40;
   const int kSweepBlock = 20; // drop cutoff below the 440 Hz tone right before this block

   SineOscNode osc;
   SvfFilterNode filter;
   osc.PrepareToPlay(kSampleRate, kNumFrames);
   filter.SetInitialCutoff(8000.0f); // well above 440 Hz: near-unattenuated passband
   filter.PrepareToPlay(kSampleRate, kNumFrames);

   AudioEngine::Instance().SetTopology(DspTest::BuildLinearTopology({ &osc, &filter }));

   std::vector<float> chan0(kNumFrames), chan1(kNumFrames);
   float* chans[kNumChannels] = { chan0.data(), chan1.data() };
   AudioBuffer buffer;
   buffer.channels = chans;
   buffer.numChannels = kNumChannels;
   buffer.numFrames = kNumFrames;

   float peakBeforeSweep = 0.0f;
   float peakAfterSweep = 0.0f;

   bool all = true;

   for (int b = 0; b < kNumBlocks; ++b)
   {
      if (b == kSweepBlock)
         filter.PushCutoff(120.0f); // well below 440 Hz: heavy attenuation

      AudioEngine::Instance().ProcessOffline(buffer);

      if (b == kSweepBlock - 5)
      {
         for (int i = 0; i < kNumFrames; ++i)
            peakBeforeSweep = std::max(peakBeforeSweep, std::fabs(chan0[i]));
      }
      if (b == kNumBlocks - 1) // smoothing + filter settle well after the sweep
      {
         for (int i = 0; i < kNumFrames; ++i)
            peakAfterSweep = std::max(peakAfterSweep, std::fabs(chan0[i]));
      }
   }

   // 1) Below-cutoff sweep actually attenuates the 440 Hz tone - a gain-only
   //    chain has no way to exercise this, it's the point of the filter.
   {
      const bool passbandOk = peakBeforeSweep > 0.7f; // near-unattenuated at 8 kHz cutoff
      const bool attenuatedOk = peakAfterSweep < 0.3f; // well attenuated at 120 Hz cutoff
      printf("DSPTEST filter passband: expected >0.700  got %.4f  %s\n", peakBeforeSweep,
             passbandOk ? "OK" : "FAIL");
      printf("DSPTEST filter sweep attenuation: expected <0.300  got %.4f  %s\n", peakAfterSweep,
             attenuatedOk ? "OK" : "FAIL");
      all &= passbandOk;
      all &= attenuatedOk;
   }

   // 2) MeterRing: one peak value written per ProcessBlock call, drained here.
   {
      float meterValues[64] = {};
      const int meterCount = filter.Meter().Read(meterValues, 64);
      bool shapeOk = meterCount == kNumBlocks;
      for (int i = 0; i < meterCount; ++i)
         shapeOk &= (meterValues[i] >= 0.0f && meterValues[i] <= 1.0f);
      printf("DSPTEST filter meter ring: expected %d entries  got %d  %s\n", kNumBlocks, meterCount,
             shapeOk ? "OK" : "FAIL");
      all &= shapeOk;
   }

   return all;
}

// Coverage for the P2.7 PolyBlepOsc extension (Generate/Advance split,
// triangle, pulse) - direct against DspMath::PolyBlepOsc, not through a
// full WavetableNode/AudioEngine chain, matching how DspTest's other
// fixtures exercise ParamMailbox/TptSvf directly rather than via a node.
bool RunOscWaveformFixture()
{
   const double kSampleRate = 48000.0;
   const double kFreq = 440.0;
   const int kNumSamples = 48000; // 1 second - plenty of cycles for a stable rate

   bool all = true;

   // Triangle: naive (phase-only) formula, no BLEP - see DspMath.h's comment
   // on why. Peak should still sit at +/-1 and the zero-crossing rate should
   // still track the set frequency exactly, same invariants Saw/Square
   // already prove for the BLEP-corrected waveforms.
   {
      DspMath::PolyBlepOsc osc;
      osc.SetFrequency(kFreq, kSampleRate);
      float peak = 0.0f;
      int crossings = 0;
      float prev = 0.0f;
      for (int i = 0; i < kNumSamples; i++)
      {
         const float s = osc.Generate(DspMath::kWaveTriangle);
         osc.Advance();
         peak = std::max(peak, std::fabs(s));
         if (i > 0 && (prev < 0.0f) != (s < 0.0f))
            crossings++;
         prev = s;
      }
      const double freqHz = crossings / 2.0 / (kNumSamples / kSampleRate);
      const bool peakOk = std::fabs(peak - 1.0f) < 0.02f;
      const bool freqOk = std::fabs(freqHz - kFreq) < 2.0;
      printf("DSPTEST triangle peak amplitude: expected 1.000  got %.4f  %s\n", peak,
             peakOk ? "OK" : "FAIL");
      printf("DSPTEST triangle zero-crossing freq: expected %.1f Hz  got %.2f Hz  %s\n",
             kFreq, freqHz, freqOk ? "OK" : "FAIL");
      all &= peakOk;
      all &= freqOk;
   }

   // Pulse at a 25% duty cycle: peak still +/-1 (BLEP-corrected edges
   // overshoot only momentarily), and the *positive*-phase fraction should
   // land near 0.25, proving pulseWidth actually reshapes the wave rather
   // than just aliasing Square's fixed 50%.
   {
      DspMath::PolyBlepOsc osc;
      osc.SetFrequency(kFreq, kSampleRate);
      float peak = 0.0f;
      int positiveCount = 0;
      for (int i = 0; i < kNumSamples; i++)
      {
         const float s = osc.Generate(DspMath::kWavePulse, 0.25f);
         osc.Advance();
         peak = std::max(peak, std::fabs(s));
         if (s > 0.0f)
            positiveCount++;
      }
      const double dutyMeasured = (double)positiveCount / (double)kNumSamples;
      const bool peakOk = peak > 0.95f && peak < 1.3f; // BLEP correction overshoots slightly at the edge
      const bool dutyOk = std::fabs(dutyMeasured - 0.25) < 0.02;
      printf("DSPTEST pulse peak amplitude: expected ~1.0  got %.4f  %s\n", peak, peakOk ? "OK" : "FAIL");
      printf("DSPTEST pulse duty cycle: expected 0.250  got %.4f  %s\n", dutyMeasured,
             dutyOk ? "OK" : "FAIL");
      all &= peakOk;
      all &= dutyOk;
   }

   return all;
}

// Wavetable coverage. Replaces the P3a note-scheduling fixture, whose subject
// (the Note Sequencer's beat-quantised step scheduling) no longer exists -
// live MIDI is the note source now, and its timing is CoreMIDI's rather than
// anything this codebase computes. What is worth asserting instead is the two
// things the wavetable engine can silently get wrong: the bank's mip pyramid
// (a broken one aliases, which is inaudible in a unit test but obvious as a
// spectral check) and the node's note path (a stuck or never-released voice).
bool RunWavetableFixture()
{
   bool all = true;
   const double sampleRate = 48000.0;
   const int numFrames = 256;

   Wavetable::EnsureBuilt();

   // 1. Every mip level must be band-limited to its own harmonic ceiling.
   //    Checked by correlating the frame against harmonics above that ceiling:
   //    a correctly built level has essentially no energy there.
   {
      bool bandLimitOk = true;
      for (int table = 0; table < Wavetable::NumTables() && bandLimitOk; table++)
      {
         // Level 4 holds harmonics 1..32; harmonic 40 must be absent from it.
         const int level = 4;
         const int ceiling = Wavetable::kMaxHarmonic >> level;
         const float* frame = Wavetable::Frame(table, Wavetable::kFrames - 1, level);
         double re = 0.0, im = 0.0, energy = 0.0;
         const int probe = ceiling + 8;
         for (int i = 0; i < Wavetable::kFrameSize; i++)
         {
            const double ph = 2.0 * M_PI * (double)probe * (double)i / (double)Wavetable::kFrameSize;
            re += frame[i] * cos(ph);
            im += frame[i] * sin(ph);
            energy += (double)frame[i] * frame[i];
         }
         const double aboveCeiling = sqrt(re * re + im * im) / (double)Wavetable::kFrameSize;
         if (aboveCeiling > 1e-4 && energy > 1e-6)
            bandLimitOk = false;
      }
      printf("DSPTEST wavetable band limit: harmonics above each mip's ceiling absent  %s\n",
             bandLimitOk ? "OK" : "FAIL");
      all &= bandLimitOk;
   }

   // 2. Mip selection must actually climb as pitch rises, or the pyramid is
   //    built but never used - the failure mode that looks fine until a high
   //    note screams.
   {
      const int lowMip = Wavetable::MipForPhaseInc(55.0 / sampleRate);
      const int highMip = Wavetable::MipForPhaseInc(4186.0 / sampleRate);
      const bool mipOk = highMip > lowMip && lowMip >= 0 && highMip < Wavetable::kMipLevels;
      printf("DSPTEST wavetable mip select: A1 -> level %d, C8 -> level %d  %s\n", lowMip, highMip,
             mipOk ? "OK" : "FAIL");
      all &= mipOk;
   }

   // 3. The node's note path: a note-on sounds, and a note-off actually
   //    releases the voice rather than leaving it stuck.
   {
      WavetableNode wt;
      wt.engines[0].on = true;
      wt.engines[0].ampAttack = 1.0f;
      wt.engines[0].ampDecay = 20.0f;
      wt.engines[0].ampSustain = 0.8f;
      wt.engines[0].ampRelease = 20.0f;
      wt.engines[1].on = false;
      wt.volume = 1.0f;

      AudioNode* audio = wt.GetAudioNode();
      audio->PrepareToPlay(sampleRate, numFrames);
      wt.CookIfNeeded(1);

      NoteEventQueue inbox;
      audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());

      std::vector<float> chan0(numFrames), chan1(numFrames);
      float* chans[2] = { chan0.data(), chan1.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = numFrames;

      auto RunBlocks = [&](int blocks) -> float
      {
         float peak = 0.0f;
         for (int b = 0; b < blocks; b++)
         {
            audio->ProcessBlock(nullptr, 0, buf);
            for (int i = 0; i < numFrames; i++)
               peak = std::max(peak, std::fabs(chan0[i]));
         }
         return peak;
      };

      const float silentBefore = RunBlocks(2);

      NoteEvent on;
      on.note = 69; // A4
      on.velocity = 1.0f;
      on.isNoteOn = true;
      on.frameOffset = 0;
      inbox.Push(on);
      const float soundingPeak = RunBlocks(8);

      NoteEvent off = on;
      off.isNoteOn = false;
      off.velocity = 0.0f;
      inbox.Push(off);
      RunBlocks(12); // well past the 20ms release
      const float afterRelease = RunBlocks(4);

      const bool noteOk = silentBefore < 1e-5f && soundingPeak > 0.05f && afterRelease < 1e-4f;
      printf("DSPTEST wavetable notes: silent=%.6f sounding=%.4f released=%.6f  %s\n", silentBefore,
             soundingPeak, afterRelease, noteOk ? "OK" : "FAIL");
      all &= noteOk;
   }

   // Free-running render of one configured engine, used by the warp and
   // filter checks below. A fresh node per call, so no phase, filter or
   // smoothing state carries between configurations - which is what makes
   // "amount 0 is bit-identical to off" a meaningful comparison rather than a
   // measurement of whatever the previous run left behind.
   auto RenderFree = [&](const std::function<void(WavetableNode&)>& configure,
                         std::vector<float>& out) {
      WavetableNode wt;
      wt.volume = 1.0f;
      wt.frequency = 220.0f;
      wt.glide = 0.0f;
      wt.engines[0].on = true;
      wt.engines[0].table = 0;
      wt.engines[0].position = 0.5f;
      wt.engines[0].volume = 1.0f;
      wt.engines[1].on = false;
      configure(wt);

      AudioNode* audio = wt.GetAudioNode();
      // Cook (which pushes params) *before* PrepareToPlay, so the mailbox
      // starts at the configured values instead of smoothing toward them - a
      // ramp would make two runs differ for the first few thousand samples for
      // reasons that have nothing to do with what is being tested.
      wt.CookIfNeeded(1);
      audio->PrepareToPlay(sampleRate, numFrames);

      std::vector<float> l(numFrames), r(numFrames);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = numFrames;

      out.clear();
      for (int b = 0; b < 16; b++)
      {
         audio->ProcessBlock(nullptr, 0, buf);
         out.insert(out.end(), l.begin(), l.end());
      }
   };

   auto Rms = [](const std::vector<float>& v) {
      double sum = 0.0;
      for (float x : v)
         sum += (double)x * x;
      return v.empty() ? 0.0 : sqrt(sum / (double)v.size());
   };

   // 4. Every warp mode must be finite and bounded at full depth, and every
   //    warp mode at zero depth must be bit-identical to "off". The second
   //    half is the contract WarpReadPhase/WarpSample's `amount <= 0`
   //    early-out exists to keep: without it, selecting a mode changes the
   //    sound before its depth knob has been touched.
   {
      std::vector<float> dry;
      RenderFree([](WavetableNode& wt) { wt.engines[0].warpMode = SynthModes::kWarpOff; }, dry);

      bool boundedOk = true, neutralOk = true;
      int firstBadMode = -1, firstNonNeutral = -1;
      for (int mode = 0; mode < SynthModes::kNumWarpModes; mode++)
      {
         std::vector<float> deep;
         RenderFree([mode](WavetableNode& wt) {
            wt.engines[0].warpMode = mode;
            wt.engines[0].warpAmount = 0.85f;
            wt.engines[0].warpRatio = 3.0f;
         }, deep);
         for (float x : deep)
         {
            if (!std::isfinite(x) || std::fabs(x) > 4.0f)
            {
               boundedOk = false;
               if (firstBadMode < 0)
                  firstBadMode = mode;
               break;
            }
         }

         std::vector<float> zero;
         RenderFree([mode](WavetableNode& wt) {
            wt.engines[0].warpMode = mode;
            wt.engines[0].warpAmount = 0.0f;
         }, zero);
         if (zero != dry)
         {
            neutralOk = false;
            if (firstNonNeutral < 0)
               firstNonNeutral = mode;
         }
      }

      printf("DSPTEST wavetable warp bounded: all %d modes finite and |x|<=4  %s\n",
             SynthModes::kNumWarpModes, boundedOk ? "OK" : "FAIL");
      if (!boundedOk)
         printf("DSPTEST   first offending mode: %s\n", SynthModes::WarpName(firstBadMode));
      printf("DSPTEST wavetable warp neutral at zero depth: %s\n", neutralOk ? "OK" : "FAIL");
      if (!neutralOk)
         printf("DSPTEST   first non-neutral mode: %s\n", SynthModes::WarpName(firstNonNeutral));
      all &= boundedOk && neutralOk;
   }

   // 5. The per-engine filter: a lowpass an octave and a half below the
   //    fundamental must remove most of the signal, a highpass well above it
   //    must remove most of the signal, and the steeper slope must remove more
   //    than the shallower one - which is the check that catches a cascade
   //    that is wired up but only ever runs its first stage.
   {
      std::vector<float> open, lp12, lp24, hp;
      RenderFree([](WavetableNode& wt) { wt.engines[0].filterType = SynthModes::kFilterOff; }, open);
      RenderFree([](WavetableNode& wt) {
         wt.engines[0].filterType = SynthModes::kFilterLP12;
         wt.engines[0].cutoff = 80.0f;
         wt.engines[0].resonance = 0.0f;
      }, lp12);
      RenderFree([](WavetableNode& wt) {
         wt.engines[0].filterType = SynthModes::kFilterLP24;
         wt.engines[0].cutoff = 80.0f;
         wt.engines[0].resonance = 0.0f;
      }, lp24);
      RenderFree([](WavetableNode& wt) {
         wt.engines[0].filterType = SynthModes::kFilterHP24;
         wt.engines[0].cutoff = 8000.0f;
         wt.engines[0].resonance = 0.0f;
      }, hp);

      const double openRms = Rms(open);
      const double lp12Rms = Rms(lp12);
      const double lp24Rms = Rms(lp24);
      const double hpRms = Rms(hp);
      const bool filterOk = openRms > 0.01 && lp12Rms < openRms * 0.5 && lp24Rms < lp12Rms &&
                            hpRms < openRms * 0.2;
      printf("DSPTEST wavetable filter: open=%.4f lp12=%.4f lp24=%.4f hp24=%.4f  %s\n", openRms,
             lp12Rms, lp24Rms, hpRms, filterOk ? "OK" : "FAIL");
      all &= filterOk;
   }


   // 5. Polyphony normalisation must be continuous. `active` is an integer
   //    recounted every sample, so applying 1/sqrt(active) directly made any
   //    overlapping note-on step the gain on the *already sounding* voice
   //    (1 -> 0.7071 = -3 dB in one sample). The new note's attack cannot mask
   //    that, because the discontinuity is on the old voice - so the check is
   //    a sample-to-sample delta scan across the second note's onset, compared
   //    against the same waveform's own steady-state slew.
   {
      WavetableNode wt;
      wt.volume = 1.0f;
      wt.mix = 0.0f;
      wt.glide = 0.0f;
      wt.engines[0].on = true;
      wt.engines[0].table = 0;
      wt.engines[0].position = 0.0f; // sine end of "Basic Shapes"
      wt.engines[0].unison = 1;
      wt.engines[0].phaseRandomize = 0.0f;
      wt.engines[0].warpMode = SynthModes::kWarpOff;
      wt.engines[0].warpAmount = 0.0f;
      wt.engines[0].filterType = SynthModes::kFilterOff;
      wt.engines[0].ampAttack = 4.0f;
      wt.engines[0].ampDecay = 1.0f;
      wt.engines[0].ampSustain = 1.0f;
      wt.engines[0].ampRelease = 4000.0f; // long enough that note 1's tail is
                                          // still loud when note 2 arrives
      wt.engines[1].on = false;

      AudioNode* audio = wt.GetAudioNode();
      // Cook first, then prepare: PrepareToPlay latches every mailbox value
      // immediately, so nothing is still ramping when the first note lands.
      wt.CookIfNeeded(1);
      audio->PrepareToPlay(sampleRate, numFrames);

      NoteEventQueue inbox;
      audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());

      std::vector<float> chan0(numFrames), chan1(numFrames);
      float* chans[2] = { chan0.data(), chan1.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = numFrames;

      std::vector<float> stream;
      auto RunBlocks = [&](int blocks) {
         for (int b = 0; b < blocks; b++)
         {
            audio->ProcessBlock(nullptr, 0, buf);
            stream.insert(stream.end(), chan0.begin(), chan0.end());
         }
      };

      NoteEvent on1;
      on1.note = 69; // A4
      on1.velocity = 1.0f;
      on1.isNoteOn = true;
      on1.voiceId = 101;
      inbox.Push(on1);
      RunBlocks(38); // ~200 ms

      NoteEvent off1 = on1;
      off1.isNoteOn = false;
      off1.velocity = 0.0f;
      inbox.Push(off1);
      RunBlocks(4); // into the (very long) release, tail still near full level

      const int boundary = (int)stream.size();
      NoteEvent on2 = on1;
      on2.note = 64; // E4, a second voice - note 1's tail is still sounding
      on2.voiceId = 102;
      inbox.Push(on2);
      RunBlocks(20);

      // The delta of the delta, not the delta: a one-sample level step of s
      // shows up in the first difference added to the waveform's own slew, so
      // whether it reads as a spike at all depends on which way the waveform
      // happened to be moving - a -3 dB step landing against a falling slope
      // can measure *smaller* than the steady slew, which is exactly what this
      // check saw before it was written this way. The second difference has no
      // such cancellation: a band-limited waveform's is ~A*w^2, orders below
      // its amplitude at these pitches, while any step survives it whole.
      auto MaxStep = [&](int from, int to) {
         float m = 0.0f;
         for (int i = std::max(2, from); i < std::min(to, (int)stream.size()); i++)
            m = std::max(m, std::fabs(stream[i] - 2.0f * stream[i - 1] + stream[i - 2]));
         return m;
      };

      // Steady state of note 1 alone, sampled from its sustain.
      const float steadyStep = MaxStep(4800, 9000);
      // Only the first ~0.7 ms after the onset: far enough into the 4 ms
      // attack that note 2 contributes almost nothing of its own curvature,
      // so anything large here came from a step applied to note 1.
      const float boundaryStep = MaxStep(boundary - 4, boundary + 32);
      const bool continuousOk = steadyStep > 1e-7f && boundaryStep < steadyStep * 8.0f;
      printf("DSPTEST wavetable poly-norm continuity: steady step=%.6f  note-on step=%.6f "
             "(%.2fx)  %s\n",
             steadyStep, boundaryStep, steadyStep > 0.0f ? boundaryStep / steadyStep : 0.0f,
             continuousOk ? "OK" : "FAIL");
      all &= continuousOk;
   }

   // 6. ...and smoothing it must not change steady-state loudness: a held
   //    3-note chord has to measure exactly 1/sqrt(3) of the same three
   //    voices summed unnormalised (which is what each note renders as on its
   //    own, where active == 1 and no normalisation applies).
   {
      auto Configure = [](WavetableNode& wt) {
         wt.volume = 1.0f;
         wt.mix = 0.0f;
         wt.glide = 0.0f;
         wt.engines[0].on = true;
         wt.engines[0].table = 0;
         wt.engines[0].position = 0.0f;
         wt.engines[0].unison = 1;
         wt.engines[0].phaseRandomize = 0.0f;
         wt.engines[0].warpMode = SynthModes::kWarpOff;
         wt.engines[0].warpAmount = 0.0f;
         wt.engines[0].filterType = SynthModes::kFilterOff;
         wt.engines[0].ampAttack = 4.0f;
         wt.engines[0].ampDecay = 1.0f;
         wt.engines[0].ampSustain = 1.0f;
         wt.engines[0].ampRelease = 260.0f;
         wt.engines[1].on = false;
      };

      const int kChordBlocks = 200; // ~1.07 s
      // Every note starts at frame 0 of block 0 with a reset phase, so a
      // solo render and the same note inside the chord stay sample-aligned.
      auto Render = [&](const std::vector<int>& notes, std::vector<float>& out) {
         WavetableNode wt;
         Configure(wt);
         AudioNode* audio = wt.GetAudioNode();
         wt.CookIfNeeded(1);
         audio->PrepareToPlay(sampleRate, numFrames);

         NoteEventQueue inbox;
         audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
         for (size_t n = 0; n < notes.size(); n++)
         {
            NoteEvent on;
            on.note = notes[n];
            on.velocity = 1.0f;
            on.isNoteOn = true;
            on.voiceId = 200 + (int)n;
            inbox.Push(on);
         }

         std::vector<float> chan0(numFrames), chan1(numFrames);
         float* chans[2] = { chan0.data(), chan1.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = numFrames;
         out.clear();
         for (int b = 0; b < kChordBlocks; b++)
         {
            audio->ProcessBlock(nullptr, 0, buf);
            out.insert(out.end(), chan0.begin(), chan0.end());
         }
      };

      const std::vector<int> chordNotes = { 60, 64, 67 };
      std::vector<float> chord, solo[3];
      Render(chordNotes, chord);
      for (int n = 0; n < 3; n++)
         Render({ chordNotes[n] }, solo[n]);

      // Measured from 0.2 s in, well past both the 4 ms attack and the
      // normalisation ramp, so this is purely a steady-state comparison.
      const int from = (int)(sampleRate * 0.2);
      const int to = (int)chord.size();
      double chordSum = 0.0, expectSum = 0.0;
      const double expectedNorm = 1.0 / sqrt(3.0);
      for (int i = from; i < to; i++)
      {
         const double expect = ((double)solo[0][i] + solo[1][i] + solo[2][i]) * expectedNorm;
         chordSum += (double)chord[i] * chord[i];
         expectSum += expect * expect;
      }
      const double n = (double)(to - from);
      const double chordRms = sqrt(chordSum / n);
      const double expectRms = sqrt(expectSum / n);
      const bool gainOk = expectRms > 0.01 && std::fabs(chordRms - expectRms) < expectRms * 0.01;
      printf("DSPTEST wavetable poly-norm steady gain: chord RMS=%.5f  1/sqrt(3) of summed "
             "voices=%.5f  %s\n",
             chordRms, expectRms, gainOk ? "OK" : "FAIL");
      all &= gainOk;
   }


   return all;
}

// Envelope (ModulatorNodes.h) since 03-envelope-to-shaper.md: no longer a
// note-triggered generator, a mod-in -> mod-out ADSR shaper. The gate is the
// "in" signal crossing `threshold`; output collapses toward 0.5 as the
// envelope level falls (ModDepthNode's convention). Value01() is the only
// public surface, so every check below reconstructs the internal envelope
// level algebraically from out01 = 0.5 + (in01-0.5)*envLevel rather than
// reaching into the node's private stage/level fields.
bool RunEnvelopeFixture()
{
   bool all = true;

   struct StubModulator : IModulator
   {
      float level = 0.0f;
      float Value01() override { return level; }
   };

   Transport& transport = Transport::Instance();
   transport.SetPlaying(true);

   // Ticks in small steps so a long AdvanceMs() call doesn't run into Tick's
   // own 0.25s-per-call clamp (a stalled-frame guard, not a granularity knob).
   auto AdvanceMs = [&](double ms)
   {
      double remainingSeconds = ms / 1000.0;
      while (remainingSeconds > 1.0e-6)
      {
         const float step = (float)std::min(remainingSeconds, 0.005);
         transport.Tick(step);
         remainingSeconds -= step;
      }
   };

   // out01 = 0.5 + (in01-0.5)*envLevel, inverted; in01 must not be 0.5 for
   // this to be solvable, so the two phases below pin the stub at 1.0/0.0.
   auto EnvLevelFromOut = [](float out01, float in01) { return (out01 - 0.5f) / (in01 - 0.5f); };

   EnvelopeNode env;
   env.attackMs = 50.0f;
   env.decayMs = 100.0f;
   env.sustainLevel = 0.4f;
   env.releaseMs = 150.0f;
   env.threshold = 0.5f;

   StubModulator stub;
   env.input = &stub;

   // Gate opens: input rises above threshold, starting attack from 0.
   stub.level = 1.0f;
   env.Value01();

   AdvanceMs(env.attackMs);
   const float levelAfterAttack = EnvLevelFromOut(env.Value01(), stub.level);
   const bool attackOk = levelAfterAttack > 0.95f;
   printf("DSPTEST envelope attack: expected ~1.0 after %.0fms  got %.4f  %s\n",
          env.attackMs, levelAfterAttack, attackOk ? "OK" : "FAIL");
   all &= attackOk;

   AdvanceMs(env.decayMs + 5.0);
   const float levelAfterDecay = EnvLevelFromOut(env.Value01(), stub.level);
   const bool decayOk = std::fabs(levelAfterDecay - env.sustainLevel) < 0.03f;
   printf("DSPTEST envelope decay settles at sustain: expected ~%.2f  got %.4f  %s\n",
          env.sustainLevel, levelAfterDecay, decayOk ? "OK" : "FAIL");
   all &= decayOk;

   // Falling back below threshold starts release; staying below it on a
   // later tick (the mod-cable analogue of a second note-off arriving
   // mid-release) must not restart the envelope - the level only ever falls.
   stub.level = 0.0f;
   AdvanceMs(2.0);
   const float levelEarlyRelease = EnvLevelFromOut(env.Value01(), stub.level);

   AdvanceMs(1.0);
   const float levelAfterSecondLowTick = EnvLevelFromOut(env.Value01(), stub.level);
   const bool noRestartOk = levelAfterSecondLowTick <= levelEarlyRelease + 0.02f;
   printf("DSPTEST envelope staying below threshold during release does not restart: %.4f -> %.4f  %s\n",
          levelEarlyRelease, levelAfterSecondLowTick, noRestartOk ? "OK" : "FAIL");
   all &= noRestartOk;

   AdvanceMs(env.releaseMs + 20.0);
   const float levelAfterRelease = EnvLevelFromOut(env.Value01(), stub.level);
   const bool releaseOk = levelAfterRelease < 0.02f;
   printf("DSPTEST envelope release reaches zero: got %.4f  %s\n",
          levelAfterRelease, releaseOk ? "OK" : "FAIL");
   all &= releaseOk;

   // Nothing patched in: output holds steady at constantIn regardless of
   // envelope stage - with the default constantIn/threshold both 0.5, in01
   // sits exactly at centre, so 0.5 + (in01-0.5)*envLevel collapses to 0.5
   // whatever the envelope is doing.
   EnvelopeNode unpatched;
   unpatched.attackMs = 5.0f;
   unpatched.decayMs = 5.0f;
   unpatched.releaseMs = 5.0f;
   const float unpatchedOut = unpatched.Value01();
   AdvanceMs(50.0);
   const float unpatchedOutLater = unpatched.Value01();
   const bool unpatchedOk = std::fabs(unpatchedOut - 0.5f) < 1.0e-4f &&
                            std::fabs(unpatchedOutLater - 0.5f) < 1.0e-4f;
   printf("DSPTEST envelope unpatched holds steady at constantIn: %.4f -> %.4f  %s\n",
          unpatchedOut, unpatchedOutLater, unpatchedOk ? "OK" : "FAIL");
   all &= unpatchedOk;

   transport.SetPlaying(false);
   transport.Rewind();

   return all;
}

bool RunVoiceStealFixture()
{
   bool all = true;
   VoiceAllocator alloc(4);
   alloc.SetSampleRate(48000.0);
   alloc.SetADSR(1000.0f, 1000.0f, 1.0f, 1000.0f); // slow enough to stay in Attack/active for this test

   const int firstVoice = alloc.NoteOn(60, 1.0f, 1); // oldest once the next three fill the rest
   alloc.NoteOn(61, 1.0f, 2);
   alloc.NoteOn(62, 1.0f, 3);
   alloc.NoteOn(63, 1.0f, 4);
   // All 4 voices now active - the 5th NoteOn must steal the oldest (first).
   const int stolen = alloc.NoteOn(64, 1.0f, 5);
   const bool stealOk = stolen == firstVoice;
   printf("DSPTEST voice steal picks oldest: oldest=%d  stolen=%d  %s\n",
          firstVoice, stolen, stealOk ? "OK" : "FAIL");
   all &= stealOk;
   return all;
}

// docs/plans/audio/P3c-P3a2-design.md §0.1's exit criterion: BeatsFor on all
// 18 divisions at both 4/4 and 7/8. Expected values are computed by hand
// here (not by re-deriving MusicTime's own formula), so this is checking the
// implementation against independently-written arithmetic, not against
// itself.
bool RunMusicTimeFixture()
{
   using namespace MusicTime;
   bool all = true;

   Transport& transport = Transport::Instance();
   const int savedNum = transport.TimeSigNumerator();
   const int savedDen = transport.TimeSigDenominator();

   struct Entry
   {
      RateDivision d;
      const char* name;
      double beats; // at 4/4 unless barBased
      bool barBased;
      double bars; // only meaningful when barBased
   };
   const Entry table[] = {
      { k4Bars, "4 bars", 0.0, true, 4.0 },
      { k2Bars, "2 bars", 0.0, true, 2.0 },
      { k1Bar, "1 bar", 0.0, true, 1.0 },
      { kHalf, "1/2", 2.0, false, 0.0 },
      { kHalfDot, "1/2.", 3.0, false, 0.0 },
      { kHalfTrip, "1/2T", 4.0 / 3.0, false, 0.0 },
      { kQuarter, "1/4", 1.0, false, 0.0 },
      { kQuarterDot, "1/4.", 1.5, false, 0.0 },
      { kQuarterTrip, "1/4T", 2.0 / 3.0, false, 0.0 },
      { kEighth, "1/8", 0.5, false, 0.0 },
      { kEighthDot, "1/8.", 0.75, false, 0.0 },
      { kEighthTrip, "1/8T", 1.0 / 3.0, false, 0.0 },
      { kSixteenth, "1/16", 0.25, false, 0.0 },
      { kSixteenthDot, "1/16.", 0.375, false, 0.0 },
      { kSixteenthTrip, "1/16T", 1.0 / 6.0, false, 0.0 },
      { kThirtySecond, "1/32", 0.125, false, 0.0 },
      { kThirtySecondDot, "1/32.", 0.1875, false, 0.0 },
      { kThirtySecondTrip, "1/32T", 1.0 / 12.0, false, 0.0 },
      { kSixtyFourth, "1/64", 0.0625, false, 0.0 },
   };
   const int n = (int)(sizeof(table) / sizeof(table[0]));
   const bool countOk = (n == (int)kNumRateDivisions);
   printf("DSPTEST musictime table completeness: expected %d entries got %d  %s\n", (int)kNumRateDivisions, n,
          countOk ? "OK" : "FAIL");
   all &= countOk;

   const struct { int num, den; double beatsPerBar; const char* label; } sigs[] = {
      { 4, 4, 4.0, "4/4" },
      { 7, 8, 3.5, "7/8" },
   };
   for (const auto& sig : sigs)
   {
      transport.SetTimeSignature(sig.num, sig.den);
      for (int i = 0; i < n; i++)
      {
         const double expected = table[i].barBased ? table[i].bars * sig.beatsPerBar : table[i].beats;
         const double got = BeatsFor(table[i].d);
         const bool ok = fabs(got - expected) < 1e-6;
         printf("DSPTEST BeatsFor(%s) at %s: expected %.6f got %.6f  %s\n", table[i].name, sig.label, expected,
                got, ok ? "OK" : "FAIL");
         all &= ok;
      }
   }

   transport.SetTimeSignature(savedNum, savedDen);
   return all;
}

// docs/plans/audio/P3c-P3a2-design.md §1.1's exit criterion. Runs the real
// pipeline (AudioEffectNode -> AudioEffectRuntime -> AudioFilterKernel's
// mailbox push -> ProcessBlock), not just the closed-form formula, so a bug
// in the coefficient plumbing (wrong mailbox slot, wrong stage count, ...)
// shows up here even though AudioFilterDsp::MagnitudeDb itself would still
// "pass" against its own math.
namespace AudioFilterTest
{
   // Feeds a settled sine at `evalHz` through the real kernel pipeline and
   // measures its RMS gain in dB - the running-code counterpart to
   // AudioFilterDsp::MagnitudeDb's scratch-instance measurement.
   float MeasureNodeMagnitudeDb(AudioEffectNode& node, float evalHz, double sampleRate)
   {
      AudioNode* audioNode = node.GetAudioNode();
      const int blockSize = 512;
      std::vector<float> inBuf(blockSize), outBuf(blockSize);
      float* inPtr = inBuf.data();
      float* outPtr = outBuf.data();
      AudioBuffer inBuffer;
      inBuffer.channels = &inPtr;
      inBuffer.numChannels = 1;
      inBuffer.numFrames = blockSize;
      AudioBuffer outBuffer;
      outBuffer.channels = &outPtr;
      outBuffer.numChannels = 1;
      outBuffer.numFrames = blockSize;
      const AudioBuffer* inputs[1] = { &inBuffer };

      double phase = 0.0;
      const double phaseInc = 2.0 * M_PI * (double)evalHz / sampleRate;
      const int totalBlocks = 40;
      const int measureFromBlock = totalBlocks / 2; // let the mailbox smoothing settle first
      double sumInSq = 0.0, sumOutSq = 0.0;

      for (int blk = 0; blk < totalBlocks; blk++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            inBuf[i] = (float)sin(phase);
            phase += phaseInc;
         }
         audioNode->ProcessBlock(inputs, 1, outBuffer);
         if (blk >= measureFromBlock)
         {
            for (int i = 0; i < blockSize; i++)
            {
               sumInSq += (double)inBuf[i] * (double)inBuf[i];
               sumOutSq += (double)outBuf[i] * (double)outBuf[i];
            }
         }
      }
      const int n = (totalBlocks - measureFromBlock) * blockSize;
      const double inRms = sqrt(sumInSq / n);
      const double outRms = sqrt(sumOutSq / n);
      return (float)(20.0 * log10(std::max(1e-9, outRms / std::max(1e-9, inRms))));
   }

   // AudioEffectNode configured for the Audio Filter def, prepared for
   // `sampleRate`. Heap-allocated: AudioEffectNode owns a
   // unique_ptr<AudioEffectRuntime> and declares its own destructor
   // (out-of-line, so main.cpp never needs AudioEffectRuntime's definition),
   // which suppresses the implicit move constructor a by-value return would
   // need.
   std::unique_ptr<AudioEffectNode> MakeSingleBandNode(int type, float freq, float q, float gainDb,
                                                       double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Audio Filter")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      *node->ParamPtr("type") = (float)type;
      *node->ParamPtr("freq") = freq;
      *node->ParamPtr("q") = q;
      *node->ParamPtr("gain") = gainDb;
      node->mix = 1.0f;
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }
}

bool RunAudioFilterFixture()
{
   using namespace AudioFilterTest;
   bool all = true;
   const double sampleRate = 44100.0; // matches AudioEffectNode::CookIfNeeded's no-device fallback

   // 1) Magnitude response at known cutoffs against the analytic response,
   //    measured through the real running kernel - ±0.5 dB.
   struct Case { int type; float freq, q, gainDb, evalHz; const char* label; };
   const Case cases[] = {
      { AudioFilterDsp::kLP24, 1000.0f, 0.707f, 0.0f, 300.0f, "LP24 passband" },
      { AudioFilterDsp::kLP24, 1000.0f, 0.707f, 0.0f, 1000.0f, "LP24 @ cutoff" },
      { AudioFilterDsp::kLP24, 1000.0f, 0.707f, 0.0f, 8000.0f, "LP24 stopband" },
      { AudioFilterDsp::kHP12, 500.0f, 0.707f, 0.0f, 4000.0f, "HP12 passband" },
      { AudioFilterDsp::kPeak, 1000.0f, 2.0f, 6.0f, 1000.0f, "peak @ centre" },
      { AudioFilterDsp::kBP, 1000.0f, 5.0f, 0.0f, 1000.0f, "BP @ centre" },
      { AudioFilterDsp::kBP, 1000.0f, 5.0f, 0.0f, 100.0f, "BP far below" },
   };
   for (const Case& c : cases)
   {
      std::unique_ptr<AudioEffectNode> node = MakeSingleBandNode(c.type, c.freq, c.q, c.gainDb, sampleRate);
      const float measured = MeasureNodeMagnitudeDb(*node, c.evalHz, sampleRate);
      const float analytic = AudioFilterDsp::MagnitudeDb(c.type, c.freq, c.q, c.gainDb, c.evalHz, sampleRate);
      const bool ok = fabsf(measured - analytic) < 0.5f;
      printf("DSPTEST filter magnitude (%s): analytic %.2f dB  measured %.2f dB  %s\n", c.label, analytic,
             measured, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 2) -3 dB point per slope: extra cascaded stages pull it below the
   //    nominal cutoff (SynthModes.h documents the same effect for
   //    Wavetable's filter), so slope order - not an exact frequency - is
   //    what's asserted: LP36's -3dB point is lower than LP24's, which is
   //    lower than LP12's, which is at or below the nominal cutoff.
   {
      auto Find3dB = [&](int type, float cutoff, float q) {
         float lo = 20.0f, hi = cutoff;
         for (int iter = 0; iter < 40; iter++)
         {
            const float mid = sqrtf(lo * hi); // bisect in log space
            const float db = AudioFilterDsp::MagnitudeDb(type, cutoff, q, 0.0f, mid, sampleRate);
            // Passband (db >= -3) is below the crossing for a lowpass, so a
            // passband reading means the crossing is above mid - raise lo;
            // an over-attenuated reading means it's below mid - lower hi.
            if (db < -3.0f)
               hi = mid;
            else
               lo = mid;
         }
         return sqrtf(lo * hi);
      };
      const float f12 = Find3dB(AudioFilterDsp::kLP12, 2000.0f, 0.707f);
      const float f24 = Find3dB(AudioFilterDsp::kLP24, 2000.0f, 0.707f);
      const float f36 = Find3dB(AudioFilterDsp::kLP36, 2000.0f, 0.707f);
      const bool orderOk = f36 <= f24 + 1.0f && f24 <= f12 + 1.0f && f12 <= 2000.0f + 1.0f;
      printf("DSPTEST filter -3dB point per slope: LP12=%.0fHz LP24=%.0fHz LP36=%.0fHz (cutoff 2000Hz)  %s\n",
             f12, f24, f36, orderOk ? "OK" : "FAIL");
      all &= orderOk;
   }

   // 3) Stability sweep: every type x a freq/Q grid, no NaN/Inf anywhere in
   //    the block.
   {
      bool stableOk = true;
      const float freqs[] = { 20.0f, 100.0f, 1000.0f, 5000.0f, 15000.0f, 19999.0f };
      const float qs[] = { 0.1f, 0.707f, 5.0f, 18.0f };
      int checked = 0;
      for (int type = 0; type < AudioFilterDsp::kNumFilterTypes; type++)
      {
         for (float freq : freqs)
         {
            for (float q : qs)
            {
               std::unique_ptr<AudioEffectNode> node = MakeSingleBandNode(type, freq, q, 6.0f, sampleRate);
               AudioNode* audioNode = node->GetAudioNode();
               const int blockSize = 256;
               std::vector<float> inBuf(blockSize, 0.0f), outBuf(blockSize, 0.0f);
               for (int i = 0; i < blockSize; i++)
                  inBuf[i] = (i % 2 == 0) ? 1.0f : -1.0f; // worst-case Nyquist-ish content
               float* inPtr = inBuf.data();
               float* outPtr = outBuf.data();
               AudioBuffer inBuffer;
               inBuffer.channels = &inPtr;
               inBuffer.numChannels = 1;
               inBuffer.numFrames = blockSize;
               AudioBuffer outBuffer;
               outBuffer.channels = &outPtr;
               outBuffer.numChannels = 1;
               outBuffer.numFrames = blockSize;
               const AudioBuffer* inputs[1] = { &inBuffer };
               for (int blk = 0; blk < 4; blk++)
                  audioNode->ProcessBlock(inputs, 1, outBuffer);
               checked++;
               for (int i = 0; i < blockSize; i++)
                  if (!std::isfinite(outBuf[i]))
                     stableOk = false;
            }
         }
      }
      printf("DSPTEST filter stability sweep: %d type x freq x Q combinations, no NaN/Inf  %s\n", checked,
             stableOk ? "OK" : "FAIL");
      all &= stableOk;
   }

   return all;
}

// The exit criterion for Dynamics' compressor-only kernel. Runs the real
// pipeline (AudioEffectNode -> AudioEffectRuntime -> DynamicsKernel), not
// just DynamicsDsp::GainComputerDb in isolation - the same reason
// RunAudioFilterFixture measures through MeasureNodeMagnitudeDb rather than
// trusting the closed-form formula alone.
namespace DynamicsTest
{
   std::unique_ptr<AudioEffectNode> MakeDynamicsNode(float threshold, float ratio, float attackMs, float releaseMs,
                                                      float makeupDb, double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Dynamics")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      *node->ParamPtr("threshold") = threshold;
      *node->ParamPtr("ratio") = ratio;
      *node->ParamPtr("attack") = attackMs;
      *node->ParamPtr("release") = releaseMs;
      *node->ParamPtr("makeup") = makeupDb;
      node->mix = 1.0f;
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }

   // Runs `numSamples` mono samples of constant amplitude `amplitude`
   // through the node one sample at a time (block size 1), so the caller can
   // observe the branching detector's sample-accurate step response -
   // Audio Filter's fixture never needed this because its coefficients don't
   // have their own time-domain envelope to measure.
   template <typename SampleFn>
   void RunSamples(AudioEffectNode& node, int numSamples, SampleFn getSample, std::vector<float>* outSamples)
   {
      AudioNode* audioNode = node.GetAudioNode();
      float inVal = 0.0f, outVal = 0.0f;
      float* inPtr = &inVal;
      float* outPtr = &outVal;
      AudioBuffer inBuffer;
      inBuffer.channels = &inPtr;
      inBuffer.numChannels = 1;
      inBuffer.numFrames = 1;
      AudioBuffer outBuffer;
      outBuffer.channels = &outPtr;
      outBuffer.numChannels = 1;
      outBuffer.numFrames = 1;
      const AudioBuffer* inputs[1] = { &inBuffer };
      for (int i = 0; i < numSamples; i++)
      {
         inVal = getSample(i);
         audioNode->ProcessBlock(inputs, 1, outBuffer);
         if (outSamples != nullptr)
            outSamples->push_back(outVal);
      }
   }
}

bool RunDynamicsFixture()
{
   using namespace DynamicsTest;
   bool all = true;
   const double sampleRate = 44100.0; // matches AudioEffectNode::CookIfNeeded's no-device fallback

   // 1) Static gain-reduction curve vs analytic, measured through the real
   //    running kernel, at 5 thresholds x 4 ratios - hard knee (0 dB) so the
   //    comparison is against the formula's linear-above-knee branch
   //    directly, not its own quadratic interpolation.
   {
      const float thresholds[] = { -40.0f, -30.0f, -20.0f, -10.0f, -5.0f };
      const float ratios[] = { 2.0f, 4.0f, 8.0f, 20.0f };
      bool curveOk = true;
      int checked = 0;
      for (float threshold : thresholds)
      {
         for (float ratio : ratios)
         {
            const float inputDb = std::min(0.0f, threshold + 12.0f); // comfortably above threshold
            auto node = MakeDynamicsNode(threshold, ratio, 0.1f, 5.0f, 0.0f, sampleRate);
            const float amplitude = DspMath::DbToLinear(inputDb);
            std::vector<float> out;
            // Settle: fast attack (0.1 ms) reaches steady state in a few
            // hundred samples; 2000 is generous headroom.
            RunSamples(*node, 2000, [&](int) { return amplitude; }, &out);
            const float measuredOutDb = DspMath::LinearToDb(std::fabs(out.back()));
            const float measuredReductionDb = inputDb - measuredOutDb;
            const float analyticYG = DynamicsDsp::GainComputerDb(inputDb, threshold, ratio);
            const float analyticReductionDb = inputDb - analyticYG;
            const bool ok = std::fabs(measuredReductionDb - analyticReductionDb) < 0.5f;
            checked++;
            if (!ok)
               curveOk = false;
         }
      }
      printf("DSPTEST dynamics gain-reduction curve: %d threshold x ratio combinations vs analytic  %s\n",
             checked, curveOk ? "OK" : "FAIL");
      all &= curveOk;
   }

   // 2) Attack/release time constants off a step input, +/-10%. Amplitude
   //    held constant (a synthetic "DC-like" magnitude, not a real tone -
   //    the detector's abs-value + branching smoother reacts to |x| alone,
   //    so this isolates the envelope's own timing from anything a sine's
   //    zero crossings would add) so the step is exact to the sample.
   {
      const float threshold = -20.0f, ratio = 4.0f;
      const float attackMs = 20.0f, releaseMs = 200.0f;
      const float quietDb = -40.0f, loudDb = -6.0f;
      auto node = MakeDynamicsNode(threshold, ratio, attackMs, releaseMs, 0.0f, sampleRate);
      const float quietAmp = DspMath::DbToLinear(quietDb);
      const float loudAmp = DspMath::DbToLinear(loudDb);
      const float finalYG = DynamicsDsp::GainComputerDb(loudDb, threshold, ratio);
      const float finalReductionDb = loudDb - finalYG; // > 0: this is a compress-mode step, reduction only grows

      // Settle at quiet first (reduction ~0 below threshold).
      RunSamples(*node, 4000, [&](int) { return quietAmp; }, nullptr);

      // Step to loud; find the sample where reduction crosses 63.2% of its
      // final value - exactly one time constant for a one-pole step
      // response, which is what the attack coefficient IS.
      std::vector<float> attackTrace;
      RunSamples(*node, 10000, [&](int) { return loudAmp; }, &attackTrace);
      const float attackTargetDb = 0.632f * finalReductionDb;
      int attackSample = -1;
      for (size_t i = 0; i < attackTrace.size(); i++)
      {
         const float outDb = DspMath::LinearToDb(std::fabs(attackTrace[i]));
         const float reductionDb = loudDb - outDb;
         if (reductionDb >= attackTargetDb)
         {
            attackSample = (int)i;
            break;
         }
      }
      const float measuredAttackMs = attackSample >= 0 ? (float)attackSample / (float)sampleRate * 1000.0f : -1.0f;
      const bool attackOk = attackSample >= 0 && std::fabs(measuredAttackMs - attackMs) < attackMs * 0.10f + 0.5f;

      // Step back to quiet; find the sample where reduction falls to 36.8%
      // of its (now-settled) final value - one release time constant.
      std::vector<float> releaseTrace;
      RunSamples(*node, 20000, [&](int) { return quietAmp; }, &releaseTrace);
      const float releaseTargetDb = 0.368f * finalReductionDb;
      int releaseSample = -1;
      for (size_t i = 0; i < releaseTrace.size(); i++)
      {
         const float outDb = DspMath::LinearToDb(std::fabs(releaseTrace[i]));
         // out = in * gr, gr = DbToLinear(-mSmoothedReductionDb) with makeup
         // 0 here, so quietDb - outDb recovers the kernel's own smoothed
         // reduction directly through the real signal path - the gain
         // computer's *target* would read 0 here (quiet is below threshold),
         // but the applied (smoothed) reduction is exactly what's releasing.
         const float reductionDb = quietDb - outDb;
         if (reductionDb <= releaseTargetDb)
         {
            releaseSample = (int)i;
            break;
         }
      }
      const float measuredReleaseMs =
         releaseSample >= 0 ? (float)releaseSample / (float)sampleRate * 1000.0f : -1.0f;
      const bool releaseOk =
         releaseSample >= 0 && std::fabs(measuredReleaseMs - releaseMs) < releaseMs * 0.10f + 0.5f;

      printf("DSPTEST dynamics attack time constant: target %.1f ms measured %.2f ms  %s\n", attackMs,
             measuredAttackMs, attackOk ? "OK" : "FAIL");
      printf("DSPTEST dynamics release time constant: target %.1f ms measured %.2f ms  %s\n", releaseMs,
             measuredReleaseMs, releaseOk ? "OK" : "FAIL");
      all &= attackOk;
      all &= releaseOk;
   }

   return all;
}

bool RunDelayFixture()
{
   using namespace DelayTest;
   bool all = true;
   const double sampleRate = 44100.0; // matches AudioEffectNode::CookIfNeeded's no-device fallback

   // 1) Impulse timing: simple mode, no feedback, tempo-synced, so the only
   //    tap is the delay line's own read/write ordering - one write-then-
   //    read latency sample beyond the nominal division length (the
   //    class comment on DelayLine documents k=0 as "the most recent
   //    write", and Read()'s own floor of 1 sample means the earliest
   //    readable tap is always one sample behind that write, not zero) -
   //    checked across 3 divisions x 2 tempos.
   {
      struct Case { MusicTime::RateDivision div; float bpm; };
      const Case cases[] = { { MusicTime::kQuarter, 120.0f }, { MusicTime::kEighth, 120.0f },
                             { MusicTime::kEighth, 90.0f } };
      bool timingOk = true;
      int checked = 0;
      for (const Case& c : cases)
      {
         Transport::Instance().SetTempo(c.bpm);
         auto node = MakeDelayNode(false, sampleRate);
         *node->ParamPtr("sync") = 1.0f;
         *node->ParamPtr("rateDiv") = (float)c.div;
         *node->ParamPtr("feedback") = 0.0f;
         node->CookIfNeeded(2);

         const double expectedSeconds = MusicTime::BeatsFor(c.div) * 60.0 / c.bpm;
         const int expectedSamples = (int)std::lround(expectedSeconds * sampleRate) + 1; // +1: see comment above

         std::vector<std::vector<float>> out;
         RunSamples(*node, expectedSamples + 32, 1, [](int i, int) { return i == 0 ? 1.0f : 0.0f; }, &out);

         int peakIdx = -1;
         float peakVal = 0.0f;
         for (size_t i = 0; i < out[0].size(); i++)
         {
            if (std::fabs(out[0][i]) > peakVal)
            {
               peakVal = std::fabs(out[0][i]);
               peakIdx = (int)i;
            }
         }
         const bool ok = peakIdx >= 0 && std::abs(peakIdx - expectedSamples) <= 1;
         checked++;
         if (!ok)
            timingOk = false;
      }
      printf("DSPTEST delay impulse timing: %d division x tempo combinations, tap within 1 sample of expected  %s\n",
             checked, timingOk ? "OK" : "FAIL");
      all &= timingOk;
   }

   // 2) Feedback decay: 50% feedback should lose 6.02 dB (20*log10(0.5)) per
   //    repeat - measured by comparing the peak of the Nth repeat to the
   //    peak of the (N+1)th, at tone=0 (flat) so it can't bias the result.
   {
      Transport::Instance().SetTempo(120.0f);
      auto node = MakeDelayNode(false, sampleRate);
      *node->ParamPtr("sync") = 1.0f;
      *node->ParamPtr("rateDiv") = (float)MusicTime::kEighth;
      *node->ParamPtr("feedback") = 50.0f;
      node->CookIfNeeded(3);

      const double beatSeconds = MusicTime::BeatsFor(MusicTime::kEighth) * 60.0 / 120.0;
      const int repeatSamples = (int)std::lround(beatSeconds * sampleRate);

      std::vector<std::vector<float>> out;
      RunSamples(*node, repeatSamples * 6 + 32, 1, [](int i, int) { return i == 0 ? 1.0f : 0.0f; }, &out);

      auto PeakNear = [&](int center) {
         float peak = 0.0f;
         for (int i = std::max(0, center - 2); i <= center + 2 && i < (int)out[0].size(); i++)
            peak = std::max(peak, std::fabs(out[0][i]));
         return peak;
      };
      const float repeat1 = PeakNear(repeatSamples + 1);
      const float repeat2 = PeakNear(repeatSamples * 2 + 1);
      const float measuredDropDb = DspMath::LinearToDb(repeat1) - DspMath::LinearToDb(std::max(1e-9f, repeat2));
      const bool decayOk = repeat1 > 0.01f && std::fabs(measuredDropDb - 6.02f) < 0.5f;
      printf("DSPTEST delay feedback decay: 50%% feedback measured %.2f dB drop per repeat (expect 6.02 dB)  %s\n",
             measuredDropDb, decayOk ? "OK" : "FAIL");
      all &= decayOk;
   }

   // 3) Bounce L/R alternation: an impulse's first repeat should peak on the
   //    opposite channel from where the direct tap landed.
   {
      Transport::Instance().SetTempo(120.0f);
      auto node = MakeDelayNode(true, sampleRate);
      *node->ParamPtr("sync") = 1.0f;
      *node->ParamPtr("rateDiv") = (float)MusicTime::kEighth;
      *node->ParamPtr("feedback") = 60.0f;
      node->CookIfNeeded(4);

      const double beatSeconds = MusicTime::BeatsFor(MusicTime::kEighth) * 60.0 / 120.0;
      const int tapSamples = (int)std::lround(beatSeconds * sampleRate);

      std::vector<std::vector<float>> out;
      RunSamples(
         *node, tapSamples * 3 + 32, 2, [](int i, int ch) { return (i == 0 && ch == 0) ? 1.0f : 0.0f; }, &out);

      auto PeakChannelNear = [&](int ch, int center) {
         float peak = 0.0f;
         for (int i = std::max(0, center - 2); i <= center + 2 && i < (int)out[ch].size(); i++)
            peak = std::max(peak, std::fabs(out[ch][i]));
         return peak;
      };
      const float firstL = PeakChannelNear(0, tapSamples + 1);
      const float firstR = PeakChannelNear(1, tapSamples + 1);
      const float secondL = PeakChannelNear(0, tapSamples * 2 + 1);
      const float secondR = PeakChannelNear(1, tapSamples * 2 + 1);
      const bool alternateOk = firstL > firstR * 3.0f && secondR > secondL * 3.0f;
      printf("DSPTEST delay bounce alternation: tap1 L %.3f R %.3f, tap2 L %.3f R %.3f  %s\n", firstL, firstR,
             secondL, secondR, alternateOk ? "OK" : "FAIL");
      all &= alternateOk;
   }

   Transport::Instance().SetTempo(120.0f); // restore default for any fixture that runs after this one
   return all;
}

namespace ReverbTest
{
   std::unique_ptr<AudioEffectNode> MakeReverbNode(double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Reverb")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      node->mix = 1.0f; // 100% wet so the fixture measures the reverb tail directly, not a dry/wet blend
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }

   // Windowed-RMS envelope of an impulse response, hop-quantized (windowSize
   // 512 / hop 256 at 44.1kHz - ~5.8ms resolution, plenty against the >=0.1s
   // decay times under test). Returns the RT60-style time in seconds from
   // the envelope's own peak to the first window that falls below
   // peak*0.001 (-60dB), or -1 if it never does within the buffer.
   float MeasureDecaySeconds(AudioEffectNode& node, double sampleRate, float bufferSeconds)
   {
      const int numSamples = (int)(bufferSeconds * sampleRate);
      const int windowSize = 512;
      const int hop = 256;

      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(node, numSamples, 1, [](int i, int) { return i == 0 ? 1.0f : 0.0f; }, &out);

      std::vector<float> envs;
      for (int w0 = 0; w0 + windowSize <= numSamples; w0 += hop)
      {
         double sumSq = 0.0;
         for (int i = 0; i < windowSize; i++)
         {
            const float v = out[0][(size_t)(w0 + i)];
            sumSq += (double)v * (double)v;
         }
         envs.push_back((float)std::sqrt(sumSq / windowSize));
      }
      if (envs.empty())
         return -1.0f;

      int peakIdx = 0;
      float peakVal = 0.0f;
      for (size_t i = 0; i < envs.size(); i++)
         if (envs[i] > peakVal)
         {
            peakVal = envs[i];
            peakIdx = (int)i;
         }
      if (peakVal <= 0.0f)
         return -1.0f;

      const float thresh = peakVal * 0.001f; // -60dB
      for (int i = peakIdx; i < (int)envs.size(); i++)
      {
         if (envs[i] < thresh)
            return (float)((i - peakIdx) * hop) / (float)sampleRate;
      }
      return -1.0f;
   }
}

bool RunReverbFixture()
{
   using namespace ReverbTest;
   bool all = true;
   const double sampleRate = 44100.0;

   // 1) RT60 accuracy: damping=0 and predelay=0 to isolate the per-line
   //    decay-gain formula (ReverbKernel.h's class comment) from the extra
   //    loss the damping filter adds, at two decay settings.
   {
      const float decayTargets[] = { 1.0f, 3.0f };
      bool rt60Ok = true;
      for (float target : decayTargets)
      {
         auto node = MakeReverbNode(sampleRate);
         *node->ParamPtr("size") = 0.5f;
         *node->ParamPtr("damping") = 0.0f;
         *node->ParamPtr("predelay") = 0.0f;
         *node->ParamPtr("decay") = target;
         node->CookIfNeeded(2);

         const float measured = MeasureDecaySeconds(*node, sampleRate, target * 2.0f + 0.5f);
         const bool ok = measured > 0.0f && std::fabs(measured - target) < target * 0.2f;
         printf("DSPTEST reverb RT60 (target %.1fs): measured %.2fs  %s\n", target, measured, ok ? "OK" : "FAIL");
         rt60Ok &= ok;
      }
      all &= rt60Ok;
   }

   // 2) Predelay lands at the exact sample: silence before it, signal at/
   //    after it. The diffuser's Schroeder allpasses have an instantaneous
   //    (-g*x[n]) term, so the very first audible sample after predelay is
   //    the predelay sample itself, not one line-length later.
   {
      auto node = MakeReverbNode(sampleRate);
      *node->ParamPtr("size") = 0.5f;
      *node->ParamPtr("damping") = 0.0f;
      *node->ParamPtr("predelay") = 50.0f;
      *node->ParamPtr("decay") = 1.0f;
      node->CookIfNeeded(3);

      // predelay reaches the mailbox's ~5ms target smoother like every other
      // continuous param, so a 100ms silent lead-in (20 time constants) lets
      // it settle to its target before the impulse arrives - otherwise the
      // still-ramping value under-shoots the delay early on and the tap
      // lands before the nominal 50ms mark.
      const int warmupSamples = (int)(0.1 * sampleRate);
      const int predelaySamples = (int)std::lround(50.0 * 0.001 * sampleRate);
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(
         *node, warmupSamples + predelaySamples + 1600, 1,
         [warmupSamples](int i, int) { return i == warmupSamples ? 1.0f : 0.0f; }, &out);

      // Silence must hold exactly up to the predelay tap - that boundary is
      // sample-exact, the predelay line is a plain integer read/write. What
      // is NOT instantaneous is how soon audible energy follows it: nothing
      // reaches the final output until the *shortest* FDN line has looped
      // once (each output sample is a delay-line read, so a freshly-injected
      // sample isn't visible in any line's output until that line's own
      // activeLen samples later) - at size=0.5 that is
      // round(1051 * 0.575) = ~604 samples, the shortest of the 8 lines.
      // The window below (1600) clears even the longest line
      // (round(1867 * 0.575) = ~1074) with margin, so it asserts the real
      // "onset follows predelay" behaviour without hard-coding the FDN's
      // internal per-line lengths into the test.
      bool silentBefore = true;
      for (int i = warmupSamples; i < warmupSamples + predelaySamples - 1; i++) // -1: interpolation can bleed one sample early
         if (std::fabs(out[0][(size_t)i]) > 1.0e-5f)
            silentBefore = false;
      bool nonzeroAfter = false;
      for (int i = warmupSamples + predelaySamples; i < warmupSamples + predelaySamples + 1600; i++)
         if (std::fabs(out[0][(size_t)i]) > 1.0e-5f)
            nonzeroAfter = true;
      const bool predelayOk = silentBefore && nonzeroAfter;
      printf("DSPTEST reverb predelay: silent for %d samples, onset follows within the shortest FDN loop  %s\n",
             predelaySamples - 1, predelayOk ? "OK" : "FAIL");
      all &= predelayOk;
   }

   // 3) Energy decays monotonically (no build-up - the mixing matrix is
   //    orthogonal, not merely bounded): the envelope's second half (well
   //    after the onset) must average lower than its first half.
   {
      auto node = MakeReverbNode(sampleRate);
      *node->ParamPtr("size") = 0.7f;
      *node->ParamPtr("damping") = 0.3f;
      *node->ParamPtr("predelay") = 0.0f;
      *node->ParamPtr("decay") = 1.5f;
      node->CookIfNeeded(4);

      const int numSamples = (int)(3.0 * sampleRate);
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(*node, numSamples, 1, [](int i, int) { return i == 0 ? 1.0f : 0.0f; }, &out);

      const int windowSize = 1024;
      std::vector<float> envs;
      for (int w0 = 0; w0 + windowSize <= numSamples; w0 += windowSize)
      {
         double sumSq = 0.0;
         for (int i = 0; i < windowSize; i++)
         {
            const float v = out[0][(size_t)(w0 + i)];
            sumSq += (double)v * (double)v;
         }
         envs.push_back((float)std::sqrt(sumSq / windowSize));
      }
      const size_t n = envs.size();
      double firstThird = 0.0, lastThird = 0.0;
      for (size_t i = 0; i < n / 3; i++)
         firstThird += envs[i];
      for (size_t i = n - n / 3; i < n; i++)
         lastThird += envs[i];
      firstThird /= std::max<size_t>(1, n / 3);
      lastThird /= std::max<size_t>(1, n / 3);
      const bool decayOk = lastThird < firstThird * 0.5;
      printf("DSPTEST reverb monotonic decay: first-third RMS %.5f, last-third RMS %.5f  %s\n", firstThird,
             lastThird, decayOk ? "OK" : "FAIL");
      all &= decayOk;
   }

   // 4) Damping measurably shortens the decay: same `decay` param, damping=0
   //    vs damping=1, should show a clearly shorter measured RT60 with
   //    damping engaged (the frequency-dependent LF-vs-HF split the design
   //    doc's Tier 2 calls for isn't implemented - this checks the coarser,
   //    audible effect damping does have: extra loss in the feedback loop).
   {
      auto nodeFlat = MakeReverbNode(sampleRate);
      *nodeFlat->ParamPtr("size") = 0.5f;
      *nodeFlat->ParamPtr("damping") = 0.0f;
      *nodeFlat->ParamPtr("predelay") = 0.0f;
      *nodeFlat->ParamPtr("decay") = 2.0f;
      nodeFlat->CookIfNeeded(5);
      const float rt60Flat = MeasureDecaySeconds(*nodeFlat, sampleRate, 4.5f);

      auto nodeDamped = MakeReverbNode(sampleRate);
      *nodeDamped->ParamPtr("size") = 0.5f;
      *nodeDamped->ParamPtr("damping") = 1.0f;
      *nodeDamped->ParamPtr("predelay") = 0.0f;
      *nodeDamped->ParamPtr("decay") = 2.0f;
      nodeDamped->CookIfNeeded(6);
      const float rt60Damped = MeasureDecaySeconds(*nodeDamped, sampleRate, 4.5f);

      const bool dampingOk = rt60Flat > 0.0f && rt60Damped > 0.0f && rt60Damped < rt60Flat * 0.9f;
      printf("DSPTEST reverb damping: damping=0 RT60 %.2fs, damping=1 RT60 %.2fs  %s\n", rt60Flat, rt60Damped,
             dampingOk ? "OK" : "FAIL");
      all &= dampingOk;
   }

   // 5) No denormal/NaN spike on a long tail: short decay, run well past it,
   //    the tail should have fully flushed to exact zero (FlushDenormal's
   //    1e-20 threshold) with no NaN/Inf anywhere along the way.
   {
      auto node = MakeReverbNode(sampleRate);
      *node->ParamPtr("size") = 0.5f;
      *node->ParamPtr("damping") = 0.5f;
      *node->ParamPtr("predelay") = 0.0f;
      *node->ParamPtr("decay") = 0.3f;
      node->CookIfNeeded(7);

      const int numSamples = (int)(3.0 * sampleRate);
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(*node, numSamples, 1, [](int i, int) { return i == 0 ? 1.0f : 0.0f; }, &out);

      bool finiteOk = true;
      for (float v : out[0])
         if (!std::isfinite(v))
            finiteOk = false;
      bool flushedOk = true;
      for (int i = numSamples - 1000; i < numSamples; i++)
         if (out[0][(size_t)i] != 0.0f)
            flushedOk = false;
      const bool tailOk = finiteOk && flushedOk;
      printf("DSPTEST reverb long tail: finite %s, flushed to zero by end %s  %s\n", finiteOk ? "yes" : "no",
             flushedOk ? "yes" : "no", tailOk ? "OK" : "FAIL");
      all &= tailOk;
   }

   // 6) SIMD vs Scalar A/B numerical equivalence: test across multiple configurations
   //    (digital default, analog lush, small room mono) with impulse, sine tone, and noise.
   {
      bool simdEquivOk = true;
      float maxDiff = 0.0f;
      const int testFrames = 2048;
      const int blockSize = 256;

      const EffectDef* reverbDef = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Reverb")
            reverbDef = &d;

      struct TestConfig {
         float size;
         float decay;
         float damping;
         float predelay;
         float width;
         float analog;
      };

      const TestConfig configs[] = {
         { 0.5f, 1.5f, 0.2f, 10.0f, 0.8f, 0.0f },
         { 0.8f, 2.5f, 0.6f, 25.0f, 1.0f, 1.0f },
         { 0.2f, 0.5f, 0.9f, 0.0f, 0.0f, 0.0f },
      };

      for (const auto& cfg : configs)
      {
         if (!reverbDef) break;

         AudioEffectNode nodeSimd(*reverbDef);
         *nodeSimd.ParamPtr("size") = cfg.size;
         *nodeSimd.ParamPtr("decay") = cfg.decay;
         *nodeSimd.ParamPtr("damping") = cfg.damping;
         *nodeSimd.ParamPtr("predelay") = cfg.predelay;
         *nodeSimd.ParamPtr("width") = cfg.width;
         *nodeSimd.ParamPtr("analog") = cfg.analog;

         AudioEffectNode nodeScalar(*reverbDef);
         *nodeScalar.ParamPtr("size") = cfg.size;
         *nodeScalar.ParamPtr("decay") = cfg.decay;
         *nodeScalar.ParamPtr("damping") = cfg.damping;
         *nodeScalar.ParamPtr("predelay") = cfg.predelay;
         *nodeScalar.ParamPtr("width") = cfg.width;
         *nodeScalar.ParamPtr("analog") = cfg.analog;

         ReverbKernel rkSimd, rkScalar;
         rkSimd.PrepareToPlay(sampleRate, blockSize);
         rkScalar.PrepareToPlay(sampleRate, blockSize);
         rkSimd.PushParams(nodeSimd, sampleRate);
         rkScalar.PushParams(nodeScalar, sampleRate);

         std::vector<float> inL(testFrames), inR(testFrames);
         for (int i = 0; i < testFrames; i++)
         {
            float s = (i == 0) ? 1.0f : 0.0f;
            s += 0.3f * std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sampleRate);
            s += 0.05f * ((float)(i % 17) / 17.0f - 0.5f);
            inL[i] = s;
            inR[i] = s * 0.9f;
         }

         float inChL[blockSize], inChR[blockSize];
         float* inChannels[2] = { inChL, inChR };
         AudioBuffer inBuf;
         inBuf.channels = inChannels;
         inBuf.numChannels = 2;
         inBuf.numFrames = blockSize;

         float outSimdL[blockSize], outSimdR[blockSize];
         float* outSimdChannels[2] = { outSimdL, outSimdR };
         AudioBuffer outSimdBuf;
         outSimdBuf.channels = outSimdChannels;
         outSimdBuf.numChannels = 2;
         outSimdBuf.numFrames = blockSize;

         float outScalarL[blockSize], outScalarR[blockSize];
         float* outScalarChannels[2] = { outScalarL, outScalarR };
         AudioBuffer outScalarBuf;
         outScalarBuf.channels = outScalarChannels;
         outScalarBuf.numChannels = 2;
         outScalarBuf.numFrames = blockSize;

         for (int offset = 0; offset < testFrames; offset += blockSize)
         {
            for (int k = 0; k < blockSize; k++)
            {
               inChL[k] = inL[offset + k];
               inChR[k] = inR[offset + k];
            }

            rkSimd.ProcessBlockSimd(inBuf, nullptr, outSimdBuf);
            rkScalar.ProcessBlockScalar(inBuf, nullptr, outScalarBuf);

            for (int ch = 0; ch < 2; ch++)
            {
               for (int k = 0; k < blockSize; k++)
               {
                  const float diff = std::fabs(outSimdBuf.channels[ch][k] - outScalarBuf.channels[ch][k]);
                  maxDiff = std::max(maxDiff, diff);
                  if (diff > 1.0e-5f)
                     simdEquivOk = false;
               }
            }
         }
      }
      printf("DSPTEST reverb SIMD vs Scalar A/B: max diff %.2e (tol 1.0e-5)  %s\n",
             maxDiff, simdEquivOk ? "OK" : "FAIL");
      all &= simdEquivOk;
   }

   // 7) Live kernel vs the frozen pre-perf kernel (ProcessBlockLegacy,
   //    ReverbKernelLegacy.cpp): 10 s at 48 kHz / 256, B1's Reverb settings
   //    (the defaults), analog off and on. The input is a B1-like voice: a
   //    detuned saw with a new note every 250 ms and a decaying envelope.
   //    Halfway through, every param moves (both kernels get the same push),
   //    so any cached coefficient that fails to invalidate shows up here.
   //    kReverbLegacyTol is the quality rule from docs/plans/perf: 0 while
   //    only exact changes are in, 1e-4 (-80 dBFS) once an approximate one
   //    (the fast LFO sine) is in. The measured max diff is always printed.
   {
      constexpr float kReverbLegacyTol = 1.0e-4f;
      const double sampleRate = 48000.0;
      const int blockSize = 256;
      const int totalFrames = 480000; // 10 s

      const EffectDef* reverbDef = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Reverb")
            reverbDef = &d;

      for (int analogOn = 0; analogOn < 2 && reverbDef; analogOn++)
      {
         AudioEffectNode node(*reverbDef);
         *node.ParamPtr("analog") = (float)analogOn;

         ReverbKernel live, legacy;
         live.PrepareToPlay(sampleRate, blockSize);
         legacy.PrepareToPlay(sampleRate, blockSize);
         live.PushParams(node, sampleRate);
         legacy.PushParams(node, sampleRate);

         float inChL[blockSize], inChR[blockSize];
         float* inChannels[2] = { inChL, inChR };
         AudioBuffer inBuf;
         inBuf.channels = inChannels;
         inBuf.numChannels = 2;
         inBuf.numFrames = blockSize;
         float liveL[blockSize], liveR[blockSize], oldL[blockSize], oldR[blockSize];
         float* liveCh[2] = { liveL, liveR };
         float* oldCh[2] = { oldL, oldR };
         AudioBuffer liveBuf, oldBuf;
         liveBuf.channels = liveCh;
         oldBuf.channels = oldCh;
         liveBuf.numChannels = oldBuf.numChannels = 2;
         liveBuf.numFrames = oldBuf.numFrames = blockSize;

         static const float kNotesHz[8] = { 110.0f, 164.8f, 130.8f, 196.0f, 146.8f, 220.0f, 123.5f, 174.6f };
         float phaseA = 0.0f, phaseB = 0.0f;
         float maxDiff = 0.0f;
         for (int offset = 0; offset < totalFrames; offset += blockSize)
         {
            if (offset == totalFrames / 2)
            {
               *node.ParamPtr("size") = 0.85f;
               *node.ParamPtr("decay") = 4.0f;
               *node.ParamPtr("damping") = 0.7f;
               *node.ParamPtr("predelay") = 37.0f;
               *node.ParamPtr("width") = 0.3f;
               live.PushParams(node, sampleRate);
               legacy.PushParams(node, sampleRate);
            }
            for (int k = 0; k < blockSize; k++)
            {
               const int n = offset + k;
               const int noteLen = 12000; // 250 ms
               const float hz = kNotesHz[(n / noteLen) % 8];
               const float env = std::exp(-(float)(n % noteLen) / 3000.0f);
               phaseA += hz / (float)sampleRate;
               phaseB += hz * 1.006f / (float)sampleRate;
               phaseA -= std::floor(phaseA);
               phaseB -= std::floor(phaseB);
               const float saw = (2.0f * phaseA - 1.0f) + (2.0f * phaseB - 1.0f);
               inChL[k] = 0.35f * env * saw;
               inChR[k] = 0.35f * env * (0.8f * saw + 0.2f * (2.0f * phaseA - 1.0f));
            }
            live.ProcessBlockSimd(inBuf, nullptr, liveBuf);
            legacy.ProcessBlockLegacy(inBuf, nullptr, oldBuf);
            for (int ch = 0; ch < 2; ch++)
               for (int k = 0; k < blockSize; k++)
                  maxDiff = std::max(maxDiff, std::fabs(liveBuf.channels[ch][k] - oldBuf.channels[ch][k]));
         }
         const bool ok = maxDiff <= kReverbLegacyTol;
         printf("DSPTEST reverb live vs pre-perf kernel, analog %s, 10 s: max diff %.3e (tol %.1e)  %s\n",
                analogOn ? "on" : "off", maxDiff, kReverbLegacyTol, ok ? "OK" : "FAIL");
         all &= ok;
      }
   }

   return all;
}

namespace WavetableShaperTest
{
   std::unique_ptr<AudioEffectNode> MakeNode(double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Wavetable Shaper")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      node->mix = 1.0f; // 100% wet so the fixture measures the shaped signal directly
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      return node;
   }
}

// Wavetable Shaper's DSP fixture (new-audio-node/SKILL.md §5) - the first of
// this family per-effect fixture (no per-effect fixture exists yet for
// Drive/Bitcrush/etc.), following RunReverbFixture/RunDelayFixture's shape:
// render the real node -> AudioEngine chain and assert against an analytic
// expectation, here WavetableShaperDsp::Shape itself (the same pure function
// the kernel and the visualizer both call).
bool RunWavetableShaperFixture()
{
   using namespace WavetableShaperTest;
   bool all = true;
   const double sampleRate = 44100.0;

   // 1) Node parity: the real node's wet output must match
   //    WavetableShaperDsp::Shape() followed by the same one-pole DC blocker
   //    DriveKernel.h declares (bias/table asymmetry both leak DC, so the
   //    blocker is always in the signal path), sample for sample, once the
   //    mailbox's smoothing ramp has had time to converge to its pushed
   //    targets. A slow sine sweep (not a constant, which the DC blocker
   //    would itself drive toward zero and defeat the comparison) exercises
   //    the whole -1..1 input range while giving both the node and the local
   //    reference identical history to build DC-blocker state from.
   {
      auto node = MakeNode(sampleRate);
      const int table = 1;    // Harmonics - plenty of curve shape to exercise
      const float position = 0.3f;
      const float driveDb = 6.0f;
      const float bias = 0.15f;
      const float smooth = 0.2f;
      *node->ParamPtr("table") = (float)table;
      *node->ParamPtr("position") = position;
      *node->ParamPtr("drive") = driveDb;
      *node->ParamPtr("bias") = bias;
      *node->ParamPtr("smooth") = smooth;
      *node->ParamPtr("output") = 0.0f;
      node->CookIfNeeded(1);

      // Warmup on silence: lets the mailbox's ~5ms one-pole smoother
      // converge to the pushed targets (independent of the input signal)
      // while leaving the DC blocker's state at its zero-input steady state
      // (0,0), which the local reference below also starts from.
      const int warmupSamples = (int)(0.5 * sampleRate);
      DelayTest::RunSamples(*node, warmupSamples, 1, [](int, int) { return 0.0f; }, nullptr);

      const int testSamples = 4000;
      const float sweepHz = 37.0f; // slow relative to sample rate, sweeps the full -1..1 range repeatedly
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(
         *node, testSamples, 1,
         [sweepHz](int i, int) { return sinf(2.0f * (float)M_PI * sweepHz * (float)i / (float)44100.0); }, &out);

      DcBlocker refBlocker; // starts zeroed, matching the node's post-warmup state
      float maxErr = 0.0f;
      for (int i = 0; i < testSamples; i++)
      {
         const float x = sinf(2.0f * (float)M_PI * sweepHz * (float)i / (float)44100.0);
         const float shaped = WavetableShaperDsp::Shape(x, table, position, driveDb, bias, smooth);
         const float expected = refBlocker.Process(shaped);
         maxErr = std::max(maxErr, std::fabs(expected - out[0][(size_t)i]));
      }
      const bool parityOk = maxErr < 1.0e-3f;
      printf("DSPTEST wavetable shaper node parity: max error vs WavetableShaperDsp::Shape %.6f  %s\n", maxErr,
             parityOk ? "OK" : "FAIL");
      all &= parityOk;
   }

   // 2) Finite and bounded across a sweep of every table x several positions
   //    x drive up to 24dB x bias +-1 - the sweep that catches a bad wrap or
   //    a null frame pointer. Called directly against the pure function,
   //    which is exactly what both the audio thread and the visualizer call.
   {
      bool boundedOk = true;
      int checked = 0;
      for (int table = 0; table < Wavetable::NumTables(); table++)
      {
         for (float position : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
         {
            for (float driveDb : { 0.0f, 12.0f, 24.0f })
            {
               for (float bias : { -1.0f, 0.0f, 1.0f })
               {
                  for (float smooth : { 0.0f, 0.5f, 1.0f })
                  {
                     for (int i = 0; i < 64; i++)
                     {
                        const float x = -1.0f + 2.0f * (float)i / 63.0f;
                        const float y = WavetableShaperDsp::Shape(x, table, position, driveDb, bias, smooth);
                        checked++;
                        // Shape(x) = RawShape(x) - RawShape(0), and RawShape is just a table
                        // read (bounded by the bank's own +-1 amplitude), so the true worst
                        // case is +-2, not +-1 - measured up to ~1.77 at the bias=+-1,
                        // drive=24dB corners where RawShape(x) and RawShape(0) land near
                        // opposite ends of the table. 2.05 leaves headroom for that corner
                        // while still catching a real blow-up (NaN/Inf or a bad wrap).
                        if (!std::isfinite(y) || std::fabs(y) > 2.05f)
                           boundedOk = false;
                     }
                  }
               }
            }
         }
      }
      printf("DSPTEST wavetable shaper bounded sweep: %d points across every table x position x drive x bias x "
             "smooth, finite and |y|<=2.05  %s\n",
             checked, boundedOk ? "OK" : "FAIL");
      all &= boundedOk;
   }

   // 3) bias != 0 produces no residual DC once the blocker settles: an
   //    asymmetric curve (bias offset) leaks a constant offset into the
   //    shaped signal, and the always-on DcBlocker must remove it.
   {
      auto node = MakeNode(sampleRate);
      *node->ParamPtr("table") = 3.0f; // Formant - a strongly asymmetric curve under bias
      *node->ParamPtr("position") = 0.6f;
      *node->ParamPtr("drive") = 10.0f;
      *node->ParamPtr("bias") = 0.6f;
      *node->ParamPtr("smooth") = 0.0f;
      *node->ParamPtr("output") = 0.0f;
      node->CookIfNeeded(2);

      const int warmupSamples = (int)(0.5 * sampleRate);
      DelayTest::RunSamples(*node, warmupSamples, 1, [](int, int) { return 0.0f; }, nullptr);

      const int testSamples = (int)(0.3 * sampleRate);
      const float toneHz = 220.0f;
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(
         *node, testSamples, 1,
         [toneHz](int i, int) { return sinf(2.0f * (float)M_PI * toneHz * (float)i / 44100.0f); }, &out);

      // Average over the final quarter, well after the ~8Hz blocker has
      // settled - a residual DC offset would show up as a nonzero mean.
      double sum = 0.0;
      const int windowStart = testSamples * 3 / 4;
      for (int i = windowStart; i < testSamples; i++)
         sum += out[0][(size_t)i];
      const double meanDc = sum / (double)(testSamples - windowStart);
      const bool dcOk = std::fabs(meanDc) < 0.01;
      printf("DSPTEST wavetable shaper DC blocking: mean %.5f over settled window (bias 0.6)  %s\n", meanDc,
             dcOk ? "OK" : "FAIL");
      all &= dcOk;
   }

   // 4) Increasing smooth strictly reduces high-frequency energy in the
   //    output for a fixed input - the claim the control makes (a lower mip
   //    level holds strictly fewer harmonics). Measured directly off the
   //    pure function via a first-difference energy proxy over a ramp that
   //    sweeps the whole curve repeatedly, which is sensitive to exactly the
   //    high-order wiggle a coarser mip level removes.
   {
      const int table = 1; // Harmonics - has real high-order content to remove
      const float position = 0.5f, driveDb = 12.0f, bias = 0.0f;
      const float smoothSteps[] = { 0.0f, 0.33f, 0.66f, 1.0f };
      float prevEnergy = -1.0f;
      bool monotoneOk = true;
      const int kNumPoints = 2000;
      for (float smooth : smoothSteps)
      {
         std::vector<float> y(kNumPoints);
         for (int i = 0; i < kNumPoints; i++)
         {
            // Two full triangle sweeps of the curve's domain, so the
            // first-difference series captures the curve's own wiggle
            // rather than a slow ramp's near-zero derivative.
            const float phase = fmodf((float)i / (float)kNumPoints * 4.0f, 2.0f);
            const float x = phase <= 1.0f ? (-1.0f + 2.0f * phase) : (3.0f - 2.0f * phase);
            y[i] = WavetableShaperDsp::Shape(x, table, position, driveDb, bias, smooth);
         }
         double energy = 0.0;
         for (int i = 1; i < kNumPoints; i++)
         {
            const double d = (double)y[i] - (double)y[i - 1];
            energy += d * d;
         }
         printf("DSPTEST wavetable shaper smooth=%.2f HF-energy proxy %.6f\n", smooth, energy);
         if (prevEnergy >= 0.0 && !(energy < prevEnergy))
            monotoneOk = false;
         prevEnergy = (float)energy;
      }
      printf("DSPTEST wavetable shaper smooth reduces HF energy monotonically  %s\n", monotoneOk ? "OK" : "FAIL");
      all &= monotoneOk;
   }

   return all;
}
}
