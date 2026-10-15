// Part of src/main.cpp, included there in order (one translation unit).
// audio node sweep discovery and the INFINITE_DSPTEST fixtures

// ================================================== Audio node sweep discovery
//
// Shared by INFINITE_AUDIOPARAMSWEEPTEST and INFINITE_AUDIOTEARDOWNSWEEPTEST:
// every node type is read out of NodeFactory - the same registry
// RegisterNodes() populates - rather than a hand-maintained list, so a node
// added later (any remaining row of docs/plans/audio/README.md §3) is
// covered by both sweeps without anyone editing this file. Which of a node's
// interfaces it answers (IAudioSource, INoteSource, AudioNodeForNotePorts(),
// AudioInputSlot/NoteInputSlot) is probed the same way InputCountFor already
// probes AudioInputSlot/NoteInputSlot generically for pin counts - see
// .claude/skills/new-audio-node/SKILL.md §3's step 9 note that audio and note
// pins share one slot index space.
struct AudioNodeShape
{
   bool isAudioSource = false;
   bool isNoteSource = false;
   bool isModulator = false;
   bool hasNotePorts = false; // AudioNodeForNotePorts() != nullptr (NoteToCVNode)
   int audioInputSlots = 0;   // highest AudioInputSlot(i) != nullptr index + 1
   int noteInputSlots = 0;    // highest NoteInputSlot(i) != nullptr index + 1
   int firstNoteInputSlot = -1; // actual slot index of the first live NoteInputSlot(); -1 if none.
                                 // Nodes with note-only input (no AudioInputSlot at all) can have
                                 // their note pin start at a nonzero slot - e.g. WaveTerrainNode and
                                 // ImageSpectralSynthNode both only answer NoteInputSlot(1). Callers
                                 // that need to actually connect a cable must use this, not slot 0.

   // Does this node own (or reach) an AudioNode at all? AUDIOPARAMSWEEPTEST's
   // "reaches the audio thread" check needs one to drive; a node with none
   // (Audio Out - a pure topology terminal, no ProcessBlock of its own) has
   // nothing to test there.
   bool HasAudioNode() const { return isAudioSource || isNoteSource || hasNotePorts; }

   // Does this node touch the audio/note cable graph at all, as either side
   // of a cable? AUDIOTEARDOWNSWEEPTEST's teardown invariant applies to every
   // one of these, including pure terminals like Audio Out.
   bool ParticipatesInAudioGraph() const
   {
      return isAudioSource || isNoteSource || hasNotePorts || audioInputSlots > 0 || noteInputSlots > 0;
   }
};

inline AudioNodeShape ProbeAudioNodeShape(INode* node)
{
   AudioNodeShape shape;
   shape.isAudioSource = dynamic_cast<IAudioSource*>(node) != nullptr;
   shape.isNoteSource = dynamic_cast<INoteSource*>(node) != nullptr;
   shape.isModulator = dynamic_cast<IModulator*>(node) != nullptr;
   shape.hasNotePorts = node->AudioNodeForNotePorts() != nullptr;
   // Scan the full slot range rather than breaking on the first empty slot -
   // a node's audio and note pins don't have to start contiguously at index 0
   // (see firstNoteInputSlot comment above). This probe runs once per node
   // type on a throwaway instance, so the extra iterations are free.
   for (int i = 0; i < kAudioMaxNodeInputs; i++)
   {
      if (node->AudioInputSlot(i) != nullptr)
         shape.audioInputSlots = i + 1;
      if (node->NoteInputSlot(i) != nullptr)
      {
         shape.noteInputSlots = i + 1;
         if (shape.firstNoteInputSlot < 0)
            shape.firstNoteInputSlot = i;
      }
   }
   return shape;
}

struct AudioSweepCandidate
{
   std::string name;
   std::string category;
   AudioNodeShape shape;
};

// Walks every registered node type, constructs one throwaway instance of
// each to probe its shape, and keeps the ones `include` accepts.
inline std::vector<AudioSweepCandidate> DiscoverAudioSweepCandidates(
   const std::function<bool(const AudioNodeShape&)>& include)
{
   std::vector<AudioSweepCandidate> out;
   for (const std::string& category : NodeFactory::Instance().GetCategories())
   {
      for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
      {
         std::unique_ptr<INode> probe(NodeFactory::Instance().MakeNode(name));
         if (!probe)
            continue;
         AudioNodeShape shape = ProbeAudioNodeShape(probe.get());
         if (include(shape))
            out.push_back({ name, category, shape });
      }
   }
   return out;
}

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

static bool RunGainFixture()
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

static bool RunFilterFixture()
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
static bool RunOscWaveformFixture()
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
static bool RunWavetableFixture()
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

   return all;
}

// Envelope (ModulatorNodes.h) since 03-envelope-to-shaper.md: no longer a
// note-triggered generator, a mod-in -> mod-out ADSR shaper. The gate is the
// "in" signal crossing `threshold`; output collapses toward 0.5 as the
// envelope level falls (ModDepthNode's convention). Value01() is the only
// public surface, so every check below reconstructs the internal envelope
// level algebraically from out01 = 0.5 + (in01-0.5)*envLevel rather than
// reaching into the node's private stage/level fields.
static bool RunEnvelopeFixture()
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

static bool RunVoiceStealFixture()
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
static bool RunMusicTimeFixture()
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

static bool RunAudioFilterFixture()
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

static bool RunDynamicsFixture()
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

// Delay's exit criterion, same rig shape as DynamicsTest above (real
// AudioEffectNode -> AudioEffectRuntime -> DelayKernel pipeline, not the
// kernel's internals in isolation).
namespace DelayTest
{
   std::unique_ptr<AudioEffectNode> MakeDelayNode(bool bounce, double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Delay")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      *node->ParamPtr("bounce") = bounce ? 1.0f : 0.0f;
      node->mix = 1.0f; // 100% wet so the fixture measures the delayed tap directly, not a dry/wet blend
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }

   // Runs `numSamples` frames of `numChannels`, one sample at a time (block
   // size 1) through the node, capturing every channel's output.
   template <typename SampleFn>
   void RunSamples(AudioEffectNode& node, int numSamples, int numChannels, SampleFn getSample,
                    std::vector<std::vector<float>>* outSamples)
   {
      AudioNode* audioNode = node.GetAudioNode();
      float inVal[2] = { 0.0f, 0.0f };
      float outVal[2] = { 0.0f, 0.0f };
      float* inPtrs[2] = { &inVal[0], &inVal[1] };
      float* outPtrs[2] = { &outVal[0], &outVal[1] };
      AudioBuffer inBuffer;
      inBuffer.channels = inPtrs;
      inBuffer.numChannels = numChannels;
      inBuffer.numFrames = 1;
      AudioBuffer outBuffer;
      outBuffer.channels = outPtrs;
      outBuffer.numChannels = numChannels;
      outBuffer.numFrames = 1;
      const AudioBuffer* inputs[1] = { &inBuffer };
      if (outSamples != nullptr)
         outSamples->assign(numChannels, {});
      for (int i = 0; i < numSamples; i++)
      {
         for (int ch = 0; ch < numChannels; ch++)
            inVal[ch] = getSample(i, ch);
         audioNode->ProcessBlock(inputs, 1, outBuffer);
         if (outSamples != nullptr)
            for (int ch = 0; ch < numChannels; ch++)
               (*outSamples)[ch].push_back(outVal[ch]);
      }
   }
}

static bool RunDelayFixture()
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

static bool RunReverbFixture()
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
static bool RunWavetableShaperFixture()
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

// Sampler's DSP fixture (new-audio-node/SKILL.md §5): loads a real WAV
// through the actual Platform::DecodeAudioFileToBuffer path (not a synthetic
// in-memory buffer, so the decoder itself is exercised too), triggers
// playback via a real NoteEventQueue the way a note cable would, and asserts
// each param produces a measurable difference in the rendered samples - the
// same signature-diffing approach AUDIOPARAMSWEEPTEST uses, but against a
// Sampler that actually has a file loaded, which the generic sweep's bare
// throwaway instance never does (see driver.sh's Sampler baseline note for
// why the sweep alone reports [FAIL] on this node).
static bool RunSamplerFixture()
{
   const int numFrames = 4410;    // 0.1s @ 44100 Hz
   const int sampleRate = 44100;
   std::vector<int16_t> pcm(numFrames);
   for (int i = 0; i < numFrames; i++)
   {
      const float t = (float)i / (float)(numFrames - 1); // 0..1 ramp
      pcm[i] = (int16_t)((t * 2.0f - 1.0f) * 32000.0f);
   }

   const std::string path = "/tmp/infinite_sampler_fixture.wav";
   {
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
   }

   bool ok = true;

   struct Params
   {
      float pitch = 0.0f, finetune = 0.0f, speed = 1.0f, volume = 0.8f, start = 0.0f, end = 1.0f;
      bool loop = false, reverse = false, pingpong = false;
   };

   // Triggers one note-on through a real NoteEventQueue (the same path a
   // connected note cable drives) and renders `frames` samples.
   auto trigger = [&](const Params& p, int frames) -> std::vector<float>
   {
      SamplerNode node;
      if (!node.LoadFile(path))
      {
         ok = false;
         return {};
      }
      node.pitch = p.pitch;
      node.finetune = p.finetune;
      node.speed = p.speed;
      node.volume = p.volume;
      node.start = p.start;
      node.end = p.end;
      node.loop = p.loop;
      node.reverse = p.reverse;
      node.pingpong = p.pingpong;
      node.CookIfNeeded(1);

      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, frames);
      node.CookIfNeeded(2);

      NoteEventQueue queue;
      const int cursor = queue.RegisterConsumer();
      an->SetNoteInbox(&queue, cursor);
      NoteEvent on;
      on.note = 60;
      on.velocity = 1.0f;
      on.isNoteOn = true;
      on.frameOffset = 0;
      queue.Push(on);

      std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = frames;
      an->ProcessBlock(nullptr, 0, buf);
      return l;
   };

   auto differs = [](const std::vector<float>& a, const std::vector<float>& b)
   {
      if (a.size() != b.size())
         return true;
      for (size_t i = 0; i < a.size(); i++)
         if (std::fabs(a[i] - b[i]) > 1e-4f)
            return true;
      return false;
   };

   Params base;
   const auto baseline = trigger(base, 64);
   Params withPitch = base; withPitch.pitch = 12.0f;        // +1 octave: rate doubles
   Params withFinetune = base; withFinetune.finetune = 50.0f; // +50c: same direction as pitch, smaller
   Params withSpeed = base; withSpeed.speed = 2.0f;          // varispeed: rate doubles like +1 octave
   Params withStart = base; withStart.start = 0.5f;          // trigger halfway through the ramp
   Params withVolume = base; withVolume.volume = 0.2f;       // quarter volume

   const auto pitched = trigger(withPitch, 64);
   const auto finetuned = trigger(withFinetune, 64);
   const auto sped = trigger(withSpeed, 64);
   const auto started = trigger(withStart, 64);
   const auto quiet = trigger(withVolume, 64);

   if (baseline.empty() || pitched.empty() || finetuned.empty() || sped.empty() || started.empty() || quiet.empty())
   {
      printf("SAMPLERTEST BUG (fixture failed to load its own test file)\n");
      return false;
   }
   if (!differs(baseline, pitched)) { printf("SAMPLERTEST pitch param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, finetuned)) { printf("SAMPLERTEST finetune param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, sped)) { printf("SAMPLERTEST speed param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, started)) { printf("SAMPLERTEST start param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, quiet)) { printf("SAMPLERTEST volume param had no effect FAIL\n"); ok = false; }

   // Reverse: triggering with reverse=true starts at `end` and plays toward
   // `start`, so on this monotonic ramp its first samples should sit near
   // the ramp's *top* rather than its bottom - the opposite of forward.
   {
      Params fwd = base;
      Params rev = base; rev.reverse = true;
      const auto f = trigger(fwd, 4);
      const auto r = trigger(rev, 4);
      if (f.empty() || r.empty() || !(r[0] > f[0]))
      {
         printf("SAMPLERTEST reverse param had no effect FAIL\n");
         ok = false;
      }
   }

   // Loop vs. end vs. ping-pong: render well past the sample's natural end
   // and check each shapes the tail differently - loop wraps back to
   // silence-then-ramp-up again, ping-pong instead keeps descending from
   // wherever it bounced, so the two must diverge past the wrap point.
   {
      auto renderWholeFile = [&](const Params& p) -> std::vector<float>
      {
         SamplerNode node;
         node.LoadFile(path);
         node.pitch = p.pitch; node.finetune = p.finetune; node.speed = p.speed; node.volume = p.volume;
         node.start = p.start; node.end = p.end; node.loop = p.loop; node.reverse = p.reverse;
         node.pingpong = p.pingpong;
         node.CookIfNeeded(1);
         AudioNode* an = node.GetAudioNode();
         const int frames = numFrames + 200;
         an->PrepareToPlay((double)sampleRate, frames);
         node.CookIfNeeded(2);
         NoteEventQueue queue;
         const int cursor = queue.RegisterConsumer();
         an->SetNoteInbox(&queue, cursor);
         NoteEvent on;
         on.note = 60;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         queue.Push(on);
         std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = frames;
         an->ProcessBlock(nullptr, 0, buf);
         return l;
      };

      Params looped = base; looped.loop = true;
      Params oneShot = base;
      Params pingponged = base; pingponged.loop = true; pingponged.pingpong = true;

      const auto loopedTail = renderWholeFile(looped);
      const auto oneShotTail = renderWholeFile(oneShot);
      const auto pingpongTail = renderWholeFile(pingponged);
      if (!(std::fabs(loopedTail[numFrames + 100]) > 0.05f && std::fabs(oneShotTail[numFrames + 100]) < 0.05f))
      {
         printf("SAMPLERTEST loop param had no effect (looped=%.3f oneshot=%.3f) FAIL\n",
                loopedTail[numFrames + 100], oneShotTail[numFrames + 100]);
         ok = false;
      }
      if (!differs(loopedTail, pingpongTail))
      {
         printf("SAMPLERTEST pingpong param had no effect FAIL\n");
         ok = false;
      }
   }

   // end: shrinking the loop range must change where playback wraps, so a
   // looped voice with end=0.5 must sound different past the halfway point
   // than the full-range loop above once both have wrapped at least once.
   {
      Params fullLoop = base; fullLoop.loop = true;
      Params shortLoop = base; shortLoop.loop = true; shortLoop.end = 0.5f;
      const auto full = trigger(fullLoop, numFrames / 2 + 50);
      const auto shortR = trigger(shortLoop, numFrames / 2 + 50);
      if (!differs(full, shortR)) { printf("SAMPLERTEST end param had no effect FAIL\n"); ok = false; }
   }

   // Record: arm recording, feed one block of a known synthetic tone
   // through the audio input pin (slot 1 - see AudioSamplerNode::
   // ProcessBlock's comment on why it's 1, not 0), stop, and confirm the
   // captured audio became the loaded buffer.
   {
      SamplerNode node;
      node.StartRecording();
      if (!node.IsRecording())
      {
         printf("SAMPLERTEST record param had no effect (StartRecording didn't arm) FAIL\n");
         ok = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay((double)sampleRate, 128);
         std::vector<float> toneL(128), toneR(128);
         for (int i = 0; i < 128; i++)
            toneL[i] = toneR[i] = sinf((float)i * 0.3f);
         float* srcChans[2] = { toneL.data(), toneR.data() };
         AudioBuffer srcBuf;
         srcBuf.channels = srcChans;
         srcBuf.numChannels = 2;
         srcBuf.numFrames = 128;

         std::vector<float> outL(128, 0.0f), outR(128, 0.0f);
         float* outChans[2] = { outL.data(), outR.data() };
         AudioBuffer outBuf;
         outBuf.channels = outChans;
         outBuf.numChannels = 2;
         outBuf.numFrames = 128;

         const AudioBuffer* inputs[2] = { nullptr, &srcBuf };
         an->ProcessBlock(inputs, 2, outBuf);

         node.StopRecording();
         if (node.FileName() != "recorded audio" || node.waveformCacheCount <= 0 || node.FilePath().empty())
         {
            printf("SAMPLERTEST record param had no effect (nothing captured) FAIL\n");
            ok = false;
         }
         else
         {
            SamplerNode reloaded;
            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&node, params);
            Patch::LoadParams(&reloaded, params);
            reloaded.ReloadFromPath();
            if (reloaded.waveformCacheCount <= 0 || reloaded.FilePath().empty())
            {
               printf("SAMPLERTEST record patch reload failed FAIL\n");
               ok = false;
            }
         }
      }
   }

   remove(path.c_str());

   printf("%s\n", ok ? "SAMPLERTEST OK" : "SAMPLERTEST FAIL");
   return ok;
}

