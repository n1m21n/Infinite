// Audio-node sweep discovery + AUDIOPARAMSWEEPTEST / FM checks (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
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

// Walks every registered node type, constructs one throwaway instance of
// each to probe its shape, and keeps the ones `include` accepts.
std::vector<AudioSweepCandidate> DiscoverAudioSweepCandidates(
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
      float values[8] = {};
      bool DiffersFrom(const Signature& o) const
      {
         if (valid != o.valid)
            return true;
         for (int i = 0; i < 8; i++)
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
   // Seconds of transport time one block advances a SweepNeedsClock rig. 16x real time, so the
   // 12-block probe window spans several beats (a real one is a quarter of a beat: no onset in it).
   inline float SweepClockDt(int numFrames) { return 16.0f * (float)numFrames / 48000.0f; }

   struct Rig
   {
      std::unique_ptr<INode> node;
      AudioNode* audio = nullptr;
      NoteEventQueue inbox;
      int outboxCursor = -1; // this rig's own cursor on audio->NoteOutbox(), if it has one
      const AudioBuffer* driveInputs[kAudioMaxNodeInputs] = {};
      AudioBuffer drive[kAudioMaxNodeInputs];
      int numInputs = 0;
      ReadMode mode = ReadMode::kUnobservable;
      bool advanceClock = false; // INode::SweepNeedsClock: tick the transport before every block
      float driveL[512] = {};
      float driveR[512] = {};
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
      n->SweepPrepare();
      rig.advanceClock = n->SweepNeedsClock();
      {
         // Every rig starts from beat 0, so a control rig and an altered rig see identical clocks.
         Transport& tr = Transport::Instance();
         tr.SetPlaying(rig.advanceClock);
         tr.Seek(0.0);
      }
      rig.mode = ModeFor(n, cand.shape);

      FillDriveTone(rig.driveL, rig.driveR, blockSize, sampleRate);
      static float* driveChans[2];
      driveChans[0] = rig.driveL;
      driveChans[1] = rig.driveR;
      rig.numInputs = std::min(cand.shape.audioInputSlots, kAudioMaxNodeInputs);
      for (int i = 0; i < rig.numInputs; i++)
      {
         rig.drive[i].channels = driveChans;
         rig.drive[i].numChannels = 2;
         rig.drive[i].numFrames = blockSize;
         rig.driveInputs[i] = &rig.drive[i];
      }

      rig.audio->PrepareToPlay(sampleRate, blockSize);

      if (cand.shape.noteInputSlots > 0)
      {
         PushHeldNoteOn(rig);
         // Slot-aware on purpose: firstNoteInputSlot is 1 for WaveTerrain/
         // ImageSpectralSynth/Plugin, and going through the same overload the
         // topology builder uses is what keeps this sweep honest about the
         // slot dispatch (see WireNoteInboxLikeTopology).
         rig.audio->SetNoteInbox(std::max(0, cand.shape.firstNoteInputSlot), &rig.inbox,
                                 rig.inbox.RegisterConsumer());
      }
      if (NoteEventQueue* outbox = rig.audio->NoteOutbox())
         rig.outboxCursor = outbox->RegisterConsumer();
      return true;
   }

   inline Signature RunOneBlock(Rig& rig, int numFrames)
   {
      float* outChans[2] = { rig.scratchL, rig.scratchR };
      AudioBuffer out;
      out.channels = outChans;
      out.numChannels = 2;
      out.numFrames = numFrames;
      if (rig.advanceClock)
         Transport::Instance().Tick(SweepClockDt(numFrames)); // headless: no audio clock, Tick moves Beats()
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

   // Multi-block PCM window for nodes whose params need more than one block to show (see
   // INode::SweepMeasureBlocks / SweepSpectralSignature). Level mode sums per-block RMS and keeps the
   // loudest peak; spectral mode reduces the left channel to dB energy at 8 fixed frequencies
   // (Goertzel), which sees pitch, stretch and window changes that leave peak/RMS untouched.
   inline Signature RunPcmWindow(Rig& rig, int blockSize, int numBlocks, bool spectral, double sampleRate)
   {
      Signature s;
      s.valid = true;
      std::vector<float> left;
      if (spectral)
         left.reserve((size_t)numBlocks * blockSize);
      for (int b = 0; b < numBlocks; b++)
      {
         const Signature blk = RunOneBlock(rig, blockSize);
         if (spectral)
            left.insert(left.end(), rig.scratchL, rig.scratchL + blockSize);
         else
         {
            s.values[0] += blk.values[0];
            s.values[2] += blk.values[2];
            s.values[1] = std::max(s.values[1], blk.values[1]);
            s.values[3] = std::max(s.values[3], blk.values[3]);
         }
      }
      if (spectral)
      {
         static const double kFreqs[8] = { 150, 300, 450, 600, 900, 1500, 3000, 6000 };
         for (int k = 0; k < 8; k++)
         {
            const double w = 2.0 * M_PI * kFreqs[k] / sampleRate;
            const double coeff = 2.0 * std::cos(w);
            double q1 = 0.0, q2 = 0.0;
            for (float x : left)
            {
               const double q0 = coeff * q1 - q2 + (double)x;
               q2 = q1;
               q1 = q0;
            }
            const double power = (q1 * q1 + q2 * q2 - coeff * q1 * q2) / std::max<size_t>(1, left.size());
            s.values[k] = (float)(10.0 * std::log10(power + 1e-12));
         }
      }
      return s;
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
         if (rig.advanceClock)
            Transport::Instance().Tick(SweepClockDt(blockSize));
         rig.audio->ProcessBlock(rig.numInputs > 0 ? rig.driveInputs : nullptr, rig.numInputs, out);
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

   struct ParamTestResult
   {
      bool ok = false;
      bool skip = false;
      // Set when this param was reported via EffectParamDef::uiOnly rather
      // than by actually exercising it - see TestOneParam's early-out below.
      bool uiOnly = false;
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
         rig.node->SweepPostAlter(probeSlot.name, frame);
         return true;
      };

      // Note-outbox nodes measure over a multi-block window (mirroring
      // warmUpAndAlter's 12-block warmup) so delayed/arpeggiated events and
      // frameOffset-only changes land inside the measurement; latent nodes
      // run their latency pipeline so altered output reaches the read window.
      auto measure = [&](Rig& rig) -> Signature
      {
         if (rig.mode == ReadMode::kNoteOutbox)
            return RunNoteWindow(rig, blockSize, 12);
         const int lat = rig.audio ? rig.audio->LatencySamples() : 0;
         if (lat > 0)
         {
            const int extraBlocks = (lat + blockSize - 1) / blockSize;
            for (int b = 0; b < extraBlocks; b++)
               RunOneBlock(rig, blockSize);
         }
         const int window = rig.node->SweepMeasureBlocks();
         const bool spectral = rig.node->SweepSpectralSignature();
         if (rig.mode == ReadMode::kPcm && (window > 1 || spectral))
            return RunPcmWindow(rig, blockSize, std::max(1, window), spectral, sampleRate);
         return RunOneBlock(rig, blockSize);
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

      if (cand.shape.noteInputSlots == 0)
         return { false, false }; // no retrigger path - genuine miss for this candidate value

      // Retrigger both a control (unaltered) and the altered rig, and compare
      // those two against each other rather than either against `before` -
      // isolates the param's own effect from the retrigger's own effect.
      Rig rigControl;
      if (!warmUpAndAlter(rigControl, false))
         return { false, true };
      // Both retriggered rigs are measured at the same beat (12 warm-up blocks in), not one after the other.
      auto rewindToMeasurePoint = [&](const Rig& rig)
      {
         if (!rig.advanceClock)
            return;
         Transport& tr = Transport::Instance();
         tr.Seek(0.0);
         for (int b = 0; b < 12; b++)
            tr.Tick(SweepClockDt(blockSize));
      };
      PushHeldNoteOn(rigControl);
      rewindToMeasurePoint(rigControl);
      const Signature control = measure(rigControl);

      PushHeldNoteOn(rigAfter);
      rewindToMeasurePoint(rigAfter);
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
   inline ParamTestResult TestOneParam(const AudioSweepCandidate& cand, const ParamSlot& probeSlot,
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

int RunAudioParamSweepTest()
{
   using namespace AudioParamSweep;

   const double sampleRate = 48000.0;
   const int blockSize = 256;
   int frame = 1;

   std::vector<AudioSweepCandidate> candidates =
      DiscoverAudioSweepCandidates([](const AudioNodeShape& s) { return s.HasAudioNode(); });

   bool overallOk = true;
   for (const AudioSweepCandidate& cand : candidates)
   {
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
      for (auto& slot : randomizer.slots)
      {
         ParamTestResult r = TestOneParam(cand, slot, sampleRate, blockSize, frame);
         if (r.skip)
            continue;
         anyObservable = true;
         if (r.uiOnly)
            printf("  [pass] %-24s param '%s' is UI-only (no DSP effect by design)\n", cand.name.c_str(),
                   slot.name.c_str());
         else
            printf("  [%s] %-24s param '%s' reaches audio thread within one block\n",
                   r.ok ? "pass" : "FAIL", cand.name.c_str(), slot.name.c_str());
         if (!r.ok)
            allReached = false;
      }
      if (!anyObservable)
      {
         printf("  [SKIP] %-24s no synthetic input available (hardware/external-driven note source)\n",
                cand.name.c_str());
      }
      else if (!allReached)
      {
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
bool RunFmModeDebugCheck()
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

// RunFmModeDebugCheck above only confirms fmMode's *plumbing* reaches the
// audio node - it never renders a sample. This actually renders through
// OscillatorNode's free-running path with a full-scale sine "fm in"
// modulator, at fmDepth 0 and 2, for both fmMode values, and checks that the
// rendered signal audibly moves (zero-crossing count as a cheap brightness/
// instantaneous-frequency proxy). This is the regression test for
// docs/prompts/fm-mode-linear-through-zero-prompt.md: before that fix,
// "fm" mode was exponential pitch FM, which barely moved a 220Hz carrier at
// any sane depth (a fraction of a semitone per unit of modulator swing)
// while "pm" moved it drastically - the two modes should both move by a
// comparable order of magnitude at the same depth, not leave the gap the
// old exponential math did.
bool RunFmRenderCheck()
{
   bool ok = true;
   const double sampleRate = 44100.0;
   const int blockSize = 1024;
   const int numBlocks = 8;
   const float modFreqHz = 5000.0f; // well above the 220Hz carrier - real FM, not vibrato

   auto renderZeroCrossings = [&](int fmMode, float fmDepth) -> int
   {
      OscillatorNode n;
      n.waveform = OscillatorNode::kSine;
      n.frequency = 220.0f;
      n.fmMode = fmMode;
      n.fmDepth = fmDepth;
      n.CookIfNeeded(1);
      AudioNode* audioNode = n.GetAudioNode();
      audioNode->PrepareToPlay(sampleRate, blockSize);

      std::vector<float> modBuf(blockSize), outBuf(blockSize);
      float* modPtr = modBuf.data();
      float* outPtr = outBuf.data();
      AudioBuffer modBuffer;
      modBuffer.channels = &modPtr;
      modBuffer.numChannels = 1;
      modBuffer.numFrames = blockSize;
      AudioBuffer outBuffer;
      outBuffer.channels = &outPtr;
      outBuffer.numChannels = 1;
      outBuffer.numFrames = blockSize;
      const AudioBuffer* inputs[2] = { nullptr, &modBuffer };

      double phase = 0.0;
      const double phaseInc = 2.0 * M_PI * (double)modFreqHz / sampleRate;
      std::vector<float> lastBlock;
      for (int blk = 0; blk < numBlocks; blk++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            modBuf[i] = (float)sin(phase);
            phase += phaseInc;
         }
         audioNode->ProcessBlock(inputs, 2, outBuffer);
         if (blk == numBlocks - 1)
            lastBlock.assign(outBuf.begin(), outBuf.end());
      }

      int crossings = 0;
      for (size_t i = 1; i < lastBlock.size(); i++)
         if ((lastBlock[i - 1] < 0.0f) != (lastBlock[i] < 0.0f))
            crossings++;
      return crossings;
   };

   for (int fmMode = 0; fmMode <= 1; fmMode++)
   {
      const int atZero = renderZeroCrossings(fmMode, 0.0f);
      const int atDepth = renderZeroCrossings(fmMode, 2.0f);
      const int moved = std::abs(atDepth - atZero);
      const char* label = fmMode == 0 ? "pm" : "fm";
      const bool pass = moved >= 4;
      printf("  [%s] fmMode=%s zero-crossings depth0=%d depth2=%d (moved %d)\n",
             pass ? "pass" : "FAIL", label, atZero, atDepth, moved);
      if (!pass)
         ok = false;
   }

   printf("%s\n", ok ? "FM RENDER CHECK OK" : "FM RENDER CHECK FAIL");
   return ok;
}
}
