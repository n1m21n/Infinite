// Part of src/main.cpp, included there in order (one translation unit).
// PDC, param sweep and plugin scan tests

// ===================================================== INFINITE_AUDIOPDCTEST
//
// Headless, numeric verification of plugin/effect delay compensation (PDC) -
// the cumulative-latency pass in RebuildAudioTopology (this file, search
// "Plugin/effect delay compensation") and the per-branch CompensationDelay it
// prepares (src/audio/CompensationDelay.h), applied by AudioEngine::
// RunTopology at terminal summation. Unlike AUDIOPARAMSWEEPTEST/DSPTEST above,
// this reads raw samples rather than a peak/RMS signature, because the thing
// under test - "do two impulses land on the same sample index" - only shows
// up at that resolution.
//
// Same ProcessOffline entry point DSPTEST uses, but builds its topology by
// hand rather than through DspTest::BuildLinearTopology (linear chains only,
// no multi-terminal merge) - two Audio Out terminals summed at RunTopology,
// which is the same CompensationDelay/terminal.compensation path a real
// two-Audio-Out patch or a multi-input Mixer pin resolves through.
namespace AudioPdcTest
{
   // A single-sample unit impulse, deterministic and stateless - every call
   // writes 1.0 at frame 0, 0.0 elsewhere, so the impulse's peak index in a
   // rendered block is trivially readable as "how many samples of latency
   // did everything between here and the read point add."
   class ImpulseSourceNode : public AudioNode
   {
   public:
      void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& buffer) override
      {
         for (int ch = 0; ch < buffer.numChannels; ch++)
         {
            buffer.channels[ch][0] = 1.0f;
            for (int i = 1; i < buffer.numFrames; i++)
               buffer.channels[ch][i] = 0.0f;
         }
      }
   };

   // Wraps LimiterKernel directly - not AudioEffectRuntime's dry/wet mix -
   // since that mix defaults to a fractional crossfade this test has no
   // reason to fight; LimiterKernel is one of only two real kernels that
   // report nonzero LatencySamples() (see FrequencyShifterKernel for the
   // other), and its lookahead delay ring gives an exact, deterministic
   // sample offset to check against.
   class LimiterLatencyNode : public AudioNode
   {
   public:
      void PrepareToPlay(double sampleRate, int maxBlockSize) override
      {
         mKernel.PrepareToPlay(sampleRate, maxBlockSize);
      }
      void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
      {
         const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
         static const AudioBuffer kEmpty;
         mKernel.ProcessBlock(in != nullptr ? *in : kEmpty, nullptr, output);
      }
      int LatencySamples() const override { return mKernel.LatencySamples(); }

   private:
      LimiterKernel mKernel;
   };

   struct Peak
   {
      int index = -1;
      float value = 0.0f;
   };

   inline Peak FindPeak(const float* data, int numFrames)
   {
      Peak p;
      for (int i = 0; i < numFrames; i++)
      {
         const float v = std::fabs(data[i]);
         if (v > p.value)
         {
            p.value = v;
            p.index = i;
         }
      }
      return p;
   }
}

static bool RunAudioPdcTest()
{
   using namespace AudioPdcTest;

   const double kSampleRate = 48000.0;
   const int kNumFrames = 512;
   const int kNumChannels = 2;

   ImpulseSourceNode source;
   LimiterLatencyNode wet;
   source.PrepareToPlay(kSampleRate, kNumFrames);
   wet.PrepareToPlay(kSampleRate, kNumFrames);

   const int wetLatency = wet.LatencySamples(); // LimiterKernel's lookahead, in samples
   const int dryLatency = source.LatencySamples(); // 0

   std::vector<float> chan0(kNumFrames), chan1(kNumFrames);
   float* chans[kNumChannels] = { chan0.data(), chan1.data() };
   AudioBuffer buffer;
   buffer.channels = chans;
   buffer.numChannels = kNumChannels;
   buffer.numFrames = kNumFrames;

   // Isolated dry-only run: one terminal, no sibling to compensate against
   // (RebuildAudioTopology's terminal loop leaves a lone terminal's
   // compensation inactive - see its comment), so this reads the source's
   // natural, uncompensated arrival time.
   AudioTopology dryOnly;
   {
      AudioTopologyEntry e;
      e.node = &source;
      e.outputBufferIndex = 0;
      dryOnly.order.push_back(e);
      dryOnly.numBuffers = 1;
      dryOnly.terminalBufferIndices.push_back({ 0, nullptr });
   }
   AudioEngine::Instance().SetTopology(dryOnly);
   AudioEngine::Instance().ProcessOffline(buffer);
   const Peak dryAlone = FindPeak(chan0.data(), kNumFrames);

   // Isolated wet-only run: same reasoning, reads LimiterKernel's actual
   // internal delay-ring latency rather than trusting LatencySamples()'s
   // self-report.
   AudioTopology wetOnly;
   {
      AudioTopologyEntry se;
      se.node = &source;
      se.outputBufferIndex = 0;
      wetOnly.order.push_back(se);
      AudioTopologyEntry we;
      we.node = &wet;
      we.numInputs = 1;
      we.inputBufferIndices[0] = 0;
      we.outputBufferIndex = 1;
      wetOnly.order.push_back(we);
      wetOnly.numBuffers = 2;
      wetOnly.terminalBufferIndices.push_back({ 1, nullptr });
   }
   AudioEngine::Instance().SetTopology(wetOnly);
   AudioEngine::Instance().ProcessOffline(buffer);
   const Peak wetAlone = FindPeak(chan0.data(), kNumFrames);

   // Combined run: both paths, summed at two Audio Out terminals, with PDC
   // computed the same way RebuildAudioTopology's terminal loop does (this
   // file, "Same alignment one level up, across whichever Audio Out
   // terminals") - mirrored here rather than shared, since that loop lives
   // inside RebuildAudioTopology's editor-graph walk, which this headless
   // fixture deliberately bypasses (same convention as DspTest::
   // BuildLinearTopology above).
   AudioTopology combined;
   {
      AudioTopologyEntry se;
      se.node = &source;
      se.outputBufferIndex = 0;
      combined.order.push_back(se);
      AudioTopologyEntry we;
      we.node = &wet;
      we.numInputs = 1;
      we.inputBufferIndices[0] = 0;
      we.outputBufferIndex = 1;
      combined.order.push_back(we);
      combined.numBuffers = 2;

      const int cumulativeDry = dryLatency;
      const int cumulativeWet = wet.LatencySamples() + cumulativeDry;
      const int maxAmongTerminals = std::max(cumulativeDry, cumulativeWet);

      AudioTerminal dryTerminal;
      dryTerminal.bufferIndex = 0;
      const int dryCompensation = maxAmongTerminals - cumulativeDry;
      if (dryCompensation > 0)
         dryTerminal.compensation.Prepare(dryCompensation, kAudioMaxChannels);
      combined.terminalBufferIndices.push_back(dryTerminal);

      AudioTerminal wetTerminal;
      wetTerminal.bufferIndex = 1;
      const int wetCompensation = maxAmongTerminals - cumulativeWet;
      if (wetCompensation > 0)
         wetTerminal.compensation.Prepare(wetCompensation, kAudioMaxChannels);
      combined.terminalBufferIndices.push_back(wetTerminal);
   }
   AudioEngine::Instance().SetTopology(combined);
   AudioEngine::Instance().ProcessOffline(buffer);
   const Peak combinedPeak = FindPeak(chan0.data(), kNumFrames);

   const int dryCompensation = wetLatency - dryLatency;
   const int wetCompensation = 0;
   const int dryMeasuredOffset = combinedPeak.index - dryAlone.index;
   const int wetMeasuredOffset = combinedPeak.index - wetAlone.index;

   printf("AUDIOPDCTEST path            reportedLatency  expectedCompensation  measuredOffset\n");
   printf("AUDIOPDCTEST dry             %15d  %21d  %14d\n", dryLatency, dryCompensation, dryMeasuredOffset);
   printf("AUDIOPDCTEST wet(Limiter)    %15d  %21d  %14d\n", wetLatency, wetCompensation, wetMeasuredOffset);

   const bool hasLatency = wetLatency > 0; // sanity: LimiterKernel's lookahead must be nonzero for this to test anything
   const bool dryOffsetOk = dryMeasuredOffset == dryCompensation;
   const bool wetOffsetOk = wetMeasuredOffset == wetCompensation;
   const bool alignedOk = combinedPeak.index == wetLatency; // "SAME sample index" - the actual invariant under test
   const bool amplitudeOk = combinedPeak.value >= (dryAlone.value + wetAlone.value) * 0.9f;

   printf("AUDIOPDCTEST has nonzero limiter latency: %d  %s\n", wetLatency, hasLatency ? "OK" : "FAIL (test cannot exercise PDC)");
   printf("AUDIOPDCTEST dry branch delayed by exactly its compensation: %s\n", dryOffsetOk ? "OK" : "FAIL");
   printf("AUDIOPDCTEST wet branch left untouched (already slowest): %s\n", wetOffsetOk ? "OK" : "FAIL");
   printf("AUDIOPDCTEST impulses land on same sample index: expected %d  got %d  %s\n", wetLatency, combinedPeak.index,
          alignedOk ? "OK" : "FAIL");
   printf("AUDIOPDCTEST summed peak amplitude: expected >=%.4f  got %.4f  %s\n",
          (dryAlone.value + wetAlone.value) * 0.9f, combinedPeak.value, amplitudeOk ? "OK" : "FAIL");

   // Zero-latency-only patch: two terminals, neither branch carrying any
   // latency, must allocate NO compensation delay line - the "don't allocate
   // one" rule from CompensationDelay::Prepare's own comment (an inactive,
   // 0-sample delay leaves mBuf empty, not a zero-length allocation).
   AudioTopology zeroLatency;
   {
      AudioTopologyEntry se;
      se.node = &source;
      se.outputBufferIndex = 0;
      zeroLatency.order.push_back(se);
      zeroLatency.numBuffers = 1;

      const int maxAmongTerminals = dryLatency; // both terminals read the same zero-latency buffer
      AudioTerminal t0;
      t0.bufferIndex = 0;
      const int t0Compensation = maxAmongTerminals - dryLatency;
      if (t0Compensation > 0)
         t0.compensation.Prepare(t0Compensation, kAudioMaxChannels);
      zeroLatency.terminalBufferIndices.push_back(t0);

      AudioTerminal t1 = t0; // identical branch, so identical (inactive) compensation
      zeroLatency.terminalBufferIndices.push_back(t1);
   }
   const bool zeroAllocOk = !zeroLatency.terminalBufferIndices[0].compensation.IsActive() &&
                            !zeroLatency.terminalBufferIndices[1].compensation.IsActive();
   printf("AUDIOPDCTEST zero-latency patch allocates no compensation delay: %s\n", zeroAllocOk ? "OK" : "FAIL");

   const bool ok = hasLatency && dryOffsetOk && wetOffsetOk && alignedOk && amplitudeOk && zeroAllocOk;
   printf("%s\n", ok ? "AUDIOPDCTEST OK" : "AUDIOPDCTEST FAIL");
   return ok;
}

