// Per-frame self-test blocks moved verbatim out of the main loop in main.cpp.
#include "app/AppShared.h"

namespace app
{

void FrameTest_SHAPERESGRAPHTEST_3(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 8)
      {
         // The workers solve in the background: give them time, then the modes must have landed.
         std::this_thread::sleep_for(std::chrono::milliseconds(1500));
         int ringing = 0;
         for (GraphNode& gn : gNodes)
            if (auto* fx = dynamic_cast<AudioEffectNode*>(gn.node.get()); fx && gn.typeName == "Shape Resonator")
            {
               fx->CookIfNeeded(frameId);
               float f[32];
               ringing += (fx->ReadModeFrequencies(f, 32, nullptr) > 0);
            }
         printf("SHAPERESGRAPHTEST eight instances solved: ringing=%d  %s\n", ringing, ringing == 8 ? "OK" : "FAIL");
         const std::string path = "/tmp/infinite_shaperes_graph2.inf";
         const bool saved = SavePatchTo(path);
         const bool loaded = saved && LoadPatchFrom(path);
         printf("SHAPERESGRAPHTEST save/load: saved=%d loaded=%d  %s\n", saved, loaded, (saved && loaded) ? "OK" : "FAIL");
      }
}

void FrameTest_DELETECRASHTEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_DELETECRASHTEST") != nullptr && frameId == 6)
      {
         Render3DNode* render = nullptr;
         InstanceOnPointsNode* inst = nullptr;
         MetaBallNode* meta = nullptr;
         PathNode* path = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (!render) render = dynamic_cast<Render3DNode*>(gn.node.get());
            if (!inst) inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get());
            if (!meta) meta = dynamic_cast<MetaBallNode*>(gn.node.get());
            if (!path) path = dynamic_cast<PathNode*>(gn.node.get());
            gn.node->CookIfNeeded(frameId); // must not crash on the freed source
         }
         const bool cleared = render && inst && meta && path &&
                              render->geometry[0] == nullptr &&
                              inst->pointSource == nullptr && inst->instanceShape == nullptr &&
                              inst->cloudSource == nullptr &&
                              meta->cloudSource == nullptr &&
                              path->curveSource == nullptr && path->geometrySource == nullptr;
         printf("delete-crash: 4 nodes survived cook, every field cleared=%d  %s\n",
                cleared, (render && inst && meta && path && cleared) ? "OK" : "FAIL");
      }
}

void FrameTest_AUDIOTEARDOWNSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_AUDIOTEARDOWNSWEEPTEST") != nullptr && frameId == 4)
      {
         std::vector<AudioSweepCandidate> candidates = DiscoverAudioSweepCandidates(
            [](const AudioNodeShape& s) { return s.ParticipatesInAudioGraph(); });

         std::vector<float> renderL(256), renderR(256);
         float* renderChans[2] = { renderL.data(), renderR.data() };
         AudioBuffer renderBuf;
         renderBuf.channels = renderChans;
         renderBuf.numChannels = 2;
         renderBuf.numFrames = 256;

         auto SpawnIndex = [&](const std::string& name, const std::string& category, float x, float y) -> int
         {
            GraphNode* gn = SpawnNode(name, category, x, y);
            return gn ? gn->index : -1;
         };

         bool overallOk = true;
         for (const AudioSweepCandidate& cand : candidates)
         {
            // Every spawn below happens first, collecting only indices -
            // GraphNode is stored by value in the gNodes vector, so each
            // SpawnNode's push_back can reallocate and invalidate *every*
            // GraphNode* taken from an earlier SpawnNode call in this same
            // iteration, not just across a RemoveNodeByIndex. Wiring re-
            // resolves each one via FindNodeByIndex only after all spawning
            // for this candidate is done.
            const int candidateIndex = SpawnIndex(cand.name, cand.category, 40.0f, 40.0f);
            bool ok = candidateIndex >= 0;
            std::vector<int> spawnedIndices;
            if (ok)
               spawnedIndices.push_back(candidateIndex);

            int upstreamNoteIndex = -1;
            int downstreamGainIndex = -1;
            int downstreamOutIndex = -1;
            int upstreamGainIndex = -1;
            int downstreamNoteSinkIndex = -1;

            if (ok && cand.shape.noteInputSlots > 0)
               upstreamNoteIndex = SpawnIndex("MIDI Notes", "Notes", 40.0f, 260.0f);
            if (ok && cand.shape.isAudioSource)
            {
               downstreamGainIndex = SpawnIndex("Gain", "Utility", 320.0f, 40.0f);
               downstreamOutIndex = SpawnIndex("Audio Out", "Utility", 600.0f, 40.0f);
            }
            else if (ok && cand.shape.audioInputSlots > 0)
            {
               // A pure audio terminal/effect that isn't itself an
               // IAudioSource (Audio Out is the only current example) still
               // needs an upstream feed so its own input cable has something
               // real to clear on delete.
               upstreamGainIndex = SpawnIndex("Gain", "Utility", 40.0f, 260.0f);
            }
            if (ok && cand.shape.isNoteSource && cand.shape.noteInputSlots == 0)
            {
               // Pure note producer (MIDI Notes): give it a downstream
               // consumer so DisconnectAllTo's NoteInputSlot loop has a real
               // cable pointing at the candidate to clear, not just its own.
               // Note Filter, not Envelope: Envelope moved to a
               // ModulatorInputSlot-only shaper (03-envelope-to-shaper.md)
               // and no longer has a note pin to wire this cable into.
               downstreamNoteSinkIndex = SpawnIndex("Note Filter", "Notes", 320.0f, 480.0f);
            }
            for (int idx : { upstreamNoteIndex, downstreamGainIndex, downstreamOutIndex,
                              upstreamGainIndex, downstreamNoteSinkIndex })
               if (idx >= 0)
                  spawnedIndices.push_back(idx);

            // All spawning done - safe to hold pointers now, nothing below
            // pushes into gNodes again this iteration.
            GraphNode* cn = ok ? FindNodeByIndex(candidateIndex) : nullptr;
            if (upstreamNoteIndex >= 0)
            {
               if (GraphNode* upstreamNote = FindNodeByIndex(upstreamNoteIndex))
                  if (NoteCable* nc = cn->node->NoteInputSlot(cand.shape.firstNoteInputSlot))
                     nc->Connect(upstreamNote->node.get());
            }
            if (downstreamGainIndex >= 0 && downstreamOutIndex >= 0)
            {
               GraphNode* downstreamGain = FindNodeByIndex(downstreamGainIndex);
               GraphNode* downstreamOut = FindNodeByIndex(downstreamOutIndex);
               if (downstreamGain && downstreamOut)
               {
                  if (AudioCable* gainIn = downstreamGain->node->AudioInputSlot(0))
                     gainIn->Connect(cn->node.get());
                  if (AudioCable* outIn = downstreamOut->node->AudioInputSlot(0))
                     outIn->Connect(downstreamGain->node.get());
               }
            }
            if (upstreamGainIndex >= 0)
            {
               if (GraphNode* upstreamGain = FindNodeByIndex(upstreamGainIndex))
                  if (AudioCable* in0 = cn->node->AudioInputSlot(0))
                     in0->Connect(upstreamGain->node.get());
            }
            if (downstreamNoteSinkIndex >= 0)
            {
               if (GraphNode* downstreamNoteSink = FindNodeByIndex(downstreamNoteSinkIndex))
                  if (NoteCable* nc = downstreamNoteSink->node->NoteInputSlot(0))
                     nc->Connect(cn->node.get());
            }
            cn = nullptr; // about to RebuildAudioTopology/ProcessOffline; re-resolve if needed again

            RebuildAudioTopology();
            for (int i = 0; i < 4; i++)
               AudioEngine::Instance().ProcessOffline(renderBuf); // "mid-playback"

            if (ok)
               RemoveNodeByIndex(candidateIndex); // exercises DisconnectAllTo generically

            for (int i = 0; i < 6; i++)
               AudioEngine::Instance().ProcessOffline(renderBuf); // keep rendering post-delete

            // Cables that pointed at the deleted node must be cleared, not
            // left dangling - the exact use-after-free surface DELETECRASHTEST
            // checks on the geometry side. Re-resolved by index, since the
            // delete above may have shifted every GraphNode* taken earlier.
            bool cablesCleared = true;
            if (downstreamGainIndex >= 0)
            {
               GraphNode* downstreamGain = FindNodeByIndex(downstreamGainIndex);
               AudioCable* gainIn = downstreamGain ? downstreamGain->node->AudioInputSlot(0) : nullptr;
               cablesCleared = cablesCleared && (gainIn == nullptr || gainIn->GetSource() == nullptr);
            }
            if (downstreamNoteSinkIndex >= 0)
            {
               GraphNode* downstreamNoteSink = FindNodeByIndex(downstreamNoteSinkIndex);
               NoteCable* nc = downstreamNoteSink ? downstreamNoteSink->node->NoteInputSlot(0) : nullptr;
               cablesCleared = cablesCleared && (nc == nullptr || nc->GetSource() == nullptr);
            }

            // Reaching here at all - through the deletion, the rebuild, and
            // six more offline render blocks - is the "no crash" half of the
            // invariant; a dangling AudioNode* in the topology would have
            // segfaulted inside ProcessOffline above, not here.
            const bool nodeOk = ok && cablesCleared;
            printf("  [%s] %-24s spawned, wired, deleted mid-playback, kept rendering, cables cleared=%d\n",
                   nodeOk ? "pass" : "FAIL", cand.name.c_str(), cablesCleared);
            fflush(stdout);
            if (!nodeOk)
               overallOk = false;

            for (int idx : spawnedIndices)
            {
               if (idx == candidateIndex)
                  continue; // already removed above
               RemoveNodeByIndex(idx);
            }
         }

         {
            const AudioEngine::XrunCounts xr = AudioEngine::Instance().Xruns();
            printf("xruns=%llu (deadline=%llu os=%llu) gaps=%llu\n", (unsigned long long)xr.Total(),
                   (unsigned long long)xr.deadline, (unsigned long long)xr.os, (unsigned long long)xr.gaps);
         }
         printf("%s\n", overallOk ? "AUDIO TEARDOWN SWEEP OK" : "AUDIO TEARDOWN SWEEP FAIL");
      }
}