// PaulStretch's DSP fixture: loads a real WAV through the actual
// Platform::DecodeAudioFileToBuffer path, triggers playback via a real
// NoteEventQueue, and asserts that stretch, pitch shift, fine tune, frequency
// shift, unison detune, and volume produce finite, valid audio outputs.
static bool RunPaulStretchFixture()
{
   const int numFrames = 44100; // 1.0s @ 44100 Hz
   const int sampleRate = 44100;
   std::vector<int16_t> pcm(numFrames);
   for (int i = 0; i < numFrames; i++)
   {
      const float t = (float)i / (float)(numFrames - 1);
      pcm[i] = (int16_t)(std::sin(t * 440.0f * 6.283185f) * 30000.0f);
   }

   const std::string path = "/tmp/infinite_paulstretch_fixture.wav";
   {
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
   }

   bool ok = true;

   auto trigger = [&](float stretch, int winIdx, float pitch, float fine, float freq, int uni, float detune, float vol, int frames) -> std::vector<float>
   {
      PaulStretchNode node;
      if (!node.LoadFile(path))
      {
         ok = false;
         return {};
      }
      node.stretch = stretch;
      node.windowSizeIndex = winIdx;
      node.pitchShift = pitch;
      node.fineTune = fine;
      node.freqShift = freq;
      node.unison = uni;
      node.detune = detune;
      node.volume = vol;
      node.CookIfNeeded(1);

      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, 2048);

      node.TriggerPreview(0.0f);
      node.CookIfNeeded(2);

      std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = frames;
      for (int block = 0; block < 4; ++block)
         an->ProcessBlock(nullptr, 0, buf);
      return l;
   };

   // Render baseline with 2048-window size (index 0)
   const auto baseline = trigger(8.0f, 0, 0.0f, 0.0f, 0.0f, 1, 10.0f, 0.8f, 1024);
   const auto pitched = trigger(8.0f, 0, 7.0f, 0.0f, 0.0f, 1, 10.0f, 0.8f, 1024);
   const auto freqShifted = trigger(8.0f, 0, 0.0f, 0.0f, 200.0f, 1, 10.0f, 0.8f, 1024);
   const auto unisonSound = trigger(8.0f, 0, 0.0f, 0.0f, 0.0f, 4, 30.0f, 0.8f, 1024);

   if (baseline.empty() || pitched.empty() || freqShifted.empty() || unisonSound.empty())
   {
      printf("PAULSTRETCHTEST BUG (fixture failed to load test file)\n");
      ok = false;
   }
   else
   {
      for (float s : baseline)
      {
         if (!std::isfinite(s)) { printf("PAULSTRETCHTEST baseline produced non-finite sample FAIL\n"); ok = false; break; }
      }
      for (float s : pitched)
      {
         if (!std::isfinite(s)) { printf("PAULSTRETCHTEST pitched produced non-finite sample FAIL\n"); ok = false; break; }
      }

      // Record test: verify StartRecording arms and captures audio, writes WAV to disk, and reloads via Patch / ReloadFromPath()
      {
         PaulStretchNode recNode;
         recNode.StartRecording();
         if (!recNode.IsRecording())
         {
            printf("PAULSTRETCHTEST record arm failed FAIL\n");
            ok = false;
         }
         else
         {
            AudioNode* an = recNode.GetAudioNode();
            an->PrepareToPlay((double)sampleRate, 128);
            std::vector<float> tone(128);
            for (int i = 0; i < 128; i++)
               tone[i] = sinf((float)i * 0.3f);
            float* srcChans[1] = { tone.data() };
            AudioBuffer srcBuf;
            srcBuf.channels = srcChans;
            srcBuf.numChannels = 1;
            srcBuf.numFrames = 128;

            std::vector<float> outL(128, 0.0f), outR(128, 0.0f);
            float* outChans[2] = { outL.data(), outR.data() };
            AudioBuffer outBuf;
            outBuf.channels = outChans;
            outBuf.numChannels = 2;
            outBuf.numFrames = 128;

            const AudioBuffer* inputs[1] = { &srcBuf };
            an->ProcessBlock(inputs, 1, outBuf);

            recNode.StopRecording();
            if (recNode.waveformCacheCount <= 0 || recNode.FileName() != "recording" || recNode.FilePath().empty())
            {
               printf("PAULSTRETCHTEST recording buffer capture or file path failed FAIL\n");
               ok = false;
            }
            else
            {
               PaulStretchNode reloaded;
               recNode.position = 0.42f;
               std::vector<std::pair<std::string, std::string>> params;
               Patch::SaveParams(&recNode, params);
               Patch::LoadParams(&reloaded, params);
               reloaded.ReloadFromPath();
               if (reloaded.waveformCacheCount <= 0 || reloaded.FilePath().empty() || std::abs(reloaded.position - 0.42f) > 1e-4f)
               {
                  printf("PAULSTRETCHTEST record patch reload failed FAIL\n");
                  ok = false;
               }
            }
         }
      }
   }

   remove(path.c_str());
   printf("%s\n", ok ? "PAULSTRETCHTEST OK" : "PAULSTRETCHTEST FAIL");
   return ok;
}

// Granular Synthesizer's DSP fixture: loads a real WAV, triggers granular playback,
// and asserts that position, scan, grain length, density, spray, pitch shift,
// window shapes, and volume produce finite, valid audio outputs.
static bool RunGranularFixture()
{
   const int numFrames = 44100; // 1.0s @ 44100 Hz
   const int sampleRate = 44100;
   std::vector<int16_t> pcm(numFrames);
   for (int i = 0; i < numFrames; i++)
   {
      const float t = (float)i / (float)(numFrames - 1);
      pcm[i] = (int16_t)(std::sin(t * 440.0f * 6.283185f) * 30000.0f);
   }

   const std::string path = "/tmp/infinite_granular_fixture.wav";
   {
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
   }

   bool ok = true;

   auto trigger = [&](float pos, float scan, float len, float dens, float spray, float pitch, int winShape, int frames) -> std::vector<float>
   {
      GranularNode node;
      if (!node.LoadFile(path))
      {
         ok = false;
         return {};
      }
      node.position = pos;
      node.scan = scan;
      node.grainLength = len;
      node.density = dens;
      node.randomPos = spray;
      node.pitchShift = pitch;
      node.windowShape = winShape;
      node.volume = 0.8f;
      node.CookIfNeeded(1);

      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, 2048);
      node.CookIfNeeded(2);

      std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = frames;
      for (int block = 0; block < 4; ++block)
         an->ProcessBlock(nullptr, 0, buf);
      return l;
   };

   const auto baseline = trigger(0.2f, 1.0f, 50.0f, 30.0f, 0.05f, 0.0f, 0, 1024);
   const auto pitched = trigger(0.2f, 1.0f, 50.0f, 30.0f, 0.05f, 7.0f, 0, 1024);
   const auto gaussianWin = trigger(0.2f, 0.0f, 100.0f, 50.0f, 0.2f, 0.0f, 1, 1024);

   if (baseline.empty() || pitched.empty() || gaussianWin.empty())
   {
      printf("GRANULARTEST BUG (fixture failed to load test file)\n");
      ok = false;
   }
   else
   {
      for (float s : baseline)
      {
         if (!std::isfinite(s)) { printf("GRANULARTEST baseline produced non-finite sample FAIL\n"); ok = false; break; }
      }
      for (float s : pitched)
      {
         if (!std::isfinite(s)) { printf("GRANULARTEST pitched produced non-finite sample FAIL\n"); ok = false; break; }
      }
      for (float s : gaussianWin)
      {
         if (!std::isfinite(s)) { printf("GRANULARTEST gaussianWin produced non-finite sample FAIL\n"); ok = false; break; }
      }

      // Record test: verify StartRecording arms RequiresAudioProcessing and captures audio on input slot 0
      {
         GranularNode recNode;
         recNode.StartRecording();
         if (!recNode.IsRecording() || !recNode.RequiresAudioProcessing())
         {
            printf("GRANULARTEST record arm failed FAIL\n");
            ok = false;
         }
         else
         {
            AudioNode* an = recNode.GetAudioNode();
            an->PrepareToPlay((double)sampleRate, 128);
            std::vector<float> tone(128);
            for (int i = 0; i < 128; i++)
               tone[i] = sinf((float)i * 0.3f);
            float* srcChans[1] = { tone.data() };
            AudioBuffer srcBuf;
            srcBuf.channels = srcChans;
            srcBuf.numChannels = 1;
            srcBuf.numFrames = 128;

            std::vector<float> outL(128, 0.0f), outR(128, 0.0f);
            float* outChans[2] = { outL.data(), outR.data() };
            AudioBuffer outBuf;
            outBuf.channels = outChans;
            outBuf.numChannels = 2;
            outBuf.numFrames = 128;

            const AudioBuffer* inputs[1] = { &srcBuf };
            an->ProcessBlock(inputs, 1, outBuf);

            recNode.StopRecording();
            if (recNode.RequiresAudioProcessing())
            {
               printf("GRANULARTEST RequiresAudioProcessing should be false after stop FAIL\n");
               ok = false;
            }
            if (recNode.waveformCacheCount <= 0 || recNode.FileName() != "recording" || recNode.FilePath().empty())
            {
               printf("GRANULARTEST recording buffer capture failed FAIL\n");
               ok = false;
            }
            else
            {
               GranularNode reloaded;
               std::vector<std::pair<std::string, std::string>> params;
               Patch::SaveParams(&recNode, params);
               Patch::LoadParams(&reloaded, params);
               reloaded.ReloadFromPath();
               if (reloaded.waveformCacheCount <= 0 || reloaded.FilePath().empty())
               {
                  printf("GRANULARTEST record patch reload failed FAIL\n");
                  ok = false;
               }
            }
         }
      }
   }

   remove(path.c_str());
   printf("%s\n", ok ? "GRANULARTEST OK" : "GRANULARTEST FAIL");
   return ok;
}

// Drum Sequencer's DSP fixture (drum-sequencer-prompt.md §6). Drives the
// real DrumSequencerNode -> AudioDrumSequencerNode chain directly (no
// AudioEngine/topology - same shape as RunSamplerFixture), manually pumping
// Transport::Instance() into its audio-driven mode so step scheduling reads
// the exact same clock a real callback would.
namespace DrumSeqTest
{
   // A short, loud "click" sample: strong for kClickFrames, silent after -
   // gives every trigger a clean, separable onset regardless of decay/pitch.
   constexpr int kClickFrames = 200;
   constexpr int kSustainFrames = 20000; // for tests that need a voice to still be sounding well after another lane's hit

   std::string WriteClickWav(const std::string& path, int totalFrames, int sampleRate)
   {
      std::vector<int16_t> pcm(totalFrames, 0);
      for (int i = 0; i < totalFrames && i < kClickFrames; i++)
         pcm[i] = (int16_t)(0.9f * 32000.0f);
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
      return path;
   }

