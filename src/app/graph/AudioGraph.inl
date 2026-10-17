// Part of src/main.cpp, included there in order (one translation unit).
// audio topology, engine start, scene settings, device recovery

   void DisconnectAllTo(INode* dying)
   {
      // a deleted modulator must also be cleared from any Math node feeding on it
      auto* dyingMod = dynamic_cast<IModulator*>(dying);
      auto* dyingFile = dynamic_cast<AudioFileNode*>(dying);
      auto* dyingGeometry = dynamic_cast<IGeometrySource*>(dying);
      auto* dyingXY = dynamic_cast<MacroXYNode*>(dying);
      IModulator* dyingY = dyingXY ? dyingXY->YOutput() : nullptr;
      for (GraphNode& other : gNodes)
      {
         if (auto* render = dynamic_cast<Render3DNode*>(other.node.get()))
         {
            for (int slot = 0; slot < Render3DNode::kSlots; slot++)
            {
               if (dyingGeometry != nullptr && render->geometry[slot] == dyingGeometry)
                  render->geometry[slot] = nullptr;
            }
            if ((const void*)render->camera == (const void*)dying)
               render->camera = nullptr;
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               if ((const void*)render->lights[i] == (const void*)dying)
                  render->lights[i] = nullptr;
         }
         // Every other geometry-consuming node, whatever it calls its field(s)
         // internally - GeometryInputSlot(slot) finds it generically, so a new
         // node type can't forget to register here and leave a pointer to a
         // freed node that crashes on the next cook.
         if (dyingGeometry != nullptr)
         {
            for (int slot = 0; slot < kMaxGeometrySlots; slot++)
            {
               if (IGeometrySource** field = other.node->GeometryInputSlot(slot))
                  if (*field == dyingGeometry)
                     *field = nullptr;
            }
         }
         if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(other.node.get()))
         {
            if (dyingFile != nullptr && audio->fileSource == dyingFile)
               audio->fileSource = nullptr;
         }
         for (int slot = 0, modCount = other.node->ModulatorInputCount(); slot < modCount; slot++)
         {
            IModulator** slotField = other.node->ModulatorInputSlot(slot);
            if ((dyingMod != nullptr && *slotField == dyingMod) ||
                (dyingY != nullptr && *slotField == dyingY))
               *slotField = nullptr;
         }
         int inputs = InputCountFor(other);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(other, slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
         // Same generic pattern as the geometry loop above, for audio/note
         // pins - AudioInputSlot()/NoteInputSlot() finds them whatever the
         // node calls its field internally, so a new audio node type can't
         // forget to register here either.
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = other.node->AudioInputSlot(slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = other.node->NoteInputSlot(slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
         // Turbo: Receive Audio/Notes' hidden cables point past the Send, at
         // whatever feeds it, so they have to let go of a dying node too.
         for (int w = 0; AudioCable* cable = other.node->WirelessAudioInput(w); w++)
            if (cable->GetSource() == dying)
               cable->Disconnect();
         for (int w = 0; NoteCable* cable = other.node->WirelessNoteInput(w); w++)
            if (cable->GetSource() == dying)
               cable->Disconnect();
      }
   }

   // Looks up the pooled-buffer index a node's AudioNode was assigned, or -1
   // if that node isn't (yet) an entry in `outOrder` - either because it has
   // no audio input connected to it at all, or (defensively) because it
   // isn't an audio source. -1 means "read as silence", same as an
   // unconnected pin - see AudioTopologyEntry in AudioEngine.h.
   AudioNode* AudioNodeOfAny(INode* node)
   {
      if (node == nullptr)
         return nullptr;
      if (auto* src = dynamic_cast<IAudioSource*>(node))
         return src->GetAudioNode();
      if (auto* noteSrc = dynamic_cast<INoteSource*>(node))
         return noteSrc->GetAudioNode();
      if (auto* adisp = dynamic_cast<AudioDisplacementNode*>(node))
         return adisp->GetAudioNode();
      if (auto* at = dynamic_cast<AudioTextureNode*>(node))
         return at->GetAudioNode();
      if (auto* ar = dynamic_cast<AudioRibbonNode*>(node))
         return ar->GetAudioNode();
      if (auto* acr = dynamic_cast<AudioColorRampNode*>(node))
         return acr->GetAudioNode();
      if (auto* aan = dynamic_cast<AudioAnalyzeNode*>(node))
         return aan->GetAudioNode();
      return node->AudioNodeForNotePorts();
   }

   int AudioBufferIndexOf(INode* node, const std::unordered_map<AudioNode*, int>& bufferIndexOf, int outputSlot = 0)
   {
      // Turbo 0.44: a node with several audio outputs maps each to its own
      // AudioNode (and so its own buffer).
      AudioNode* an = (node != nullptr && outputSlot > 0) ? node->AudioNodeForOutput(outputSlot) : nullptr;
      if (an == nullptr)
         an = AudioNodeOfAny(node);
      if (an == nullptr)
         return -1;
      auto it = bufferIndexOf.find(an);
      return (it != bufferIndexOf.end()) ? it->second : -1;
   }

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
   // Turbo: points every Receive Audio / Receive Notes' hidden cables at
   // whatever feeds the Sends on its channel - past the Send itself, which
   // has no audio node of its own. Run once per frame; true when anything
   // changed, so the caller rebuilds the topology once. A Send fed straight
   // from its own Receive is skipped (that loop has no delay stage).
   bool UpdateWirelessCables()
   {
      bool changed = false;
      for (GraphNode& gn : gNodes)
      {
         auto* recv = dynamic_cast<WirelessNode*>(gn.node.get());
         if (recv == nullptr || recv->IsSend())
            continue;
         if (recv->GetKind() == WirelessNode::kAudio)
         {
            int w = 0;
            for (WirelessNode* s : WirelessNode::Senders(WirelessNode::kAudio, recv->channel))
            {
               const AudioCable& in = static_cast<SendAudioNode*>(s)->input;
               if (!in.IsConnected() || in.GetSource() == gn.node.get())
                  continue;
               AudioCable* hidden = gn.node->WirelessAudioInput(w);
               if (hidden == nullptr)
                  break;
               if (hidden->GetSource() != in.GetSource() || hidden->GetOutputSlot() != in.GetOutputSlot())
               {
                  hidden->Connect(in.GetSource(), in.GetOutputSlot());
                  changed = true;
               }
               w++;
            }
            for (; AudioCable* hidden = gn.node->WirelessAudioInput(w); w++)
               if (hidden->IsConnected())
               {
                  hidden->Disconnect();
                  changed = true;
               }
         }
         else if (recv->GetKind() == WirelessNode::kNotes)
         {
            int w = 0;
            for (WirelessNode* s : WirelessNode::Senders(WirelessNode::kNotes, recv->channel))
            {
               const NoteCable& in = static_cast<SendNotesNode*>(s)->noteInput;
               if (!in.IsConnected() || in.GetSource() == gn.node.get())
                  continue;
               NoteCable* hidden = gn.node->WirelessNoteInput(w);
               if (hidden == nullptr)
                  break;
               if (hidden->GetSource() != in.GetSource() || hidden->GetOutputSlot() != in.GetOutputSlot())
               {
                  hidden->Connect(in.GetSource(), in.GetOutputSlot());
                  changed = true;
               }
               w++;
            }
            for (; NoteCable* hidden = gn.node->WirelessNoteInput(w); w++)
               if (hidden->IsConnected())
               {
                  hidden->Disconnect();
                  changed = true;
               }
         }
      }
      return changed;
   }

   void CollectAudioChain(INode* node, std::set<INode*>& visited,
                           std::vector<AudioTopologyEntry>& outOrder,
                           std::unordered_map<AudioNode*, int>& bufferIndexOf)
   {
      if (node == nullptr || visited.count(node) != 0)
         return;
      visited.insert(node);
      // Turbo 0.59: a switched-off Set block is left out entirely, so whatever
      // it fed (a master mixer outside the block, say) reads silence.
      if (node->blockOff)
         return;

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
            CollectAudioChain(cable->GetSource(), visited, outOrder, bufferIndexOf);
            entry.inputBufferIndices[slot] = AudioBufferIndexOf(cable->GetSource(), bufferIndexOf, cable->GetOutputSlot());
         }
      }
      // Turbo: Receive Audio's hidden per-Send cables (no pins of their own,
      // so they take topology inputs 0..n-1). A wireless loop back to this
      // node is already `visited`, so it reads as silence instead of recursing.
      for (int w = 0; w < kAudioMaxNodeInputs; w++)
      {
         AudioCable* cable = node->WirelessAudioInput(w);
         if (cable == nullptr)
            break;
         entry.numInputs = std::max(entry.numInputs, w + 1);
         if (cable->IsConnected())
         {
            CollectAudioChain(cable->GetSource(), visited, outOrder, bufferIndexOf);
            entry.inputBufferIndices[w] = AudioBufferIndexOf(cable->GetSource(), bufferIndexOf, cable->GetOutputSlot());
         }
      }

      // A bypassed audio processor aliases its first connected upstream
      // buffer instead of running its DSP. Downstream cables still point at
      // this node, so publish the alias under the bypassed AudioNode key.
      if (node->bypassed)
      {
         if (AudioNode* audioNode = AudioNodeOfAny(node))
         {
            for (int slot = 0; slot < entry.numInputs; ++slot)
            {
               if (entry.inputBufferIndices[slot] >= 0)
               {
                  bufferIndexOf[audioNode] = entry.inputBufferIndices[slot];
                  return;
               }
            }
            // A bypassed source has no upstream buffer to alias. Do not add
            // its AudioNode to the topology: downstream lookups resolve to
            // -1 and therefore receive silence. This is the expected mute
            // behaviour for Video and every other audio-generating source.
            return;
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
            CollectAudioChain(cable->GetSource(), visited, outOrder, bufferIndexOf);
      }
      for (int w = 0; NoteCable* cable = node->WirelessNoteInput(w); w++)
         if (cable->IsConnected())
            CollectAudioChain(cable->GetSource(), visited, outOrder, bufferIndexOf);

      if (AudioNode* audioNode = AudioNodeOfAny(node))
      {
         entry.node = audioNode;
         entry.outputBufferIndex = (int)outOrder.size();
         bufferIndexOf[audioNode] = entry.outputBufferIndex;
         outOrder.push_back(entry);
         // Turbo 0.44: extra audio outputs - one more entry (and buffer)
         // each, same inputs, right after the node's own.
         for (int x = 0; x < node->ExtraAudioNodeCount(); x++)
         {
            AudioNode* extra = node->ExtraAudioNode(x);
            if (extra == nullptr || bufferIndexOf.count(extra) != 0)
               continue;
            AudioTopologyEntry e = entry;
            e.node = extra;
            e.outputBufferIndex = (int)outOrder.size();
            bufferIndexOf[extra] = e.outputBufferIndex;
            outOrder.push_back(e);
         }
      }
   }

   // Rebuilds the whole real-time audio topology (the DAG in AudioEngine.h's
   // AudioTopology, not a flat list) from scratch and publishes it. Called
   // after any connect/disconnect/spawn/delete that touches an
   // audio-relevant node or link. A full rebuild rather than an incremental
   // diff is deliberate - see docs/plans/audio/README.md P2: a graph this
   // size is cheap to walk fully, and SetTopology's one-generation-retire
   // design already makes a full rebuild (nodes AND its buffer pool) cheap
   // on the main thread.
   //
   // AudioOutputNode and OutputNode contribute no AudioNode of their own to
   // `order`, but each connected audio input's source buffer becomes a
   // terminal entry wired into that node's capture ring.
   // Turbo 0.50: nodes whose LatencySamples() can change without a graph
   // edit (AudioNode::LatencyMayChange, the Super Mixer limiter), captured
   // by RebuildAudioTopology only where that latency reaches a delay-
   // compensation merge. The main loop polls them once per frame and asks
   // for a rebuild when one changed, so a limiter toggle on a plain
   // mixer -> Audio Out chain never rebuilds (a rebuild re-prepares, and so
   // resets, every effect in the patch).
   struct LatencyWatch
   {
      AudioNode* node;
      int latency;
   };
   std::vector<LatencyWatch> gLatencyWatch;

   void RebuildAudioTopology()
   {
      gLatencyWatch.clear(); // never keep pointers past a rebuild (or a deferred one)
      if (gDeferAudioRebuild)
         return;
      std::vector<AudioTopologyEntry> order;
      std::set<INode*> visited;
      std::unordered_map<AudioNode*, int> bufferIndexOf;
      std::vector<AudioTerminal> terminals;
      std::vector<bool> terminalLive; // Turbo: parallel to terminals

      for (GraphNode& gn : gNodes)
      {
         auto* audioOut = dynamic_cast<AudioOutputNode*>(gn.node.get());
         auto* outNode = dynamic_cast<OutputNode*>(gn.node.get());
         if (audioOut == nullptr && outNode == nullptr)
            continue;
         // Output nodes are terminals, not processors, so they never pass
         // through CollectAudioChain's bypass branch. Excluding the terminal
         // here is what makes their bypass control actually mute the device.
         if (gn.node->bypassed || gn.node->blockOff)
            continue;

         AudioCaptureRing* ring = audioOut ? &audioOut->CaptureRing() : &outNode->CaptureRing();
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = gn.node->AudioInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            CollectAudioChain(cable->GetSource(), visited, order, bufferIndexOf);
            const int idx = AudioBufferIndexOf(cable->GetSource(), bufferIndexOf, cable->GetOutputSlot());
            if (idx >= 0)
            {
               // Capture is set unconditionally, gated at write-time on the
               // ring's own `enabled` flag - see AudioTerminal's comment.
               terminals.push_back({ idx, ring });
               terminals.back().live = audioOut != nullptr && audioOut->live;
               terminalLive.push_back(audioOut != nullptr && audioOut->live);
            }
         }
      }

      // Note-only chains that never reach an Audio Out at all - an Envelope
      // driving a visual param through Modulation::Bind has no audio cable
      // anywhere in its chain, so the walk above never finds it (and never
      // will: its output is a modulator value, not an audio buffer). Seed
      // separately from every node with a connected note input; visited
      // dedupes anything the Audio Out walk already picked up (e.g. an
      // Oscillator's own note-source chain).
      for (GraphNode& gn : gNodes)
      {
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = gn.node->NoteInputSlot(slot);
            if (cable != nullptr && cable->IsConnected())
               CollectAudioChain(gn.node.get(), visited, order, bufferIndexOf);
         }
         for (int w = 0; NoteCable* cable = gn.node->WirelessNoteInput(w); w++)
            if (cable->IsConnected())
               CollectAudioChain(gn.node.get(), visited, order, bufferIndexOf);
      }

      // A node that must run even though nothing downstream pulls it and it
      // has no note input - e.g. a Sampler currently recording its audio
      // input with nothing wired past it. `visited` dedupes against the two
      // seeds above.
      for (GraphNode& gn : gNodes)
      {
         if (gn.node->RequiresAudioProcessing())
            CollectAudioChain(gn.node.get(), visited, order, bufferIndexOf);
      }

      // Turbo 0.43: in Timeline mode every node an audio clip plays must run,
      // wired to an Audio Out or not.
      ArrangeSeedAudio(visited, order, bufferIndexOf);

      // Wire each note-consuming node's inbox to its producer's outbox, now
      // that every relevant node has an `order` entry. NoteEventQueue needs
      // no PrepareToPlay (a fixed-size member, allocated with the AudioNode
      // itself), so this can run before or after the PrepareToPlay loop
      // below without ordering consequences.
      //
      // A real loop over every unified slot, not just 0: every note-consuming
      // node before AudioPluginNode put its one note pin at slot 0, but
      // AudioPluginNode's lives at slot 1 (audio stays at slot 0 there so
      // existing patches keep loading unchanged) - a node exposes at most one
      // note pin, so whichever slot answers non-null is it.
      // A producer's outbox can fan out to more than one consumer (two
      // synths off one Note Sequencer, an FM patch, etc) - each consumer
      // needs its own read cursor so draining events in one doesn't starve
      // another (see NoteEventQueue's multi-consumer contract). Reset every
      // outbox that's in play this generation before handing out fresh
      // cursor ids, so ids never leak across rebuilds.
      {
         std::set<NoteEventQueue*> resetOutboxes;
         for (GraphNode& gn : gNodes)
         {
            // Visit every slot the node exposes, not just the first - a
            // multi-input consumer (Note Merge) can have several connected
            // note pins whose producers all need their outbox reset.
            auto resetFor = [&resetOutboxes](NoteCable* cable)
            {
               if (cable == nullptr || !cable->IsConnected())
                  return;
               if (AudioNode* producer = AudioNodeOfAny(cable->GetSource()))
               {
                  if (NoteEventQueue* outbox = producer->NoteOutbox(cable->GetOutputSlot()))
                  {
                     if (resetOutboxes.insert(outbox).second)
                        outbox->ResetConsumers();
                  }
               }
            };
            for (int slot = 0; slot < kMaxNoteSlots; slot++)
               resetFor(gn.node->NoteInputSlot(slot));
            for (int w = 0; NoteCable* cable = gn.node->WirelessNoteInput(w); w++)
               resetFor(cable);
         }
      }
      for (GraphNode& gn : gNodes)
      {
         AudioNode* consumer = AudioNodeOfAny(gn.node.get());
         if (consumer == nullptr)
            continue;
         // Same "every slot, not just the first" widening as the reset pass
         // above. Every slot the node actually exposes gets a fresh call
         // this generation - including unconnected ones, set to
         // nullptr/-1 - so a slot that was wired last generation and got
         // disconnected doesn't leave a stale producer pointer behind on
         // the audio node.
         auto wire = [consumer](int slot, NoteCable* cable)
         {
            NoteEventQueue* inbox = nullptr;
            int cursor = -1;
            if (cable->IsConnected())
            {
               if (AudioNode* producer = AudioNodeOfAny(cable->GetSource()))
               {
                  inbox = producer->NoteOutbox(cable->GetOutputSlot());
                  if (inbox != nullptr)
                     cursor = inbox->RegisterConsumer();
               }
            }
            consumer->SetNoteInbox(slot, inbox, cursor);
         };
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = gn.node->NoteInputSlot(slot);
            if (cable == nullptr)
               continue;
            wire(slot, cable);
         }
         // Turbo: Receive Notes' hidden per-Send cables are inboxes 0..n-1
         // (it has no note pins of its own to collide with).
         for (int w = 0; NoteCable* cable = gn.node->WirelessNoteInput(w); w++)
            wire(w, cable);
      }

      const double sampleRate = AudioEngine::Instance().SampleRate();
      if (sampleRate > 0.0)
      {
         int blockFrames = (int)Platform::AudioDeviceBufferFrames(gAudioOutputDeviceId);
         if (blockFrames <= 0)
            blockFrames = gAudioBufferFrames;
         blockFrames = std::clamp(blockFrames, 64, kAudioMaxBlockFrames);
         for (AudioTopologyEntry& entry : order)
            entry.node->PrepareToPlay(sampleRate, blockFrames);
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
      // outputBufferIndex is exactly its own position in `order` (set at
      // push_back time above) - so a single forward pass can compute every
      // entry's cumulative latency by looking up its already-computed
      // inputs' cumulative latency by buffer index, with no separate
      // topological sort needed.
      //
      // Cumulative latency = this node's own reported latency plus the
      // latest-arriving (max) of its connected inputs' cumulative latency -
      // a node's output is only as "early" as its slowest input, same logic
      // any DAW's PDC uses. Note-only inputs carry no cumulative latency of
      // their own (MIDI events aren't delayed by this pass at all) and
      // aren't represented in inputBufferIndices, so they don't factor in.
      std::vector<int> cumulativeLatency(order.size(), 0);
      for (size_t k = 0; k < order.size(); k++)
      {
         AudioTopologyEntry& entry = order[k];
         int maxUpstream = 0;
         for (int i = 0; i < entry.numInputs; i++)
         {
            const int idx = entry.inputBufferIndices[i];
            if (idx >= 0)
               maxUpstream = std::max(maxUpstream, cumulativeLatency[(size_t)idx]);
         }
         cumulativeLatency[k] = entry.node->LatencySamples() + maxUpstream;
      }

      // At every merge point - a node with more than one connected input
      // pin, or the terminal summation below - each branch but the
      // slowest-arriving one needs a compensating delay equal to the gap, so
      // every pin/terminal lands sample-aligned instead of comb-filtering
      // against its siblings. A branch already at (or past) the max, or a
      // node with only one connected pin, gets an inactive (unallocated)
      // CompensationDelay - Prepare(0, ...) is the default-constructed
      // state, so those pins are simply left untouched. Fixed capacity
      // (kAudioMaxChannels), matching PooledBuffer::Allocate's own
      // discipline, so a topology generation is never under-allocated if
      // the device's actual channel count changes mid-generation.
      for (AudioTopologyEntry& entry : order)
      {
         int maxAmongConnected = 0;
         for (int i = 0; i < entry.numInputs; i++)
         {
            const int idx = entry.inputBufferIndices[i];
            if (idx >= 0)
               maxAmongConnected = std::max(maxAmongConnected, cumulativeLatency[(size_t)idx]);
         }
         for (int i = 0; i < entry.numInputs; i++)
         {
            const int idx = entry.inputBufferIndices[i];
            if (idx < 0)
               continue;
            const int delay = maxAmongConnected - cumulativeLatency[(size_t)idx];
            if (delay > 0)
               entry.inputCompensation[i].Prepare(delay, kAudioMaxChannels);
         }
      }

      // Same alignment one level up, across whichever Audio Out terminals
      // are summed together into the device buffer in RunTopology.
      {
         // Turbo: "live" Audio Outs neither set nor receive this alignment.
         int maxAmongTerminals = 0;
         for (size_t t = 0; t < terminals.size(); t++)
            if (terminals[t].bufferIndex >= 0 && !(t < terminalLive.size() && terminalLive[t]))
               maxAmongTerminals = std::max(maxAmongTerminals, cumulativeLatency[(size_t)terminals[t].bufferIndex]);
         for (size_t t = 0; t < terminals.size(); t++)
         {
            AudioTerminal& terminal = terminals[t];
            if (terminal.bufferIndex < 0 || (t < terminalLive.size() && terminalLive[t]))
               continue;
            const int delay = maxAmongTerminals - cumulativeLatency[(size_t)terminal.bufferIndex];
            if (delay > 0)
               terminal.compensation.Prepare(delay, kAudioMaxChannels);
         }
      }

      // Turbo 0.50: which nodes' latency feeds a merge (a node with 2+
      // connected inputs, or 2+ aligned Audio Outs). `order` is topological,
      // so one reverse pass carries the mark from every merge back up to all
      // of its ancestors.
      {
         std::vector<char> feedsMerge(order.size(), 0);
         int aligned = 0;
         for (size_t t = 0; t < terminals.size(); t++)
            if (terminals[t].bufferIndex >= 0 && !(t < terminalLive.size() && terminalLive[t]))
               aligned++;
         if (aligned > 1)
            for (size_t t = 0; t < terminals.size(); t++)
               if (terminals[t].bufferIndex >= 0 && !(t < terminalLive.size() && terminalLive[t]))
                  feedsMerge[(size_t)terminals[t].bufferIndex] = 1;
         for (size_t k = order.size(); k-- > 0;)
         {
            const AudioTopologyEntry& entry = order[k];
            int connected = 0;
            for (int i = 0; i < entry.numInputs; i++)
               connected += entry.inputBufferIndices[i] >= 0 ? 1 : 0;
            if (connected < 2 && !feedsMerge[k])
               continue;
            for (int i = 0; i < entry.numInputs; i++)
               if (entry.inputBufferIndices[i] >= 0)
                  feedsMerge[(size_t)entry.inputBufferIndices[i]] = 1;
         }
         for (size_t k = 0; k < order.size(); k++)
            if (feedsMerge[k] && order[k].node->LatencyMayChange())
               gLatencyWatch.push_back({ order[k].node, order[k].node->LatencySamples() });
      }

      // Turbo: taps between nodes (MPC Out -> MPC) resolve against the graph
      // the engine is about to run.
      for (GraphNode& gn : gNodes)
         gn.node->ResolveAudioTaps();

      AudioTopology topology;
      ArrangeBuildAudioTerminals(bufferIndexOf, topology);
      topology.order = std::move(order);
      MarkDuplicateDeviceTerminals(terminals); // Turbo 0.45 (upstream R477)
      topology.terminalBufferIndices = std::move(terminals);
      topology.numBuffers = (int)topology.order.size();
      AudioEngine::Instance().SetTopology(std::move(topology));
      gRetiredSafeAfterBlocks = AudioEngine::Instance().BlocksDone() + 2; // Turbo 0.47
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
      // Turbo 0.51 (upstream 0141df2): park the audio thread on an empty topology
      // BEFORE the device opens, so the rebuild's PrepareToPlay never writes
      // nodes a live callback can still reach.
      AudioEngine::Instance().SetTopology(AudioTopology{});
      if (!AudioEngine::Instance().Start(outError))
         return false;
      RebuildAudioTopology();
      return true;
   }

   Patch::SceneSettings CaptureSceneSettings()
   {
      Patch::SceneSettings s;
      s.present = true;
      s.audioOutputDeviceId = gAudioOutputDeviceId;
      s.audioInputDeviceId = gAudioInputDeviceId;
      s.audioDriver = gAudioDriver;
      s.audioMirror = gAudioMirror;
      s.audioMirrorDevice = gAudioMirrorDevice;
      s.audioSampleRate = gAudioSampleRate;
      s.audioBufferFrames = gAudioBufferFrames;
      s.audioOversample = gAudioOversample;
      s.targetFps = gTargetFps;
      s.vsync = gVsync;
      s.snapToGrid = gSnapToGrid;
      s.gridSnap = gGridSnap;
      s.zoomSensitivity = gZoomSensitivity;
      s.minimapEnabled = gMinimapEnabled;
      s.minimapCorner = gMinimapCorner;
      s.minimapSize = gMinimapSize;
      s.minimapOpacity = gMinimapOpacity;
      s.nodePanelOpen = gNodePanelOpen;
      s.nodePanelWidth = gNodePanelWidth;
      s.viewportPanelDock = gViewportPanelDock;
      s.viewportPanelWidth = gViewportPanelWidth;
      s.viewportPanelHeight = gViewportPanelHeight;
      s.themePreset = CategoryColors::CurrentPreset();
      s.diagnosticLog = gDiagnosticLogEnabled;
      s.autosaveEnabled = gAutosaveEnabled;
      s.autosaveSeconds = gAutosaveSeconds;
      s.audioAutoStart = gAudioAutoStart;
      s.uiScale = gUiScaleSetting;
      s.uiFont = gUiFontId;
      s.updateCheck = gUpdateCheckEnabled;
      s.startWithExample = gStartWithExample;
      s.startupMode = gStartupMode;
      s.startupPatch = gStartupPatch;
      s.hasStartupMode = true;
      return s;
   }

   void ApplySceneSettings(const Patch::SceneSettings& s, bool restartRunningAudio)
   {
      if (!s.present)
         return;

      const bool wasRunning = restartRunningAudio && AudioEngine::Instance().SampleRate() > 0.0;
      if (wasRunning)
         AudioEngine::Instance().Stop();

      gAudioOutputDeviceId = s.audioOutputDeviceId;
      gAudioInputDeviceId = s.audioInputDeviceId;
      gAudioDriver = std::max(0, std::min(s.audioDriver, 4));
      gAudioMirror = s.audioMirror;
      gAudioMirrorDevice = s.audioMirrorDevice;
      gAudioSampleRate = s.audioSampleRate;
      gAudioBufferFrames = std::max(32, std::min(s.audioBufferFrames, 8192));
      gAudioOversample = std::max(1.0f, std::min(s.audioOversample, 4.0f));
      gTargetFps = std::max(0, std::min(s.targetFps, 1000));
      gVsync = s.vsync;
      gSnapToGrid = s.snapToGrid;
      gGridSnap = std::max(5.0f, std::min(s.gridSnap, 100.0f));
      gZoomSensitivity = std::max(0.05f, std::min(s.zoomSensitivity, 1.5f));
      gMinimapEnabled = s.minimapEnabled;
      gMinimapCorner = std::max(0, std::min(s.minimapCorner, 3));
      gMinimapSize = std::max(120.0f, std::min(s.minimapSize, 360.0f));
      gMinimapOpacity = std::max(0.2f, std::min(s.minimapOpacity, 1.0f));
      gNodePanelOpen = s.nodePanelOpen;
      gNodePanelWidth = std::max(kNodePanelMinWidth, std::min(s.nodePanelWidth, 720.0f));
      gViewportPanelDock = std::max(0, std::min(s.viewportPanelDock, 3));
      gViewportPanelWidth = std::max(kViewportPanelMinWidth, std::min(s.viewportPanelWidth, 900.0f));
      gViewportPanelHeight = std::max(kViewportPanelMinHeight, std::min(s.viewportPanelHeight, 800.0f));
      gDiagnosticLogEnabled = s.diagnosticLog;
      gAutosaveEnabled = s.autosaveEnabled;
      gAutosaveSeconds = std::max(15, std::min(s.autosaveSeconds, 300));
      // App-level preference: only the machine settings file sets it (a
      // patch's own saved settings never switch it on or off).
      if (!restartRunningAudio)
      {
         gAudioAutoStart = s.audioAutoStart;
         gUpdateCheckEnabled = s.updateCheck;
         gStartWithExample = s.startWithExample;
         if (s.hasStartupMode)
         {
            gStartupMode = std::clamp(s.startupMode, 0, 3);
            gStartupPatch = s.startupPatch;
         }
         else
         {
            // First run of 0.59 on this machine: open empty, audio off (the
            // new defaults), whatever the older version was set to.
            gStartupMode = 0;
            gAudioAutoStart = false;
         }
         const float wantScale = (s.uiScale >= 0.5f && s.uiScale <= 3.0f) ? s.uiScale : 0.0f;
         if (wantScale != gUiScaleSetting)
         {
            gUiScaleSetting = wantScale;
            gUiRescaleRequested = true;
         }
         if (s.uiFont != gUiFontId)
         {
            gUiFontId = s.uiFont;
            gUiRescaleRequested = true;
         }
      }
      RuntimeLog::SetEnabled(gDiagnosticLogEnabled);

      Platform::AudioSetRequestedDriver(gAudioDriver);
      {
         std::string mirrorError;
         Platform::AudioMirrorApply(gAudioMirror, gAudioMirrorDevice, mirrorError);
      }
      AudioEngine::Instance().SetRequestedDevice(gAudioOutputDeviceId);
      AudioEngine::Instance().SetRequestedInputDevice(gAudioInputDeviceId);
      AudioEngine::Instance().SetRequestedSampleRate(gAudioSampleRate);
      AudioEngine::Instance().SetRequestedBufferFrames(gAudioBufferFrames);
      CategoryColors::SetPreset(s.themePreset);
      ApplyTheme();
      gMainSwapIntervalApplied = -1; // re-applied right before the next swap

      if (wasRunning)
      {
         gAudioStartError.clear();
         if (!StartAudioEngine(gAudioStartError))
            fprintf(stderr, "audio settings restore: %s\n", gAudioStartError.c_str());
      }
   }

   bool SameSceneSettings(const Patch::SceneSettings& a, const Patch::SceneSettings& b)
   {
      return a.audioOutputDeviceId == b.audioOutputDeviceId &&
             a.audioInputDeviceId == b.audioInputDeviceId && a.audioDriver == b.audioDriver &&
             a.audioMirror == b.audioMirror && a.audioMirrorDevice == b.audioMirrorDevice &&
             a.audioSampleRate == b.audioSampleRate &&
             a.audioBufferFrames == b.audioBufferFrames &&
             a.audioOversample == b.audioOversample && a.targetFps == b.targetFps &&
             a.vsync == b.vsync && a.snapToGrid == b.snapToGrid && a.gridSnap == b.gridSnap &&
             a.zoomSensitivity == b.zoomSensitivity && a.minimapEnabled == b.minimapEnabled &&
             a.minimapCorner == b.minimapCorner && a.minimapSize == b.minimapSize &&
             a.minimapOpacity == b.minimapOpacity && a.nodePanelOpen == b.nodePanelOpen &&
             a.nodePanelWidth == b.nodePanelWidth &&
             a.viewportPanelDock == b.viewportPanelDock && a.viewportPanelWidth == b.viewportPanelWidth &&
             a.viewportPanelHeight == b.viewportPanelHeight && a.themePreset == b.themePreset &&
             a.diagnosticLog == b.diagnosticLog && a.autosaveEnabled == b.autosaveEnabled &&
             a.autosaveSeconds == b.autosaveSeconds && a.audioAutoStart == b.audioAutoStart &&
             a.uiScale == b.uiScale && a.uiFont == b.uiFont && a.updateCheck == b.updateCheck &&
             a.startWithExample == b.startWithExample && a.startupMode == b.startupMode &&
             a.startupPatch == b.startupPatch;
   }

   void PersistSceneSettingsIfChanged()
   {
      static bool haveLast = false;
      static Patch::SceneSettings last;
      const Patch::SceneSettings current = CaptureSceneSettings();
      if (haveLast && SameSceneSettings(last, current))
      {
         gSuppressSettingsDirtyOnce = false;
         return;
      }
      if (haveLast && !gSuppressSettingsDirtyOnce && !gPatchPath.empty())
         gPatchDirty = true;
      gSuppressSettingsDirtyOnce = false;
      std::string error;
      if (!Patch::SaveAppSettings(current, error))
         fprintf(stderr, "settings: %s\n", error.c_str());
      last = current;
      haveLast = true;
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
      // Turbo 0.44.1: never touch the device while the timeline renders
      // offline (the main thread owns the graph; a restart reopened the ASIO
      // driver mid-render and crashed). Pending flags stay latched for later.
      if (AudioEngine::Instance().OfflineRender())
         return;
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

      // The window closes after kAudioRecoveryWindowMs of calm since the last
      // attempt ended, not since it opened: on Windows a WASAPI/ASIO restart
      // can take a second or more, so five failing attempts would otherwise
      // outlive a fixed 10 s window and recovery would never give up.
      if (gAudioRecoveryWindowStartMs < 0.0 || gLastAudioRecoveryAttemptMs < 0.0 ||
          nowMs - gLastAudioRecoveryAttemptMs > kAudioRecoveryWindowMs)
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
      // Stamp again once the restart is done: the min interval runs from the
      // end of an attempt. A slow restart fires its own config-change
      // notifications, and those must be swallowed, not start another one.
      gLastAudioRecoveryAttemptMs = glfwGetTime() * 1000.0;
   }

   void RemoveNodeByIndex(int index)
   {
      GraphNode* victim = FindNodeByIndex(index);
      if (victim == nullptr)
         return;
      PushUndoCheckpoint();
      ArrangeOnNodeRemoved(victim->uid);
      Modulation::Instance().UnbindAllFor(index);
      PaletteBinding::Instance().UnbindAllFor(index);
      GestureRecorder::Instance().ClearForNode(index);
      MidiMap::RemoveNode(index);
      gModHistory.erase(index);
      DisconnectAllTo(victim->node.get());
      // A deleted Group's membership set must go with it - otherwise its
      // GroupNode* stays around as a dangling map key that a future
      // allocation could reuse, silently handing a stranger's members to a
      // brand new group. Every other group also drops the index in case it
      // was one of its members.
      if (auto* deadGroup = dynamic_cast<GroupNode*>(victim->node.get()))
         gGroupMembers.erase(deadGroup);
      for (auto& entry : gGroupMembers)
         entry.second.erase(index);

      // Don't destroy the node/viewport here: this frame's ImGui draw list may
      // already contain AddImage() calls (queued while drawing this node's
      // body/mini-viewport earlier this frame) referencing their GL output
      // textures. Deleting those textures now, before
      // ImGui_ImplOpenGL3_RenderDrawData() actually submits the draw list at
      // the end of the frame, frees a GL name that the pending draw commands
      // still reference - producing a one-frame flash of garbage/reused-texture
      // content. Retire ownership instead; actual teardown happens at the top
      // of the next frame's loop, once this frame is fully rendered and
      // presented (see gRetiredNodes/gRetiredViewports drain next to
      // glfwPollEvents()).
      if (auto vp = gNodeViewports.extract(index); !vp.empty())
         gRetiredViewports.push_back(std::move(vp));
      gRetiredNodes.push_back(std::move(victim->node));

      gNodes.erase(std::remove_if(gNodes.begin(), gNodes.end(),
                                  [index](const GraphNode& g) { return g.index == index; }),
                   gNodes.end());

      // After, not before: victim is still in gNodes up to the erase() just
      // above, and rebuilding earlier would publish a topology that can
      // include the about-to-be-freed node's own AudioNode - exactly the
      // "pointer to a freed node crashes on the next cook" hazard
      // DisconnectAllTo's own comment warns about, just on the audio thread.
      RebuildAudioTopology();
   }