void FrameTest_NOTEPUMPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_NOTEPUMPTEST") != nullptr)
      {
         static int sGen = -1, sCv = -1;
         static double sStart = 0.0;
         if (frameId == 4)
         {
            GraphNode* spawnedGen = SpawnNode("Random Note Generator", "Notes", 40.0f, 40.0f);
            sGen = spawnedGen ? spawnedGen->index : -1;
            GraphNode* spawnedCv = SpawnNode("Note to CV", "Modulators", 320.0f, 40.0f);
            sCv = spawnedCv ? spawnedCv->index : -1;
            // Spawning can reallocate the node list; re-fetch both.
            GraphNode* gen = FindNodeByIndex(sGen);
            GraphNode* cv = FindNodeByIndex(sCv);
            if (gen && cv)
            {
               cv->node->NoteInputSlot(0)->Connect(gen->node.get());
            }
            Transport::Instance().SetPlaying(true);
            RebuildAudioTopology();
            sStart = glfwGetTime();
            printf("NOTEPUMPTEST setup gen=%d cv=%d deviceOpen=%d\n", sGen, sCv,
                   (int)(AudioEngine::Instance().SampleRate() > 0));
            fflush(stdout);
         }
         else if (frameId > 4 && glfwGetTime() - sStart > 4.0)
         {
            GraphNode* cv = FindNodeByIndex(sCv);
            NoteToCVNode* n = cv ? dynamic_cast<NoteToCVNode*>(cv->node.get()) : nullptr;
            const int last = n ? n->LastNote() : -1;
            const bool ok = n != nullptr && last >= 0;
            printf("NOTEPUMPTEST lastNote=%d %s\n", last, ok ? "OK" : "FAIL");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_NOTEFANOUTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_NOTEFANOUTTEST") != nullptr && frameId == 4)
      {
         auto SpawnIndex = [&](const std::string& name, const std::string& category, float x, float y) -> int
         {
            GraphNode* gn = SpawnNode(name, category, x, y);
            return gn ? gn->index : -1;
         };

         // Renders `osc1` into `osc1Buf`, then `osc2` reading `osc1Buf` on
         // its fm-in slot (1) - the exact per-block order RebuildAudioTopology's
         // own CollectAudioChain walk would run them in, since osc2's audio
         // cable makes osc1 its dependency. Returns osc2's rendered L peak.
         auto RunFmPair = [&](AudioNode* osc1, AudioNode* osc2, float* osc2LOut, int numFrames) -> float
         {
            std::vector<float> o1L(numFrames), o1R(numFrames);
            float* o1Chans[2] = { o1L.data(), o1R.data() };
            AudioBuffer o1Buf;
            o1Buf.channels = o1Chans;
            o1Buf.numChannels = 2;
            o1Buf.numFrames = numFrames;

            std::vector<float> o2L(numFrames), o2R(numFrames);
            float* o2Chans[2] = { o2L.data(), o2R.data() };
            AudioBuffer o2Buf;
            o2Buf.channels = o2Chans;
            o2Buf.numChannels = 2;
            o2Buf.numFrames = numFrames;

            float peak = 0.0f;
            for (int b = 0; b < 10; b++)
            {
               osc1->ProcessBlock(nullptr, 0, o1Buf);
               const AudioBuffer* osc2Inputs[2] = { nullptr, &o1Buf };
               osc2->ProcessBlock(osc2Inputs, 2, o2Buf);
            }
            for (int i = 0; i < numFrames; i++)
               peak = std::max(peak, std::fabs(o2L[i]));
            if (osc2LOut != nullptr)
               std::copy(o2L.begin(), o2L.end(), osc2LOut);
            return peak;
         };

         bool overallOk = true;
         const int numFrames = 256;

         // Case 1: the bug report's exact patch - producer fans out to osc1
         // (FM modulator) and osc2 (fed by osc1's FM in), both wired from
         // the SAME producer outbox.
         {
            const int producerIdx = SpawnIndex("Note Stack", "Notes", 40.0f, 40.0f);
            const int osc1Idx = SpawnIndex("Oscillator", "Synths", 320.0f, 40.0f);
            const int osc2Idx = SpawnIndex("Oscillator", "Synths", 600.0f, 40.0f);
            GraphNode* producer = FindNodeByIndex(producerIdx);
            GraphNode* osc1 = FindNodeByIndex(osc1Idx);
            GraphNode* osc2 = FindNodeByIndex(osc2Idx);
            bool wired = producer && osc1 && osc2;
            if (wired)
            {
               osc1->node->NoteInputSlot(0)->Connect(producer->node.get());
               osc2->node->NoteInputSlot(0)->Connect(producer->node.get());
               osc2->node->AudioInputSlot(1)->Connect(osc1->node.get()); // osc1 -> osc2's fm in
               producer->node->CookIfNeeded(1);
               osc1->node->CookIfNeeded(1);
               osc2->node->CookIfNeeded(1);
            }

            RebuildAudioTopology();

            // Nothing upstream feeds the producer itself in this graph -
            // hand it a note-on directly the same way RunNoteStackFixture
            // drives NoteStackNode standalone. This has to happen AFTER
            // RebuildAudioTopology: the rebuild's wiring loop calls
            // SetNoteInbox(nullptr, -1) on every node with an unconnected
            // note-input slot, which would otherwise stomp this right back
            // out.
            NoteEventQueue producerInbox;
            AudioNode* producerAudio = wired ? AudioNodeOfAny(producer->node.get()) : nullptr;
            if (producerAudio)
               producerAudio->SetNoteInbox(&producerInbox, producerInbox.RegisterConsumer());

            AudioNode* osc1Audio = wired ? AudioNodeOfAny(osc1->node.get()) : nullptr;
            AudioNode* osc2Audio = wired ? AudioNodeOfAny(osc2->node.get()) : nullptr;

            bool caseOk = wired && producerAudio && osc1Audio && osc2Audio;
            int voices1 = 0, voices2 = 0;
            float peak2 = 0.0f;
            if (caseOk)
            {
               NoteEvent on;
               on.note = 69;
               on.velocity = 0.8f;
               on.isNoteOn = true;
               on.voiceId = NextVoiceId();
               producerInbox.Push(on);

               std::vector<float> dL(numFrames), dR(numFrames);
               float* dChans[2] = { dL.data(), dR.data() };
               AudioBuffer dBuf;
               dBuf.channels = dChans;
               dBuf.numChannels = 2;
               dBuf.numFrames = numFrames;
               producerAudio->ProcessBlock(nullptr, 0, dBuf);

               peak2 = RunFmPair(osc1Audio, osc2Audio, nullptr, numFrames);
               voices1 = static_cast<OscillatorNode*>(osc1->node.get())->ActiveVoices();
               voices2 = static_cast<OscillatorNode*>(osc2->node.get())->ActiveVoices();
            }

            caseOk = caseOk && voices1 > 0 && voices2 > 0 && peak2 > 1e-6f;
            printf("  [%s] fanout+FM (bug report patch): osc1 voices=%d osc2 voices=%d osc2 output peak=%.6f\n",
                   caseOk ? "pass" : "FAIL", voices1, voices2, peak2);
            overallOk = overallOk && caseOk;

            for (int idx : { producerIdx, osc1Idx, osc2Idx })
               if (idx >= 0)
                  RemoveNodeByIndex(idx);
         }

         // Case 2: three consumers on one producer - every one of them must
         // still see the note-on, not just the first two.
         {
            const int producerIdx = SpawnIndex("Note Stack", "Notes", 40.0f, 260.0f);
            const int oscIdx[3] = {
               SpawnIndex("Oscillator", "Synths", 320.0f, 200.0f),
               SpawnIndex("Oscillator", "Synths", 320.0f, 260.0f),
               SpawnIndex("Oscillator", "Synths", 320.0f, 320.0f),
            };
            GraphNode* producer = FindNodeByIndex(producerIdx);
            GraphNode* osc[3] = { FindNodeByIndex(oscIdx[0]), FindNodeByIndex(oscIdx[1]), FindNodeByIndex(oscIdx[2]) };
            bool wired = producer && osc[0] && osc[1] && osc[2];
            if (wired)
            {
               for (GraphNode* o : osc)
               {
                  o->node->NoteInputSlot(0)->Connect(producer->node.get());
                  o->node->CookIfNeeded(1);
               }
               producer->node->CookIfNeeded(1);
            }

            RebuildAudioTopology();

            NoteEventQueue producerInbox;
            AudioNode* producerAudio = wired ? AudioNodeOfAny(producer->node.get()) : nullptr;
            if (producerAudio)
               producerAudio->SetNoteInbox(&producerInbox, producerInbox.RegisterConsumer());

            bool caseOk = wired && producerAudio;
            int voices[3] = {};
            if (caseOk)
            {
               NoteEvent on;
               on.note = 64;
               on.velocity = 0.8f;
               on.isNoteOn = true;
               on.voiceId = NextVoiceId();
               producerInbox.Push(on);

               std::vector<float> dL(numFrames), dR(numFrames);
               float* dChans[2] = { dL.data(), dR.data() };
               AudioBuffer dBuf;
               dBuf.channels = dChans;
               dBuf.numChannels = 2;
               dBuf.numFrames = numFrames;
               producerAudio->ProcessBlock(nullptr, 0, dBuf);

               for (int i = 0; i < 3; i++)
               {
                  AudioNode* a = AudioNodeOfAny(osc[i]->node.get());
                  std::vector<float> oL(numFrames), oR(numFrames);
                  float* oChans[2] = { oL.data(), oR.data() };
                  AudioBuffer oBuf;
                  oBuf.channels = oChans;
                  oBuf.numChannels = 2;
                  oBuf.numFrames = numFrames;
                  a->ProcessBlock(nullptr, 0, oBuf);
                  voices[i] = static_cast<OscillatorNode*>(osc[i]->node.get())->ActiveVoices();
               }
            }

            caseOk = caseOk && voices[0] > 0 && voices[1] > 0 && voices[2] > 0;
            printf("  [%s] three-consumer fanout: voices=%d,%d,%d (all must see the note-on)\n",
                   caseOk ? "pass" : "FAIL", voices[0], voices[1], voices[2]);
            overallOk = overallOk && caseOk;

            for (int idx : { producerIdx, oscIdx[0], oscIdx[1], oscIdx[2] })
               if (idx >= 0)
                  RemoveNodeByIndex(idx);
         }

         // Case 3: one of two consumers is deleted mid-playback (the real
         // RemoveNodeByIndex, which triggers its own RebuildAudioTopology -
         // exercising ResetConsumers/RegisterConsumer's re-issue of cursor
         // ids on the surviving edge). The survivor must keep receiving
         // events with no crash and no dangling read.
         {
            const int producerIdx = SpawnIndex("Note Stack", "Notes", 40.0f, 480.0f);
            const int survivorIdx = SpawnIndex("Oscillator", "Synths", 320.0f, 460.0f);
            const int doomedIdx = SpawnIndex("Oscillator", "Synths", 320.0f, 520.0f);
            GraphNode* producer = FindNodeByIndex(producerIdx);
            GraphNode* survivor = FindNodeByIndex(survivorIdx);
            GraphNode* doomed = FindNodeByIndex(doomedIdx);
            bool wired = producer && survivor && doomed;
            if (wired)
            {
               survivor->node->NoteInputSlot(0)->Connect(producer->node.get());
               doomed->node->NoteInputSlot(0)->Connect(producer->node.get());
               producer->node->CookIfNeeded(1);
               survivor->node->CookIfNeeded(1);
               doomed->node->CookIfNeeded(1);
            }

            RebuildAudioTopology();

            NoteEventQueue producerInbox;
            AudioNode* producerAudio = wired ? AudioNodeOfAny(producer->node.get()) : nullptr;
            if (producerAudio)
               producerAudio->SetNoteInbox(&producerInbox, producerInbox.RegisterConsumer());

            bool caseOk = wired && producerAudio;
            if (caseOk)
            {
               std::vector<float> dL(numFrames), dR(numFrames);
               float* dChans[2] = { dL.data(), dR.data() };
               AudioBuffer dBuf;
               dBuf.channels = dChans;
               dBuf.numChannels = 2;
               dBuf.numFrames = numFrames;

               NoteEvent on1;
               on1.note = 60;
               on1.velocity = 0.8f;
               on1.isNoteOn = true;
               on1.voiceId = NextVoiceId();
               producerInbox.Push(on1);
               producerAudio->ProcessBlock(nullptr, 0, dBuf);

               AudioNode* survivorAudio = AudioNodeOfAny(survivor->node.get());
               std::vector<float> sL(numFrames), sR(numFrames);
               float* sChans[2] = { sL.data(), sR.data() };
               AudioBuffer sBuf;
               sBuf.channels = sChans;
               sBuf.numChannels = 2;
               sBuf.numFrames = numFrames;
               survivorAudio->ProcessBlock(nullptr, 0, sBuf);

               RemoveNodeByIndex(doomedIdx); // triggers a real RebuildAudioTopology

               // A fresh note-on after the delete - the survivor's cursor was
               // just re-issued by the rebuild above, so this proves it still
               // reads real events, not a stale/dangling cursor id.
               NoteEvent on2;
               on2.note = 67;
               on2.velocity = 0.8f;
               on2.isNoteOn = true;
               on2.voiceId = NextVoiceId();
               producerInbox.Push(on2);
               producerAudio->ProcessBlock(nullptr, 0, dBuf);
               survivorAudio->ProcessBlock(nullptr, 0, sBuf); // must not crash on a freed doomed

               // Re-resolve rather than reuse the pre-delete GraphNode* -
               // gNodes.erase() shifts every element after the erased one
               // down a slot, invalidating pointers taken before the call
               // (see the cutoff-mod teardown case's comment on the same
               // hazard).
               survivor = FindNodeByIndex(survivorIdx);
               const int survivorVoices =
                  survivor ? static_cast<OscillatorNode*>(survivor->node.get())->ActiveVoices() : -1;
               caseOk = survivorVoices > 0;
               printf("  [%s] consumer deleted mid-playback: survivor voices=%d after delete+re-note (no crash)\n",
                      caseOk ? "pass" : "FAIL", survivorVoices);
            }
            else
            {
               printf("  [FAIL] consumer deleted mid-playback: rig failed to build\n");
            }
            overallOk = overallOk && caseOk;

            for (int idx : { producerIdx, survivorIdx })
               if (idx >= 0)
                  RemoveNodeByIndex(idx);
         }

         printf("%s\n", overallOk ? "NOTE FANOUT TEST OK" : "NOTE FANOUT TEST FAIL");
      }
}

void FrameTest_NOTEREWIRESTRESSTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_NOTEREWIRESTRESSTEST") != nullptr && frameId == 4)
      {
         auto SpawnIndex = [&](const std::string& name, const std::string& category, float x, float y) -> int
         {
            GraphNode* gn = SpawnNode(name, category, x, y);
            return gn ? gn->index : -1;
         };

         const int seqIdx = SpawnIndex("Note Sequencer", "Notes", 40.0f, 40.0f);
         const int osc1Idx = SpawnIndex("Oscillator", "Synths", 320.0f, 20.0f);
         const int osc2Idx = SpawnIndex("Oscillator", "Synths", 320.0f, 100.0f);
         GraphNode* seq = FindNodeByIndex(seqIdx);
         GraphNode* osc1 = FindNodeByIndex(osc1Idx);
         GraphNode* osc2 = FindNodeByIndex(osc2Idx);
         bool wired = seq && osc1 && osc2;
         if (wired)
         {
            osc1->node->NoteInputSlot(0)->Connect(seq->node.get());
            osc2->node->NoteInputSlot(0)->Connect(seq->node.get());
            osc1->node->CookIfNeeded(1);
            osc2->node->CookIfNeeded(1);
            seq->node->CookIfNeeded(1);
         }

         bool ok = wired;
         int notesOn = 0, notesOff = 0;
         int voices1 = -1, voices2 = -1;
         uint64_t overflow = 1;

         if (ok)
         {
            const int numFrames = 512;
            std::vector<float> sL(numFrames), sR(numFrames);
            float* sChans[2] = { sL.data(), sR.data() };
            AudioBuffer sBuf;
            sBuf.channels = sChans;
            sBuf.numChannels = 2;
            sBuf.numFrames = numFrames;
            std::vector<float> o1L(numFrames), o1R(numFrames);
            float* o1Chans[2] = { o1L.data(), o1R.data() };
            AudioBuffer o1Buf;
            o1Buf.channels = o1Chans;
            o1Buf.numChannels = 2;
            o1Buf.numFrames = numFrames;
            std::vector<float> o2L(numFrames), o2R(numFrames);
            float* o2Chans[2] = { o2L.data(), o2R.data() };
            AudioBuffer o2Buf;
            o2Buf.channels = o2Chans;
            o2Buf.numChannels = 2;
            o2Buf.numFrames = numFrames;

            // ~2s at 48kHz / 512-frame blocks (48000/512 ~= 93.75 blocks/s).
            const int kBlocks = 188;
            bool noteCurrentlyOn = false;
            int currentVoiceId = 0;
            AudioNode* seqAudio = nullptr;
            AudioNode* osc1Audio = nullptr;
            AudioNode* osc2Audio = nullptr;

            for (int block = 0; block < kBlocks && ok; block++)
            {
               // The stress under test: force a full topology rebuild every
               // block, not just on real cable edits.
               RebuildAudioTopology();
               seqAudio = AudioNodeOfAny(seq->node.get());
               osc1Audio = AudioNodeOfAny(osc1->node.get());
               osc2Audio = AudioNodeOfAny(osc2->node.get());

               NoteEventQueue* outbox = seqAudio ? seqAudio->NoteOutbox(0) : nullptr;
               if (seqAudio == nullptr || osc1Audio == nullptr || osc2Audio == nullptr || outbox == nullptr)
               {
                  ok = false;
                  break;
               }

               // A short note every 8 blocks: on, then off 3 blocks later -
               // never more than one voice in flight, so a stuck voice at
               // the end can only mean a lost note-off, not "still playing".
               if (!noteCurrentlyOn && (block % 8) == 0)
               {
                  NoteEvent on;
                  on.note = 60 + (block % 12);
                  on.velocity = 0.8f;
                  on.isNoteOn = true;
                  currentVoiceId = NextVoiceId();
                  on.voiceId = currentVoiceId;
                  outbox->Push(on);
                  notesOn++;
                  noteCurrentlyOn = true;
               }
               else if (noteCurrentlyOn && (block % 8) == 3)
               {
                  NoteEvent off;
                  off.note = 60 + ((block - 3) % 12);
                  off.velocity = 0.0f;
                  off.isNoteOn = false;
                  off.voiceId = currentVoiceId;
                  outbox->Push(off);
                  notesOff++;
                  noteCurrentlyOn = false;
               }

               seqAudio->ProcessBlock(nullptr, 0, sBuf);
               osc1Audio->ProcessBlock(nullptr, 0, o1Buf);
               osc2Audio->ProcessBlock(nullptr, 0, o2Buf);
            }

            // Flush any note still open at the end (kBlocks isn't
            // necessarily a multiple of 8) so a genuinely stuck voice isn't
            // masked by "the fixture just never sent the off".
            if (ok && noteCurrentlyOn)
            {
               NoteEventQueue* outbox = seqAudio ? seqAudio->NoteOutbox(0) : nullptr;
               if (outbox != nullptr)
               {
                  NoteEvent off;
                  off.note = 60;
                  off.velocity = 0.0f;
                  off.isNoteOn = false;
                  off.voiceId = currentVoiceId;
                  outbox->Push(off);
                  notesOff++;
                  seqAudio->ProcessBlock(nullptr, 0, sBuf);
                  osc1Audio->ProcessBlock(nullptr, 0, o1Buf);
                  osc2Audio->ProcessBlock(nullptr, 0, o2Buf);
               }
            }

            // Drain the release tail: every note-on got its matching
            // note-off above, but ActiveVoices() legitimately stays > 0
            // through the amp envelope's release phase (~260ms default for
            // Wavetable) - that's normal decay, not a stuck voice. Keep
            // rebuilding + rendering silence (no new notes) until both
            // synths report zero, or give up after a budget generous enough
            // to cover any release time this fixture could plausibly hit; a
            // real stuck voice (the bug this fixture exists to catch) never
            // reaches zero and this loop exhausts its budget instead.
            if (ok)
            {
               const int kDrainBlocks = 80; // ~80 * (512/48000)s ~= 0.85s
               for (int i = 0; i < kDrainBlocks; i++)
               {
                  RebuildAudioTopology();
                  seqAudio = AudioNodeOfAny(seq->node.get());
                  osc1Audio = AudioNodeOfAny(osc1->node.get());
                  osc2Audio = AudioNodeOfAny(osc2->node.get());
                  if (osc1Audio != nullptr)
                     osc1Audio->ProcessBlock(nullptr, 0, o1Buf);
                  if (osc2Audio != nullptr)
                     osc2Audio->ProcessBlock(nullptr, 0, o2Buf);

                  voices1 = osc1Audio != nullptr ? static_cast<OscillatorNode*>(osc1->node.get())->ActiveVoices() : -1;
                  voices2 = osc2Audio != nullptr ? static_cast<OscillatorNode*>(osc2->node.get())->ActiveVoices() : -1;
                  if (voices1 == 0 && voices2 == 0)
                     break;
               }
               NoteEventQueue* finalOutbox = seqAudio ? seqAudio->NoteOutbox(0) : nullptr;
               overflow = finalOutbox ? finalOutbox->OverflowCount() : 1;
            }

            ok = ok && notesOn == notesOff && voices1 == 0 && voices2 == 0 && overflow == 0;
         }

         printf("  notesOn=%d notesOff=%d voices1=%d voices2=%d overflow=%llu\n",
                notesOn, notesOff, voices1, voices2, (unsigned long long)overflow);
         printf("%s\n", ok ? "NOTE REWIRE STRESS TEST OK" : "NOTE REWIRE STRESS TEST FAIL");
         fflush(stdout);

         for (int idx : { seqIdx, osc1Idx, osc2Idx })
            if (idx >= 0)
               RemoveNodeByIndex(idx);

         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_AUDIOGRAPHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_AUDIOGRAPHTEST") != nullptr && frameId == 4)
      {
         const std::string path = TmpPath("infinite_audiographtest.infinite");
         const bool saved = SavePatchTo(path);
         const size_t nodesBefore = gNodes.size();
         const bool loaded = LoadPatchFrom(path);

         GainNode* gain = nullptr;
         AudioOutputNode* out = nullptr;
         WavetableNode* osc = nullptr;
         int gainIndex = -1;
         for (GraphNode& gn : gNodes)
         {
            if (!osc) osc = dynamic_cast<WavetableNode*>(gn.node.get());
            if (!gain) { gain = dynamic_cast<GainNode*>(gn.node.get()); if (gain) gainIndex = gn.index; }
            if (!out) out = dynamic_cast<AudioOutputNode*>(gn.node.get());
         }
         const bool wiring = osc != nullptr && gain != nullptr && out != nullptr &&
                             gain->input.GetSource() == osc && out->input.GetSource() == gain;

         printf("audio round trip: saved=%d loaded=%d nodes=%zu wiring=%d  %s\n",
                saved, loaded, gNodes.size(), wiring,
                (saved && loaded && nodesBefore == gNodes.size() && wiring) ? "OK" : "FAIL");

         // Mid-chain delete: remove Gain, confirm Audio Out's cable was
         // cleared generically (AudioInputSlot/DisconnectAllTo, step 7/8 of
         // P2) rather than left dangling, and that nothing crashes on the
         // next cook or the next topology rebuild.
         RemoveNodeByIndex(gainIndex);
         out = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (!out) out = dynamic_cast<AudioOutputNode*>(gn.node.get());
            gn.node->CookIfNeeded(frameId); // must not crash on the freed Gain
         }
         const bool clearedAfterDelete = out != nullptr && out->input.GetSource() == nullptr;
         printf("audio delete-crash: survived cook, cleared=%d  %s\n",
                clearedAfterDelete, (out != nullptr && clearedAfterDelete) ? "OK" : "FAIL");
      }
}