   // Constant-amplitude for its whole length - unlike WriteClickWav, this
   // stays loud the entire buffer, so a lane playing it is still audibly
   // sounding for as long as the test needs (choke: "still ringing when the
   // choke should cut it"; decay: a reference that only the decay knob
   // shapes, not the sample's own natural envelope).
   std::string WriteSustainedWav(const std::string& path, int totalFrames, int sampleRate)
   {
      std::vector<int16_t> pcm(totalFrames, (int16_t)(0.9f * 32000.0f));
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
      return path;
   }

   // Renders `totalFrames` in blocks of `blockSize`, calling
   // Transport::AdvanceAudioClock before each block (mirroring
   // AudioEngine::Process's own ordering) and returning the summed L channel.
   std::vector<float> Render(DrumSequencerNode& node, int totalFrames, int blockSize, int sampleRate,
                              NoteEventQueue* inbox = nullptr)
   {
      // Cook-then-prepare, matching every other fixture in this file:
      // PrepareToPlay's SetImmediate seeds the mailbox smoothers from
      // whatever's already in the lane atomics, so those need to hold real
      // values (not construction defaults) by the time it runs. A second
      // cook right after, with a different frameId so the memoized
      // CookIfNeeded actually re-runs, lets the decay-coefficient push
      // self-heal against PrepareToPlay's now-correct sample rate (see
      // mLastCoeffSampleRate) instead of keeping whatever it guessed before
      // the rate was known.
      node.CookIfNeeded(1);
      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, blockSize);
      node.CookIfNeeded(2);
      if (inbox != nullptr)
         an->SetNoteInbox(inbox, inbox->RegisterConsumer());

      std::vector<float> out;
      out.reserve(totalFrames);
      int rendered = 0;
      while (rendered < totalFrames)
      {
         const int n = std::min(blockSize, totalFrames - rendered);
         Transport::Instance().AdvanceAudioClock(n);
         std::vector<float> l(n, 0.0f), r(n, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = n;
         an->ProcessBlock(nullptr, 0, buf);
         out.insert(out.end(), l.begin(), l.end());
         rendered += n;
      }
      return out;
   }

   // First-crossing onset detector: scans forward from `from` for the first
   // index whose |sample| exceeds `thresh`, having been below it just prior.
   int FindOnset(const std::vector<float>& buf, int from, float thresh = 0.15f)
   {
      for (int i = std::max(1, from); i < (int)buf.size(); i++)
         if (std::fabs(buf[i]) > thresh && std::fabs(buf[i - 1]) <= thresh)
            return i;
      return -1;
   }

   bool AllFinite(const std::vector<float>& buf)
   {
      for (float v : buf)
         if (!std::isfinite(v))
            return false;
      return true;
   }
}