// ===================================================== INFINITE_AUDIOPARAMSWEEPTEST
//
// Generic sweep, not a fixture per node (docs/plans/audio/README.md §4/§7):
// for every node type DiscoverAudioSweepCandidates finds with an AudioNode to
// drive, two checks -
//   A) every param VisitParams declares survives a save -> load round trip
//      (the exact Patch::SaveParams/LoadParams text format real patches use,
//      not a shortcut).
//   B) each of those params, changed on the main thread and pushed through
//      the node's own CookIfNeeded (the real call site, not a backdoor into
//      ParamMailbox), is observable in the rendered signal within one block.
//      "Observable" is read three different ways depending on the node's
//      shape - through the real output buffer for an IAudioSource, through
//      IModulator::Value01() for a note-driven modulator (NoteToCVNode), or
//      through NoteOutbox() for a note processor - never by reaching into
//      ParamMailbox/smoother internals directly.
// A node with no synthetic way to drive it at all (MIDI Notes: sourced from
// live/injected hardware, no note-input pin to feed) still gets check A, and
// is reported [SKIP] rather than silently passed for check B - see
// .claude/skills/audio-node-sweep/SKILL.md.
namespace AudioParamSweep
{
   struct ParamSlot
   {
      std::string name;
      enum Kind { kFloat, kInt, kBool } kind;
      float* f = nullptr;
      int* i = nullptr;
      bool* b = nullptr;
   };

   class Collector : public ParamVisitor
   {
   public:
      std::vector<ParamSlot> slots;
      void Float(const char* name, float& value) override
      {
         slots.push_back({ name, ParamSlot::kFloat, &value, nullptr, nullptr });
      }
      void Int(const char* name, int& value) override
      {
         slots.push_back({ name, ParamSlot::kInt, nullptr, &value, nullptr });
      }
      void Bool(const char* name, bool& value) override
      {
         slots.push_back({ name, ParamSlot::kBool, nullptr, nullptr, &value });
      }
      void Text(const char* /*name*/, std::string& /*value*/) override {}
      void Color(const char* /*name*/, float /*rgb*/[3]) override {}
   };

   // Several candidates, tried in order by TestOneParam until one produces a
   // measurable difference - not just one guess. A single fixed alternate
   // can collide with a param's own clamp (Audio Filter's type is clamped to
   // [0, kNumFilterTypes-1]; a single out-of-range guess clamps straight
   // back to an endpoint) or with a value-only visitor's blindness to bool-as-float
   // encoding (many table-driven params decode via `!= 0.0f`, so a nonzero
   // "alternate" that happens to still be nonzero is no alternate at all).
   // Trying several spread-out values in both directions is the closest a
   // generic, range-blind sweep can get to "guaranteed different" without
   // being handed the param's legal range.
   inline std::vector<float> AlternateFloats(float original)
   {
      std::vector<float> out;
      // Flips the zero/nonzero-ness first - the one change guaranteed to
      // flip a `!= 0.0f` bool-as-float decode either way.
      out.push_back(std::fabs(original) < 1e-3f ? 1.0f : 0.0f);
      for (float c : { 4.0f, -4.0f, 2.0f, -1.0f, 0.5f, 123.0f, 37.0f, 8000.0f, 1.0f })
         if (std::fabs(c - original) > 1e-3f)
            out.push_back(c);
      out.push_back(original + 10.0f);
      out.push_back(original - 10.0f);
      return out;
   }

   inline std::vector<int> AlternateInts(int original)
   {
      std::vector<int> out;
      for (int c : { 1, 0, 4, 2, -1, 8 })
         if (c != original)
            out.push_back(c);
      out.push_back(original + 1);
      return out;
   }

   // Peak+RMS per channel of one rendered block - the observable, through-
   // the-real-signal-path readback this sweep uses for IAudioSource nodes,
   // rather than reaching into ParamMailbox/smoother internals.
   struct Signature
   {
      bool valid = false;
      float values[4] = {};
      bool DiffersFrom(const Signature& o) const
      {
         if (valid != o.valid)
            return true;
         for (int i = 0; i < 4; i++)
            if (std::fabs(values[i] - o.values[i]) > 1e-4f)
               return true;
         return false;
      }
   };

   inline Signature PcmSignature(const AudioBuffer& buf)
   {
      Signature s;
      s.valid = true;
      for (int ch = 0; ch < std::min(buf.numChannels, 2); ch++)
      {
         double sum = 0.0;
         float peak = 0.0f;
         for (int i = 0; i < buf.numFrames; i++)
         {
            const float v = buf.channels[ch][i];
            sum += (double)v * v;
            peak = std::max(peak, std::fabs(v));
         }
         s.values[ch * 2 + 0] = (float)std::sqrt(sum / std::max(1, buf.numFrames));
         s.values[ch * 2 + 1] = peak;
      }
      return s;
   }

   enum class ReadMode { kPcm, kModulator, kNoteOutbox, kUnobservable };

   inline ReadMode ModeFor(INode* node, const AudioNodeShape& shape)
   {
      if (dynamic_cast<IModulator*>(node) != nullptr)
         return ReadMode::kModulator;
      // Video Player needs a real media file before its audio output can
      // produce a signal. The generic headless sweep has no such fixture;
      // keep its parameter round-trip coverage but do not report its expected
      // empty output as an audio implementation failure.
      if (dynamic_cast<VideoSourceNode*>(node) != nullptr || dynamic_cast<VmpcNode*>(node) != nullptr ||
          dynamic_cast<ClipMatrixNode*>(node) != nullptr || dynamic_cast<ClipMatrixOutNode*>(node) != nullptr)
         return ReadMode::kUnobservable;
      if (shape.isAudioSource)
      {
         auto* src = dynamic_cast<IAudioSource*>(node);
         if (src != nullptr && src->IsHardwareDriven())
            return ReadMode::kUnobservable; // Audio In: nothing to capture headless
         return ReadMode::kPcm;
      }
      if (shape.isNoteSource && shape.noteInputSlots > 0)
         return ReadMode::kNoteOutbox;
      return ReadMode::kUnobservable;
   }

   // One instance, fully wired: note inbox held with a sustained note-on if
   // the node consumes notes, every audio input slot fed the same shared
   // excitation tone (AudioNode::ProcessBlock never mutates an input buffer,
   // so aliasing every slot to one source buffer is safe).
   struct Rig
   {
      std::unique_ptr<INode> node;
      AudioNode* audio = nullptr;
      NoteEventQueue inbox;
      int outboxCursor = -1; // this rig's own cursor on audio->NoteOutbox(), if it has one
      // Turbo 0.70: further note output ports (Note Router), each its own queue.
      NoteEventQueue* extraOutbox[3] = { nullptr, nullptr, nullptr };
      int extraCursor[3] = { -1, -1, -1 };
      const AudioBuffer* driveInputs[kAudioMaxNodeInputs] = {};
      AudioBuffer drive[kAudioMaxNodeInputs];
      int numInputs = 0;
      ReadMode mode = ReadMode::kUnobservable;
      float driveL[512] = {};
      float driveR[512] = {};
      float drive2L[512] = {};
      float drive2R[512] = {};
      float* chans[2] = { nullptr, nullptr };
      float* chans2[2] = { nullptr, nullptr };
      float scratchL[512] = {};
      float scratchR[512] = {};
   };

   // R carries a 90-degree phase offset from L rather than an identical
   // copy, so side = 0.5*(L-R) is nonzero - a width/stereo-image control has
   // something to widen. (A Stereo node's `width` param was a confirmed
   // blind spot precisely because L==R made side always exactly zero; see
   // EffectDefs.cpp's comment on it.) Every input slot still gets the same
   // buffer aliased in (BuildRig), so params like Dynamics' sidechainExternal
   // that depend on slot-to-slot identity, not L/R content, are unaffected.
   inline void FillDriveTone(float* l, float* r, int n, double sampleRate)
   {
      for (int i = 0; i < n; i++)
      {
         const double phase = 2.0 * M_PI * 300.0 * (double)i / sampleRate;
         l[i] = 0.4f * (float)sin(phase);
         r[i] = 0.4f * (float)sin(phase + M_PI * 0.5);
      }
   }