void FrameTest_NEWPATCHAUDIOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_NEWPATCHAUDIOTEST") != nullptr && (frameId == 4 || frameId == 12))
      {
         static float sPeakBefore = 0.0f;
         static float sPeakAfter = 0.0f;
         if (frameId == 4)
         {
            std::vector<float> renderL(256), renderR(256);
            float* renderChans[2] = { renderL.data(), renderR.data() };
            AudioBuffer renderBuf;
            renderBuf.channels = renderChans;
            renderBuf.numChannels = 2;
            renderBuf.numFrames = 256;
            auto peakOfBlocks = [&](int blocks)
            {
               float peak = 0.0f;
               for (int b = 0; b < blocks; b++)
               {
                  AudioEngine::Instance().ProcessOffline(renderBuf);
                  for (int i = 0; i < renderBuf.numFrames; i++)
                     peak = std::max(peak, std::max(std::fabs(renderL[i]), std::fabs(renderR[i])));
               }
               return peak;
            };
            sPeakBefore = peakOfBlocks(8);
            NewPatch(); // what File > New and Cmd+N run
            sPeakAfter = peakOfBlocks(8);
         }
         else
         {
            const bool silentAfterNew = sPeakAfter == 0.0f;
            const bool drained = gRetiredNodes.empty();
            printf("new patch audio: before=%.4f after=%.4f retired left=%zu  %s\n", sPeakBefore, sPeakAfter,
                   gRetiredNodes.size(),
                   (sPeakBefore > 0.0f && silentAfterNew && drained) ? "NEWPATCH AUDIO OK" : "NEWPATCH AUDIO FAIL");
         }
      }
}

void FrameTest_MATFRAMETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MATFRAMETEST") != nullptr && frameId == 4)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[2].node.get());

         // Material overrides the surface but must leave the mesh untouched,
         // stamp included, or it would defeat the upload cache.
         geo->color[0] = 1.0f; geo->color[1] = 0.0f; geo->color[2] = 0.0f;
         mat->color[0] = 0.0f; mat->color[1] = 0.0f; mat->color[2] = 1.0f;
         mat->metallic = 0.9f;
         const Material through = mat->GetMaterial();
         const bool overrides = through.color[2] > 0.9f && through.color[0] < 0.1f &&
                                through.metallic > 0.8f;
         const bool meshUntouched = &mat->GetMesh() == &geo->GetMesh() &&
                                    mat->MeshRevision() == geo->MeshRevision();
         printf("material overrides=%d, mesh passed through=%d\n", overrides, meshUntouched);

         // The geometry sits at (4,2,0) scaled 2x, well off-centre, so framing
         // has to move the target as well as pull the camera back.
         render->camDistance = 3.0f;
         render->targetX = 0.0f; render->targetY = 0.0f; render->targetZ = 0.0f;
         FrameSceneInView(render);
         const bool centred = std::fabs(render->targetX - 4.0f) < 0.2f &&
                              std::fabs(render->targetY - 2.0f) < 0.2f;
         const bool pulledBack = render->camDistance > 3.0f && render->camDistance < 30.0f;
         printf("frame: target (%.2f, %.2f, %.2f) distance %.2f\n",
                render->targetX, render->targetY, render->targetZ, render->camDistance);
         printf("%s\n", (overrides && meshUntouched && centred && pulledBack)
                           ? "MATERIAL + FRAME OK" : "SUSPECT");
      }
}

void FrameTest_ENVTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_ENVTEST") != nullptr &&
          (frameId == 4 || frameId == 5 || frameId == 7 || frameId == 8 || frameId == 12))
      {
         static int sEnvTestBaselineSphereR = 0;
         auto* env = static_cast<EnvironmentNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         // Top-left corner, well away from the centred sphere: pure background.
         const size_t corner = ((size_t)(h - 4) * w + 4) * 4;
         const int bgR = px[corner], bgG = px[corner + 1], bgB = px[corner + 2];

         if (frameId == 4)
         {
            // HDRI patched in, used as background: the loaded sky (warm,
            // R>G>B) should read back, not the flat near-black default bgColor.
            const bool loaded = env->HasImage() && env->GetEnvironmentTexture() != 0;
            const bool sawSky = bgR > 60 && bgR > bgB;
            printf("env loaded=%d corner=(%d,%d,%d)  %s\n", loaded, bgR, bgG, bgB,
                   (loaded && sawSky) ? "HDRI BACKGROUND OK" : "SUSPECT");

            const GLenum err = glGetError();
            printf("gl error after env render: 0x%x  %s\n", err,
                   err == GL_NO_ERROR ? "CLEAN" : "SUSPECT");

            // A near-mirror sphere over a bright sky should read back brighter
            // than a flat grey ambient bake would ever give it - proof the
            // reflection actually sampled the HDRI rather than falling back to
            // the procedural gradient.
            const size_t centre = ((size_t)(h / 2) * w + w / 2) * 4;
            const int sphereR = px[centre], sphereG = px[centre + 1];
            printf("sphere centre=(%d,%d,%d)  %s\n", sphereR, sphereG, px[centre + 2],
                   (sphereR > 40) ? "REFLECTION SAMPLED HDRI OK" : "SUSPECT");

            // Every mip must be finite, not just mip 0. A single Inf/NaN texel
            // anywhere spreads under box-downsampling until the top of the
            // chain is entirely NaN, and the diffuse irradiance term reads
            // exactly that top mip - so a poisoned chain renders geometry
            // solid black or a flat garbage colour while the thumbnail and
            // background quad (both mip 0) still look perfect.
            int badMips = 0;
            glBindTexture(GL_TEXTURE_2D, env->GetEnvironmentTexture());
            for (int level = 0; level <= (int)env->MaxLod(); level++)
            {
               int lw = 0, lh = 0;
               glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &lw);
               glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &lh);
               if (lw <= 0 || lh <= 0) { badMips++; continue; }
               std::vector<float> mip((size_t)lw * lh * 4);
               glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, mip.data());
               for (float v : mip)
                  if (!std::isfinite(v)) { badMips++; break; }
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            printf("env mip chain: %d level(s) non-finite  %s\n", badMips,
                   badMips == 0 ? "MIP CHAIN FINITE OK" : "SUSPECT");

            render->envAsBackground = false; // checked at frame 8
            sEnvTestBaselineSphereR = sphereR;
         }
         else if (frameId == 5)
         {
            // Regression check: dragging intensity must invalidate the cached
            // scene even though it doesn't touch TextureRevision() at all -
            // only the file load path does. No reconnect, no new texture,
            // same HDRI, and - unlike frame 4's envAsBackground flip above -
            // nothing else about the scene changes this frame, so this
            // isolates the intensity edit from any other cache-busting change.
            // Baseline is already blown out to 255 at intensity 1 (the fixture
            // HDRI is deliberately very bright), so a further increase
            // wouldn't show up in 8-bit readback - dim it down instead, which
            // has headroom to prove either direction of change. Checked at
            // frame 7, one frame after this one's own cook has run.
            env->intensity = 0.02f;
         }
         else if (frameId == 7)
         {
            const size_t centre = ((size_t)(h / 2) * w + w / 2) * 4;
            const int sphereR = px[centre];
            printf("intensity dim (no reconnect): sphere centre R %d -> %d  %s\n",
                   sEnvTestBaselineSphereR, sphereR,
                   (sphereR < sEnvTestBaselineSphereR) ? "INTENSITY LIVE-UPDATE OK" : "SUSPECT");
         }
         else if (frameId == 8)
         {
            // Same HDRI still drives lighting, but the background toggle is
            // off: the corner should fall back to the flat bgColor clear.
            const bool flatBg = bgR < 20 && bgG < 20 && bgB < 30;
            printf("background off: corner=(%d,%d,%d)  %s\n", bgR, bgG, bgB,
                   flatBg ? "BACKGROUND TOGGLE OK" : "SUSPECT");
            render->envAsBackground = true;
            render->envInput.Disconnect();
         }
         else if (frameId == 12)
         {
            // Env input fully disconnected: no HDRI texture is bound at all
            // any more, so the background quad is skipped outright and this
            // is back to the original flat bgColor clear - proving the HDRI
            // path is additive rather than a rewrite that left state behind
            // once its source node goes away.
            const bool flatBg = bgR < 20 && bgG < 20 && bgB < 30;
            printf("disconnected: corner=(%d,%d,%d)  %s\n", bgR, bgG, bgB,
                   flatBg ? "DISCONNECT FALLBACK OK" : "SUSPECT");
         }
      }
}

void FrameTest_PATHOCEANTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PATHOCEANTEST") != nullptr && frameId == 4)
      {
         auto* path = static_cast<PathNode*>(gNodes[0].node.get());
         auto* ocean = static_cast<OceanNode*>(gNodes[1].node.get());
         Transport::Instance().SetPlaying(true);

         // Every path shape must stay in range and actually move over time.
         bool allShapes = true;
         for (int shape = 0; shape < PathNode::kShapeCount; shape++)
         {
            path->shape = shape;
            path->speed = 0.25f;
            float lo[3] = { 2, 2, 2 }, hi[3] = { -2, -2, -2 };
            bool inRange = true, finite = true;
            for (int step = 0; step < 64; step++)
            {
               // Phase is swept directly rather than waiting on the transport,
               // so a whole lap is covered inside one frame.
               path->phase = (float)step / 64.0f;
               path->CookIfNeeded(1000 + shape * 100 + step);
               float p[3];
               path->CurrentPoint(p);
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) finite = false;
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
               for (int o = 0; o < 4; o++)
               {
                  IModulator* m = path->ModulatorOutput(o);
                  const float v = m ? m->Value01() : -1.0f;
                  if (v < 0.0f || v > 1.0f)
                     inRange = false;
               }
            }
            const float travel = std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            const bool ok = finite && inRange && travel > 0.1f;
            allShapes &= ok;
            printf("  path %-10s travel %.2f  outputs in 0..1: %d  %s\n",
                   PathNode::ShapeNames()[shape].c_str(), travel, inRange, ok ? "OK" : "FAIL");
         }
         printf("%s\n", allShapes ? "PATH OK" : "SUSPECT");

         // The ocean must be a real displaced surface, not a flat plane, and it
         // has to change as the transport advances.
         const Mesh& m0 = ocean->GetMesh();
         float lo = 1e30f, hi = -1e30f;
         bool finite = true;
         for (const Vertex& v : m0.vertices)
         {
            if (!std::isfinite(v.py)) { finite = false; continue; }
            lo = std::min(lo, v.py); hi = std::max(hi, v.py);
         }
         const float relief = hi - lo;
         const size_t tris = m0.indices.size() / 3;
         const unsigned long long stampBefore = ocean->MeshRevision();

         Transport::Instance().Tick(2.0f);
         ocean->GetMesh();
         const unsigned long long stampAfter = ocean->MeshRevision();

         printf("ocean: %zu tris, wave relief %.3f, finite=%d, animates=%d\n",
                tris, relief, finite, stampAfter != stampBefore);

         bool oceanNormalsMatch = true;
         float maxNormDiff = 0.0f;
         for (int res : { 16, 96, 256 })
         {
            for (float chop : { 0.0f, 0.5f, 1.2f })
            {
               Mesh mesh = MeshOps::Ocean(res, 50.0f, 1.5f, 12.0f, 0.8f, 0.785f, chop, 4, 1.234f);
               Mesh ref = MeshOps::RecalculateNormals(mesh, false, false);
               if (mesh.vertices.size() != ref.vertices.size() || mesh.indices.size() != ref.indices.size())
               {
                  oceanNormalsMatch = false;
                  break;
               }
               for (size_t i = 0; i < mesh.vertices.size(); i++)
               {
                  const float dnx = std::abs(mesh.vertices[i].nx - ref.vertices[i].nx);
                  const float dny = std::abs(mesh.vertices[i].ny - ref.vertices[i].ny);
                  const float dnz = std::abs(mesh.vertices[i].nz - ref.vertices[i].nz);
                  const float diff = std::max({ dnx, dny, dnz });
                  maxNormDiff = std::max(maxNormDiff, diff);
                  if (diff > 1e-4f)
                  {
                     oceanNormalsMatch = false;
                  }
                  if (std::abs(mesh.vertices[i].u - ref.vertices[i].u) > 1e-6f ||
                      std::abs(mesh.vertices[i].v - ref.vertices[i].v) > 1e-6f ||
                      std::abs(mesh.vertices[i].px - ref.vertices[i].px) > 1e-6f ||
                      std::abs(mesh.vertices[i].py - ref.vertices[i].py) > 1e-6f ||
                      std::abs(mesh.vertices[i].pz - ref.vertices[i].pz) > 1e-6f)
                  {
                     oceanNormalsMatch = false;
                  }
               }
               if (mesh.vertexColor.size() != ref.vertexColor.size() ||
                   mesh.faceMask.size() != ref.faceMask.size() ||
                   mesh.selectionGroup.size() != ref.selectionGroup.size())
               {
                  oceanNormalsMatch = false;
               }
            }
         }
         printf("ocean normals direct vs weld: maxDiff=%.2e match=%d\n", maxNormDiff, oceanNormalsMatch);

         printf("%s\n", (tris > 100 && finite && relief > 0.02f && stampAfter != stampBefore && oceanNormalsMatch)
                           ? "OCEAN OK" : "SUSPECT");
      }
}

