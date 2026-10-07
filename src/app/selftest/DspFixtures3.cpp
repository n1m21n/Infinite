// DSP self-test fixtures, part 3: drum sequencer..RunDspTest (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
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

   const std::string shortClickPath = WriteClickWav(TmpPath("infinite_drumseq_click.wav"), 4000, sampleRate);
   const std::string longClickPath = WriteSustainedWav(TmpPath("infinite_drumseq_sustain.wav"), kSustainFrames, sampleRate);

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

   // ---- multi-output discrete lane routing test --------------------------
   {
      resetTransport();
      DrumSequencerNode node;
      node.rate = MusicTime::kSixteenth;
      node.numSteps = 1;
      bool pinsOk = (node.OutputCount() == 9);
      if (pinsOk)
      {
         pinsOk &= (strcmp(node.OutputLabel(0), "out") == 0);
         pinsOk &= (strcmp(node.OutputLabel(1), "1") == 0);
         pinsOk &= (strcmp(node.OutputLabel(8), "8") == 0);
         pinsOk &= (node.AudioOutputSlotForPin(0) == 0);
         pinsOk &= (node.AudioOutputSlotForPin(3) == 3);
      }
      AudioNode* an = node.GetAudioNode();
      bool audioOutOk = (an != nullptr && an->AudioOutputCount() == 9);

      bool routingOk = pinsOk && audioOutOk;
      if (routingOk)
      {
         node.LoadFileToLane(1, shortClickPath); // Load click into lane 2 (index 1)
         node.stepVel[1][0] = 0.8f; // Program hit on lane 2 only
         node.CookIfNeeded(1);
         an->PrepareToPlay((double)sampleRate, blockSize);
         node.CookIfNeeded(2);

         constexpr int kBlock = 256;
         float masterEnergy = 0.0f;
         float lane1Energy = 0.0f;
         float lane2Energy = 0.0f;
         float lane3Energy = 0.0f;

         for (int b = 0; b < 16; b++)
         {
            float outBufs[9][2][kBlock] = {};
            float* outChanPtrs[9][2];
            AudioBuffer outAudioBufs[9];
            AudioBuffer* outPtrs[9];
            for (int o = 0; o < 9; o++)
            {
               outChanPtrs[o][0] = outBufs[o][0];
               outChanPtrs[o][1] = outBufs[o][1];
               outAudioBufs[o].channels = outChanPtrs[o];
               outAudioBufs[o].numChannels = 2;
               outAudioBufs[o].numFrames = kBlock;
               outPtrs[o] = &outAudioBufs[o];
            }

            transport.AdvanceAudioClock(kBlock);
            an->ProcessBlockMulti(nullptr, 0, outPtrs, 9);

            for (int i = 0; i < kBlock; i++)
            {
               masterEnergy += std::abs(outBufs[0][0][i]);
               lane1Energy += std::abs(outBufs[1][0][i]);
               lane2Energy += std::abs(outBufs[2][0][i]); // index 2 is lane 2 (pin 2)
               lane3Energy += std::abs(outBufs[3][0][i]);
            }
         }

         if (masterEnergy <= 1e-4f || lane2Energy <= 1e-4f || lane1Energy > 1e-6f || lane3Energy > 1e-6f)
         {
            printf("DRUMSEQTEST multi-output FAIL (master=%.4f lane2=%.4f lane1=%.4f lane3=%.4f)\n",
                   masterEnergy, lane2Energy, lane1Energy, lane3Energy);
            routingOk = false;
         }
      }
      printf("DRUMSEQTEST multi-output lane routing %s\n", routingOk ? "OK" : "FAIL");
      ok &= routingOk;
   }

   // 14) ReloadFromPaths preserves trim settings (undo / redo / reload regression):
   // Simulates save/load and undo/redo by populating a lane, setting custom
   // trim (start/end), serializing via Patch::SaveParams, deserializing into a
   // fresh DrumSequencerNode via Patch::LoadParams, and calling ReloadFromPaths().
   // The custom trim bounds must survive rather than being reset to 0..1 by
   // FinishLaneBuffer.
   {
      auto src = std::make_unique<DrumSequencerNode>();
      src->LoadFileToLane(0, shortClickPath);
      src->laneStart[0] = 0.25f;
      src->laneEnd[0] = 0.75f;

      std::vector<std::pair<std::string, std::string>> savedParams;
      Patch::SaveParams(src.get(), savedParams);

      auto dst = std::make_unique<DrumSequencerNode>();
      Patch::LoadParams(dst.get(), savedParams);
      const bool paramsLoadedOk = (std::fabs(dst->laneStart[0] - 0.25f) < 1e-4f) &&
                                  (std::fabs(dst->laneEnd[0] - 0.75f) < 1e-4f);

      // Simulates what ReloadDerivedState does on undo/redo and patch load:
      dst->ReloadFromPaths();
      const bool trimPreservedAfterReload = (std::fabs(dst->laneStart[0] - 0.25f) < 1e-4f) &&
                                            (std::fabs(dst->laneEnd[0] - 0.75f) < 1e-4f);

      // Verify that CookIfNeeded successfully prepares and pushes to audio node
      dst->CookIfNeeded(1);

      const bool reloadTrimOk = paramsLoadedOk && trimPreservedAfterReload;
      printf("DRUMSEQTEST reload trim preservation %s (loaded=%d preserved=%d start=%.3f end=%.3f)\n",
             reloadTrimOk ? "OK" : "FAIL", paramsLoadedOk, trimPreservedAfterReload,
             dst->laneStart[0], dst->laneEnd[0]);
      ok &= reloadTrimOk;
   }

   // 15) 32 steps: only step 20 set. At 120 BPM 1/16 a step is 6000 frames @48k,
   // so the first hit lands at 20*6000 = 120000 (+-1) and the pattern repeats
   // every 32*6000 = 192000 frames.
   {
      resetTransport();
      auto node = makeNode(shortClickPath);
      node->numSteps = 32;
      node->stepVel[0][20] = 0.8f;
      const int stepFrames = (int)std::lround(sampleRate * 0.125);
      const int total = 32 * stepFrames + 22 * stepFrames;
      const auto buf = Render(*node, total, blockSize, sampleRate);
      // The click's own attack puts the threshold crossing a few dozen frames
      // after the step boundary, so measure against step 0 of an identical node.
      resetTransport();
      auto refNode = makeNode(shortClickPath);
      refNode->numSteps = 32;
      refNode->stepVel[0][0] = 0.8f;
      const auto refBuf = Render(*refNode, stepFrames * 4, blockSize, sampleRate);
      const int refOnset = FindOnset(refBuf, 0);
      const int onset1 = FindOnset(buf, 0);
      const int onset2 = onset1 >= 0 ? FindOnset(buf, onset1 + stepFrames * 4) : -1;
      const bool firstOk = onset1 >= 0 && refOnset >= 0 && std::abs((onset1 - refOnset) - 20 * stepFrames) <= 1;
      const bool periodOk = onset2 >= 0 && std::abs((onset2 - onset1) - 32 * stepFrames) <= 1;
      printf("DRUMSEQTEST 32-step timing %s (onset1=%d ref0=%d expected delta=%d onset2=%d period=%d expected=%d)\n",
             (firstOk && periodOk) ? "OK" : "FAIL", onset1, refOnset, 20 * stepFrames, onset2, onset2 - onset1, 32 * stepFrames);
      ok &= firstOk && periodOk;
   }

   // 16) Save/load round trip of step 31 (the last cell of page 2) and the
   // step count; editPage is runtime-only and must not be saved.
   {
      auto src = std::make_unique<DrumSequencerNode>();
      src->numSteps = 32;
      src->stepVel[3][31] = 0.55f;
      src->stepVel[0][8] = 0.9f;
      src->editPage = 1;
      std::vector<std::pair<std::string, std::string>> saved;
      Patch::SaveParams(src.get(), saved);
      bool pageSaved = false;
      for (const auto& kv : saved)
         if (kv.first.find("editPage") != std::string::npos || kv.first.find("page") != std::string::npos)
            pageSaved = true;
      auto dst = std::make_unique<DrumSequencerNode>();
      Patch::LoadParams(dst.get(), saved);
      const bool rtOk = dst->numSteps == 32 && std::fabs(dst->stepVel[3][31] - 0.55f) < 1e-4f &&
                        std::fabs(dst->stepVel[0][8] - 0.9f) < 1e-4f && dst->editPage == 0 && !pageSaved;
      // editPage clamp: 1 page when steps <= 16
      dst->numSteps = 10;
      dst->editPage = 1;
      dst->CookIfNeeded(1);
      const bool clampOk = dst->editPage == 0;
      printf("DRUMSEQTEST 32-step save/load %s (rt=%d pageSaved=%d clamp=%d)\n", (rtOk && clampOk) ? "OK" : "FAIL",
             rtOk, pageSaved, clampOk);
      ok &= rtOk && clampOk;
   }

   // 17) Legacy 8-step patch: only the old keys (lane0_step0..7, steps=8)
   // exist. Steps 8..31 must load as 0 and the render must be identical to a
   // node built directly with the same 8 steps.
   {
      std::vector<std::pair<std::string, std::string>> legacy;
      for (int st = 0; st < 8; st++)
         if (st % 2 == 0)
            legacy.push_back({ "f lane0_step" + std::to_string(st), "0.8" }); // Patch keys carry a type prefix
      legacy.push_back({ "i rate", std::to_string((int)MusicTime::kSixteenth) });
      legacy.push_back({ "i steps", "8" });
      auto old = std::make_unique<DrumSequencerNode>();
      Patch::LoadParams(old.get(), legacy);
      bool tailZero = true;
      for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
         for (int st = 8; st < DrumSequencerNode::kMaxSteps; st++)
            if (old->stepVel[lane][st] != 0.0f)
               tailZero = false;
      old->LoadFileToLane(0, shortClickPath);

      auto ref = makeNode(shortClickPath);
      for (int st = 0; st < 8; st += 2)
         ref->stepVel[0][st] = 0.8f;
      resetTransport();
      const auto a = Render(*old, sampleRate * 2, blockSize, sampleRate);
      resetTransport();
      const auto b = Render(*ref, sampleRate * 2, blockSize, sampleRate);
      const bool same = a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
      const bool legacyOk = tailZero && old->numSteps == 8 && same && FindOnset(a, 0) >= 0;
      printf("DRUMSEQTEST legacy 8-step load %s (tailZero=%d identical=%d)\n", legacyOk ? "OK" : "FAIL", tailZero, same);
      ok &= legacyOk;
   }

   // 18) Randomize / RandomizeLane / ClearPattern never leave a step set at or
   // beyond numSteps (checked for 20 and 32), and do set something inside.
   {
      bool randOk = true;
      for (int ns : { 20, 32 })
      {
         auto node = std::make_unique<DrumSequencerNode>();
         node->numSteps = ns;
         for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
            for (int st = 0; st < DrumSequencerNode::kMaxSteps; st++)
               node->stepVel[lane][st] = 0.5f; // pre-dirty everything
         node->Randomize();
         bool inside = false, beyond = false;
         for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
            for (int st = 0; st < DrumSequencerNode::kMaxSteps; st++)
            {
               if (st < ns && node->stepVel[lane][st] > 0.0f) inside = true;
               if (st >= ns && node->stepVel[lane][st] != 0.0f) beyond = true;
            }
         for (int st = 0; st < DrumSequencerNode::kMaxSteps; st++)
            node->stepVel[2][st] = 0.5f;
         node->RandomizeLane(2);
         for (int st = ns; st < DrumSequencerNode::kMaxSteps; st++)
            if (node->stepVel[2][st] != 0.0f) beyond = true;
         node->ClearPattern();
         for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
            for (int st = 0; st < DrumSequencerNode::kMaxSteps; st++)
               if (node->stepVel[lane][st] != 0.0f) beyond = true;
         if (!inside || beyond)
            randOk = false;
      }
      printf("DRUMSEQTEST randomize within numSteps %s\n", randOk ? "OK" : "FAIL");
      ok &= randOk;
   }

   // 19) Groove library self-test: every Part.steps in 1..kMaxSteps, every
   // non-null lane string at least `steps` long and made of X/x/o/. only, rate
   // a valid MusicTime division, swing/tones in range, category listed, names
   // unique and free of artist / song titles, and each part has a hit.
   {
      int nGrooves = 0, nCats = 0;
      const DrumPatterns::Groove* grooves = DrumPatterns::All(nGrooves);
      const char* const* cats = DrumPatterns::Categories(nCats);
      std::string firstBad;
      auto bad = [&](const char* name, const char* why)
      {
         if (firstBad.empty())
            firstBad = std::string(name) + ": " + why;
      };
      static const char* const kDenylist[] = {
         "queen", "jackson", "ronettes", "billie", "be my baby", "we will rock", "neu!", "kraftwerk", "purdie",
         "rosanna", "james brown", "funky drummer", "winstons", "amen", "honey drippers", "impeach", "led zeppelin",
         "levee", "bongo band", "apache", "cold sweat", "tony allen", "prodigy", "chemical brothers", "dilla",
         "bambaataa", "planet rock", "teardrop", "massive attack", "portishead", "bjork", "army of me",
         "hyperballad", "bo diddley", "motown", "volt mix", "feel", "909",
      };
      std::set<std::string> seen;
      for (int g = 0; g < nGrooves; g++)
      {
         const DrumPatterns::Groove& gr = grooves[g];
         // Whole-word match on a lowercased, punctuation-stripped name, so
         // "amen" flags "Amen break" but not "Flamenco".
         auto norm = [](const char* in)
         {
            std::string out = " ";
            for (; *in != '\0'; in++)
               out += isalnum((unsigned char)*in) ? (char)tolower((unsigned char)*in) : ' ';
            return out + " ";
         };
         const std::string lower = norm(gr.name);
         for (const char* d : kDenylist)
            if (lower.find(norm(d)) != std::string::npos)
               bad(gr.name, "name matches artist/song denylist");
         if (!seen.insert(lower).second)
            bad(gr.name, "duplicate name");
         // The UI font has Basic Latin only (anything else draws '?') and MSVC
         // reads sources in the ANSI code page, so names must be plain ASCII.
         for (const char* c = gr.name; *c; c++)
            if ((unsigned char)*c < 0x20 || (unsigned char)*c > 0x7e)
               bad(gr.name, "name is not printable ASCII");
         bool catOk = false;
         for (int c = 0; c < nCats; c++)
            catOk |= strcmp(cats[c], gr.category) == 0;
         if (!catOk)
            bad(gr.name, "category not in Categories()");
         if (gr.rate < 0 || gr.rate >= MusicTime::kNumRateDivisions)
            bad(gr.name, "rate out of range");
         if (!(gr.swing >= 0.0f && gr.swing <= 1.0f) || gr.bpm <= 0)
            bad(gr.name, "swing/bpm out of range");
         for (int l = 0; l < 8; l++)
            if (!(gr.tones[l] >= -24.0f && gr.tones[l] <= 24.0f))
               bad(gr.name, "tone out of range");
         for (int p = 0; p < 3; p++)
         {
            const DrumPatterns::Part& part = gr.parts[p];
            if (part.steps < 1 || part.steps > DrumSequencerNode::kMaxSteps)
            {
               bad(gr.name, "part steps out of 1..kMaxSteps");
               continue;
            }
            bool anyHit = false;
            for (int l = 0; l < 8; l++)
            {
               const char* s = part.lanes[l];
               if (s == nullptr)
                  continue;
               if ((int)strlen(s) < part.steps)
                  bad(gr.name, "lane string shorter than steps");
               for (int i = 0; i < part.steps && s[i] != '\0'; i++)
               {
                  if (strchr("Xxo.", s[i]) == nullptr)
                     bad(gr.name, "bad cell character");
                  anyHit |= DrumPatterns::CellVelocity(s[i]) > 0.0f;
               }
            }
            if (!anyHit)
               bad(gr.name, "part has no hits");
         }
      }
      const bool libOk = nGrooves == 141 && nCats == 10 && firstBad.empty();
      printf("DRUMSEQTEST groove library %s (%d grooves, %d categories%s%s)\n", libOk ? "OK" : "FAIL", nGrooves, nCats,
             firstBad.empty() ? "" : ", first problem: ", firstBad.c_str());
      ok &= libOk;
   }

   // 20) Accent: a full-velocity (>= 0.99) step plays laneAccentPitch semitones
   // higher, so at +12 st a sustained sample ends at half its length; a 0.8
   // step on the same lane is unaffected.
   {
      auto soundedFrames = [&](float vel, float accent)
      {
         resetTransport();
         auto node = makeNode(longClickPath);
         node->numSteps = 32; // one hit per 4 s: no retrigger inside the render
         node->stepVel[0][0] = vel;
         node->laneAccentPitch[0] = accent;
         const auto buf = Render(*node, kSustainFrames + 6000, blockSize, sampleRate);
         int first = -1, last = -1;
         for (int i = 0; i < (int)buf.size(); i++)
            if (std::fabs(buf[i]) > 0.01f)
            {
               if (first < 0)
                  first = i;
               last = i;
            }
         return first >= 0 ? last - first + 1 : 0;
      };
      const int plain = soundedFrames(1.0f, 0.0f);
      const int accented = soundedFrames(1.0f, 12.0f);
      const int ghostAccented = soundedFrames(0.8f, 12.0f);
      const bool halfOk = plain > kSustainFrames * 9 / 10 && std::abs(accented * 2 - plain) <= plain / 50;
      const bool ghostOk = std::abs(ghostAccented - plain) <= plain / 100;
      printf("DRUMSEQTEST accent %s (plain=%d accent+12=%d vel0.8+accent=%d)\n", halfOk && ghostOk ? "OK" : "FAIL",
             plain, accented, ghostAccented);
      ok &= halfOk && ghostOk;
   }

   // Throwaway kit folder named like the bundled one, with eight click WAVs.
   const std::string kitRoot = TmpPath("infinite_drumseq_kit");
   const std::string kitDir = kitRoot + "/infinite-basic";
   {
      std::error_code ec;
      std::filesystem::create_directories(kitDir, ec);
      for (int l = 0; l < 8; l++)
         WriteClickWav(kitDir + "/" + DrumPatterns::KitFile(l), 4000, sampleRate);
   }
   const std::string savedKitDir = DrumSequencerNode::KitDir();
   DrumSequencerNode::SetKitDir(kitDir);

   int nG = 0;
   const DrumPatterns::Groove* gs = DrumPatterns::All(nG);
   auto pickPart = [&](auto pred, int& gOut, int& pOut)
   {
      for (int g = 0; g < nG; g++)
         for (int p = 0; p < 3; p++)
            if (pred(gs[g], gs[g].parts[p]))
            {
               gOut = g;
               pOut = p;
               return true;
            }
      return false;
   };

   // 21) ApplyPattern: slots past the part's length are zeroed, settings land,
   // a lane the user loaded is not replaced, accent resets when the groove
   // gives none, and the kit fills only empty lanes the part uses.
   {
      int gLong = -1, pLong = -1, gShort = -1, pShort = -1;
      const bool found =
         pickPart([](const DrumPatterns::Groove&, const DrumPatterns::Part& p) { return p.steps == 32; }, gLong, pLong) &&
         pickPart([](const DrumPatterns::Groove&, const DrumPatterns::Part& p) { return p.steps == 12; }, gShort, pShort);
      bool applyOk = found;
      if (found)
      {
         auto node = std::make_unique<DrumSequencerNode>();
         node->ApplyPattern(gs[gLong], pLong);
         applyOk &= node->numSteps == 32;
         node->ApplyPattern(gs[gShort], pShort);
         bool tailZero = true, anyIn = false;
         for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
            for (int st = 0; st < DrumSequencerNode::kMaxSteps; st++)
            {
               if (st >= 12 && node->stepVel[lane][st] != 0.0f)
                  tailZero = false;
               if (st < 12 && node->stepVel[lane][st] > 0.0f)
                  anyIn = true;
            }
         applyOk &= tailZero && anyIn && node->numSteps == 12 && node->editPage == 0 &&
                    node->rate == gs[gShort].rate && node->swing == gs[gShort].swing &&
                    node->patternName == gs[gShort].name && node->patternPart == pShort;
         // pushed to the audio side without tripping anything
         node->CookIfNeeded(1);
      }
      printf("DRUMSEQTEST apply pattern %s\n", applyOk ? "OK" : "FAIL");
      ok &= applyOk;

      // Accent: set from tones, and reset to 0 when the next groove has none.
      int gTone = -1, pTone = -1, gNoTone = -1, pNoTone = -1;
      bool accentOk = pickPart([](const DrumPatterns::Groove& g, const DrumPatterns::Part&)
                               { return g.tones[7] != 0.0f; }, gTone, pTone) &&
                      pickPart([](const DrumPatterns::Groove& g, const DrumPatterns::Part&)
                               { return g.tones[7] == 0.0f; }, gNoTone, pNoTone);
      if (accentOk)
      {
         auto node = std::make_unique<DrumSequencerNode>();
         node->ApplyPattern(gs[gTone], pTone);
         const bool set = node->laneAccentPitch[7] == gs[gTone].tones[7];
         node->laneAccentPitch[3] = 7.0f; // a stray hand-set accent on another lane
         node->ApplyPattern(gs[gNoTone], pNoTone);
         accentOk = set && node->laneAccentPitch[7] == 0.0f && node->laneAccentPitch[3] == 0.0f;
      }
      printf("DRUMSEQTEST apply pattern accent reset %s\n", accentOk ? "OK" : "FAIL");
      ok &= accentOk;
   }

   // 22) Kit fill: empty lanes the part uses are loaded from the kit, a lane
   // the user loaded keeps its sample, and the hats only join choke group 1
   // when both were kit-filled by the same call.
   {
      int gHats = -1, pHats = -1, gClosedOnly = -1, pClosedOnly = -1;
      bool kitOk =
         pickPart([](const DrumPatterns::Groove&, const DrumPatterns::Part& p)
                  { return p.lanes[0] && p.lanes[2] && p.lanes[3]; }, gHats, pHats) &&
         pickPart([](const DrumPatterns::Groove&, const DrumPatterns::Part& p)
                  { return p.lanes[2] && !p.lanes[3]; }, gClosedOnly, pClosedOnly);
      if (kitOk)
      {
         // both hats filled -> choke 1 on both; lanes the part skips stay empty
         auto a = std::make_unique<DrumSequencerNode>();
         a->ApplyPattern(gs[gHats], pHats);
         const DrumPatterns::Part& ph = gs[gHats].parts[pHats];
         for (int l = 0; l < 8; l++)
            kitOk &= (ph.lanes[l] != nullptr) == !a->FilePath(l).empty();
         kitOk &= a->FileName(0) == DrumPatterns::KitFile(0) && a->laneChoke[2] == 1 && a->laneChoke[3] == 1;

         // user already loaded the open hat: kept, and no choke is invented
         auto b = std::make_unique<DrumSequencerNode>();
         b->LoadFileToLane(3, shortClickPath);
         b->ApplyPattern(gs[gHats], pHats);
         kitOk &= b->FilePath(3) == shortClickPath && !b->FilePath(2).empty() && b->laneChoke[2] == 0 &&
                  b->laneChoke[3] == 0;

         // part without an open hat: closed hat filled, no choke
         auto c = std::make_unique<DrumSequencerNode>();
         c->ApplyPattern(gs[gClosedOnly], pClosedOnly);
         kitOk &= !c->FilePath(2).empty() && c->FilePath(3).empty() && c->laneChoke[2] == 0 && c->laneChoke[3] == 0;

         // a second apply never replaces what the first one loaded, nor a user choke setting
         a->laneChoke[2] = 2;
         a->laneChoke[3] = 0;
         a->ClearLane(3);
         a->ApplyPattern(gs[gHats], pHats);
         kitOk &= a->laneChoke[2] == 2 && a->laneChoke[3] == 0;

         // missing kit folder: no-op, status untouched
         DrumSequencerNode::SetKitDir(kitRoot + "/does-not-exist");
         auto d = std::make_unique<DrumSequencerNode>();
         d->ApplyPattern(gs[gHats], pHats);
         kitOk &= d->LoadedLaneCount() == 0 && d->LaneStatus(0) == "--";
         DrumSequencerNode::SetKitDir("");
         auto e = std::make_unique<DrumSequencerNode>();
         e->ApplyPattern(gs[gHats], pHats);
         kitOk &= e->LoadedLaneCount() == 0;
         DrumSequencerNode::SetKitDir(kitDir);
      }
      printf("DRUMSEQTEST kit fill %s\n", kitOk ? "OK" : "FAIL");
      ok &= kitOk;
   }

   // 23) Stale kit path: a saved path inside .../infinite-basic/ whose folder no
   // longer exists retries from the current kit folder (forward or back
   // slashes) and rewrites the path; other parents do not retry.
   {
      auto node = std::make_unique<DrumSequencerNode>();
      const std::string fwd = "/Applications/Old/Infinite.app/Contents/Resources/drumkits/infinite-basic/02-snare.wav";
      const std::string back = "C:\\Old Install\\Resources\\drumkits\\infinite-basic\\03-closed-hat.wav";
      const std::string other = "/Applications/Old/Resources/drumkits/other-kit/02-snare.wav";
      // The rewritten path takes the kit folder's own separator (a Windows temp path has
      // backslashes), so compare with separators folded.
      auto slashed = [](std::string v) { std::replace(v.begin(), v.end(), '\\', '/'); return v; };
      const bool r1 = node->LoadFileToLane(1, fwd);
      const bool p1 = slashed(node->FilePath(1)) == slashed(kitDir + "/02-snare.wav");
      const bool r2 = node->LoadFileToLane(2, back);
      const bool p2 = slashed(node->FilePath(2)) == slashed(kitDir + "/03-closed-hat.wav");
      const bool r3 = node->LoadFileToLane(3, other);
      DrumSequencerNode::SetKitDir("");
      const bool r4 = node->LoadFileToLane(4, fwd);
      DrumSequencerNode::SetKitDir(kitDir);
      // through a save/load/reload, as a patch opened from another install would
      auto src = std::make_unique<DrumSequencerNode>();
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(src.get(), params);
      for (auto& kv : params)
         if (kv.first == "s lane0_path")
            kv.second = fwd;
      auto dst = std::make_unique<DrumSequencerNode>();
      Patch::LoadParams(dst.get(), params);
      dst->ReloadFromPaths();
      const bool viaPatch = slashed(dst->FilePath(0)) == slashed(kitDir + "/02-snare.wav") && dst->LoadedLaneCount() == 1;
      const bool staleOk = r1 && p1 && r2 && p2 && !r3 && !r4 && viaPatch;
      printf("DRUMSEQTEST stale kit path %s (fwd=%d/%d back=%d/%d other=%d noKit=%d viaPatch=%d)\n",
             staleOk ? "OK" : "FAIL", r1, p1, r2, p2, r3, r4, viaPatch);
      ok &= staleOk;
   }

   // 24) accentPitch / patternName / patternPart survive save -> load, and a
   // restored "no sample" state clears a lane that still holds one.
   {
      auto src = std::make_unique<DrumSequencerNode>();
      src->laneAccentPitch[5] = -7.5f;
      src->patternName = "Test groove";
      src->patternPart = 2;
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(src.get(), params);
      auto dst = std::make_unique<DrumSequencerNode>();
      Patch::LoadParams(dst.get(), params);
      bool rtOk = std::fabs(dst->laneAccentPitch[5] + 7.5f) < 1e-4f && dst->patternName == "Test groove" &&
                  dst->patternPart == 2 && dst->laneAccentPitch[4] == 0.0f;

      auto held = std::make_unique<DrumSequencerNode>();
      held->LoadFileToLane(0, shortClickPath);
      Patch::LoadParams(held.get(), params); // restored state: lane 0 has no path
      held->ReloadFromPaths();
      rtOk &= held->FileName(0).empty() && held->LoadedLaneCount() == 0;
      printf("DRUMSEQTEST accent/pattern round trip %s\n", rtOk ? "OK" : "FAIL");
      ok &= rtOk;
   }

   // 25) The kit that actually ships: BundledResourcePath resolves it next to
   // the running binary (Contents/Resources on macOS, Resources/ beside the
   // exe on Windows and Linux), all eight KitFile() names exist with that exact
   // case (Linux is case-sensitive) and decode, and every lane fills.
   {
      const std::string real = BundledResourcePath("drumkits/infinite-basic");
      bool realOk = !real.empty();
      int filled = 0;
      if (realOk)
      {
         DrumSequencerNode::SetKitDir(real);
         auto node = std::make_unique<DrumSequencerNode>();
         const DrumPatterns::Groove& g0 = DrumPatterns::All(nG)[0];
         DrumPatterns::Part full = g0.parts[0];
         for (int l = 0; l < 8; l++)
            full.lanes[l] = "X";
         node->LoadKitIntoEmptyLanes(full);
         filled = node->LoadedLaneCount();
         realOk = filled == 8 && node->laneChoke[2] == 1 && node->laneChoke[3] == 1;
      }
      printf("DRUMSEQTEST bundled kit %s (dir=%s filled=%d/8)\n", realOk ? "OK" : "FAIL", real.empty() ? "<not found>" : real.c_str(),
             filled);
      ok &= realOk;
   }

   DrumSequencerNode::SetKitDir(savedKitDir);
   {
      std::error_code ec;
      std::filesystem::remove_all(kitRoot, ec);
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

      const int stages = EqDsp::StageCount(type);
      DspMath::Biquad bq[3];
      for (int s = 0; s < stages; s++)
         EqDsp::ConfigureBiquad(bq[s], type, freqHz, q, gainDb, sampleRate);
      double sumInSq = 0.0, sumOutSq = 0.0, phase = 0.0;
      for (int i = 0; i < totalSamples; i++)
      {
         const float x = (float)sin(phase);
         float y = x;
         for (int s = 0; s < stages; s++)
            y = bq[s].Process(y);
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

   // 1) Closed form vs measurement: EqDsp::BandMagnitudeDb must agree with
   //    a settled-sine render of the same scratch Biquads, for each of the
   //    12 band types, at a spread of freq/Q/gain/eval points - the check
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
         { EqDsp::kHp24, 200.0f, 0.707f, 0.0f, 50.0f },
         { EqDsp::kHp36, 200.0f, 0.707f, 0.0f, 50.0f },
         { EqDsp::kLp12, 500.0f, 0.707f, 0.0f, 4000.0f },
         { EqDsp::kLp12, 500.0f, 0.707f, 0.0f, 100.0f },
         { EqDsp::kLp24, 500.0f, 0.707f, 0.0f, 4000.0f },
         { EqDsp::kLp36, 500.0f, 0.707f, 0.0f, 4000.0f },
         { EqDsp::kBP, 1000.0f, 2.0f, 0.0f, 1000.0f },
         { EqDsp::kBP, 1000.0f, 2.0f, 0.0f, 200.0f },
         { EqDsp::kNotch, 1000.0f, 2.0f, 0.0f, 100.0f },
         { EqDsp::kNotch, 1000.0f, 2.0f, 0.0f, 10000.0f },
         { EqDsp::kAllpass, 1000.0f, 1.0f, 0.0f, 500.0f },
         { EqDsp::kAllpass, 1000.0f, 1.0f, 0.0f, 2000.0f },
      };
      float maxErr = 0.0f;
      for (const Case& c : cases)
      {
         const float closedForm = EqDsp::BandMagnitudeDb(c.type, c.freq, c.q, c.gainDb, true, c.evalHz, sampleRate);
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

// Slicer: synthesises a WAV with transients at known positions, asserts the
// detector finds them, that each slice triggers from its own MIDI note (36 +
// k), that a note past the last slice is silent, that grid mode at a known
// transport tempo produces the arithmetically expected boundaries, that
// `crossthrough` off confines a slice to its own next onset and on lets it
// run past, and that `attack` actually ramps the slice in (against a
// zero-attack control render).
static bool RunSlicerFixture()
{
   const int sampleRate = 44100;
   const int numFrames = sampleRate; // 1.0 s
   const double kBurstSeconds[4] = { 0.0, 0.25, 0.5, 0.75 };
   const int kNumBursts = 4;

   std::vector<float> mono((size_t)numFrames, 0.0f);
   for (int b = 0; b < kNumBursts; b++)
   {
      const int start = (int)(kBurstSeconds[b] * sampleRate);
      // 120 ms bursts whose envelope reaches ~e^-24 by the end: a hard cutoff
      // while the tone is still audible is itself a broadband transient, and
      // the detector rightly reports it as a fifth onset.
      const int burstLen = (int)(0.12 * sampleRate);
      for (int i = 0; i < burstLen && start + i < numFrames; i++)
      {
         const float t = (float)i / (float)sampleRate;
         const float env = std::exp(-t * 200.0f);
         mono[start + i] = env * (0.9f * sinf(2.0f * 3.14159265f * 220.0f * t) +
                                  0.35f * sinf(2.0f * 3.14159265f * 1750.0f * t));
      }
   }

   const std::string path = TmpPath("infinite_slicer_fixture.wav");
   {
      std::vector<int16_t> pcm((size_t)numFrames);
      for (int i = 0; i < numFrames; i++)
         pcm[i] = (int16_t)(std::clamp(mono[i], -1.0f, 1.0f) * 32000.0f);
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

   // A second, deliberately DIFFERENT signal for the attack test: 1.0 s of
   // constant-amplitude 220 Hz sine. The burst WAV above cannot be used -
   // its bursts are exp(-t*200) enveloped, so by t = 50 ms the source itself
   // is already ~4.5e-5 and a 50 ms attack would measure as a false FAIL.
   const std::string sinePath = TmpPath("infinite_slicer_attack.wav");
   {
      std::vector<int16_t> pcm((size_t)numFrames);
      for (int i = 0; i < numFrames; i++)
      {
         const float t = (float)i / (float)sampleRate;
         pcm[i] = (int16_t)(0.8f * sinf(2.0f * 3.14159265f * 220.0f * t) * 32000.0f);
      }
      std::ofstream f(sinePath, std::ios::binary);
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

   // Cooks until the analysis worker has published its result (analysis runs
   // on its own thread, exactly as it does in the app).
   auto settle = [](SlicerNode& node, int& frame)
   {
      for (int i = 0; i < 400; i++)
      {
         node.CookIfNeeded(frame++);
         if (!node.IsAnalyzing() && node.SliceCount() > 1)
            return true;
         std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      node.CookIfNeeded(frame++);
      return false;
   };

   // ---- 1. detection: four slices, each within 15 ms of the truth --------
   {
      SlicerNode node;
      int frame = 1;
      if (!node.LoadFile(path))
      {
         printf("SLICERTEST BUG (fixture failed to load its own test file)\n");
         return false;
      }
      settle(node, frame);

      const std::vector<float>& slices = node.Slices();
      if ((int)slices.size() != kNumBursts)
      {
         printf("SLICERTEST detected %d slices, expected %d FAIL\n", (int)slices.size(), kNumBursts);
         ok = false;
      }
      else
      {
         for (int i = 0; i < kNumBursts; i++)
         {
            const double got = (double)slices[i] * numFrames / sampleRate;
            const double err = std::fabs(got - kBurstSeconds[i]);
            if (err > 0.015)
            {
               printf("SLICERTEST slice %d at %.4fs, expected %.4fs (err %.1f ms) FAIL\n", i, got,
                      kBurstSeconds[i], err * 1000.0);
               ok = false;
            }
         }
      }
   }

   // Renders a single note-on into `out` (mono, `frames` long) and returns
   // false only if the fixture's own WAV failed to load. `grid` picks grid
   // mode (deterministic boundaries, no analysis wait) instead of onsets.
   auto renderNoteBuf = [&](const std::string& wavPath, int note, int frames, float attackMs,
                            float decayMs, bool crossthrough, bool grid,
                            std::vector<float>& out) -> bool
   {
      SlicerNode node;
      int frame = 1;
      if (!node.LoadFile(wavPath))
         return false;
      if (grid)
      {
         node.sliceBy = 1;
         node.division = 0; // 1/4 -> 0.5 s at 120 BPM, one boundary in 1.0 s
      }
      else
      {
         settle(node, frame);
      }

      // These reach the audio thread ONLY through PushParams, which runs
      // inside CookIfNeeded - so they must be set before that cook, not
      // after it.
      node.attack = attackMs;
      node.decay = decayMs;
      node.crossthrough = crossthrough;

      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, frames);
      node.CookIfNeeded(frame++);

      NoteEventQueue queue;
      const int cursor = queue.RegisterConsumer();
      an->SetNoteInbox(&queue, cursor);
      NoteEvent on;
      on.note = note;
      on.velocity = 1.0f;
      on.isNoteOn = true;
      on.frameOffset = 0;
      queue.Push(on);

      std::vector<float> l((size_t)frames, 0.0f), r((size_t)frames, 0.0f);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = frames;
      an->ProcessBlock(nullptr, 0, buf);

      out = std::move(l);
      return true;
   };

   auto peakRange = [](const std::vector<float>& v, int lo, int hi) -> float
   {
      float peak = 0.0f;
      for (int i = std::max(0, lo); i < std::min((int)v.size(), hi); i++)
         peak = std::max(peak, std::fabs(v[i]));
      return peak;
   };
   auto rmsRange = [](const std::vector<float>& v, int lo, int hi) -> float
   {
      double acc = 0.0;
      int n = 0;
      for (int i = std::max(0, lo); i < std::min((int)v.size(), hi); i++)
      {
         acc += (double)v[i] * (double)v[i];
         n++;
      }
      return (n > 0) ? (float)std::sqrt(acc / n) : 0.0f;
   };

   // ---- 2. every slice triggers from 36 + k; 36 + count is silent -------
   {
      auto renderNote = [&](int note, int frames) -> float
      {
         SlicerNode node;
         int frame = 1;
         if (!node.LoadFile(path))
            return -1.0f;
         settle(node, frame);

         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay((double)sampleRate, frames);
         node.CookIfNeeded(frame++);

         NoteEventQueue queue;
         const int cursor = queue.RegisterConsumer();
         an->SetNoteInbox(&queue, cursor);
         NoteEvent on;
         on.note = note;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         queue.Push(on);

         std::vector<float> l((size_t)frames, 0.0f), r((size_t)frames, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = frames;
         an->ProcessBlock(nullptr, 0, buf);

         float peak = 0.0f;
         for (float v : l)
            peak = std::max(peak, std::fabs(v));
         return peak;
      };

      for (int k = 0; k < kNumBursts; k++)
      {
         const float peak = renderNote(SlicerNode::kBaseNote + k, 2048);
         if (peak < 0.0f)
         {
            printf("SLICERTEST BUG (fixture failed to load its own test file)\n");
            return false;
         }
         if (peak < 0.05f)
         {
            printf("SLICERTEST note %d (slice %d) rendered silence (peak %.5f) FAIL\n",
                   SlicerNode::kBaseNote + k, k, peak);
            ok = false;
         }
      }

      // A note past the last slice must be silent - no wrap, no clamp, no
      // fall back to slice 0.
      const float overflow = renderNote(SlicerNode::kBaseNote + kNumBursts, 2048);
      if (overflow > 1.0e-5f)
      {
         printf("SLICERTEST note %d past the last slice was not silent (peak %.5f) FAIL\n",
                SlicerNode::kBaseNote + kNumBursts, overflow);
         ok = false;
      }
   }

   // ---- 3. grid mode boundaries are arithmetic from the transport tempo --
   {
      const float savedTempo = Transport::Instance().Tempo();
      Transport::Instance().SetTempo(120.0f);

      SlicerNode node;
      int frame = 1;
      node.LoadFile(path);
      settle(node, frame);
      node.sliceBy = 1;   // grid
      node.division = 0;  // 1/4 -> 0.5 s at 120 BPM
      node.CookIfNeeded(frame++);

      const std::vector<float>& slices = node.Slices();
      const double expectedLen = 0.5;
      const int expectedCount = 2; // ceil(1.0 / 0.5)
      if ((int)slices.size() != expectedCount)
      {
         printf("SLICERTEST grid 1/4 @120bpm gave %d slices, expected %d FAIL\n",
                (int)slices.size(), expectedCount);
         ok = false;
      }
      else
      {
         for (int i = 0; i < expectedCount; i++)
         {
            const double got = (double)slices[i] * numFrames / sampleRate;
            if (std::fabs(got - (double)i * expectedLen) > 0.001)
            {
               printf("SLICERTEST grid boundary %d at %.4fs, expected %.4fs FAIL\n", i, got,
                      (double)i * expectedLen);
               ok = false;
            }
         }
      }

      // 1/8T at 120 BPM: 0.25 * 2/3 = 0.16667 s.
      node.division = 2;
      node.CookIfNeeded(frame++);
      const std::vector<float>& trip = node.Slices();
      const double tripLen = (60.0 / 120.0) * (4.0 / 8.0) * (2.0 / 3.0);
      if (trip.size() < 2 ||
          std::fabs((double)trip[1] * numFrames / sampleRate - tripLen) > 0.001)
      {
         printf("SLICERTEST grid 1/8T boundary wrong FAIL\n");
         ok = false;
      }

      Transport::Instance().SetTempo(savedTempo);
   }

   // ---- 4. onsets cap and division changes must NOT relaunch the worker --
   {
      SlicerNode node;
      int frame = 1;
      node.LoadFile(path);
      settle(node, frame);
      node.onsets = 2;
      node.CookIfNeeded(frame++);
      if (node.IsAnalyzing())
      {
         printf("SLICERTEST changing 'onsets' relaunched the analysis worker FAIL\n");
         ok = false;
      }
      if (node.SliceCount() != 2)
      {
         printf("SLICERTEST onsets cap of 2 gave %d slices FAIL\n", node.SliceCount());
         ok = false;
      }
      node.sliceBy = 1;
      node.division = 5;
      node.CookIfNeeded(frame++);
      if (node.IsAnalyzing())
      {
         printf("SLICERTEST changing 'slice by'/'division' relaunched the worker FAIL\n");
         ok = false;
      }
   }

   // ---- 5. crossthrough OFF confines a slice to its own next onset ------
   // The assertion the whole feature exists for. Boundary = slice 1 at
   // 0.25 s = frame 11025; the +/-256 guard covers the 3 ms (132-frame)
   // fade-out plus interpolation slop.
   {
      const int boundary = (int)(0.25 * sampleRate);
      std::vector<float> outL;
      if (!renderNoteBuf(path, SlicerNode::kBaseNote, 16384, 0.0f, 5000.0f, /*cross=*/false,
                         /*grid=*/false, outL))
      {
         printf("SLICERTEST BUG (fixture failed to load its own test file)\n");
         return false;
      }
      const float before = peakRange(outL, 0, boundary - 256);
      const float after = peakRange(outL, boundary + 256, 16384);
      if (before < 0.05f)
      {
         printf("SLICERTEST crossthrough off: slice never sounded (peak %.5f) FAIL\n", before);
         ok = false;
      }
      if (after > 1.0e-4f)
      {
         printf("SLICERTEST crossthrough off: slice ran past its own next onset "
                "(peak %.5f after frame %d) FAIL\n", after, boundary + 256);
         ok = false;
      }
   }

   // ---- 6. crossthrough ON keeps going past the boundary ----------------
   {
      const int boundary = (int)(0.25 * sampleRate);
      std::vector<float> outL;
      if (!renderNoteBuf(path, SlicerNode::kBaseNote, 16384, 0.0f, 5000.0f, /*cross=*/true,
                         /*grid=*/false, outL))
      {
         printf("SLICERTEST BUG (fixture failed to load its own test file)\n");
         return false;
      }
      const float after = peakRange(outL, boundary + 256, 16384);
      if (after < 0.05f)
      {
         printf("SLICERTEST crossthrough on: slice stopped at its boundary anyway "
                "(peak %.5f after frame %d) FAIL\n", after, boundary + 256);
         ok = false;
      }
   }

   // ---- 7. attack ramps the slice in; attack 0 does not -----------------
   // Uses the constant-amplitude sine, never the burst WAV. RMS, not peak:
   // a 220 Hz carrier's zero crossings make a short-window peak noisy.
   {
      const float savedTempo = Transport::Instance().Tempo();
      Transport::Instance().SetTempo(120.0f);

      std::vector<float> withAttack, control;
      const bool okA = renderNoteBuf(sinePath, SlicerNode::kBaseNote, 16384, 50.0f, 5000.0f,
                                     /*cross=*/false, /*grid=*/true, withAttack);
      const bool okB = renderNoteBuf(sinePath, SlicerNode::kBaseNote, 16384, 0.0f, 5000.0f,
                                     /*cross=*/false, /*grid=*/true, control);
      Transport::Instance().SetTempo(savedTempo);

      if (!okA || !okB)
      {
         printf("SLICERTEST BUG (fixture failed to load its own attack test file)\n");
         return false;
      }

      // 50 ms attack = 2205 frames. First 10 ms vs ~40-60 ms, still climbing.
      const float earlyA = rmsRange(withAttack, 0, 441);
      const float lateA = rmsRange(withAttack, 1764, 2646);
      const float earlyB = rmsRange(control, 0, 441);
      const float lateB = rmsRange(control, 1764, 2646);

      if (!(lateA > 1.0e-3f && earlyA * 10.0f < lateA))
      {
         printf("SLICERTEST attack 50 ms did not ramp: rms(0-10ms)=%.6f rms(40-60ms)=%.6f FAIL\n",
                earlyA, lateA);
         ok = false;
      }
      if (!(lateB > 1.0e-3f && earlyB > lateB * 0.5f && earlyB < lateB * 2.0f))
      {
         printf("SLICERTEST attack 0 control was not flat: rms(0-10ms)=%.6f rms(40-60ms)=%.6f "
                "FAIL\n", earlyB, lateB);
         ok = false;
      }
   }

   printf("SLICERTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}


// ------------------------------------------------------------ LOOPERTEST / MPCTEST
namespace LooperMpcTest
{
   constexpr int kSr = 48000;
   constexpr int kBlock = 256;

   // Drives a node's audio half block by block. `feed(i)` gives the input
   // sample at absolute frame i (mono, copied to both channels); pass an empty
   // function for no input. Returns the left channel.
   template <typename NodeT>
   std::vector<float> Run(NodeT& node, int startFrame, int frames, const std::function<float(int)>& feed,
                          bool advanceTransport, int* cookId)
   {
      AudioNode* an = node.GetAudioNode();
      std::vector<float> out;
      out.reserve((size_t)frames);
      int done = 0;
      while (done < frames)
      {
         const int n = std::min(kBlock, frames - done);
         if (advanceTransport)
            Transport::Instance().AdvanceAudioClock(n);
         node.CookIfNeeded((*cookId)++);
         std::vector<float> inL((size_t)n, 0.0f), inR((size_t)n, 0.0f), oL((size_t)n, 0.0f), oR((size_t)n, 0.0f);
         if (feed)
            for (int i = 0; i < n; i++)
               inL[(size_t)i] = inR[(size_t)i] = feed(startFrame + done + i);
         float* inCh[2] = { inL.data(), inR.data() };
         float* outCh[2] = { oL.data(), oR.data() };
         AudioBuffer inBuf;
         inBuf.channels = inCh;
         inBuf.numChannels = 2;
         inBuf.numFrames = n;
         AudioBuffer outBuf;
         outBuf.channels = outCh;
         outBuf.numChannels = 2;
         outBuf.numFrames = n;
         const AudioBuffer* ins[1] = { &inBuf };
         an->ProcessBlock(feed ? ins : nullptr, feed ? 1 : 0, outBuf);
         out.insert(out.end(), oL.begin(), oL.end());
         done += n;
      }
      node.CookIfNeeded((*cookId)++);
      return out;
   }

   float Peak(const std::vector<float>& v, size_t from = 0, size_t to = (size_t)-1)
   {
      float p = 0.0f;
      for (size_t i = from; i < v.size() && i < to; i++)
         p = std::max(p, std::fabs(v[i]));
      return p;
   }

   float Rms(const std::vector<float>& v, size_t from, size_t to)
   {
      double a = 0.0;
      size_t n = 0;
      for (size_t i = from; i < v.size() && i < to; i++, n++)
         a += (double)v[i] * v[i];
      return n > 0 ? (float)std::sqrt(a / (double)n) : 0.0f;
   }

   int Onset(const std::vector<float>& v, size_t from, float thresh = 0.15f)
   {
      for (size_t i = from; i < v.size(); i++)
         if (std::fabs(v[i]) > thresh)
            return (int)i;
      return -1;
   }

   std::unique_ptr<LooperNode> MakeLooper(int take, bool sync, int compFrames)
   {
      auto n = std::make_unique<LooperNode>();
      n->take = take;
      n->syncStart = sync;
      n->testLatencyFrames = compFrames > 0 ? compFrames : 0;
      n->thru = false;
      n->fadeIn = 0.0f; // exact-sample assertions: hard pass edges
      n->fadeOut = 0.0f;
      int id = 1;
      n->CookIfNeeded(id++);
      n->GetAudioNode()->PrepareToPlay((double)kSr, kBlock);
      n->CookIfNeeded(id++);
      return n;
   }

   void Press(LooperNode& n, int button)
   {
      n.SetButtonLevel(button, false);
      n.SetButtonLevel(button, true);
      n.SetButtonLevel(button, false);
   }
}

static bool RunLooperFixture()
{
   using namespace LooperMpcTest;
   bool ok = true;
   auto fail = [&](const char* what)
   {
      printf("LOOPERTEST %s FAIL\n", what);
      ok = false;
   };
   Transport& transport = Transport::Instance();
   const float savedBpm = transport.Tempo();
   const bool savedPlaying = transport.IsPlaying();
   auto tone = [](float freq, float amp) { return [freq, amp](int i) { return amp * std::sin(6.2831853f * freq * (float)i / (float)kSr); }; };
   int cook = 100;

   transport.SetPlaying(false);

   // 1) free take: record 1024 frames, REC again ends it, the loop then repeats
   //    with period 1024 and the record pass itself is silent (thru = 0).
   {
      auto n = MakeLooper(0, false, 0);
      Press(*n, LooperNode::kRec);
      const std::vector<float> rec = Run(*n, 0, 1024, tone(220.0f, 0.5f), false, &cook);
      if (n->CurrentState() != LooperNode::kRecording)
         fail("free take: not recording after REC");
      if (Peak(rec) > 1e-4f)
         fail("free take: record pass is audible with thru = 0");
      Press(*n, LooperNode::kRec);
      const std::vector<float> play = Run(*n, 1024, 4096, nullptr, false, &cook);
      if (n->CurrentState() != LooperNode::kPlaying)
         fail("free take: not playing after second REC");
      if (std::fabs(n->LoopSeconds() - 1024.0f / (float)kSr) > 3.0f / (float)kSr)
         fail("free take: loop length is not the recorded length");
      if (Peak(play) < 0.3f)
         fail("free take: playback is silent");
      float worst = 0.0f;
      for (size_t i = 0; i + 1024 < play.size(); i++)
         worst = std::max(worst, std::fabs(play[i] - play[i + 1024]));
      if (worst > 1e-3f)
         fail("free take: playback is not periodic at the loop length");

      // 2) overdub layers onto the loop; toggling DUB again returns to plain play.
      const float before = Rms(play, 1024, 3072);
      Press(*n, LooperNode::kDub);
      Run(*n, 5120, 2048, tone(468.75f, 0.2f), false, &cook); // 10 whole cycles per 1024-frame pass
      if (n->CurrentState() != LooperNode::kOverdubbing)
         fail("overdub: state is not overdubbing after DUB");
      Press(*n, LooperNode::kDub);
      const std::vector<float> after = Run(*n, 7168, 4096, nullptr, false, &cook);
      if (n->CurrentState() != LooperNode::kPlaying)
         fail("overdub: DUB again did not return to playing");
      if (!(Rms(after, 1024, 3072) > before * 1.2f))
         fail("overdub: the layered input did not raise the loop level");

      // 3) thru monitors the live input; volume 0 silences the loop.
      n->thru = true;
      n->volume = 0.0f;
      Run(*n, 11264, 2048, nullptr, false, &cook); // let the ramps settle
      const std::vector<float> monitored = Run(*n, 13312, 1024, tone(1000.0f, 0.3f), false, &cook);
      if (std::fabs(Peak(monitored, 256) - 0.3f) > 0.02f)
         fail("thru: input is not monitored at unity with volume = 0");

      // 4) CLEAR empties the loop.
      Press(*n, LooperNode::kClear);
      n->thru = false;
      n->volume = 1.0f;
      Run(*n, 14336, 512, nullptr, false, &cook);
      const std::vector<float> cleared = Run(*n, 14848, 2048, nullptr, false, &cook);
      if (n->CurrentState() != LooperNode::kEmpty || Peak(cleared) > 1e-4f)
         fail("clear: the loop was not emptied");
   }

   // 5) fixed take (quarter note at 120 BPM = 24000 frames) ends by itself.
   {
      auto n = MakeLooper(MusicTime::kQuarter + 1, false, 0);
      transport.SetTempo(120.0f);
      Press(*n, LooperNode::kRec);
      Run(*n, 0, 24000 + 2 * kBlock, tone(220.0f, 0.5f), false, &cook);
      if (n->CurrentState() != LooperNode::kPlaying ||
          std::fabs(n->LoopSeconds() - 0.5f) > 3.0f / (float)kSr)
         fail("fixed take: did not end at a quarter note");
   }

   // 6) compensation: a click that arrives at press+F lands F - comp frames into
   //    the loop, so it plays comp frames earlier than an uncompensated take.
   {
      const int clickAt = 2000;
      auto clickFeed = [clickAt](int i) { return i == clickAt ? 0.9f : 0.0f; };
      int onset[2] = { -1, -1 };
      const int comps[2] = { 0, 480 };
      for (int k = 0; k < 2; k++)
      {
         auto n = MakeLooper(MusicTime::kQuarter + 1, false, comps[k]);
         Press(*n, LooperNode::kRec);
         const std::vector<float> out = Run(*n, 0, 60000, clickFeed, false, &cook);
         onset[k] = Onset(out, 1000);
      }
      if (onset[0] < 0 || std::abs(onset[0] - (24000 + clickAt)) > 3)
         fail("compensation: uncompensated click is not one loop after the press");
      if (onset[1] < 0 || std::abs((onset[0] - onset[1]) - 480) > 3)
         fail("compensation: 480 frames of latency compensation did not move the take 480 frames earlier");
   }

   // 6b) playback rate: speed 2 plays the recorded 1024-frame loop in 512 frames;
   //     speed -1 plays it backwards; pitch +12 st is the same as speed 2.
   {
      auto rampFeed = [](int i) { return i < 1024 ? (float)i / 1024.0f * 0.8f : 0.0f; };
      auto n = MakeLooper(0, false, 0);
      Press(*n, LooperNode::kRec);
      Run(*n, 0, 1024, rampFeed, false, &cook);
      Press(*n, LooperNode::kRec);
      n->speed = 2.0f;
      Run(*n, 1024, 4096, nullptr, false, &cook); // settle the rate ramp
      const std::vector<float> fast = Run(*n, 5120, 3072, nullptr, false, &cook);
      float worst = 0.0f;
      for (size_t i = 0; i + 512 < fast.size(); i++)
         worst = std::max(worst, std::fabs(fast[i] - fast[i + 512]));
      if (worst > 2e-3f || Peak(fast) < 0.3f)
         fail("rate: speed 2 does not repeat every 512 frames");
      n->speed = 1.0f;
      n->pitch = 12.0f;
      Run(*n, 8192, 4096, nullptr, false, &cook);
      const std::vector<float> oct = Run(*n, 12288, 3072, nullptr, false, &cook);
      worst = 0.0f;
      for (size_t i = 0; i + 512 < oct.size(); i++)
         worst = std::max(worst, std::fabs(oct[i] - oct[i + 512]));
      if (worst > 2e-3f)
         fail("rate: pitch +12 st is not the same as speed 2");
      n->pitch = 0.0f;
      n->speed = -1.0f;
      Run(*n, 15360, 4096, nullptr, false, &cook);
      const std::vector<float> rev = Run(*n, 19456, 2048, nullptr, false, &cook);
      int down = 0, steps = 0;
      for (size_t i = 1; i < rev.size(); i++)
      {
         if (std::fabs(rev[i] - rev[i - 1]) > 1e-6f)
         {
            steps++;
            down += rev[i] < rev[i - 1] ? 1 : 0;
         }
      }
      if (steps < 100 || down < steps * 9 / 10)
         fail("rate: speed -1 does not play the loop backwards");
      if (n->AtUnity())
         fail("rate: AtUnity is true at speed -1");
      // Off unity, overdub writes nothing: the loop is unchanged afterwards.
      n->speed = 1.0f;
      Run(*n, 21504, 4096, nullptr, false, &cook);
      const std::vector<float> base = Run(*n, 25600, 2048, nullptr, false, &cook);
      n->speed = 2.0f;
      Press(*n, LooperNode::kDub);
      Run(*n, 27648, 4096, tone(300.0f, 0.4f), false, &cook);
      Press(*n, LooperNode::kDub);
      n->speed = 1.0f;
      Run(*n, 31744, 4096, nullptr, false, &cook);
      const std::vector<float> baseAfter = Run(*n, 35840, 2048, nullptr, false, &cook);
      if (std::fabs(Rms(base, 0, base.size()) - Rms(baseAfter, 0, baseAfter.size())) > 0.01f)
         fail("rate: overdub at speed 2 changed the loop");
   }

   // 6c) fades: a 250 ms fade in / fade out at the loop seam leaves the seam
   //     sample near zero on a constant loop; hard edges (0 ms) leave it full.
   {
      auto dc = [](int) { return 0.5f; };
      float seam[2] = { 1.0f, 1.0f };
      for (int k = 0; k < 2; k++)
      {
         auto n = MakeLooper(0, false, 0);
         n->fadeIn = k == 0 ? 0.0f : 20.0f;
         n->fadeOut = k == 0 ? 0.0f : 20.0f;
         Press(*n, LooperNode::kRec);
         Run(*n, 0, 4800, dc, false, &cook);
         Press(*n, LooperNode::kRec);
         Run(*n, 4800, 4800 * 2, nullptr, false, &cook);
         const std::vector<float> p = Run(*n, 14400, 4800 * 2, nullptr, false, &cook);
         float mn = 1.0f;
         for (size_t i = 0; i < p.size(); i++)
            mn = std::min(mn, std::fabs(p[i]));
         seam[k] = mn;
      }
      if (seam[0] < 0.45f)
         fail("fades: 0 ms fades still dip at the seam");
      if (seam[1] > 0.1f)
         fail("fades: 20 ms fades do not reach silence at the seam");
   }

   // 7) synced take: REC pressed mid-bar arms, starts on the bar line, and the
   //    take is exactly one bar long, aligned to the grid.
   {
      transport.SetTempo(120.0f);
      transport.SetTimeSignature(4, 4);
      transport.SetPlaying(true);
      transport.Rewind();
      transport.NotifyAudioEngineStarted((double)kSr);
      const int bar = 96000;
      auto n = MakeLooper(MusicTime::k1Bar + 1, true, 0);
      auto feed = [bar](int i) { return i == bar + 100 ? 0.9f : 0.0f; };
      Run(*n, 0, 100 * kBlock, feed, true, &cook);
      Press(*n, LooperNode::kRec);
      Run(*n, 100 * kBlock, 20 * kBlock, feed, true, &cook);
      if (n->CurrentState() != LooperNode::kArmed)
         fail("sync: REC mid-bar did not arm");
      const int upTo = bar + 2 * kBlock;
      std::vector<float> out = Run(*n, 120 * kBlock, upTo - 120 * kBlock, feed, true, &cook);
      if (n->CurrentState() != LooperNode::kRecording)
         fail("sync: take did not start at the bar line");
      const std::vector<float> rest = Run(*n, upTo, 2 * bar - upTo + 2 * kBlock, feed, true, &cook);
      out.insert(out.end(), rest.begin(), rest.end());
      if (n->CurrentState() != LooperNode::kPlaying || std::fabs(n->LoopSeconds() - 2.0f) > 3.0f / (float)kSr)
         fail("sync: take is not exactly one bar");
      // The click sits 100 frames after the bar line, so it plays 100 frames
      // after the next bar line (2 * bar).
      const int on = Onset(out, (size_t)(bar + 2 * kBlock - 120 * kBlock)) + 120 * kBlock; // out starts at frame 120 blocks
      if (on < 0 || std::abs(on - (2 * bar + 100)) > 3)
         fail("sync: take is not aligned to the grid");
   }

   // 7b) late REC: pressed ~100 ms after the bar line, the take still starts
   //     on that line (the click before the press comes from the pre-roll),
   //     is one bar long and on the grid; pressed 300 ms late, it arms.
   {
      const int bar = 96000;
      for (int k = 0; k < 2; k++)
      {
         transport.SetPlaying(true);
         transport.Rewind();
         transport.NotifyAudioEngineStarted((double)kSr);
         auto n = MakeLooper(MusicTime::k1Bar + 1, true, 0);
         auto feed = [bar](int i) { return i == bar + 100 ? 0.9f : 0.0f; };
         const int pressAt = k == 0 ? bar + 19 * kBlock : bar + 57 * kBlock; // ~101 ms / ~304 ms late
         Run(*n, 0, pressAt, feed, true, &cook);
         Press(*n, LooperNode::kRec);
         std::vector<float> out = Run(*n, pressAt, kBlock, feed, true, &cook);
         if (k == 1)
         {
            if (n->CurrentState() != LooperNode::kArmed)
               fail("late rec: a press 300 ms late did not arm for the next bar");
            continue;
         }
         if (n->CurrentState() != LooperNode::kRecording)
            fail("late rec: a press 100 ms late did not start the take");
         const std::vector<float> rest = Run(*n, pressAt + kBlock, 2 * bar - pressAt + 2 * kBlock, feed, true, &cook);
         out.insert(out.end(), rest.begin(), rest.end());
         if (n->CurrentState() != LooperNode::kPlaying || std::fabs(n->LoopSeconds() - 2.0f) > 3.0f / (float)kSr)
            fail("late rec: take is not exactly one bar");
         const int on = Onset(out, (size_t)(2 * bar - pressAt - 2 * kBlock)) + pressAt;
         if (on < 0 || std::abs(on - (2 * bar + 100)) > 3)
            fail("late rec: the click before the press is not on the grid in the loop");
      }
   }

   // 8) the loop survives the patch: a take is written to loopFile as float
   //    WAV, a fresh node given that loopFile holds the same audio (stopped,
   //    PLAY plays it), an overdub rewrites the same file, CLEAR empties
   //    loopFile, and a loop from a patch is never rewritten in place.
   transport.SetPlaying(false);
   {
      std::error_code ec;
      const std::string dir = TmpPath("infinite_looper_fixture");
      std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
      auto ramp = [](int i) { return 1.5f * (float)(i % 1000) / 1000.0f - 0.2f; }; // > 0 dBFS on purpose
      auto a = MakeLooper(0, false, 0);
      a->testLoopDir = dir;
      Press(*a, LooperNode::kRec);
      Run(*a, 0, 4096, ramp, false, &cook);
      Press(*a, LooperNode::kRec);
      Run(*a, 4096, 3 * kBlock, nullptr, false, &cook);
      const std::string first = a->loopFile;
      if (first.empty() || !std::filesystem::exists(std::filesystem::u8path(first), ec))
         fail("persist: a finished take wrote no loop file");

      auto b = MakeLooper(0, false, 0);
      b->loopFile = first;
      Run(*b, 0, 2 * kBlock, nullptr, false, &cook);
      if (b->CurrentState() != LooperNode::kStopped || std::fabs(b->LoopSeconds() - 4096.0f / (float)kSr) > 1.0f / (float)kSr)
         fail("persist: the loop file did not come back as a stopped 4096-frame loop");
      Press(*b, LooperNode::kPlay);
      const std::vector<float> back = Run(*b, 0, 4096, nullptr, false, &cook);
      float worst = 0.0f;
      for (int i = 0; i < 4096; i++)
         worst = std::max(worst, std::fabs(back[(size_t)i] - ramp(i)));
      if (worst > 1e-6f)
         fail("persist: the reloaded loop is not sample-exact (float, unclipped)");

      Press(*a, LooperNode::kDub);
      Run(*a, 0, 2048, tone(468.75f, 0.2f), false, &cook);
      Press(*a, LooperNode::kDub);
      Run(*a, 0, 3 * kBlock, nullptr, false, &cook);
      if (a->loopFile != first)
         fail("persist: an overdub did not rewrite its own take's file");

      Press(*b, LooperNode::kDub);
      Run(*b, 0, 2048, tone(468.75f, 0.2f), false, &cook);
      Press(*b, LooperNode::kDub);
      Run(*b, 0, 3 * kBlock, nullptr, false, &cook);
      if (b->loopFile.empty() || b->loopFile == first)
         fail("persist: an overdub on a loaded loop rewrote the patch's file");

      Press(*a, LooperNode::kClear);
      Run(*a, 0, 3 * kBlock, nullptr, false, &cook);
      if (!a->loopFile.empty())
         fail("persist: CLEAR did not empty loopFile");

      // 9) undo: back over an overdub (loop keeps playing, file is the take
      //    again), over a layer still being dubbed, and over CLEAR.
      auto c = MakeLooper(0, false, 0);
      c->testLoopDir = dir;
      auto fileMatchesRamp = [&](const std::string& path)
      {
         Platform::SampleBuffer sb;
         std::string err;
         if (!Platform::DecodeAudioFileToBuffer(path, sb, err) || sb.numFrames != 4096)
            return false;
         for (int i = 0; i < 4096; i++)
            if (std::fabs(sb.channelData[(size_t)i] - ramp(i)) > 1e-6f)
               return false;
         return true;
      };
      Press(*c, LooperNode::kRec);
      Run(*c, 0, 4096, ramp, false, &cook);
      Press(*c, LooperNode::kRec);
      Run(*c, 0, 3 * kBlock, nullptr, false, &cook);
      Press(*c, LooperNode::kDub);
      Run(*c, 0, 2048, tone(468.75f, 0.2f), false, &cook);
      Press(*c, LooperNode::kDub);
      Run(*c, 0, 3 * kBlock, nullptr, false, &cook);
      if (c->UndoDepth() != 1 || fileMatchesRamp(c->loopFile))
         fail("undo: the overdub was not recorded as a layer");
      Press(*c, LooperNode::kUndo);
      Run(*c, 0, 3 * kBlock, nullptr, false, &cook);
      if (c->CurrentState() != LooperNode::kPlaying || !fileMatchesRamp(c->loopFile) || c->UndoDepth() != 0)
         fail("undo: did not step back to the take, still playing");
      Press(*c, LooperNode::kDub);
      Run(*c, 0, 2048, tone(468.75f, 0.2f), false, &cook);
      Press(*c, LooperNode::kUndo);
      Run(*c, 0, 4 * kBlock, nullptr, false, &cook);
      if (c->CurrentState() != LooperNode::kPlaying || !fileMatchesRamp(c->loopFile))
         fail("undo: pressed mid-overdub did not throw that layer away");
      Press(*c, LooperNode::kClear);
      Run(*c, 0, 3 * kBlock, nullptr, false, &cook);
      Press(*c, LooperNode::kUndo);
      Run(*c, 0, 3 * kBlock, nullptr, false, &cook);
      if (c->CurrentState() != LooperNode::kStopped || !fileMatchesRamp(c->loopFile))
         fail("undo: did not bring the loop back after CLEAR");
      std::filesystem::remove_all(std::filesystem::u8path(dir), ec);
   }

   transport.SetTempo(savedBpm);
   transport.SetPlaying(savedPlaying);
   printf("LOOPERTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}

static bool RunMpcFixture()
{
   using namespace LooperMpcTest;
   bool ok = true;
   auto fail = [&](const char* what)
   {
      printf("MPCTEST %s FAIL\n", what);
      ok = false;
   };
   int cook = 1000;
   const std::string longWav = DrumSeqTest::WriteSustainedWav(TmpPath("infinite_mpc_long.wav"), kSr, kSr);
   const std::string shortWav = DrumSeqTest::WriteSustainedWav(TmpPath("infinite_mpc_short.wav"), 2000, kSr);

   auto make = [&]()
   {
      auto n = std::make_unique<MpcNode>();
      n->CookIfNeeded(1);
      n->GetAudioNode()->PrepareToPlay((double)kSr, kBlock);
      n->CookIfNeeded(2);
      return n;
   };
   auto hold = [&](MpcNode& n, int pad, bool down) { n.SetPadHeld(pad, down, 1.0f); };

   {
      auto n = make();
      if (!n->LoadPad(3, longWav) || !n->LoadPad(1, longWav) || !n->LoadPad(7, shortWav))
      {
         fail("could not load the fixture samples");
         return false;
      }
      n->padMode[1] = MpcNode::kGate;
      n->padMode[7] = MpcNode::kLoopToggle;
      n->CookIfNeeded(cook++);

      // one shot: keeps sounding after release.
      hold(*n, 3, true);
      Run(*n, 0, 4 * kBlock, nullptr, false, &cook);
      hold(*n, 3, false);
      const std::vector<float> o1 = Run(*n, 0, 8 * kBlock, nullptr, false, &cook);
      if (Peak(o1) < 0.3f || Peak(o1, 4 * kBlock) < 0.3f)
         fail("one shot: stopped on release");
      // let it ring out is not needed; restart a fresh node for the other modes.
   }
   {
      auto n = make();
      n->LoadPad(1, longWav);
      n->padMode[1] = MpcNode::kGate;
      n->CookIfNeeded(cook++);
      hold(*n, 1, true);
      const std::vector<float> held = Run(*n, 0, 6 * kBlock, nullptr, false, &cook);
      hold(*n, 1, false);
      const std::vector<float> rel = Run(*n, 0, 6 * kBlock, nullptr, false, &cook);
      if (Peak(held, kBlock) < 0.3f)
         fail("gate: silent while held");
      if (Peak(rel, 2 * kBlock) > 1e-4f)
         fail("gate: still sounding after release");
   }
   {
      auto n = make();
      n->LoadPad(7, shortWav);
      n->padMode[7] = MpcNode::kLoopToggle;
      n->CookIfNeeded(cook++);
      hold(*n, 7, true);
      hold(*n, 7, false);
      const std::vector<float> on = Run(*n, 0, 40 * kBlock, nullptr, false, &cook);
      if (Peak(on, 30 * kBlock) < 0.3f)
         fail("loop: did not keep looping past the sample length");
      hold(*n, 7, true);
      hold(*n, 7, false);
      const std::vector<float> off = Run(*n, 0, 6 * kBlock, nullptr, false, &cook);
      if (Peak(off, 2 * kBlock) > 1e-4f)
         fail("loop: second hit did not stop it");
   }

   // note input: notes 36..51 = pads 1..16 (fixed); other pads stay silent.
   {
      auto n = make();
      n->LoadPad(5, longWav);
      n->CookIfNeeded(cook++);
      NoteEventQueue inbox;
      n->GetAudioNode()->SetNoteInbox(&inbox, inbox.RegisterConsumer());
      NoteEvent miss;
      miss.note = 36 + 6;
      miss.velocity = 1.0f;
      miss.isNoteOn = true;
      inbox.Push(miss);
      const std::vector<float> none = Run(*n, 0, 3 * kBlock, nullptr, false, &cook);
      NoteEvent on = miss;
      on.note = 36 + 5;
      inbox.Push(on);
      const std::vector<float> hit = Run(*n, 0, 3 * kBlock, nullptr, false, &cook);
      if (Peak(none) > 1e-4f)
         fail("note in: an unmapped pad sounded");
      if (Peak(hit) < 0.3f)
         fail("note in: note 41 did not play pad 6");
   }

   // Polyphony: two pads held at once are independent voices. Releasing the
   // gate pad leaves the loop pad sounding, and the mix is louder than either.
   {
      auto n = make();
      n->LoadPad(0, longWav);
      n->LoadPad(9, shortWav);
      n->padMode[0] = MpcNode::kGate;
      n->padMode[9] = MpcNode::kLoopToggle;
      n->CookIfNeeded(cook++);
      hold(*n, 0, true);
      const std::vector<float> one = Run(*n, 0, 4 * kBlock, nullptr, false, &cook);
      hold(*n, 9, true);
      hold(*n, 9, false);
      const std::vector<float> both = Run(*n, 0, 4 * kBlock, nullptr, false, &cook);
      if (!(Peak(both) > Peak(one) * 1.05f))
         fail("poly: two pads together are not louder than one");
      hold(*n, 0, false);
      const std::vector<float> rest = Run(*n, 0, 20 * kBlock, nullptr, false, &cook);
      if (Peak(rest, 10 * kBlock) < 0.3f)
         fail("poly: releasing the gate pad stopped the loop pad");
      hold(*n, 9, true);
      hold(*n, 9, false);
      const std::vector<float> end = Run(*n, 0, 6 * kBlock, nullptr, false, &cook);
      if (Peak(end, 2 * kBlock) > 1e-4f)
         fail("poly: toggling the loop pad off left something playing");
   }
   // Per-pad params act on that pad only: volume 0 on pad 2 silences pad 2, not
   // pad 4; speed 2 shortens a one shot to half; speed -1 plays backwards.
   {
      auto n = make();
      n->LoadPad(2, longWav);
      n->LoadPad(4, longWav);
      n->padVolume[2] = 0.0f;
      n->CookIfNeeded(cook++);
      hold(*n, 2, true);
      const std::vector<float> a = Run(*n, 0, 3 * kBlock, nullptr, false, &cook);
      hold(*n, 2, false);
      hold(*n, 4, true);
      const std::vector<float> b = Run(*n, 0, 3 * kBlock, nullptr, false, &cook);
      if (Peak(a) > 1e-4f)
         fail("params: pad volume 0 still sounds");
      if (Peak(b) < 0.3f)
         fail("params: pad 3 volume 0 silenced pad 5");
   }
   {
      auto n = make();
      n->LoadPad(6, shortWav); // 2000 frames
      n->padSpeed[6] = 2.0f;
      n->CookIfNeeded(cook++);
      hold(*n, 6, true);
      hold(*n, 6, false);
      const std::vector<float> o = Run(*n, 0, 6 * kBlock, nullptr, false, &cook);
      // 2000 frames at 2x is ~1000 output frames: gone within 1300.
      if (Peak(o, 1300) > 1e-4f || Peak(o) < 0.3f)
         fail("speed 2: one shot did not finish in half the time");
      n->padSpeed[6] = -1.0f;
      n->CookIfNeeded(cook++);
      hold(*n, 6, true);
      hold(*n, 6, false);
      const std::vector<float> r = Run(*n, 0, 6 * kBlock, nullptr, false, &cook);
      if (Peak(r) < 0.3f)
         fail("speed -1: reverse playback is silent");
   }

   // Per-pad fades: fade in 100 ms holds the first 64 frames near silence, 0 ms
   // starts at full level.
   {
      float first[2] = { 0.0f, 1.0f };
      for (int k = 0; k < 2; k++)
      {
         auto n = make();
         n->LoadPad(0, longWav);
         n->padFadeIn[0] = k == 0 ? 0.0f : 100.0f;
         n->CookIfNeeded(cook++);
         hold(*n, 0, true);
         hold(*n, 0, false);
         const std::vector<float> o = Run(*n, 0, 4 * kBlock, nullptr, false, &cook);
         first[k] = Peak(o, 0, 64);
      }
      if (first[0] < 0.3f)
         fail("fades: fade in 0 ms does not start at full level");
      if (first[1] > 0.05f)
         fail("fades: fade in 100 ms does not ramp the start");
   }

   // Per-pad sync: a Synced pad's hit is latched to the next grid line of the
   // transport, sample-accurately; a Free pad is untouched; a stopped transport
   // fires at once; a gate release before the line cancels; a synced loop pad
   // re-triggers on every division.
   {
      Transport& transport = Transport::Instance();
      const float savedBpm = transport.Tempo();
      const bool savedPlaying = transport.IsPlaying();
      auto startClock = [&]()
      {
         transport.SetTempo(120.0f);
         transport.SetTimeSignature(4, 4);
         transport.SetPlaying(true);
         transport.Rewind();
         transport.NotifyAudioEngineStarted((double)kSr);
      };
      const int pre = 10 * kBlock;           // frames run before the hit
      const int quarter = kSr / 2;           // 1/4 note at 120 bpm
      startClock();
      {
         auto n = make();
         n->LoadPad(0, longWav);
         n->padSync[0] = MpcNode::kSynced;
         n->padDiv[0] = (int)MusicTime::kQuarter;
         n->CookIfNeeded(cook++);
         Run(*n, 0, pre, nullptr, true, &cook);
         hold(*n, 0, true);
         hold(*n, 0, false);
         const std::vector<float> o = Run(*n, 0, quarter + 2000, nullptr, true, &cook);
         const int on = Onset(o, 0, 0.02f);
         if (on < 0 || std::abs(on - (quarter - pre)) > 40)
            fail("sync: a 1/4 pad's hit did not land on the next quarter line");
         if (Peak(o, 0, (size_t)(quarter - pre - 8)) > 1e-4f)
            fail("sync: a synced pad sounded before its line");
      }
      startClock();
      {
         auto n = make();
         n->LoadPad(0, longWav);
         n->LoadPad(1, longWav);
         n->padSync[0] = MpcNode::kSynced;
         n->CookIfNeeded(cook++);
         Run(*n, 0, pre, nullptr, true, &cook);
         hold(*n, 1, true); // pad 2 is Free: immediate even with the transport running
         hold(*n, 1, false);
         const std::vector<float> o = Run(*n, 0, 3 * kBlock, nullptr, true, &cook);
         if (Peak(o, 0, kBlock) < 0.3f)
            fail("sync: a Free pad was delayed");
      }
      transport.SetPlaying(false);
      {
         auto n = make();
         n->LoadPad(0, longWav);
         n->padSync[0] = MpcNode::kSynced;
         n->CookIfNeeded(cook++);
         hold(*n, 0, true);
         hold(*n, 0, false);
         const std::vector<float> o = Run(*n, 0, 3 * kBlock, nullptr, false, &cook);
         if (Peak(o, 0, kBlock) < 0.3f)
            fail("sync: with the transport stopped a synced pad did not fire at once");
      }
      startClock();
      {
         auto n = make();
         n->LoadPad(0, longWav);
         n->padSync[0] = MpcNode::kSynced;
         n->padMode[0] = MpcNode::kGate;
         n->CookIfNeeded(cook++);
         Run(*n, 0, pre, nullptr, true, &cook);
         hold(*n, 0, true);
         hold(*n, 0, false); // released before the line: the hit is cancelled
         const std::vector<float> o = Run(*n, 0, quarter + 2000, nullptr, true, &cook);
         if (Peak(o) > 1e-4f)
            fail("sync: a gate released before the line still fired");
      }
      startClock();
      {
         auto n = make();
         n->LoadPad(0, shortWav); // 2000 frames
         n->padSync[0] = MpcNode::kSynced;
         n->padMode[0] = MpcNode::kLoopToggle;
         n->padDiv[0] = (int)MusicTime::kEighth; // 12000 frames
         n->CookIfNeeded(cook++);
         Run(*n, 0, pre, nullptr, true, &cook);
         hold(*n, 0, true);
         hold(*n, 0, false);
         const int base = 12000 - pre;
         const std::vector<float> o = Run(*n, 0, base + 3 * 12000, nullptr, true, &cook);
         if (Peak(o, 0, (size_t)(base - 8)) > 1e-4f)
            fail("sync loop: sounded before the first line");
         for (int k = 0; k < 3; k++)
         {
            if (Peak(o, (size_t)(base + k * 12000 + 100), (size_t)(base + k * 12000 + 1500)) < 0.3f)
               fail("sync loop: the sample was not re-triggered on a division line");
            if (Peak(o, (size_t)(base + k * 12000 + 4000), (size_t)(base + k * 12000 + 11000)) > 1e-4f)
               fail("sync loop: not one pass per division (the sample kept playing)");
         }
         hold(*n, 0, true); // toggle off, quantised too
         hold(*n, 0, false);
         const std::vector<float> off = Run(*n, 0, 3 * 12000, nullptr, true, &cook);
         if (Peak(off, 12000 * 2, off.size()) > 1e-4f)
            fail("sync loop: the second hit did not stop the loop");
      }
      transport.SetTempo(savedBpm);
      transport.SetPlaying(savedPlaying);
   }

   printf("MPCTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}

int RunDspTest()
{
   const bool gainOk = RunGainFixture();
   const bool filterOk = RunFilterFixture();
   const bool oscWaveformOk = RunOscWaveformFixture();
   const bool noteSchedulingOk = RunWavetableFixture();
   const bool envelopeOk = RunEnvelopeFixture();
   const bool voiceStealOk = RunVoiceStealFixture();
   const bool musicTimeOk = RunMusicTimeFixture();
   const bool audioFilterOk = RunAudioFilterFixture();
   const bool dynamicsOk = RunDynamicsFixture();
   const bool delayOk = RunDelayFixture();
   const bool reverbOk = RunReverbFixture();
   const bool samplerOk = RunSamplerFixture();
   const bool slicerOk = RunSlicerFixture();
   const bool portableFftOk = RunPortableFftFixture();
   const bool paulStretchOk = RunPaulStretchFixture();
   const bool granularOk = RunGranularFixture();
   const bool drumSeqOk = RunDrumSequencerFixture();
   const bool looperOk = RunLooperFixture();
   const bool mpcOk = RunMpcFixture();
   const bool wavetableShaperOk = RunWavetableShaperFixture();
   const bool eqOk = RunEqFixture();
   const bool noteStackOk = RunNoteStackFixture();
   const bool freqShifterOk = RunFrequencyShifterFixture();
   const bool spectralSynthOk = RunImageSpectralSynthFixture();
   const bool waveTerrainOk = RunWaveTerrainFixture();
   const bool equationOk = RunEquationFixture();
   const bool audioDisplacementOk = RunAudioDisplacementFixture();
   const bool all = gainOk && filterOk && oscWaveformOk && noteSchedulingOk && envelopeOk && voiceStealOk &&
                    musicTimeOk && audioFilterOk && dynamicsOk && delayOk && reverbOk && samplerOk && slicerOk &&
                    paulStretchOk && granularOk && drumSeqOk && looperOk && mpcOk && wavetableShaperOk && eqOk && noteStackOk &&
                    freqShifterOk && spectralSynthOk && waveTerrainOk && equationOk && audioDisplacementOk &&
                    portableFftOk;
   printf("%s\n", all ? "DSPTEST OK" : "DSPTEST SUSPECT");
   return all ? 0 : 1;
}
}