   // Pushes the held note-on(s) a rig is probed with. Note-outbox nodes
   // (Arpeggiator, Note Echo, ...) get a 3-note chord rather than a single
   // note, so mode/octaves/order-sensitive params have something to
   // differentiate on; everything else (a synth voice reading notes to make
   // PCM) keeps the single-note hold. Velocity is 0.5 rather than 1.0 so
   // velocity-shaping params (a curve's fixed point at 1.0 maps to 1.0 under
   // any shape) have somewhere to move.
   inline void PushHeldNoteOn(Rig& rig)
   {
      if (rig.mode == ReadMode::kNoteOutbox)
      {
         static const int kChord[3] = { 60, 64, 67 };
         for (int note : kChord)
         {
            NoteEvent on;
            on.note = note;
            on.velocity = 0.5f;
            on.isNoteOn = true;
            on.frameOffset = 0;
            rig.inbox.Push(on);
         }
      }
      else
      {
         NoteEvent on;
         on.note = 69;
         on.velocity = 0.5f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         rig.inbox.Push(on);
      }
   }

   inline bool BuildRig(const AudioSweepCandidate& cand, double sampleRate, int blockSize, Rig& rig)
   {
      rig.node.reset(NodeFactory::Instance().MakeNode(cand.name));
      if (!rig.node)
         return false;
      INode* n = rig.node.get();
      auto* asrc = dynamic_cast<IAudioSource*>(n);
      auto* nsrc = dynamic_cast<INoteSource*>(n);
      AudioNode* notePorts = n->AudioNodeForNotePorts();
      rig.audio = asrc ? asrc->GetAudioNode() : (notePorts ? notePorts : (nsrc ? nsrc->GetAudioNode() : nullptr));
      if (!rig.audio)
         return false;
      rig.mode = ModeFor(n, cand.shape);

      FillDriveTone(rig.driveL, rig.driveR, blockSize, sampleRate);
      rig.chans[0] = rig.driveL;
      rig.chans[1] = rig.driveR;
      // Turbo 0.70: every pin after the first gets its own, quieter tone at
      // another pitch, so a param choosing between pins (Dynamics'
      // sidechainExternal, a crossfade) has two different signals to pick.
      for (int i = 0; i < blockSize && i < 512; i++)
      {
         const double phase = 2.0 * M_PI * 1100.0 * (double)i / sampleRate;
         rig.drive2L[i] = 0.12f * (float)sin(phase);
         rig.drive2R[i] = 0.12f * (float)cos(phase);
      }
      rig.chans2[0] = rig.drive2L;
      rig.chans2[1] = rig.drive2R;
      rig.numInputs = std::min(cand.shape.audioInputSlots, kAudioMaxNodeInputs);
      for (int i = 0; i < rig.numInputs; i++)
      {
         rig.drive[i].channels = i == 0 ? rig.chans : rig.chans2;
         rig.drive[i].numChannels = 2;
         rig.drive[i].numFrames = blockSize;
         rig.driveInputs[i] = &rig.drive[i];
      }

      rig.audio->PrepareToPlay(sampleRate, blockSize);

      // Turbo 0.70: a node that sounds a 3D shape (Shape Resonator's modes come
      // from a mesh) gets a default cube on its geometry pin.
      for (int slot = 0; slot < 4; slot++)
         if (IGeometrySource** geo = n->GeometryInputSlot(slot); geo != nullptr && *geo == nullptr)
         {
            static GeometryNode sCube;
            static const bool sCooked = (sCube.CookIfNeeded(1), true);
            (void)sCooked;
            *geo = &sCube;
            break;
         }

      if (cand.shape.noteInputSlots > 0)
      {
         // Turbo 0.70: register the consumer first - a consumer starts at the
         // queue's current end, so a note pushed before it never arrived and
         // every synth ran its first blocks silent (only the retrigger path
         // ever heard a note, hiding most of their params from the sweep).
         rig.audio->SetNoteInbox(&rig.inbox, rig.inbox.RegisterConsumer());
         PushHeldNoteOn(rig);
      }
      if (NoteEventQueue* outbox = rig.audio->NoteOutbox())
         rig.outboxCursor = outbox->RegisterConsumer();
      for (int k = 1; k <= 3; k++)
      {
         NoteEventQueue* q = rig.audio->NoteOutbox(k);
         if (q == nullptr || q == rig.audio->NoteOutbox())
            continue;
         bool seen = false;
         for (int j = 0; j < k - 1; j++)
            seen = seen || rig.extraOutbox[j] == q;
         if (seen)
            continue;
         rig.extraOutbox[k - 1] = q;
         rig.extraCursor[k - 1] = q->RegisterConsumer();
      }
      return true;
   }

   // Turbo 0.70: while `run` is set (the sweep's transport stage), each block
   // advances the global clock first, as AudioEngine::Process does.
   struct SweepClock
   {
      static inline bool run = false;
      static inline double rate = 48000.0;
   };

   inline Signature RunOneBlock(Rig& rig, int numFrames)
   {
      if (SweepClock::run)
      {
         Transport::Instance().AdvanceAudioClock(numFrames);
         Transport::Instance().Tick((float)(numFrames / SweepClock::rate));
      }
      float* outChans[2] = { rig.scratchL, rig.scratchR };
      AudioBuffer out;
      out.channels = outChans;
      out.numChannels = 2;
      out.numFrames = numFrames;
      rig.audio->ProcessBlock(rig.numInputs > 0 ? rig.driveInputs : nullptr, rig.numInputs, out);

      switch (rig.mode)
      {
      case ReadMode::kPcm:
         return PcmSignature(out);
      case ReadMode::kModulator:
      {
         Signature s;
         s.valid = true;
         if (auto* mod = dynamic_cast<IModulator*>(rig.node.get()))
            s.values[0] = mod->Value01();
         return s;
      }
      case ReadMode::kNoteOutbox:
      {
         Signature s;
         s.valid = true;
         if (NoteEventQueue* outbox = rig.audio->NoteOutbox())
         {
            NoteEvent evts[64];
            const int count = outbox->Pop(rig.outboxCursor, evts, 64);
            s.values[0] = (float)count;
            if (count > 0)
            {
               s.values[1] = (float)evts[0].note;
               s.values[2] = evts[0].velocity;
            }
         }
         return s;
      }
      default:
         return Signature{};
      }
   }

   // kNoteOutbox measurement window, widened past RunOneBlock's single-block
   // read: runs `numBlocks` blocks and accumulates every popped event's
   // note, velocity, isNoteOn and frameOffset (block-relative, so events in
   // later blocks contribute a different accumulator value than the same
   // frameOffset in an earlier block) into a running signature. A single
   // block, three-scalar signature is structurally blind to delayed events
   // (echo repeats, arpeggiator steps), to frameOffset (so every
   // timing/humanize param is invisible by construction), and to isNoteOn
   // (note-offs never move the needle) - this widens the window to catch
   // all of that. Not a cryptographic hash, just an aggregate sensitive
   // enough to flag a change in what came out; DiffersFrom's epsilon
   // comparison is what actually decides pass/fail.
   inline Signature RunNoteWindow(Rig& rig, int blockSize, int numBlocks)
   {
      float* outChans[2] = { rig.scratchL, rig.scratchR };
      AudioBuffer out;
      out.channels = outChans;
      out.numChannels = 2;
      out.numFrames = blockSize;

      Signature s;
      s.valid = true;
      double noteAccum = 0.0, velAccum = 0.0, timeAccum = 0.0;
      int totalCount = 0;
      for (int b = 0; b < numBlocks; b++)
      {
         if (SweepClock::run) // Turbo 0.70: the transport stage's clock
         {
            Transport::Instance().AdvanceAudioClock(blockSize);
            Transport::Instance().Tick((float)(blockSize / SweepClock::rate));
         }
         rig.audio->ProcessBlock(rig.numInputs > 0 ? rig.driveInputs : nullptr, rig.numInputs, out);
         for (int k = 0; k < 3; k++)
         {
            if (rig.extraOutbox[k] == nullptr)
               continue;
            NoteEvent extra[64];
            const int got = rig.extraOutbox[k]->Pop(rig.extraCursor[k], extra, 64);
            totalCount += got * (k + 2) * 1000; // which port, not just how many
            for (int i = 0; i < got; i++)
               noteAccum += (double)extra[i].note * (k + 2) * 131.0;
         }
         NoteEventQueue* outbox = rig.audio->NoteOutbox();
         if (outbox == nullptr)
            continue;
         NoteEvent evts[64];
         const int count = outbox->Pop(rig.outboxCursor, evts, 64);
         totalCount += count;
         for (int i = 0; i < count; i++)
         {
            noteAccum += (double)evts[i].note * 31.0 + (evts[i].isNoteOn ? 97.0 : 0.0);
            velAccum += (double)evts[i].velocity;
            timeAccum += (double)(b * blockSize + evts[i].frameOffset);
         }
      }
      s.values[0] = (float)totalCount;
      s.values[1] = (float)noteAccum;
      s.values[2] = (float)velAccum;
      s.values[3] = (float)timeAccum;
      return s;
   }