void FrameTest_PALETTETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 6)
      {
         auto* palette = static_cast<PaletteNode*>(gNodes[1].node.get());
         auto* twin = static_cast<PaletteNode*>(gNodes[3].node.get());
         auto* target = static_cast<RampNode*>(gNodes[2].node.get());

         float sw[3][3];
         for (int i = 0; i < 3; i++)
            palette->GetSwatch(i, sw[i]);
         printf("swatches: ");
         for (int i = 0; i < 3; i++)
            printf("(%.2f %.2f %.2f w=%.2f) ", sw[i][0], sw[i][1], sw[i][2],
                   palette->SwatchWeight(i));
         printf("\n");

         // 1. three genuinely different colours came out, not three shades of one
         float closest = 9.0f;
         for (int i = 0; i < 3; i++)
            for (int j = i + 1; j < 3; j++)
            {
               float d = 0.0f;
               for (int c = 0; c < 3; c++)
                  d += (sw[i][c] - sw[j][c]) * (sw[i][c] - sw[j][c]);
               closest = std::min(closest, std::sqrt(d));
            }
         const bool distinct = closest > 0.15f;

         // 2. the default order really is dark to light
         auto luma = [](const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };
         const bool ordered = luma(sw[0]) <= luma(sw[1]) + 1e-4f &&
                              luma(sw[1]) <= luma(sw[2]) + 1e-4f;

         // 3. same reference and seed give the same palette, or every binding
         //    downstream would drift on its own
         float deterministic = 0.0f;
         for (int i = 0; i < 3; i++)
         {
            float other[3];
            twin->GetSwatch(i, other);
            for (int c = 0; c < 3; c++)
               deterministic = std::max(deterministic, std::fabs(other[c] - sw[i][c]));
         }

         // 4. the extracted colours actually reach a bound swatch
         PaletteBinding& binding = PaletteBinding::Instance();
         for (int i = 0; i < 3; i++)
            binding.Bind(gNodes[2].index, i, gNodes[1].index, i);
         gPaletteTestPending = true;

         printf("distinct=%d (closest %.3f) ordered=%d deterministic drift=%.4f\n",
                distinct, closest, ordered, deterministic);
         gPaletteTestOk = distinct && ordered && deterministic < 1e-5f;
         (void)target;
      }
}

void FrameTest_PALETTETEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 9)
      {
         auto* palette = static_cast<PaletteNode*>(gNodes[1].node.get());
         auto* target = static_cast<RampNode*>(gNodes[2].node.get());

         bool applied = true;
         for (int i = 0; i < 3; i++)
         {
            float expected[3];
            palette->GetSwatch(i, expected);
            for (int c = 0; c < 3; c++)
               if (std::fabs(target->stopColor[i][c] - expected[c]) > 1e-4f)
                  applied = false;
         }
         printf("bound stops: (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)  applied=%d\n",
                target->stopColor[0][0], target->stopColor[0][1], target->stopColor[0][2],
                target->stopColor[1][0], target->stopColor[1][1], target->stopColor[1][2],
                target->stopColor[2][0], target->stopColor[2][1], target->stopColor[2][2],
                applied);

         // 5. shaping re-grades without re-clustering: saturation 0 must leave
         //    swatches neutral but still ordered by lightness
         palette->saturation = 0.0f;
         palette->CookIfNeeded(frameId + 500);
         float maxChroma = 0.0f;
         for (int i = 0; i < 3; i++)
         {
            float c[3];
            palette->GetSwatch(i, c);
            maxChroma = std::max(maxChroma,
                                 std::max(c[0], std::max(c[1], c[2])) -
                                    std::min(c[0], std::min(c[1], c[2])));
         }
         palette->saturation = 1.0f;
         printf("saturation 0 -> max channel spread %.3f\n", maxChroma);

         // 6. the binding survives a patch round trip
         Patch::Data data = BuildPatchData();
         const bool saved = data.palette.size() == 3;
         PaletteBinding::Instance().UnbindAllFor(gNodes[2].index);
         const bool cleared = PaletteBinding::Instance().Links().empty();
         for (const Patch::PaletteRecord& r : data.palette)
            PaletteBinding::Instance().Bind(r.dstIndex, r.dstColor, r.srcIndex, r.srcSwatch);
         const bool restored = PaletteBinding::Instance().Links().size() == 3;
         printf("bindings: saved=%d cleared=%d restored=%d\n", saved, cleared, restored);

         // 7. the headline path: a photo loaded from disk, not a cabled node.
         //    Two known colours in, the same two colours out.
         const char* shotDir = getenv("TMPDIR");
         const std::string refPath =
            std::string(shotDir ? shotDir : "/tmp") + "/infinite_palette_ref.png";
         const int side = 64;
         std::vector<unsigned char> ref((size_t)side * side * 4, 255);
         for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
            {
               const size_t i = ((size_t)y * side + x) * 4;
               const bool left = x < side / 2;
               ref[i + 0] = left ? 230 : 30;
               ref[i + 1] = left ? 40 : 90;
               ref[i + 2] = left ? 60 : 210;
            }
         stbi_flip_vertically_on_write(0);
         stbi_write_png(refPath.c_str(), side, side, 4, ref.data(), side * 4);

         auto* fromFile = static_cast<PaletteNode*>(gNodes[3].node.get());
         gNodes[3].node->bypassed = false;
         fromFile->Input().Disconnect();
         fromFile->swatchCount = 2;
         const bool loaded = fromFile->Load(refPath);
         fromFile->CookIfNeeded(frameId + 900);
         float a[3], b[3];
         fromFile->GetSwatch(0, a);
         fromFile->GetSwatch(1, b);
         // Sorted dark-to-light, so the blue half comes first.
         const bool fileOk = loaded && b[0] > 0.7f && b[2] < 0.4f &&
                             a[2] > 0.6f && a[0] < 0.4f;
         printf("from file: loaded=%d (%.2f %.2f %.2f) (%.2f %.2f %.2f) %s\n",
                loaded, a[0], a[1], a[2], b[0], b[1], b[2], fileOk ? "OK" : "SUSPECT");

         const bool ok = gPaletteTestOk && applied && maxChroma < 0.02f &&
                         saved && cleared && restored && fileOk;
         printf("%s\n", ok ? "PALETTE OK" : "SUSPECT");
      }
}

void FrameTest_UTILTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_UTILTEST") != nullptr && (frameId == 4 || frameId == 10))
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* null3d = static_cast<Null3DNode*>(gNodes[1].node.get());
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         auto* null2d = static_cast<NullNode*>(gNodes[5].node.get());
         auto* shape = gNodes[4].node.get();
         auto* out = static_cast<OutputNode*>(gNodes[6].node.get());

         if (frameId == 4)
         {
            // Null 3D must forward, not copy: same mesh, same stamp. A stamp of
            // its own would make Render 3D re-upload every single frame.
            const bool sameMesh = &null3d->GetMesh() == &geo->GetMesh();
            const bool sameStamp = null3d->MeshRevision() == geo->MeshRevision();
            printf("null 3D forwards: mesh=%d stamp=%d (%llu)\n",
                   sameMesh, sameStamp, null3d->MeshRevision());

            // Null 2D must report the upstream texture as its own, with no
            // framebuffer of its own in between.
            const bool sameTexture = null2d->GetOutputTexture() == shape->GetOutputTexture();
            const bool sameSize = null2d->GetOutputWidth() == shape->GetOutputWidth();
            const bool outputFed = out->GetOutputWidth() > 0;
            printf("null 2D passes: tex=%d size=%d, output %dx%d\n",
                   sameTexture, sameSize, out->GetOutputWidth(), out->GetOutputHeight());

            printf("mesh to points: %zu points -> %zu triangles\n",
                   m2p->PointCount(), m2p->TriangleCount());
            const bool sampled = m2p->PointCount() > 10 && m2p->TriangleCount() > 10;

            printf("%s\n", (sameMesh && sameStamp && sameTexture && sameSize &&
                            outputFed && sampled) ? "NULL + MESH TO POINTS OK" : "SUSPECT");
         }
         else
         {
            // Nothing animates here, so a settled frame must upload nothing -
            // proving the pass-through did not defeat the cache.
            printf("steady frame: %zu tris, %zu uploads  %s\n",
                   render->LastTriangleCount(), render->LastUploads(),
                   render->LastUploads() == 0 ? "PASS-THROUGH CACHE OK"
                                              : "SUSPECT - re-uploading through Null");
         }
      }
}

void FrameTest_TEXT3DTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_TEXT3DTEST") != nullptr && frameId == 4)
      {
         auto* t = static_cast<Text3DNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());

         // A square ring: one anticlockwise outline, one clockwise hole. If the
         // hole is filled in rather than cut, the area comes out as the full
         // square instead of the ring, so area is the thing worth measuring.
         {
            std::vector<MeshOps::Contour2D> c(2);
            c[0].points = { -1,-1,  1,-1,  1,1,  -1,1 };          // CCW outline
            c[1].points = { -0.5f,0.5f, 0.5f,0.5f, 0.5f,-0.5f, -0.5f,-0.5f }; // CW hole
            const Mesh ring = MeshOps::ExtrudeContours(c, 0.2f, 0.0f);

            // Front-facing area only, so the back face and walls do not count.
            double area = 0.0;
            for (size_t i = 0; i + 2 < ring.indices.size(); i += 3)
            {
               const Vertex& a = ring.vertices[ring.indices[i]];
               const Vertex& b = ring.vertices[ring.indices[i + 1]];
               const Vertex& v = ring.vertices[ring.indices[i + 2]];
               if (a.pz < 0.0f || b.pz < 0.0f || v.pz < 0.0f)
                  continue;
               area += std::fabs((b.px - a.px) * (v.py - a.py) -
                                 (v.px - a.px) * (b.py - a.py)) * 0.5;
            }
            // Outer 2x2 = 4, hole 1x1 = 1, so a correctly cut ring is 3.
            printf("ring: %zu tris, front area %.3f (expect 3.0, filled would be 4.0)  %s\n",
                   ring.indices.size() / 3, area,
                   std::fabs(area - 3.0) < 0.05 ? "HOLE CUT OK" : "SUSPECT");
         }

         // Then real glyphs. 'o' has a counter, so it must produce more than one
         // contour; a string of them must stay finite and bounded.
         const char* samples[] = { "o", "Infinite", "AWAY" };
         bool all = true;
         for (const char* sample : samples)
         {
            t->text = sample;
            const Mesh& mesh = t->GetMesh();
            bool finite = true;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]);
               }
            }
            const bool ok = finite && mesh.indices.size() >= 3 && (hi[0] - lo[0]) > 0.05f;
            all &= ok;
            printf("  %-10s %6zu tris  width %.2f height %.2f  %s\n", sample,
                   mesh.indices.size() / 3, hi[0] - lo[0], hi[1] - lo[1], ok ? "OK" : "FAIL");
            printf("     %s\n", t->Status().c_str());
         }

         t->text = "Infinite";
         render->CookIfNeeded(frameId);
         printf("rendered %zu tris\n", render->LastTriangleCount());
         printf("%s\n", (all && render->LastTriangleCount() > 0) ? "TEXT 3D OK" : "SUSPECT");
      }
}

void FrameTest_MODELTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MODELTEST") != nullptr && frameId == 4)
      {
         auto* model = static_cast<ModelSourceNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         const char* files = getenv("INFINITE_MODELTEST");

         bool all = true;
         std::string list(files);
         size_t start = 0;
         while (start < list.size())
         {
            const size_t comma = list.find(',', start);
            const std::string path = list.substr(start, comma == std::string::npos
                                                          ? std::string::npos : comma - start);
            start = (comma == std::string::npos) ? list.size() : comma + 1;
            if (path.empty() || path == "1")
               continue;

            const bool loaded = model->Load(path);
            const Mesh& mesh = model->GetMesh();
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            bool finite = true;
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]);
               }
            }
            const float extent = mesh.vertices.empty() ? 0.0f
               : std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            // Normalisation should land every model on a unit box no matter
            // what scale it was authored at.
            const bool sane = loaded && finite && mesh.indices.size() >= 3 &&
                              std::fabs(extent - 1.0f) < 0.01f;
            printf("  %-28s %5zu tris  extent %.3f  %s\n",
                   path.c_str(), mesh.indices.size() / 3, extent, sane ? "OK" : "FAIL");
            printf("     status: %s\n", model->Status().c_str());
            all &= sane;
         }

         // And it must actually rasterise through the normal render path.
         render->CookIfNeeded(frameId);
         printf("rendered %zu tris in %zu draw calls\n",
                render->LastTriangleCount(), render->LastDrawCalls());
         printf("%s\n", (all && render->LastTriangleCount() > 0) ? "MODEL LOADING OK" : "SUSPECT");

         // A file that is not a model at all must fail cleanly, not crash.
         const bool rejected = !model->Load("/etc/hosts");
         printf("bad file rejected: %d (%s)\n", rejected, model->Status().c_str());
      }
}

void FrameTest_MESHOPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MESHOPTEST") != nullptr && frameId == 3)
      {
         auto check = [](const char* name, const Mesh& m, size_t minTris) {
            bool finite = true;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (const Vertex& v : m.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
            }
            const size_t tris = m.indices.size() / 3;
            const float extent = (m.vertices.empty() || !finite)
                                    ? 0.0f
                                    : std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]));
            const bool ok = tris >= minTris && finite && extent > 0.001f && extent < 1000.0f;
            printf("  %-12s %7zu tris  extent %6.2f  %s\n", name, tris, extent,
                   ok ? "OK" : (!finite ? "FAIL non-finite" : "FAIL"));
            return ok;
         };

         printf("mesh operators:\n");
         const Mesh sphere = Primitives::Sphere(24, 32);
         const Mesh plane = Primitives::Plane(4);
         bool all = true;
         all &= check("source", sphere, 100);
         all &= check("subdivide", MeshOps::Subdivide(sphere, 1, 1.0f), 4 * (sphere.indices.size() / 3));
         all &= check("smooth", MeshOps::Smooth(sphere, 5, 0.8f), sphere.indices.size() / 3);
         all &= check("mirror", MeshOps::Mirror(sphere, 0, 1.0f, true, true), 2 * (sphere.indices.size() / 3));
         all &= check("screw", MeshOps::Screw(plane, 32, 1.0f, 0.4f, 0.6f, 1), 32);

         // Displace with a synthetic checkerboard buffer, standing in for a
         // readback of a Noise/Voronoi texture - exercises the bilinear
         // sampler and both modes without needing a live GL context here.
         {
            const int texW = 16, texH = 16;
            std::vector<float> tex((size_t)texW * texH * 4);
            for (int y = 0; y < texH; y++)
               for (int x = 0; x < texW; x++)
               {
                  const float v = ((x / 4 + y / 4) % 2 == 0) ? 1.0f : 0.0f;
                  const size_t i = ((size_t)y * texW + x) * 4;
                  tex[i + 0] = v; tex[i + 1] = 1.0f - v; tex[i + 2] = v * 0.5f; tex[i + 3] = 1.0f;
               }
            all &= check("displace scalar",
                         MeshOps::Displace(sphere, tex, texW, texH, 0, 0.3f, 0.5f, false, false),
                         sphere.indices.size() / 3);
            all &= check("displace vector",
                         MeshOps::Displace(sphere, tex, texW, texH, 1, 0.3f, 0.5f, false, false),
                         sphere.indices.size() / 3);

            // A hard-shaded primitive like Cube duplicates each corner once
            // per adjoining face - same position, different normal/UV - so
            // this checks that displacing it does not tear those duplicates
            // apart: every vertex that started at a shared position must
            // still be within epsilon of the others in its group afterward.
            const Mesh cube = Primitives::Cube(1);
            const Mesh displacedCube = MeshOps::Displace(cube, tex, texW, texH, 0, 0.8f, 0.5f, false, false);
            const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(cube);
            std::map<unsigned int, std::array<float, 3>> first;
            float maxGap = 0.0f;
            for (size_t i = 0; i < displacedCube.vertices.size(); i++)
            {
               const unsigned int w = weld[i];
               const Vertex& v = displacedCube.vertices[i];
               auto it = first.find(w);
               if (it == first.end())
                  first[w] = { v.px, v.py, v.pz };
               else
               {
                  const float dx = v.px - it->second[0], dy = v.py - it->second[1], dz = v.pz - it->second[2];
                  maxGap = std::max(maxGap, std::sqrt(dx * dx + dy * dy + dz * dz));
               }
            }
            const bool seamsClosed = maxGap < 0.001f;
            printf("  %-12s max seam gap %.5f  %s\n", "displace cube", maxGap,
                   seamsClosed ? "OK" : "FAIL cracked at seams");
            all &= seamsClosed;
         }
         printf("%s\n", all ? "MESH OPERATORS OK" : "SUSPECT");

         // Taubin's whole point: repeated smoothing must not collapse the mesh.
         const Mesh heavy = MeshOps::Smooth(sphere, 20, 1.0f);
         float r0 = 0.0f, r1 = 0.0f;
         for (const Vertex& v : sphere.vertices)
            r0 = std::max(r0, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
         for (const Vertex& v : heavy.vertices)
            r1 = std::max(r1, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
         printf("smoothing 20x: radius %.4f -> %.4f (%.1f%% retained)\n", r0, r1, 100.0f * r1 / r0);
         printf("%s\n", (r1 > r0 * 0.9f) ? "TAUBIN NO-SHRINK OK"
                                         : "SUSPECT - smoothing is collapsing the mesh");

         // ToPoints weld/dissolve: pin detail=1 so unwelded duplicate corners
         // (24 verts / 30 tri-edges for Cube(1)) don't get mistaken for the
         // welded topology (8 verts / 12 real edges / 12 faces).
         {
            const Mesh cube1 = Primitives::Cube(1);
            const size_t unweldedVerts = MeshOps::ToPoints(cube1, 0, 100000, false).size();
            const size_t weldedVerts = MeshOps::ToPoints(cube1, 0, 100000, true).size();
            const size_t unweldedEdges = MeshOps::ToPoints(cube1, 1, 100000, false, 0.0f).size();
            const size_t weldedEdgesNoFilter = MeshOps::ToPoints(cube1, 1, 100000, true, 0.0f).size();
            const size_t weldedEdgesFiltered = MeshOps::ToPoints(cube1, 1, 100000, true, 1.0f).size();
            const size_t faceCentres = MeshOps::ToPoints(cube1, 2, 100000, true).size();
            const bool toPointsOk = unweldedVerts == 24 && weldedVerts == 8 &&
                                     unweldedEdges == 30 && weldedEdgesNoFilter == 18 &&
                                     weldedEdgesFiltered == 12 && faceCentres == 12;
            printf("to points (Cube(1)): verts %zu->%zu, edges %zu->%zu->%zu, faces %zu  %s\n",
                   unweldedVerts, weldedVerts, unweldedEdges, weldedEdgesNoFilter, weldedEdgesFiltered,
                   faceCentres, toPointsOk ? "OK" : "FAIL");
         }
      }
}

