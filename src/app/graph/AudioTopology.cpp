// Audio chain collection, topology rebuild, engine start and recovery (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Walks backward from `node` through its own audio inputs before adding
   // an entry for `node` itself, so sources land before their consumers -
   // AudioEngine::RunTopology walks `outOrder` front-to-back, so that order
   // is load-bearing, not cosmetic (see docs/plans/audio/
   // audio-graph-semantics.md §3). Each entry gets its own pooled output
   // buffer, indexed by its position in `outOrder`; `bufferIndexOf` records
   // that mapping as nodes are pushed so a later entry can look up which
   // buffer(s) its own inputs should read. `visited` both dedupes a source
   // feeding several consumers (each consumer just looks up the same buffer
   // index - no reprocessing, no aliasing) and backstops WouldCreateAudioCycle
   // in case a cycle ever slips through.
   void CollectAudioChain(INode* node, std::set<INode*>& visited,
                           std::vector<AudioTopologyEntry>& outOrder,
                           std::unordered_map<AudioNode*, int>& bufferIndexOf,
                           int& nextBufferIndex)
   {
      if (node == nullptr || visited.count(node) != 0)
         return;
      visited.insert(node);

      AudioTopologyEntry entry;
      // No early break on the first null slot: audio and note pins share one
      // slot index space (see the "a node with a note pin at slot 0 and an
      // audio pin at slot 1" comment a few hundred lines up), so a node like
      // Sample Player - note pin at slot 0, its recording input at slot 1 -
      // has a real gap at slot 0 that isn't "past its declared slot count".
      // Scanning the full range costs nothing (kMaxAudioSlots is 8) and
      // matches the equally-generic teardown loop nearby, which never
      // assumed contiguity either.
      for (int slot = 0; slot < kMaxAudioSlots && slot < kAudioMaxNodeInputs; slot++)
      {
         AudioCable* cable = node->AudioInputSlot(slot);
         if (cable == nullptr)
            continue;
         entry.numInputs = slot + 1;
         if (cable->IsConnected())
         {
            bool hopped = false;
            INode* resolved = ResolvedAudioSource(cable->GetSource(), &hopped);
            if (resolved != nullptr)
            {
               CollectAudioChain(resolved, visited, outOrder, bufferIndexOf, nextBufferIndex);
               const int outputSlot = hopped ? 0 : cable->GetOutputSlot();
               entry.inputBufferIndices[slot] = AudioBufferIndexOf(resolved, outputSlot, bufferIndexOf);
            }
         }
      }

      // A note-consuming node's producer must be visited (and land earlier
      // in outOrder) before this node's own entry is appended, exactly like
      // the audio-input walk above - the note-cable analogue of the same
      // ordering requirement.
      for (int slot = 0; slot < kMaxNoteSlots; slot++)
      {
         NoteCable* cable = node->NoteInputSlot(slot);
         if (cable != nullptr && cable->IsConnected())
         {
            INode* resolved = ResolvedAudioSource(cable->GetSource());
            if (resolved != nullptr)
               CollectAudioChain(resolved, visited, outOrder, bufferIndexOf, nextBufferIndex);
         }
      }

      if (AudioNode* audioNode = AudioNodeOfAny(node))
      {
         if (!node->bypassed)
         {
            entry.node = audioNode;
            entry.noteOnly = dynamic_cast<INoteSource*>(node) != nullptr || node->AudioNodeForNotePorts() != nullptr;
            if (auto* aen = dynamic_cast<AudioEffectNode*>(node))
            {
               const std::string& name = aen->Def().name;
               if (name == "Audio Filter") entry.stageId = kAudioStageFilter;
               else if (name == "Wavetable Shaper" || name == "Drive" || name == "Bitcrush" || name == "Cycle Shaper" || name == "Transient Shaper") entry.stageId = kAudioStageShaper;
               else if (name == "Delay") entry.stageId = kAudioStageDelay;
               else if (name == "Reverb") entry.stageId = kAudioStageReverb;
               else if (name == "Dynamics") entry.stageId = kAudioStageDynamics;
               else entry.stageId = kAudioStageOther;
            }
            else if (dynamic_cast<SamplerNode*>(node) || dynamic_cast<WavetableNode*>(node) || dynamic_cast<OscillatorNode*>(node))
            {
               entry.stageId = kAudioStageSynths;
            }
            else if (dynamic_cast<MixerNode*>(node) || dynamic_cast<SpatialMixerNode*>(node))
            {
               entry.stageId = kAudioStageMixer;
            }
            else
            {
               entry.stageId = kAudioStageOther;
            }
            const int numOuts = std::clamp(audioNode->AudioOutputCount(), 1, kAudioMaxNodeOutputs);
            entry.numOutputs = numOuts;
            entry.outputBufferIndex = nextBufferIndex;
            for (int o = 0; o < numOuts; o++)
               entry.outputBufferIndices[o] = nextBufferIndex + o;
            bufferIndexOf[audioNode] = nextBufferIndex;
            nextBufferIndex += numOuts;
            outOrder.push_back(entry);
         }
      }
   }


   const std::set<uint64_t>& ArrangeVideoSourceConflictClips()
   {
      if (gArrangeVideoConflictBuiltRevision == gArrange.revision)
         return gArrangeVideoSourceConflictClipIds;
      gArrangeVideoConflictBuiltRevision = gArrange.revision;
      gArrangeVideoSourceConflictClipIds.clear();

      // Which video lanes each source node appears on, and which clips put it
      // there. Disabled and unassigned clips are skipped for the same reason
      // CollectArrangeVideoLayers skips them: they never composite, so they
      // cannot collide with anything.
      std::unordered_map<uint64_t, std::set<uint64_t>> lanesPerSrc;
      std::unordered_map<uint64_t, std::vector<uint64_t>> clipsPerSrc;
      for (const Arrange::Lane& lane : gArrange.lanes)
      {
         if (lane.type != Arrange::kLaneVideo)
            continue;
         for (const Arrange::Clip& c : lane.clips)
         {
            if (!c.enabled || c.srcUid == 0)
               continue;
            lanesPerSrc[c.srcUid].insert(lane.id);
            clipsPerSrc[c.srcUid].push_back(c.id);
         }
      }
      for (const auto& kv : lanesPerSrc)
      {
         if (kv.second.size() <= 1)
            continue; // several clips on ONE lane are fine - a lane never overlaps itself
         for (uint64_t clipId : clipsPerSrc[kv.first])
            gArrangeVideoSourceConflictClipIds.insert(clipId);
      }
      return gArrangeVideoSourceConflictClipIds;
   }


   CompensationDelay& ArrangeTerminalCompensation(uint64_t laneId, uint64_t srcUid, int srcOutput)
   {
      uint64_t key = laneId * 0x9E3779B97F4A7C15ull;
      key ^= srcUid + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
      key ^= (uint64_t)(uint32_t)srcOutput + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
      ArrangeTerminalComp& slot = gArrangeTerminalComp[key];
      if (slot.delay == nullptr)
         slot.delay = std::make_unique<CompensationDelay>();
      slot.usedThisRebuild = true;
      return *slot.delay;
   }


   // Called once, immediately after SetTopology, so CurrentGeneration() is
   // the generation that just started referencing the surviving entries.
   void ReapArrangeTerminalCompensation()
   {
      const uint64_t current = AudioEngine::Instance().CurrentGeneration();
      const uint64_t completed = AudioEngine::Instance().CompletedGeneration();
      const bool audioRaceable = AudioEngine::Instance().SampleRate() > 0.0 && AudioEngine::Instance().IsAlive();
      for (auto it = gArrangeTerminalComp.begin(); it != gArrangeTerminalComp.end();)
      {
         if (it->second.usedThisRebuild)
         {
            it->second.usedThisRebuild = false;
            it->second.lastUsedGeneration = current;
            ++it;
         }
         else if (!audioRaceable || completed > it->second.lastUsedGeneration)
         {
            it = gArrangeTerminalComp.erase(it);
         }
         else
         {
            ++it;
         }
      }
   }


   // Timeline routing: the arrangement's audio clips feed the device and
   // every canvas Audio Out is bypassed. The mode, plus an arrangement-driven
   // offline render, which is Timeline by definition.
   // Every modulation bound to `node`, as (paramIndex, display label). Shared by
   // the Clip Settings list and its self-test, so what the test proves is what
   // the panel actually shows.
   //
   // Reads Modulation::KnownParam, the sticky record, rather than this frame's
   // FrameParams: the inspector can draw on a frame where the source node did
   // not (scrolled off canvas, or a top-docked panel drawing before the canvas
   // registers anything). A param with no sticky record yet still gets listed,
   // under its index, rather than silently vanishing from the list.
   void ArrangeCollectClipModBindings(const GraphNode& node,
                                      std::vector<std::pair<int, std::string>>& out)
   {
      out.clear();
      const Modulation& mod = Modulation::Instance();
      for (const auto& kv : mod.Links())
      {
         if (kv.first.first != node.index || kv.second.nodeIndex < 0)
            continue;
         const int paramIndex = kv.first.second;
         const ParamRef* known = mod.KnownParam(node.index, paramIndex);
         const GraphNode* modNode = FindNodeByIndex(kv.second.nodeIndex);
         std::string label = known != nullptr ? StripParamLabel(known->name.c_str())
                                              : ("param " + std::to_string(paramIndex));
         if (modNode != nullptr)
            label += "  <-  " + NodeTitleWithInstance(*modNode);
         out.emplace_back(paramIndex, label);
      }
   }


   bool ArrangeTimelineRoutingActive()
   {
      return gAudioMode == AudioMode::Timeline ||
             (gOfflineRender.active && gOfflineRender.arrangeDriven && gOfflineRender.timelineAudio) ||
             (gArrangeWavRender.active && gArrangeWavRender.timelineAudio);
   }


   // A take starts every node from its prepared state. Without this, a note-only node the
   // no-device pump (AudioEngine::PumpNoteNodesWithoutDevice, wall-clock driven) already ran keeps the
   // generator state those blocks left, and RebuildAudioTopology skips it because it is "already
   // prepared" at this rate - so two renders of one patch started from different random draws.
   void ForceAudioRepare()
   {
      for (GraphNode& gn : gNodes)
      {
         AudioNode* an = nullptr;
         if (auto* s = dynamic_cast<IAudioSource*>(gn.node.get()))
            an = s->GetAudioNode();
         if (an == nullptr)
            if (auto* s = dynamic_cast<INoteSource*>(gn.node.get()))
               an = s->GetAudioNode();
         if (an == nullptr)
            an = gn.node->AudioNodeForNotePorts();
         if (an != nullptr)
            an->preparedForSampleRate = -1.0;
      }
   }


   void RebuildAudioTopology()
   {
      if (gDeferAudioRebuild)
         return;
      ScopedPerfTimer perfTimer("RebuildAudioTopology");
      std::vector<AudioTopologyEntry> order;
      std::set<INode*> visited;
      std::unordered_map<AudioNode*, int> bufferIndexOf;
      std::vector<AudioTerminal> terminals;
      int nextBufferIndex = 0;

      // Timeline routing: the arrangement's audio clips feed the device and
      // every canvas Audio Out is bypassed. Depends on the mode alone (plus
      // an arrangement-driven offline render, which is Timeline by
      // definition) - not on the panel being open and not on the transport
      // playing. Pausing silences the sum in RunTopology instead, which
      // leaves the topology, the PDC state and the node graph untouched.
      const bool timelineRouting = ArrangeTimelineRoutingActive();

      // Every enabled, assigned clip on every audio lane - the WHOLE
      // arrangement, not the ones under the playhead. Grouped into one
      // terminal per (laneId, srcUid, srcOutput) carrying all of that
      // combination's windows, so a node used by five clips on one lane is
      // scheduled once and its five onsets are sample-accurate.
      struct ScheduledTerminal
      {
         uint64_t laneId; uint64_t srcUid; int srcOutput;
         float laneGain;
         float lanePanL, lanePanR;
         std::vector<ClipWindow> windows;
      };
      std::vector<ScheduledTerminal> scheduled;
      std::vector<ClipWindow> clipWindows;
      if (timelineRouting)
      {
         // Straight off the model (WP5b): ticks convert to beats with no
         // tempo, so a tempo change does not touch the schedule at all. BPM
         // sync (step 3, ClipWindow::sampleBpm) keeps that property: only
         // the clip's OWN sampleBpm is copied here, never combined with the
         // current project tempo - RunTopology divides by the live tempo
         // itself every block, so this schedule never goes stale on a tempo
         // edit alone.
         std::unordered_map<std::string, size_t> indexOfKey;
         bool anySolo = false;
         for (const Arrange::Lane& lane : gArrange.lanes)
            anySolo = anySolo || (lane.type == Arrange::kLaneAudio && lane.solo);
         for (const Arrange::Lane& lane : gArrange.lanes)
         {
            if (lane.type != Arrange::kLaneAudio)
               continue;
            if (!gArrangeRenderActiveLaneScope.empty() && !gArrangeRenderActiveLaneScope.count(lane.id))
               continue;
            if (!Arrange::LaneEffectivelyEnabled(gArrange, lane))
               continue;
            // Mixer's rule: muted, or someone else is soloed. Gain 0 rather
            // than skipping the lane, so its clips keep their live waveform.
            const bool silenced = lane.mute || (anySolo && !lane.solo);
            const float laneLinear = silenced ? 0.0f : std::pow(10.0f, lane.gainDb / 20.0f);
            float panL = 1.0f, panR = 1.0f;
            DspMath::EqualPowerPan(std::clamp(lane.pan, -1.0f, 1.0f), panL, panR);
            panL *= (float)M_SQRT2;
            panR *= (float)M_SQRT2;
            for (const Arrange::Clip& c : lane.clips)
            {
               // An offline clip (srcUid 0: node deleted, or never assigned)
               // and a disabled clip are both silent. `enabled` was not read
               // at all before WP3, so disabling an audio clip changed nothing
               // you could hear. A uid that no longer resolves is dropped at
               // terminal creation below.
               if (c.srcUid == 0 || !c.enabled || c.length <= 0)
                  continue;
               char keyBuf[80];
               snprintf(keyBuf, sizeof(keyBuf), "%llu/%llu/%d",
                        (unsigned long long)lane.id, (unsigned long long)c.srcUid, c.srcOutput);
               const std::string key(keyBuf);
               auto it = indexOfKey.find(key);
               if (it == indexOfKey.end())
               {
                  it = indexOfKey.emplace(key, scheduled.size()).first;
                  scheduled.push_back({ lane.id, c.srcUid, c.srcOutput, laneLinear, panL, panR, {} });
               }
               ClipWindow w;
               w.clipId = c.id; // labels the live waveform buckets (WP8)
               w.shape = ArrangeClipShape(c.srcUid, c.srcOutput, c.start, c.length);
               w.startBeat = Arrange::TicksToBeats(c.start);
               w.endBeat = Arrange::TicksToBeats(c.End());
               w.fadeInBeats = Arrange::TicksToBeats(c.fadeIn);
               w.fadeOutBeats = Arrange::TicksToBeats(c.fadeOut);
               w.gain = std::pow(10.0f, c.gainDb / 20.0f);
               w.pitch = c.pitch;
               // Mirrors the model so RunTopology's exact-seek lookahead can
               // gate on it without reaching into gArrange from the audio
               // thread - see ClipWindow::sampleDropped's own comment.
               w.sampleDropped = c.sampleDropped;
               // Audio Sample timing inputs - see ClipWindow::sampleBpm.
               w.sampleBpm = c.sampleDropped ? c.sampleBpm : 0.0f;
               w.origBpm = c.sampleDropped ? c.origBpm : 0.0f;
               w.syncToTempo = c.sampleDropped && c.syncToTempo;
               // Straight copy - see ClipWindow::sourceOffsetSeconds's own
               // comment. Passed through for EVERY audio clip, not just a
               // Sample: the model already moves this field on any clip's trim
               // or split (ArrangeModel.cpp's TrimEdge/Split call
               // SampleSourceBpm with the clip's own sync state, Sample or
               // not), and RunTopology's position lock now applies to every
               // clip, so zeroing it here would have made a trimmed non-Sample
               // clip replay from the file's start instead of from its trim
               // point. sampleBpm/syncToTempo above stay Sample-only, which is
               // what makes the mapping fall back to native speed.
               w.sourceOffsetSeconds = c.sourceOffsetSeconds;
               // Retrigger is an Audio Sample-only feature (Clip Settings
               // hides the control for every other category) - a stray
               // `retrigger=true` left over from a patch saved before this
               // restriction existed must not resurrect the behavior.
               w.retrigger = c.retrigger && c.sampleDropped;
               // Combined track pan + clip pan with equal-power scaling.
               // Combining them here prevents the track balance from muting opposite-panned clips.
               const float combinedPan = std::clamp(c.pan + lane.pan, -1.0f, 1.0f);
               DspMath::EqualPowerPan(combinedPan, w.panL, w.panR);
               w.panL *= (float)M_SQRT2;
               w.panR *= (float)M_SQRT2;
               scheduled[it->second].windows.push_back(w);
            }
         }
         // A clip needs its node to itself. The node has ONE playback
         // position and RunTopology's position lock states, per block, which
         // source second belongs at this instant - so a node scheduled on two
         // lanes gets two such statements in the same block and the last
         // terminal to run wins, leaving the other lane's clip audibly playing
         // its neighbour's position. Retrigger has the same single-position
         // problem and is additionally suppressed here (falling back to
         // timeline-continuous for that window) since it is a flag we own;
         // the position lock is not suppressible - there is no correct
         // position to state for two clips at once - so the set below records
         // EVERY clip on a multi-lane source, not just the retriggering ones,
         // and the inspector explains it. Duplicating the source node is the
         // only real fix, which is what the warning says.
         gArrangeRetriggerConflictClipIds.clear();
         std::unordered_map<uint64_t, std::set<uint64_t>> lanesPerSrc;
         for (const ScheduledTerminal& st : scheduled)
            lanesPerSrc[st.srcUid].insert(st.laneId);
         for (ScheduledTerminal& st : scheduled)
         {
            if (lanesPerSrc[st.srcUid].size() <= 1)
               continue;
            for (ClipWindow& w : st.windows)
            {
               w.retrigger = false;
               gArrangeRetriggerConflictClipIds.insert(w.clipId);
            }
         }

         // Sort each terminal's windows and mark the abutting edges. The
         // model forbids overlap on a lane and keeps it sorted, so this is a
         // no-op on a valid model - kept because RunTopology's cursor walk
         // stalls on an unsorted array, and one malformed load should not be
         // able to silence a lane.
         for (ScheduledTerminal& st : scheduled)
         {
            std::sort(st.windows.begin(), st.windows.end(),
                      [](const ClipWindow& x, const ClipWindow& y) { return x.startBeat < y.startBeat; });
            // Clamping startBeat alone would invert a window fully contained
            // in its predecessor (start pushed past its own end), leaving the
            // array unsorted - and RunTopology's cursor walk assumes sorted,
            // so it would stall there and silence every later window on the
            // lane. Clamp the end up too, then drop what is left empty.
            for (size_t i = 1; i < st.windows.size(); i++)
            {
               st.windows[i].startBeat = std::max(st.windows[i].startBeat, st.windows[i - 1].endBeat);
               st.windows[i].endBeat = std::max(st.windows[i].endBeat, st.windows[i].startBeat);
            }
            st.windows.erase(std::remove_if(st.windows.begin(), st.windows.end(),
                                            [](const ClipWindow& w) { return !(w.endBeat > w.startBeat); }),
                             st.windows.end());
            // An edge shared with the neighbouring window to within half a
            // tick is one continuous run of the same node, so no declick.
            const double kAbutEpsilonBeats = 0.5 / (double)Arrange::kPPQ;
            for (size_t i = 0; i + 1 < st.windows.size(); i++)
            {
               if (std::abs(st.windows[i].endBeat - st.windows[i + 1].startBeat) <= kAbutEpsilonBeats)
               {
                  st.windows[i].abutsNext = true;
                  st.windows[i + 1].abutsPrev = true;
               }
            }
         }
      }

      // Spatial Mixer is a terminal like Audio Out, except it also owns the
      // AudioNode that renders its inputs: walk it into `order`, then its own
      // rendered buffer is the terminal. Its binaural render must never be a
      // source for anything else (it has no output pin).
      for (GraphNode& gn : gNodes)
      {
         auto* spatial = dynamic_cast<SpatialMixerNode*>(gn.node.get());
         if (spatial == nullptr || timelineRouting || spatial->ConnectedCount() == 0)
            continue;
         CollectAudioChain(spatial, visited, order, bufferIndexOf, nextBufferIndex);
         const int idx = AudioBufferIndexOf(spatial, 0, bufferIndexOf);
         if (idx < 0)
            continue;
         terminals.push_back({ idx, &spatial->PdcAnchor() });
         terminals.back().live = spatial->live;
      }

      for (GraphNode& gn : gNodes)
      {
         auto* audioOut = dynamic_cast<AudioOutputNode*>(gn.node.get());
         auto* outNode = dynamic_cast<OutputNode*>(gn.node.get());
         if (audioOut == nullptr && outNode == nullptr)
            continue;

         AudioCaptureRing* ring = audioOut ? &audioOut->CaptureRing() : &outNode->CaptureRing();
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = gn.node->AudioInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            bool hopped = false;
            INode* resolved = ResolvedAudioSource(cable->GetSource(), &hopped);
            if (resolved != nullptr)
            {
               CollectAudioChain(resolved, visited, order, bufferIndexOf, nextBufferIndex);
               const int outputSlot = hopped ? 0 : cable->GetOutputSlot();
               const int idx = AudioBufferIndexOf(resolved, outputSlot, bufferIndexOf);
               if (idx >= 0)
               {
                  // Timeline routing replaces canvas routing outright (see the
                  // `scheduled` loop below), rather than gating
                  // this terminal per-cable. Gating by reachability let a
                  // shared mixer downstream of the active clip leak whatever
                  // else it was also summing in - it answered "is the active
                  // clip somewhere upstream of this terminal", not "is this
                  // terminal's whole signal just the active clip's own
                  // audio", which is what "Strict" is supposed to mean.
                  // resolved's own node is still walked into `order` above,
                  // so if it happens to BE (or feed) an active clip's node,
                  // that clip's own terminal below still finds it processed.
                  if (!timelineRouting)
                  {
                     // Capture is set unconditionally, gated at write-time on
                     // the ring's own `enabled` flag - see AudioTerminal's comment.
                     terminals.push_back({ idx, ring });
                     terminals.back().live = audioOut != nullptr && audioOut->live;
                  }
               }
            }
         }
      }

      // Route each scheduled (lane, node, output) straight to the device, one
      // terminal carrying all of its windows, bypassing every canvas Audio
      // Out and any mixer downstream of it. A clip's node does not need to be
      // wired to an Audio Out on canvas at all - the timeline is its own
      // routing. In an offline arrangement render these terminals are summed
      // into the master offline buffer in RunTopology and written in one pass
      // into OutputNode's CaptureRing by pumpOfflineAudio.
      for (ScheduledTerminal& st : scheduled)
      {
         // By uid only. Node indices restart at 1 on NewPatch and are reused;
         // a uid never is, so an unresolvable uid is silence, never a
         // fallback to whatever node now holds a recycled index.
         GraphNode* activeGn = FindNodeByUid(st.srcUid);
         if (activeGn == nullptr || activeGn->node == nullptr)
            continue;
         INode* resolved = ResolvedAudioSource(activeGn->node.get());
         if (resolved == nullptr)
            continue;
         CollectAudioChain(resolved, visited, order, bufferIndexOf, nextBufferIndex);
         const int idx = AudioBufferIndexOf(resolved, st.srcOutput, bufferIndexOf);
         if (idx < 0)
            continue;
         AudioTerminal term;
         term.bufferIndex = idx;
         term.gain = 1.0f; // clip gain rides on the window, lane gain on laneGain
         term.sourceNode = AudioNodeOfAny(resolved);
         term.laneGain = st.laneGain;
         term.lanePanL = st.lanePanL;
         term.lanePanR = st.lanePanR;
         term.windowOffset = (int)clipWindows.size();
         term.numWindows = (int)st.windows.size();
         term.externalCompensation = &ArrangeTerminalCompensation(st.laneId, st.srcUid, st.srcOutput);
         clipWindows.insert(clipWindows.end(), st.windows.begin(), st.windows.end());
         terminals.push_back(term);
      }

      // Note nodes and note-only chains that never reach an Audio Out at all —
      // an Envelope driving a visual param through Modulation::Bind, or standalone
      // note generators/sequencers/processors (Note Sequencer, MIDI Notes, Random Note,
      // Chorder, Arpeggiator, etc.) that have no audio cable or Audio Out anywhere in
      // their chain. Seed separately from every node that produces or processes notes,
      // or has a connected note input; visited dedupes anything the Audio Out walk
      // already picked up.
      for (GraphNode& gn : gNodes)
      {
         bool isNoteNode = (dynamic_cast<INoteSource*>(gn.node.get()) != nullptr ||
                            gn.node->AudioNodeForNotePorts() != nullptr ||
                            gn.category == "Notes");
         if (!isNoteNode)
         {
            for (int slot = 0; slot < kMaxNoteSlots; slot++)
            {
               NoteCable* cable = gn.node->NoteInputSlot(slot);
               if (cable != nullptr && cable->IsConnected())
               {
                  isNoteNode = true;
                  break;
               }
            }
         }
         if (isNoteNode)
         {
            CollectAudioChain(gn.node.get(), visited, order, bufferIndexOf, nextBufferIndex);
         }
      }

      // A node that must run even though nothing downstream pulls it and it
      // has no note input - e.g. a Sampler currently recording its audio
      // input with nothing wired past it. `visited` dedupes against the two
      // seeds above.
      for (GraphNode& gn : gNodes)
      {
         if (gn.node->RequiresAudioProcessing())
            CollectAudioChain(gn.node.get(), visited, order, bufferIndexOf, nextBufferIndex);
      }

      // Describe each note-consuming node's inbox wiring to its producer's
      // outbox, now that every relevant node has an `order` entry. This used
      // to mutate the live AudioNode/NoteEventQueue objects directly here on
      // the main thread (ResetConsumers/RegisterConsumer/SetNoteInbox) - but
      // the audio thread can still be mid-ProcessList over those same
      // objects for the *previous* generation when RebuildAudioTopology
      // runs, which raced cursor resets and inbox pointer writes against
      // concurrent Pop() (stuck notes - a lost note-off - and torn
      // inbox/cursor reads). Now this only builds a description
      // (topology.noteOutboxes/noteWires below); AudioEngine::
      // ApplyNoteWiringIfNew applies it, once per generation, on whichever
      // thread actually owns that generation's ProcessList (the audio
      // thread during RunTopology, or the main thread via
      // ApplyNoteWiringIfNoDevice/PumpNoteNodesWithoutDevice when no device
      // is open) - the only thread that can safely touch those objects.
      // NoteEventQueue needs no PrepareToPlay (a fixed-size member,
      // allocated with the AudioNode itself), so this can run before or
      // after the PrepareToPlay loop below without ordering consequences.
      //
      // A real loop over every unified slot, not just 0: every note-consuming
      // node before AudioPluginNode put its one note pin at slot 0, but
      // AudioPluginNode's lives at slot 1 (audio stays at slot 0 there so
      // existing patches keep loading unchanged) - a node exposes at most one
      // note pin, so whichever slot answers non-null is it.
      // A producer's outbox can fan out to more than one consumer (two
      // synths off one Note Sequencer, an FM patch, etc) - each consumer
      // needs its own read cursor so draining events in one doesn't starve
      // another (see NoteEventQueue's multi-consumer contract). Cursor ids
      // are handed out 0..n-1 per outbox in the same walk order as the old
      // Reset+Register pass, capped at NoteEventQueue::kMaxConsumers -
      // exactly RegisterConsumer's own cap, so an outbox that would have
      // overflowed still overflows the same way (cursor -1, inbox pointer
      // still recorded - matching what SetNoteInbox used to be called with).
      std::vector<NoteOutboxPlan> noteOutboxes;
      std::vector<NoteWire> noteWires;
      {
         std::unordered_map<NoteEventQueue*, int> consumerCounts;
         for (GraphNode& gn : gNodes)
         {
            AudioNode* consumer = AudioNodeOfAny(gn.node.get());
            if (consumer == nullptr)
               continue;
            // Every slot the node actually exposes gets a wire this
            // generation - including unconnected ones, described as
            // nullptr/-1 - so a slot that was wired last generation and got
            // disconnected doesn't leave a stale producer pointer applied on
            // the audio node.
            for (int slot = 0; slot < kMaxNoteSlots; slot++)
            {
               NoteCable* cable = gn.node->NoteInputSlot(slot);
               if (cable == nullptr)
                  continue;
               NoteEventQueue* inbox = nullptr;
               int cursor = -1;
               if (cable->IsConnected())
               {
                  INode* resolved = ResolvedAudioSource(cable->GetSource());
                  if (AudioNode* producer = AudioNodeOfAny(resolved))
                  {
                     inbox = producer->NoteOutbox(cable->GetOutputSlot());
                     if (inbox != nullptr)
                     {
                        int& count = consumerCounts[inbox];
                        if (count < NoteEventQueue::kMaxConsumers)
                           cursor = count++;
                        // else: cursor stays -1 (overflow), matching
                        // RegisterConsumer's own -1 return; inbox stays set
                        // on the wire (matching the old SetNoteInbox call)
                        // but is never adopted since cursor < 0.
                     }
                  }
               }
               noteWires.push_back(NoteWire{ consumer, slot, inbox, cursor });
            }
         }
         noteOutboxes.reserve(consumerCounts.size());
         for (const auto& [queue, count] : consumerCounts)
            noteOutboxes.push_back(NoteOutboxPlan{ queue, count });
      }

      const double sampleRate = AudioEngine::Instance().SampleRate() > 0.0
         ? AudioEngine::Instance().SampleRate()
         : ((gOfflineRender.active && gOfflineRender.node != nullptr) ? gOfflineRender.node->OfflineAudioSampleRate()
                                                                      : gHeadlessAudioRate);
      if (sampleRate > 0.0)
      {
         for (AudioTopologyEntry& entry : order)
         {
            // Skip re-preparing a node that is already live at this sample
            // rate. RebuildAudioTopology runs far more often than "this node
            // just started running" - every cable connect/disconnect, every
            // Timeline Strict active-clip change - and nearly every DSP
            // kernel's PrepareToPlay unconditionally calls Reset(), zeroing
            // filter/delay/reverb/compressor state. Calling it unconditionally
            // here zeroed that state out from under audio that was actively
            // flowing through an already-running node on every rebuild,
            // producing an audible click/pop whenever the node was reachable
            // to a connected Audio Out (silent otherwise, since nothing was
            // listening to the reset buffer). See AudioNode::preparedForSampleRate.
            if (entry.node->preparedForSampleRate != sampleRate)
            {
               entry.node->PrepareToPlay(sampleRate, kAudioMaxBlockFrames);
               entry.node->preparedForSampleRate = sampleRate;
            }
         }
      }

      // Plugin/effect delay compensation (PDC). There is no compensation
      // machinery anywhere else in the topology to plug a latency number
      // into - this is the one place that both knows every branch's
      // cumulative latency and runs on the main thread, so it's also the
      // one place that decides and allocates each branch's compensating
      // delay (CompensationDelay::Prepare - see its own comment on why that
      // must happen here, not in RunTopology).
      //
      // `order` is topologically sorted (CollectAudioChain only appends a
      // node after every source it reads from) and each entry's
      // outputBufferIndices are assigned - so a single forward pass can compute
      // every entry's cumulative latency by looking up its already-computed
      // inputs' cumulative latency by buffer index, with no separate
      // topological sort needed.
      //
      // Cumulative latency = this node's own reported latency plus the
      // latest-arriving (max) of its connected inputs' cumulative latency -
      // a node's output is only as "early" as its slowest input, same logic
      // any DAW's PDC uses. Note-only inputs carry no cumulative latency of
      // their own (MIDI events aren't delayed by this pass at all) and
      // aren't represented in inputBufferIndices, so they don't factor in.
      std::vector<int> cumulativeLatencyByBuffer(nextBufferIndex, 0);
      for (size_t k = 0; k < order.size(); k++)
      {
         AudioTopologyEntry& entry = order[k];
         int maxUpstream = 0;
         for (int i = 0; i < entry.numInputs; i++)
         {
            const int idx = entry.inputBufferIndices[i];
            if (idx >= 0 && idx < (int)cumulativeLatencyByBuffer.size())
               maxUpstream = std::max(maxUpstream, cumulativeLatencyByBuffer[(size_t)idx]);
         }
         const int entryLatency = entry.node->LatencySamples() + maxUpstream;
         for (int o = 0; o < entry.numOutputs; o++)
         {
            const int outIdx = entry.outputBufferIndices[o];
            if (outIdx >= 0 && outIdx < (int)cumulativeLatencyByBuffer.size())
               cumulativeLatencyByBuffer[(size_t)outIdx] = entryLatency;
         }
      }

      // At every merge point - a node with more than one connected input
      // pin, or the terminal summation below - each branch but the
      // slowest-arriving one needs a compensating delay equal to the gap, so
      // every pin/terminal lands sample-aligned instead of comb-filtering
      // against its siblings. A branch already at (or past) the max, or a
      // node with only one connected pin, gets an inactive (unallocated)
      // CompensationDelay. Fixed capacity (kAudioMaxChannels), matching
      // PooledBuffer::Allocate's own discipline, so a topology generation is
      // never under-allocated if the device's actual channel count changes
      // mid-generation.
      //
      // Prepare() is called unconditionally for every pin, connected or not
      // (not gated on `delay > 0` the way this used to read) for two
      // reasons: it lives on the persistent AudioNode now (see
      // AudioNode::inputCompensation), not a value freshly zero-constructed
      // in `entry` every rebuild, so a pin whose delay requirement drops
      // back to 0 (cable disconnected, a sibling branch got shorter) must
      // still be told to deactivate - otherwise it would keep delaying this
      // generation's audio by an amount computed for a topology that no
      // longer exists. And Prepare() is itself idempotent (see its own
      // comment): calling it again with the same delay this pin already had
      // is a no-op that preserves the in-flight ring contents, so an
      // unrelated rebuild elsewhere in the graph no longer clicks a merge
      // point that didn't actually change.
      for (AudioTopologyEntry& entry : order)
      {
         int maxAmongConnected = 0;
         for (int i = 0; i < entry.numInputs; i++)
         {
            const int idx = entry.inputBufferIndices[i];
            if (idx >= 0 && idx < (int)cumulativeLatencyByBuffer.size())
               maxAmongConnected = std::max(maxAmongConnected, cumulativeLatencyByBuffer[(size_t)idx]);
         }
         for (int i = 0; i < entry.numInputs; i++)
         {
            const int idx = entry.inputBufferIndices[i];
            int delay = 0;
            if (idx >= 0 && idx < (int)cumulativeLatencyByBuffer.size())
               delay = maxAmongConnected - cumulativeLatencyByBuffer[(size_t)idx];
            entry.node->inputCompensation[i].Prepare(std::max(0, delay), kAudioMaxChannels);
         }
      }

      // Same alignment one level up, across whichever Audio Out terminals
      // are summed together into the device buffer in RunTopology. Prefers
      // the owning AudioCaptureRing's persistent compensation (survives
      // across rebuilds, same reasoning as the input-pin loop above) for any
      // terminal that has one - every ordinary canvas Audio Out terminal
      // does. A Timeline Strict clip terminal has no ring, so it falls back
      // to its own value (rebuilt fresh, and correctly so - it only exists
      // for the lifetime of one active-clip generation to begin with).
      {
         int maxAmongTerminals = 0;
         for (const AudioTerminal& terminal : terminals)
            if (!terminal.live && terminal.bufferIndex >= 0 && terminal.bufferIndex < (int)cumulativeLatencyByBuffer.size())
               maxAmongTerminals = std::max(maxAmongTerminals, cumulativeLatencyByBuffer[(size_t)terminal.bufferIndex]);
         for (AudioTerminal& terminal : terminals)
         {
            int delay = 0;
            if (!terminal.live && terminal.bufferIndex >= 0 && terminal.bufferIndex < (int)cumulativeLatencyByBuffer.size())
               delay = maxAmongTerminals - cumulativeLatencyByBuffer[(size_t)terminal.bufferIndex];
            CompensationDelay& terminalComp = terminal.capture != nullptr
                                                  ? terminal.capture->compensation
                                                  : (terminal.externalCompensation != nullptr
                                                        ? *terminal.externalCompensation
                                                        : terminal.compensation);
            terminalComp.Prepare(std::max(0, delay), kAudioMaxChannels);
         }
      }

      // One source reaching the device through two canvas terminals (Audio Out
      // + Output) is mixed once, not twice (R477).
      MarkDuplicateDeviceTerminals(terminals);

      AudioTopology topology;
      topology.order = std::move(order);
      topology.terminalBufferIndices = std::move(terminals);
      topology.clipWindows = std::move(clipWindows);
      topology.numBuffers = nextBufferIndex;
      topology.noteOutboxes = std::move(noteOutboxes);
      topology.noteWires = std::move(noteWires);
      AudioEngine::Instance().SetTopology(std::move(topology));
      // No device running means no audio callback thread will ever apply
      // this generation's note wiring via RunTopology - and a number of
      // self-test fixtures call RebuildAudioTopology() synchronously with no
      // device open and then immediately drive nodes via direct
      // ProcessBlock() calls, expecting the wiring to already be live. Apply
      // it here, synchronously, in that case; it's safe specifically because
      // no device is open, so nothing else can be concurrently touching
      // these objects.
      AudioEngine::Instance().ApplyNoteWiringIfNoDevice();
      // The topology just published describes this revision and this
      // routing, so ArrangeAudioRebuildIfStale stays quiet until one moves -
      // whichever of the ~40 graph-edit call sites got here first.
      gArrangeAudioBuiltRevision = gArrange.revision;
      gArrangeAudioBuiltRouting = timelineRouting;
      gAudioTopologyRebuildCount++;
      ReapArrangeTerminalCompensation();
   }


   // The main loop's once-a-frame audio trigger (WP5b). gArrange.revision is
   // the one change signal for the arrangement: every model op and every
   // direct field edit bumps it, so there is no per-site dirty flag to forget
   // and nothing to hash. The only other input the schedule has is the
   // routing (mode, or an arrangement-driven offline render), OR'd in here.
   // Graph changes are not polled: every one of them already calls
   // RebuildAudioTopology directly, which records both values above.
   // Tempo is deliberately absent - see gArrangeAudioBuiltRevision.
   // Offline render owns its own rebuilds, so the trigger stands down while
   // one runs. Returns whether it rebuilt.
   bool ArrangeAudioRebuildIfStale()
   {
      if (gOfflineRender.active)
         return false;
      if (gArrange.revision == gArrangeAudioBuiltRevision &&
          ArrangeTimelineRoutingActive() == gArrangeAudioBuiltRouting)
         return false;
      const unsigned long long before = gAudioTopologyRebuildCount;
      RebuildAudioTopology();
      return gAudioTopologyRebuildCount != before;
   }


   // Single choke point for turning the audio engine on. Every call site that
   // starts the device MUST go through this, not AudioEngine::Instance().
   // Start() directly - see docs/plans/optimization/prompts/
   // 01-audio-lifecycle-correctness.md bugs 1/2.
   //
   // Why here and not inside AudioEngine::Start() itself: Start() only knows
   // about the device and its own atomics: it has no visibility into gNodes,
   // AudioCable, or any of the editor-graph machinery RebuildAudioTopology
   // needs to walk to find every node and call PrepareToPlay on it - that's
   // all main.cpp state, deliberately kept out of the audio/ library (see
   // AudioEngine.h's class comment; it owns the device and the topology
   // publish, not the graph the topology is built from).
   //
   // Why a rebuild and not just re-publishing the existing topology: Start()
   // negotiates the device's *actual* sample rate, which is not necessarily
   // what was requested (see Platform::AudioDeviceOpen's doc comment) and can
   // differ from whatever rate the topology's nodes were last prepared with -
   // possibly zero, if this is the very first Start() of the process and every
   // node so far was built/rebuilt with AudioEngine::SampleRate() still 0
   // (RebuildAudioTopology's `sampleRate > 0.0` guard above skips
   // PrepareToPlay entirely in that case, on purpose - see its comment). A
   // fresh RebuildAudioTopology() call, made after Start() returns
   // successfully, re-reads AudioEngine::Instance().SampleRate() (now the
   // real negotiated rate) and calls PrepareToPlay(sampleRate, ...) on every
   // node - the same call RebuildAudioTopology already makes on every cable
   // edit, patch load and node removal. This is also the one place the
   // negotiated rate actually reaches each node's ParamMailbox/smoothers,
   // not just AudioEngine::mSampleRate itself (which Start() already writes) -
   // closing both bug 1 (nodes never prepared at engine-start) and bug 2
   // (stale mSampleRate readers) with the same call.
   //
   // This does not reopen AudioEngine::SetTopology's one-generation-retire
   // discipline (AudioEngine.h:66-74): RebuildAudioTopology always ends in
   // exactly one SetTopology call, same as every other caller.
   bool StartAudioEngine(std::string& outError)
   {
      // Park the audio thread on an empty topology BEFORE the device opens.
      // Start() makes the callback live immediately, and the rebuild below
      // calls PrepareToPlay (plain-field writes: sample rate, smoothers,
      // voices) on the very nodes the old topology still references - TSan
      // flagged that as a data race on every start / rate change. With the
      // empty list published first, nothing the callback can reach is being
      // re-prepared; the rebuild's SetTopology then publishes the real one.
      AudioEngine::Instance().SetTopology(AudioTopology{});
      if (!AudioEngine::Instance().Start(outError))
         return false;
      RebuildAudioTopology();
      return true;
   }


   // Single choke point for device-change/sleep-wake self-healing - called
   // once a frame from the main loop, main thread only. See
   // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md.
   //
   // Platform::AudioDeviceConfigDidChange/AudioWillSleep/AudioDidWake are
   // consuming reads of flags latched by notification handlers that can run
   // on arbitrary threads (Platform.h's doc comment); this is where that
   // turns into actual work, through StartAudioEngine - the same choke
   // point Apply-audio-settings already uses (main.cpp's menu code), so
   // there is exactly one restart implementation, not two.
   void PollAudioRecovery()
   {
      const bool willSleep = Platform::AudioWillSleep();
      const bool didWake = Platform::AudioDidWake();
      const bool configChanged = Platform::AudioDeviceConfigDidChange();
      const bool wasRunning = AudioEngine::Instance().SampleRate() > 0.0;

      if (willSleep)
      {
         // Stop deliberately before the OS tears the device out from under a
         // live AVAudioEngine, rather than letting whatever CoreAudio does
         // during suspend surface as an ordinary config-change/dead-engine
         // case. Symmetric with the toolbar's "Stop Audio" - nothing
         // sleep-specific for this to get wrong. Wake, below, restarts it if
         // it was running; this is not itself a failure, so it does not
         // touch the backoff window state.
         if (wasRunning)
            AudioEngine::Instance().Stop();
         return; // let didWake/configChanged (if also set this frame) wait one frame
      }

      // IsAlive() only means something if the engine believes it should be
      // running - see its own comment. A dead engine while nothing was
      // supposed to be on isn't a recovery case, and neither is a wake with
      // nothing previously running: per rule 4, this never silently starts
      // audio the user had turned off.
      const bool dead = wasRunning && !AudioEngine::Instance().IsAlive();
      if (!wasRunning || (!didWake && !configChanged && !dead))
         return;

      const double nowMs = glfwGetTime() * 1000.0;

      // Idempotent: collapse everything that fired this frame (and anything
      // still landing from the last kAudioRecoveryMinIntervalMs) into at
      // most one attempt, so a device flapping several times a second or a
      // burst of config-change notifications on one wake can't spawn
      // overlapping restarts.
      if (gLastAudioRecoveryAttemptMs >= 0.0 && nowMs - gLastAudioRecoveryAttemptMs < kAudioRecoveryMinIntervalMs)
         return;

      if (gAudioRecoveryWindowStartMs < 0.0 || nowMs - gAudioRecoveryWindowStartMs > kAudioRecoveryWindowMs)
      {
         gAudioRecoveryWindowStartMs = nowMs;
         gAudioRecoveryAttemptsInWindow = 0;
      }

      if (gAudioRecoveryAttemptsInWindow >= kAudioRecoveryMaxAttemptsPerWindow)
      {
         // Rate-limited give-up: a device that fails this many times in one
         // window is treated as really gone rather than retried forever.
         // Stop rather than leave it in limbo - SampleRate() dropping to 0
         // is what the toolbar/status bar (main.cpp's menu bar block) reads
         // to switch to "Start Audio" and surface gAudioStartError on
         // hover, so this alone is the "honest UI" requirement: there is no
         // separate flag for the UI to also check.
         if (wasRunning)
            AudioEngine::Instance().Stop();
         gAudioStartError = "audio device recovery gave up after " +
            std::to_string(kAudioRecoveryMaxAttemptsPerWindow) + " attempts";
         return;
      }

      gLastAudioRecoveryAttemptMs = nowMs;
      gAudioRecoveryAttemptsInWindow++;

      // Stop unconditionally before restarting, even if IsAlive() already
      // reads false / the device object is in some unknown state:
      // Platform::AudioDeviceClose() is a no-op once already closed, and
      // this guarantees StartAudioEngine always builds a fresh AVAudioEngine
      // rather than reusing one in a state nothing here can characterize.
      AudioEngine::Instance().Stop();

      gAudioStartError.clear();
      if (!StartAudioEngine(gAudioStartError))
         fprintf(stderr, "audio device recovery: %s\n", gAudioStartError.c_str());
   }


   // Whole-graph scan for a node whose output comes from outside the process
   // entirely (see INode::IsHardwareDriven's doc comment) - Offline Render
   // must refuse up front rather than silently rendering black/frozen frames
   // where a live camera, MIDI controller, or Syphon/Spout receiver would
   // have been, since none of those can be pre-synthesized for a take that
   // isn't running in real time.
   INode* FindHardwareDrivenNode()
   {
      for (GraphNode& gn : gNodes)
      {
         if (gn.node && gn.node->IsHardwareDriven())
            return gn.node.get();
      }
      return nullptr;
   }


   // The same refusal, scoped to an arrangement render (WP7 #4). A timeline
   // take only pre-synthesizes the nodes its own clips reference, so the
   // whole-patch sweep above would refuse a perfectly renderable timeline
   // just because an unrelated camera sits on the canvas. Only clips that
   // actually play are considered: disabled ones are excluded everywhere
   // else in the render path (WP5's `0` key), and an unassigned clip
   // (srcUid = 0, or a uid whose node is gone) references nothing at all.
   //
   // `wantVideo`/`wantAudio` say which lane types this job draws from, so a
   // video-only job isn't refused by a camera an audio clip happens to point
   // at. A canvas-sourced side is NOT covered here - that side renders the
   // live graph and is checked with the whole-patch sweep by the caller.
   INode* FindHardwareDrivenNodeInArrangeRange(int64_t startTick, int64_t endTick,
                                               bool wantVideo, bool wantAudio)
   {
      for (const Arrange::Lane& l : gArrange.lanes)
      {
         const bool isVideo = l.type == Arrange::kLaneVideo;
         if (isVideo ? !wantVideo : !wantAudio)
            continue;
         // Honour the same scope the scheduler does, or a scoped take gets
         // refused over a camera it was never going to cook: a "Render Track"
         // over a lane it isn't rendering.
         if (!gArrangeRenderActiveLaneScope.empty() && !gArrangeRenderActiveLaneScope.count(l.id))
            continue;
         for (const Arrange::Clip& c : l.clips)
         {
            if (!c.enabled || c.srcUid == 0)
               continue;
            if (c.End() <= startTick || c.start >= endTick) // half-open, same as the scheduler
               continue;
            GraphNode* gn = FindNodeByUid(c.srcUid);
            if (gn != nullptr && gn->node != nullptr && gn->node->IsHardwareDriven())
               return gn->node.get();
         }
      }
      return nullptr;
   }
}