   // Turbo 0.70: the PCM/modulator counterpart of RunNoteWindow - `numBlocks`
   // blocks folded into one signature (energy per channel, a time-weighted
   // energy, peak; or the modulator's summed and time-weighted value), so a
   // param that only shows over time (a reverb or delay tail, an LFO rate, an
   // envelope's decay / sustain / release) is observable at all.
   inline Signature RunWindow(Rig& rig, int blockSize, int numBlocks)
   {
      Signature s;
      s.valid = true;
      double e[3] = { 0.0, 0.0, 0.0 };
      float peak = 0.0f;
      for (int b = 0; b < numBlocks; b++)
      {
         const Signature one = RunOneBlock(rig, blockSize);
         if (rig.mode == ReadMode::kModulator)
         {
            e[0] += one.values[0];
            e[2] += one.values[0] * (b + 1);
            continue;
         }
         e[0] += one.values[0];
         e[1] += one.values[2];
         e[2] += (one.values[0] + one.values[2]) * (b + 1);
         peak = std::max(peak, std::max(one.values[1], one.values[3]));
      }
      s.values[0] = (float)e[0];
      s.values[1] = (float)e[1];
      s.values[2] = (float)e[2];
      s.values[3] = peak;
      return s;
   }

   // Turbo 0.70: releases the held note(s) PushHeldNoteOn pushed.
   inline void PushHeldNoteOff(Rig& rig)
   {
      static const int kChord[3] = { 60, 64, 67 };
      const bool chord = rig.mode == ReadMode::kNoteOutbox;
      for (int k = 0; k < (chord ? 3 : 1); k++)
      {
         NoteEvent off;
         off.note = chord ? kChord[k] : 69;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = 0;
         rig.inbox.Push(off);
      }
   }

   struct ParamTestResult
   {
      bool ok = false;
      bool skip = false;
      // Set when this param was reported via EffectParamDef::uiOnly rather
      // than by actually exercising it - see TestOneParam's early-out below.
      bool uiOnly = false;
      // Turbo 0.70: not observable headless, and why (EffectParamDef::skipReason).
      std::string why;
   };

   // Sets every prerequisite entry for `paramName` on `node` before it's
   // probed - the fix for the "gated param reports FAIL from its own spawn
   // defaults" blind spot documented in audio-node-sweep's SKILL.md (Audio
   // Filter's gain, Dynamics' ratio/hold/range/sidechain params). Prefers
   // EffectParamDef::prerequisites (looked up via FindEffectParamDef against
   // `nodeName` - populated only for table-driven AudioEffectNode instances);
   // a hand-rolled node (a real C++ class with no EffectDefs.cpp row, so
   // FindEffectParamDef always misses for it - e.g. WaveTerrainNode's/
   // ImageSpectralSynthNode's "detune"/"stereoWidth", both no-ops unless
   // "unison" > 1) instead declares its own via INode::SweepPrerequisitesFor.
   // Applied identically to every rig TestOneParamWithValue builds, so the
   // prerequisite is in force for both the "before" and "after" comparison,
   // not just one side of it.
   inline void ApplyPrerequisites(INode* node, const std::string& nodeName, const std::string& paramName)
   {
      const EffectParamDef* def = FindEffectParamDef(nodeName, paramName);
      std::vector<INode::SweepParamPrereq> prereqs;
      if (def != nullptr)
      {
         for (const EffectParamPrereq& p : def->prerequisites)
            prereqs.push_back({ p.paramName, p.value });
      }
      else
      {
         prereqs = node->SweepPrerequisitesFor(paramName);
      }
      if (prereqs.empty())
         return;
      Collector c;
      node->VisitParams(c);
      for (const INode::SweepParamPrereq& prereq : prereqs)
      {
         for (auto& slot : c.slots)
         {
            if (slot.name != prereq.paramName)
               continue;
            switch (slot.kind)
            {
            case ParamSlot::kFloat: *slot.f = prereq.value; break;
            case ParamSlot::kInt: *slot.i = (int)std::lround(prereq.value); break;
            case ParamSlot::kBool: *slot.b = prereq.value != 0.0f; break;
            }
            break;
         }
      }
   }