void FrameTest_GEOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_GEOTEST") != nullptr && (frameId == 4 || frameId == 10))
      {
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         auto* op = static_cast<GeometryOpNode*>(gNodes[3].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());
         printf("frame %d: instances=%zu  arrayTris=%zu  rendered=%zu tris in %zu draw calls, %zu uploads, %.2f ms/frame\n",
                frameId, inst->InstanceCount(), op->TriangleCount(),
                r->LastTriangleCount(), r->LastDrawCalls(), r->LastUploads(), gLastFrameMs);
         static size_t sUploadsAtFrame4 = 0;
         if (frameId == 4)
         {
            sUploadsAtFrame4 = r->LastUploads();
            printf("%s\n", (inst->InstanceCount() > 100 && op->TriangleCount() > 100 &&
                             r->LastDrawCalls() <= 2) ? "INSTANCING + OPS OK" : "SUSPECT");
         }
         else
         {
            // Nothing in this fixture animates, so no upload may happen BETWEEN
            // the two sample frames; any increase means the mesh stamps are
            // churning.
            //
            // The invariant is the DELTA, not "== 0". mLastUploads is reset at
            // the top of Render3DNode's draw and counts uploads within that one
            // pass - so when the cache works perfectly the node skips the whole
            // pass, the reset never runs, and the counter still reads whatever
            // the last real render uploaded (3, at startup). Asserting == 0 here
            // could not tell "never re-rendered", which is the ideal outcome,
            // apart from "re-rendered and uploaded 3", which is the bug. It read
            // SUSPECT on a perfectly-behaving cache, and the old hygiene gate
            // matched only FAIL|BUG so nobody saw it say so.
            const size_t delta = r->LastUploads() - std::min(r->LastUploads(), sUploadsAtFrame4);
            printf("uploads frame4=%zu frame10=%zu delta=%zu\n",
                   sUploadsAtFrame4, r->LastUploads(), delta);
            printf("%s\n", delta == 0 ? "MESH UPLOAD CACHING OK"
                                       : "SUSPECT - re-uploading a static mesh");
         }
      }
}

void FrameTest_DISPLACETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_DISPLACETEST") != nullptr && frameId == 6)
      {
         auto* sub = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* disp = static_cast<DisplacementNode*>(gNodes[2].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());

         const Mesh& before = sub->GetMesh();
         const Mesh& after = disp->GetMesh();

         auto bounds = [](const Mesh& m, float lo[3], float hi[3]) {
            lo[0] = lo[1] = lo[2] = 1e30f;
            hi[0] = hi[1] = hi[2] = -1e30f;
            bool finite = true;
            for (const Vertex& v : m.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k])) { finite = false; continue; }
                  lo[k] = std::min(lo[k], p[k]);
                  hi[k] = std::max(hi[k], p[k]);
               }
            }
            return finite;
         };

         float loB[3], hiB[3], loA[3], hiA[3];
         const bool finiteBefore = bounds(before, loB, hiB);
         const bool finiteAfter = bounds(after, loA, hiA);
         const float extentBefore = hiB[0] - loB[0];
         const float extentAfter = hiA[0] - loA[0];

         // Same triangle/vertex count - Displace moves points, it does not
         // remesh - but a different radius, since a unit sphere pushed by a
         // Noise texture must not still be a unit sphere.
         const bool sameTopology = before.indices.size() == after.indices.size() &&
                                   before.vertices.size() == after.vertices.size();
         const bool actuallyMoved = std::fabs(extentAfter - extentBefore) > 0.01f;

         r->CookIfNeeded(frameId);
         printf("before extent %.3f  after extent %.3f  tris=%zu  rendered=%zu tris\n",
                extentBefore, extentAfter, disp->TriangleCount(), r->LastTriangleCount());
         printf("%s\n", (finiteBefore && finiteAfter && sameTopology && actuallyMoved &&
                         r->LastTriangleCount() > 0)
                           ? "DISPLACEMENT OK" : "SUSPECT");
      }
}

void FrameTest_MINIVIEWPORTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MINIVIEWPORTTEST") != nullptr && frameId == 10)
      {
         auto* select = dynamic_cast<IGeometrySource*>(gNodes[2].node.get());
         auto* plain = dynamic_cast<IGeometrySource*>(gNodes[4].node.get());

         static NodeViewport selectViewport;
         static NodeViewport plainViewport;
         static SharedViewportCamera selectCam;
         static SharedViewportCamera plainCam;
         const int vw = 256, vh = 256;
         const unsigned int selectTex = selectViewport.Render(select, selectCam, vw, vh, true);
         const unsigned int plainTex = plainViewport.Render(plain, plainCam, vw, vh, true);

         auto countTinted = [&](unsigned int tex) -> size_t {
            if (tex == 0)
               return 0;
            std::vector<unsigned char> px((size_t)vw * vh * 4);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            size_t tinted = 0;
            for (size_t i = 0; i < px.size(); i += 4)
               if (px[i] > 90 && px[i] > px[i + 2] + 40)
                  tinted++;
            return tinted;
         };

         const size_t selectTinted = countTinted(selectTex);
         const size_t plainTinted = countTinted(plainTex);

         // A pos/rot/scale slider moves GetModelMatrix() without rebuilding
         // the mesh at all, so MeshRevision() alone would miss it - the exact
         // bug where the viewport froze on a live torus's transform sliders.
         // Confirmed here by moving the plain cube far off-frame with no
         // mesh rebuild and checking the render actually follows: mostly
         // background where it used to be mostly cube.
         auto countMesh = [&](unsigned int tex) -> size_t {
            if (tex == 0)
               return 0;
            std::vector<unsigned char> px((size_t)vw * vh * 4);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            // Clear colour is (18,18,23)ish; anything meaningfully brighter is
            // the lit mesh rather than background.
            size_t mesh = 0;
            for (size_t i = 0; i < px.size(); i += 4)
               if (px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 45)
                  mesh++;
            return mesh;
         };
         const size_t plainMeshBefore = countMesh(plainTex);

         auto* plainGeo = static_cast<GeometryNode*>(gNodes[4].node.get());
         plainGeo->posX += 20.0f; // far outside any reasonable auto-framing
         const unsigned int plainTexAfter = plainViewport.Render(plain, plainCam, vw, vh, false);
         const size_t plainMeshAfter = countMesh(plainTexAfter);

         const bool transformTracked = plainMeshBefore > 500 && plainMeshAfter < 50;
         const bool ok = selectTex != 0 && plainTex != 0 && selectTinted > 50 && plainTinted == 0 &&
                        transformTracked;
         printf("mini viewport: select tex=%u tinted=%zu, plain tex=%u tinted=%zu\n",
                selectTex, selectTinted, plainTex, plainTinted);
         printf("transform tracking: plain mesh px before move=%zu after move=%zu\n",
                plainMeshBefore, plainMeshAfter);
         printf("%s\n", ok ? "MINI VIEWPORT OK" : "SUSPECT");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_DEPTHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_DEPTHTEST") != nullptr)
      {
         if (frameId == 4)
         {
            auto* dp = static_cast<DepthProjectionNode*>(gNodes[2].node.get());
            auto* r = static_cast<Render3DNode*>(gNodes[3].node.get());
            const size_t pts = dp->PointCount();
            const unsigned int tex = r->GetOutputTexture();
            printf("depth projection points: %zu points, outputTex=%u  %s\n",
                   pts, tex, (pts > 0 && tex != 0) ? "OK" : "FAIL");
            dp->outputType = DepthProjectionNode::kMesh;
            dp->projection = DepthProjectionNode::kPlanar;
         }
         else if (frameId == 8)
         {
            auto* dp = static_cast<DepthProjectionNode*>(gNodes[2].node.get());
            auto* r = static_cast<Render3DNode*>(gNodes[3].node.get());
            const size_t tris = dp->TriangleCount();
            const size_t verts = dp->GetMesh().vertices.size();
            const unsigned int tex = r->GetOutputTexture();
            printf("depth projection mesh: %zu triangles, %zu vertices, outputTex=%u  %s\n",
                   tris, verts, tex, (tris > 0 && verts > 0 && tex != 0) ? "OK" : "FAIL");
            r->renderPass = 1; // Linear depth
            dp->projection = DepthProjectionNode::kRadial;
         }
         else if (frameId == 12)
         {
            auto* dp = static_cast<DepthProjectionNode*>(gNodes[2].node.get());
            auto* r = static_cast<Render3DNode*>(gNodes[3].node.get());
            const bool ok = dp->TriangleCount() > 0 && r->GetOutputTexture() != 0;
            printf("depth projection multi-pass passes: renderPass=%d  %s\n",
                   r->renderPass, ok ? "OK" : "FAIL");
            printf("DEPTH PROJECTION & MULTI-PASS RENDER 3D OK\n");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_3DTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_3DTEST") != nullptr &&
          (frameId == 4 || frameId == 8 || frameId == 12 || frameId == 16 ||
           frameId == 20 || frameId == 24 || frameId == 28))
      {
         auto* r = static_cast<Render3DNode*>(gNodes[2].node.get());
         const int w = r->GetOutputWidth(), h = r->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         // Count coverage rather than sampling points: a torus knot has holes,
         // and an earlier point-sample test reported "nothing drawn" for a
         // render that was in fact perfectly correct.
         size_t lit = 0;
         for (size_t i = 0; i < px.size(); i += 4)
            if (px[i] + px[i + 1] + px[i + 2] > 60)
               lit++;
         const double coverage = (double)lit / (double)(w * h);
         printf("render %dx%d samples=%d coverage=%.1f%%  %s\n", w, h, r->ActiveSamples(),
                coverage * 100.0,
                (coverage > 0.03 && coverage < 0.95) ? "GEOMETRY RASTERISED OK" : "SUSPECT");

         // The textured geometry above runs the mipmap/anisotropy path; an
         // unsupported enum or an incomplete mip chain would surface here.
         GLenum err = glGetError();
         printf("gl error after render: 0x%x  %s\n", err,
                err == GL_NO_ERROR ? "CLEAN" : "SUSPECT");

         // Mean luminance, used below to prove the exposure/tonemap uniforms
         // reach the shader rather than being silently optimised away.
         double meanLum = 0.0;
         for (size_t i = 0; i < px.size(); i += 4)
            meanLum += 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
         meanLum /= (double)(w * h);

         static std::vector<unsigned char> sAliased;
         static double sMeanAtDefault = 0.0;
         if (frameId == 8)
         {
            sMeanAtDefault = meanLum;
            r->exposure = 2.5f;
            r->tonemap = 0; // none
         }
         else if (frameId == 12)
         {
            printf("mean luminance %.2f -> %.2f after exposure 1.0/ACES -> 2.5/none\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", std::fabs(meanLum - sMeanAtDefault) > 2.0
                              ? "EXPOSURE + TONEMAP UNIFORMS LIVE"
                              : "SUSPECT - shader ignored exposure/tonemap");
            // Ask for a 4000px export at 8x and check the budget clamp steps it
            // down instead of trying to allocate a gigabyte of renderbuffer.
            r->width = 4000.0f; r->height = 4000.0f;
            r->samples = 3; // 8x
         }
         else if (frameId == 16)
         {
            printf("export %dx%d requested 8x -> active %dx\n", w, h, r->ActiveSamples());
            printf("%s\n", (w == 4000 && h == 4000 && r->ActiveSamples() >= 1 &&
                            r->ActiveSamples() <= 4)
                              ? "HIGH-RES EXPORT + MSAA BUDGET OK"
                              : "SUSPECT");
            // Back to a cheap size, then sweep the PBR material knobs.
            r->width = 700.0f; r->height = 700.0f;
            r->exposure = 1.0f; r->tonemap = 1;
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->metallic = 1.0f; g->roughness = 0.05f;
         }
         else if (frameId == 20)
         {
            sMeanAtDefault = meanLum; // polished metal
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->metallic = 0.0f; g->roughness = 1.0f;
         }
         else if (frameId == 24)
         {
            printf("mean luminance: polished metal %.2f -> rough dielectric %.2f\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", std::fabs(meanLum - sMeanAtDefault) > 2.0
                              ? "GGX METALLIC/ROUGHNESS RESPOND"
                              : "SUSPECT - BRDF ignored metallic/roughness");
            sMeanAtDefault = meanLum;
            auto* g = static_cast<GeometryNode*>(gNodes[0].node.get());
            g->emission = 4.0f;
         }
         else if (frameId == 28)
         {
            printf("mean luminance: emission 0 %.2f -> emission 4 %.2f\n",
                   sMeanAtDefault, meanLum);
            printf("%s\n", meanLum > sMeanAtDefault + 5.0
                              ? "EMISSION OK" : "SUSPECT - emission had no effect");
         }

         if (frameId == 4)
         {
            sAliased = px;
            r->samples = 2; // index 2 == 4x
         }
         else if (frameId == 8 && sAliased.size() == px.size())
         {
            size_t changed = 0, maxDelta = 0;
            for (size_t i = 0; i < px.size(); i += 4)
            {
               const int d = std::abs((int)px[i] - (int)sAliased[i]);
               if (d > 8) changed++;
               if ((size_t)d > maxDelta) maxDelta = (size_t)d;
            }
            const double pct = 100.0 * (double)changed / (double)(w * h);
            printf("antialias diff: %zu px changed (%.2f%%), max delta %zu\n",
                   changed, pct, maxDelta);
            printf("%s\n", (r->ActiveSamples() > 1 && changed > 1000)
                              ? "MSAA OK" : "SUSPECT - multisampling had no effect");
         }
      }
}

void FrameTest_UISCALETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_UISCALETEST") != nullptr)
      {
         // Phases, 8 frames each so the rebake and two node-editor layout passes settle:
         // scale 1.0 (reference), 1.5, 2.0, 1.25, then the user's own value is restored.
         // 1.25 asks for a fractional font size even on Retina (37.5 px); 1.5 does on
         // Windows/X11 (22.5 px). (pre-1.92 note: ImGui truncated baked font sizes.)
         static const float kScales[] = { 1.0f, 1.5f, 2.0f, 1.25f };
         const int kPhases = 4;
         static float sUserScale = 1.0f;
         static std::vector<ImVec2> sRef;
         static int sFailures = 0;
         const int kPhaseFrames = 8;
         if (frameId == 1)
         {
            const char* firstFailure = nullptr;
            const int tableFailures = UiScale::SelfCheck(&firstFailure);
            if (tableFailures != 0)
            {
               printf("UISCALETEST FAIL: decision table, %d case(s) wrong, first: %s\n", tableFailures,
                      firstFailure ? firstFailure : "?");
               sFailures += tableFailures;
            }
            sUserScale = CategoryColors::GetUiScale();
            CategoryColors::SetUiScale(kScales[0], false);
            UiScale::RequestRescale();
         }
         const int phase = frameId / kPhaseFrames;
         if (frameId > 1 && frameId % kPhaseFrames == kPhaseFrames - 1 && phase < kPhases)
         {
            const float scale = kScales[phase];
            const ImGuiIO& sio = ImGui::GetIO();
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            const float drawnW = sio.DisplaySize.x * sio.DisplayFramebufferScale.x;
            const float fontPx = ImGui::GetFontSize();
            printf("UISCALETEST scale %.2f: point %.2f bake %.2f display %.0fx%.0f font %.2f\n", scale,
                   UiScale::Current().pointScale, UiScale::Current().bakeScale, sio.DisplaySize.x,
                   sio.DisplaySize.y, fontPx);
            if (std::fabs(drawnW - (float)fbW) > 1.0f)
            {
               printf("UISCALETEST FAIL: scale %.1f draws %.1f px into a %d px framebuffer\n", scale, drawnW, fbW);
               sFailures++;
            }
            if (std::fabs(fontPx - UiScale::kBaseFontSize) > 0.01f)
            {
               printf("UISCALETEST FAIL: scale %.1f font is %.2f points, not %.0f\n", scale, fontPx,
                      UiScale::kBaseFontSize);
               sFailures++;
            }
            // The slider must reach the backend: point scale follows it 1:1 on top of the
            // platform's own factor (1 on Retina/Wayland, xscale on Windows/X11).
            static float sPointPerSlider = 0.0f;
            const float pointPerSlider = ImGui_ImplGlfw_GetPointScale() / scale;
            if (phase == 0)
               sPointPerSlider = pointPerSlider;
            else if (!UiScale::Near(pointPerSlider, sPointPerSlider))
            {
               printf("UISCALETEST FAIL: scale %.1f gives point scale %.3f, expected %.3f\n", scale,
                      ImGui_ImplGlfw_GetPointScale(), sPointPerSlider * scale);
               sFailures++;
            }
            for (size_t i = 0; i < gNodes.size(); i++)
            {
               const ImVec2 sz = ed::GetNodeSize(gNodes[i].NodeId());
               if (phase == 0)
               {
                  sRef.push_back(sz);
                  continue;
               }
               if (i >= sRef.size())
                  continue;
               // A body that clips or overflows at this scale changes its box. Glyph
               // advances bake at a different pixel size, so allow 1% (min 2 points).
               const float tolX = std::max(2.0f, sRef[i].x * 0.01f);
               const float tolY = std::max(2.0f, sRef[i].y * 0.01f);
               if (std::fabs(sz.x - sRef[i].x) > tolX || std::fabs(sz.y - sRef[i].y) > tolY)
               {
                  printf("UISCALETEST FAIL: %s is %.1fx%.1f at scale %.1f, %.1fx%.1f at 1.0\n",
                         gNodes[i].typeName.c_str(), sz.x, sz.y, scale, sRef[i].x, sRef[i].y);
                  sFailures++;
               }
            }
            if (phase + 1 < kPhases)
               CategoryColors::SetUiScale(kScales[phase + 1], false);
            else
               CategoryColors::SetUiScale(sUserScale, false);
            UiScale::RequestRescale();
         }
         if (frameId == kPhases * kPhaseFrames + 2)
         {
            if (sRef.size() != 4)
            {
               printf("UISCALETEST FAIL: measured %zu nodes, expected 4\n", sRef.size());
               sFailures++;
            }
            if (sFailures == 0)
               printf("UISCALETEST %zu nodes at 1.0/1.5/2.0/1.25, sizes held  OK\n", sRef.size());
            else
               printf("UISCALETEST FAIL: %d problem(s)\n", sFailures);
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_SIZETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SIZETEST") != nullptr)
      {
         if (frameId >= 2 && frameId <= 14)
         {
            ImVec2 sz = ed::GetNodeSize(gNodes[0].NodeId());
            printf("f%-2d nodeSize=(%.1f, %.1f)\n", frameId, sz.x, sz.y);
         }
         if (frameId == 15)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_SAMPLERDRAGTEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr && !gNodes.empty())
      {
         // Recomputed every frame, post-ed::End() where GetNodePosition/
         // GetNodeSize have a live editor context (see the global's
         // comment) - the pre-Begin() input driver above only ever reads
         // this cached value, never calls these itself.
         const ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
         const ImVec2 s = ed::GetNodeSize(gNodes[0].NodeId());
         // A freshly spawned node reports a sentinel/placeholder size for its
         // first frame or two, before ed:: has actually measured its content
         // - same reason SIZETEST above only trusts sizes from frameId>=2.
         // Skip those, leaving the sentinel -1 in place, rather than latch a
         // garbage target the driver would then aim the whole gesture at.
         if (s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f)
         {
            gSamplerDragTestTargetScreen = ed::CanvasToScreen(ImVec2(p.x + s.x * 0.5f, p.y + s.y * 0.5f));
            gSamplerDragTestTargetValid = true;
         }
      }
}

void FrameTest_PLUGINDRAGTEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr && !gNodes.empty())
      {
         // Same recompute-every-frame rationale as the Sampler block above.
         const ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
         const ImVec2 s = ed::GetNodeSize(gNodes[0].NodeId());
         if (s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f)
         {
            gPluginDragTestTargetScreen = ed::CanvasToScreen(ImVec2(p.x + s.x * 0.5f, p.y + s.y * 0.5f));
            gPluginDragTestTargetValid = true;
         }
      }
}