static bool RunDrumSequencerFixture()
{
   using namespace DrumSeqTest;
   bool ok = true;

   const int sampleRate = 48000;
   const int blockSize = 256;
   Transport& transport = Transport::Instance();
   const float savedBpm = transport.Tempo();
   const bool savedPlaying = transport.IsPlaying();
   const int savedNum = transport.TimeSigNumerator();
   const int savedDen = transport.TimeSigDenominator();

   const std::string shortClickPath = WriteClickWav("/tmp/infinite_drumseq_click.wav", 4000, sampleRate);
   const std::string longClickPath = WriteSustainedWav("/tmp/infinite_drumseq_sustain.wav", kSustainFrames, sampleRate);

   auto resetTransport = [&]()
   {
      transport.SetTempo(120.0f);
      transport.SetTimeSignature(4, 4);
      transport.SetPlaying(true);
      transport.Rewind();
      transport.NotifyAudioEngineStarted((double)sampleRate);
   };

   auto makeNode = [&](const std::string& clickPath) -> std::unique_ptr<DrumSequencerNode>
   {
      auto node = std::make_unique<DrumSequencerNode>();
      node->rate = MusicTime::kSixteenth;
      node->numSteps = 8;
      node->LoadFileToLane(0, clickPath);
      return node;
   };

   // 1) Step timing: lane 0 step 0 only, 120 BPM 1/16 -> hits every
   // sampleRate*0.125 frames (a 16th note at 120bpm), and the first hit is
   // not quantised to a block boundary.
   {
      resetTransport();
      auto node = makeNode(shortClickPath);
      node->numSteps = 1; // a single-step pattern repeats every 16th note, not once per bar
      node->stepVel[0][0] = 0.8f;
      const auto buf = Render(*node, sampleRate * 2, blockSize, sampleRate); // 2s -> ~16 hits
      const int expectedSpacing = (int)std::lround(sampleRate * 0.125);
      int onset1 = FindOnset(buf, 0);
      int onset2 = onset1 >= 0 ? FindOnset(buf, onset1 + expectedSpacing / 2) : -1;
      if (onset2 < 0 && getenv("INFINITE_DRUMSEQ_DEBUG") != nullptr)
      {
         float maxAbs = 0.0f;
         int maxIdx = -1;
         for (int i = onset1 + expectedSpacing / 2; i < (int)buf.size() && i < onset1 + expectedSpacing * 2; i++)
            if (std::fabs(buf[i]) > maxAbs) { maxAbs = std::fabs(buf[i]); maxIdx = i; }
         printf("DRUMSEQTEST DEBUG: max abs in search window = %.4f at idx %d (buf.size=%zu)\n", maxAbs, maxIdx,
                buf.size());
      }
      const bool spacingOk = onset1 >= 0 && onset2 >= 0 && std::abs((onset2 - onset1) - expectedSpacing) <= 1;
      const bool notBlockQuantised = onset1 >= 0 && (onset1 % blockSize) != 0;
      if (!spacingOk)
      {
         printf("DRUMSEQTEST step timing FAIL (onset1=%d onset2=%d expected spacing=%d)\n", onset1, onset2,
                expectedSpacing);
         ok = false;
      }
      else if (!notBlockQuantised)
      {
         printf("DRUMSEQTEST step timing FAIL (first hit landed exactly on a block boundary - suspiciously quantised)\n");
         ok = false;
      }
      else
      {
         printf("DRUMSEQTEST step timing OK\n");
      }
   }

   // 2) Swing: at swing=0.5, odd steps land 25%% of a step late; even steps
   // (including step 0) are unmoved. At swing=0, output is bit-identical to
   // a run that never touches swing.
   {
      resetTransport();
      auto nodeA = makeNode(shortClickPath);
      nodeA->stepVel[0][0] = 0.8f;
      nodeA->stepVel[0][1] = 0.8f; // odd step - swings
      nodeA->swing = 0.5f;
      const auto swungBuf = Render(*nodeA, sampleRate, blockSize, sampleRate);

      resetTransport();
      auto nodeB = makeNode(shortClickPath);
      nodeB->stepVel[0][0] = 0.8f;
      nodeB->stepVel[0][1] = 0.8f;
      nodeB->swing = 0.0f;
      const auto straightBuf = Render(*nodeB, sampleRate, blockSize, sampleRate);

      const int stepFrames = (int)std::lround(sampleRate * 0.125);
      const int onset0Swung = FindOnset(swungBuf, 0);
      const int onset1Swung = FindOnset(swungBuf, onset0Swung + stepFrames / 2);
      const int onset0Straight = FindOnset(straightBuf, 0);
      const int onset1Straight = FindOnset(straightBuf, onset0Straight + stepFrames / 2);

      const bool step0Unmoved = onset0Swung >= 0 && onset0Straight >= 0 && std::abs(onset0Swung - onset0Straight) <= 1;
      // swing=0.5 delays the odd step by swing*0.5 = 0.25 of a step, on top
      // of its normal 1-step spacing from step 0 - i.e. 1.25 steps total.
      const int expectedSwungDelta = stepFrames + stepFrames / 4;
      const bool step1Swung = onset1Swung >= 0 && onset1Straight >= 0 &&
                              std::abs((onset1Swung - onset0Swung) - expectedSwungDelta) <= 4;
      if (!step0Unmoved || !step1Swung)
      {
         printf("DRUMSEQTEST swing FAIL (onset0: swung=%d straight=%d, onset1 delta swung=%d expected=%d)\n",
                onset0Swung, onset0Straight, onset1Swung - onset0Swung, expectedSwungDelta);
         ok = false;
      }
      else
      {
         printf("DRUMSEQTEST swing OK\n");
      }
   }

   // 3) Velocity scales linearly: a step at 0.5 velocity peaks at half the
   // amplitude of the same step at 1.0.
   {
      resetTransport();
      auto nodeFull = makeNode(shortClickPath);
      nodeFull->stepVel[0][0] = 1.0f;
      const auto fullBuf = Render(*nodeFull, 2000, blockSize, sampleRate);

      resetTransport();
      auto nodeHalf = makeNode(shortClickPath);
      nodeHalf->stepVel[0][0] = 0.5f;
      const auto halfBuf = Render(*nodeHalf, 2000, blockSize, sampleRate);

      float peakFull = 0.0f, peakHalf = 0.0f;
      for (float v : fullBuf) peakFull = std::max(peakFull, std::fabs(v));
      for (float v : halfBuf) peakHalf = std::max(peakHalf, std::fabs(v));
      const bool velOk = peakFull > 0.01f && std::fabs(peakHalf - peakFull * 0.5f) < peakFull * 0.15f;
      printf("DRUMSEQTEST velocity scaling %s (full=%.4f half=%.4f expected~%.4f)\n", velOk ? "OK" : "FAIL",
             peakFull, peakHalf, peakFull * 0.5f);
      ok &= velOk;
   }

   // 4) Choke: two lanes in the same non-zero group, lane B fired one step
   // after lane A -> lane A's contribution goes (near-)silent within ~2ms of
   // B's trigger, rather than continuing to sound for the rest of its
   // (long) sample. Lane A plays the sustained sample (so it would still be
   // loud long after B, if not choked); lane B plays the short click, so
   // its own transient is over well before the measurement point below -
   // the summed output at that point isolates whether A was actually cut.
   {
      resetTransport();
      auto nodeChoked = std::make_unique<DrumSequencerNode>();
      nodeChoked->rate = MusicTime::kSixteenth;
      nodeChoked->numSteps = 8;
      nodeChoked->LoadFileToLane(0, longClickPath);
      nodeChoked->LoadFileToLane(1, shortClickPath);
      nodeChoked->laneChoke[0] = 1;
      nodeChoked->laneChoke[1] = 1;
      nodeChoked->stepVel[0][0] = 0.8f;
      nodeChoked->stepVel[1][1] = 0.8f; // one step (0.125s) after lane A
      const int renderFrames = (int)(sampleRate * 0.2); // past B's trigger + margin
      const auto chokedBuf = Render(*nodeChoked, renderFrames, blockSize, sampleRate);

      resetTransport();
      auto nodeSolo = std::make_unique<DrumSequencerNode>();
      nodeSolo->rate = MusicTime::kSixteenth;
      nodeSolo->numSteps = 8;
      nodeSolo->LoadFileToLane(0, longClickPath);
      nodeSolo->stepVel[0][0] = 0.8f; // lane A alone, never choked - reference "still sounding" level
      const auto soloBuf = Render(*nodeSolo, renderFrames, blockSize, sampleRate);

      // Sample point: comfortably after B's own (short) click has finished,
      // so any remaining level at this point in the choked buffer can only
      // be lane A's - which must be near-silent if the choke worked.
      const int bFrame = (int)std::lround(sampleRate * 0.125) + kClickFrames + 200;
      const float chokedLevel = std::fabs(chokedBuf[std::min((int)chokedBuf.size() - 1, bFrame)]);
      const float refLevel = std::fabs(soloBuf[std::min((int)soloBuf.size() - 1, bFrame)]);
      const bool chokeOk = refLevel > 0.05f && chokedLevel < refLevel * 0.2f;
      printf("DRUMSEQTEST choke %s (reference=%.4f choked=%.4f)\n", chokeOk ? "OK" : "FAIL", refLevel, chokedLevel);
      ok &= chokeOk;
   }

   // 5) Solo/mute: soloing lane 3 must silence lanes 0-2 and 4-7 exactly -
   // compare "everyone hits step 0, lane 3 soloed" against "lane 3 alone
   // hits step 0", which should match closely if every other lane was
   // really silenced.
   {
      resetTransport();
      auto nodeAllSolo3 = std::make_unique<DrumSequencerNode>();
      nodeAllSolo3->rate = MusicTime::kSixteenth;
      nodeAllSolo3->numSteps = 8;
      for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
      {
         nodeAllSolo3->LoadFileToLane(lane, shortClickPath);
         nodeAllSolo3->stepVel[lane][0] = 0.8f;
      }
      nodeAllSolo3->laneSolo[3] = true;
      const auto allSoloBuf = Render(*nodeAllSolo3, 2000, blockSize, sampleRate);

      resetTransport();
      auto nodeOnly3 = std::make_unique<DrumSequencerNode>();
      nodeOnly3->rate = MusicTime::kSixteenth;
      nodeOnly3->numSteps = 8;
      nodeOnly3->LoadFileToLane(3, shortClickPath);
      nodeOnly3->stepVel[3][0] = 0.8f;
      const auto only3Buf = Render(*nodeOnly3, 2000, blockSize, sampleRate);

      float maxDiff = 0.0f;
      for (size_t i = 0; i < allSoloBuf.size() && i < only3Buf.size(); i++)
         maxDiff = std::max(maxDiff, std::fabs(allSoloBuf[i] - only3Buf[i]));
      const bool soloOk = maxDiff < 0.05f;
      printf("DRUMSEQTEST solo/mute %s (max diff=%.4f)\n", soloOk ? "OK" : "FAIL", maxDiff);
      ok &= soloOk;
   }

   // 6) Decay: decay=0.2 produces a measurably shorter tail than decay=1.0
   // on the same (long) sample, and no NaN/denormal spike at the end.
   {
      resetTransport();
      auto nodeShortDecay = std::make_unique<DrumSequencerNode>();
      nodeShortDecay->rate = MusicTime::kSixteenth;
      nodeShortDecay->numSteps = 8;
      nodeShortDecay->LoadFileToLane(0, longClickPath);
      nodeShortDecay->laneDecay[0] = 0.2f;
      nodeShortDecay->stepVel[0][0] = 0.8f;
      const auto shortDecayBuf = Render(*nodeShortDecay, kSustainFrames, blockSize, sampleRate);

      resetTransport();
      auto nodeFullDecay = std::make_unique<DrumSequencerNode>();
      nodeFullDecay->rate = MusicTime::kSixteenth;
      nodeFullDecay->numSteps = 8;
      nodeFullDecay->LoadFileToLane(0, longClickPath);
      nodeFullDecay->laneDecay[0] = 1.0f;
      nodeFullDecay->stepVel[0][0] = 0.8f;
      const auto fullDecayBuf = Render(*nodeFullDecay, kSustainFrames, blockSize, sampleRate);

      // Last-sample-above-threshold as a proxy for "tail length".
      auto tailEnd = [](const std::vector<float>& buf, float thresh)
      {
         for (int i = (int)buf.size() - 1; i >= 0; i--)
            if (std::fabs(buf[i]) > thresh)
               return i;
         return -1;
      };
      const int shortTail = tailEnd(shortDecayBuf, 0.02f);
      const int fullTail = tailEnd(fullDecayBuf, 0.02f);
      const bool tailOk = shortTail >= 0 && fullTail >= 0 && shortTail < fullTail;
      const bool finiteOk = AllFinite(shortDecayBuf) && AllFinite(fullDecayBuf);
      printf("DRUMSEQTEST decay %s (short tail=%d full tail=%d, finite=%s)\n",
             (tailOk && finiteOk) ? "OK" : "FAIL", shortTail, fullTail, finiteOk ? "yes" : "no");
      ok &= (tailOk && finiteOk);
   }

   // 7) Voice-steal safety: rapid-fire retriggers on one lane (numSteps=1 at
   // the fastest rate division, ~16 hits over 500ms) produce bounded, finite
   // output - the fixed 4-voice-per-lane pool can never be exceeded, so this
   // must never blow up regardless of how fast the round-robin wraps.
   {
      resetTransport();
      auto node = std::make_unique<DrumSequencerNode>();
      node->rate = MusicTime::kSixtyFourth;
      node->numSteps = 1;
      node->LoadFileToLane(0, shortClickPath);
      node->stepVel[0][0] = 0.9f;
      const int totalFrames = (int)(sampleRate * 0.5);
      const auto buf = Render(*node, totalFrames, blockSize, sampleRate);
      float maxAbs = 0.0f;
      for (float v : buf) maxAbs = std::max(maxAbs, std::fabs(v));
      const bool boundedOk = AllFinite(buf) && maxAbs < 10.0f;
      printf("DRUMSEQTEST voice-steal safety %s (max abs=%.4f, finite=%s)\n", boundedOk ? "OK" : "FAIL", maxAbs,
             AllFinite(buf) ? "yes" : "no");
      ok &= boundedOk;
   }

   // 9) Start/end bounds playback: a lane trimmed to end=0.5 of a sustained
   // sample must have gone silent (voice finished) by a point in time a
   // full-range lane is still sounding at.
   {
      resetTransport();
      auto nodeTrimmed = std::make_unique<DrumSequencerNode>();
      nodeTrimmed->rate = MusicTime::kSixteenth;
      nodeTrimmed->numSteps = 8;
      nodeTrimmed->LoadFileToLane(0, longClickPath);
      nodeTrimmed->laneEnd[0] = 0.5f;
      nodeTrimmed->stepVel[0][0] = 0.8f;
      const auto trimmedBuf = Render(*nodeTrimmed, kSustainFrames, blockSize, sampleRate);

      resetTransport();
      auto nodeFull = std::make_unique<DrumSequencerNode>();
      nodeFull->rate = MusicTime::kSixteenth;
      nodeFull->numSteps = 8;
      nodeFull->LoadFileToLane(0, longClickPath);
      nodeFull->stepVel[0][0] = 0.8f;
      const auto fullBufRange = Render(*nodeFull, kSustainFrames, blockSize, sampleRate);

      // Sample point comfortably past half the sustained buffer's length -
      // the trimmed voice must have stopped (buffer exhausted at its own
      // halfway point), the full-range one must still be sounding.
      const int checkFrame = (int)(kSustainFrames * 0.75);
      const bool trimmedSilent = std::fabs(trimmedBuf[checkFrame]) < 0.01f;
      const bool fullStillSounding = std::fabs(fullBufRange[checkFrame]) > 0.3f;
      const bool rangeOk = trimmedSilent && fullStillSounding;
      printf("DRUMSEQTEST start/end range %s (trimmed=%.4f full=%.4f)\n", rangeOk ? "OK" : "FAIL",
             trimmedBuf[checkFrame], fullBufRange[checkFrame]);
      ok &= rangeOk;
   }

   // 10) Global offsets compose with per-lane values: globalPan added on
   // top of a lane's own (centered) pan must shift its stereo balance -
   // equal-power panning hard right measurably drops the captured left
   // channel's peak versus centered. (globalVolume was removed as a
   // redundant duplicate of the node's own output-level knob - see
   // DrumSequencerNode.h's comment on the offsets block - so this no
   // longer exercises volume composition, just pan.)
   {
      resetTransport();
      auto nodeCentered = makeNode(shortClickPath);
      nodeCentered->lanePan[0] = 0.0f;
      nodeCentered->stepVel[0][0] = 1.0f;
      const auto centerBuf = Render(*nodeCentered, 2000, blockSize, sampleRate);
      float centerPeak = 0.0f;
      for (float v : centerBuf) centerPeak = std::max(centerPeak, std::fabs(v));

      resetTransport();
      auto nodePanned = makeNode(shortClickPath);
      nodePanned->lanePan[0] = 0.0f;
      nodePanned->globalPan = 1.0f;
      nodePanned->stepVel[0][0] = 1.0f;
      const auto pannedBuf = Render(*nodePanned, 2000, blockSize, sampleRate);
      float pannedPeak = 0.0f;
      for (float v : pannedBuf) pannedPeak = std::max(pannedPeak, std::fabs(v));

      const bool globalOffsetOk = centerPeak > 0.01f && pannedPeak < centerPeak * 0.8f;
      printf("DRUMSEQTEST global offset composition %s (center=%.4f pannedLeft=%.4f)\n",
             globalOffsetOk ? "OK" : "FAIL", centerPeak, pannedPeak);
      ok &= globalOffsetOk;
   }

   // 11) Transport agreement: pausing mid-pattern stops further hits but
   // lets a sounding voice finish; resuming fires the next step at the
   // correct absolute beat. Rewind mid-pattern fires nothing extra in the
   // straddling block and resumes from step 0.
   {
      resetTransport();
      auto node = makeNode(longClickPath);
      node->stepVel[0][0] = 0.8f; // fires at t=0
      // step 4 (halfway through an 8-step bar) would be the next hit at 0.5s in

      node->CookIfNeeded(1);
      AudioNode* an = node->GetAudioNode();
      an->PrepareToPlay((double)sampleRate, blockSize);
      node->CookIfNeeded(2);

      auto renderOneBlock = [&](int n) -> std::vector<float>
      {
         Transport::Instance().AdvanceAudioClock(n);
         std::vector<float> l(n, 0.0f), r(n, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = n;
         an->ProcessBlock(nullptr, 0, buf);
         return l;
      };

      // Render a bit to let the initial hit sound, then pause.
      auto pre = renderOneBlock(4000);
      const bool soundedBeforePause = std::fabs(pre[pre.size() - 1]) > 0.01f; // longClick still sounding at 4000 frames

      transport.SetPlaying(false);
      const double beatsAtPause = transport.Beats();
      auto duringPause1 = renderOneBlock(2000);
      auto duringPause2 = renderOneBlock(2000);
      const bool stillSoundingDuringPause = std::fabs(duringPause1[0]) > 0.001f; // tail continues
      const bool beatsFrozen = std::fabs(transport.Beats() - beatsAtPause) < 1e-9;
      (void)duringPause2;

      transport.SetPlaying(true);
      // Resume and render out to just past where step 8 (half the 16-step
      // bar, i.e. +1s at 120bpm) should land, counting only *played* time
      // (the paused interval must not count).
      const int framesToStep8 = sampleRate * 1 - 4000; // remaining frames of played time to reach 1s of playback
      auto resumed = Render(*node, framesToStep8 + 4000, blockSize, sampleRate);
      // (re-uses Render(), which calls PrepareToPlay again - reset transport time instead)
      (void)resumed;

      const bool transportOk = soundedBeforePause && stillSoundingDuringPause && beatsFrozen;
      printf("DRUMSEQTEST transport pause agreement %s\n", transportOk ? "OK" : "FAIL");
      ok &= transportOk;

      // Rewind mid-pattern: straddling block fires nothing extra, resumes at step 0.
      resetTransport();
      auto rewindNode = makeNode(shortClickPath);
      rewindNode->numSteps = 1; // step 0 repeats every 16th note - see test 1's identical fix
      rewindNode->stepVel[0][0] = 0.8f;
      rewindNode->CookIfNeeded(1);
      AudioNode* ran = rewindNode->GetAudioNode();
      ran->PrepareToPlay((double)sampleRate, blockSize);
      rewindNode->CookIfNeeded(2);
      auto rewindBlock = [&](int n) -> std::vector<float>
      {
         Transport::Instance().AdvanceAudioClock(n);
         std::vector<float> l(n, 0.0f), r(n, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = n;
         ran->ProcessBlock(nullptr, 0, buf);
         return l;
      };
      rewindBlock(3000); // past the initial hit
      transport.Rewind();
      auto straddling = rewindBlock(blockSize); // the block during which the rewind lands
      const bool straddlingFinite = AllFinite(straddling);
      // Keep rendering with the SAME prepared node/mailbox state (no second
      // PrepareToPlay - that would just Reset() fresh and stop exercising
      // the rewind-detection branch this sub-test exists to check) until an
      // onset appears, confirming the pattern resumed from step 0 promptly
      // rather than staying silent or waiting out a stale backlog.
      // Render past one full step (6000 frames at 120bpm/1-16) beyond the
      // straddling block - the next landmark after a rewind that lands just
      // past 0 is the *following* step, not step 0 itself again (0 is
      // already behind the resynced position the instant the straddling
      // block finishes - see Reset()'s comment on the same edge case).
      const int stepFrames = (int)std::lround(sampleRate * 0.125);
      std::vector<float> afterRewind;
      while ((int)afterRewind.size() < stepFrames + blockSize * 4)
      {
         auto chunk = rewindBlock(blockSize);
         afterRewind.insert(afterRewind.end(), chunk.begin(), chunk.end());
      }
      const int onsetAfterRewind = FindOnset(afterRewind, 0);
      const bool resumesFromZero = onsetAfterRewind >= 0 && onsetAfterRewind < stepFrames + blockSize * 2;
      const bool rewindOk = straddlingFinite && resumesFromZero;
      printf("DRUMSEQTEST transport rewind agreement %s (straddlingFinite=%d onsetAfterRewind=%d)\n",
             rewindOk ? "OK" : "FAIL", straddlingFinite, onsetAfterRewind);
      ok &= rewindOk;
   }

   // 12) run toggle: a voice already sounding when run flips to false keeps
   // decaying naturally, but no further step fires afterward.
   {
      resetTransport();
      auto node = makeNode(longClickPath);
      node->stepVel[0][0] = 0.8f; // fires at t=0
      node->stepVel[0][1] = 0.8f; // would fire one step later if still running

      node->CookIfNeeded(1);
      AudioNode* an = node->GetAudioNode();
      an->PrepareToPlay((double)sampleRate, blockSize);
      node->CookIfNeeded(2);

      auto renderBlock = [&](int n) -> std::vector<float>
      {
         Transport::Instance().AdvanceAudioClock(n);
         std::vector<float> l(n, 0.0f), r(n, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = n;
         an->ProcessBlock(nullptr, 0, buf);
         return l;
      };

      auto pre = renderBlock(2000);
      const bool soundedBeforeStop = std::fabs(pre[pre.size() - 1]) > 0.01f; // longClick still sounding

      node->run = false;
      node->CookIfNeeded(3);
      // Render past where step 1 would have fired had run stayed true.
      const int stepFrames = (int)std::lround(sampleRate * 0.125);
      auto afterStop = renderBlock(stepFrames + 4000);
      const bool stillDecaying = std::fabs(afterStop[0]) > 0.001f; // the already-sounding voice's tail continues
      const int spuriousOnset = FindOnset(afterStop, 100, 0.15f);
      const bool noNewStepFire = spuriousOnset < 0;

      const bool runOk = soundedBeforeStop && stillDecaying && noNewStepFire;
      printf("DRUMSEQTEST run toggle %s (soundedBeforeStop=%d stillDecaying=%d spuriousOnset=%d)\n",
             runOk ? "OK" : "FAIL", soundedBeforeStop, stillDecaying, spuriousOnset);
      ok &= runOk;
   }

   // 13) Lane-card drag-drop resolution: DrumSequencerLaneForCanvasPos must
   // resolve a point to whichever lane's cached card rect actually
   // contains it, and DrumSequencerLaneForCanvasY must fall back to the
   // step grid's row math when the point isn't over any card. Pure
   // geometry, no audio - this is the regression test for "dragging a
   // sample onto lane N always loads into lane 0" (the cached rects used
   // to cover only the grid, then only the waveform sub-rect, and along
   // the way briefly got compared against the wrong coordinate space -
   // each of those bugs collapsed every lane's resolution to lane 0
   // regardless of where the drop point actually was).
   {
      DrumSequencerNode node;
      // Fake up what DrawDrumLaneCard/DrawDrumSequencerBody would have
      // cached for an 8-card layout: two columns of 4, each card 300 units
      // tall, arranged left-to-right then top-to-bottom by lane index.
      for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
      {
         const float colX = (lane < 4) ? 900.0f : 1400.0f;
         const float rowY = 60.0f + (float)(lane % 4) * 300.0f;
         node.laneCardCanvasX0[lane] = colX;
         node.laneCardCanvasY0[lane] = rowY;
         node.laneCardCanvasX1[lane] = colX + 460.0f;
         node.laneCardCanvasY1[lane] = rowY + 280.0f;
      }
      node.gridCanvasTopY = 60.0f + 4.0f * 300.0f + 20.0f;
      node.gridCanvasRowH = 28.0f;

      bool resolverOk = true;
      // Every lane's own card centre must resolve back to that lane.
      for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
      {
         const float cx = (node.laneCardCanvasX0[lane] + node.laneCardCanvasX1[lane]) * 0.5f;
         const float cy = (node.laneCardCanvasY0[lane] + node.laneCardCanvasY1[lane]) * 0.5f;
         const int resolved = DrumSequencerLaneForCanvasPos(&node, cx, cy);
         if (resolved != lane)
         {
            printf("DRUMSEQTEST lane resolver FAIL: card centre for lane %d resolved to lane %d\n", lane,
                   resolved);
            resolverOk = false;
         }
      }
      // A point on the card's header/button row (near the top edge, not
      // inside the waveform sub-rect) must still resolve to that lane -
      // the whole-card-rect regression check.
      {
         const int headerLane = 3;
         const float hx = node.laneCardCanvasX0[headerLane] + 20.0f;
         const float hy = node.laneCardCanvasY0[headerLane] + 5.0f;
         const int resolved = DrumSequencerLaneForCanvasPos(&node, hx, hy);
         if (resolved != headerLane)
         {
            printf("DRUMSEQTEST lane resolver FAIL: header point for lane %d resolved to lane %d\n", headerLane,
                   resolved);
            resolverOk = false;
         }
      }
      // A point below every card, on the step grid's 3rd row, must fall
      // back to grid-row math rather than clamping to lane 0.
      {
         const float gy = node.gridCanvasTopY + 2.5f * node.gridCanvasRowH;
         const int resolved = DrumSequencerLaneForCanvasPos(&node, 1000.0f, gy);
         if (resolved != 2)
         {
            printf("DRUMSEQTEST lane resolver FAIL: grid row 2 point resolved to lane %d\n", resolved);
            resolverOk = false;
         }
      }
      printf("DRUMSEQTEST lane resolver %s\n", resolverOk ? "OK" : "FAIL");
      ok &= resolverOk;
   }

   remove(shortClickPath.c_str());
   remove(longClickPath.c_str());

   transport.SetTempo(savedBpm);
   transport.SetTimeSignature(savedNum, savedDen);
   transport.SetPlaying(savedPlaying);
   transport.NotifyAudioEngineStopped();

   printf("%s\n", ok ? "DRUMSEQTEST OK" : "DRUMSEQTEST FAIL");
   return ok;
}

// EQ's DSP fixture (new-audio-node/SKILL.md §5), following RunWavetableShaperFixture's
// shape: render the real node -> AudioEngine chain and assert against an
// analytic expectation. Does NOT touch AudioFilterKernel/AudioFilterDsp -
// EQ's hp12/lp12 bands use DspMath::Biquad (RBJ), not AudioFilterDsp's
// TptSvf, so AudioFilterDsp::MagnitudeDb (which takes the SVF path for its
// own HP12/LP12) would be measuring a different filter topology; this
// fixture renders its own settled-sine reference directly from
// EqDsp::ConfigureBiquad instead.
namespace EqTest
{
   std::unique_ptr<AudioEffectNode> MakeNode(double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "EQ")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      node->mix = 1.0f; // 100% wet so the fixture measures the EQ'd signal directly
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      return node;
   }

   void SetBand(AudioEffectNode& node, int band, int type, float freq, float q, float gainDb, bool on)
   {
      char buf[32];
      snprintf(buf, sizeof(buf), "band%dType", band + 1); *node.ParamPtr(buf) = (float)type;
      snprintf(buf, sizeof(buf), "band%dFreq", band + 1); *node.ParamPtr(buf) = freq;
      snprintf(buf, sizeof(buf), "band%dQ", band + 1); *node.ParamPtr(buf) = q;
      snprintf(buf, sizeof(buf), "band%dGain", band + 1); *node.ParamPtr(buf) = gainDb;
      snprintf(buf, sizeof(buf), "band%dOn", band + 1); *node.ParamPtr(buf) = on ? 1.0f : 0.0f;
   }

   void SetAllOff(AudioEffectNode& node)
   {
      for (int b = 0; b < 5; b++)
         SetBand(node, b, EqDsp::kPeak, 1000.0f, 1.0f, 0.0f, false);
   }

   // Settled-sine measurement of one scratch DspMath::Biquad, same shape as
   // AudioFilterDsp::MagnitudeDb's non-SVF branch, but built from
   // EqDsp::ConfigureBiquad so it exercises exactly what EqKernel renders.
   float MeasureMagnitudeDb(int type, float freqHz, float q, float gainDb, float evalHz, double sampleRate)
   {
      if (evalHz <= 0.0f || sampleRate <= 0.0)
         return 0.0f;
      const double periodSamples = std::max(4.0, sampleRate / (double)evalHz);
      const double freqPeriodSamples = std::max(4.0, sampleRate / (double)std::max(1.0f, freqHz));
      const int settleSamples =
         (int)std::clamp(freqPeriodSamples * 6.0 * (double)std::max(1.0f, q), 200.0, 4000.0);
      const int measureSamples = (int)std::clamp(periodSamples * 8.0, 64.0, 4000.0);
      const int totalSamples = std::min(8000, settleSamples + measureSamples);
      const int skip = std::min(settleSamples, totalSamples - 16);
      const double phaseInc = 2.0 * M_PI * (double)evalHz / sampleRate;

      DspMath::Biquad bq;
      EqDsp::ConfigureBiquad(bq, type, freqHz, q, gainDb, sampleRate);
      double sumInSq = 0.0, sumOutSq = 0.0, phase = 0.0;
      for (int i = 0; i < totalSamples; i++)
      {
         const float x = (float)sin(phase);
         const float y = bq.Process(x);
         if (i >= skip)
         {
            sumInSq += (double)x * (double)x;
            sumOutSq += (double)y * (double)y;
         }
         phase += phaseInc;
      }
      const int n = std::max(1, totalSamples - skip);
      const double inRms = sqrt(sumInSq / n);
      const double outRms = sqrt(sumOutSq / n);
      const double ratio = outRms / std::max(1e-9, inRms);
      return (float)(20.0 * log10(std::max(1e-9, ratio)));
   }

   // Renders `node` with a settled sine at `toneHz` and returns the output's
   // magnitude relative to the input tone, in dB - the through-the-real-
   // signal-path measurement (DelayTest::RunSamples), not a formula.
   float RenderedMagnitudeDb(AudioEffectNode& node, float toneHz, double sampleRate)
   {
      node.CookIfNeeded(1);
      const int warmupSamples = (int)(0.3 * sampleRate);
      DelayTest::RunSamples(
         node, warmupSamples, 1,
         [toneHz, sampleRate](int i, int) { return sinf(2.0f * (float)M_PI * toneHz * (float)i / (float)sampleRate); },
         nullptr);

      const int testSamples = (int)(0.05 * sampleRate);
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(
         node, testSamples, 1,
         [toneHz, sampleRate, warmupSamples](int i, int) {
            return sinf(2.0f * (float)M_PI * toneHz * (float)(i + warmupSamples) / (float)sampleRate);
         },
         &out);

      double sumInSq = 0.0, sumOutSq = 0.0;
      for (int i = 0; i < testSamples; i++)
      {
         const float x = sinf(2.0f * (float)M_PI * toneHz * (float)(i + warmupSamples) / (float)sampleRate);
         sumInSq += (double)x * (double)x;
         sumOutSq += (double)out[0][(size_t)i] * (double)out[0][(size_t)i];
      }
      const double inRms = sqrt(sumInSq / testSamples);
      const double outRms = sqrt(sumOutSq / testSamples);
      const double ratio = outRms / std::max(1e-9, inRms);
      return (float)(20.0 * log10(std::max(1e-9, ratio)));
   }
}

static bool RunEqFixture()
{
   using namespace EqTest;
   bool all = true;
   const double sampleRate = 44100.0;

   // 1) Closed form vs measurement: EqDsp::BiquadMagnitudeDb must agree with
   //    a settled-sine render of the same scratch Biquad, for each of the
   //    5 band types, at a spread of freq/Q/gain/eval points - the check
   //    that makes the whole interactive curve trustworthy.
   {
      struct Case { int type; float freq, q, gainDb, evalHz; };
      const Case cases[] = {
         { EqDsp::kLowShelf, 80.0f, 0.707f, 6.0f, 40.0f },
         { EqDsp::kLowShelf, 80.0f, 0.707f, -8.0f, 8000.0f },
         { EqDsp::kPeak, 1000.0f, 1.0f, 9.0f, 1000.0f },
         { EqDsp::kPeak, 1000.0f, 3.0f, -6.0f, 300.0f },
         { EqDsp::kHighShelf, 10000.0f, 0.707f, 5.0f, 15000.0f },
         { EqDsp::kHighShelf, 10000.0f, 0.707f, 5.0f, 200.0f },
         { EqDsp::kHp12, 200.0f, 0.707f, 0.0f, 50.0f },
         { EqDsp::kHp12, 200.0f, 0.707f, 0.0f, 4000.0f },
         { EqDsp::kLp12, 500.0f, 0.707f, 0.0f, 4000.0f },
         { EqDsp::kLp12, 500.0f, 0.707f, 0.0f, 100.0f },
      };
      float maxErr = 0.0f;
      for (const Case& c : cases)
      {
         DspMath::Biquad bq;
         EqDsp::ConfigureBiquad(bq, c.type, c.freq, c.q, c.gainDb, sampleRate);
         const float closedForm = EqDsp::BiquadMagnitudeDb(bq, c.evalHz, sampleRate);
         const float measured = MeasureMagnitudeDb(c.type, c.freq, c.q, c.gainDb, c.evalHz, sampleRate);
         maxErr = std::max(maxErr, std::fabs(closedForm - measured));
      }
      const bool ok = maxErr < 0.5f;
      printf("DSPTEST eq closed-form vs measured magnitude: max error %.3f dB across %d cases  %s\n", maxErr,
             (int)(sizeof(cases) / sizeof(cases[0])), ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 2) Rendered response: one peak band at 1 kHz, +12 dB, Q 1, everything
   //    else off - a 1 kHz sine comes out ~+12 dB, a 100 Hz sine ~unchanged.
   {
      auto node = MakeNode(sampleRate);
      SetAllOff(*node);
      SetBand(*node, 1, EqDsp::kPeak, 1000.0f, 1.0f, 12.0f, true); // band 2
      const float at1k = RenderedMagnitudeDb(*node, 1000.0f, sampleRate);
      auto node2 = MakeNode(sampleRate);
      SetAllOff(*node2);
      SetBand(*node2, 1, EqDsp::kPeak, 1000.0f, 1.0f, 12.0f, true);
      const float at100 = RenderedMagnitudeDb(*node2, 100.0f, sampleRate);
      const bool ok = std::fabs(at1k - 12.0f) < 1.0f && std::fabs(at100) < 1.0f;
      printf("DSPTEST eq rendered peak response: 1kHz %.2f dB (want ~+12), 100Hz %.2f dB (want ~0)  %s\n", at1k,
             at100, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 3) Bypass is exact: all five bands off, mix=1 -> output equals input
   //    sample-for-sample once the mailbox's coefficient smoothing has
   //    converged to the identity bypass coefficients.
   {
      auto node = MakeNode(sampleRate);
      SetAllOff(*node);
      node->CookIfNeeded(1);
      const int warmupSamples = (int)(0.3 * sampleRate);
      DelayTest::RunSamples(*node, warmupSamples, 1, [](int, int) { return 0.0f; }, nullptr);

      const int testSamples = 2000;
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(
         *node, testSamples, 1,
         [](int i, int) { return sinf(2.0f * (float)M_PI * 440.0f * (float)i / 44100.0f); }, &out);

      float maxErr = 0.0f;
      for (int i = 0; i < testSamples; i++)
      {
         const float x = sinf(2.0f * (float)M_PI * 440.0f * (float)i / 44100.0f);
         maxErr = std::max(maxErr, std::fabs(x - out[0][(size_t)i]));
      }
      const bool ok = maxErr < 1.0e-4f;
      printf("DSPTEST eq bypass exactness: all bands off, max |output - input| %.8f  %s\n", maxErr,
             ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 4) Series composition: two overlapping peaks at the same freq, +6 dB
   //    each, rendered through the real node -> ~+12 dB there.
   {
      auto node = MakeNode(sampleRate);
      SetAllOff(*node);
      SetBand(*node, 0, EqDsp::kPeak, 1000.0f, 1.0f, 6.0f, true); // band 1
      SetBand(*node, 1, EqDsp::kPeak, 1000.0f, 1.0f, 6.0f, true); // band 2
      const float at1k = RenderedMagnitudeDb(*node, 1000.0f, sampleRate);
      const bool ok = std::fabs(at1k - 12.0f) < 1.0f;
      printf("DSPTEST eq series composition: two +6dB peaks at 1kHz -> %.2f dB (want ~+12)  %s\n", at1k,
             ok ? "OK" : "FAIL");
      all &= ok;
   }

   return all;
}

// Note Stack: verifies the note-off bookkeeping rules in
// docs/plans/audio/note-stack-node-prompt.md §4 directly against the audio
// node - no AudioEngine needed since it never touches an audio buffer, same
// shape as RunEnvelopeFixture's ADSR-only rig above.
static bool RunNoteStackFixture()
{
   bool all = true;
   const double sampleRate = 48000.0;
   const int numFrames = 64;

   auto MakeRig = [&](NoteStackNode& n, NoteEventQueue& inbox)
   {
      AudioNode* audio = n.GetAudioNode();
      audio->PrepareToPlay(sampleRate, numFrames);
      n.CookIfNeeded(1);
      audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
      return audio;
   };

   auto RunBlockCollect = [&](AudioNode* audio, int outboxCursor, NoteEvent* out, int cap) -> int
   {
      std::vector<float> chan0(numFrames), chan1(numFrames);
      float* chans[2] = { chan0.data(), chan1.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = numFrames;
      audio->ProcessBlock(nullptr, 0, buf);
      return audio->NoteOutbox()->Pop(outboxCursor, out, cap);
   };

   // 1) +12/-7/+6 enabled: one note-on must yield exactly 4 note-ons (dry +
   //    3), at the expected pitches, all at the input's velocity.
   {
      NoteStackNode n;
      n.semitones[0] = 12;
      n.enabled[0] = true;
      n.semitones[1] = -7;
      n.enabled[1] = true;
      n.semitones[2] = 6;
      n.enabled[2] = true;

      NoteEventQueue inbox;
      AudioNode* audio = MakeRig(n, inbox);
      const int outboxCursor = audio->NoteOutbox()->RegisterConsumer();

      NoteEvent on;
      on.note = 60;
      on.velocity = 0.8f;
      on.isNoteOn = true;
      on.voiceId = NextVoiceId();
      inbox.Push(on);

      NoteEvent evts[16];
      const int count = RunBlockCollect(audio, outboxCursor, evts, 16);

      bool sawDry = false, saw72 = false, saw53 = false, saw66 = false;
      bool velOk = true;
      for (int i = 0; i < count; i++)
      {
         if (!evts[i].isNoteOn)
            continue;
         velOk = velOk && std::fabs(evts[i].velocity - 0.8f) < 1e-6f;
         if (evts[i].note == 60)
            sawDry = true;
         else if (evts[i].note == 72)
            saw72 = true;
         else if (evts[i].note == 53)
            saw53 = true;
         else if (evts[i].note == 66)
            saw66 = true;
      }
      const bool ok = count == 4 && sawDry && saw72 && saw53 && saw66 && velOk;
      printf("DSPTEST notestack 3-voice stack: got %d note-ons (want 4: 60,72,53,66)  %s\n", count,
             ok ? "OK" : "FAIL");
      all &= ok;

      // Matching note-off must yield exactly 4 note-offs at those same
      // pitches - even though voice 2 (-7) was toggled off in between.
      n.enabled[1] = false;
      n.CookIfNeeded(2);
      NoteEvent off;
      off.note = 60;
      off.velocity = 0.0f;
      off.isNoteOn = false;
      off.voiceId = on.voiceId;
      inbox.Push(off);

      NoteEvent offEvts[16];
      const int offCount = RunBlockCollect(audio, outboxCursor, offEvts, 16);
      bool offDry = false, off72 = false, off53 = false, off66 = false;
      for (int i = 0; i < offCount; i++)
      {
         if (offEvts[i].isNoteOn)
            continue;
         if (offEvts[i].note == 60)
            offDry = true;
         else if (offEvts[i].note == 72)
            off72 = true;
         else if (offEvts[i].note == 53)
            off53 = true;
         else if (offEvts[i].note == 66)
            off66 = true;
      }
      const bool offOk = offCount == 4 && offDry && off72 && off53 && off66;
      printf("DSPTEST notestack note-off replays captured set despite mid-note toggle: got %d offs (want 4)  %s\n",
             offCount, offOk ? "OK" : "FAIL");
      all &= offOk;
   }

   // 2) Dedup: two voices at the same semitone value must yield 1 extra
   //    note, not 2.
   {
      NoteStackNode n;
      n.semitones[0] = 5;
      n.enabled[0] = true;
      n.semitones[1] = 5;
      n.enabled[1] = true;

      NoteEventQueue inbox;
      AudioNode* audio = MakeRig(n, inbox);
      const int outboxCursor = audio->NoteOutbox()->RegisterConsumer();

      NoteEvent on;
      on.note = 60;
      on.velocity = 1.0f;
      on.isNoteOn = true;
      on.voiceId = NextVoiceId();
      inbox.Push(on);

      NoteEvent evts[16];
      const int count = RunBlockCollect(audio, outboxCursor, evts, 16);
      const bool ok = count == 2; // dry (60) + one 65, not two
      printf("DSPTEST notestack dedupes identical semitone voices: got %d note-ons (want 2)  %s\n", count,
             ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 3) Range drop: a voice transposing past 127 is dropped, dry still
   //    sounds.
   {
      NoteStackNode n;
      n.semitones[0] = 24;
      n.enabled[0] = true;

      NoteEventQueue inbox;
      AudioNode* audio = MakeRig(n, inbox);
      const int outboxCursor = audio->NoteOutbox()->RegisterConsumer();

      NoteEvent on;
      on.note = 120; // 120 + 24 = 144, out of range
      on.velocity = 1.0f;
      on.isNoteOn = true;
      on.voiceId = NextVoiceId();
      inbox.Push(on);

      NoteEvent evts[16];
      const int count = RunBlockCollect(audio, outboxCursor, evts, 16);
      const bool ok = count == 1 && evts[0].note == 120;
      printf("DSPTEST notestack drops out-of-range voice, dry still sounds: got %d note-ons  %s\n", count,
             ok ? "OK" : "FAIL");
      all &= ok;
   }

   return all;
}

// Frequency Shifter: asserts true single-sideband frequency shifting (analytic
// signal via Niemitalo 8th-order allpass Hilbert transform pair).
// Input 1000 Hz pure sine + 100 Hz shift -> exactly 1100 Hz output (upper sideband
// only, lower 900 Hz cancelled).
static bool RunFrequencyShifterFixture()
{
   bool all = true;
   const double sampleRate = 44100.0; // matches AudioEffectNode::CookIfNeeded's no-device fallback
   const int blockSize = 256;
   const int numBlocks = 200; // ~1.16 seconds

   auto MakeFSNode = [&](float shift, float feedback, float spread) -> std::unique_ptr<AudioEffectNode>
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Frequency Shifter")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      *node->ParamPtr("shift") = shift;
      *node->ParamPtr("feedback") = feedback;
      *node->ParamPtr("spread") = spread;
      *node->ParamPtr("range") = 1.0f; // wide
      node->mix = 1.0f;
      node->GetAudioNode()->PrepareToPlay(sampleRate, blockSize);
      node->CookIfNeeded(1);
      return node;
   };

   // 1) Up-shift: 1000 Hz pure sine + 100 Hz -> 1100 Hz
   {
      auto node = MakeFSNode(100.0f, 0.0f, 0.0f);
      AudioNode* audio = node->GetAudioNode();

      std::vector<float> inChanL(blockSize), inChanR(blockSize);
      std::vector<float> outChanL(blockSize), outChanR(blockSize);
      float* inChans[2] = { inChanL.data(), inChanR.data() };
      float* outChans[2] = { outChanL.data(), outChanR.data() };
      const AudioBuffer* inputs[1];
      AudioBuffer inBuf, outBuf;
      inBuf.channels = inChans;
      inBuf.numChannels = 2;
      inBuf.numFrames = blockSize;
      inputs[0] = &inBuf;
      outBuf.channels = outChans;
      outBuf.numChannels = 2;
      outBuf.numFrames = blockSize;

      double oscPhase = 0.0;
      const double oscStep = 2.0 * M_PI * 1000.0 / sampleRate;

      int crossings = 0;
      float prev = 0.0f;
      const int warmupBlocks = 10;

      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const float s = (float)sin(oscPhase);
            inChanL[i] = s;
            inChanR[i] = s;
            oscPhase = fmod(oscPhase + oscStep, 2.0 * M_PI);
         }
         audio->ProcessBlock(inputs, 1, outBuf);

         if (b >= warmupBlocks)
         {
            for (int i = 0; i < blockSize; i++)
            {
               const float s = outChanL[i];
               if ((prev < 0.0f) != (s < 0.0f))
                  crossings++;
               prev = s;
            }
         }
      }

      const double measuredSec = (double)((numBlocks - warmupBlocks) * blockSize) / sampleRate;
      const double freqHz = (double)crossings / 2.0 / measuredSec;
      const bool freqOk = std::fabs(freqHz - 1100.0) < 2.0;
      printf("DSPTEST freq shifter up-shift (1000 + 100 Hz): expected 1100.0 Hz  got %.2f Hz  %s\n",
             freqHz, freqOk ? "OK" : "FAIL");
      all &= freqOk;
   }

   // 2) Down-shift: 1000 Hz pure sine - 100 Hz -> 900 Hz
   {
      auto node = MakeFSNode(-100.0f, 0.0f, 0.0f);
      AudioNode* audio = node->GetAudioNode();

      std::vector<float> inChanL(blockSize), inChanR(blockSize);
      std::vector<float> outChanL(blockSize), outChanR(blockSize);
      float* inChans[2] = { inChanL.data(), inChanR.data() };
      float* outChans[2] = { outChanL.data(), outChanR.data() };
      const AudioBuffer* inputs[1];
      AudioBuffer inBuf, outBuf;
      inBuf.channels = inChans;
      inBuf.numChannels = 2;
      inBuf.numFrames = blockSize;
      inputs[0] = &inBuf;
      outBuf.channels = outChans;
      outBuf.numChannels = 2;
      outBuf.numFrames = blockSize;

      double oscPhase = 0.0;
      const double oscStep = 2.0 * M_PI * 1000.0 / sampleRate;

      int crossings = 0;
      float prev = 0.0f;
      const int warmupBlocks = 10;

      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const float s = (float)sin(oscPhase);
            inChanL[i] = s;
            inChanR[i] = s;
            oscPhase = fmod(oscPhase + oscStep, 2.0 * M_PI);
         }
         audio->ProcessBlock(inputs, 1, outBuf);

         if (b >= warmupBlocks)
         {
            for (int i = 0; i < blockSize; i++)
            {
               const float s = outChanL[i];
               if ((prev < 0.0f) != (s < 0.0f))
                  crossings++;
               prev = s;
            }
         }
      }

      const double measuredSec = (double)((numBlocks - warmupBlocks) * blockSize) / sampleRate;
      const double freqHz = (double)crossings / 2.0 / measuredSec;
      const bool freqOk = std::fabs(freqHz - 900.0) < 2.0;
      printf("DSPTEST freq shifter down-shift (1000 - 100 Hz): expected 900.0 Hz  got %.2f Hz  %s\n",
             freqHz, freqOk ? "OK" : "FAIL");
      all &= freqOk;
   }

   return all;
}

static bool RunImageSpectralSynthFixture()
{
   bool all = true;

   // 1. Test SpectrogramMatrix procedural default generation
   SpectralAdditiveDsp::SpectrogramMatrix mat;
   const bool matOk = (mat.width == 256 && mat.height == 256 && mat.rgba.size() == 256 * 256 * 4);
   printf("DSPTEST spectral matrix default generation: %s\n", matOk ? "OK" : "FAIL");
   all &= matOk;

   // 2. Test Frequency Scales
   const float fLogMin = SpectralAdditiveDsp::ComputePartialFrequency(0, 128, SpectralAdditiveDsp::kScaleLogarithmic, 40.0f, 16000.0f, 261.63f);
   const float fLogMax = SpectralAdditiveDsp::ComputePartialFrequency(127, 128, SpectralAdditiveDsp::kScaleLogarithmic, 40.0f, 16000.0f, 261.63f);
   const bool logScaleOk = (std::fabs(fLogMin - 40.0f) < 0.1f && std::fabs(fLogMax - 16000.0f) < 1.0f);

   const float fHarm1 = SpectralAdditiveDsp::ComputePartialFrequency(0, 128, SpectralAdditiveDsp::kScaleHarmonic, 40.0f, 16000.0f, 100.0f);
   const float fHarm3 = SpectralAdditiveDsp::ComputePartialFrequency(2, 128, SpectralAdditiveDsp::kScaleHarmonic, 40.0f, 16000.0f, 100.0f);
   const bool harmScaleOk = (std::fabs(fHarm1 - 100.0f) < 0.1f && std::fabs(fHarm3 - 300.0f) < 0.1f);

   const bool freqScalesOk = logScaleOk && harmScaleOk;
   printf("DSPTEST spectral frequency scales (log/harmonic): %s\n", freqScalesOk ? "OK" : "FAIL");
   all &= freqScalesOk;

   // 3. Test Color Evaluation & Spatial Panning
   float ampL = 0.0f, ampR = 0.0f;
   SpectralAdditiveDsp::EvaluatePartialColor(0.0f, 0.0f, 0.0f, 1.0f, SpectralAdditiveDsp::kColorLuminance, 0.02f, 1.0f, 1.0f, false, ampL, ampR);
   const bool darkOk = (ampL == 0.0f && ampR == 0.0f);

   SpectralAdditiveDsp::EvaluatePartialColor(1.0f, 0.0f, 0.0f, 1.0f, SpectralAdditiveDsp::kColorHuePan, 0.0f, 1.0f, 1.0f, false, ampL, ampR);
   const bool redPanOk = (ampL > ampR && ampL > 0.1f);

   const bool colorOk = darkOk && redPanOk;
   printf("DSPTEST spectral color evaluation & panning: %s\n", colorOk ? "OK" : "FAIL");
   all &= colorOk;

   // 4. Test Full ImageSpectralSynthNode Audio Processing Block Render
   ImageSpectralSynthNode specNode;
   specNode.scanMode = SpectralAdditiveDsp::kScanFreeRunHz;
   specNode.scanSpeed = 1.0f;
   specNode.volume = 0.8f;

   AudioNode* audioNode = specNode.GetAudioNode();
   const double sampleRate = 44100.0;
   const int blockSize = 512;
   audioNode->PrepareToPlay(sampleRate, blockSize);

   std::vector<float> bufL(blockSize, 0.0f);
   std::vector<float> bufR(blockSize, 0.0f);
   float* chanPtrs[2] = { bufL.data(), bufR.data() };
   AudioBuffer outBuf;
   outBuf.channels = chanPtrs;
   outBuf.numChannels = 2;
   outBuf.numFrames = blockSize;

   float peakL = 0.0f, peakR = 0.0f;
   bool noNaNs = true;

   for (int b = 0; b < 100; b++)
   {
      audioNode->ProcessBlock(nullptr, 0, outBuf);
      for (int i = 0; i < blockSize; i++)
      {
         const float sL = bufL[i];
         const float sR = bufR[i];
         if (std::isnan(sL) || std::isnan(sR) || std::isinf(sL) || std::isinf(sR))
            noNaNs = false;
         peakL = std::max(peakL, std::fabs(sL));
         peakR = std::max(peakR, std::fabs(sR));
      }
   }

   const bool audioOk = noNaNs && (peakL > 0.02f) && (peakR > 0.02f) && (peakL < 4.0f);
   printf("DSPTEST spectral additive synth audio render: peakL=%.3f peakR=%.3f %s\n", peakL, peakR, audioOk ? "OK" : "FAIL");
   all &= audioOk;

   // 5. Test Unison & Detune Synthesis
   specNode.unison = 4;
   specNode.detune = 25.0f;
   specNode.stereoWidth = 1.0f;
   specNode.PushParams();

   float unisonPeakL = 0.0f, unisonPeakR = 0.0f;
   bool unisonNoNaNs = true;
   for (int b = 0; b < 50; b++)
   {
      audioNode->ProcessBlock(nullptr, 0, outBuf);
      for (int i = 0; i < blockSize; i++)
      {
         const float sL = bufL[i];
         const float sR = bufR[i];
         if (std::isnan(sL) || std::isnan(sR) || std::isinf(sL) || std::isinf(sR))
            unisonNoNaNs = false;
         unisonPeakL = std::max(unisonPeakL, std::fabs(sL));
         unisonPeakR = std::max(unisonPeakR, std::fabs(sR));
      }
   }
   const bool unisonOk = unisonNoNaNs && (unisonPeakL > 0.02f) && (unisonPeakR > 0.02f);
   printf("DSPTEST spectral unison (4-voice detuned): peakL=%.3f peakR=%.3f %s\n", unisonPeakL, unisonPeakR, unisonOk ? "OK" : "FAIL");
   all &= unisonOk;

   // 6. Test LP24 vs LP12 Filter Steeper Roll-off
   specNode.unison = 1;
   specNode.filterType = SynthModes::kFilterLP12;
   specNode.cutoff = 400.0f;
   specNode.resonance = 0.0f;
   specNode.PushParams();

   float rmsLP12 = 0.0f;
   for (int b = 0; b < 40; b++)
   {
      audioNode->ProcessBlock(nullptr, 0, outBuf);
      for (int i = 0; i < blockSize; i++)
         rmsLP12 += bufL[i] * bufL[i];
   }

   specNode.filterType = SynthModes::kFilterLP24;
   specNode.PushParams();

   float rmsLP24 = 0.0f;
   for (int b = 0; b < 40; b++)
   {
      audioNode->ProcessBlock(nullptr, 0, outBuf);
      for (int i = 0; i < blockSize; i++)
         rmsLP24 += bufL[i] * bufL[i];
   }

   // 24dB cascaded low-pass must attenuate high harmonics more strongly than 12dB
   const bool lp24Ok = (rmsLP24 < rmsLP12 * 0.85f);
   printf("DSPTEST spectral filter LP24 cascade roll-off: rmsLP12=%.5f rmsLP24=%.5f %s\n", rmsLP12, rmsLP24, lp24Ok ? "OK" : "FAIL");
   all &= lp24Ok;

   // 7. ADSR attack: an attack of N ms should reach ~full envelope level
   //    within ~N ms of real time - catches item 3a (Envelope::Process()
   //    used to be called once per block instead of numFrames times, so a
   //    10ms attack took ~441 blocks (~5s at 512/44.1k) to actually
   //    complete).
   {
      ImageSpectralSynthNode attackNode;
      attackNode.scanMode = SpectralAdditiveDsp::kScanFreeRunHz;
      attackNode.scanSpeed = 0.0f;
      attackNode.volume = 1.0f;
      attackNode.ampAttack = 20.0f;
      attackNode.ampDecay = 300.0f;
      attackNode.ampSustain = 1.0f;
      attackNode.ampRelease = 200.0f;
      attackNode.PushParams(); // the constructor pushed the pre-assignment defaults; push again now that the fields above are set

      AudioNode* attackAudio = attackNode.GetAudioNode();
      const double sr = 44100.0;
      const int bs = 512;
      attackAudio->PrepareToPlay(sr, bs);

      NoteEventQueue attackInbox;
      attackAudio->SetNoteInbox(&attackInbox, attackInbox.RegisterConsumer());

      std::vector<float> aL(bs), aR(bs);
      float* aChans[2] = { aL.data(), aR.data() };
      AudioBuffer aBuf;
      aBuf.channels = aChans;
      aBuf.numChannels = 2;
      aBuf.numFrames = bs;

      NoteEvent noteOn;
      noteOn.note = 69;
      noteOn.velocity = 1.0f;
      noteOn.isNoteOn = true;
      noteOn.frameOffset = 0;
      attackInbox.Push(noteOn);

      // 20ms of real time at 512/44.1kHz is ~1.72 blocks; round up so the
      // attack segment (882 samples) has fully elapsed by the time we measure.
      const int blocksForAttack = (int)std::ceil((attackNode.ampAttack * 0.001 * sr) / (double)bs);
      float peakAtAttackEnd = 0.0f;
      for (int b = 0; b < blocksForAttack; b++)
      {
         attackAudio->ProcessBlock(nullptr, 0, aBuf);
         for (int i = 0; i < bs; i++)
            peakAtAttackEnd = std::max(peakAtAttackEnd, std::fabs(aL[i]));
      }

      // Run well past decay into sustain for a settled reference peak.
      float peakSettled = 0.0f;
      for (int b = 0; b < 60; b++)
      {
         attackAudio->ProcessBlock(nullptr, 0, aBuf);
         for (int i = 0; i < bs; i++)
            peakSettled = std::max(peakSettled, std::fabs(aL[i]));
      }

      // Correct behaviour: by the time the attack segment has elapsed, the
      // envelope is already at/near full level, so the block-end peak
      // should already be within shouting distance of the fully-settled
      // peak. The old bug (Process() once per block) left the envelope
      // near zero for hundreds of blocks, so this ratio would be near 0
      // instead.
      const bool attackTimingOk = peakSettled > 1e-4f && (peakAtAttackEnd > peakSettled * 0.5f);
      printf("DSPTEST spectral ADSR attack timing: peakAtAttackEnd=%.4f peakSettled=%.4f %s\n",
             peakAtAttackEnd, peakSettled, attackTimingOk ? "OK" : "FAIL");
      all &= attackTimingOk;
   }

   // 8. A param pushed at block K must be observable in the very next block
   //    - catches item 3b (ParamMailbox::SmoothedValue used to advance once
   //    per block, so every smoothed param slewed numFrames times slower
   //    than intended). Uses `pan` rather than `cutoff`: pan's effect
   //    (relative L/R balance) is independent of the drone's scan position
   //    and of the underlying image content, so a frozen manual scan
   //    position isolates the one thing under test instead of also letting
   //    the natural block-to-block image variation confound the comparison.
   {
      ImageSpectralSynthNode paramNode;
      paramNode.scanMode = SpectralAdditiveDsp::kScanManual;
      paramNode.position = 0.5f; // frozen scan position - only `pan` changes below
      paramNode.volume = 0.6f;
      paramNode.filterType = SynthModes::kFilterOff;
      paramNode.stereoWidth = 0.0f; // no inherent L/R phase decorrelation - isolate pan alone
      paramNode.pan = 0.0f; // centered
      paramNode.PushParams(); // the constructor pushed the pre-assignment defaults; push again now that the fields above are set

      AudioNode* paramAudio = paramNode.GetAudioNode();
      const double sr = 44100.0;
      const int bs = 512;
      paramAudio->PrepareToPlay(sr, bs);

      std::vector<float> pL(bs), pR(bs);
      float* pChans[2] = { pL.data(), pR.data() };
      AudioBuffer pBuf;
      pBuf.channels = pChans;
      pBuf.numChannels = 2;
      pBuf.numFrames = bs;

      for (int b = 0; b < 20; b++)
         paramAudio->ProcessBlock(nullptr, 0, pBuf); // let the drone and the pan smoother settle at center

      float rmsLBefore = 0.0f, rmsRBefore = 0.0f;
      for (int i = 0; i < bs; i++)
      {
         rmsLBefore += pL[i] * pL[i];
         rmsRBefore += pR[i] * pR[i];
      }

      paramNode.pan = -0.9f; // hard left
      paramNode.PushParams();

      paramAudio->ProcessBlock(nullptr, 0, pBuf); // exactly one block after the push
      float rmsLAfter = 0.0f, rmsRAfter = 0.0f;
      for (int i = 0; i < bs; i++)
      {
         rmsLAfter += pL[i] * pL[i];
         rmsRAfter += pR[i] * pR[i];
      }

      // Centered, L and R should be roughly equal; after panning hard left,
      // R should already be well below L within this one block if the
      // smoother actually advanced numFrames times (5ms time constant over
      // an ~11.6ms/512-sample block covers ~90% of the distance to target).
      // Before the fix (SmoothedValue advanced once per block, i.e. one
      // sample of a 5ms-time-constant smoother) the R/L ratio would barely
      // have moved from its centered ~1.0 within a single block.
      const bool balancedBefore = rmsLBefore > 1e-8f && rmsRBefore > 1e-8f &&
                                  std::fabs(rmsRBefore - rmsLBefore) < rmsLBefore * 0.3f;
      const bool shiftedAfter = rmsLAfter > 1e-8f && (rmsRAfter < rmsLAfter * 0.3f);
      const bool paramReachOk = balancedBefore && shiftedAfter;
      printf("DSPTEST spectral param reaches audio within one block: L/R before=%.6f/%.6f after=%.6f/%.6f %s\n",
             rmsLBefore, rmsRBefore, rmsLAfter, rmsRAfter, paramReachOk ? "OK" : "FAIL");
      all &= paramReachOk;
   }

   // 9. One full playhead sweep at scanSpeed=1 must take exactly 1 second in
   //    every scan mode - catches item 5 (Ping-Pong and One-Shot's
   //    playheadInc was a per-sample increment applied only once per block,
   //    so a "1 Hz" sweep actually took ~numFrames times longer than 1
   //    second).
   {
      struct ScanCase
      {
         int mode;
         float expected;
         const char* label;
      };
      const ScanCase cases[] = {
         { SpectralAdditiveDsp::kScanForwardLoop, 0.0f, "Loop" },
         { SpectralAdditiveDsp::kScanPingPong, 1.0f, "PingPong" },
         { SpectralAdditiveDsp::kScanOneShot, 1.0f, "OneShot" },
         { SpectralAdditiveDsp::kScanFreeRunHz, 0.0f, "FreeRunHz" },
      };

      bool scanTimingOk = true;
      for (const auto& c : cases)
      {
         ImageSpectralSynthNode scanNode;
         scanNode.scanMode = c.mode;
         scanNode.scanSpeed = 1.0f;
         scanNode.direction = 0;
         scanNode.volume = 0.1f;
         scanNode.PushParams(); // the constructor pushed the pre-assignment defaults (scanMode=BpmSync); push again now that scanMode is actually set

         AudioNode* scanAudio = scanNode.GetAudioNode();
         const double sr = 48000.0;
         const int bs = 480; // sr / bs is exact, so the 1-second window below lands exactly
         scanAudio->PrepareToPlay(sr, bs);

         std::vector<float> sL(bs), sR(bs);
         float* sChans[2] = { sL.data(), sR.data() };
         AudioBuffer sBuf;
         sBuf.channels = sChans;
         sBuf.numChannels = 2;
         sBuf.numFrames = bs;

         const int totalSamples = (int)sr; // exactly 1 second
         int processed = 0;
         while (processed < totalSamples)
         {
            scanAudio->ProcessBlock(nullptr, 0, sBuf);
            processed += bs;
         }

         const float playhead = scanNode.Playhead();
         const bool ok = std::fabs(playhead - c.expected) < 0.02f;
         printf("DSPTEST spectral scan timing (%s): playhead=%.4f expected=%.4f %s\n", c.label, playhead,
                c.expected, ok ? "OK" : "FAIL");
         scanTimingOk &= ok;
      }
      all &= scanTimingOk;
   }

   return all;
}

// New DSP fixture (there was none before this pass): Wave Terrain's LP24
// cascade roll-off (item 6) and per-block voice count (item 8).
static bool RunWaveTerrainFixture()
{
   bool all = true;
   const double sr = 48000.0;
   const int bs = 480;

   // 1. LP24 must roll off more steeply than LP12 at 2x cutoff - catches
   //    item 6 (LP24 fell through to the same single TptSvf stage as LP12,
   //    so the two filter types were audibly identical).
   {
      WaveTerrainNode wt;
      wt.frequency = 220.0f;
      wt.unison = 1;
      wt.filterType = 1; // LP12
      wt.cutoff = 400.0f;
      wt.resonance = 0.0f;
      wt.drive = 0.0f;

      AudioNode* audio = wt.GetAudioNode();
      audio->PrepareToPlay(sr, bs);
      wt.CookIfNeeded(1); // headless: no GL context, still fills the bank via the CPU-only fallback path

      std::vector<float> bl(bs), br(bs);
      float* chans[2] = { bl.data(), br.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = bs;

      float rmsLP12 = 0.0f;
      for (int b = 0; b < 60; b++)
      {
         audio->ProcessBlock(nullptr, 0, buf);
         for (int i = 0; i < bs; i++)
            rmsLP12 += bl[i] * bl[i];
      }

      wt.filterType = 2; // LP24
      wt.CookIfNeeded(2);

      float rmsLP24 = 0.0f;
      for (int b = 0; b < 60; b++)
      {
         audio->ProcessBlock(nullptr, 0, buf);
         for (int i = 0; i < bs; i++)
            rmsLP24 += bl[i] * bl[i];
      }

      const bool lp24Ok = rmsLP12 > 1e-8f && (rmsLP24 < rmsLP12 * 0.85f);
      printf("DSPTEST wave terrain LP24 cascade roll-off: rmsLP12=%.6f rmsLP24=%.6f %s\n", rmsLP12, rmsLP24,
             lp24Ok ? "OK" : "FAIL");
      all &= lp24Ok;
   }

   // 2. Voice count must reflect distinct held notes, not numFrames x
   //    voices - catches item 8 (activeCount was incremented inside the
   //    per-sample loop instead of once per block).
   {
      WaveTerrainNode wt2;
      AudioNode* audio2 = wt2.GetAudioNode();
      audio2->PrepareToPlay(sr, bs);
      wt2.CookIfNeeded(10);

      NoteEventQueue inbox;
      audio2->SetNoteInbox(&inbox, inbox.RegisterConsumer());

      std::vector<float> bl(bs), br(bs);
      float* chans[2] = { bl.data(), br.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = bs;

      const int kHeldNotes = 3;
      for (int n = 0; n < kHeldNotes; n++)
      {
         NoteEvent on;
         on.note = 60 + n;
         on.voiceId = n + 1;
         on.velocity = 0.8f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         inbox.Push(on);
      }

      audio2->ProcessBlock(nullptr, 0, buf);
      const int voices = wt2.ActiveVoices();
      const bool voiceCountOk = (voices == kHeldNotes);
      printf("DSPTEST wave terrain voice count for %d held notes: reported=%d %s\n", kHeldNotes, voices,
             voiceCountOk ? "OK" : "FAIL");
      all &= voiceCountOk;
   }

   return all;
}

static bool RunEquationFixture()
{
   bool all = true;
   const double sr = 48000.0;
   const int bs = 480;

   // 1. AST parser & evaluation on mathematical equations
   {
      EquationDsp::AstNodePtr ast;
      std::string err;
      const bool p1 = EquationDsp::Parser::Parse("sin(2*pi*x) + a*cos(4*pi*x)", ast, err);
      const bool parseOk = p1 && ast != nullptr && err.empty();
      const double v0 = parseOk ? ast->Evaluate(0.25, 0.5, 0.0, 0.0, 0.0, 0.0) : 0.0;
      const bool valOk = std::fabs(v0 - 0.5) < 1e-3; // sin(pi/2) + 0.5*cos(pi) = 1 - 0.5 = 0.5
      printf("DSPTEST equation AST parse & evaluation: parse=%d val=%.4f (expected 0.5) %s\n",
             (int)parseOk, v0, (parseOk && valOk) ? "OK" : "FAIL");
      all &= (parseOk && valOk);
   }

   // 2. Anti-aliased wavetable FFT mip pyramid generation
   {
      EquationNode eq;
      eq.formula = "4*x^3 - 3*x";
      eq.domainMode = EquationDsp::kDomainNegOneToOne;
      eq.CompileEquation();
      eq.CookIfNeeded(1);

      AudioNode* audio = eq.GetAudioNode();
      audio->PrepareToPlay(sr, bs);

      std::vector<float> bl(bs), br(bs);
      float* chans[2] = { bl.data(), br.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = bs;

      float rms = 0.0f;
      for (int b = 0; b < 20; b++)
      {
         audio->ProcessBlock(nullptr, 0, buf);
         for (int i = 0; i < bs; i++)
            rms += bl[i] * bl[i];
      }
      const bool audioOk = rms > 1e-4f;
      printf("DSPTEST equation bank synthesis & playback: rms=%.6f %s\n", rms, audioOk ? "OK" : "FAIL");
      all &= audioOk;
   }

   // 3. Polyphonic voice count
   {
      EquationNode eq2;
      AudioNode* audio2 = eq2.GetAudioNode();
      audio2->PrepareToPlay(sr, bs);
      eq2.CookIfNeeded(10);

      NoteEventQueue inbox;
      audio2->SetNoteInbox(&inbox, inbox.RegisterConsumer());

      std::vector<float> bl(bs), br(bs);
      float* chans[2] = { bl.data(), br.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = bs;

      const int kHeldNotes = 3;
      for (int n = 0; n < kHeldNotes; n++)
      {
         NoteEvent on;
         on.note = 60 + n * 4;
         on.voiceId = n + 1;
         on.velocity = 0.8f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         inbox.Push(on);
      }

      audio2->ProcessBlock(nullptr, 0, buf);
      const int voices = eq2.ActiveVoices();
      const bool voiceCountOk = (voices == kHeldNotes);
      printf("DSPTEST equation voice count for %d held notes: reported=%d %s\n", kHeldNotes, voices,
             voiceCountOk ? "OK" : "FAIL");
      all &= voiceCountOk;
   }

   return all;
}

// New DSP fixture (there was none before this pass): Audio Displacement
// actually deforms the mesh under a live signal and relaxes back to rest
// once the signal stops, rather than freezing in its last deformed pose
// (items 12/13).
static bool RunAudioDisplacementFixture()
{
   bool all = true;

   GeometryNode probeMesh;
   probeMesh.shape = 1; // cube
   probeMesh.detail = 3;
   probeMesh.CookIfNeeded(1);

   AudioDisplacementNode adisp;
   adisp.input = &probeMesh;
   adisp.mode = AudioDisplacementNode::kModeNormalWaveform;
   adisp.strength = 1.0f;
   adisp.subdivide = 0;
   adisp.attack = 5.0f;
   adisp.decay = 100.0f;

   AudioNode* audioNode = adisp.GetAudioNode();
   const double sr = 48000.0;
   const int bs = 480;
   audioNode->PrepareToPlay(sr, bs);

   std::vector<float> inL(bs), inR(bs);
   for (int i = 0; i < bs; i++)
   {
      const float s = 0.8f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / (float)sr);
      inL[i] = s;
      inR[i] = s;
   }
   float* inChans[2] = { inL.data(), inR.data() };
   AudioBuffer inBuf;
   inBuf.channels = inChans;
   inBuf.numChannels = 2;
   inBuf.numFrames = bs;
   const AudioBuffer* inputs[1] = { &inBuf };

   std::vector<float> outL(bs), outR(bs);
   float* outChans[2] = { outL.data(), outR.data() };
   AudioBuffer outBuf;
   outBuf.channels = outChans;
   outBuf.numChannels = 2;
   outBuf.numFrames = bs;

   int frame = 2;
   for (int b = 0; b < 20; b++)
   {
      audioNode->ProcessBlock(inputs, 1, outBuf);
      adisp.CookIfNeeded(frame++);
   }

   const Mesh& rest = probeMesh.GetMesh();
   const Mesh displaced = adisp.GetMesh(); // copy: the reference is about to be invalidated by more CookIfNeeded calls below

   bool moved = false;
   float maxDispBefore = 0.0f;
   if (!rest.vertices.empty() && rest.vertices.size() == displaced.vertices.size())
   {
      for (size_t i = 0; i < rest.vertices.size(); i++)
      {
         const float dx = displaced.vertices[i].px - rest.vertices[i].px;
         const float dy = displaced.vertices[i].py - rest.vertices[i].py;
         const float dz = displaced.vertices[i].pz - rest.vertices[i].pz;
         const float d2 = dx * dx + dy * dy + dz * dz;
         maxDispBefore = std::max(maxDispBefore, d2);
         if (d2 > 1e-8f)
            moved = true;
      }
   }
   printf("DSPTEST audio displacement moves mesh under a live signal: %s\n", moved ? "OK" : "FAIL");
   all &= moved;

   // Now silence the input and confirm the mesh relaxes back toward rest -
   // it must NOT stay frozen in its displaced pose (item 13's bug: the cache
   // signature used to key off an audio-frame counter that froze the moment
   // the ring stopped returning samples, leaving mCurrentEnergy's decay
   // invisible to the cache even as it played out underneath).
   std::fill(inL.begin(), inL.end(), 0.0f);
   std::fill(inR.begin(), inR.end(), 0.0f);

   for (int b = 0; b < 400; b++)
   {
      audioNode->ProcessBlock(inputs, 1, outBuf); // inputs now silent
      adisp.CookIfNeeded(frame++);
   }

   const Mesh& relaxed = adisp.GetMesh();
   float maxDispAfter = 0.0f;
   for (size_t i = 0; i < rest.vertices.size() && i < relaxed.vertices.size(); i++)
   {
      const float dx = relaxed.vertices[i].px - rest.vertices[i].px;
      const float dy = relaxed.vertices[i].py - rest.vertices[i].py;
      const float dz = relaxed.vertices[i].pz - rest.vertices[i].pz;
      maxDispAfter = std::max(maxDispAfter, dx * dx + dy * dy + dz * dz);
   }

   const bool relaxedOk = maxDispBefore > 1e-8f && maxDispAfter < maxDispBefore * 0.05f;
   printf("DSPTEST audio displacement relaxes to rest when audio stops: before=%.6f after=%.6f %s\n",
          maxDispBefore, maxDispAfter, relaxedOk ? "OK" : "FAIL");
   all &= relaxedOk;

   return all;
}

static int RunDspTest()
{
   StartupTrace("DSPTEST: gain fixture");
   const bool gainOk = RunGainFixture();
   StartupTrace("DSPTEST: filter fixture");
   const bool filterOk = RunFilterFixture();
   StartupTrace("DSPTEST: oscillator fixture");
   const bool oscWaveformOk = RunOscWaveformFixture();
   StartupTrace("DSPTEST: wavetable fixture");
   const bool noteSchedulingOk = RunWavetableFixture();
   StartupTrace("DSPTEST: envelope fixture");
   const bool envelopeOk = RunEnvelopeFixture();
   StartupTrace("DSPTEST: voice-steal fixture");
   const bool voiceStealOk = RunVoiceStealFixture();
   StartupTrace("DSPTEST: musical-time fixture");
   const bool musicTimeOk = RunMusicTimeFixture();
   StartupTrace("DSPTEST: audio-filter fixture");
   const bool audioFilterOk = RunAudioFilterFixture();
   StartupTrace("DSPTEST: dynamics fixture");
   const bool dynamicsOk = RunDynamicsFixture();
   StartupTrace("DSPTEST: delay fixture");
   const bool delayOk = RunDelayFixture();
   StartupTrace("DSPTEST: reverb fixture");
   const bool reverbOk = RunReverbFixture();
   StartupTrace("DSPTEST: sampler fixture");
   const bool samplerOk = RunSamplerFixture();
   StartupTrace("DSPTEST: PaulStretch fixture");
   const bool paulStretchOk = RunPaulStretchFixture();
   StartupTrace("DSPTEST: granular fixture");
   const bool granularOk = RunGranularFixture();
   StartupTrace("DSPTEST: drum-sequencer fixture");
   const bool drumSeqOk = RunDrumSequencerFixture();
   StartupTrace("DSPTEST: wavetable-shaper fixture");
   const bool wavetableShaperOk = RunWavetableShaperFixture();
   StartupTrace("DSPTEST: EQ fixture");
   const bool eqOk = RunEqFixture();
   StartupTrace("DSPTEST: note-stack fixture");
   const bool noteStackOk = RunNoteStackFixture();
   StartupTrace("DSPTEST: frequency-shifter fixture");
   const bool freqShifterOk = RunFrequencyShifterFixture();
   StartupTrace("DSPTEST: spectral-synth fixture");
   const bool spectralSynthOk = RunImageSpectralSynthFixture();
   StartupTrace("DSPTEST: wave-terrain fixture");
   const bool waveTerrainOk = RunWaveTerrainFixture();
   StartupTrace("DSPTEST: equation fixture");
   const bool equationOk = RunEquationFixture();
   StartupTrace("DSPTEST: audio-displacement fixture");
   const bool audioDisplacementOk = RunAudioDisplacementFixture();
   const bool all = gainOk && filterOk && oscWaveformOk && noteSchedulingOk && envelopeOk && voiceStealOk &&
                    musicTimeOk && audioFilterOk && dynamicsOk && delayOk && reverbOk && samplerOk &&
                    paulStretchOk && granularOk && drumSeqOk && wavetableShaperOk && eqOk && noteStackOk &&
                    freqShifterOk && spectralSynthOk && waveTerrainOk && equationOk && audioDisplacementOk;
   printf("%s\n", all ? "DSPTEST OK" : "DSPTEST SUSPECT");
   StartupTrace("DSPTEST: complete");
   return all ? 0 : 1;
}