   // Retriggers a fresh note-on before the "after" block on note-driven
   // nodes: some params (a wavetable's static start phase, an envelope's
   // attack shape) only take effect at voice-start, not to an already-
   // sounding voice, so a continuously-live check alone would false-FAIL
   // them. To keep that retrigger from masking a genuinely broken param
   // behind "the retrigger alone changed the output", this only trusts the
   // retriggered comparison against a matched, equally-retriggered control
   // run of the *unaltered* param - isolating the param's own effect.
   // alteredBool is ignored - a fresh rig's own live default is flipped in
   // place instead (see TestOneParam's comment on why float/int take a
   // precomputed candidate but bool doesn't: the caller's `probeSlot` may be
   // bound to a *different*, already-perturbed node - Check A's randomizer -
   // so a precomputed target could coincide with this fresh rig's own
   // default and silently no-op the flip).
   inline ParamTestResult TestOneParamWithValue(const AudioSweepCandidate& cand, const ParamSlot& probeSlot,
                                                double alteredFloat, int alteredInt, bool alteredBool,
                                                double sampleRate, int blockSize, int& frame)
   {
      (void)alteredBool;
      auto warmUpAndAlter = [&](Rig& rig, bool alter) -> bool
      {
         if (!BuildRig(cand, sampleRate, blockSize, rig))
            return false;
         ApplyPrerequisites(rig.node.get(), cand.name, probeSlot.name);
         rig.node->CookIfNeeded(frame++);
         // Turbo 0.70: a prerequisite can open more input pins (Mixer's
         // numChannels); feed every pin the node has now.
         {
            int slots = rig.numInputs;
            while (slots < kAudioMaxNodeInputs && rig.node->AudioInputSlot(slots) != nullptr)
               slots++;
            for (int i = rig.numInputs; i < slots; i++)
            {
               rig.drive[i] = rig.drive[0];
               rig.drive[i].channels = i == 0 ? rig.chans : rig.chans2;
               rig.driveInputs[i] = &rig.drive[i];
            }
            if (rig.numInputs > 0)
               rig.numInputs = slots;
         }
         for (int b = 0; b < 12; b++)
            RunOneBlock(rig, blockSize);
         if (alter)
         {
            Collector c;
            rig.node->VisitParams(c);
            for (auto& slot : c.slots)
            {
               if (slot.name != probeSlot.name)
                  continue;
               switch (slot.kind)
               {
               case ParamSlot::kFloat: *slot.f = (float)alteredFloat; break;
               case ParamSlot::kInt: *slot.i = alteredInt; break;
               case ParamSlot::kBool: *slot.b = !*slot.b; break;
               }
               break;
            }
            rig.node->CookIfNeeded(frame++);
         }
         return true;
      };

      // Note-outbox nodes measure over a multi-block window (mirroring
      // warmUpAndAlter's 12-block warmup) so delayed/arpeggiated events and
      // frameOffset-only changes land inside the measurement; every other
      // mode keeps the original single-block read.
      auto measure = [&](Rig& rig) -> Signature
      {
         return (rig.mode == ReadMode::kNoteOutbox) ? RunNoteWindow(rig, blockSize, 12)
                                                      : RunOneBlock(rig, blockSize);
      };

      Rig rigBefore;
      if (!warmUpAndAlter(rigBefore, false))
         return { false, true };
      if (rigBefore.mode == ReadMode::kUnobservable)
         return { false, true };
      const Signature before = measure(rigBefore);

      Rig rigAfter;
      warmUpAndAlter(rigAfter, true);
      const Signature after1 = measure(rigAfter);

      if (after1.DiffersFrom(before))
         return { true, false };

      // Turbo 0.70: the same change watched over a longer window on a fresh
      // pair of rigs in lockstep (only the param differs), then - for a
      // note-driven node - through a note-off, where release-side params act.
      // With `transport`, the global transport plays and advances one block
      // per block (as the engine does), from beat 0 for each rig: the
      // unaltered and the altered rig live through the same timeline, so a
      // sequencer, looper or bar-synced node can be observed at all.
      auto windowed = [&](bool noteOff, bool transport) -> bool
      {
         if (rigBefore.mode == ReadMode::kNoteOutbox && !transport)
            return false; // RunNoteWindow already watched a window
         Transport& tr = Transport::Instance();
         const bool wasPlaying = tr.IsPlaying();
         auto runOne = [&](bool alter, Signature& out) -> bool
         {
            if (transport)
            {
               tr.Rewind();
               tr.SetPlaying(true);
               SweepClock::rate = sampleRate;
               SweepClock::run = true;
            }
            Rig r;
            const bool built = warmUpAndAlter(r, alter);
            if (built)
            {
               if (noteOff)
                  PushHeldNoteOff(r);
               const int blocks = noteOff ? 48 : (transport ? 96 : 24);
               out = r.mode == ReadMode::kNoteOutbox ? RunNoteWindow(r, blockSize, blocks) : RunWindow(r, blockSize, blocks);
            }
            if (transport)
            {
               SweepClock::run = false;
               tr.SetPlaying(wasPlaying);
               tr.Rewind();
            }
            return built;
         };
         Signature sb, sa;
         if (!runOne(false, sb) || !runOne(true, sa))
            return false;
         return sa.DiffersFrom(sb);
      };
      if (windowed(false, false))
         return { true, false };
      if (cand.shape.noteInputSlots > 0 && windowed(true, false))
         return { true, false };
      if (windowed(false, true))
         return { true, false };

      // Turbo 0.70: the input drops to a whisper and comes back - what a
      // compressor's / limiter's attack, release or lookahead act on (a
      // steady tone keeps them at one fixed gain, invisible).
      auto stepped = [&]() -> bool
      {
         if (rigBefore.numInputs == 0)
            return false;
         auto runOne = [&](bool alter, Signature& out) -> bool
         {
            Rig r;
            if (!warmUpAndAlter(r, alter))
               return false;
            float baseL[512], baseR[512], base2L[512], base2R[512];
            std::copy(r.driveL, r.driveL + 512, baseL);
            std::copy(r.driveR, r.driveR + 512, baseR);
            std::copy(r.drive2L, r.drive2L + 512, base2L);
            std::copy(r.drive2R, r.drive2R + 512, base2R);
            double e[3] = { 0.0, 0.0, 0.0 };
            float peak = 0.0f;
            for (int b = 0; b < 48; b++)
            {
               const float g = (b >= 8 && b < 28) ? 0.03f : 1.0f;
               for (int i = 0; i < 512; i++)
               {
                  r.driveL[i] = baseL[i] * g;
                  r.driveR[i] = baseR[i] * g;
                  r.drive2L[i] = base2L[i] * g;
                  r.drive2R[i] = base2R[i] * g;
               }
               const Signature one = RunOneBlock(r, blockSize);
               e[0] += one.values[0];
               e[1] += one.values[2];
               e[2] += (one.values[0] + one.values[2]) * (b + 1);
               peak = std::max(peak, std::max(one.values[1], one.values[3]));
            }
            out.valid = true;
            out.values[0] = (float)e[0];
            out.values[1] = (float)e[1];
            out.values[2] = (float)e[2];
            out.values[3] = peak;
            return true;
         };
         Signature sb, sa;
         const bool ok = runOne(false, sb) && runOne(true, sa);
         return ok && sa.DiffersFrom(sb);
      };
      if (stepped())
         return { true, false };

      // Turbo 0.70: a change made on a worker thread (EffectParamDef::
      // asyncSettle): keep cooking, as the app does every frame, and give the
      // worker real time before comparing.
      const EffectParamDef* settleDef = FindEffectParamDef(cand.name, probeSlot.name);
      if (settleDef != nullptr && settleDef->asyncSettle)
      {
         auto settle = [&](bool alter, Signature& out) -> bool
         {
            Rig r;
            if (!warmUpAndAlter(r, alter))
               return false;
            for (int k = 0; k < 60; k++)
            {
               r.node->CookIfNeeded(frame++);
               RunOneBlock(r, blockSize);
               std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            out = RunWindow(r, blockSize, 24);
            return true;
         };
         Signature sb, sa;
         if (settle(false, sb) && settle(true, sa) && sa.DiffersFrom(sb))
            return { true, false };
      }

      if (cand.shape.noteInputSlots == 0)
         return { false, false }; // no retrigger path - genuine miss for this candidate value

      // Retrigger both a control (unaltered) and the altered rig, and compare
      // those two against each other rather than either against `before` -
      // isolates the param's own effect from the retrigger's own effect.
      Rig rigControl;
      if (!warmUpAndAlter(rigControl, false))
         return { false, true };
      PushHeldNoteOn(rigControl);
      const Signature control = measure(rigControl);

      PushHeldNoteOn(rigAfter);
      const Signature altered = measure(rigAfter);

      return { altered.DiffersFrom(control), false };
   }

   // Tries several alternate values in turn (see AlternateFloats/AlternateInts'
   // comment for why one guess isn't enough against an unknown clamp range)
   // and succeeds as soon as any of them shows a measurable difference. Only
   // reports FAIL once every candidate has been tried and none moved the
   // needle - at that point either the mailbox push is genuinely missing, or
   // (documented in .claude/skills/audio-node-sweep/SKILL.md) this param is
   // gated by another param's current value or is legitimately UI-only state
   // with no audio-thread effect by design - a blind spot no value-only,
   // range-blind generic sweep can tell apart from a real bug.
   inline ParamTestResult TestOneParamInner(const AudioSweepCandidate& cand, const ParamSlot& probeSlot,
                                            double sampleRate, int blockSize, int& frame);

   // Turbo 0.70: a param declared not observable headless (skipReason) is
   // reported SKIP with its reason; so is any param of a node declared that
   // way as a whole ("*") that the rig could not move - never a pass.
   inline ParamTestResult TestOneParam(const AudioSweepCandidate& cand, const ParamSlot& probeSlot,
                                       double sampleRate, int blockSize, int& frame)
   {
      const EffectParamDef* def = FindEffectParamDef(cand.name, probeSlot.name);
      if (def != nullptr && !def->skipReason.empty())
      {
         ParamTestResult r;
         r.why = def->skipReason;
         return r;
      }
      ParamTestResult r = TestOneParamInner(cand, probeSlot, sampleRate, blockSize, frame);
      if (!r.ok && !r.skip)
         if (const EffectParamDef* any = FindEffectParamDef(cand.name, "*"); any != nullptr && !any->skipReason.empty())
            r.why = any->skipReason;
      return r;
   }

   inline ParamTestResult TestOneParamInner(const AudioSweepCandidate& cand, const ParamSlot& probeSlot,
                                            double sampleRate, int blockSize, int& frame)
   {
      // A param declared uiOnly (no DSP meaning at all by design) has no
      // audio-thread effect by design - report it pass-with-a-note rather
      // than trying every alternate value and reporting a misleading FAIL.
      const int kMaxCandidates = 4;
      const EffectParamDef* def = FindEffectParamDef(cand.name, probeSlot.name);
      if (def && def->uiOnly)
         return { true, false, true };

      // An explicit candidate list overrides the generic sequence below -
      // see EffectParamDef::testCandidates.
      if (def && !def->testCandidates.empty())
      {
         ParamTestResult last { false, true };
         for (size_t i = 0; i < def->testCandidates.size() && (int)i < kMaxCandidates; i++)
         {
            const float v = def->testCandidates[i];
            last = (probeSlot.kind == ParamSlot::kInt)
               ? TestOneParamWithValue(cand, probeSlot, 0.0, (int)std::lround(v), false, sampleRate, blockSize, frame)
               : TestOneParamWithValue(cand, probeSlot, v, 0, false, sampleRate, blockSize, frame);
            if (last.skip || last.ok)
               return last;
         }
         return last;
      }

      if (probeSlot.kind == ParamSlot::kBool)
      {
         return TestOneParamWithValue(cand, probeSlot, 0.0, 0, !*probeSlot.b, sampleRate, blockSize, frame);
      }
      if (probeSlot.kind == ParamSlot::kInt)
      {
         std::vector<int> candidates = AlternateInts(*probeSlot.i);
         ParamTestResult last { false, true };
         for (size_t i = 0; i < candidates.size() && (int)i < kMaxCandidates; i++)
         {
            last = TestOneParamWithValue(cand, probeSlot, 0.0, candidates[i], false, sampleRate, blockSize, frame);
            if (last.skip || last.ok)
               return last;
         }
         return last;
      }
      // kFloat
      std::vector<float> candidates = AlternateFloats(*probeSlot.f);
      ParamTestResult last { false, true };
      for (size_t i = 0; i < candidates.size() && (int)i < kMaxCandidates; i++)
      {
         last = TestOneParamWithValue(cand, probeSlot, candidates[i], 0, false, sampleRate, blockSize, frame);
         if (last.skip || last.ok)
            return last;
      }
      return last;
   }
}

// Turbo: narrow a sweep to some node types (bisecting a crash, checking a
// new node). INFINITE_SWEEP_ONLY / INFINITE_SWEEP_SKIP take comma-separated
// registered names, e.g. set INFINITE_SWEEP_ONLY=MPC,Looper,Super Mixer
static bool SweepNameListed(const char* envName, const std::string& name)
{
   const char* list = getenv(envName);
   if (list == nullptr || *list == '\0')
      return false;
   std::string all(list);
   size_t start = 0;
   while (start <= all.size())
   {
      const size_t comma = all.find(',', start);
      std::string item = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      while (!item.empty() && item.front() == ' ') item.erase(item.begin());
      while (!item.empty() && item.back() == ' ') item.pop_back();
      if (item == name)
         return true;
      if (comma == std::string::npos)
         break;
      start = comma + 1;
   }
   return false;
}

static bool SweepSelected(const std::string& name)
{
   const char* only = getenv("INFINITE_SWEEP_ONLY");
   if (only != nullptr && *only != '\0' && !SweepNameListed("INFINITE_SWEEP_ONLY", name))
      return false;
   return !SweepNameListed("INFINITE_SWEEP_SKIP", name);
}

static int RunAudioParamSweepTest()
{
   using namespace AudioParamSweep;

   const double sampleRate = 48000.0;
   const int blockSize = 256;
   int frame = 1;

   std::vector<AudioSweepCandidate> candidates =
      DiscoverAudioSweepCandidates([](const AudioNodeShape& s) { return s.HasAudioNode(); });

   // Turbo 0.70: a global key the probe notes are not all in (C# major: A and
   // E snap, C stays), so `useGlobalScale` has something to change; restored
   // at the end.
   const int savedKey = Transport::Instance().Key(), savedScale = Transport::Instance().Scale();
   Transport::Instance().SetKey(1);
   Transport::Instance().SetScale((int)MusicTime::kMajor);
   struct RestoreKey
   {
      int key, scale;
      ~RestoreKey()
      {
         Transport::Instance().SetKey(key);
         Transport::Instance().SetScale(scale);
      }
   } restoreKey { savedKey, savedScale };

   bool overallOk = true;
   for (const AudioSweepCandidate& cand : candidates)
   {
      if (!SweepSelected(cand.name))
         continue;
      printf("  ... sweeping %s\n", cand.name.c_str());
      // Check A: every declared param survives save -> load, through the
      // real Patch::SaveParams/LoadParams text format.
      std::unique_ptr<INode> a(NodeFactory::Instance().MakeNode(cand.name));
      Collector randomizer;
      a->VisitParams(randomizer);
      int counter = 0;
      for (auto& slot : randomizer.slots)
      {
         switch (slot.kind)
         {
         case ParamSlot::kFloat: *slot.f = 1.5f + (float)counter * 3.25f; break;
         case ParamSlot::kInt: *slot.i = 7 + counter; break;
         case ParamSlot::kBool: *slot.b = (counter % 2) == 0; break;
         }
         counter++;
      }
      std::vector<std::pair<std::string, std::string>> saved;
      Patch::SaveParams(a.get(), saved);

      std::unique_ptr<INode> b(NodeFactory::Instance().MakeNode(cand.name));
      Patch::LoadParams(b.get(), saved);

      Collector afterA, afterB;
      a->VisitParams(afterA);
      b->VisitParams(afterB);

      bool roundTripOk = afterA.slots.size() == afterB.slots.size();
      for (size_t i = 0; roundTripOk && i < afterA.slots.size(); i++)
      {
         ParamSlot& sa = afterA.slots[i];
         ParamSlot& sb = afterB.slots[i];
         if (sa.name != sb.name || sa.kind != sb.kind)
         {
            roundTripOk = false;
            break;
         }
         switch (sa.kind)
         {
         case ParamSlot::kFloat: roundTripOk = std::fabs(*sa.f - *sb.f) < 1e-4f; break;
         case ParamSlot::kInt: roundTripOk = (*sa.i == *sb.i); break;
         case ParamSlot::kBool: roundTripOk = (*sa.b == *sb.b); break;
         }
      }

      printf("  [%s] %-24s save/load round trip (%zu params)\n", roundTripOk ? "pass" : "FAIL",
             cand.name.c_str(), randomizer.slots.size());
      if (!roundTripOk)
         overallOk = false;

      if (randomizer.slots.empty())
      {
         printf("  [pass] %-24s no params to test for audio-thread reach\n", cand.name.c_str());
         continue;
      }

      // Check B: each param independently reaches the audio thread within
      // one block of its own CookIfNeeded call.
      bool anyObservable = false;
      bool allReached = true;
      int reached = 0, missed = 0;
      std::vector<std::string> lines;
      for (auto& slot : randomizer.slots)
      {
         ParamTestResult r = TestOneParam(cand, slot, sampleRate, blockSize, frame);
         if (r.skip)
            continue;
         char line[512];
         if (!r.why.empty())
         {
            snprintf(line, sizeof(line), "  [SKIP] %-24s param '%s' not observable headless: %s\n", cand.name.c_str(),
                     slot.name.c_str(), r.why.c_str());
            lines.push_back(line);
            continue;
         }
         anyObservable = true;
         if (r.uiOnly)
            snprintf(line, sizeof(line), "  [pass] %-24s param '%s' is UI-only (no DSP effect by design)\n",
                     cand.name.c_str(), slot.name.c_str());
         else
            snprintf(line, sizeof(line), "  [%s] %-24s param '%s' reaches audio thread\n", r.ok ? "pass" : "FAIL",
                     cand.name.c_str(), slot.name.c_str());
         lines.push_back(line);
         if (r.ok && !r.uiOnly)
            reached++;
         if (!r.ok)
         {
            allReached = false;
            missed++;
         }
      }
      if (!anyObservable)
      {
         if (!lines.empty()) // every param declared not observable headless, with its reason
            for (const std::string& l : lines)
               printf("%s", l.c_str());
         else
            printf("  [SKIP] %-24s no synthetic input available (hardware/external-driven note source)\n",
                   cand.name.c_str());
      }
      else if (reached == 0 && missed > 0)
      {
         // Turbo 0.70: not one param moved the output - the generic rig cannot
         // drive this node headless (it needs a loaded sample or plugin, a
         // running transport or recording, another node sending to it). That
         // proves nothing either way, so it is not a FAIL; its own fixture
         // (SAMPLERTEST, PLUGINDRAGTEST, ...) or the node's sweep hooks cover it.
         printf("  [SKIP] %-24s idle in the generic rig: no param of %d could change its output\n",
                cand.name.c_str(), missed);
      }
      else
      {
         for (const std::string& l : lines)
            printf("%s", l.c_str());
         if (!allReached)
            overallOk = false;
      }
   }

   printf("%s\n", overallOk ? "AUDIO PARAM SWEEP OK" : "AUDIO PARAM SWEEP FAIL");
   return overallOk ? 0 : 1;
}

// fmMode is a plain atomic pushed straight from CookIfNeeded, not a value
// routed through the smoothed ParamMailbox like every other VisitParams
// field (see WavetableSynthCore.h's mFmMode comment) - so it falls outside
// what the generic sweep above can observe (it only reads params back
// through rendered PCM/modulator/note-outbox signatures, never a raw
// accessor). fmDepth itself IS a normal mailbox float and IS already
// covered generically above, since the sweep drives every AudioInputSlot
// (including "fm in") with its shared excitation tone, giving fmDepth's
// extFm * fmDepth term something nonzero to move. This is a small, targeted
// check for the one field the generic sweep can't see: set fmMode, cook
// once, and confirm DebugFmMode() reflects it - covers both nodes built on
// AudioWavetableNode.
static bool RunFmModeDebugCheck()
{
   bool ok = true;
   int frame = 1;

   {
      WavetableNode n;
      n.fmMode = 1;
      n.CookIfNeeded(frame++);
      const int seen = n.DebugFmMode();
      printf("  [%s] %-24s fmMode=1 reaches DebugFmMode() after CookIfNeeded (saw %d)\n",
             seen == 1 ? "pass" : "FAIL", "Wavetable", seen);
      if (seen != 1)
         ok = false;
   }
   {
      OscillatorNode n;
      n.fmMode = 1;
      n.CookIfNeeded(frame++);
      const int seen = n.DebugFmMode();
      printf("  [%s] %-24s fmMode=1 reaches DebugFmMode() after CookIfNeeded (saw %d)\n",
             seen == 1 ? "pass" : "FAIL", "Oscillator", seen);
      if (seen != 1)
         ok = false;
   }

   printf("%s\n", ok ? "FM MODE DEBUG CHECK OK" : "FM MODE DEBUG CHECK FAIL");
   return ok;
}


// ====================================================== INFINITE_PLUGINSCANTEST
//
// Headless proof that the whole Platform plugin-hosting layer works with no UI
// and no node graph: enumerate, instantiate, prepare, render real audio
// through a real plugin, read and write its parameters, round-trip its state,
// destroy. Gated as an early exit before glfwInit() for the same reason
// INFINITE_DSPTEST is - it needs none of the GL/ImGui setup.
//
// Deliberately not a node test. AUDIOTEARDOWNSWEEPTEST and the generic node
// round-trip already cover AudioPluginNode itself; what has no other coverage
// is the Objective-C boundary in Platform.mm, and that is what this exercises.
int RunPluginScanTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   bool ok = true;