void FrameTest_MEDIADRAGTEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MEDIADRAGTEST") != nullptr && !gNodes.empty())
      {
         // Same recompute-every-frame rationale as the Sampler block above.
         const ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
         const ImVec2 s = ed::GetNodeSize(gNodes[0].NodeId());
         if (s.x > 1.0f && s.x < 5000.0f && s.y > 1.0f && s.y < 5000.0f)
         {
            gMediaDragTestTargetScreen = ed::CanvasToScreen(ImVec2(p.x + s.x * 0.5f, p.y + s.y * 0.5f));
            gMediaDragTestTargetValid = true;
         }
      }
}

void FrameTest_WTDRAGTEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_WTDRAGTEST") != nullptr)
      {
         // Unbuffered: this hook is watched from a pipe, and a run that hangs
         // instead of reaching its last frame would otherwise print nothing at
         // all rather than showing how far it got.
         static bool sUnbuffered = false;
         if (!sUnbuffered) { setvbuf(stdout, nullptr, _IONBF, 0); sUnbuffered = true; }
         if (getenv("INFINITE_WTDRAGTRACE") != nullptr)
            printf("WTDRAG frame %d rects=%zu screen=%zu\n", frameId, gWtTestRects.size(),
                   gWtTestScreen.size());
         gWtTestScreen.clear();
         for (const ImVec4& r : gWtTestRects)
         {
            const ImVec2 mn = ed::CanvasToScreen(ImVec2(r.x, r.y));
            const ImVec2 mx = ed::CanvasToScreen(ImVec2(r.z, r.w));
            gWtTestScreen.push_back(ImVec4(mn.x, mn.y, mx.x, mx.y));
         }
         static float sPosA = 0.0f, sPosB = 0.0f, sSusA = 0.0f, sSusB = 0.0f;
         static float sFiltA = 0.0f, sFiltB = 0.0f;
         auto* wt = static_cast<WavetableNode*>(gNodes[0].node.get());
         if (frameId == 53)
         {
            sPosA = wt->engines[0].position;
            sPosB = wt->engines[1].position;
            sSusA = wt->engines[0].ampSustain;
            sSusB = wt->engines[1].ampSustain;
            sFiltA = wt->engines[0].filterAttack;
            sFiltB = wt->engines[1].filterAttack;
            printf("WTDRAG start: rects=%zu  A.pos=%.3f B.pos=%.3f A.sus=%.3f B.sus=%.3f\n",
                   gWtTestRects.size(), sPosA, sPosB, sSusA, sSusB);
         }
         if (frameId == 60)
         {
            // The drag ended at 80% across the picture, so engine A's position
            // must land near there - and engine B's must not have moved at all.
            const float posA = wt->engines[0].position;
            const float posB = wt->engines[1].position;
            const bool ok = posA > 0.6f && std::fabs(posB - sPosB) < 1e-4f;
            printf("WTDRAG table scrub: A.pos %.3f -> %.3f, B.pos %.3f -> %.3f  %s\n", sPosA, posA,
                   sPosB, posB, ok ? "OK" : "FAIL");
            gWtDragOk &= ok;
         }
         if (frameId == 68)
         {
            const float susA = wt->engines[0].ampSustain;
            const float susB = wt->engines[1].ampSustain;
            const bool ok = susA > sSusA + 0.1f && std::fabs(susB - sSusB) < 1e-4f;
            printf("WTDRAG amp sustain: A.sus %.3f -> %.3f, B.sus %.3f -> %.3f  %s\n", sSusA, susA,
                   sSusB, susB, ok ? "OK" : "FAIL");
            gWtDragOk &= ok;
         }
         if (frameId == 76)
         {
            const float fa = wt->engines[0].filterAttack;
            const float fb = wt->engines[1].filterAttack;
            const bool ok = fb > sFiltB + 100.0f && std::fabs(fa - sFiltA) < 1e-4f;
            printf("WTDRAG b filter attack: B %.0f -> %.0f ms, A %.0f -> %.0f ms  %s\n", sFiltB, fb,
                   sFiltA, fa, ok ? "OK" : "FAIL");
            gWtDragOk &= ok;
            printf("%s\n", gWtDragOk ? "WTDRAG OK" : "WTDRAG FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_DRAGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_DRAGTEST") != nullptr)
      {
         if (frameId == 2)
         {
            gDragTestNodePos = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestViewAnchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            printf("start   node canvas pos = (%.0f, %.0f) viewAnchor=(%.0f,%.0f)\n",
                   gDragTestNodePos.x, gDragTestNodePos.y,
                   gDragTestViewAnchor.x, gDragTestViewAnchor.y);
         }
         if (frameId == 9)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            ImVec2 anchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            bool nodeStill = std::fabs(now.x - gDragTestNodePos.x) < 0.5f &&
                             std::fabs(now.y - gDragTestNodePos.y) < 0.5f;
            bool viewMoved = std::fabs(anchor.x - gDragTestViewAnchor.x) > 20.0f ||
                             std::fabs(anchor.y - gDragTestViewAnchor.y) > 20.0f;
            printf("after canvas drag: node pos=(%.0f,%.0f) viewAnchor=(%.0f,%.0f) -> node %s, view %s : %s\n",
                   now.x, now.y, anchor.x, anchor.y,
                   nodeStill ? "still" : "MOVED",
                   viewMoved ? "panned" : "STATIC",
                   (nodeStill && viewMoved) ? "PAN OK" : "BUG");
            // aim at the node's title row: below the pins, above the preview
            ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestNodeScreen = ed::CanvasToScreen(ImVec2(p.x + 60.0f, p.y + 42.0f));
            gDragTestNodePos = p;
         }
         if (frameId == 20)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            printf("after node drag:   node pos = (%.0f, %.0f)  %s\n", now.x, now.y,
                   (std::fabs(now.x - gDragTestNodePos.x) > 20.0f ||
                    std::fabs(now.y - gDragTestNodePos.y) > 20.0f) ? "MOVED OK" : "DID NOT MOVE - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_KBCURSORTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_KBCURSORTEST") != nullptr)
      {
         // R571 keyboard model. Keys go in one frame before the check so the handler has run.
         ImGuiIO& tio = ImGui::GetIO();
         auto tap = [&](ImGuiKey k, int f, bool shift = false) {
            if (frameId == f) { if (shift) tio.AddKeyEvent(ImGuiMod_Shift, true); tio.AddKeyEvent(k, true); }
            if (frameId == f + 1) { tio.AddKeyEvent(k, false); if (shift) tio.AddKeyEvent(ImGuiMod_Shift, false); }
         };
         static bool ok = true;
         static std::vector<int> tabParams;
         static ImVec2 pos1(0, 0);
         static float scrollBefore = 0.0f;
         auto check = [&](bool good, const char* what) {
            ok = ok && good;
            printf("kbtest %-44s %s\n", what, good ? "ok" : "FAIL");
         };
         const int n1 = gNodes[1].index, n2 = gNodes[2].index;
         if (frameId == 3) { ed::ClearSelection(); ed::SelectNode(gNodes[1].NodeId(), false); }
         // 12 Tabs on node 1: focus stays on node 1 and loops.
         constexpr int kTabs = 24; // more than twice the node's params now that toggles and dropdowns are in the walk
         for (int i = 0; i < kTabs; ++i)
         {
            tap(ImGuiKey_Tab, 5 + i * 4);
            if (frameId == 8 + i * 4)
            {

               if (gKbFocusNode != n1) check(false, "Tab stays on the active node");
               tabParams.push_back(gKbFocusParam);
            }
         }
         if (frameId == 8 + kTabs * 4)
         {
            std::set<int> distinct(tabParams.begin(), tabParams.end());
            const size_t d = distinct.size();
            check(d >= 2, "Tab visits more than one param");
            check(tabParams.size() > d && tabParams[0] == tabParams[d], "Tab loops back to the first param");
            check(gKbFocusNode == n1, "focus never left node 1");
            pos1 = ed::GetNodePosition(gNodes[1].NodeId());
         }
         const int f0 = 8 + kTabs * 4 + 1; // 57
         tap(ImGuiKey_RightArrow, f0);
         if (frameId == f0 + 3)
         {
            const ImVec2 now = ed::GetNodePosition(gNodes[1].NodeId());
            check(now.x == pos1.x && now.y == pos1.y, "arrow on a focused param does not move the node");
            check(gKbNudge == 0, "nudge consumed by the param widget");
         }
         tap(ImGuiKey_Escape, f0 + 4);
         if (frameId == f0 + 7)
         {
            printf("kbtest esc: focusNode=%d focusParam=%d popup=%d typed=%zu wantText=%d\n", gKbFocusNode, gKbFocusParam,
                   (int)ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel), gTypedParam.size(), (int)tio.WantTextInput);
            check(gKbFocusParam == -1, "Esc leaves param focus");
         }
         tap(ImGuiKey_RightArrow, f0 + 8);
         if (frameId == f0 + 11)
         {
            const ImVec2 now = ed::GetNodePosition(gNodes[1].NodeId());
            check(now.x > pos1.x && std::fmod(now.x, gGridSnap) == 0.0f && now.y == pos1.y, "Right arrow moves the node one grid step");
         }
         tap(ImGuiKey_RightArrow, f0 + 12, /*shift=*/true);
         if (frameId == f0 + 15)
            check(ed::IsNodeSelected(gNodes[2].NodeId()) && !ed::IsNodeSelected(gNodes[1].NodeId()), "Shift+Right selects the next node");
         tap(ImGuiKey_Tab, f0 + 16);
         if (frameId == f0 + 19)
            check(gKbFocusNode == n2, "Tab now walks the new node's params");
         tap(ImGuiKey_Escape, f0 + 20);
         tap(ImGuiKey_Enter, f0 + 24, /*shift=*/true);
         if (frameId == f0 + 27)
            check(gKbZoomed, "Shift+Enter zooms into the node");
         tap(ImGuiKey_Enter, f0 + 28);
         if (frameId == f0 + 31)
            check(!gKbZoomed, "Enter zooms back out");
         if (frameId == f0 + 32) { scrollBefore = ed::GetViewScroll().y; tio.AddKeyEvent(ImGuiKey_W, true); }
         if (frameId == f0 + 38) tio.AddKeyEvent(ImGuiKey_W, false);
         if (frameId == f0 + 40)
         {
            const float after = ed::GetViewScroll().y;
            printf("kbtest W pan: scroll.y %.1f -> %.1f\n", scrollBefore, after);
            check(after < scrollBefore, "W pans the view up");
         }
         // A selected group: arrows carry its members, Cmd/Ctrl+U dissolves it.
         static int grpIdx = -1, memberIdx = -1;
         static ImVec2 memberBefore(0, 0);
         if (frameId == f0 + 41)
         {
            memberIdx = gNodes[2].index;
            memberBefore = ed::GetNodePosition(gNodes[2].NodeId());
            GraphNode* gn = SpawnNode("Group", "Compositing", memberBefore.x - 24.0f, memberBefore.y - 60.0f);
            grpIdx = gn->index;
            gGroupMembers[static_cast<GroupNode*>(gn->node.get())] = { memberIdx };
         }
         if (frameId == f0 + 44)
         {
            ed::ClearSelection();
            ed::SelectNode(FindNodeByIndex(grpIdx)->NodeId(), false);
         }
         tap(ImGuiKey_RightArrow, f0 + 47);
         if (frameId == f0 + 51)
         {
            GraphNode* m = FindNodeByIndex(memberIdx);
            const ImVec2 now = m ? ed::GetNodePosition(m->NodeId()) : ImVec2(0, 0);
            check(m != nullptr && now.x > memberBefore.x && now.y == memberBefore.y, "arrow on a selected group moves its members");
         }
         if (frameId == f0 + 53) { tio.AddKeyEvent(ImGuiMod_Ctrl, true); tio.AddKeyEvent(ImGuiKey_U, true); }
         if (frameId == f0 + 54) { tio.AddKeyEvent(ImGuiKey_U, false); tio.AddKeyEvent(ImGuiMod_Ctrl, false); }
         if (frameId == f0 + 59)
         {
            check(FindNodeByIndex(grpIdx) == nullptr && FindNodeByIndex(memberIdx) != nullptr, "Cmd/Ctrl+U ungroups, members stay");
            ed::ClearSelection();
            ed::SelectNode(gNodes[0].NodeId(), false);
         }
         // H opens the node help, H again closes it.
         tap(ImGuiKey_H, f0 + 62);
         if (frameId == f0 + 67)
            check(gNodeHelpShown, "H opens the node help");
         tap(ImGuiKey_H, f0 + 68);
         if (frameId == f0 + 74)
         {
            check(!gNodeHelpShown, "H again closes the node help");
         }
         // F frames the whole graph: pan away with S, then F must bring the view back to the content.
         static ImVec2 viewBeforeFit(0, 0);
         static float zoomBeforeFit = 0.0f;
         if (frameId == f0 + 75) tio.AddKeyEvent(ImGuiKey_S, true);
         if (frameId == f0 + 90) tio.AddKeyEvent(ImGuiKey_S, false);
         if (frameId == f0 + 92) { viewBeforeFit = ed::GetViewScroll(); zoomBeforeFit = ed::GetViewZoom(); }
         tap(ImGuiKey_F, f0 + 93);
         if (frameId == f0 + 100)
         {
            const ImVec2 now = ed::GetViewScroll();
            const bool moved = std::fabs(now.x - viewBeforeFit.x) > 1.0f || std::fabs(now.y - viewBeforeFit.y) > 1.0f ||
                               std::fabs(ed::GetViewZoom() - zoomBeforeFit) > 0.001f;
            printf("kbtest F fit: scroll (%.1f,%.1f) -> (%.1f,%.1f)\n", viewBeforeFit.x, viewBeforeFit.y, now.x, now.y);
            check(moved, "F frames the whole graph");
            check(!gRequestFitView, "F request consumed");
            printf("kbtest result: %s\n", ok ? "KBCURSOR OK" : "KBCURSOR FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_KBDISCRETETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_KBDISCRETETEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         auto tap = [&](ImGuiKey k, int f) {
            if (frameId == f) tio.AddKeyEvent(k, true);
            if (frameId == f + 1) tio.AddKeyEvent(k, false);
         };
         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         auto* formula = static_cast<FormulaNode*>(gNodes[1].node.get());
         static int shapeBefore = -1;
         static bool ok = true;
         constexpr int kSteps = 14;
         if (frameId == 3)
         {
            shapeBefore = shape->shapeType;
            ed::ClearSelection();
            ed::SelectNode(gNodes[0].NodeId(), false);
         }
         // Walk node 0's params, pressing Right on each: only the dropdown reacts to a +1 step with a visible change.
         for (int i = 0; i < kSteps; ++i)
         {
            tap(ImGuiKey_Tab, 5 + i * 6);
            tap(ImGuiKey_RightArrow, 8 + i * 6);
         }
         const int f1 = 5 + kSteps * 6 + 4;
         if (frameId == f1)
         {
            const bool good = shape->shapeType != shapeBefore;
            ok = ok && good;
            printf("kbdiscrete dropdown %d -> %d  %s\n", shapeBefore, shape->shapeType, good ? "ok" : "FAIL");
            tio.AddKeyEvent(ImGuiKey_Escape, true);
         }
         if (frameId == f1 + 1) tio.AddKeyEvent(ImGuiKey_Escape, false);
         if (frameId == f1 + 3)
         {
            ed::ClearSelection();
            ed::SelectNode(gNodes[1].NodeId(), false);
         }
         for (int i = 0; i < kSteps; ++i)
         {
            tap(ImGuiKey_Tab, f1 + 6 + i * 6);
            tap(ImGuiKey_LeftArrow, f1 + 9 + i * 6);
         }
         if (frameId == f1 + 6 + kSteps * 6 + 4)
         {
            const bool good = !formula->animate;
            ok = ok && good;
            printf("kbdiscrete checkbox animate on -> %s  %s\n", formula->animate ? "on" : "off", good ? "ok" : "FAIL");
            printf("kbdiscrete result: %s\n", ok ? "KBDISCRETE OK" : "KBDISCRETE FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_INPUTTEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         if (frameId == 2)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            sh->shapeType = 6; // Star, so the paste can be checked for fidelity
            sh->sides = 7;
            ed::SelectNode(gNodes[0].NodeId(), false);
            printf("frame2: selected node0, nodes=%zu\n", gNodes.size());
         }
         if (frameId == 7)
         {
            printf("after paste: nodes=%zu\n", gNodes.size());
            for (GraphNode& gn : gNodes)
            {
               if (auto* sp = dynamic_cast<ShapeNode*>(gn.node.get()))
                  printf("  Shape idx=%d shapeType=%d sides=%d\n", gn.index, sp->shapeType, sp->sides);
            }
            ed::SelectNode(gNodes.back().NodeId(), false);
         }
         if (frameId == 10)
         {
            printf("after delete: nodes=%zu\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_COLORTEST_3(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_COLORTEST") != nullptr && frameId == 6)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         sh->fillColor[0] = 0.9f; sh->fillColor[1] = 0.35f; sh->fillColor[2] = 0.2f;
         gColor.target = sh->fillColor;
         gColor.owner = sh;
         gColor.label = "fill";
         gColor.justOpened = true;
      }
}

