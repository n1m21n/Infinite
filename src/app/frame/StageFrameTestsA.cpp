// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawFrameTestsA(FrameCtx& fc)
{
   auto& window = fc.window;
   auto& frameId = fc.frameId;
   auto& isBenchB5c = fc.isBenchB5c;


      FrameTest_COLORTEST_2(frameId, window);

      FrameTest_RECTEST(frameId, window);

      FrameTest_RECTEARDOWNTEST(frameId, window);

      FrameTest_RESYNTHTEST(frameId, window);

      FrameTest_BYPASSTEST(frameId, window);

      FrameTest_CURVESLUTTEST(frameId, window);

      FrameTest_PHASEATEST(frameId, window);

      FrameTest_SELECTTEST(frameId, window);

      FrameTest_DISTRIBUTETEST(frameId, window);

      FrameTest_PHASE4TEST(frameId, window);

      FrameTest_INSTANCESELECTTEST(frameId, window);

      FrameTest_PADPATHTEST(frameId, window);

      FrameTest_UNDOTEST(frameId, window);

      // Modulation never creates an undo entry or dirties the patch, and one
      // user dropdown pick is exactly one undo entry holding the pre-pick
      // state (regression, docs/fix-briefs/modulated-dropdown-undo-spam.md).
      // A cable-driven discrete control writes its value back through the
      // caller's onSelect lambda / returns "changed" to the caller, and most
      // of those callers open with PushUndoCheckpoint() for the user-click
      // path - so every index boundary a modulator crossed used to serialize
      // the whole patch onto gUndoStack and set gPatchDirty. The existing
      // UNDOTEST never binds a modulator to anything, which is why this
      // shipped. Drives all five widget shapes over real drawn frames:
      //   AudioKnobRow::Dropdown      Audio Filter "type"
      //   AudioBareDropdown           Oscillator "oscWave"
      //   AudioKnobRow::DropdownKnob  Oscillator "oscFmMode"
      //   DropdownButton              Audio Color Ramp "mode"
      //   AudioKnobRow::Checkbox      Delay "sync to tempo"
      // from two Constants swept 0..1..0 several times. Then a second, unbound
      // Audio Filter's "type" dropdown is opened with its real onSelect lambda
      // and a row is committed through CommitDropdownPick - the code the popup
      // click runs - to prove the pick pushes one entry and Undo restores it.
      FrameTest_MODDROPDOWNUNDOTEST(frameId, window);

      FrameTest_ARRANGETEST(frameId, window);

      // Overhaul WP2 (docs/plans/arrangement/overhaul-prompt.md): the transport
      // clock's three new contracts - a tempo change doesn't move the
      // playhead, Beats() is seekable, and the loop wraps at a block boundary
      // rather than a UI frame.
      //
      // The audio clock is driven by hand here (NotifyAudioEngineStarted +
      // AdvanceAudioClock) with the real engine stopped first, so the fixture
      // is deterministic, needs no audio device, and never races a live audio
      // thread calling the same functions.
      FrameTest_TRANSPORTTEST(frameId, window);

      // Regression guard for the BuildPatchData() perf fix in
      // docs/plans/undo-delete-perf-prompt.md: it used to be O(N^2) with
      // dynamic_cast in the inner loop, which is what actually caused the
      // per-click stutter and the multi-second undo hang on a large patch.
      // The ceiling is deliberately generous - the fixed version should run
      // in low single-digit milliseconds even at this node count, so this is
      // here to catch a return to O(N^2) (or worse), not to chase a specific
      // number.
      // Arrangement timeline audio scheduling (overhaul WP3). Deterministic:
      // no device, no wall clock - the transport runs in offline mode and
      // AudioEngine::ProcessOffline is pumped by hand, so a block is a block
      // no matter how loaded the machine is.
      //
      // Everything here goes through the REAL RebuildAudioTopology over the
      // real gArrange model, not a hand-built topology: the bugs WP3 fixes
      // all lived in what that function decided to put in the topology, so a
      // fixture that built its own would test nothing.
      if (getenv("INFINITE_ARRANGEAUDIOTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;

         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kBpm = 120.0;            // 2 beats per second
         const double kSamplesPerBeat = kSr * 60.0 / kBpm;

         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();
         const AudioMode savedMode = gAudioMode;

         GraphNode* oscGn = SpawnNode("Oscillator", "Synthesizers", 0.0f, 0.0f);
         const bool spawned = oscGn != nullptr && oscGn->node != nullptr;
         printf("arrange audio spawn oscillator: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {
            const uint64_t oscUid = oscGn->uid;
            const int oscIndex = oscGn->index;

            // One audio lane, clips filled in per section. Built straight into
            // gArrange (WP5b); revision keeps climbing across the reset so the
            // rebuild trigger never sees it rewind onto an old built value.
            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            Arrange::FindLane(gArrange, laneId)->name = "A1";
            (void)oscIndex;

            auto clearClips = [&]()
            {
               std::vector<uint64_t> ids;
               for (const Arrange::Clip& c : Arrange::FindLane(gArrange, laneId)->clips)
                  ids.push_back(c.id);
               Arrange::Delete(gArrange, ids);
            };
            auto addClip = [&](double startBeat, double lengthBeats, bool enabled)
            {
               Arrange::Clip c;
               c.start = Arrange::BeatsToTicks(startBeat);
               c.length = Arrange::BeatsToTicks(lengthBeats);
               c.srcUid = oscUid;
               c.enabled = enabled;
               Arrange::PlaceOverwrite(gArrange, laneId, c);
            };
            // Rebuilds the per-frame trigger fired while a render ran - it is
            // called once per block, so crossing a clip boundary with it
            // running is exactly the "a boundary must never rebuild" check.
            int staleRebuildsDuringRender = 0;

            gAudioMode = AudioMode::Timeline;
            tr.SetTempo((float)kBpm);
            tr.SetLoop(false, 0.0, 0.0);
            tr.SetPlaying(true);
            tr.SetOfflineMode(true, kSr);

            // Renders [startBeat, startBeat + numBlocks*kBlock samples) and
            // returns channel 0, concatenated. Rebuilds the topology first,
            // then prepares every node by hand: the PrepareToPlay loop inside
            // RebuildAudioTopology keys off a live device or an offline render
            // job, and this fixture has neither.
            // Params reach an AudioNode through its mailbox, which
            // CookIfNeeded fills - a node that has never been cooked runs on
            // its constructor defaults with an empty mailbox and produces
            // nothing. The main loop does this every frame; this fixture runs
            // its whole life inside one.
            int fixtureCookFrame = 1000000;
            auto cookAll = [&]()
            {
               fixtureCookFrame++;
               for (GraphNode& gn : gNodes)
                  gn.node->CookIfNeeded(fixtureCookFrame);
            };

            std::vector<float> lastChan1; // channel 1 of the most recent render()
            auto render = [&](double startBeat, int numBlocks)
            {
               RebuildAudioTopology();
               cookAll();
               for (GraphNode& gn : gNodes)
                  if (auto* an = dynamic_cast<AudioNode*>(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(startBeat);

               std::vector<float> chan0((size_t)kBlock), chan1((size_t)kBlock);
               float* chans[2] = { chan0.data(), chan1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;

               std::vector<float> out;
               out.reserve((size_t)kBlock * (size_t)numBlocks);
               lastChan1.clear();
               staleRebuildsDuringRender = 0;
               for (int b = 0; b < numBlocks; b++)
               {
                  if (ArrangeAudioRebuildIfStale())
                     staleRebuildsDuringRender++;
                  AudioEngine::Instance().ProcessOffline(buffer);
                  out.insert(out.end(), chan0.begin(), chan0.end());
                  lastChan1.insert(lastChan1.end(), chan1.begin(), chan1.end());
               }
               return out;
            };

            // Peak |x| over the samples covering [fromBeat, toBeat) of a
            // render that started at `originBeat`.
            auto peakOverBeats = [&](const std::vector<float>& x, double originBeat,
                                     double fromBeat, double toBeat)
            {
               const long long lo = std::max(0LL, (long long)((fromBeat - originBeat) * kSamplesPerBeat));
               const long long hi = std::min((long long)x.size(), (long long)((toBeat - originBeat) * kSamplesPerBeat));
               float peak = 0.0f;
               for (long long i = lo; i < hi; i++)
                  peak = std::max(peak, std::fabs(x[(size_t)i]));
               return peak;
            };

            // --- A. Two abutting clips of the same node are both audible ----
            // The original bug: the per-frame set of active srcIndex never
            // changed across the seam, so no rebuild happened and the stale
            // single-clip window silenced everything after the first clip.
            {
               clearClips();
               addClip(0.0, 2.0, true);
               addClip(2.0, 2.0, true);
               const std::vector<float> x = render(0.0, 800); // 800*256 = 204800 samples = 4.27 beats

               const float first = peakOverBeats(x, 0.0, 0.2, 1.8);
               const float second = peakOverBeats(x, 0.0, 2.2, 3.8);
               // The seam itself: abutting windows skip the declick, so the
               // signal must run straight through rather than dip to silence.
               const float seam = peakOverBeats(x, 0.0, 1.98, 2.02);
               // Two clip boundaries crossed (beat 2 and beat 4) with the
               // per-frame trigger polled every block: zero rebuilds.
               const bool aOk = first > 0.05f && second > 0.05f && seam > 0.05f &&
                                staleRebuildsDuringRender == 0;
               printf("arrange audio abutting clips: %s (first %.4f, second %.4f, seam %.4f, boundary rebuilds %d)\n",
                      aOk ? "OK" : "FAIL", first, second, seam, staleRebuildsDuringRender);
               allOk = allOk && aOk;
            }

            // --- B. Onset lands on the scheduled sample --------------------
            {
               clearClips();
               addClip(2.0, 2.0, true);
               const std::vector<float> x = render(0.0, 800);

               const long long expected = (long long)(2.0 * kSamplesPerBeat);
               long long firstAudible = -1;
               for (size_t i = 0; i < x.size(); i++)
                  if (std::fabs(x[i]) > 1e-5f) { firstAudible = (long long)i; break; }
               // The declick ramp is zero at exactly the onset sample and the
               // oscillator's own phase starts near zero, so the first sample
               // over the noise floor lands a hair after the scheduled one -
               // never before it, and never a UI frame later.
               const bool bOk = firstAudible >= expected && (firstAudible - expected) <= 8;
               printf("arrange audio onset: %s (scheduled %lld, first audible %lld, error %lld samples)\n",
                      bOk ? "OK" : "FAIL", expected, firstAudible, firstAudible - expected);
               allOk = allOk && bOk;
            }

            // --- C. A disabled clip is silent -------------------------------
            // `enabled` was not read by the audio path at all before WP3.
            {
               clearClips();
               addClip(0.0, 4.0, false);
               const std::vector<float> x = render(0.0, 400);
               const float peak = peakOverBeats(x, 0.0, 0.0, 2.0);
               const bool cOk = peak < 1e-6f;
               printf("arrange audio disabled clip: %s (peak %.8f)\n", cOk ? "OK" : "FAIL", peak);
               allOk = allOk && cOk;
            }

            // --- D. Paused in Timeline mode is silent -----------------------
            {
               clearClips();
               addClip(0.0, 4.0, true);
               tr.SetPlaying(false);
               const std::vector<float> x = render(0.0, 200);
               const float peak = peakOverBeats(x, 0.0, 0.0, 1.0);
               tr.SetPlaying(true);
               const bool dOk = peak < 1e-6f;
               printf("arrange audio paused: %s (peak %.8f)\n", dOk ? "OK" : "FAIL", peak);
               allOk = allOk && dOk;
            }

            // --- E. Seeking from clip A into clip B of the same node --------
            // The other half of the original bug: the set of active srcIndex
            // is identical on both sides of the seek, so nothing rebuilt and
            // clip B played silence.
            {
               clearClips();
               addClip(0.0, 2.0, true);
               addClip(4.0, 2.0, true);
               render(0.5, 100);                     // land inside clip A
               const std::vector<float> x = render(4.5, 200); // jump into clip B
               const float peak = peakOverBeats(x, 4.5, 4.6, 5.5);
               const bool eOk = peak > 0.05f;
               printf("arrange audio seek across clips: %s (peak %.4f)\n", eOk ? "OK" : "FAIL", peak);
               allOk = allOk && eOk;
            }

            // --- F. A rebuild mid-clip does not break the signal ------------
            // Editing an unrelated lane rebuilds the whole topology. With the
            // schedule carried on the terminal (and PDC state living outside
            // the topology), the block after the rebuild must continue the
            // same envelope rather than restart it.
            {
               clearClips();
               addClip(0.0, 8.0, true);
               RebuildAudioTopology();
               cookAll();
               for (GraphNode& gn : gNodes)
                  if (auto* an = dynamic_cast<AudioNode*>(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(1.0);

               std::vector<float> chan0((size_t)kBlock), chan1((size_t)kBlock);
               float* chans[2] = { chan0.data(), chan1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;

               // The rebuild goes through the same per-frame trigger the main
               // loop runs (WP5b: revision is the only change signal). It is
               // polled before every block, so the 39 blocks without an edit
               // are 39 no-op frames: exactly one rebuild over the whole run,
               // and the edit moves revision by exactly one.
               float beforePeak = 0.0f, afterPeak = 0.0f;
               const unsigned long long rebuildsBefore = gAudioTopologyRebuildCount;
               uint64_t otherLane = 0;
               bool bumpedOnce = false, noOpFrameQuiet = false;
               int triggered = 0;
               for (int b = 0; b < 40; b++)
               {
                  if (b == 20)
                  {
                     // An edit on a *different* lane - the clip under the
                     // playhead is untouched.
                     const uint64_t rev = gArrange.revision;
                     otherLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
                     bumpedOnce = gArrange.revision == rev + 1;
                  }
                  if (ArrangeAudioRebuildIfStale())
                     triggered++;
                  if (b == 20)
                     noOpFrameQuiet = !ArrangeAudioRebuildIfStale(); // same frame again: nothing changed
                  AudioEngine::Instance().ProcessOffline(buffer);
                  for (int i = 0; i < kBlock; i++)
                  {
                     if (b == 19) beforePeak = std::max(beforePeak, std::fabs(chan0[i]));
                     if (b == 20) afterPeak = std::max(afterPeak, std::fabs(chan0[i]));
                  }
               }
               const unsigned long long rebuilds = gAudioTopologyRebuildCount - rebuildsBefore;
               const bool fOk = beforePeak > 0.05f && afterPeak > 0.05f &&
                                std::fabs(afterPeak - beforePeak) < 0.25f * beforePeak &&
                                bumpedOnce && triggered == 1 && rebuilds == 1 && noOpFrameQuiet;
               printf("arrange audio rebuild mid-clip: %s (before %.4f, after %.4f, revision +1 %d, rebuilds %llu over 40 polled frames, no-op frame quiet %d)\n",
                      fOk ? "OK" : "FAIL", beforePeak, afterPeak, (int)bumpedOnce, rebuilds, (int)noOpFrameQuiet);
               allOk = allOk && fOk;
               Arrange::RemoveLane(gArrange, otherLane);
            }

            // --- H. Lane mix strip: mute, solo, pan, gain -------------------
            // The header's S / M / pan / gain, read by the schedule at
            // rebuild time. Mixer's rules: a muted lane is silent, and any
            // soloed audio lane silences every unsoloed one.
            {
               clearClips();
               addClip(0.0, 4.0, true);
               Arrange::Lane* ln = Arrange::FindLane(gArrange, laneId);
               auto bump = [&]() { gArrange.revision++; };

               const std::vector<float> ref = render(0.0, 200);
               const float refL = peakOverBeats(ref, 0.0, 0.2, 1.0);
               const float refR = peakOverBeats(lastChan1, 0.0, 0.2, 1.0);

               ln->mute = true; bump();
               const float muted = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->mute = false; bump();

               const uint64_t soloLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
               Arrange::FindLane(gArrange, soloLane)->solo = true; bump();
               const float othersSoloed = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->solo = true; bump();
               const float bothSoloed = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               Arrange::RemoveLane(gArrange, soloLane);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->solo = false; bump();

               ln->pan = -1.0f; bump();
               const float hardL = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               const float hardLR = peakOverBeats(lastChan1, 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->pan = 0.0f;
               ln->gainDb = -6.0206f; bump();
               const float halfGain = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->gainDb = 0.0f; bump();

               // Centre is unity per side (equal-power * sqrt2), hard left is
               // sqrt2 on L and nothing on R.
               const bool hOk = refL > 0.05f && std::fabs(refL - refR) < 0.01f * refL && muted < 1e-6f &&
                                othersSoloed < 1e-6f && bothSoloed > 0.9f * refL &&
                                std::fabs(hardL - refL * (float)M_SQRT2) < 0.02f * refL && hardLR < 1e-4f &&
                                std::fabs(halfGain - 0.5f * refL) < 0.02f * refL;
               printf("arrange audio lane mix: %s (centre L %.4f R %.4f, muted %.8f, other soloed %.8f, both soloed %.4f, "
                      "hard-left L %.4f R %.8f, -6dB %.4f)\n",
                      hOk ? "OK" : "FAIL", refL, refR, muted, othersSoloed, bothSoloed, hardL, hardLR, halfGain);
               allOk = allOk && hOk;
            }

            // --- G. Mode resets to Canvas on New and on Open ---------------
            {
               gAudioMode = AudioMode::Timeline;
               NewPatch();
               const bool afterNew = gAudioMode == AudioMode::Canvas;

               gAudioMode = AudioMode::Timeline;
               const bool afterOpen = !LoadPatchFrom("/nonexistent-arrangeaudiotest.ifp") ||
                                      gAudioMode == AudioMode::Canvas;
               // A failed open must NOT reset the mode - it never became a new
               // document - so re-check with a real round trip through a file
               // this fixture writes itself.
               bool afterRealOpen = true;
               {
                  const std::string path = "/tmp/infinite-arrangeaudiotest.ifp";
                  SpawnNode("Oscillator", "Synthesizers", 0.0f, 0.0f); // a patch with no nodes will not save
                  const bool saved = SavePatchTo(path);
                  if (saved)
                  {
                     gAudioMode = AudioMode::Timeline;
                     const bool loaded = LoadPatchFrom(path);
                     afterRealOpen = loaded && gAudioMode == AudioMode::Canvas;
                     if (!afterRealOpen)
                        printf("  [diag] saved %d loaded %d status '%s'\n", (int)saved, (int)loaded, gPatchStatus.c_str());
                     remove(path.c_str());
                  }
                  else
                  {
                     printf("  [diag] save failed: '%s'\n", gPatchStatus.c_str());
                  }
               }
               const bool gOk = afterNew && afterOpen && afterRealOpen;
               printf("arrange audio mode resets: %s (new %d, failed-open %d, open %d)\n",
                      gOk ? "OK" : "FAIL", (int)afterNew, (int)afterOpen, (int)afterRealOpen);
               allOk = allOk && gOk;
            }
         }

         tr.SetOfflineMode(false);
         tr.SetPlaying(true);
         gAudioMode = savedMode;
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         RebuildAudioTopology();

         printf("arrange audio test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Arrangement Timeline Audio Sample timing, measured on real rendered
      // audio. A synthetic 100 BPM click track (44.1 kHz stereo, so the
      // file/engine rate conversion is exercised too, and 10 s long so it is
      // not an exact loop length) goes through the real drop path
      // (ArrangeImportMediaFile + ArrangePollMediaImports), then every case
      // renders through RebuildAudioTopology + ProcessOffline and compares
      // each click's onset in the output against where the box says it
      // belongs. INFINITE_ARRANGESAMPLETEST=<dir> also writes the renders
      // there as WAVs for inspection.
      if (getenv("INFINITE_ARRANGESAMPLETEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         const std::string outDir = getenv("INFINITE_ARRANGESAMPLETEST");

         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kFileSr = 44100.0;
         const double kFileBpm = 100.0;
         const double kFileSeconds = 10.0;

         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();
         const AudioMode savedMode = gAudioMode;

         // Click track: a 6 ms 2 kHz burst on every beat, left and right
         // slightly different so a channel swap would show.
         const std::string wavPath = TmpPath("infinite_arrangesample_click.wav");
         {
            const int frames = (int)(kFileSeconds * kFileSr);
            std::vector<float> inter((size_t)frames * 2, 0.0f);
            const double beatSec = 60.0 / kFileBpm;
            for (int k = 0; k * beatSec < kFileSeconds; k++)
            {
               const int f0 = (int)std::llround(k * beatSec * kFileSr);
               for (int j = 0; j < (int)(0.006 * kFileSr) && f0 + j < frames; j++)
               {
                  const double env = std::exp(-(double)j / (0.0015 * kFileSr));
                  const float v = (float)(0.8 * env * std::sin(2.0 * 3.14159265358979 * 2000.0 * j / kFileSr));
                  inter[(size_t)(f0 + j) * 2] = v;
                  inter[(size_t)(f0 + j) * 2 + 1] = 0.9f * v;
               }
            }
            AudioRecordings::WriteWav(wavPath, inter.data(), frames, kFileSr, 2);
         }

         const uint64_t revBefore = gArrange.revision;
         gArrange = Arrange::Model();
         gArrange.revision = revBefore + 1;
         const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);

         gAudioMode = AudioMode::Timeline;
         tr.SetTempo(120.0f);
         Arrange::gSampleLiveTempoBpm = 120.0;
         tr.SetLoop(false, 0.0, 0.0);

         auto waitImports = [&]()
         {
            for (int i = 0; i < 500 && !gArrangePendingImports.empty(); i++)
            {
               ArrangePollMediaImports();
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return gArrangePendingImports.empty();
         };

         ArrangeImportMediaFile(wavPath, laneId, 0, Arrange::ImportMediaKind::Audio);
         const bool imported = waitImports();
         uint64_t clipId = 0;
         if (const Arrange::Lane* ln = Arrange::FindLane(gArrange, laneId))
            if (!ln->clips.empty())
               clipId = ln->clips.front().id;
         Arrange::Clip* c0 = Arrange::FindClip(gArrange, clipId);
         const bool importOk = imported && c0 != nullptr && !c0->importPending;
         printf("arrange sample import: %s\n", importOk ? "OK" : "FAIL");
         allOk = allOk && importOk;

         if (importOk)
         {
            // --- Estimate + drop defaults ------------------------------------
            {
               const double beats = Arrange::TicksToBeats(c0->length);
               const double expectBeats = kFileSeconds * kFileBpm / 60.0;
               const bool ok = std::fabs(c0->sampleBpm - kFileBpm) < 0.6 && c0->origBpm > 0.0f && c0->syncToTempo &&
                               std::fabs(beats - expectBeats) < 0.01;
               printf("arrange sample estimate: %s (sampleBpm %.2f, detected %.2f, sync %d, box %.3f beats, expected %.3f)\n",
                      ok ? "OK" : "FAIL", (double)c0->sampleBpm, (double)c0->origBpm, (int)c0->syncToTempo, beats, expectBeats);
               allOk = allOk && ok;
            }

            tr.SetPlaying(true);
            tr.SetOfflineMode(true, kSr);
            int cookFrame = 2000000;
            uint64_t lastDriftReseeks = 0;
            auto render = [&](double startBeat, double seconds, const char* name)
            {
               Arrange::gSampleLiveTempoBpm = (double)tr.Tempo();
               RebuildAudioTopology();
               cookFrame++;
               for (GraphNode& gn : gNodes)
                  gn.node->CookIfNeeded(cookFrame);
               // AudioNodeOfAny, not dynamic_cast: the sample player's
               // AudioNode lives inside its AudioFileNode, and a render that
               // skips preparing it runs at the wrong file/engine rate.
               for (GraphNode& gn : gNodes)
                  if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(startBeat);
               std::vector<float> ch0((size_t)kBlock), ch1((size_t)kBlock);
               float* chans[2] = { ch0.data(), ch1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;
               std::vector<float> inter;
               const uint64_t reseeksBefore = gClipSampleDriftReseeks.load();
               const int blocks = (int)std::ceil(seconds * kSr / kBlock);
               for (int b = 0; b < blocks; b++)
               {
                  ArrangeAudioRebuildIfStale();
                  AudioEngine::Instance().ProcessOffline(buffer);
                  for (int i = 0; i < kBlock; i++)
                  {
                     inter.push_back(ch0[(size_t)i]);
                     inter.push_back(ch1[(size_t)i]);
                  }
               }
               lastDriftReseeks = gClipSampleDriftReseeks.load() - reseeksBefore;
               if (!outDir.empty())
                  AudioRecordings::WriteWav(outDir + "/" + name + ".wav", inter.data(),
                                            (int)(inter.size() / 2), kSr, 2);
               return inter;
            };

            // Click times (seconds from render start), left channel: each
            // click is a group of samples over 0.03 separated by >= 150 ms
            // under 0.01, timed at its loudest sample. The peak, not the
            // first sample over a threshold, so a stretcher's windowed
            // pre-echo or the 2 ms clip-edge declick ramp can't bias it.
            auto onsets = [&](const std::vector<float>& inter)
            {
               std::vector<double> out;
               const long long frames = (long long)(inter.size() / 2);
               long long i = 0;
               while (i < frames)
               {
                  if (std::fabs(inter[(size_t)i * 2]) < 0.03f) { i++; continue; }
                  long long peakAt = i, lastLoud = i;
                  float peak = 0.0f;
                  for (long long j = i; j < frames && j - lastLoud < (long long)(0.15 * kSr); j++)
                  {
                     const float v = std::fabs(inter[(size_t)j * 2]);
                     if (v >= 0.01f) lastLoud = j;
                     if (v > peak) { peak = v; peakAt = j; }
                  }
                  out.push_back((double)peakAt / kSr);
                  i = lastLoud + (long long)(0.15 * kSr);
               }
               return out;
            };

            // Compares measured onsets against the expected times inside
            // [0, until) seconds; reports count and worst error.
            auto check = [&](const char* label, const std::vector<float>& inter,
                             const std::vector<double>& expected, double tolMs)
            {
               // Same end cut-off expectedClicks applies.
               const double renderSeconds = (double)(inter.size() / 2) / kSr;
               std::vector<double> got;
               for (double g : onsets(inter))
                  if (g < renderSeconds - 0.02)
                     got.push_back(g);
               double worst = 0.0;
               int matched = 0;
               for (double e : expected)
               {
                  double best = 1e9;
                  for (double g : got)
                     best = std::min(best, std::fabs(g - e));
                  if (best < 0.05)
                     matched++;
                  // A click right on a clip's start edge goes through the
                  // engine's 2 ms declick ramp, which moves its peak later.
                  const bool onEdge = e < 0.003;
                  worst = std::max(worst, onEdge ? std::max(0.0, best - 0.002) : best);
               }
               const bool countOk = got.size() == expected.size();
               const bool ok = countOk && matched == (int)expected.size() && worst * 1000.0 <= tolMs &&
                               lastDriftReseeks == 0;
               printf("arrange sample %s: %s (expected %d clicks, got %d, worst error %.2f ms, tol %.1f, drift reseeks %llu)\n",
                      label, ok ? "OK" : "FAIL", (int)expected.size(), (int)got.size(), worst * 1000.0, tolMs,
                      (unsigned long long)lastDriftReseeks);
               if (!ok)
               {
                  printf("  [diag] got:");
                  for (size_t i = 0; i < got.size() && i < 40; i++) printf(" %.4f", got[i]);
                  printf("\n  [diag] expected:");
                  for (size_t i = 0; i < expected.size() && i < 40; i++) printf(" %.4f", expected[i]);
                  printf("\n");
               }
               allOk = allOk && ok;
            };

            // Expected click times for a render starting at `startBeat` of
            // `seconds`: clicks sit at source beats k (source second k*0.6),
            // placed on the timeline at clipStart + (srcSec - offset) *
            // effBpm / 60 beats, inside the box only.
            auto expectedClicks = [&](uint64_t id, double startBeat, double seconds)
            {
               std::vector<double> out;
               const Arrange::Clip* c = Arrange::FindClip(gArrange, id);
               const double tempo = (double)tr.Tempo();
               const double eff = Arrange::SampleSourceBpm(c->syncToTempo, c->sampleBpm, tempo);
               for (int k = 0; k * 60.0 / kFileBpm < kFileSeconds; k++)
               {
                  const double srcSec = k * 60.0 / kFileBpm - c->sourceOffsetSeconds;
                  if (srcSec < -1e-9) continue;
                  const double beat = Arrange::TicksToBeats(c->start) + srcSec * eff / 60.0;
                  if (beat < Arrange::TicksToBeats(c->start) - 1e-9 || beat >= Arrange::TicksToBeats(c->End()) - 1e-6)
                     continue;
                  const double t = (beat - startBeat) * 60.0 / tempo;
                  if (t >= -1e-9 && t < seconds - 0.02)
                     out.push_back(std::max(0.0, t));
               }
               return out;
            };

            // Click peak sits ~0.125 ms into the synthetic click. Direct
            // (unstretched) reads are interpolation-exact; WSOLA may move a
            // transient by up to its +/-128-frame alignment search.
            const double tolDirect = 0.5;
            const double tol = 6.0;

            // Synced at 120 (sample is 100): stretched to 1.2x, beat k at k*0.5 s.
            tr.SetTempo(120.0f);
            {
               auto x = render(0.0, 8.0, "synced_120");
               check("synced @120", x, expectedClicks(clipId, 0.0, 8.0), tol);
            }
            // Synced at 140: tempo change alone moves every click.
            tr.SetTempo(140.0f);
            {
               auto x = render(0.0, 7.0, "synced_140");
               check("synced @140", x, expectedClicks(clipId, 0.0, 7.0), tol);
            }
            // Play from mid-clip.
            {
               auto x = render(5.3, 4.0, "synced_140_from_5.3");
               check("synced @140 from beat 5.3", x, expectedClicks(clipId, 5.3, 4.0), tol);
            }
            // Sync off at 120: box rescales to keep the same audio, and the
            // audio plays at native speed (one click per 0.6 s).
            tr.SetTempo(120.0f);
            Arrange::gSampleLiveTempoBpm = 120.0;
            {
               const Arrange::Tick before = Arrange::FindClip(gArrange, clipId)->length;
               ArrangeSetSampleSync(clipId, false);
               const Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
               const bool ok = !c->syncToTempo && std::llabs(c->length - (Arrange::Tick)std::llround(before * 120.0 / 100.0)) <= 1;
               printf("arrange sample sync off keeps audio in box: %s (%.3f -> %.3f beats)\n", ok ? "OK" : "FAIL",
                      Arrange::TicksToBeats(before), Arrange::TicksToBeats(c->length));
               allOk = allOk && ok;
               auto x = render(0.0, 11.0, "unsynced_120");
               check("unsynced @120 (native speed, whole file)", x, expectedClicks(clipId, 0.0, 11.0), tolDirect);
            }
            // Unsynced, Sample BPM typed: must change nothing audible.
            {
               const Arrange::Tick before = Arrange::FindClip(gArrange, clipId)->length;
               ArrangeSetSampleBpm(clipId, 50.0f);
               const bool ok = Arrange::FindClip(gArrange, clipId)->length == before;
               printf("arrange sample unsynced BPM edit leaves box: %s\n", ok ? "OK" : "FAIL");
               allOk = allOk && ok;
               auto x = render(0.0, 11.0, "unsynced_120_bpm50");
               check("unsynced @120 after Sample BPM edit", x, expectedClicks(clipId, 0.0, 11.0), tolDirect);
            }
            // Unsynced at 150: native speed still, box fixed at 20 beats =
            // 8 s, so the file's last 2 s are cut at the box end.
            tr.SetTempo(150.0f);
            {
               auto x = render(0.0, 11.0, "unsynced_150");
               check("unsynced @150 (tail cut at box end)", x, expectedClicks(clipId, 0.0, 11.0), tolDirect);
            }
            // Back to synced, then Sample BPM 50: the clip now claims to be
            // 50 BPM, so at 120 it plays 2.4x and the box halves to 8.33 beats.
            tr.SetTempo(120.0f);
            Arrange::gSampleLiveTempoBpm = 120.0;
            {
               ArrangeSetSampleSync(clipId, true);   // eff 120 -> 50
               ArrangeSetSampleBpm(clipId, 100.0f); // back to the real tempo
               const Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
               const double beats = Arrange::TicksToBeats(c->length);
               const bool ok = std::fabs(beats - 16.6667) < 0.02;
               printf("arrange sample sync round trip restores box: %s (%.3f beats)\n", ok ? "OK" : "FAIL", beats);
               allOk = allOk && ok;
               ArrangeSetSampleBpm(clipId, 50.0f);
               auto x = render(0.0, 8.0, "synced_120_bpm50");
               check("synced @120, Sample BPM 50 (2.4x, click per half beat)", x, expectedClicks(clipId, 0.0, 8.0), tol);
               ArrangeSetSampleBpm(clipId, 100.0f);
            }
            // Pitch +7 st while synced: time-preserving, clicks stay put.
            {
               Arrange::FindClip(gArrange, clipId)->pitch = 7.0f;
               gArrange.revision++;
               auto x = render(0.0, 8.0, "synced_120_pitch7");
               check("synced @120, pitch +7 (timing unchanged)", x, expectedClicks(clipId, 0.0, 8.0), tol);
               Arrange::FindClip(gArrange, clipId)->pitch = 0.0f;
               gArrange.revision++;
            }
            // Split at beat 6.5 (clone node for the right half, like the
            // blade tool), then a paste of the whole original at beat 20.
            {
               uint64_t rightId = 0;
               Arrange::Split(gArrange, clipId, Arrange::BeatsToTicks(6.5), &rightId);
               ArrangeRespawnCloneNode(rightId);
               Arrange::Clip pasted = *Arrange::FindClip(gArrange, clipId);
               pasted.id = 0;
               pasted.start = Arrange::BeatsToTicks(20.0);
               pasted.length = Arrange::BeatsToTicks(10.0);
               pasted.sourceOffsetSeconds = 0.0f;
               uint64_t pasteId = 0;
               Arrange::PlaceOverwrite(gArrange, laneId, pasted, &pasteId);
               ArrangeRespawnCloneNode(pasteId);
               // The crash path: rebuild repeatedly while the clones decode.
               for (int i = 0; i < 20; i++)
               {
                  gArrange.revision++;
                  ArrangeAudioRebuildIfStale();
               }
               const bool clonesOk = waitImports() && Arrange::FindClip(gArrange, rightId) != nullptr &&
                                     Arrange::FindClip(gArrange, pasteId) != nullptr;
               printf("arrange sample split + paste clones decoded: %s\n", clonesOk ? "OK" : "FAIL");
               allOk = allOk && clonesOk;
               if (clonesOk)
               {
                  auto x = render(0.0, 15.5, "split_and_paste");
                  std::vector<double> e = expectedClicks(clipId, 0.0, 15.5);
                  for (double t : expectedClicks(rightId, 0.0, 15.5)) e.push_back(t);
                  for (double t : expectedClicks(pasteId, 0.0, 15.5)) e.push_back(t);
                  std::sort(e.begin(), e.end());
                  check("split + paste @120", x, e, tol);
               }
            }

            // Static waveform lines up with the audio: the bucket holding
            // each click's box position has a peak, the buckets between do not.
            {
               ArrangeSyncClipVisuals();
               const auto it = gArrangeSampleStaticWaves.find(clipId);
               bool ok = it != gArrangeSampleStaticWaves.end();
               int hits = 0, clicks = 0;
               if (ok)
               {
                  const ArrangeClipWave& w = it->second;
                  for (double t : expectedClicks(clipId, 0.0, 1000.0))
                  {
                     const double beat = t * 120.0 / 60.0;
                     const int b = (int)(Arrange::BeatsToTicks(beat) / kArrangeWaveBucketTicks);
                     clicks++;
                     if (b >= 0 && b < (int)w.maxv.size() && w.maxv[(size_t)b] > 0.3f)
                        hits++;
                  }
                  ok = clicks > 0 && hits == clicks;
               }
               printf("arrange sample static waveform aligned: %s (%d/%d clicks in their bucket)\n", ok ? "OK" : "FAIL",
                      hits, clicks);
               allOk = allOk && ok;
            }
         }

         tr.SetOfflineMode(false);
         tr.SetPlaying(true);
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         // The paste crash: several topology publishes landing inside one
         // real audio callback used to free a ProcessList the callback was
         // still walking. Hammer SetTopology with the device running and the
         // Sample clips playing; a regression shows up as a crash (or an ASan
         // report), not as a FAIL line.
         if (AudioEngine::Instance().SampleRate() > 0.0 || StartAudioEngine(gAudioStartError))
         {
            gAudioMode = AudioMode::Timeline;
            RebuildAudioTopology();
            tr.SeekBeats(0.0);
            for (int i = 0; i < 400; i++)
            {
               RebuildAudioTopology();
               if (i % 8 == 0)
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AudioEngine::Instance().PumpMainThread();
            printf("arrange sample live rebuild stress: OK (400 publishes under a running device at %.0f Hz)\n",
                   AudioEngine::Instance().SampleRate());
            if (!hadEngine)
            {
               AudioEngine::Instance().Stop();
               tr.NotifyAudioEngineStopped();
            }
         }
         else
            printf("arrange sample live rebuild stress: SKIP (no audio device)\n");
         gAudioMode = savedMode;
         RebuildAudioTopology();
         printf("arrange sample test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Arrangement Timeline Sample through the REAL export path (symptom
      // "offline render/export is untested"): the render queue, the WAV
      // writer, the MP4 take through gArrangeTimelineExportNode and
      // CompositeArrangeTimelineVideo, with audio gated by
      // ArrangeTimelineRoutingActive. A 100 BPM click track synced at 120
      // starts at beat 2 (1.0 s) on an audio lane and a Ramp clip starts at
      // the same beat on a video lane, so in both files the first click and
      // the first non-black frame belong at exactly 1.0 s and every click
      // after it on a 0.5 s grid. The fixture only produces the files
      // (INFINITE_ARRANGESAMPLEEXPORTTEST=<dir>); measuring them is ffmpeg's
      // job, outside the app, so the check can't share the app's arithmetic.
      FrameTest_ARRANGESAMPLEEXPORTTEST(frameId, window);

      // Overhaul WP4: the arrangement video compositor. Lane order (top lane
      // frontmost), skip rules (disabled, unassigned), model opacity, and the
      // geometry-clip cache (no FBO allocation in steady state, per-target
      // keying, eviction, gPanelViewports untouched). Runs whole inside one
      // main-loop frame on fixture-owned targets, so neither the panel nor a
      // render has to be open.
      FrameTest_ARRANGEVIDEOTEST(frameId, window);

      // Overhaul WP5a (docs/plans/arrangement/overhaul-prompt.md): the panel's
      // editing layer, driven through the same helpers the keys, clicks and
      // menus call (ArrangeClickSelect, ArrangeDrag*, ArrangeCopySelection,
      // AddNodeToArrangeTimeline, ...) - never by UI scripting. Checks that
      // selection is by id (survives a lane reorder, a node delete and its
      // undo; a vanished id clears rather than landing on another clip), that
      // group gestures keep the model valid, that `0` and every gesture leave
      // exactly one undo entry (a no-move click none), that the clipboard
      // belongs to its document, and where Add to Timeline puts a clip.
      if (getenv("INFINITE_ARRANGEEDITTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         tr.SetTempo(120.0f);
         tr.Seek(0.0);
         const Arrange::Tick kBar = Arrange::kTicksPerBar;
         std::string why;

         // A clean model with `video` video lanes then `audio` audio lanes.
         auto freshModel = [&](int video, int audio)
         {
            ArrangeEdit([&]()
            {
               while (!gArrange.lanes.empty())
                  Arrange::RemoveLane(gArrange, gArrange.lanes.front().id);
               for (int i = 0; i < video; i++)
                  Arrange::AddLane(gArrange, Arrange::kLaneVideo);
               for (int i = 0; i < audio; i++)
                  Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            });
            gArrangeSel.clear();
            gArrangeSelAnchor = 0;
         };
         auto place = [&](int lane, Arrange::Tick start, Arrange::Tick len, uint64_t uid)
         {
            Arrange::Clip c;
            c.start = start;
            c.length = len;
            c.srcUid = uid;
            uint64_t id = 0;
            ArrangeEdit([&]() { Arrange::PlaceOverwrite(gArrange, gArrange.lanes[lane].id, c, &id); });
            return id;
         };
         auto selIs = [&](std::set<uint64_t> want)
         {
            const std::vector<uint64_t> ids = ArrangeSelectionIds();
            return std::set<uint64_t>(ids.begin(), ids.end()) == want;
         };

         // --- A. Selection by id: lane reorder, node delete, undo -----------
         {
            GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            const uint64_t cubeUid = cube ? cube->uid : 0;
            const int cubeIndex = cube ? cube->index : -1;
            GraphNode* sphere = SpawnNode("Sphere", "3D", 200.0f, 0.0f);
            const uint64_t sphereUid = sphere ? sphere->uid : 0;
            bool aOk = cubeUid != 0 && sphereUid != 0;
            if (aOk)
            {
               freshModel(2, 0);
               const uint64_t laneV1 = gArrange.lanes[0].id;
               const uint64_t laneV2 = gArrange.lanes[1].id;
               const uint64_t c1 = place(0, 0, kBar, cubeUid);
               const uint64_t c2 = place(1, 0, kBar, sphereUid);
               const uint64_t c3 = place(0, kBar * 2, kBar, sphereUid);
               ArrangeClickSelect(c1, false, false);
               ArrangeClickSelect(c2, true, false);
               aOk = selIs({ c1, c2 }) && gArrangeSelAnchor == c2;

               // Reorder: the ids follow their clips to the new lane indices.
               ArrangeEdit([&]() { Arrange::ReorderLane(gArrange, laneV2, 0); });
               aOk = aOk && gArrange.lanes[0].id == laneV2 && selIs({ c1, c2 }) &&
                     Arrange::Find(gArrange, c1).lane == Arrange::LaneIndex(gArrange, laneV1) &&
                     Arrange::Find(gArrange, c2).lane == Arrange::LaneIndex(gArrange, laneV2);

               // Node delete: c1 goes offline (srcUid 0), stays selected.
               RemoveNodeByIndex(cubeIndex);
               aOk = aOk && selIs({ c1, c2 }) && Arrange::FindClip(gArrange, c1) != nullptr &&
                     Arrange::FindClip(gArrange, c1)->srcUid == 0;

               // Undo (a graph entry): the same clips, the link restored.
               Undo();
               aOk = aOk && selIs({ c1, c2 }) && Arrange::FindClip(gArrange, c1) != nullptr &&
                     Arrange::FindClip(gArrange, c1)->srcUid == cubeUid && FindNodeByUid(cubeUid) != nullptr &&
                     Arrange::Find(gArrange, c3).Valid();

               // An id that stops resolving clears; it never retargets.
               ArrangeClickSelect(c3, false, false);
               ArrangeDuplicateSelection();
               const std::vector<uint64_t> dup = ArrangeSelectionIds();
               aOk = aOk && dup.size() == 1 && dup[0] != c3;
               Undo();
               aOk = aOk && ArrangeSelectionIds().empty() && gArrangeSelAnchor == 0;
               aOk = aOk && Arrange::Validate(gArrange, &why);
            }
            printf("arrange edit select by id: %s\n", aOk ? "OK" : "FAIL");
            allOk = allOk && aOk;
         }

         // --- B. Group move, duplicate, delete, edge trim keep Validate -----
         {
            freshModel(2, 1);
            const uint64_t g1 = place(0, 0, kBar, 0);
            const uint64_t g2 = place(1, kBar, kBar, 0);
            const uint64_t x = place(0, kBar * 8, kBar, 0);
            const uint64_t aud = place(2, 0, kBar, 0);
            ArrangeClickSelect(g1, false, false);
            ArrangeClickSelect(g2, true, false);
            bool bOk = ArrangeGroupSelection();
            const uint64_t gid = Arrange::FindClip(gArrange, g1)->groupId;
            bOk = bOk && gid != 0 && Arrange::FindClip(gArrange, g2)->groupId == gid;

            // A plain click on one member selects the whole group; Alt-click one.
            ArrangeClickSelect(g2, false, true);
            bOk = bOk && selIs({ g2 });
            ArrangeClickSelect(g1, false, false);
            bOk = bOk && selIs({ g1, g2 });

            // Move: the whole group, one undo entry.
            size_t undoBefore = gUndoStack.size();
            ArrangeDragBegin(kArrangeDragMove, g1, Arrange::kEdgeStart, 0);
            ArrangeDragUpdate(kBar, 0);
            ArrangeDragUpdate(kBar * 3, 0);
            const bool pushed = ArrangeDragEnd();
            bOk = bOk && pushed && gUndoStack.size() == undoBefore + 1 &&
                  Arrange::FindClip(gArrange, g1)->start == kBar * 3 &&
                  Arrange::FindClip(gArrange, g2)->start == kBar * 4 && Arrange::Validate(gArrange, &why);

            // A lane delta that would put a member on the audio lane (or off
            // the end) is refused as a whole; the time delta still applies.
            ArrangeDragBegin(kArrangeDragMove, g1, Arrange::kEdgeStart, 0);
            ArrangeDragUpdate(kBar, 1);
            ArrangeDragEnd();
            bOk = bOk && Arrange::Find(gArrange, g1).lane == 0 && Arrange::Find(gArrange, g2).lane == 1 &&
                  Arrange::FindClip(gArrange, g1)->start == kBar * 4 && Arrange::FindClip(gArrange, aud)->start == 0 &&
                  Arrange::Validate(gArrange, &why);

            // Group edge trim: only the member flush with the end moves.
            ArrangeDragBegin(kArrangeDragGroupEdge, g2, Arrange::kEdgeEnd, 0);
            ArrangeDragUpdate(kBar * 5 + kBar / 2, 0);
            ArrangeDragEnd();
            bOk = bOk && Arrange::FindClip(gArrange, g1)->length == kBar &&
                  Arrange::FindClip(gArrange, g2)->End() == kBar * 5 + kBar / 2 && Arrange::Validate(gArrange, &why);

            // Duplicate: a new block, its own new group.
            ArrangeClickSelect(g1, false, false);
            bOk = bOk && ArrangeDuplicateSelection();
            const std::vector<uint64_t> copies = ArrangeSelectionIds();
            bOk = bOk && copies.size() == 2 && Arrange::Validate(gArrange, &why);
            if (copies.size() == 2)
            {
               const uint64_t ng = Arrange::FindClip(gArrange, copies[0])->groupId;
               bOk = bOk && ng != 0 && ng != gid && Arrange::FindClip(gArrange, copies[1])->groupId == ng;
            }

            // Delete a group by clicking one member: both go.
            if (!copies.empty())
               ArrangeClickSelect(copies[0], false, false);
            bOk = bOk && ArrangeDeleteSelection() && Arrange::Validate(gArrange, &why);
            for (uint64_t id : copies)
               bOk = bOk && !Arrange::Find(gArrange, id).Valid();
            bOk = bOk && Arrange::Find(gArrange, g1).Valid() && Arrange::Find(gArrange, x).Valid();

            // Ungroup.
            ArrangeClickSelect(g1, false, false);
            bOk = bOk && ArrangeUngroupSelection() && Arrange::FindClip(gArrange, g1)->groupId == 0 &&
                  Arrange::FindClip(gArrange, g2)->groupId == 0 && Arrange::Validate(gArrange, &why);
            printf("arrange edit group ops: %s\n", bOk ? "OK" : "FAIL");
            allOk = allOk && bOk;
         }

         // --- C. `0` toggles enabled, undoably ------------------------------
         {
            freshModel(1, 0);
            const uint64_t a = place(0, 0, kBar, 0);
            const uint64_t b = place(0, kBar, kBar, 0);
            ArrangeClickSelect(a, false, false);
            ArrangeClickSelect(b, true, false);
            const size_t undoBefore = gUndoStack.size();
            bool cOk = ArrangeToggleEnabledSelection() && gUndoStack.size() == undoBefore + 1 &&
                       !Arrange::FindClip(gArrange, a)->enabled && !Arrange::FindClip(gArrange, b)->enabled;
            Undo();
            cOk = cOk && Arrange::FindClip(gArrange, a)->enabled && Arrange::FindClip(gArrange, b)->enabled;
            // Mixed selection: one press disables all of it.
            ArrangeEdit([&]() { Arrange::SetEnabled(gArrange, { a }, Arrange::kDisable); });
            cOk = cOk && ArrangeToggleEnabledSelection() && !Arrange::FindClip(gArrange, b)->enabled;
            cOk = cOk && ArrangeToggleEnabledSelection() && Arrange::FindClip(gArrange, a)->enabled &&
                  Arrange::FindClip(gArrange, b)->enabled && Arrange::Validate(gArrange, &why);
            printf("arrange edit enable toggle: %s\n", cOk ? "OK" : "FAIL");
            allOk = allOk && cOk;
         }

         // --- D. A gesture that changes nothing pushes nothing --------------
         {
            freshModel(1, 0);
            const uint64_t a = place(0, 0, kBar, 0);
            place(0, kBar * 2, kBar, 0);
            ArrangeClickSelect(a, false, false);
            const size_t undoBefore = gUndoStack.size();
            const uint64_t revBefore = gArrange.revision;
            // A click: begin, no mouse movement, release.
            ArrangeDragBegin(kArrangeDragMove, a, Arrange::kEdgeStart, 0);
            bool dOk = !ArrangeDragEnd();
            // A drag that goes away and comes back.
            ArrangeDragBegin(kArrangeDragMove, a, Arrange::kEdgeStart, 0);
            ArrangeDragUpdate(kBar / 2, 0);
            ArrangeDragUpdate(0, 0);
            dOk = dOk && !ArrangeDragEnd();
            // A trim that goes nowhere, and an empty popup-field gesture.
            ArrangeDragBegin(kArrangeDragTrimEnd, a, Arrange::kEdgeEnd, kBar);
            ArrangeDragUpdate(kBar + kBar / 4, 0);
            ArrangeDragUpdate(kBar, 0);
            dOk = dOk && !ArrangeDragEnd();
            ArrangeGestureBegin();
            dOk = dOk && !ArrangeGestureEnd();
            dOk = dOk && gUndoStack.size() == undoBefore && Arrange::FindClip(gArrange, a)->start == 0 &&
                  Arrange::FindClip(gArrange, a)->length == kBar && gArrange.revision >= revBefore;
            printf("arrange edit no-move click: %s (undo %zu -> %zu)\n", dOk ? "OK" : "FAIL", undoBefore,
                   gUndoStack.size());
            allOk = allOk && dOk;
         }

         // --- E. Clipboard: paste at the playhead; cleared on New and Open --
         {
            freshModel(2, 0);
            const uint64_t a = place(0, 0, kBar, 0);
            const uint64_t b = place(1, kBar, kBar, 0);
            ArrangeClickSelect(a, false, false);
            ArrangeClickSelect(b, true, false);
            ArrangeGroupSelection();
            ArrangeClickSelect(a, false, false);
            bool eOk = ArrangeCopySelection() && gArrangeClipboard.items.size() == 2;
            eOk = eOk && ArrangePasteAt(kBar * 4);
            const std::vector<uint64_t> pasted = ArrangeSelectionIds();
            eOk = eOk && pasted.size() == 2 && Arrange::Validate(gArrange, &why);
            if (pasted.size() == 2)
            {
               const Arrange::Clip* p0 = Arrange::FindClip(gArrange, pasted[0]);
               const Arrange::Clip* p1 = Arrange::FindClip(gArrange, pasted[1]);
               const Arrange::Tick lo = std::min(p0->start, p1->start);
               eOk = eOk && lo == kBar * 4 && p0->groupId != 0 && p0->groupId == p1->groupId &&
                     p0->groupId != Arrange::FindClip(gArrange, a)->groupId;
            }
            // An undo is not a new document: the clipboard stays.
            Undo();
            eOk = eOk && !gArrangeClipboard.items.empty();
            // Open is: save, reload, and the clipboard is gone.
            const std::string path = TmpPath("arrange_edittest_open.inf");
            SavePatchTo(path);
            LoadPatchFrom(path);
            std::remove(path.c_str());
            eOk = eOk && !ArrangePasteAt(0) && gArrangeClipboard.items.empty() && gArrangeSel.empty();
            // New is too.
            freshModel(1, 0);
            place(0, 0, kBar, 0);
            ArrangeClickSelect(gArrange.lanes[0].clips[0].id, false, false);
            eOk = eOk && ArrangeCopySelection() && !gArrangeClipboard.items.empty();
            NewPatch();
            eOk = eOk && !ArrangePasteAt(0) && gArrangeClipboard.items.empty() && gArrangeSel.empty();
            printf("arrange edit clipboard cleared on new: %s\n", eOk ? "OK" : "FAIL");
            allOk = allOk && eOk;
         }

         // --- F. Add to Timeline picks the lane (and output) ----------------
         {
            NewPatch();
            tr.Seek(0.0);
            freshModel(0, 0);
            GraphNode* video = SpawnNode("Video", "Source", 0.0f, 0.0f);
            const int videoIndex = video ? video->index : -1;
            const uint64_t videoUid = video ? video->uid : 0;
            GraphNode* osc = SpawnNode("Oscillator", "Synthesizers", 300.0f, 0.0f);
            const int oscIndex = osc ? osc->index : -1;
            const uint64_t oscUid = osc ? osc->uid : 0;
            GraphNode* cube = SpawnNode("Cube", "3D", 600.0f, 0.0f);
            const uint64_t cubeUid = cube ? cube->uid : 0;
            bool fOk = videoIndex >= 0 && oscIndex >= 0 && cubeUid != 0;
            if (fOk)
            {
               GraphNode* vgn = FindNodeByIndex(videoIndex);
               fOk = IsNodeVideoCompatible(*vgn) && IsNodeAudioCompatible(*vgn) &&
                     ArrangeLaneTypeForNode(*vgn) == Arrange::kLaneVideo;

               const uint64_t v = AddNodeToArrangeTimeline(videoIndex);                        // natural: video
               const uint64_t va = AddNodeToArrangeTimeline(videoIndex, Arrange::kLaneAudio);  // submenu: audio
               const uint64_t v2 = AddNodeToArrangeTimeline(videoIndex, Arrange::kLaneVideo);  // lands after v
               const uint64_t o = AddNodeToArrangeTimeline(oscIndex);                          // audio only
               const Arrange::Loc lv = Arrange::Find(gArrange, v);
               const Arrange::Loc lva = Arrange::Find(gArrange, va);
               const Arrange::Loc lo = Arrange::Find(gArrange, o);
               fOk = fOk && lv.Valid() && lva.Valid() && lo.Valid() && Arrange::Find(gArrange, v2).Valid();
               if (fOk)
               {
                  const Arrange::Clip* cv = Arrange::FindClip(gArrange, v);
                  const Arrange::Clip* cva = Arrange::FindClip(gArrange, va);
                  const Arrange::Clip* cv2 = Arrange::FindClip(gArrange, v2);
                  const Arrange::Clip* co = Arrange::FindClip(gArrange, o);
                  fOk = gArrange.lanes[lv.lane].type == Arrange::kLaneVideo && cv->srcOutput == 0 &&
                        cv->srcUid == videoUid && cv->length == kBar && cv->start == 0 &&
                        gArrange.lanes[lva.lane].type == Arrange::kLaneAudio && cva->srcOutput == 1 &&
                        cva->srcUid == videoUid &&
                        cv2->start == cv->End() && Arrange::Find(gArrange, v2).lane == lv.lane &&
                        gArrange.lanes[lo.lane].type == Arrange::kLaneAudio && co->srcOutput == 0 &&
                        co->srcUid == oscUid && co->start == cva->End();
               }
               // Canvas Assign picker: by uid, type-checked, no-op pushes nothing.
               const size_t undoBefore = gUndoStack.size();
               fOk = fOk && !ArrangeAssignClipSource(o, cubeUid);             // cube has no audio
               fOk = fOk && !ArrangeAssignClipSource(o, oscUid);              // already that source
               fOk = fOk && gUndoStack.size() == undoBefore;
               fOk = fOk && ArrangeAssignClipSource(v, cubeUid) && gUndoStack.size() == undoBefore + 1 &&
                     Arrange::FindClip(gArrange, v)->srcUid == cubeUid;
               fOk = fOk && Arrange::Validate(gArrange, &why);
            }
            printf("arrange edit add to timeline lane pick: %s\n", fOk ? "OK" : "FAIL");
            allOk = allOk && fOk;
         }

         // --- G. Whole-group Group/Ungroup, blade, add-at-playhead ----------
         {
            freshModel(3, 0);
            auto gidOf = [&](uint64_t id) { return Arrange::FindClip(gArrange, id)->groupId; };
            auto groupAll = [&](std::vector<uint64_t> ids)
            {
               ArrangePruneSelection(); // settle a patch-generation reset first
               gArrangeSel.clear();
               gArrangeSel.insert(ids.begin(), ids.end());
               return ArrangeGroupSelection();
            };
            const uint64_t a = place(0, 0, kBar, 0);
            const uint64_t b = place(0, kBar * 2, kBar, 0);
            const uint64_t c = place(1, 0, kBar, 0);
            const uint64_t d = place(1, kBar * 2, kBar, 0);
            const uint64_t e = place(2, 0, kBar, 0);
            bool gOk = groupAll({ a, b }) && groupAll({ c, d }) && gidOf(a) != gidOf(c);

            // Two groups merge into one; one undo entry.
            size_t undoBefore = gUndoStack.size();
            ArrangeClickSelect(a, false, false);
            ArrangeClickSelect(c, true, false);
            gOk = gOk && ArrangeCanGroupSelection() && ArrangeGroupSelection() &&
                  gUndoStack.size() == undoBefore + 1 && gidOf(a) != 0 && gidOf(a) == gidOf(b) &&
                  gidOf(a) == gidOf(c) && gidOf(a) == gidOf(d) && selIs({ a, b, c, d });
            const bool mergeOk = gOk;

            // Exactly one whole group: nothing to do, nothing pushed, same id.
            const uint64_t g4 = gidOf(a);
            undoBefore = gUndoStack.size();
            ArrangeClickSelect(b, false, false);
            gOk = gOk && !ArrangeCanGroupSelection() && !ArrangeGroupSelection() &&
                  gUndoStack.size() == undoBefore && gidOf(a) == g4;
            const bool noopOk = gOk;

            // A loose clip plus one member of a group: the whole group joins.
            ArrangeUngroupSelection();
            gOk = gOk && gidOf(a) == 0 && gidOf(d) == 0 && groupAll({ a, b, c });
            ArrangeClickSelect(a, false, true); // Alt: this member only
            ArrangeClickSelect(e, true, false);
            gOk = gOk && selIs({ a, e }) && ArrangeGroupSelection() && gidOf(e) != 0 &&
                  gidOf(e) == gidOf(a) && gidOf(e) == gidOf(b) && gidOf(e) == gidOf(c) && gidOf(d) == 0;
            const bool mixOk = gOk;

            // Ungroup from any one member dissolves the whole group.
            ArrangeClickSelect(c, false, true);
            gOk = gOk && ArrangeCanUngroupSelection() && ArrangeUngroupSelection() && gidOf(a) == 0 &&
                  gidOf(b) == 0 && gidOf(c) == 0 && gidOf(e) == 0 && !ArrangeCanUngroupSelection();
            const bool ungroupOk = gOk;

            // Blade on a grouped clip cuts every member at that tick, as one
            // undo entry; the right halves stay in the group.
            gOk = gOk && groupAll({ a, c });
            gArrangeSel.clear();
            undoBefore = gUndoStack.size();
            gOk = gOk && ArrangeBladeSplitAt(a, kBar / 4) && gUndoStack.size() == undoBefore + 1 &&
                  gArrange.lanes[0].clips.size() == 3 && gArrange.lanes[1].clips.size() == 3 &&
                  Arrange::FindClip(gArrange, a)->length == kBar / 4 &&
                  Arrange::FindClip(gArrange, c)->length == kBar / 4 && gArrangeSel.empty();
            if (gOk)
            {
               const Arrange::Clip& r0 = gArrange.lanes[0].clips[1];
               const Arrange::Clip& r1 = gArrange.lanes[1].clips[1];
               gOk = r0.start == kBar / 4 && r1.start == kBar / 4 && r0.groupId == gidOf(a) &&
                     r1.groupId == gidOf(a) && Arrange::Validate(gArrange, &why);
            }
            // Outside the clip (or on an edge) is not a cut.
            gOk = gOk && !ArrangeBladeSplitAt(b, kBar * 2) && !ArrangeBladeSplitAt(b, kBar * 5);
            const bool bladeOk = gOk;

            // Add to Timeline lands in the first one-bar gap at the playhead
            // and asks the panel to scroll to it.
            NewPatch();
            freshModel(1, 0);
            GraphNode* video = SpawnNode("Video", "Source", 0.0f, 0.0f);
            const int videoIndex = video ? video->index : -1;
            const uint64_t videoUid = video ? video->uid : 0;
            gOk = gOk && videoIndex >= 0;
            if (videoIndex >= 0)
            {
               place(0, 0, kBar, videoUid);
               place(0, kBar * 2, kBar, videoUid);
               tr.SeekBeats(2.0); // half a bar in, inside the first clip
               const uint64_t n1 = AddNodeToArrangeTimeline(videoIndex);
               tr.SeekBeats(0.0);
               const uint64_t n2 = AddNodeToArrangeTimeline(videoIndex);
               gOk = gOk && n1 != 0 && n2 != 0 && Arrange::FindClip(gArrange, n1)->start == kBar &&
                     Arrange::FindClip(gArrange, n2)->start == kBar * 3 && gArrangeRevealClipId == n2 &&
                     gArrangeFlashClipId == n2 && Arrange::Validate(gArrange, &why);
            }
            gArrangeRevealClipId = 0;
            gArrangeFlashClipId = 0;
            printf("arrange edit whole groups + blade + add at playhead: %s (merge %d noop %d mix %d ungroup %d blade %d)\n",
                   gOk ? "OK" : "FAIL", mergeOk, noopOk, mixOk, ungroupOk, bladeOk);
            allOk = allOk && gOk;
         }

         if (!why.empty())
            printf("arrange edit validate: %s\n", why.c_str());
         gArrangePanelOpen = false;
         NewPatch();
         printf("arrange edit test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // WP6: time display, snap grid, markers, playhead keys, scrub-on-release.
      // Drives the same functions the panel's keys and gestures call.
      FrameTest_ARRANGEMARKERTEST(frameId, window);

      // Overhaul WP7 (docs/plans/arrangement/overhaul-prompt.md): the export
      // queue. Checks the parts of a take that are decided before a single
      // frame or sample is written - the range a kind resolves to, the exact
      // frame and sample budgets that range implies (#2: a 2.4s range must
      // not round up to a 3s file), which terminals the audio comes out of
      // for each source combination, that a take never changes what the user
      // is monitoring, and the queue's own mechanics. Deliberately NOT a
      // full encode: a video take is pumped by the main loop a frame at a
      // time and this fixture lives inside one frame. The audio-only path
      // runs for real when a device can be opened, and says so when not.
      if (getenv("INFINITE_ARRANGERENDERTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         tr.SetTempo(120.0f); // 1 beat = 0.5s, so every expected time below is exact
         tr.Seek(0.0);
         const AudioMode modeBefore = gAudioMode;

         // uid read right after each spawn, not after both: SpawnNode()
         // push_backs onto gNodes, which can reallocate and invalidate every
         // GraphNode* into it - including a pointer from an earlier spawn
         // still held when a later one runs.
         GraphNode* rampGn = SpawnNode("Ramp", "Source", 0.0f, 0.0f);
         const uint64_t rampUid = rampGn != nullptr ? rampGn->uid : 0;
         GraphNode* oscGn = SpawnNode("Oscillator", "Synthesizers", 200.0f, 0.0f);
         const uint64_t oscUid = oscGn != nullptr ? oscGn->uid : 0;
         const bool spawned = rampGn != nullptr && oscGn != nullptr;
         printf("arrange render spawn: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {

            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            const uint64_t vLane = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
            const uint64_t aLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            auto place = [&](uint64_t lane, uint64_t uid, double startBeat, double lenBeats) {
               Arrange::Clip c;
               c.start = Arrange::BeatsToTicks(startBeat);
               c.length = Arrange::BeatsToTicks(lenBeats);
               c.srcUid = uid;
               Arrange::PlaceOverwrite(gArrange, lane, c);
            };
            // Video [0, 6) beats = [0, 3)s, audio [0, 8) beats = [0, 4)s.
            place(vLane, rampUid, 0.0, 6.0);
            place(aLane, oscUid, 0.0, 8.0);
            Arrange::AddMarker(gArrange, Arrange::BeatsToTicks(1.0), "A");
            Arrange::AddMarker(gArrange, Arrange::BeatsToTicks(5.8), "B"); // 2.4s after A
            gArrange.settings.loop.enabled = true;
            gArrange.settings.loop.start = Arrange::BeatsToTicks(2.0);
            gArrange.settings.loop.end = Arrange::BeatsToTicks(4.4); // 1.2s, fractional

            // --- A. Every range kind resolves to the span it names ---------
            struct RangeCase { int kind; double aBeat; double bBeat; const char* what; };
            const RangeCase cases[] = {
               { kArrangeRangeWhole,   0.0, 8.0, "whole" },      // the audio clip is the longest
               { kArrangeRangeLoop,    2.0, 4.4, "loop" },
               { kArrangeRangeMarkers, 1.0, 5.8, "markers" },
               { kArrangeRangeCustom,  3.0, 7.0, "custom" },
            };
            bool aOk = true;
            for (const RangeCase& rc : cases)
            {
               Arrange::Tick ra = 0, rb = 0;
               ArrangeRenderResolveRange(rc.kind, 0, 1, Arrange::BeatsToTicks(3.0),
                                         Arrange::BeatsToTicks(7.0), ra, rb);
               const bool ok = ra == Arrange::BeatsToTicks(rc.aBeat) && rb == Arrange::BeatsToTicks(rc.bBeat);
               if (!ok)
                  printf("arrange render range %s: FAIL (got %.3f..%.3f beats, want %.3f..%.3f)\n", rc.what,
                         Arrange::TicksToBeats(ra), Arrange::TicksToBeats(rb), rc.aBeat, rc.bBeat);
               aOk = aOk && ok;
            }
            // Reversed markers swap rather than producing a negative range.
            {
               Arrange::Tick ra = 0, rb = 0;
               ArrangeRenderResolveRange(kArrangeRangeMarkers, 1, 0, 0, 0, ra, rb);
               aOk = aOk && ra == Arrange::BeatsToTicks(1.0) && rb == Arrange::BeatsToTicks(5.8);
            }
            // An empty range is widened, never handed to the runner as-is.
            {
               Arrange::Tick ra = 0, rb = 0;
               ArrangeRenderResolveRange(kArrangeRangeCustom, 0, 0, Arrange::BeatsToTicks(2.0),
                                         Arrange::BeatsToTicks(2.0), ra, rb);
               aOk = aOk && rb > ra;
            }
            printf("arrange render range kinds: %s\n", aOk ? "OK" : "FAIL");
            allOk = allOk && aOk;

            // --- B. Frame budget is ceil, not whole seconds (#2) -----------
            // The marker range is 2.4s: 72 frames at 30fps, and the old
            // ceil(durationSeconds) * fps would have written 90 (a 3s file).
            const int f30 = ArrangeRenderFrameBudget(2.4, 30);
            const int f60 = ArrangeRenderFrameBudget(2.4, 60);
            const int fLoop = ArrangeRenderFrameBudget(1.2, 25);       // exact, no rounding
            const int fPartial = ArrangeRenderFrameBudget(1.201, 25);  // one frame more
            const int fTiny = ArrangeRenderFrameBudget(0.001, 1);      // never zero frames
            const bool bOk = f30 == 72 && f60 == 144 && fLoop == 30 && fPartial == 31 && fTiny == 1;
            printf("arrange render frame budget: %s (2.4s@30=%d 2.4s@60=%d 1.2s@25=%d 1.201s@25=%d 0.001s@1=%d)\n",
                   bOk ? "OK" : "FAIL", f30, f60, fLoop, fPartial, fTiny);
            allOk = allOk && bOk;

            // --- C. Sample budget rounds to the nearest whole sample -------
            const long long s48 = ArrangeRenderSampleBudget(2.4, 48000.0);
            const long long s441 = ArrangeRenderSampleBudget(2.4, 44100.0);
            const long long sTiny = ArrangeRenderSampleBudget(0.0, 48000.0);
            const bool cOk = s48 == 115200 && s441 == 105840 && sTiny == 1;
            printf("arrange render sample budget: %s (2.4s@48k=%lld 2.4s@44.1k=%lld)\n", cOk ? "OK" : "FAIL",
                   s48, s441);
            allOk = allOk && cOk;

            // --- D. The source matrix picks the right audio terminals ------
            // ArrangeTimelineRoutingActive() is the single gate: Timeline
            // audio must use the timeline's terminals even though the user is
            // monitoring the canvas, and canvas audio must not, even when the
            // take is compositing the timeline's video.
            gAudioMode = AudioMode::Canvas;
            struct MatrixCase { int audio; int video; bool wantTimeline; const char* what; };
            const MatrixCase matrix[] = {
               { kArrangeAudioTimeline, kArrangeVideoTimeline, true,  "timeline A + timeline V" },
               { kArrangeAudioCanvas,   kArrangeVideoTimeline, false, "canvas A + timeline V" },
               { kArrangeAudioTimeline, kArrangeVideoCanvas,   true,  "timeline A + canvas V" },
               { kArrangeAudioCanvas,   kArrangeVideoCanvas,   false, "canvas A + canvas V" },
               { kArrangeAudioNone,     kArrangeVideoTimeline, false, "no audio" },
            };
            bool dOk = true;
            for (const MatrixCase& mc : matrix)
            {
               gOfflineRender.active = true;
               gOfflineRender.arrangeDriven = true;
               gOfflineRender.timelineAudio = mc.audio == kArrangeAudioTimeline;
               gOfflineRender.timelineVideo = mc.video == kArrangeVideoTimeline;
               const bool got = ArrangeTimelineRoutingActive();
               if (got != mc.wantTimeline)
                  printf("arrange render routing %s: FAIL (got %d, want %d)\n", mc.what, got ? 1 : 0,
                         mc.wantTimeline ? 1 : 0);
               dOk = dOk && got == mc.wantTimeline;
            }
            // The audio-only path routes through its own flag, not gOfflineRender's.
            gOfflineRender.active = false;
            gOfflineRender.arrangeDriven = false;
            gOfflineRender.timelineAudio = false;
            gOfflineRender.timelineVideo = false;
            gArrangeWavRender.active = true;
            gArrangeWavRender.timelineAudio = true;
            dOk = dOk && ArrangeTimelineRoutingActive();
            gArrangeWavRender.timelineAudio = false;
            dOk = dOk && !ArrangeTimelineRoutingActive();
            gArrangeWavRender.active = false;
            // And with nothing rendering it is the monitoring mode alone.
            dOk = dOk && !ArrangeTimelineRoutingActive();
            gAudioMode = AudioMode::Timeline;
            dOk = dOk && ArrangeTimelineRoutingActive();
            gAudioMode = AudioMode::Canvas;
            printf("arrange render source matrix: %s\n", dOk ? "OK" : "FAIL");
            allOk = allOk && dOk;

            // --- E. The render dialog's sources --------------------------
            // The Audio/Video source dropdowns are gone: a timeline take is
            // always timeline audio, and it is a movie exactly when the range
            // covers video clips, otherwise a WAV. Calls the shipping helpers
            // rather than a local copy of their logic - the previous version
            // of this test asserted on a private lambda, so it kept passing
            // after the behaviour it described had been deleted.
            bool eOk = true;
            // Audio never follows the monitoring mode any more.
            gAudioMode = AudioMode::Canvas;
            eOk = eOk && ArrangeRenderEffectiveAudioSource() == kArrangeAudioTimeline;
            gAudioMode = AudioMode::Timeline;
            eOk = eOk && ArrangeRenderEffectiveAudioSource() == kArrangeAudioTimeline;
            gAudioMode = AudioMode::Canvas;
            // And the deprecated pinned setting no longer overrides it.
            gArrange.settings.renderAudioSource = kArrangeAudioCanvas;
            eOk = eOk && ArrangeRenderEffectiveAudioSource() == kArrangeAudioTimeline;
            gArrange.settings.renderAudioSource = -1;
            // Video follows the range. The fixture built above has no video
            // lane, so every range is audio-only; a range that covers nothing
            // is audio-only whatever the project holds.
            gArrange.settings.renderVideoSource = kArrangeVideoCanvas; // also ignored now
            eOk = eOk && ArrangeRenderEffectiveVideoSource(0, 0) == kArrangeVideoNone;
            eOk = eOk && ArrangeRenderEffectiveVideoSource(0, Arrange::kPPQ * 64) ==
                             (ArrangeRenderVideoClipsInRange(0, Arrange::kPPQ * 64) > 0
                                  ? kArrangeVideoTimeline
                                  : kArrangeVideoNone);
            gArrange.settings.renderVideoSource = -1;
            printf("arrange render effective sources: %s\n", eOk ? "OK" : "FAIL");
            allOk = allOk && eOk;

            // --- F. Queue mechanics ----------------------------------------
            const std::string tmpDir = std::filesystem::temp_directory_path().string();
            gArrangeRenderQueue.clear();
            gArrangeRenderActiveJobId = 0;
            gArrangeRenderQueueRunning = false;
            auto makeJob = [&](const char* name, int audio, int video, double aBeat, double bBeat) {
               ArrangeRenderJob j;
               j.id = gArrangeRenderNextJobId++;
               j.startTick = Arrange::BeatsToTicks(aBeat);
               j.endTick = Arrange::BeatsToTicks(bBeat);
               j.audioSource = audio;
               j.videoSource = video;
               j.path = tmpDir + "/infinite_wp7_" + name + (video == kArrangeVideoNone ? ".wav" : ".mp4");
               gArrangeRenderQueue.push_back(j);
               return gArrangeRenderQueue.back().id;
            };
            makeJob("whole", kArrangeAudioTimeline, kArrangeVideoTimeline, 0.0, 8.0);
            makeJob("loop", kArrangeAudioTimeline, kArrangeVideoTimeline, 2.0, 4.4);
            const uint64_t emptyId = makeJob("empty", kArrangeAudioTimeline, kArrangeVideoTimeline, 3.0, 3.0);
            const uint64_t bothNoneId = makeJob("none", kArrangeAudioNone, kArrangeVideoNone, 0.0, 4.0);

            // A job with nothing to do fails at the gate instead of arming a
            // take, and the runner moves on rather than stalling the queue.
            ArrangeRenderJob* emptyJob = ArrangeRenderFindJob(emptyId);
            ArrangeRenderJob* noneJob = ArrangeRenderFindJob(bothNoneId);
            const bool emptyRefused = emptyJob != nullptr && !ArrangeRenderBeginJob(*emptyJob) &&
                                      emptyJob->status == kArrangeJobFailed;
            const bool noneRefused = noneJob != nullptr && !ArrangeRenderBeginJob(*noneJob) &&
                                     noneJob->status == kArrangeJobFailed;
            gArrangeRenderActiveJobId = 0;
            printf("arrange render invalid jobs refused: %s (%s / %s)\n",
                   emptyRefused && noneRefused ? "OK" : "FAIL",
                   emptyJob != nullptr ? emptyJob->message.c_str() : "?",
                   noneJob != nullptr ? noneJob->message.c_str() : "?");
            allOk = allOk && emptyRefused && noneRefused;

            // Retry puts a failed job back in line with its counters reset.
            emptyJob->status = kArrangeJobFailed;
            emptyJob->framesDone = 17;
            emptyJob->status = kArrangeJobQueued;
            emptyJob->framesDone = 0;
            emptyJob->message.clear();

            // Cancel All stops the run and marks everything still waiting,
            // and leaves the finished ones alone.
            noneJob->status = kArrangeJobDone;
            gArrangeRenderQueueRunning = true;
            ArrangeRenderCancelAll();
            int cancelled = 0, done = 0, stillQueued = 0;
            for (const ArrangeRenderJob& j : gArrangeRenderQueue)
            {
               if (j.status == kArrangeJobCancelled) cancelled++;
               else if (j.status == kArrangeJobDone) done++;
               else if (j.status == kArrangeJobQueued) stillQueued++;
            }
            const bool fOk = !gArrangeRenderQueueRunning && cancelled == 3 && done == 1 && stillQueued == 0;
            printf("arrange render cancel all: %s (%d cancelled, %d done, %d still queued, running %d)\n",
                   fOk ? "OK" : "FAIL", cancelled, done, stillQueued, gArrangeRenderQueueRunning ? 1 : 0);
            allOk = allOk && fOk;

            // --- G. Two jobs never share an output file --------------------
            const std::string taken = gArrangeRenderQueue[0].path;
            gArrangeRenderQueue[0].status = kArrangeJobQueued; // back in the queue, so it owns its path
            const bool seen = ArrangeRenderPathQueued(taken, 0);
            const bool notMine = !ArrangeRenderPathQueued(taken, gArrangeRenderQueue[0].id);
            const std::string unique = ArrangeRenderUniquePath(taken);
            const bool gOk = seen && notMine && unique != taken && unique.size() > 4 &&
                             unique.compare(unique.size() - 4, 4, ".mp4") == 0 &&
                             !ArrangeRenderPathQueued(unique, 0);
            printf("arrange render unique path: %s (%s -> %s)\n", gOk ? "OK" : "FAIL", taken.c_str(),
                   unique.c_str());
            allOk = allOk && gOk;

            // --- H. A live source in the range refuses the take ------------
            // Off-range hardware must not refuse: that was the whole point of
            // scoping the check to the render range rather than the patch.
            GraphNode* camGn = SpawnNode("Video In", "Source", 400.0f, 0.0f);
            if (camGn != nullptr && camGn->node != nullptr && camGn->node->IsHardwareDriven())
            {
               Arrange::Clip cam;
               cam.start = Arrange::BeatsToTicks(10.0);
               cam.length = Arrange::BeatsToTicks(2.0);
               cam.srcUid = camGn->uid;
               Arrange::PlaceOverwrite(gArrange, vLane, cam);
               const bool inRange =
                  FindHardwareDrivenNodeInArrangeRange(Arrange::BeatsToTicks(10.0), Arrange::BeatsToTicks(12.0),
                                                       true, true) != nullptr;
               const bool outOfRange =
                  FindHardwareDrivenNodeInArrangeRange(Arrange::BeatsToTicks(0.0), Arrange::BeatsToTicks(6.0),
                                                       true, true) == nullptr;
               const bool hOk = inRange && outOfRange;
               printf("arrange render live source scoped to range: %s (in %d, out %d)\n", hOk ? "OK" : "FAIL",
                      inRange ? 1 : 0, outOfRange ? 1 : 0);
               allOk = allOk && hOk;
            }
            else
            {
               printf("arrange render live source scoped to range: SKIP (no hardware-driven node)\n");
            }

            // --- I. An audio-only take, for real, when a device exists -----
            // This is the only end-to-end path a single frame can run: no
            // encoder, no per-frame main-loop pump. Without a device there is
            // nothing to render at (every AudioNode is prepared at the device
            // rate), so it says so rather than failing.
            gArrangeRenderQueue.clear();
            gArrangeRenderActiveJobId = 0;
            gArrangeRenderQueueRunning = false;
            const std::string wavPath = tmpDir + "/infinite_wp7_audio_only.wav";
            std::error_code rmEc;
            std::filesystem::remove(wavPath, rmEc);
            if (AudioEngine::Instance().SampleRate() > 0.0 || StartAudioEngine(gAudioStartError))
            {
               const double devRate = AudioEngine::Instance().SampleRate();
               ArrangeRenderJob j;
               j.id = gArrangeRenderNextJobId++;
               j.startTick = Arrange::BeatsToTicks(1.0);
               j.endTick = Arrange::BeatsToTicks(5.8); // 2.4s
               j.audioSource = kArrangeAudioTimeline;
               j.videoSource = kArrangeVideoNone;
               j.format = 2;
               j.path = wavPath;
               gArrangeRenderQueue.push_back(j);
               gArrangeRenderQueueRunning = true;

               // What the main loop does, without the frames in between. The
               // cap is a hang guard: at a 0.1s budget per tick a 2.4s take
               // needs a handful.
               int ticks = 0;
               while (gArrangeRenderQueueRunning && ticks++ < 2000)
                  ArrangeRenderQueueTick();

               const ArrangeRenderJob& doneJob = gArrangeRenderQueue.back();
               const long long wantSamples = ArrangeRenderSampleBudget(2.4, devRate);
               long long gotSamples = -1;
               std::error_code szEc;
               const auto bytes = (long long)std::filesystem::file_size(wavPath, szEc);
               if (!szEc)
                  gotSamples = (bytes - 44) / 4; // 16-bit stereo after the canonical WAV header
               // +-1 block: the pump writes in whole blocks of
               // OfflineAudioBlockFrames() and the last one is clipped to the
               // budget, so the file is exact - the tolerance is for a writer
               // that pads, not for a pump that overruns.
               const bool iOk = doneJob.status == kArrangeJobDone && gotSamples > 0 &&
                                std::llabs(gotSamples - wantSamples) <= OfflineAudioBlockFrames() &&
                                !ArrangeRenderBusy() && gArrangeRenderActiveJobId == 0;
               printf("arrange render audio-only take: %s (%lld samples, want %lld at %.0f Hz, status %d, %d ticks)\n",
                      iOk ? "OK" : "FAIL", gotSamples, wantSamples, devRate, doneJob.status, ticks);
               allOk = allOk && iOk;
               std::filesystem::remove(wavPath, rmEc);
            }
            else
            {
               printf("arrange render audio-only take: SKIP (no audio device: %s)\n", gAudioStartError.c_str());
            }

            // --- K. A take parks the loop instead of wrapping inside it ----
            // A fractional loop used to make the take re-render the loop body
            // until the frame budget ran out (#3). WP2 moved the wrap into
            // Transport and suspends it for the duration of a take; the
            // user's own loop flag is left alone so it comes back after.
            tr.SetLoop(true, 2.0, 4.4);
            const bool loopOnBefore = tr.LoopEnabled();
            const bool suspendedBefore = tr.LoopSuspended();
            tr.SetOfflineMode(true, 48000.0);
            const bool suspendedDuring = tr.LoopSuspended();
            const bool flagKept = tr.LoopEnabled();
            tr.SetOfflineMode(false);
            const bool suspendedAfter = tr.LoopSuspended();
            const bool kOk = loopOnBefore && !suspendedBefore && suspendedDuring && flagKept &&
                             !suspendedAfter && tr.LoopEnabled();
            printf("arrange render loop parked during take: %s (before %d, during %d, after %d)\n",
                   kOk ? "OK" : "FAIL", suspendedBefore ? 1 : 0, suspendedDuring ? 1 : 0,
                   suspendedAfter ? 1 : 0);
            allOk = allOk && kOk;
            tr.SetLoop(false, 0.0, 0.0);

            // --- J. A take never changes what the user is monitoring -------
            const bool jOk = gAudioMode == modeBefore && !ArrangeRenderBusy() &&
                             !gOfflineRender.arrangeDriven && !gOfflineRender.timelineAudio &&
                             !gOfflineRender.timelineVideo && !gArrangeWavRender.active &&
                             !Transport::Instance().IsOfflineMode();
            printf("arrange render leaves live state alone: %s (mode %d, offline %d)\n", jOk ? "OK" : "FAIL",
                   (int)gAudioMode, Transport::Instance().IsOfflineMode() ? 1 : 0);
            allOk = allOk && jOk;

            // --- L. Renders follow the global audio settings ---------------
            // The popup offers no rate or buffer control; both are read off
            // the live engine, so a job can never ask for something the
            // prepared graph cannot generate.
            const double activeRate = ArrangeRenderActiveSampleRate();
            const double engineRate = AudioEngine::Instance().SampleRate();
            const bool rateFollows =
               activeRate > 0.0 &&
               (engineRate > 0.0 ? std::abs(activeRate - engineRate) < 1.0
                                 : std::abs(activeRate - (gAudioSampleRate > 0.0 ? gAudioSampleRate
                                                                                 : 48000.0)) < 1.0);
            const int blockNow = OfflineAudioBlockFrames();
            const uint32_t devPeriod = Platform::AudioDeviceBufferFrames(gAudioOutputDeviceId);
            const int wantBlock =
               std::clamp(devPeriod > 0 ? (int)devPeriod
                                        : (gAudioBufferFrames > 0 ? gAudioBufferFrames : 512),
                          1, kAudioMaxBlockFrames);
            // The buffer setting has to actually reach the pump: rendering in
            // kAudioMaxBlockFrames slabs latches MixerNode's pan/mute/solo
            // once per 4096 frames instead of once per period.
            const bool blockFollows = blockNow == wantBlock && blockNow <= kAudioMaxBlockFrames &&
                                      blockNow >= 1;
            const bool lOk = rateFollows && blockFollows;
            printf("arrange render follows audio settings: %s (%.0f Hz, %d-frame blocks, "
                   "engine %.0f Hz, setting %d, device period %u)\n",
                   lOk ? "OK" : "FAIL", activeRate, blockNow, engineRate, gAudioBufferFrames,
                   devPeriod);
            allOk = allOk && lOk;

            // R478: a headless job ignores the device and the Settings value,
            // so the same patch pumps the same blocks on every machine.
            {
               const int savedSetting = gAudioBufferFrames;
               const Headless::Mode savedMode = gHeadlessJob.mode;
               gHeadlessJob.mode = Headless::Mode::Render;
               bool fixed = true;
               for (int setting : { 64, 128, 1024 })
               {
                  gAudioBufferFrames = setting;
                  fixed = fixed && OfflineAudioBlockFrames() == kHeadlessAudioBlockFrames;
               }
               gAudioBufferFrames = savedSetting;
               gHeadlessJob.mode = savedMode;
               printf("headless audio block is fixed at %d regardless of settings: %s\n",
                      kHeadlessAudioBlockFrames, fixed ? "OK" : "FAIL");
               allOk = allOk && fixed;
            }

            std::string why;
            if (!Arrange::Validate(gArrange, &why))
            {
               printf("arrange render validate: %s\n", why.c_str());
               allOk = false;
            }
         }

         gArrangeRenderQueue.clear();
         gArrangeRenderActiveJobId = 0;
         gArrangeRenderQueueRunning = false;
         gAudioMode = modeBefore;
         tr.SetTempo(120.0f);
         tr.Seek(0.0);
         NewPatch();
         printf("arrange render test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // ---- WP8: live clip waveforms and video thumbnails ----------------
      // What a frame-4 fixture can decide: the ring's SPSC discipline and its
      // drop-rather-than-overwrite rule, the bucket maths, the cache's
      // shaping / invalidation / eviction rules, and - where a device opens -
      // a real take actually filling a clip's buckets through the audio
      // thread. What it cannot: the drawing, which the owner eyeballs.
      FrameTest_ARRANGEWAVETEST(frameId, window);

      // Per-clip modulation bypass (Arrange::Clip::bypassedModParams): the
      // list the Clip Settings panel builds, and the playhead-driven gate the
      // modulation apply loop reads. Both go through the same two functions
      // the app itself calls, so a green run here means the panel really does
      // list that binding and the gate really does fire on that beat.
      FrameTest_CLIPMODBYPASSTEST(frameId, window);


      // Clip inspector field units and parsing. The widgets themselves need a
      // mouse, but everything that decides what a typed string MEANS is pure
      // and is exactly where this went wrong before: Start/Length read and
      // write bar.beat.sixteenth, fades read and write milliseconds, and the
      // shortcut suppression has to be armed by a hover a frame before the
      // digit that opens the field arrives.
      FrameTest_CLIPFIELDTEST(frameId, window);

      FrameTest_UNDOPERFTEST(frameId, window);

      // Bring the comment into view for the screenshot check. Frame 2 rather
      // than frame 0 only so the node has settled at its spawn position and
      // measured its own size first; the fit itself runs at the end of this
      // same frame and lands on the frame after.
      if (getenv("INFINITE_COMMENTTEST") != nullptr && frameId == 2)
         gRequestFitView = true;

      // Slash mode: one keystroke had to produce a comment already taking the
      // keyboard, with no click of any kind in between, and the "/" itself must
      // not have ended up in the note.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "slash" && frameId == 15)
         {
            CommentNode* c = gNodes.size() == 1
                                ? dynamic_cast<CommentNode*>(gNodes[0].node.get())
                                : nullptr;
            const bool spawned = c != nullptr;
            const bool typed = spawned && c->text == "lighting\nrim light too hot";
            printf("slash spawned a comment=%d, text=\"%s\"  %s\n", (int)spawned,
                   spawned ? c->text.c_str() : "(none)",
                   (spawned && typed) ? "SLASH COMMENT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // In edit mode (see the synthetic double-click and typing above) report
      // whether the note's only way in works end to end.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "edit" && frameId == 19)
         {
            auto* c = static_cast<CommentNode*>(gNodes[0].node.get());
            const bool opened = gCommentEdit.target == c;
            const bool typed = c->text.find('!') != std::string::npos;
            const size_t lines = (size_t)std::count(c->text.begin(), c->text.end(), '\n') + 1;
            printf("double-click opens editor=%d, typing reaches the note=%d, %zu lines  %s\n",
                   (int)opened, (int)typed, lines,
                   (opened && typed && lines == 4) ? "COMMENT EDIT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // A note's line breaks are its content, and one param round trip backs
      // saving, undo/redo and copy/paste alike - so a comment that survives
      // being written to disk and read back survives all three. Checked here
      // rather than by eye because a screenshot cannot tell "kept the line
      // breaks" from "happens to be short enough to wrap the same way".
      //
      // Runs before the screenshot frame, and puts the note back the way it
      // found it, so the picture still shows the comment as authored. Skipped
      // in edit mode: reloading the patch would delete the node whose text is
      // being edited and close the popup under test.
      FrameTest_COMMENTTEST_4(frameId, window);

      // Group auto-fit: the box must track its members in BOTH directions, so
      // dragging one out stretches it and dragging that one back shrinks it
      // to the size it had before. Driven here rather than by hand because
      // the whole behaviour is a fixed point between our own fitting pass and
      // the editor's group geometry, and eyeballing a screenshot cannot tell
      // "shrank back exactly" from "shrank back nearly".
      FrameTest_GROUPTEST(frameId, window);

      FrameTest_LIVETEST(frameId, window);

      // Drives the real glTF/GLB drop handler (main.cpp's kGltfExt branch)
      // end to end by pushing real paths into gDroppedFiles/gDropPos - the
      // same internal queue a real OS file-drop populates - rather than any
      // OS-level UI automation of the ImGui canvas. A push at frame N is
      // consumed by the drop-handling code (above, unconditional every
      // frame) during frame N+1, before this block runs again that same
      // frame, so two-frame spacing between "push" and "verify" stages
      // gives a safety margin. Paths come from env vars so the fixture
      // doesn't depend on committing binary glTF assets into the repo.
      FrameTest_GLTFDROPTEST(frameId, window);

      // R571 slice 4: with the Shortcuts window open, Tab drives ImGui nav and Space must not reach the transport.
      FrameTest_NAVTEST(frameId, window);

      // Cmd/Ctrl+F find on a 400-node patch.
      FrameTest_FINDTEST(frameId, window);

      // R573: every theme's text and dim text clear 4.5:1 against its window and panel backgrounds.
      FrameTest_THEMECONTRASTTEST(frameId, window);

      // R575: on the frame a text field takes focus io.WantTextInput is still false, TextFocusClaimed() is not.
      FrameTest_TEXTFOCUSTEST(frameId, window);

      // R576: help tooltips default to off; Cmd/Ctrl+= and Cmd/Ctrl+- step the UI scale by 0.1.
      FrameTest_UXLEFTOVERSTEST(frameId, window);

      // R506: an anti-aliased edge keeps the shape's own colour and only alpha falls off (straight alpha).
      FrameTest_SHAPEEDGETEST(frameId, window);

      FrameTest_FIELDPIXELTEST(frameId, window);

      // Field step 17 (.field device file format): exercises the pure
      // (de)serialization layer (FieldDevice.h/.cpp), the per-node
      // ToDeviceFile/LoadDeviceFile round trip (plan §2/§7), and the
      // domain-match gate the drag-and-drop dispatch in this file applies
      // before calling LoadDeviceFile - driven via direct function calls,
      // never UI automation, per plan §8.
      FrameTest_FIELDDEVICETEST(frameId, window);

      // Field step 10 (graph domain): the reconciler diffs a fresh GraphPlan
      // against a persisted key->index ownership map (doc §5.3.3) rather than
      // delete-and-respawn. Structural actions (mount/remount/unmount) are
      // what the doc's exit criterion cares about; an unchanged live key
      // still emits an Update action every regenerate (params may need
      // reapplying) even when nothing about it changed, so "idempotent"
      // below is asserted as "zero structural actions", not "zero actions".
      FrameTest_FIELDGRAPHTEST(frameId, window);

      FrameTest_FIELDGRAPHRATETEST(frameId, window);

      if (getenv("INFINITE_RPCBATCHTEST") != nullptr && frameId == 4)
         RunRpcBatchTest();
      if (getenv("INFINITE_HISTORYTEST") != nullptr && frameId == 4)
         RunHistoryTest();

      if (getenv("INFINITE_PATCHWATCHTEST") != nullptr && frameId == 4) // needs the ImGui context, so in-loop
         RunPatchWatchTest();

      FrameTest_FIELDGRAPHUNDOTEST(frameId, window);

      FrameTest_FIELDGRAPHBLASTTEST(frameId, window);

      // Build step 15 ("Instrument Mode"): exit criterion §10.
      FrameTest_FIELDGRAPHENCAPTEST(frameId, window);

      if (getenv("INFINITE_FIELDGRAPHLIVEPARAMTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHLIVEPARAMTEST] Running Field graph live-parameter-forwarding harness...\n");
         bool allOk = true;

         // A thin counting wrapper around the real MainGraphHost so the
         // assertions below can observe exactly how many Mount/Unmount/
         // SetParam calls each phase makes, without duplicating
         // MainGraphHost's own logic (composition, not inheritance -
         // MainGraphHost is `final`).
         struct CountingFieldGraphHost final : public Field::IFieldGraphHost
         {
            MainGraphHost inner;
            int mountCalls = 0, unmountCalls = 0, setParamCalls = 0;
            int Mount(const std::string& t) override { mountCalls++; return inner.Mount(t); }
            void Unmount(int id) override { unmountCalls++; inner.Unmount(id); }
            void SetParam(int id, const std::string& n, float v) override { setParamCalls++; inner.SetParam(id, n, v); }
            void Connect(int a, int b, int c, int d) override { inner.Connect(a, b, c, d); }
            void Place(int id, float x, float y) override { inner.Place(id, x, y); }
            bool Alive(int id) const override { return inner.Alive(id); }
            std::string TypeNameOf(int id) const override { return inner.TypeNameOf(id); }
            bool Spawnable(const std::string& t) const override { return inner.Spawnable(t); }
            int Remount(int existing, const std::string& t) override { return inner.Remount(existing, t); }
            int DroppedModCount() const override { return inner.DroppedModCount(); }
            int DetachedCableCount() const override { return inner.DetachedCableCount(); }
         };

         NewPatch();
         GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
         int kernelIdx = gn->index;
         auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
         fgn->code =
            "param float amount = 0 [0, 1]\n"
            "param float unused = 0 [0, 1]\n"
            "osc = emit(\"LFO\", 0)\n"
            "set(osc, \"rateBeats\", amount)\n"
            "set(osc, \"shape\", 2.0 * unused)\n";
         CountingFieldGraphHost host;
         fgn->Regenerate(host);

         gn = FindNodeByIndex(kernelIdx);
         fgn = static_cast<FieldGraphNode*>(gn->node.get());
         int oscIdx = fgn->Ownership().Get("osc#0");

         auto setParamValue = [&](const char* name, float v) {
            for (auto& p : fgn->GetParamTable().Params())
               if (p.name == name) p.value = v;
         };

         // Assertion 1: driving `amount`'s ParamTable entry directly (simulating
         // a modulation cable write) changes the mounted LFO's actual rateBeats
         // field within the same frame, with zero Mount/Unmount/Remount calls
         // by PushLiveParams (Regenerate's own Mount calls above are excluded
         // by resetting the counters first).
         {
            host.mountCalls = 0;
            host.unmountCalls = 0;
            host.setParamCalls = 0;
            setParamValue("amount", 0.75f);
            fgn->PushLiveParams(host);

            auto* lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            bool valueForwarded = lfo != nullptr && std::abs(lfo->rateBeats - 0.75f) < 1.0e-4f;
            bool noMountUnmount = (host.mountCalls == 0) && (host.unmountCalls == 0);
            bool exactlyOneSetParam = (host.setParamCalls == 1);

            bool pass1 = valueForwarded && noMountUnmount && exactlyOneSetParam;
            printf("[FIELDGRAPHLIVEPARAMTEST] Assertion 1 (Live-forward, no Mount/Unmount): forwarded=%d rate=%f noMountUnmount=%d setParamCalls=%d  %s\n",
                   (int)valueForwarded, lfo ? lfo->rateBeats : -1.0f, (int)noMountUnmount, host.setParamCalls, pass1 ? "OK" : "FAIL");
            allOk = allOk && pass1;
         }

         // Assertion 2: a set() whose value expression is not a bare param
         // reference (`2.0 * unused`) is not in mLiveForward - driving `unused`
         // does not change the mounted LFO's shape until an explicit
         // Regenerate() runs.
         {
            auto* lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            int shapeBefore = lfo ? lfo->shape : -1;

            setParamValue("unused", 1.0f);
            fgn->PushLiveParams(host);

            lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            bool unchangedByPush = lfo != nullptr && lfo->shape == shapeBefore;

            fgn->Regenerate(host);
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            oscIdx = fgn->Ownership().Get("osc#0");
            lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            bool changedByRegenerate = lfo != nullptr && lfo->shape == 2;

            bool pass2 = unchangedByPush && changedByRegenerate;
            printf("[FIELDGRAPHLIVEPARAMTEST] Assertion 2 (Computed set() not live-forwarded): unchangedByPush=%d changedByRegen=%d  %s\n",
                   (int)unchangedByPush, (int)changedByRegenerate, pass2 ? "OK" : "FAIL");
            allOk = allOk && pass2;
         }

         // Assertion 3: PushLiveParams called with no changed param values
         // makes zero host.SetParam calls - a delta-only push, not a
         // re-push-everything-every-frame loop. Assertion 2's Regenerate()
         // call cleared mLastPushedValue (§4.2/§6: rebuilt wholesale every
         // successful Regenerate()), so one priming push is needed first -
         // otherwise this call would still see "amount" as never-pushed-since-
         // last-regenerate and push it once, which is correct behavior but
         // not what this assertion is testing.
         {
            fgn->PushLiveParams(host);
            host.setParamCalls = 0;
            fgn->PushLiveParams(host);
            bool pass3 = (host.setParamCalls == 0);
            printf("[FIELDGRAPHLIVEPARAMTEST] Assertion 3 (No-change push is a no-op): setParamCalls=%d  %s\n",
                   host.setParamCalls, pass3 ? "OK" : "FAIL");
            allOk = allOk && pass3;
         }

         NewPatch();
         printf("%s\n", allOk ? "FIELDGRAPHLIVEPARAM OK" : "SUSPECT");
      }

      // Build step 16 ("Unpack to Canvas") harness. Unlike the encapsulation/
      // live-param/undo harnesses above (which never touch a real ed::
      // node position/size - they only ever check FieldGraphNode/GraphNode
      // bookkeeping directly), this step's exit criterion needs real
      // post-layout bounding boxes, which only exist after the node editor
      // has actually drawn each revealed child at least a couple of times
      // (doc §3.3/trap 3). So this harness spans real frames rather than
      // doing everything inside one frameId gate: setup + triggering the
      // unpack happens at frameId==4 (same "runs before this frame's node
      // draw loop" position the other Field harnesses already use to call
      // RunFieldGraphRegenerate directly - safe for the same reason), then
      // the ordinary per-frame drains (RunFieldGraphUnpackPhase1's arm,
      // RunFieldGraphUnpackPhase2Tick's poll) run every subsequent real
      // frame exactly as they do for a real user click, and assertions run
      // at frameId==30 - comfortably past kMaxRetries (10) frames of
      // phase-2 polling even in the worst case, so gFieldGraphUnpackPhase2
      // is guaranteed inactive (finished, one way or the other) by then.
      static int sUnpackTestFgnIdx = -1;
      static int sUnpackTestConsumerIdx = -1;
      static std::vector<int> sUnpackTestMembers;
      static bool sUnpackTestAllOk = true;

      if (getenv("INFINITE_FIELDGRAPHUNPACKTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHUNPACKTEST] Running Field graph unpack-to-canvas harness...\n");
         bool setupOk = true;

         // Assertion 6 (disabled/no-op on an empty mMountedIndices): a
         // never-regenerated Field Graph node has nothing to unpack -
         // calling the operation directly (as if the disabled button were
         // driven programmatically) must be a documented no-op, not a
         // crash or a partial mutation. Self-contained, own NewPatch(), run
         // before the main multi-frame scenario below.
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            bool encBefore = fgn->encapsulated;
            RunFieldGraphUnpackPhase1(fgn);
            bool pass6 = (fgn->encapsulated == encBefore) && fgn->encapsulated &&
                         !gFieldGraphUnpackPhase2.active;
            printf("[FIELDGRAPHUNPACKTEST] Assertion 6 (No-op on empty mMountedIndices): "
                   "stillEncapsulated=%d phase2Armed=%d  %s\n",
                   (int)fgn->encapsulated, (int)gFieldGraphUnpackPhase2.active, pass6 ? "OK" : "FAIL");
            setupOk = setupOk && pass6;
         }

         // Main scenario: a 3-deep chain (Noise -> Curves -> Curves, wired
         // via connect()) so the topological-depth assertion has a real
         // depth-2 node with transitive depth-0/1 dependencies, plus an
         // outer cable wired into the FieldGraphNode's own derived boundary
         // output pin (the terminal, "c") to prove §4.3's no-op finding -
         // the outer cable's real target is the FieldGraphNode itself
         // (RunFieldGraphRegenerate's boundarySource), which never changes
         // identity or address across an unpack, so the cable must survive
         // untouched.
         NewPatch();
         GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
         int fgnIdx = gn->index;
         auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
         fgn->code =
            "a = emit(\"Noise\", 0)\n"
            "b = emit(\"Curves\", 1)\n"
            "c = emit(\"Curves\", 2)\n"
            "connect(a, 0, b, 0)\n"
            "connect(b, 0, c, 0)\n";
         RunFieldGraphRegenerate(fgn);

         fgn = static_cast<FieldGraphNode*>(FindNodeByIndex(fgnIdx)->node.get());
         ApplyModulationAndPalette(4); // cook once so the terminal has a real texture
         fgn = static_cast<FieldGraphNode*>(FindNodeByIndex(fgnIdx)->node.get());
         fgn->SetBoundaryOutputTarget(ResolveFieldGraphBoundaryTerminal(fgn));

         // Outer cable onto the boundary output pin - positioned well clear
         // of where the unpacked cluster will land so AutoFitGroupToMembers
         // never mistakes it for a member.
         GraphNode* consumer = SpawnNode("Curves", "Compositing", 3000.0f, 0.0f);
         int consumerIdx = consumer->index;
         std::string connErr;
         bool connected = ConnectNodes(fgnIdx, 0, consumerIdx, 0, connErr);
         bool wiredToKernel = connected;
         if (wiredToKernel)
         {
            ImageCable* cable = CableFor(*FindNodeByIndex(consumerIdx), 0);
            wiredToKernel = cable != nullptr && cable->IsConnected() &&
                            cable->GetSource() == FindNodeByIndex(fgnIdx)->node.get();
         }
         setupOk = setupOk && wiredToKernel;
         printf("[FIELDGRAPHUNPACKTEST] Setup (outer cable wired to kernel before unpack): wired=%d  %s\n",
                (int)wiredToKernel, wiredToKernel ? "OK" : "FAIL");

         bool encBefore = fgn->encapsulated;
         bool nonEmptyBefore = !fgn->MountedIndices().empty();
         sUnpackTestMembers.assign(fgn->MountedIndices().begin(), fgn->MountedIndices().end());

         // Same call the "Unpack to Canvas" button makes (DrawFieldGraphParams
         // queues gFieldGraphPendingUnpack; this test calls the queued
         // function directly, at the same before-the-node-draw-loop point in
         // the frame every other Field harness in this file already calls
         // RunFieldGraphRegenerate directly from - safe for the same reason).
         RunFieldGraphUnpackPhase1(fgn);

         sUnpackTestFgnIdx = fgnIdx;
         sUnpackTestConsumerIdx = consumerIdx;

         bool pass0 = encBefore && nonEmptyBefore && !fgn->encapsulated && gFieldGraphUnpackPhase2.active;
         printf("[FIELDGRAPHUNPACKTEST] Setup (phase 1 armed): wasEncapsulated=%d nowUnencapsulated=%d phase2Armed=%d  %s\n",
                (int)encBefore, (int)(!fgn->encapsulated), (int)gFieldGraphUnpackPhase2.active, pass0 ? "OK" : "FAIL");
         setupOk = setupOk && pass0;

         if (!setupOk)
            printf("[FIELDGRAPHUNPACKTEST] setup FAIL - assertions at frameId==30 will not be meaningful\n");
      }

      if (getenv("INFINITE_FIELDGRAPHUNPACKTEST") != nullptr && frameId == 30)
      {
         bool allOk = true;

         GraphNode* fgnGn = FindNodeByIndex(sUnpackTestFgnIdx);
         auto* fgn = fgnGn != nullptr ? static_cast<FieldGraphNode*>(fgnGn->node.get()) : nullptr;

         // Assertion 1: encapsulated flipped false; every mounted child's
         // hiddenFromCanvas cleared; exactly one new GroupNode exists whose
         // gGroupMembers set equals the mounted children's indices exactly.
         bool pass1 = fgn != nullptr && !fgn->encapsulated && !gFieldGraphUnpackPhase2.active;
         for (int idx : sUnpackTestMembers)
         {
            GraphNode* child = FindNodeByIndex(idx);
            pass1 = pass1 && child != nullptr && !child->hiddenFromCanvas;
         }
         GroupNode* spawnedGroup = nullptr;
         int groupCount = 0;
         for (GraphNode& n : gNodes)
         {
            if (auto* g = dynamic_cast<GroupNode*>(n.node.get()))
            {
               groupCount++;
               spawnedGroup = g;
            }
         }
         bool membershipMatches = spawnedGroup != nullptr && gGroupMembers.count(spawnedGroup) != 0 &&
                                   gGroupMembers[spawnedGroup] ==
                                      std::set<int>(sUnpackTestMembers.begin(), sUnpackTestMembers.end());
         pass1 = pass1 && (groupCount == 1) && membershipMatches;
         printf("[FIELDGRAPHUNPACKTEST] Assertion 1 (encapsulated false, children unhidden, 1 group with exact membership): "
                "encFalse=%d groupCount=%d membershipMatches=%d  %s\n",
                (int)(fgn != nullptr && !fgn->encapsulated), groupCount, (int)membershipMatches, pass1 ? "OK" : "FAIL");
         allOk = allOk && pass1;

         // Editor-context save/restore for the position/size reads below -
         // this runs after this frame's ed::End() already cleared the
         // current editor (same shape as FindFreeSpawnPosition/
         // RunFieldGraphUnpackPhase2Tick).
         ed::EditorContext* prevEditor = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);

         // Assertion 2: no two of the members' post-layout bounding boxes
         // overlap.
         struct Box { ImVec2 mn, mx; };
         std::vector<Box> boxes;
         for (int idx : sUnpackTestMembers)
         {
            GraphNode* gn = FindNodeByIndex(idx);
            if (gn == nullptr) continue;
            ImVec2 p = ed::GetNodePosition(gn->NodeId());
            ImVec2 s = ed::GetNodeSize(gn->NodeId());
            boxes.push_back({ p, ImVec2(p.x + s.x, p.y + s.y) });
         }
         bool noOverlap = true;
         for (size_t i = 0; i < boxes.size() && noOverlap; i++)
            for (size_t j = i + 1; j < boxes.size() && noOverlap; j++)
            {
               const Box& A = boxes[i]; const Box& B = boxes[j];
               bool overlap = A.mx.x > B.mn.x && A.mn.x < B.mx.x && A.mx.y > B.mn.y && A.mn.y < B.mx.y;
               if (overlap) noOverlap = false;
            }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 2 (No overlapping bounding boxes among %zu members): %s\n",
                boxes.size(), noOverlap ? "OK" : "FAIL");
         allOk = allOk && noOverlap;

         // Assertion 3: topological x-ordering - "c" (depth 2) sits strictly
         // right of both "a" (depth 0) and "b" (depth 1), which sits
         // strictly right of "a" - transitively via plan.connects.
         bool topoOk = false;
         if (fgn != nullptr)
         {
            int aIdx = fgn->Ownership().Get("a#0");
            int bIdx = fgn->Ownership().Get("b#1");
            int cIdx = fgn->Ownership().Get("c#2");
            GraphNode* ag = FindNodeByIndex(aIdx);
            GraphNode* bg = FindNodeByIndex(bIdx);
            GraphNode* cg = FindNodeByIndex(cIdx);
            if (ag != nullptr && bg != nullptr && cg != nullptr)
            {
               float ax = ed::GetNodePosition(ag->NodeId()).x;
               float bx = ed::GetNodePosition(bg->NodeId()).x;
               float cx = ed::GetNodePosition(cg->NodeId()).x;
               topoOk = (bx > ax) && (cx > bx) && (cx > ax);
            }
         }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 3 (Topological x-ordering across 3 depths): %s\n",
                topoOk ? "OK" : "FAIL");
         allOk = allOk && topoOk;

         ed::SetCurrentEditor(prevEditor);

         // Assertion 4 (§4.3's expected no-op, asserted rather than assumed):
         // the outer cable wired to the FieldGraphNode's boundary output pin
         // before unpacking is still connected, to the same INode*, after
         // unpacking - zero detached, because the outer cable's real target
         // was always the FieldGraphNode itself (never the terminal), and
         // the FieldGraphNode's identity/address never changes across an
         // unpack.
         bool boundaryPreserved = false;
         {
            GraphNode* consumer = FindNodeByIndex(sUnpackTestConsumerIdx);
            ImageCable* cable = consumer != nullptr ? CableFor(*consumer, 0) : nullptr;
            boundaryPreserved = fgnGn != nullptr && cable != nullptr && cable->IsConnected() &&
                                cable->GetSource() == fgnGn->node.get();
         }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 4 (Boundary cable identity preserved, zero detach): %s\n",
                boundaryPreserved ? "OK" : "FAIL");
         allOk = allOk && boundaryPreserved;

         // Assertion 5 (part 1): undo after unpack restores `encapsulated`
         // and removes the spawned GroupNode in one step. The children's
         // hiddenFromCanvas re-sync is driven by the per-frame loop in
         // ApplyModulationAndPalette (main.cpp, "Build step 15" comment
         // above `child->hiddenFromCanvas = fgn->encapsulated;"), which for
         // this frame has already run before this test block executes - so
         // Undo() here takes effect for that loop's *next* pass, at
         // frameId==31, same one-frame lag FIELDGRAPHENCAPTEST's own
         // assertion 4 already relies on for the opposite toggle direction.
         bool undoOk = false;
         {
            Undo();
            GraphNode* afterUndoGn = FindNodeByIndex(sUnpackTestFgnIdx);
            auto* afterUndoFgn = afterUndoGn != nullptr ? static_cast<FieldGraphNode*>(afterUndoGn->node.get()) : nullptr;
            bool encRestored = afterUndoFgn != nullptr && afterUndoFgn->encapsulated;
            bool groupGone = true;
            for (GraphNode& n : gNodes)
               if (dynamic_cast<GroupNode*>(n.node.get()) != nullptr) groupGone = false;
            undoOk = encRestored && groupGone;
            printf("[FIELDGRAPHUNPACKTEST] Assertion 5a (Undo restores encapsulated, removes group): "
                   "encRestored=%d groupGone=%d  %s\n",
                   (int)encRestored, (int)groupGone, undoOk ? "OK" : "FAIL");
         }
         allOk = allOk && undoOk;
         sUnpackTestAllOk = allOk;
      }

      if (getenv("INFINITE_FIELDGRAPHUNPACKTEST") != nullptr && frameId == 31)
      {
         // Assertion 5 (part 2): one real frame after Undo(), the sync loop
         // in ApplyModulationAndPalette has now run once with the restored
         // `encapsulated == true`, so every member should be hidden again.
         //
         // Undo() goes through ApplyPatchData, which - same as
         // FIELDGRAPHUNDOTEST's own assertions 3/6 - reassigns node indices
         // (see its `remap` out-param). sUnpackTestMembers holds the
         // pre-undo indices, which are no longer meaningful; re-resolve the
         // three emit keys through the (now-restored) FieldGraphNode's own
         // ownership map instead, exactly as FIELDGRAPHUNDOTEST does with
         // "osc#" + i.
         GraphNode* fgnGn = FindNodeByIndex(sUnpackTestFgnIdx);
         if (fgnGn == nullptr)
         {
            for (GraphNode& n : gNodes)
               if (dynamic_cast<FieldGraphNode*>(n.node.get()) != nullptr) fgnGn = &n;
         }
         auto* fgn = fgnGn != nullptr ? static_cast<FieldGraphNode*>(fgnGn->node.get()) : nullptr;
         bool childrenHidden = fgn != nullptr;
         for (const char* key : { "a#0", "b#1", "c#2" })
         {
            int idx = fgn != nullptr ? fgn->Ownership().Get(key) : -1;
            GraphNode* child = idx >= 0 ? FindNodeByIndex(idx) : nullptr;
            childrenHidden = childrenHidden && child != nullptr && child->hiddenFromCanvas;
         }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 5b (Children re-hidden one frame after undo): childrenHidden=%d  %s\n",
                (int)childrenHidden, childrenHidden ? "OK" : "FAIL");
         bool allOk = sUnpackTestAllOk && childrenHidden;

         NewPatch();
         sUnpackTestFgnIdx = -1;
         sUnpackTestConsumerIdx = -1;
         sUnpackTestMembers.clear();
         sUnpackTestAllOk = true;
         printf("%s\n", allOk ? "FIELDGRAPHUNPACK OK" : "SUSPECT");
      }

      FrameTest_FIELDPINSTEST(frameId, window);

      // Build step 13 (docs/plans/field/step-13-dynamic-pins-node-wiring.md):
      // node/UI/save-format wiring for kernel `output`/`input` declarations
      // on top of step 12's compiler-level PinTable/IR work and step 11's
      // hardcoded toggle pins (both exercised above by FIELDPINSTEST). This
      // was originally structural-only - every declared output's
      // ModulatorOutput() read back a fixed 0.0 placeholder, regardless of
      // domain. The device-catalog simplification pass finished that
      // follow-up for the one case with a working name-keyed runtime
      // channel: a Frame-domain, non-structural declared output (`chime`,
      // `glow`, ...) on FieldElementNode now reads its real value via
      // ElementVM::ReadFrameVar (FieldIR.cpp's DeclOutput lowering emits a
      // synthetic frame-var assign for it), and FieldSampleNode's
      // Frame-domain declared output (`bass`, always `reduce.rms(...)`) now
      // reads the same live value as the "rms" toggle output. Every other
      // declared-output domain (element/pixel/sample) still reads the fixed
      // 0.0 placeholder - see each node's DeclaredOutputPlaceholder.
      FrameTest_FIELDPINNODETEST(frameId, window);

      // GetNodeInstanceIndex answers from a cache (see NodeTitleInstanceEntry).
      // Every node's "#N" title must match the reference linear scan, with no
      // frame boundary in between, after each kind of change the cache has to
      // notice: spawn, delete, a live shape change, and back to unique.
      FrameTest_NODETITLETEST(frameId, window);

      // Audio Filter's response-curve cache (FilterCurveCache): a settled
      // curve is bit-identical to a direct full recompute, a single step is
      // exact at once, and a modulation-style streak is throttled - then
      // back to the exact full-resolution curve as soon as it stops.
      FrameTest_FILTERCURVECACHETEST(frameId, window);

      FrameTest_BYPASSRULETEST(frameId, window);

      FrameTest_BYPASSSWEEPTEST(frameId, window);

      // Live half of PATCHLAYOUTTEST: a pos-less file opened through the real
      // loader must come out laid out from drawn sizes - nothing stacked at 0,0,
      // no overlapping boxes, the picture chain ordered by wiring depth.
      FrameTest_PATCHLAYOUTLIVETEST(frameId, window);

      FrameTest_ROUNDTRIPTEST(frameId, window);

      FrameTest_PHASEFTEST(frameId, window);

      FrameTest_PHASEETEST(frameId, window);

      FrameTest_GROUP3DTEST(frameId, window);

      FrameTest_WRAPTEST(frameId, window);

      FrameTest_PHASEDTEST(frameId, window);

      FrameTest_PHASECTEST(frameId, window);

      FrameTest_MAPTEST(frameId, window);

      FrameTest_SHADOWTEST(frameId, window);

      FrameTest_BUGTEST(frameId, window);

      // Sweeps every node type that consumes an IGeometrySource, checking one
      // thing: does moving/rotating/scaling its upstream source actually move
      // the final world-space result? BUGTEST above proves specific fixtures
      // stay fixed; this proves the same property for every node that takes a
      // geometry input, generically, so a newly added node type is covered
      // without anyone having to remember to hand-write a fixture for it.
      FrameTest_TRANSFORMSWEEPTEST(frameId, window);

      // Sibling of TRANSFORMSWEEPTEST, same generic-probe approach, checking a
      // different side-channel: does GetMappingTransform() reach a node's
      // output from its input? Found via a real bug: ClothNode, MeshResynthNode
      // and MeshToPointsNode forwarded every other side-channel (material,
      // textures, model matrix) from their single geo input but not this one,
      // so a Mapping node patched upstream of any of them had its space/
      // translate/rotate/scale silently dropped before Render 3D ever saw it.
      FrameTest_MAPPINGSWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): sibling of MAPPINGSWEEPTEST,
      // checking side channel A (Material, `GetMaterial()`) instead of F.
      // Same generic-probe shape - MAPPINGSWEEPTEST already proved the
      // pattern works for one side channel.
      FrameTest_MATERIALSWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): sibling of MATERIALSWEEPTEST,
      // checking side channel E (textures, `GetMaterialTexture`/
      // `GetSurfaceTexture()`) with the identical wiring - the 17 node types
      // below are the exact same set, so a node that forwards A but not E
      // (or vice versa) shows up as one sweep failing and the other passing.
      FrameTest_TEXTURESWEEPTEST(frameId, window);

      // GeometryOpNode used to drop point clouds and curves (its GetPointCloud/
      // GetCurve fell through to IGeometrySource's nullptr defaults), so
      // Mesh to Points -> Transform -> Render 3D drew nothing. Checks, without
      // a GL context: a cloud/curve survives every op, kTransform moves the
      // cloud by exactly the offset and rotates nothing it shouldn't, and
      // PointCloudRevision only moves when the points actually changed.
      FrameTest_POINTCLOUDSWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): channels B (`Mesh::vertexColor`)
      // and C (`Particle::r/g/b`/`hasColor`) - built from scratch (no prior
      // COLOURSWEEPTEST existed anywhere in the codebase; grepped twice to
      // confirm). Two invariants, checked per node: colourless in must stay
      // colourless out (D6's "don't manufacture colour" rule,
      // `Mesh.h:296-301`), and a distinct authored colour in must survive out
      // unchanged. Covers every node type where a B/C answer is meaningful;
      // excludes InstanceOnPointsNode (its colour path is channel D,
      // `InstanceColors()`, not B/C - no sweep covers D yet, a gap for a
      // future test, not silently dropped), CurveNode/ModelSourceNode (need
      // real file/curve data to produce anything in a headless run), and
      // ImageToPointsNode/ParticleSystemNode's own colour-origination (both
      // already covered end-to-end by this session's D6 follow-up audit,
      // commit 853c732 - re-driving ParticleSystemNode here would need its
      // real time-stepped emission, which is flaky in a single-frame test).
      FrameTest_COLOURSWEEPTEST(frameId, window);

      // The instancing side-channels, same generic-probe shape as the two
      // sweeps above. InstanceOnPointsNode's GetMesh() returns the single
      // stamp mesh and carries its N placements separately, so every consumer
      // that wants the scatter walks PassthroughSource() to find the
      // instancer (Render3DNode::FindInstancer, NodeViewport's copy) and
      // reads GetInstanceGroupMatrix() off the chain head to pick up a
      // wrapping Transform. A node that takes a geometry input, forwards
      // every other side-channel, and silently drops these two turns N
      // instances into one un-instanced stamp - with no error anywhere.
      // Found via a real bug: Null3DNode (a node whose entire job is to be a
      // no-op passthrough), DisplacementNode and WrapNode dropped
      // PassthroughSource, and MaterialNode/SetColorNode/MergeByDistanceNode/
      // Switcher3DNode forwarded PassthroughSource without the group matrix,
      // so `Instance on Points -> Transform -> Material -> Render 3D` drew
      // the scatter back at the origin.
      FrameTest_INSTANCESWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): a randomised chain fuzzer, rather
      // than enumerating orderings by hand - the only tractable answer to
      // the permutation problem. Roster is the 11 single-geometry-input,
      // mesh-forwarding operator types from COLOURSWEEPTEST's
      // checkMeshForwarding list (GeometryOpNode, DisplacementNode,
      // AudioDisplacementNode, SetColorNode, MeshResynthNode, Null3DNode,
      // MaterialNode, MappingNode, MergeByDistanceNode, ClothNode,
      // FieldElementNode) - deliberately excludes the multi-input types
      // (WrapNode, JoinGeometryNode, Switcher3DNode, InstanceOnPointsNode)
      // since a random *chain* only ever needs one upstream slot filled;
      // giving every link a second, unfilled input pin would just make each
      // step behave like whatever that node does with nothing patched into
      // its second slot, not exercise anything new. Fixed-seed PRNG so a
      // failure is reproducible across runs. Expect this to surface real,
      // unrelated pre-existing bugs - that's the point (per the plan).
      FrameTest_CHAINFUZZTEST(frameId, window);

      // A different bug class from the two sweeps above: not a dropped
      // side-channel, but a revision/generation stamp that bumps when nothing
      // actually changed. Found via a real bug: DisplacementNode bumped
      // mTexGeneration on every single cook while a texture was connected,
      // even when the texture's pixels were identical to last frame, which
      // made MeshRevision() change every frame and forced ClothNode
      // downstream to treat every frame as a topology change - the cloth sim
      // reset to rest pose continuously instead of ever draping.
      //
      // The check: cook the same node twice in a row with nothing about its
      // inputs changed, and assert MeshRevision() (or the equivalent stamp)
      // did not move between the two cooks. Any node with a texture input
      // gets a *connected, static* texture for the same reason the Displace
      // bug only showed up with one patched in - the bug is invisible with no
      // texture connected at all.
      FrameTest_REVISIONSWEEPTEST(frameId, window);

      // Regression test for Render3DNode's frame-cache signature missing the
      // instancing and per-source-transform stamps. Confirms neither a
      // Particle System -> Instance on Points chain (no MeshRevision/
      // PointCloudRevision/CurveStamp bump - all motion carried by
      // InstanceRevision(), which the signature didn't track) nor a
      // spinY-animated Geometry node (a live GetModelMatrix() with no
      // revision bump of its own) leaves Render 3D's output frozen across
      // advancing frames with nothing but the transport clock moving -
      // the same as a user just sitting there, not touching the viewport.
      FrameTest_RENDER3DLIVETEST(frameId, window);

      // A different bug class from RENDER3DLIVETEST above: not a live-updating
      // source with no revision bump of its own, but the opposite direction -
      // a real upstream mesh/cloud/curve change that DOES bump a revision,
      // but never reaches Render 3D's rendered pixels because
      // BuildSceneSignature's per-slot geomRev used to XOR-fold
      // MeshRevision()/PointCloudRevision()/CurveStamp() into one value.
      // Several IGeometrySource implementations deliberately return the same
      // counter from two of those three accessors (mesh cache and point/curve
      // cache rebuilt together, sharing one stamp), which made the XOR
      // cancel to a constant 0 forever - the cache never invalidated and the
      // viewport froze on the first frame. See the SceneSignature comment in
      // Geometry3DNodes.h for the full story.
      //
      // The check, per geometry-consuming node type: cook Render 3D once,
      // capture NodeWorkCounter() (bumped only on a real, non-cached render -
      // Geometry3DNodes.cpp's CookIfNeeded increments it right after the
      // cache-hit early-return check), change something upstream that
      // genuinely alters the geometry, cook Render 3D again, and assert
      // NodeWorkCounter() advanced - i.e. the cache early-return was NOT
      // taken.
      FrameTest_RENDER3DCACHESWEEPTEST(frameId, window);

      FrameTest_FIXTEST(frameId, window);

      FrameTest_CLOTHTEST(frameId, window);

      FrameTest_PARTICLETEST(frameId, window);

      FrameTest_AUDIORECTEST(frameId, window);

      FrameTest_VIDEOAUDIOTEST(frameId, window);

      FrameTest_VIDEOSPEEDTEST(frameId, window);

      FrameTest_OFFLINERENDERTEST(frameId, window);

      FrameTest_OFFLINERENDERREFUSETEST(frameId, window);

      FrameTest_PATCHTEST(frameId, window);

      FrameTest_AUTOSAVETEST(frameId, window);

      // R30: the live validate pass marks a half-wired Blend and an empty
      // Output, leaves a fully wired graph alone, and clears when it is fixed.
      FrameTest_LIVEISSUETEST(frameId, window);
      FrameTest_LIVEISSUETEST_2(frameId, window);

      // R617: Shape Resonator inside the real graph - eight instances on one source, save/load with the shape
      // pin wired, source deleted mid-ring, and the pin counted as an input slot.
      FrameTest_SHAPERESGRAPHTEST(frameId, window);
      auto ShapeResFixtureReport = [&](const char* stage, bool wantWired) {
         int resonators = 0, wired = 0, slotOk = 0;
         for (GraphNode& gn : gNodes)
         {
            auto* fx = dynamic_cast<AudioEffectNode*>(gn.node.get());
            if (!fx || gn.typeName != "Shape Resonator")
               continue;
            resonators++;
            fx->CookIfNeeded(frameId); // must not crash on a freed or swapped source
            wired += (fx->geometry != nullptr);
            slotOk += (InputCountFor(gn) >= 2);
         }
         const bool ok = resonators == 8 && (wantWired ? wired == 8 : wired == 0) && slotOk == 8;
         printf("SHAPERESGRAPHTEST %s: resonators=%d wired=%d shape-pin-counted=%d  %s\n", stage, resonators, wired,
                slotOk, ok ? "OK" : "FAIL");
      };
      if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 6)
         ShapeResFixtureReport("after load", true);
      FrameTest_SHAPERESGRAPHTEST_3(frameId, window);
      if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 10)
      {
         ShapeResFixtureReport("after save/load", true);
         for (GraphNode& gn : gNodes)
            if (gn.typeName == "Cube")
            {
               RemoveNodeByIndex(gn.index); // the source dies while eight resonators are ringing on it
               break;
            }
      }
      if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 12)
         ShapeResFixtureReport("after source deleted", false);

      if (getenv("INFINITE_DELETECRASHTEST") != nullptr && frameId == 4)
      {
         RemoveNodeByIndex(gNodes[0].index);
      }
      FrameTest_DELETECRASHTEST_2(frameId, window);

      // DELETECRASHTEST for the audio graph (docs/plans/audio/README.md §4/§7):
      // every node type DiscoverAudioSweepCandidates finds touching the audio/
      // note cable graph at all (not just ones with an AudioNode to drive -
      // this is broader than AUDIOPARAMSWEEPTEST's filter, so it also covers
      // pure terminals like Audio Out) gets spawned into the real editor
      // graph, wired with whatever companion nodes its shape needs, rendered
      // a few blocks "mid-playback", deleted via the real RemoveNodeByIndex
      // (which is what exercises DisconnectAllTo's generic AudioInputSlot/
      // NoteInputSlot loop), then rendered a few more blocks. Surviving to the
      // final printf is most of the proof - a dangling pointer to the freed
      // node's AudioNode crashes the very next ProcessOffline, the same way
      // DELETECRASHTEST's geometry nodes crash on the next cook.
      FrameTest_AUDIOTEARDOWNSWEEPTEST(frameId, window);

      // NoteEventQueue multi-consumer fanout regression (docs/plans/
      // fm-and-note-fanout-fixes.md item 11): before the fix, a single-cursor
      // queue meant the FIRST consumer to Pop() in topology order drained
      // every event, starving every other consumer wired to the same
      // producer. The bug report's exact patch - one note source feeding two
      // synths, one of which also feeds the other's FM input, so the FM
      // modulator (a dependency) necessarily runs before the synth it
      // modulates - made the second synth silent ("output peak=0.00000").
      // Goes through the real graph and the real RebuildAudioTopology (not a
      // hand-rolled inbox), because the thing under test is the topology
      // builder's ResetConsumers()/RegisterConsumer() wiring itself, not the
      // queue's own Push/Pop (already covered by NoteEventQueue's unit-level
      // use in the other fixtures above).
      // Note generators must run with no audio device: Random Note Generator
      // -> Note to CV should light up without Start Audio (the device-less
      // note pump in AudioEngine::PumpNoteNodesWithoutDevice).
      FrameTest_NOTEPUMPTEST(frameId, window);

      FrameTest_NOTEFANOUTTEST(frameId, window);

      // Regression test for the stuck-note race fixed by AudioEngine::
      // ApplyNoteWiringIfNew (bugfix/audio-rt-note-wiring): a Note Sequencer
      // fanned out to two synths, with RebuildAudioTopology() forced every
      // single block for ~2 seconds' worth of blocks while notes are
      // flowing through it. Before the fix, RebuildAudioTopology mutated the
      // live AudioNode/NoteEventQueue cursors directly on the main thread
      // (ResetConsumers/RegisterConsumer/SetNoteInbox) - here that means
      // every one of these per-block rebuilds re-registering both synths'
      // read cursors while a note could be in flight, which used to be able
      // to skip or duplicate events (worst case: a lost note-off, a
      // permanently stuck voice). Now the rebuild only *describes* the
      // wiring (AudioTopology::noteOutboxes/noteWires) and
      // ApplyNoteWiringIfNoDevice applies it - carrying each cursor's old
      // read position forward via AudioNode::appliedInbox/appliedCursor - so
      // repeated rewiring mid-stream must not drop a single event.
      // Note events are pushed directly into the sequencer's own outbox
      // (AudioNode::NoteOutbox(), a fixed member - present whether or not
      // the sequencer is actually playing) rather than relying on real
      // tempo-driven playback, so the fixture is deterministic and doesn't
      // need to wait on wall-clock time.
      FrameTest_NOTEREWIRESTRESSTEST(frameId, window);

      // Audio Filter's "cutoff mod" sidechain input (slot 1) was removed as
      // part of the envAmount-becomes-internal-LFO redesign (EffectDefs.cpp,
      // AudioFilterKernel.h) - the sidechain-teardown fixture that used to
      // live here (spawn/wire/delete-mid-playback against that pin) went
      // with it, since the pin no longer exists to test.

      FrameTest_AUDIOGRAPHTEST(frameId, window);

      // File > New with audio rendering. NewPatch retires every node and
      // clears gNodes; the published audio topology must be republished with
      // it, or the engine keeps rendering the old nodes (audible after New,
      // and with no device the retire drain frees nodes the topology still
      // points at). frameId 4: render, New, render; frameId 12: the retired
      // nodes must have been drained.
      FrameTest_NEWPATCHAUDIOTEST(frameId, window);

      FrameTest_MATFRAMETEST(frameId, window);

      FrameTest_ENVTEST(frameId, window);

      FrameTest_PATHOCEANTEST(frameId, window);

      FrameTest_PALETTETEST(frameId, window);

      FrameTest_PALETTETEST_2(frameId, window);

      FrameTest_UTILTEST(frameId, window);

      FrameTest_TEXT3DTEST(frameId, window);

      FrameTest_MODELTEST(frameId, window);

      // Exercises every mesh operator directly, checking each produces a
      // non-degenerate mesh with finite coordinates and a sane bounding box.
      // A silently empty or NaN-riddled result is the failure mode that matters
      // here: the renderer draws nothing and says nothing.
      FrameTest_MESHOPTEST(frameId, window);

      // Frame limiter check: run uncapped, then at 30fps, and compare. Vsync is
      // off for this so the display refresh is not what is being measured.
      if (getenv("INFINITE_FPSTEST") != nullptr)
      {
         static double sUncapped = 0.0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); }
         if (frameId == 30) { sUncapped = gLastFrameMs; gTargetFps = 30; }
         if (frameId == 60)
         {
            printf("uncapped %.2f ms/frame -> capped %.2f ms/frame (target 30fps = 33.3 ms)\n",
                   sUncapped, gLastFrameMs);
            printf("%s\n", (gLastFrameMs > 30.0 && gLastFrameMs < 37.0)
                              ? "FRAME LIMITER OK" : "SUSPECT - limiter missed its budget");
         }
      }

      // 3D geometry density stress fixture, measurement half - see the setup
      // half above (INFINITE_GEOMDENSITYTEST, before the main loop starts).
      // Explicit CookIfNeeded is needed every frame because the main loop's
      // ordinary per-frame cook pass (above, "apply modulation and palette")
      // only walks Output/Syphon/OscSend nodes - a bare Render 3D with
      // nothing downstream would otherwise never cook at all. Uncapped +
      // vsync off so the frame limiter/display refresh don't mask the real
      // per-frame cost; 30 frames of warmup (mesh upload, driver
      // shader/FBO allocation) then 120 sampled frames, matching the
      // node-chain FPS investigation's sampling window.
      if (getenv("INFINITE_GEOMDENSITYTEST") != nullptr)
      {
         static double sSum = 0.0, sMin = 1e30, sMax = 0.0;
         static int sSampleCount = 0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; }
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         if (frameId >= 2)
            render->CookIfNeeded(frameId);
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
         {
            sSum += gLastFrameMs;
            sMin = std::min(sMin, gLastFrameMs);
            sMax = std::max(sMax, gLastFrameMs);
            sSampleCount++;
         }
         if (frameId == 152)
         {
            const double avg = sSampleCount > 0 ? sSum / sSampleCount : 0.0;
            const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
            printf("GEOMDENSITYTEST tris=%zu avg=%.3fms min=%.3fms max=%.3fms fps=%.2f\n",
                   render->LastTriangleCount(), avg, sMin, sMax, fps);
            printf("%s\n", sSampleCount > 0 ? "GEOMDENSITYTEST DONE" : "GEOMDENSITYTEST FAIL (no samples)");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(b) node-count scaling fixture, measurement half - see
      // INFINITE_BENCH_B5NODES's setup above. Same warmup/sample window as
      // the other perf fixtures in this file (32 warmup, 120 sampled,
      // uncapped/vsync off) so results are comparable. Uses the shared
      // Bench::BenchReport/PercentileRing infra (src/core/BenchReport.h)
      // rather than this file's older avg/min/max-only pattern, per
      // benchmark-suite.md §3 (p50/p95/p99 matter more than average for live
      // work) and §5 (one BENCH_JSON line, not printf-formatted text).
      if (getenv("INFINITE_BENCH_B5NODES") != nullptr)
      {
         static Bench::PercentileRing sFrameMs;
         static double sRssStartMb = -1.0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; sRssStartMb = Bench::ProcessRssMb(); }
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
            sFrameMs.Push(gLastFrameMs);
         if (frameId == 152)
         {
            Bench::BenchReport report;
            report.bench = "B5_fundamentals_nodecount";
            report.variant = getenv("INFINITE_BENCH_B5NODES") ? getenv("INFINITE_BENCH_B5NODES") : "";
            report.frames = 152;
            report.nodes = (int)gNodes.size();
            report.frameMs = sFrameMs;
            report.memRssStartMb = sRssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5NODES DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(c) per-stage CPU timing fixture, measurement half - see
      // INFINITE_BENCH_B5STAGES setup above. Same warmup/sample window as
      // B5(a)/B5(b). Uses ScopedStageTimer/ConditionalStageTimer wired into
      // each main-loop stage to report median ms per stage in stagesCpuMs.
      if (isBenchB5c)
      {
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; sBenchB5cRssStartMb = Bench::ProcessRssMb(); }
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
            sBenchB5cFrameMs.Push(gLastFrameMs);
         if (frameId == 152)
         {
            Bench::BenchReport report;
            report.bench = "B5_fundamentals_stages";
            const char* bArg = getenv("INFINITE_BENCH_B5STAGES") ? getenv("INFINITE_BENCH_B5STAGES") : getenv("INFINITE_BENCH_B5C");
            report.variant = (bArg && *bArg && atol(bArg) > 1) ? (std::string("n=") + bArg) : "n=100";
            report.frames = 152;
            report.nodes = (int)gNodes.size();
            report.frameMs = sBenchB5cFrameMs;
            report.stagesCpuMs = {
               { "modulation", sStageModulation.Percentile(50) },
               { "cook", sStageCook.Percentile(50) },
               { "node_bodies", sStageNodeBodies.Percentile(50) },
               { "editor_end", sStageEditorEnd.Percentile(50) },
               { "imgui_render", sStageImGuiRender.Percentile(50) },
               { "projectors", sStageProjectors.Percentile(50) },
               { "swap", sStageSwap.Percentile(50) },
            };
            sGpuTimerRing.Finish();
            report.stagesGpuMs = sGpuTimerRing.ToJsonP50();
            report.memRssStartMb = sBenchB5cRssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5STAGES DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }}
}