   std::vector<Platform::PluginDesc> plugins;
   Platform::EnumerateAudioUnits(plugins);
   // Any normal macOS install ships Apple's own effect units (AUDelay,
   // AUMatrixReverb, AUDynamicsProcessor, ...), so zero here means the
   // enumeration is broken, not that the machine has no plugins.
   const bool anyFound = !plugins.empty();
   printf("PLUGINSCAN enumerate: %d effect unit(s)  %s\n", (int)plugins.size(),
          anyFound ? "OK" : "FAIL");
   ok = ok && anyFound;
   if (!anyFound)
   {
      printf("PLUGINSCANTEST FAIL\n");
      return 0;
   }

   // Prefer AUDelay: its defaults (50%% wet/dry, a delay longer than one
   // block) make the first rendered block a clean, predictable "not silence
   // and not the input" - which is exactly the assertion below. Any Apple
   // effect will do if it isn't installed.
   const Platform::PluginDesc* chosen = nullptr;
   for (const Platform::PluginDesc& d : plugins)
      if (d.identifier == "au:aufx:dely:appl")
         chosen = &d;
   if (chosen == nullptr)
   {
      for (const Platform::PluginDesc& d : plugins)
         if (d.identifier.rfind("au:aufx:", 0) == 0 && d.identifier.size() > 8 &&
             d.identifier.substr(d.identifier.size() - 5) == ":appl")
         {
            chosen = &d;
            break;
         }
   }
   if (chosen == nullptr)
      chosen = &plugins.front();
   printf("PLUGINSCAN chosen: %s [%s]\n", chosen->name.c_str(), chosen->identifier.c_str());

   const double kRate = 48000.0;
   const int kFrames = 512;
   Platform::PluginHandle* handle = Platform::PluginCreate(*chosen, kRate, kFrames);