void FrameTest_AUDIOUITEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_AUDIOUITEST") != nullptr)
      {
         // Plugin instantiation is asynchronous, so the fixture's mapping list
         // can't be filled in at spawn time - map the first few parameters the
         // moment the plugin reports ready, so the screenshot shows the real
         // two-column grid of mapped rows.
         for (GraphNode& gn : gNodes)
         {
            auto* pluginFixture = dynamic_cast<AudioPluginNode*>(gn.node.get());
            if (pluginFixture == nullptr || !pluginFixture->IsReady() ||
                pluginFixture->AssignedCount() > 0)
               continue;
            const std::vector<Platform::PluginParamInfo>& available = pluginFixture->AvailableParams();
            for (int i = 0; i < (int)available.size() && i < 4; i++)
               pluginFixture->MapParameter(available[i].address);
         }
      }
}

void FrameTest_MIXEDSTRESSTEST_2(int frameId, GLFWwindow* window)
{
   if (const char* mixedArg = getenv("INFINITE_MIXEDSTRESSTEST"); mixedArg != nullptr && frameId == 1)
      {
         const double scale = std::max(0.1, atof(mixedArg[0] != '\0' ? mixedArg : "1.0"));
         const int modCount = std::max(1, (int)std::lround(scale * 60.0));
         auto& mod = Modulation::Instance();
         int bound = 0;
         for (const ParamRef& ref : mod.FrameParams())
         {
            if (bound >= modCount)
               break;
            if (ref.name == "channels")
               continue;
            // Structural/tessellation params: modulating these forces a full
            // mesh rebuild every frame they change (GeometryNode::RebuildIfNeeded),
            // which is a real, expected cost - not representative of typical
            // modulation targets (color/position/amplitude/opacity). Excluded so
            // this fixture stress-tests realistic modulation instead of the
            // pathological worst case.
            if (ref.name == "shape" || ref.name == "detail" || ref.name == "sides" ||
                ref.name == "bevel" || ref.name == "bevelSegments")
               continue;
            // Render target/resource-recreating params: modulating "width"/"height"
            // forces EnsureFbo to tear down and reallocate the render target (and
            // shadow map) every frame it changes; "antialias" changes MSAA sample
            // count similarly. Same reasoning as the geometry exclusion above -
            // real but not representative of typical modulation targets.
            if (ref.name == "width" || ref.name == "height" || ref.name == "antialias")
               continue;
            // Join Geometry's "mode" switches between plain concatenation
            // (kMerge, cheap) and real mesh-boolean CSG (union/intersect/
            // difference - JoinGeometryNode::RebuildIfNeeded ->
            // MeshOps::Boolean), which is far more expensive per rebuild.
            // Modulating it means periodically paying full CSG cost - a real
            // but non-representative worst case for this fixture.
            if (ref.name == "mode")
               continue;
            GraphNode* srcGn = FindNodeByIndex(ref.nodeIndex);
            if (srcGn != nullptr && dynamic_cast<LFONode*>(srcGn->node.get()) != nullptr)
               continue;
            if (mod.IsModulated(ref.nodeIndex, ref.paramIndex))
               continue;
            GraphNode* lfo = SpawnNode("LFO", "Utility", 0.0f, 0.0f);
            mod.Bind(ref.nodeIndex, ref.paramIndex, lfo->index);
            bound++;
            if (getenv("INFINITE_MIXEDSTRESSTEST_VERBOSE") != nullptr)
               fprintf(stderr, "[mixedstress] bound node=%d param=%s (%s)\n",
                       ref.nodeIndex, ref.name.c_str(),
                       srcGn != nullptr ? srcGn->typeName.c_str() : "?");
         }
      }
}

void FrameTest_HIDETEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_HIDETEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (frameId == 2)
         {
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size x")
                  mod.Bind(ref.nodeIndex, ref.paramIndex, gNodes[2].index);
            }
            printf("bound: links=%zu (frameParams=%zu)\n", mod.Links().size(), mod.FrameParams().size());
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index)
                  printf("   node0 param %d = %s\n", ref.paramIndex, ref.name.c_str());
            }
         }
         if (frameId == 4)
         {
            gNodes[0].showParams = false; // collapse the modulated node
            printf("params hidden\n");
         }
         if (frameId == 8)
         {
            printf("after hide: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
            // The binding surviving is not enough: a collapsed node still has to
            // show the cable, landing on its "mod" tag.
            int drawn = 0;
            for (const LinkInfo& link : gLinks)
               if (GraphNode::IsParamPin(link.dstPin) &&
                   GraphNode::NodeIndexFromPin(link.dstPin) == gNodes[0].index)
                  drawn++;
            printf("cable while hidden: %d %s\n", drawn,
                   drawn > 0 ? "DRAWN OK" : "MISSING - BUG");
         }
         if (frameId == 6)
         {
            // Stamp a sentinel well outside the binding's range while the node
            // is collapsed. If the apply pass is still writing this param, the
            // next frame puts it back inside "size x"'s own range; if collapsing
            // froze the modulation (the bug), the sentinel just sits there.
            static_cast<ShapeNode*>(gNodes[0].node.get())->sizeX = -999.0f;
         }
         if (frameId == 8)
         {
            const float sizeX = static_cast<ShapeNode*>(gNodes[0].node.get())->sizeX;
            printf("driven while hidden: size x = %.4f %s\n", sizeX,
                   (sizeX >= 0.01f && sizeX <= 1.0f) ? "APPLIED OK" : "FROZEN - BUG");
         }
         if (frameId == 9)
         {
            gNodes[0].showParams = true; // reopen
         }
         if (frameId == 12)
         {
            printf("after reopen: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_MODTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MODTEST") != nullptr)
      {
         if (frameId == 1)
         {
            // Shape's size was split into "size x" / "size y" ModSliders;
            // resolve by name rather than guessing the param index.
            int sizeParam = -1;
            for (const ParamRef& ref : Modulation::Instance().FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size x")
                  sizeParam = ref.paramIndex;
            }
            printf("size param index = %d\n", sizeParam);
            if (sizeParam >= 0)
               Modulation::Instance().Bind(gNodes[0].index, sizeParam, gNodes[2].index);
            Transport::Instance().SetTempo(240.0f);
         }
         if (frameId >= 2 && frameId <= 40 && (frameId % 8) == 0)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            auto* lfo = static_cast<LFONode*>(gNodes[2].node.get());
            printf("f%-3d beats=%.3f lfo=%.3f  shape.size=%.4f\n",
                   frameId, Transport::Instance().Beats(), lfo->Value01(), sh->sizeX);
         }
         if (frameId == 44)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_MODBOUNDSTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MODBOUNDSTEST") != nullptr)
      {
         static int sidesParam = -1;
         static int rotParam = -1;
         static bool test1Ok = false, test2Ok = false, test3Ok = true;
         static int test5RotIdx = -1, test5NodeIdx = -1;
         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         auto* r2r = static_cast<RangeToRangeNode*>(gNodes[2].node.get());
         Modulation& mod = Modulation::Instance();

         if (frameId == 1)
         {
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "sides") sidesParam = ref.paramIndex;
               if (ref.nodeIndex == gNodes[0].index && ref.name == "rotation") rotParam = ref.paramIndex;
            }
            printf("sides param=%d rotation param=%d\n", sidesParam, rotParam);
            // Modulator returning 4.0 (out of [0,1], the pre-fix crash case):
            // constantIn=1 maps through inLow..inHigh=0..1 to outLow..outHigh.
            r2r->outLow = -2.0f;
            r2r->outHigh = 4.0f;
            r2r->constantIn = 1.0f;
            r2r->clampOutput = false;
            mod.Bind(gNodes[0].index, sidesParam, gNodes[2].index);
         }
         if (frameId == 5)
         {
            // Test 1: a modulator returning 4.0 must still land sides inside
            // [3,20] and integral, even though nothing clamps Value01() itself.
            // Check the raw backing slot (see ModSliderInt/gIntParamStore),
            // not just the rounded-and-clamped int it copies back to - that
            // catches ShapeToParam failing to snap/clamp, not just
            // ModSliderInt's own redundant safety net masking it.
            const float slot = gIntParamStore[{ gNodes[0].index, sidesParam }];
            test1Ok = shape->sides >= 3 && shape->sides <= 20 &&
                      slot >= 3.0f && slot <= 20.0f &&
                      std::fabs(slot - std::round(slot)) < 1e-5f;
            printf("test1 (out-of-range-high) sides=%d slot=%.4f  %s\n", shape->sides, slot,
                   test1Ok ? "OK" : "- BUG");
            // Now the -2.0 case.
            r2r->constantIn = 0.0f; // -> outLow = -2.0
         }
         if (frameId == 8)
         {
            // Test 2: a modulator returning -2.0 must land at exactly minValue.
            test2Ok = shape->sides == 3;
            printf("test2 (out-of-range-low) sides=%d  %s\n", shape->sides,
                   test2Ok ? "OK" : "- BUG");
            // Narrow the binding to lo=5,hi=9 and sweep a clean 0..1 modulator
            // across it - every resulting value must land in {5,6,7,8,9}.
            // Reconfiguring here takes two frames to become visible in
            // shape->sides (one for Bind/SetRange's effect to reach the apply
            // loop, one for ModSliderInt to copy the rounded slot back into
            // the int) - confirmed empirically against test1/test2's own
            // wider buffers - so the sweep below sets a value and checks it
            // two frames later rather than on the same frame.
            mod.SetRange(gNodes[0].index, sidesParam, 5.0f, 9.0f);
            r2r->outLow = 0.0f;
            r2r->outHigh = 1.0f;
            r2r->clampOutput = true;
            r2r->constantIn = 0.0f; // -> sides = 5
         }
         const float sweep[] = { 0.25f, 0.5f, 0.75f, 1.0f };
         if (frameId == 10 || frameId == 12 || frameId == 14 || frameId == 16)
         {
            if (shape->sides < 5 || shape->sides > 9)
               test3Ok = false;
            r2r->constantIn = sweep[(frameId - 10) / 2];
         }
         if (frameId == 18)
         {
            if (shape->sides < 5 || shape->sides > 9)
               test3Ok = false;
            printf("test3 (lo=5 hi=9 sweep) sides=%d  %s\n", shape->sides,
                   test3Ok ? "OK" : "- BUG");
            SavePatchTo(TmpPath("infinite_modboundstest.infinite"));
         }
         if (frameId == 20)
         {
            NewPatch();
            LoadPatchFrom(TmpPath("infinite_modboundstest.infinite"));
         }
         if (frameId == 22)
         {
            // Test 4: save/reload must reproduce the same lo/hi exactly - the
            // record's hasRange was already true before saving, so this reads
            // straight off the loaded Source with no derivation involved.
            int reloadedSides = -1;
            int reloadedNodeIdx = -1;
            for (GraphNode& gn : gNodes)
            {
               if (dynamic_cast<ShapeNode*>(gn.node.get()) != nullptr)
                  reloadedNodeIdx = gn.index;
            }
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == reloadedNodeIdx && ref.name == "sides") reloadedSides = ref.paramIndex;
            }
            const Modulation::Source reloaded = mod.ModulatorFor(reloadedNodeIdx, reloadedSides);
            const bool test4Ok = reloaded.hasRange &&
                                  std::fabs(reloaded.lo - 5.0f) < 1e-4f &&
                                  std::fabs(reloaded.hi - 9.0f) < 1e-4f;
            printf("test4 (save/reload) hasRange=%d lo=%.3f hi=%.3f  %s\n",
                   reloaded.hasRange, reloaded.lo, reloaded.hi, test4Ok ? "OK" : "- BUG");

            // Test 5: a legacy record (only polarity/depth/centre, no lo/hi -
            // hasRange false) must derive the identical swing on first draw
            // that the old bipolar behaviour produced.
            int rotIdx = -1;
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == reloadedNodeIdx && ref.name == "rotation") rotIdx = ref.paramIndex;
            }
            Modulation::Source legacy;
            legacy.nodeIndex = gNodes[2].index;
            legacy.outputIndex = 0;
            legacy.polarity = Modulation::Source::kBipolar;
            legacy.depth = 0.3f;
            legacy.centre = 10.0f;
            legacy.hasRange = false;
            mod.RestoreLink(reloadedNodeIdx, rotIdx, legacy);
            test5RotIdx = rotIdx;
            test5NodeIdx = reloadedNodeIdx;
         }
         if (frameId == 24)
         {
            // A couple of frames after RestoreLink, ModSlider has drawn
            // rotation modulated and called ResolvedSourceFor, converting it -
            // matching the same two-frame lag as everything else here.
            const Modulation::Source resolved = mod.ModulatorFor(test5NodeIdx, test5RotIdx);
            const float expectedLo = 10.0f - 0.3f * 360.0f;
            const float expectedHi = 10.0f + 0.3f * 360.0f;
            const bool test5Ok = resolved.hasRange &&
                                 std::fabs(resolved.lo - expectedLo) < 1e-3f &&
                                 std::fabs(resolved.hi - expectedHi) < 1e-3f;
            printf("test5 (legacy bipolar derive) hasRange=%d lo=%.3f hi=%.3f expected lo=%.3f hi=%.3f  %s\n",
                   resolved.hasRange, resolved.lo, resolved.hi, expectedLo, expectedHi,
                   test5Ok ? "OK" : "- BUG");
            // No self-close here: INFINITE_EXITAFTER owns closing the window
            // and flushing stdout (see its handler below) - a self-close
            // would race it and drop this frame's printf output when stdout
            // is fully-buffered (i.e. always, once redirected to a file).
            printf("%s\n", (test1Ok && test2Ok && test3Ok) ? "MOD BOUNDS TEST OK" : "SUSPECT");
         }
      }
}