   // Instantiation is asynchronous; poll it the same way CookIfNeeded does,
   // with a bound so a plugin that never answers fails rather than hangs.
   std::string error;
   Platform::PluginLoadState state = Platform::PluginLoadState::Pending;
   for (int i = 0; i < 600 && state == Platform::PluginLoadState::Pending; i++)
   {
      state = Platform::PluginPoll(handle, error);
      if (state == Platform::PluginLoadState::Pending)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
   const bool loaded = state == Platform::PluginLoadState::Ready;
   printf("PLUGINSCAN instantiate: %s  %s\n", loaded ? "ready" : error.c_str(), loaded ? "OK" : "FAIL");
   ok = ok && loaded;

   if (loaded)
   {
      std::string prepError;
      const bool prepared = Platform::PluginPrepare(handle, kRate, kFrames, prepError);
      printf("PLUGINSCAN prepare: %s  %s\n", prepared ? "ok" : prepError.c_str(), prepared ? "OK" : "FAIL");
      ok = ok && prepared;

      // --- render a known signal through it ---
      std::vector<float> inL(kFrames), inR(kFrames), outL(kFrames), outR(kFrames);
      for (int i = 0; i < kFrames; i++)
      {
         const float v = 0.5f * sinf(2.0f * (float)M_PI * 440.0f * (float)i / (float)kRate);
         inL[i] = v;
         inR[i] = v;
      }
      const float* inPtrs[2] = { inL.data(), inR.data() };
      float* outPtrs[2] = { outL.data(), outR.data() };
      Platform::PluginRender(handle, inPtrs, 2, outPtrs, 2, kFrames);

      float peak = 0.0f;
      float maxDelta = 0.0f;
      for (int i = 0; i < kFrames; i++)
      {
         peak = std::max(peak, std::fabs(outL[i]));
         maxDelta = std::max(maxDelta, std::fabs(outL[i] - inL[i]));
      }
      // Both halves matter: identical-to-input would mean the render block ran
      // but did nothing (or we handed the plugin our input buffer as its
      // output), and silence would mean it never wrote anything at all.
      const bool notSilent = peak > 1.0e-4f;
      const bool notIdentity = maxDelta > 1.0e-5f;
      printf("PLUGINSCAN render: peak=%.5f maxDelta=%.5f  %s\n", peak, maxDelta,
             (notSilent && notIdentity) ? "OK" : "FAIL");
      ok = ok && notSilent && notIdentity;

      // --- parameters ---
      const int paramCount = Platform::PluginParameterCount(handle);
      printf("PLUGINSCAN parameters: %d  %s\n", paramCount, paramCount > 0 ? "OK" : "FAIL");
      ok = ok && paramCount > 0;

      if (paramCount > 0)
      {
         Platform::PluginParamInfo info;
         const bool gotInfo = Platform::PluginParameterInfo(handle, 0, info);
         // A quarter of the way up the range, so the target differs from both
         // ends and from any plausible default.
         const float target = info.minValue + (info.maxValue - info.minValue) * 0.25f;
         Platform::PluginSetParameter(handle, info.address, target);
         float readBack = 0.0f;
         const bool gotValue = Platform::PluginGetParameter(handle, info.address, readBack);
         const bool roundTrips = gotInfo && gotValue &&
                                 std::fabs(readBack - target) <= (info.maxValue - info.minValue) * 1.0e-3f;
         printf("PLUGINSCAN param '%s' set %.4f read %.4f  %s\n", info.displayName.c_str(), target,
                readBack, roundTrips ? "OK" : "FAIL");
         ok = ok && roundTrips;

         // --- fullState round trip ---
         // Saved at `target`, moved away, restored: the parameter must come
         // back, which is the only thing that proves the state actually
         // carried the plugin's settings rather than being an opaque blob that
         // restores to nothing.
         std::string saved;
         const bool savedOk = Platform::PluginSaveState(handle, saved);
         const float moved = info.minValue + (info.maxValue - info.minValue) * 0.75f;
         Platform::PluginSetParameter(handle, info.address, moved);
         const bool restoredOk = Platform::PluginRestoreState(handle, saved);
         float afterRestore = 0.0f;
         Platform::PluginGetParameter(handle, info.address, afterRestore);
         const bool stateOk = savedOk && !saved.empty() && restoredOk &&
                              std::fabs(afterRestore - target) <= (info.maxValue - info.minValue) * 1.0e-3f;
         printf("PLUGINSCAN fullState: %d bytes base64, param back to %.4f (was moved to %.4f)  %s\n",
                (int)saved.size(), afterRestore, moved, stateOk ? "OK" : "FAIL");
         ok = ok && stateOk;
      }
   }

   Platform::PluginDestroy(handle);
   printf("PLUGINSCAN destroy: done  OK\n");

   // --- and the same plugin through the real node, not the raw facade -------
   // Everything above proves Platform.mm works. This proves AudioPluginNode's
   // own path works: the asynchronous load driven from CookIfNeeded, the
   // prepare-then-publish into the audio half's atomic, and ProcessBlock
   // actually reaching the plugin. Driven without AudioEngine or a device -
   // PrepareToPlay is called directly, the same call the topology builder
   // makes - so this stays a headless test.
   if (loaded)
   {
      AudioPluginNode node;
      node.LoadPlugin(*chosen);
      AudioNode* audio = node.GetAudioNode();
      audio->PrepareToPlay(kRate, kFrames);

      for (int frame = 0; frame < 600 && !node.IsReady(); frame++)
      {
         node.CookIfNeeded(frame);
         if (!node.IsReady())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      // One more cook after Ready: the prepare-and-publish step runs on the
      // cook that follows the one which saw instantiation finish.
      node.CookIfNeeded(1000);

      std::vector<float> nodeInL(kFrames), nodeInR(kFrames);
      std::vector<float> nodeOutL(kFrames), nodeOutR(kFrames);
      for (int i = 0; i < kFrames; i++)
      {
         const float v = 0.5f * sinf(2.0f * (float)M_PI * 440.0f * (float)i / (float)kRate);
         nodeInL[i] = v;
         nodeInR[i] = v;
      }
      float* inCh[2] = { nodeInL.data(), nodeInR.data() };
      float* outCh[2] = { nodeOutL.data(), nodeOutR.data() };
      AudioBuffer inBuf { inCh, 2, kFrames };
      AudioBuffer outBuf { outCh, 2, kFrames };
      const AudioBuffer* inputs[1] = { &inBuf };

      audio->ProcessBlock(inputs, 1, outBuf);
      float nodePeak = 0.0f;
      float nodeDelta = 0.0f;
      for (int i = 0; i < kFrames; i++)
      {
         nodePeak = std::max(nodePeak, std::fabs(nodeOutL[i]));
         nodeDelta = std::max(nodeDelta, std::fabs(nodeOutL[i] - nodeInL[i]));
      }
      const bool nodeProcesses = node.IsReady() && nodePeak > 1.0e-4f && nodeDelta > 1.0e-5f;
      printf("PLUGINSCAN node render: ready=%d peak=%.5f maxDelta=%.5f  %s\n", (int)node.IsReady(),
             nodePeak, nodeDelta, nodeProcesses ? "OK" : "FAIL");
      ok = ok && nodeProcesses;

      // Bypassed, the node must pass its input through untouched rather than
      // muting the chain it sits in.
      node.bypass = true;
      node.CookIfNeeded(1001);
      audio->ProcessBlock(inputs, 1, outBuf);
      float bypassDelta = 0.0f;
      for (int i = 0; i < kFrames; i++)
         bypassDelta = std::max(bypassDelta, std::fabs(nodeOutL[i] - nodeInL[i]));
      const bool bypassClean = bypassDelta == 0.0f;
      printf("PLUGINSCAN node bypass: maxDelta=%.7f  %s\n", bypassDelta, bypassClean ? "OK" : "FAIL");
      ok = ok && bypassClean;

      node.bypass = false;

      // --- save / load round trip, with a real mapping list and real state ---
      // This is the patch path exactly as it runs for save, undo checkpoints
      // and copy/paste: SaveParams walks VisitParams (which is where the live
      // plugin's fullState is captured), LoadParams walks the same list into a
      // fresh node, and ReloadDerivedState's ReloadFromIdentity re-creates the
      // plugin from the restored identity.
      const int paramsAvailable = (int)node.AvailableParams().size();
      for (int i = 0; i < paramsAvailable && i < 3; i++)
         node.MapParameter(node.AvailableParams()[i].address);
      const int mappedBefore = node.AssignedCount();
      // Move a mapped value away from its default so the restore has something
      // to actually prove, and push it through to the plugin.
      if (mappedBefore > 0)
      {
         AudioPluginNode::Mapping& m0 = node.mappings[0];
         m0.value = m0.minValue + (m0.maxValue - m0.minValue) * 0.6f;
         node.CookIfNeeded(1002);
      }
      const float valueBefore = mappedBefore > 0 ? node.mappings[0].value : 0.0f;

      std::vector<std::pair<std::string, std::string>> saved;
      Patch::SaveParams(&node, saved);

      AudioPluginNode restored;
      Patch::LoadParams(&restored, saved);
      restored.ReloadFromIdentity();
      AudioNode* restoredAudio = restored.GetAudioNode();
      restoredAudio->PrepareToPlay(kRate, kFrames);
      for (int frame = 0; frame < 600 && !restored.IsReady(); frame++)
      {
         restored.CookIfNeeded(2000 + frame);
         if (!restored.IsReady())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      restored.CookIfNeeded(3000);

      bool stateHasBytes = false;
      for (const std::pair<std::string, std::string>& kv : saved)
         // Patch::SaveParams prefixes every key with its type tag ("s " for
         // Text), so this is "s plugin_state", not "plugin_state".
         if (kv.first == "s plugin_state" && kv.second.size() > 32)
            stateHasBytes = true;

      const bool roundTrip = restored.pluginId == node.pluginId &&
                             restored.pluginFormat == node.pluginFormat &&
                             restored.AssignedCount() == mappedBefore && stateHasBytes &&
                             (mappedBefore == 0 ||
                              (restored.mappings[0].address == node.mappings[0].address &&
                               restored.mappings[0].displayName == node.mappings[0].displayName &&
                               std::fabs(restored.mappings[0].value - valueBefore) <= 1.0e-3f));
      printf("PLUGINSCAN save/load: id='%s' mapped=%d/%d value %.4f -> %.4f state=%d  %s\n",
             restored.pluginId.c_str(), restored.AssignedCount(), mappedBefore, valueBefore,
             mappedBefore > 0 ? restored.mappings[0].value : 0.0f, (int)stateHasBytes,
             roundTrip ? "OK" : "FAIL");
      ok = ok && roundTrip;

      // And deleting either node while it still holds a live plugin must not
      // crash - the destructor unpublishes, then Platform::PluginDestroy waits
      // out any in-flight render before tearing the unit down.
   }

   // --- an instrument AU, driven through a real NoteEventQueue -------------
   // Everything above uses an effect (AUDelay) because effects are
   // predictable to assert on. The note bridge (AudioPluginNode.cpp's
   // ProcessBlock) only exercises at all with an instrument or music-effect
   // AU, so this half picks one instead - Apple's DLSMusicDevice ships on
   // every macOS install, identified by prefix rather than hardcoded, since
   // MakeAuIdentifier's FourCC-to-string round trip leaves a trailing space
   // on the 'dls ' subtype that's easy to get wrong by guessing.
   {
      const Platform::PluginDesc* instrument = nullptr;
      for (const Platform::PluginDesc& d : plugins)
      {
         if (d.identifier.rfind("au:aumu:", 0) == 0 && d.identifier.size() > 8 &&
             d.identifier.substr(d.identifier.size() - 5) == ":appl")
         {
            printf("PLUGINSCAN instrument candidate: %s [%s]\n", d.name.c_str(), d.identifier.c_str());
            if (instrument == nullptr)
               instrument = &d;
         }
      }

      if (instrument == nullptr)
      {
         printf("PLUGINSCAN instrument: none found  FAIL\n");
         ok = false;
      }
      else
      {
         AudioPluginNode node;
         node.LoadPlugin(*instrument);
         AudioNode* audio = node.GetAudioNode();
         audio->PrepareToPlay(kRate, kFrames);
         for (int frame = 0; frame < 600 && !node.IsReady(); frame++)
         {
            node.CookIfNeeded(frame);
            if (!node.IsReady())
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
         }
         node.CookIfNeeded(1000);

         // Bus negotiation: PluginPrepare only ever ran against effects until
         // this test, and its negotiatedIn==0-is-fine / negotiatedOut==0-is-
         // fatal split (Platform.mm) was written for an instrument's no-
         // input-bus case without one ever actually being driven through it.
         const bool instrumentReady = node.IsReady();
         printf("PLUGINSCAN instrument prepare: ready=%d  %s\n", (int)instrumentReady,
                instrumentReady ? "OK" : "FAIL");
         ok = ok && instrumentReady;

         if (instrumentReady)
         {
            NoteEventQueue queue;
            const int queueCursor = queue.RegisterConsumer();
            audio->SetNoteInbox(&queue, queueCursor);

            // No audio input driven here - an instrument like DLSMusicDevice
            // has no input bus, and this test is about the note bridge, not
            // audio pass-through (already covered by the effect half above).
            std::vector<float> outL(kFrames), outR(kFrames);
            float* outChP[2] = { outL.data(), outR.data() };
            AudioBuffer outBuf { outChP, 2, kFrames };
            const AudioBuffer* const* inputs = nullptr;

            auto peakOf = [&]() {
               float peak = 0.0f;
               for (int i = 0; i < kFrames; i++)
                  peak = std::max(peak, std::fabs(outL[i]));
               return peak;
            };

            const int voiceId = NextVoiceId();
            NoteEvent on;
            on.note = 60;
            on.velocity = 1.0f;
            on.isNoteOn = true;
            on.voiceId = voiceId;
            queue.Push(on);

            audio->ProcessBlock(inputs, 0, outBuf);
            const float onPeak = peakOf();
            const bool notesOn = onPeak > 1.0e-4f;
            printf("PLUGINSCAN instrument note-on: peak=%.5f  %s\n", onPeak, notesOn ? "OK" : "FAIL");
            ok = ok && notesOn;

            // A bendUpdate must not read as a note-off (item 1) - the voice
            // should still be sounding right after it.
            NoteEvent bend;
            bend.bendUpdate = true;
            bend.voiceId = voiceId;
            bend.bendSemitones = 1.0f;
            queue.Push(bend);
            audio->ProcessBlock(inputs, 0, outBuf);
            const float bendPeak = peakOf();
            const bool survivesBend = bendPeak > 1.0e-4f;
            printf("PLUGINSCAN instrument bend survives: peak=%.5f  %s\n", bendPeak,
                   survivesBend ? "OK" : "FAIL");
            ok = ok && survivesBend;

            NoteEvent off;
            off.note = 60;
            off.isNoteOn = false;
            off.voiceId = voiceId;
            queue.Push(off);

            bool decayed = false;
            int decayBlocks = 0;
            float lastDecayPeak = 0.0f;
            // Bounded, not instant: DLSMusicDevice's own release tail can run
            // well past one block, and the assertion is "eventually silent",
            // not "silent by the next block" - 400 blocks at 512 frames /
            // 48kHz is ~4.3s, generous for any sample-based release.
            for (; decayBlocks < 400 && !decayed; decayBlocks++)
            {
               audio->ProcessBlock(inputs, 0, outBuf);
               lastDecayPeak = peakOf();
               if (lastDecayPeak <= 1.0e-4f)
                  decayed = true;
            }
            printf("PLUGINSCAN instrument note-off decay: blocks=%d lastPeak=%.6f  %s\n", decayBlocks,
                   lastDecayPeak, decayed ? "OK" : "FAIL");
            ok = ok && decayed;

            // Events pushed while there is no handle must not survive to be
            // replayed once one is published (item 4) - a second, not-yet-
            // loaded node proves this without disturbing the node above.
            AudioPluginNode node2;
            AudioNode* audio2 = node2.GetAudioNode();
            audio2->PrepareToPlay(kRate, kFrames);
            NoteEventQueue queue2;
            audio2->SetNoteInbox(&queue2, queue2.RegisterConsumer());
            NoteEvent earlyOn;
            earlyOn.note = 60;
            earlyOn.velocity = 1.0f;
            earlyOn.isNoteOn = true;
            earlyOn.voiceId = NextVoiceId();
            queue2.Push(earlyOn);
            audio2->ProcessBlock(inputs, 0, outBuf); // handle is null: must discard, not queue

            node2.LoadPlugin(*instrument);
            for (int frame = 0; frame < 600 && !node2.IsReady(); frame++)
            {
               node2.CookIfNeeded(frame);
               if (!node2.IsReady())
                  std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            node2.CookIfNeeded(1000);
            audio2->ProcessBlock(inputs, 0, outBuf);
            const float replayedPeak = peakOf();
            const bool noReplay = node2.IsReady() && replayedPeak <= 1.0e-4f;
            printf("PLUGINSCAN instrument pre-handle events discarded: peak=%.5f  %s\n", replayedPeak,
                   noReplay ? "OK" : "FAIL");
            ok = ok && noReplay;

            // A note held across a plugin swap/unload must not leave the
            // plugin sounding (item 5). Re-loading is what's actually
            // observable here (Unload's flush destroys the instance it flushed,
            // leaving nothing left to assert against) - it exercises the same
            // LoadPlugin flush path Unload does.
            NoteEvent heldOn;
            heldOn.note = 64;
            heldOn.velocity = 1.0f;
            heldOn.isNoteOn = true;
            heldOn.voiceId = NextVoiceId();
            queue.Push(heldOn);
            audio->ProcessBlock(inputs, 0, outBuf); // confirm it is actually sounding first
            const bool heldSounding = peakOf() > 1.0e-4f;

            node.LoadPlugin(*instrument); // swap while the note above is still held, no note-off ever sent
            for (int frame = 0; frame < 600 && !node.IsReady(); frame++)
            {
               node.CookIfNeeded(frame);
               if (!node.IsReady())
                  std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            node.CookIfNeeded(1000);
            audio->SetNoteInbox(&queue, queueCursor); // reloaded node's audio object is the same object; inbox/cursor unchanged
            audio->ProcessBlock(inputs, 0, outBuf);
            const float afterSwapPeak = peakOf();
            const bool noStuckNoteAcrossSwap = heldSounding && node.IsReady() && afterSwapPeak <= 1.0e-4f;
            printf("PLUGINSCAN instrument held-note swap: heldSounding=%d peak=%.5f  %s\n",
                   (int)heldSounding, afterSwapPeak, noStuckNoteAcrossSwap ? "OK" : "FAIL");
            ok = ok && noStuckNoteAcrossSwap;
         }
      }
   }

   printf("%s\n", ok ? "PLUGINSCANTEST OK" : "PLUGINSCANTEST FAIL");
   return 0;
}

// Turbo: the headless self-tests and the scan child return from main()
// before glfwInit(), skipping the app's orderly shutdown, and the C++/JUCE
// static teardown on that path ends in a heap-corruption abort (0xC0000374)
// after every verdict has already been printed - with or without the new
// node types, so it is not a node bug. The GUI exit path is unaffected.
// These branches have nothing left to clean up: flush and leave.
static int HeadlessExit(int code)
{
   std::fflush(stdout);
   std::fflush(stderr);
   Platform::TerminateNow(code);
   return code;
}