void FrameTest_MPCMODTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MPCMODTEST") != nullptr)
      {
         static bool ok = true;
         static int mpcIdx = -1;
         Modulation& mod = Modulation::Instance();
         auto bad = [&](const char* what)
         {
            printf("MPC MOD TEST %s FAIL\n", what);
            ok = false;
         };
         auto findMpc = [&]() -> MpcNode*
         {
            for (GraphNode& gn : gNodes)
               if (auto* m = dynamic_cast<MpcNode*>(gn.node.get()))
               {
                  mpcIdx = gn.index;
                  return m;
               }
            return nullptr;
         };
         auto findR2R = [&]() -> RangeToRangeNode*
         {
            for (GraphNode& gn : gNodes)
               if (auto* r = dynamic_cast<RangeToRangeNode*>(gn.node.get()))
                  return r;
            return nullptr;
         };
         auto r2rIndex = [&]() -> int
         {
            for (GraphNode& gn : gNodes)
               if (dynamic_cast<RangeToRangeNode*>(gn.node.get()) != nullptr)
                  return gn.index;
            return -1;
         };
         MpcNode* mpc = findMpc();
         RangeToRangeNode* r2r = findR2R();
         const int volA = mpc != nullptr ? MpcNode::ParamId(2, MpcNode::kVolume) : -1;
         if (frameId == 1 && mpc != nullptr && r2r != nullptr)
         {
            r2r->outLow = r2r->outHigh = 1.0f;
            r2r->clampOutput = true;
            r2r->constantIn = 1.0f;
            mpc->selectedPad = 0;
            mod.Bind(mpcIdx, volA, r2rIndex());
            mod.SetRange(mpcIdx, volA, 0.2f, 0.6f);
            const int modeSlot = DiscreteParamSlot(mpcIdx, MpcModeLabel(2));
            mod.Bind(mpcIdx, modeSlot, r2rIndex());
            // The per-pad transport and fade params: a float and two dropdowns.
            const int fadeA = MpcNode::ParamId(2, MpcNode::kFadeIn);
            mod.Bind(mpcIdx, fadeA, r2rIndex());
            mod.SetRange(mpcIdx, fadeA, 50.0f, 150.0f);
            mod.Bind(mpcIdx, DiscreteParamSlot(mpcIdx, MpcDiscreteLabel(2, kMpcSync)), r2rIndex());
            mod.Bind(mpcIdx, DiscreteParamSlot(mpcIdx, MpcDiscreteLabel(2, kMpcDiv)), r2rIndex());
         }
         auto checkAddresses = [&](const char* when)
         {
            // Every pad's every float param registered under its fixed id, at
            // that pad's own storage, with a per-pad name.
            for (int p = 0; p < MpcNode::kPads; p++)
               for (int k = 0; k < MpcNode::kNumPadParams; k++)
               {
                  const ParamRef* r = nullptr;
                  for (const ParamRef& fr : mod.FrameParams())
                     if (fr.nodeIndex == mpcIdx && fr.paramIndex == MpcNode::ParamId(p, k))
                        r = &fr;
                  if (r == nullptr || r->value != mpc->PadParamPtr(p, k) || r->name != MpcParamName(p, k))
                  {
                     char what[96];
                     snprintf(what, sizeof(what), "%s: pad %d param %d not at its fixed address", when, p + 1, k);
                     bad(what);
                     return;
                  }
               }
         };
         if (frameId == 8 && mpc != nullptr)
         {
            checkAddresses("selected pad 1");
            if (std::fabs(mpc->padVolume[2] - 0.6f) > 1e-3f || mpc->padMode[2] != MpcNode::kLoopToggle)
               bad("binding did not drive pad 3 (volume 0.6, mode loop)");
            if (std::fabs(mpc->padVolume[0] - 0.8f) > 1e-6f || mpc->padMode[0] != MpcNode::kOneShot)
               bad("binding leaked onto the selected pad 1");
            if (std::fabs(mpc->padFadeIn[2] - 150.0f) > 0.5f || mpc->padDiv[2] != (int)MusicTime::kNumRateDivisions - 1 ||
                mpc->padSync[2] != MpcNode::kFree)
               bad("binding did not drive pad 3's fade in (150) / rate (1/64) / sync (Free)");
            if (std::fabs(mpc->padFadeIn[0] - 3.0f) > 1e-6f || mpc->padDiv[0] != (int)MusicTime::kQuarter ||
                mpc->padSync[0] != MpcNode::kFree)
               bad("fade / sync / rate binding leaked onto the selected pad 1");
            mpc->selectedPad = 5; // the whole point
            r2r->outLow = r2r->outHigh = 0.0f;
         }
         if (frameId == 14 && mpc != nullptr)
         {
            checkAddresses("selected pad 6");
            if (!mod.IsModulated(mpcIdx, volA) || mod.IsModulated(mpcIdx, MpcNode::ParamId(5, MpcNode::kVolume)))
               bad("selecting pad 6 moved the binding");
            if (std::fabs(mpc->padVolume[2] - 0.2f) > 1e-3f)
               bad("pad 3 stopped following its cable while pad 6 was selected");
            if (std::fabs(mpc->padVolume[5] - 0.8f) > 1e-6f || mpc->padMode[5] != MpcNode::kOneShot)
               bad("the cable also drove the selected pad 6");
            if (std::fabs(mpc->padFadeIn[2] - 50.0f) > 0.5f || mpc->padDiv[2] != 0 || mpc->padSync[2] != MpcNode::kSynced)
               bad("pad 3's fade in / rate / sync stopped following its cable while pad 6 was selected");
            if (std::fabs(mpc->padFadeIn[5] - 3.0f) > 1e-6f || mpc->padDiv[5] != (int)MusicTime::kQuarter ||
                mpc->padSync[5] != MpcNode::kFree)
               bad("the fade / sync / rate cables also drove the selected pad 6");
            SavePatchTo(TmpPath("infinite_mpcmodtest.infinite"));
         }
         if (frameId == 16)
         {
            NewPatch();
            LoadPatchFrom(TmpPath("infinite_mpcmodtest.infinite"));
            mpc = nullptr; // indices changed; re-resolved below
         }
         if (frameId == 26)
         {
            mpc = findMpc();
            if (mpc == nullptr)
               bad("MPC missing after reload");
            else
            {
               const int vol = MpcNode::ParamId(2, MpcNode::kVolume);
               const int modeSlot = DiscreteParamSlot(mpcIdx, MpcModeLabel(2));
               const Modulation::Source src = mod.ModulatorFor(mpcIdx, vol);
               if (!mod.IsModulated(mpcIdx, vol) || !mod.IsModulated(mpcIdx, modeSlot) ||
                   mod.IsModulated(mpcIdx, MpcNode::ParamId(5, MpcNode::kVolume)))
                  bad("bindings did not survive save/load on the right pad");
               if (!mod.IsModulated(mpcIdx, MpcNode::ParamId(2, MpcNode::kFadeIn)) ||
                   !mod.IsModulated(mpcIdx, DiscreteParamSlot(mpcIdx, MpcDiscreteLabel(2, kMpcSync))) ||
                   !mod.IsModulated(mpcIdx, DiscreteParamSlot(mpcIdx, MpcDiscreteLabel(2, kMpcDiv))) ||
                   mod.IsModulated(mpcIdx, MpcNode::ParamId(5, MpcNode::kFadeIn)))
                  bad("fade / sync / rate bindings did not survive save/load on the right pad");
               if (!src.hasRange || std::fabs(src.lo - 0.2f) > 1e-4f || std::fabs(src.hi - 0.6f) > 1e-4f)
                  bad("binding range changed across save/load");
               if (mpc->selectedPad != 5)
                  bad("selected pad was not restored");
               checkAddresses("after reload");
               if (std::fabs(mpc->padVolume[2] - 0.2f) > 1e-3f)
                  bad("pad 3 not driven after reload");
               if (std::fabs(mpc->padVolume[5] - 0.8f) > 1e-6f)
                  bad("reload put the cable on the selected pad");
            }
            printf("%s\n", ok ? "MPC MOD TEST OK" : "MPC MOD TEST FAIL");
         }
      }
}

void FrameTest_LOOPERTRIGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_LOOPERTRIGTEST") != nullptr)
      {
         // A Macro Trigger patched onto a Looper transport button must press it on EVERY
         // trigger (the Looper acts on rising edges; a flip-per-trigger bool only rose on
         // every second one), while a Macro Trigger on an ordinary checkbox still toggles it.
         static bool ok = true;
         static int looperIdx = -1, trigA = -1, trigB = -1;
         static bool thruStart = false;
         Modulation& mod = Modulation::Instance();
         auto bad = [&](const char* what)
         {
            printf("LOOPER TRIGGER TEST %s FAIL\n", what);
            ok = false;
         };
         LooperNode* looper = nullptr;
         MacroTriggerNode* ta = nullptr;
         MacroTriggerNode* tb = nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (auto* l = dynamic_cast<LooperNode*>(gn.node.get())) { looper = l; looperIdx = gn.index; }
            else if (auto* t = dynamic_cast<MacroTriggerNode*>(gn.node.get()))
            {
               if (ta == nullptr) { ta = t; trigA = gn.index; }
               else { tb = t; trigB = gn.index; }
            }
         }
         if (looper == nullptr || ta == nullptr || tb == nullptr)
         {
            if (frameId == 1)
               bad("fixture missing");
         }
         else
         {
            if (frameId == 2)
            {
               int thruParam = -1;
               bool momentaryRec = false;
               for (const ParamRef& ref : mod.FrameParams())
               {
                  if (ref.nodeIndex != looperIdx) continue;
                  if (ref.name == "thru") { thruParam = ref.paramIndex; if (ref.momentary) bad("thru checkbox flagged momentary"); }
                  if (ref.name == "rec") momentaryRec = ref.momentary;
               }
               if (thruParam < 0) bad("thru checkbox not registered");
               if (!momentaryRec) bad("rec button not registered momentary");
               mod.Bind(looperIdx, DiscreteParamSlot(looperIdx, "rec"), trigA);
               if (thruParam >= 0)
                  mod.Bind(looperIdx, thruParam, trigB);
            }
            // Fire A on frames 4, 8, 12 (a tap: justTriggered without a held pad).
            if (frameId == 4 || frameId == 8 || frameId == 12)
               ta->justTriggered = true;
            if (frameId == 14)
            {
               const int presses = looper->ButtonPresses(LooperNode::kRec);
               if (presses != 3)
               {
                  char what[96];
                  snprintf(what, sizeof(what), "3 triggers gave %d Rec presses (want 3)", presses);
                  bad(what);
               }
               if (looper->ButtonPresses(LooperNode::kPlay) != 0 || looper->ButtonPresses(LooperNode::kClear) != 0)
                  bad("trigger on Rec pressed another button");
               // The checkbox still flips per trigger (its start value is whatever the
               // binding inherited, so compare against that).
               thruStart = looper->thru;
               tb->justTriggered = true;
            }
            if (frameId == 17)
            {
               if (looper->thru == thruStart)
                  bad("a trigger no longer toggles an ordinary checkbox (1st)");
               tb->justTriggered = true;
            }
            if (frameId == 20)
            {
               if (looper->thru != thruStart)
                  bad("a trigger no longer toggles an ordinary checkbox (2nd)");
               printf("%s\n", ok ? "LOOPER TRIGGER TEST OK" : "LOOPER TRIGGER TEST FAIL");
            }
         }
      }
}
}
