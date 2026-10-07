// Recording / AV / PDC / MIDI self-tests (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
// ===================================================== INFINITE_RECSYNCTEST
// Guards the exported-movie A/V sync property. The video track's PTS is a
// plain frame counter over recordFps on both platforms, while live audio is
// stamped from the real captured sample count - so emitting one video frame
// per *rendered* frame makes the file's video duration renderedFrames/fps
// against an audio duration of real elapsed time, and the two walk apart
// linearly across the take. That is invisible while monitoring and shows up
// only in the written file, which is exactly how it shipped.
//
// OutputNode::PacedRepeat is the arithmetic that keeps them locked. This
// simulates whole takes at render rates above, below and equal to the target
// and asserts the invariant that actually matters: at every point in the
// take, the video timeline (emitted/fps) tracks the audio timeline
// (audioFrames/rate) to within a frame.
void RunRecSyncTest()
{
   printf("[REC SYNC TEST] pacing exported video against the audio clock\n");

   const double kRate = 48000.0;
   const int kFps = 30;
   int failures = 0;

   struct Case { const char* name; double renderFps; double seconds; };
   const Case cases[] = {
      { "render slower than target (20fps -> 30fps)",  20.0, 30.0 },
      { "render faster than target (60fps -> 30fps)",  60.0, 30.0 },
      { "render exactly at target  (30fps -> 30fps)",  30.0, 30.0 },
      { "render wildly slow        ( 7fps -> 30fps)",   7.0, 30.0 },
      // vsync off on a light patch: most frames are decimated away.
      { "render uncapped          (240fps -> 30fps)", 240.0, 30.0 },
   };

   for (const Case& c : cases)
   {
      long long emitted = 0;
      long long audioFrames = 0;
      double worstDriftFrames = 0.0;

      const long long renderCalls = (long long)(c.renderFps * c.seconds);
      const long long audioPerCall = (long long)(kRate / c.renderFps);

      for (long long i = 0; i < renderCalls; i++)
      {
         // One render frame: the audio thread produced its block, then the
         // capture path decides what the frame is worth.
         audioFrames += audioPerCall;
         emitted += OutputNode::PacedRepeat(audioFrames, kRate, kFps, emitted, false);

         const double videoSeconds = (double)emitted / (double)kFps;
         const double audioSeconds = (double)audioFrames / kRate;
         const double drift = std::fabs(videoSeconds - audioSeconds) * (double)kFps;
         if (drift > worstDriftFrames)
            worstDriftFrames = drift;
      }

      // The final drain pads the tail out to the audio's full length.
      emitted += OutputNode::PacedRepeat(audioFrames, kRate, kFps, emitted, true);

      const double videoSeconds = (double)emitted / (double)kFps;
      const double audioSeconds = (double)audioFrames / kRate;
      const double endDriftFrames = std::fabs(videoSeconds - audioSeconds) * (double)kFps;

      // Two frames of slack: one for the rounding in PacedRepeat, one for a
      // render call landing between audio blocks. Anything beyond that is
      // the drift this test exists to catch.
      const bool ok = worstDriftFrames <= 2.0 && endDriftFrames <= 1.0;
      if (!ok)
         failures++;
      printf("  [%s] %s: %lld frames for %.2fs audio (video %.3fs) worst drift %.2f frames, end drift %.2f frames\n",
             ok ? "pass" : "FAIL", c.name, emitted, audioSeconds, videoSeconds,
             worstDriftFrames, endDriftFrames);
   }

   // A transient collapse in render rate - the interesting case, since it is
   // what a heavy patch actually does. Render holds 30fps, freezes dead for
   // two seconds mid-take while audio keeps flowing, then recovers. Sync must
   // survive it, and the recovery must not be silently deferred to the end.
   {
      long long emitted = 0;
      long long audioFrames = 0;
      double worstDriftFrames = 0.0;
      double driftAfterRecoveryFrames = 0.0;
      const long long stallStart = 300;   // 10s in at 30fps
      const long long recoverBy = 300 + 15; // half a second of render calls

      for (long long i = 0; i < 900; i++)
      {
         // The stall costs render calls, never audio: the device keeps
         // delivering blocks the whole time it is frozen.
         audioFrames += (i == stallStart) ? (long long)(kRate * 2.0) + 1600 : 1600;
         emitted += OutputNode::PacedRepeat(audioFrames, kRate, kFps, emitted, false);

         const double drift =
            std::fabs((double)emitted / (double)kFps - (double)audioFrames / kRate) * (double)kFps;
         // The stall itself is a legitimate gap - measure recovery from after
         // it, which is the property at issue.
         if (i > stallStart && drift > worstDriftFrames)
            worstDriftFrames = drift;
         if (i == recoverBy)
            driftAfterRecoveryFrames = drift;
      }
      emitted += OutputNode::PacedRepeat(audioFrames, kRate, kFps, emitted, true);
      const double endDrift =
         std::fabs((double)emitted / (double)kFps - (double)audioFrames / kRate) * (double)kFps;

      const bool ok = driftAfterRecoveryFrames <= 1.0 && endDrift <= 1.0;
      if (!ok)
         failures++;
      printf("  [%s] 2s render freeze mid-take: caught up within %lld frames (drift %.2f), end drift %.2f frames\n",
             ok ? "pass" : "FAIL", recoverBy - stallStart, driftAfterRecoveryFrames, endDrift);
   }

   // A take with no live audio at all (engine stopped, or an audio *file*
   // source, where the platform recorders slave audio to the video clock
   // instead) must keep every rendered frame rather than stalling on an
   // audio stream that never arrives.
   const bool passthrough = OutputNode::PacedRepeat(0, kRate, kFps, 0, false) == 1 &&
                            OutputNode::PacedRepeat(1000, 0.0, kFps, 0, false) == 1;
   if (!passthrough)
      failures++;
   printf("  [%s] no live audio passes frames through unpaced\n", passthrough ? "pass" : "FAIL");

   // A stall must not be handed to the encoder as one enormous burst, and
   // must not be silently dropped either - the catch-up spreads across the
   // frames that follow.
   const int burst = OutputNode::PacedRepeat((long long)(kRate * 10.0), kRate, kFps, 0, false);
   const bool capped = burst > 0 && burst <= kFps;
   if (!capped)
      failures++;
   printf("  [%s] a 10s stall caps at %d frames per call instead of bursting 300\n",
          capped ? "pass" : "FAIL", burst);

   printf("REC SYNC %s\n", failures == 0 ? "OK" : "FAIL");
}


// ===================================================== INFINITE_AUDIORINGTEST
// AudioCaptureRing carries interleaved stereo (L,R,L,R,..) from the audio
// thread to whatever drains it (a live WAV capture, or Offline Render's
// DrainOfflineAudioCapture). Write() used to drop samples one at a time on
// overflow, which could commit an odd number of trailing samples from a call
// that always hands it an even (frame-aligned) count - splitting a stereo
// pair right down the middle. Nothing downstream ever re-anchors that phase,
// so every pair after the drop is silently channel-swapped from then on: not
// a shorter file (the sample-count/duration assertions elsewhere don't catch
// it), but every future pair decoded as noise instead of the real signal
// until another odd-offset event happens to restore parity by chance. A
// reported take that plays sped-up and then alternates silence and "weird
// noise" for the rest of its length is a much better fit for this than for a
// pure sample-count shortfall. Read() had the same class of bug from the
// consumer side (a maxCount that happens to land on an odd boundary).
//
// This drives the ring directly - no audio device, no GL, no movie file -
// and checks the one property that matters: every sample that comes back out
// of Read() belongs to a whole, correctly-paired L/R frame from what went in,
// even when a Write() call is forced to overflow mid-call.
void RunAudioRingTest()
{
   printf("[AUDIO RING TEST] stereo-pair integrity under overflow\n");
   int failures = 0;

   AudioCaptureRing ring;
   const size_t usable = AudioCaptureRing::kCapacity - 1; // one slot always kept empty

   // Fill the ring to within 3 samples of full - an odd number of free
   // slots, which is exactly the condition that let the old code commit a
   // lone unpaired sample. Filler content is irrelevant, so use pairs of 0.
   {
      std::vector<float> filler(usable - 3, 0.0f);
      ring.Write(filler.data(), (int)filler.size());
   }

   // Two marker pairs, in one call, against only 3 free slots: only the
   // first pair (111,222) fits; the second (333,444) cannot, since it would
   // need to consume the ring's last free slot AND the head. A correct
   // implementation drops the second pair whole; the old implementation
   // wrote 111,222,333 and dropped only 444, leaving 333 unpaired.
   const float markers[4] = { 111.0f, 222.0f, 333.0f, 444.0f };
   ring.Write(markers, 4);

   std::vector<float> out(usable + 8, -1.0f);
   const int n = ring.Read(out.data(), (int)out.size());

   const bool evenCount = (n % 2) == 0;
   if (!evenCount)
      failures++;
   printf("  [%s] Read() returned an even sample count (%d)\n", evenCount ? "pass" : "FAIL", n);

   // Every pair the filler wrote was (0,0), so the first sample that isn't
   // zero is where the markers begin. It must be exactly {111,222} (the
   // second marker pair correctly dropped whole), never {111,222,333,...}
   // with 333 stranded as the start of a corrupted pair, and never a lone
   // 333 or 444 appearing without its correct partner.
   int markerStart = -1;
   for (int i = 0; i + 1 < n; i += 2)
   {
      if (out[i] != 0.0f || out[i + 1] != 0.0f)
      {
         markerStart = i;
         break;
      }
   }
   const bool foundMarkers = markerStart >= 0;
   const bool pairIntact = foundMarkers && out[markerStart] == 111.0f && out[markerStart + 1] == 222.0f;
   // The second pair must be either fully present (444 right after 222, if
   // capacity allowed it) or fully absent (nothing after 222 at all) -
   // never 333 appearing alone or paired with something that isn't 444.
   const bool secondPairWholeOrAbsent =
      (markerStart + 3 >= n) ||
      (out[markerStart + 2] == 333.0f && out[markerStart + 3] == 444.0f) ||
      (out[markerStart + 2] == 0.0f); // ring had room for nothing more after 222
   if (!(foundMarkers && pairIntact && secondPairWholeOrAbsent))
      failures++;
   printf("  [%s] overflow dropped whole pairs only (found=%d intact=%d secondOk=%d, "
          "markerStart=%d n=%d)\n",
          (foundMarkers && pairIntact && secondPairWholeOrAbsent) ? "pass" : "FAIL",
          (int)foundMarkers, (int)pairIntact, (int)secondPairWholeOrAbsent, markerStart, n);

   // Repeat with an odd Read() maxCount, which used to let the consumer side
   // strand a lone sample of a pair in `out` while leaving its partner
   // behind in the ring for the next call - the same corruption from the
   // other direction.
   {
      AudioCaptureRing ring2;
      const float pairs[6] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
      ring2.Write(pairs, 6);
      float small[3] = { -1, -1, -1 };
      const int n1 = ring2.Read(small, 3); // odd maxCount
      const bool n1Even = (n1 % 2) == 0;
      float rest[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
      const int n2 = ring2.Read(rest, 8);
      const bool allPairsAccountedFor = (n1 + n2) == 6;
      // Whatever came back in the first read must be whole pairs from the
      // start of the stream (1,2[,3,4]), not a lone 1 or a 1,2,3 split.
      const bool firstReadOk = n1 >= 0 && n1 <= 2 &&
                               (n1 == 0 || (small[0] == 1.0f && small[1] == 2.0f));
      if (!(n1Even && allPairsAccountedFor && firstReadOk))
         failures++;
      printf("  [%s] odd Read() maxCount never splits a pair (n1=%d n2=%d total=%d)\n",
             (n1Even && allPairsAccountedFor && firstReadOk) ? "pass" : "FAIL", n1, n2, n1 + n2);
   }

   printf("AUDIO RING %s\n", failures == 0 ? "OK" : "FAIL");
}

// ===================================================== INFINITE_VIDEOEXACTTEST
// An offline render must show EVERY source frame exactly - no skipping ahead
// to catch up, no stale repeat - even though realtime playback is allowed to
// drop frames to keep up with the clock. This records a clip whose frame i is
// a flat grey unique to i, then steps it the way an export does
// (Platform::VideoFrameAtExact, which VideoSourceNode uses in offline mode)
// at 30, 60 and 24 fps, plus a few backward and forward seeks, and checks
// every request shows exactly the source frame that covers its time.
void RunVideoExactTest()
{
   constexpr int kW = 320;
   constexpr int kH = 240;
   constexpr int kFps = 30;
   constexpr int kFrames = 60;
   auto levelOf = [](int i) { return 16 + i * 3; }; // 16..193, 3 apart survives H.264
   int failures = 0;

   const std::string path = TmpPath("infinite_videoexacttest.mp4");
   std::remove(path.c_str());
   std::string error;
   Platform::RecorderHandle* rec = Platform::RecorderStart(path, kW, kH, kFps, error);
   if (rec == nullptr)
   {
      printf("  [FAIL] could not start recorder: %s\nVIDEOEXACTTEST FAIL - BUG\n", error.c_str());
      return;
   }
   for (int i = 0; i < kFrames; i++)
   {
      for (int spin = 0; spin < 20000 && Platform::RecorderPendingFrameCount(rec) >= 3; spin++)
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      std::vector<unsigned char> px = Platform::RecorderAcquireFrameBuffer(rec);
      px.assign((size_t)kW * kH * 4, (unsigned char)levelOf(i));
      for (size_t k = 3; k < px.size(); k += 4)
         px[k] = 255;
      if (!Platform::RecorderAppend(rec, std::move(px), 1))
         failures++;
   }
   int wrote = 0;
   if (!Platform::RecorderStop(rec, error, &wrote, nullptr) || wrote != kFrames || failures > 0)
   {
      printf("  [FAIL] recorder wrote %d of %d frames (%s)\nVIDEOEXACTTEST FAIL - BUG\n",
             wrote, kFrames, error.c_str());
      return;
   }

   // Which source frame a delivered picture is, from its centre grey.
   auto frameIndexOf = [&](const std::vector<unsigned char>& px) -> int
   {
      if ((int)px.size() < kW * kH * 4)
         return -1;
      double sum = 0.0;
      int count = 0;
      for (int y = kH / 4; y < kH * 3 / 4; y += 4)
      {
         for (int x = kW / 4; x < kW * 3 / 4; x += 4)
         {
            sum += px[((size_t)y * kW + x) * 4 + 1];
            count++;
         }
      }
      return (int)std::lround((sum / std::max(1, count) - 16.0) / 3.0);
   };

   Transport::Instance().SetOfflineMode(true, 48000.0);
   const double exportRates[] = { 30.0, 60.0, 24.0 };
   for (double rate : exportRates)
   {
      Platform::VideoHandle* vid = Platform::VideoOpen(path, error);
      if (vid == nullptr)
      {
         printf("  [FAIL] could not open the clip: %s\n", error.c_str());
         failures++;
         break;
      }
      std::vector<unsigned char> px;
      int steps = 0, wrong = 0, firstWrongStep = -1, firstWrongGot = -1, firstWrongWant = -1;
      for (int k = 0; (double)k / rate < (double)kFrames / kFps - 1e-6; k++)
      {
         const double t = (double)k / rate;
         Platform::VideoFrameAtExact(vid, t, px);
         const int want = (int)std::floor(t * kFps + 1e-6);
         const int got = frameIndexOf(px);
         steps++;
         if (got != want)
         {
            if (wrong++ == 0)
            {
               firstWrongStep = k;
               firstWrongGot = got;
               firstWrongWant = want;
            }
         }
      }
      printf("  export @ %.0f fps: %d steps, %d wrong frames", rate, steps, wrong);
      if (wrong > 0)
         printf(" (first at step %d: got frame %d, wanted %d)", firstWrongStep, firstWrongGot, firstWrongWant);
      printf("\n");
      failures += wrong;

      // Seeks, as a scrub or an arrangement jump would do mid-export.
      const int seekTo[] = { 40, 10, 11, 50, 5, 59, 0 };
      int seekWrong = 0;
      for (int f : seekTo)
      {
         Platform::VideoFrameAtExact(vid, (f + 0.5) / kFps, px);
         const int got = frameIndexOf(px);
         if (got != f)
         {
            printf("  seek to frame %d showed frame %d\n", f, got);
            seekWrong++;
         }
      }
      if (seekWrong > 0)
         printf("  [FAIL] %d of %d seeks landed on the wrong frame\n", seekWrong, (int)(sizeof(seekTo) / sizeof(seekTo[0])));
      failures += seekWrong;
      Platform::VideoClose(vid);
   }
   // INFINITE_VIDEOEXACTTEST=<movie>: also print an FNV-1a hash of every
   // frame of that movie as delivered (RGBA, bottom-up), so a decoder change
   // can be diffed pixel-for-pixel against a reference decode of the same file.
   const char* hashClip = getenv("INFINITE_VIDEOEXACTTEST");
   if (hashClip != nullptr && std::strcmp(hashClip, "1") != 0)
   {
      Platform::VideoHandle* vid = Platform::VideoOpen(hashClip, error);
      std::vector<unsigned char> px;
      const int n = vid ? (int)std::lround(Platform::VideoDuration(vid) * 30.0) : 0;
      for (int i = 0; i < n; i++)
      {
         Platform::VideoFrameAtExact(vid, (i + 0.5) / 30.0, px);
         uint64_t h = 1469598103934665603ull;
         for (unsigned char c : px)
         {
            h ^= c;
            h *= 1099511628211ull;
         }
         printf("  hash %d %016llx\n", i, (unsigned long long)h);
      }
      Platform::VideoClose(vid);
   }
   Transport::Instance().SetOfflineMode(false);
   std::remove(path.c_str());
   printf("%s\n", failures == 0 ? "VIDEOEXACTTEST OK" : "VIDEOEXACTTEST FAIL - BUG");
}

// ===================================================== INFINITE_RECEXPORTTEST
// End-to-end A/V sync measurement on a real written movie, as opposed to
// RECSYNCTEST above, which only exercises the pacing arithmetic in isolation.
//
// Marks the take with five simultaneous audio/video events - a tone burst and
// a white flash decided by the same boolean, so they are aligned at the source
// by construction - then writes the file through the real platform recorder,
// reopens it with the app's own decoders, and measures how far apart the two
// landed. Anything the muxer does to the relationship between the tracks shows
// up here: PTS assignment, repeatCount expansion, AAC encoder priming delay,
// container edit lists.
//
// Deliberately renders at a rate that does NOT match the target, since a
// render loop that happens to hold the target rate hides the entire bug class
// this exists for.
//
// Two separate verdicts, because they fail for different reasons and one is
// far more serious than the other:
//   - DRIFT: does the offset grow across the take? This is the bug that
//     shipped. Tight tolerance.
//   - OFFSET: is there a constant lead/lag on every marker? A fixed offset is
//     an encoder-priming or capture-tap-point question, not drift. Looser
//     tolerance, because video onsets can only be resolved to a frame.
//
// Headless: feeds the recorder pixel buffers directly rather than going
// through the GL readback, so it runs on the Windows CI runner - the only
// place the Media Foundation half of this can be executed by machine at all.
// width/height: recording resolution - 320x240 (the historical default), at
// only ~54MB of lifetime video bytes for the whole take, cannot cross the
// 256MB queue budget even with zero draining, so it can never exercise the
// drop path; 1280x720 can. starved: when true, shrinks the queue's byte
// budget for this take (a real hardware encoder drains far faster than this
// test can produce, so forcing it to actually reject frames needs a smaller
// ceiling, not a slower consumer) so RecorderAppend/RecorderPump's admission
// rejection engages for real, under the same safe backpressure every variant
// uses - exercising the case the non-starved variant deliberately excludes.
// label: short tag folded into this run's printf lines, so a log is
// self-identifying when more than one variant's output ends up read side by
// side.
void RunRecExportTest(int width, int height, bool starved, const char* label)
{
   printf("[REC EXPORT TEST %s] measuring A/V sync on a real exported movie\n", label);

   const int kW = width;
   const int kH = height;
   constexpr int kFps = 30;         // target/muxed frame rate
   constexpr double kRenderFps = 20.0; // deliberately mismatched
   constexpr double kRate = 48000.0;
   constexpr double kTakeSeconds = 6.0;
   constexpr double kMarkerSeconds = 0.1;
   constexpr double kToneHz = 1000.0;
   const double kMarkerAt[] = { 1.0, 2.0, 3.0, 4.0, 5.0 };
   const int kMarkerCount = (int)(sizeof(kMarkerAt) / sizeof(kMarkerAt[0]));

   const std::string path = TmpPath("infinite_recexporttest.mp4");
   std::remove(path.c_str());

   auto inMarker = [&](double t)
   {
      for (int m = 0; m < kMarkerCount; m++)
      {
         if (t >= kMarkerAt[m] && t < kMarkerAt[m] + kMarkerSeconds)
            return true;
      }
      return false;
   };

   std::string error;
   Platform::RecorderHandle* rec =
      Platform::RecorderStart(path, kW, kH, kFps, error, std::string(), true, kRate, 2);
   if (rec == nullptr)
   {
      printf("  [FAIL] could not start recorder: %s\n", error.c_str());
      printf("REC EXPORT FAIL\n");
      return;
   }

   if (starved)
   {
      // A real hardware encoder drains far faster than this test can ever
      // produce, so forcing a genuine over-budget rejection needs a smaller
      // ceiling, not a slower consumer - a handful of 720p frames' worth is
      // enough to make the byte-budget check in RecorderAppend/RecorderPump
      // engage for real under the same safe backpressure spin every other
      // variant uses, without letting the whole take collapse.
      Platform::RecorderSetTestQueueByteBudget(rec, 10ull * 1024 * 1024);
   }

   const int blockFrames = (int)(kRate / kRenderFps);
   const long long renderSteps = (long long)(kRenderFps * kTakeSeconds);
   std::vector<float> block((size_t)blockFrames * 2);
   long long audioFrames = 0;
   long long emitted = 0;
   long long audioRejected = 0;
   long long videoRejected = 0;
   double tonePhase = 0.0;

   // The per-step cap below is generous by design (see its own comment), but
   // that's fine only as long as the encoder is making progress overall. A
   // wedged encoder - observed on GitHub's shared macOS runners, which don't
   // reliably have hardware VideoToolbox access - never drops pendingCount,
   // so every remaining render step would burn its own fresh 120s cap and the
   // test would hang for hours instead of failing. Track wall-clock spent
   // waiting across the *whole* loop and bail with a verdict well before that.
   // A larger frame takes the software encoder longer per frame, so the
   // budget scales with it rather than staying fixed and flaking on the
   // slower resolution - two tiers, not a literal per-pixel scale, since this
   // is a safety bail-out and not itself something worth tuning finely.
   const auto waitBudgetStart = std::chrono::steady_clock::now();
   const auto kMaxTotalWait = std::chrono::seconds(kW * kH > 320 * 240 ? 90 : 45);
   bool encoderWedged = false;

   for (long long step = 0; step < renderSteps; step++)
   {
      const double t = (double)step / kRenderFps;
      const bool marker = inMarker(t);

      // One audio block for this render step. The tone runs continuously in
      // phase so the burst has no click at its edges for the encoder to smear.
      for (int i = 0; i < blockFrames; i++)
      {
         const float v = marker ? (float)(0.8 * std::sin(tonePhase)) : 0.0f;
         tonePhase += 2.0 * M_PI * kToneHz / kRate;
         if (tonePhase > 2.0 * M_PI)
            tonePhase -= 2.0 * M_PI;
         block[(size_t)i * 2 + 0] = v;
         block[(size_t)i * 2 + 1] = v;
      }
      if (Platform::RecorderAppendAudio(rec, block.data(), blockFrames))
         audioFrames += blockFrames;
      else
         audioRejected++;

      // ...and however many video frames the pacing says that block is worth.
      const int repeat = OutputNode::PacedRepeat(audioFrames, kRate, kFps, emitted, false);
      if (repeat > 0)
      {
         // The real render loop is paced by the display and never outruns the
         // encoder; this one runs flat out, so without backpressure it would
         // just overflow the queue and measure a movie made of dropped
         // frames. Wait for a slot rather than sleeping, so the test stays as
         // fast as the encoder is. This spin stays active even in `starved`
         // mode - real hardware encoders drain so much faster than this test
         // can produce that removing it entirely doesn't make production
         // outrun consumption, it starves the encoder's dispatch queue of a
         // chance to run at all (this test thread never yields otherwise),
         // which rejects nearly everything and wipes out marker windows
         // outright rather than exercising the bounded, recoverable drops the
         // byte budget is meant to produce. `starved` instead shrinks the
         // budget itself (see RecorderSetTestQueueByteBudget below) so real,
         // moderate admission rejections happen under this same safe pacing.
         //
         // The ceiling is deliberately generous. A refused append is not lost
         // footage - the pacer re-issues the count on the next frame - and
         // since item 2, the pixels it re-issues pad with the *previous*
         // frame rather than stamping the next step's content early, so a
         // drop here no longer moves a marker's onset backwards in time. That
         // reclassification is exactly what the `starved` variant checks via
         // the LEAD assertion below, instead of avoiding the case as the
         // pre-item-2 comment here used to.
         for (int spin = 0; spin < 120000 && Platform::RecorderPendingFrameCount(rec) >= 3; spin++)
         {
            Platform::RecorderFlushPendingAudio(rec);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (std::chrono::steady_clock::now() - waitBudgetStart > kMaxTotalWait)
            {
               encoderWedged = true;
               break;
            }
         }
         if (encoderWedged)
            break;

         std::vector<unsigned char> px = Platform::RecorderAcquireFrameBuffer(rec);
         px.assign((size_t)kW * kH * 4, marker ? (unsigned char)255 : (unsigned char)0);
         for (size_t i = 3; i < px.size(); i += 4)
            px[i] = 255; // opaque
         if (Platform::RecorderAppend(rec, std::move(px), repeat))
            emitted += repeat;
         else
            videoRejected += repeat;
      }
   }

   if (encoderWedged)
   {
      printf("  [FAIL] encoder appears wedged: pending frame count never dropped below 3 for "
             "%lldms (RecorderPump never got re-armed by AVFoundation)\n",
             (long long)std::chrono::duration_cast<std::chrono::milliseconds>(kMaxTotalWait).count());
      printf("REC EXPORT FAIL\n");
      Platform::RecorderStop(rec, error, nullptr, nullptr);
      std::remove(path.c_str());
      return;
   }

   int wrote = 0;
   int dropped = 0;
   const bool stopped = Platform::RecorderStop(rec, error, &wrote, &dropped);
   if (!stopped)
   {
      printf("  [FAIL] recorder stop failed: %s\n", error.c_str());
      printf("REC EXPORT FAIL\n");
      std::remove(path.c_str());
      return;
   }
   printf("  wrote %d frames (%d dropped) for %.2fs of audio, rendering at %.0ffps into a %dfps movie\n",
          wrote, dropped, (double)audioFrames / kRate, kRenderFps, kFps);
   printf("  paced: emitted=%lld audioAccepted=%lld audioRejected=%lld videoRejected=%lld\n",
          emitted, audioFrames, audioRejected, videoRejected);

   int failures = 0;

   // A frame the recorder accepted has to reach the file. This is separate
   // from the sync verdicts below because it fails differently: the encoder
   // used to abandon padded frames without counting them, so the movie came
   // out short while both the caller and droppedCount said everything was
   // fine. Checking the invariant directly names that as an encoder fault
   // instead of leaving it to show up as unexplained drift.
   // Every frame the recorder accepted has to reach the file. A refused
   // append is a different thing and not a failure: it never enters
   // `emitted`, and the pacer simply asks for it again on the next frame,
   // so the movie still comes out whole. What used to happen instead is
   // that accepted frames were abandoned inside the encoder without being
   // counted anywhere, so the movie came out short while both the caller
   // and droppedCount said everything was fine.
   if ((long long)wrote != emitted)
   {
      printf("  [FAIL] ACCEPTED: recorder took %lld frames but only %d reached the file "
             "(%d reported dropped, %lld appends refused)\n",
             emitted, wrote, dropped, videoRejected);
      failures++;
   }

   // ---- where the tone bursts actually landed ----
   Platform::SampleBuffer audio;
   std::string audioErr;
   std::vector<double> audioOnsets;
   if (!Platform::DecodeVideoAudioTrackToBuffer(path, audio, audioErr) || audio.numFrames <= 0)
   {
      printf("  [FAIL] could not decode the movie's audio track: %s\n", audioErr.c_str());
      failures++;
   }
   else
   {
      // Envelope over a 5ms window, then a rising edge through half amplitude.
      const int win = (int)(audio.sampleRate * 0.005);
      bool loud = false;
      for (int i = 0; i + win < audio.numFrames; i += win)
      {
         float peak = 0.0f;
         for (int k = 0; k < win; k++)
            peak = std::max(peak, std::fabs(audio.channelData[(size_t)(i + k)]));
         const bool nowLoud = peak > 0.4f;
         if (nowLoud && !loud)
            audioOnsets.push_back((double)i / audio.sampleRate);
         loud = nowLoud;
      }
      printf("  audio track: %d frames @ %.0fHz, %d tone onsets\n",
             audio.numFrames, audio.sampleRate, (int)audioOnsets.size());
   }

   // ---- where the flashes actually landed ----
   std::vector<double> videoOnsets;
   std::string videoErr;
   Platform::VideoHandle* vid = Platform::VideoOpen(path, videoErr);
   if (vid == nullptr)
   {
      printf("  [FAIL] could not open the movie for decoding: %s\n", videoErr.c_str());
      failures++;
   }
   else
   {
      const int vw = Platform::VideoWidth(vid);
      const int vh = Platform::VideoHeight(vid);
      std::vector<unsigned char> px;
      bool bright = false;
      const double stepSeconds = 1.0 / ((double)kFps * 4.0);
      for (double t = 0.0; t < kTakeSeconds; t += stepSeconds)
      {
         // This stepper runs as fast as the CPU allows, which on Windows is far
         // faster than the decode thread - so it has to wait for the decoder
         // rather than sample one early frame 720 times and report no flashes.
         // VideoDecodeIsCatchingUp is what distinguishes "not decoded yet" from
         // "there is nothing more"; it is constant false on macOS, where
         // VideoFrameAt already decoded synchronously, so this loop never
         // executes there and macOS timing is unchanged.
         bool gotFrame = Platform::VideoFrameAt(vid, t, px);
         for (int waitedMs = 0;
              !gotFrame && waitedMs < 2000 && Platform::VideoDecodeIsCatchingUp(vid);
              waitedMs++)
         {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            gotFrame = Platform::VideoFrameAt(vid, t, px);
         }
         if (!gotFrame && px.empty())
            continue;
         if ((int)px.size() < vw * vh * 4)
            continue;
         // Average the centre quarter, away from any edge artefacts.
         double sum = 0.0;
         int count = 0;
         for (int y = vh / 4; y < vh * 3 / 4; y += 4)
         {
            for (int x = vw / 4; x < vw * 3 / 4; x += 4)
            {
               sum += px[((size_t)y * vw + x) * 4];
               count++;
            }
         }
         const double lum = count > 0 ? sum / count / 255.0 : 0.0;
         const bool nowBright = lum > 0.5;
         if (nowBright && !bright)
            videoOnsets.push_back(t);
         bright = nowBright;
      }
      printf("  video track: %dx%d, %.2fs, %d flash onsets\n",
             vw, vh, Platform::VideoDuration(vid), (int)videoOnsets.size());
      Platform::VideoClose(vid);
   }

   // ---- correlate ----
   if ((int)audioOnsets.size() != kMarkerCount || (int)videoOnsets.size() != kMarkerCount)
   {
      printf("  [FAIL] expected %d markers on each track, found %d audio / %d video"
             " - the markers did not survive the round trip\n",
             kMarkerCount, (int)audioOnsets.size(), (int)videoOnsets.size());
      failures++;
   }
   else
   {
      double first = 0.0;
      double last = 0.0;
      double worstAbs = 0.0;
      double worstLeadMs = 0.0; // most-negative video-minus-audio seen, in ms
      for (int m = 0; m < kMarkerCount; m++)
      {
         const double delta = videoOnsets[m] - audioOnsets[m];
         if (m == 0)
            first = delta;
         if (m == kMarkerCount - 1)
            last = delta;
         worstAbs = std::max(worstAbs, std::fabs(delta));
         worstLeadMs = std::min(worstLeadMs, delta * 1000.0);
         printf("    marker %d: audio %.3fs  video %.3fs  video-minus-audio %+.0fms\n",
                m + 1, audioOnsets[m], videoOnsets[m], delta * 1000.0);
      }

      // The bug that shipped: the offset grows across the take. One video
      // frame of slack, since a flash onset can only be resolved to the frame
      // it starts on.
      const double driftMs = std::fabs(last - first) * 1000.0;
      const double kDriftToleranceMs = 1000.0 / kFps + 1.0;
      const bool driftOk = driftMs <= kDriftToleranceMs;
      if (!driftOk)
         failures++;
      printf("  [%s] DRIFT: offset moved %.0fms from first marker to last (tolerance %.0fms)\n",
             driftOk ? "pass" : "FAIL", driftMs, kDriftToleranceMs);

      // A constant lead/lag is a different question - encoder priming, or the
      // point in the chain the audio is tapped. Not drift, but still audible
      // past roughly a couple of frames, so it is worth a verdict of its own.
      const double kOffsetToleranceMs = 2.0 * 1000.0 / kFps + 1.0;
      const bool offsetOk = worstAbs * 1000.0 <= kOffsetToleranceMs;
      if (!offsetOk)
         failures++;
      printf("  [%s] OFFSET: worst constant offset %.0fms (tolerance %.0fms)\n",
             offsetOk ? "pass" : "FAIL", worstAbs * 1000.0, kOffsetToleranceMs);

      // The bug this catches: a dropped frame used to get padded by stamping
      // the *new* frame into the repeat slots that cover the past, pulling
      // video's onset earlier than the audio it's supposed to follow. One
      // video frame of quantization slack, same reasoning as DRIFT/OFFSET
      // above - this is one-sided because lagging is already covered by
      // OFFSET/DRIFT above and is far less objectionable than leading.
      const double kLeadToleranceMs = 1000.0 / kFps + 1.0;
      const double leadMagnitudeMs = std::max(0.0, -worstLeadMs);
      const bool leadOk = leadMagnitudeMs <= kLeadToleranceMs;
      if (!leadOk)
         failures++;
      printf("  [%s] LEAD: video must not precede its audio onset - worst lead %.0fms (tolerance %.0fms)\n",
             leadOk ? "pass" : "FAIL", leadMagnitudeMs, kLeadToleranceMs);
   }

   std::remove(path.c_str());
   printf("REC EXPORT %s\n", failures == 0 ? "OK" : "FAIL");
}

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

bool RunAudioPdcTest()
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

   // R477: one source wired to two terminals (Audio Out + Output) reaches the
   // device once. Same topology as zeroLatency, run with the dedupe the build
   // applies, then without it to prove the check can see a double count.
   AudioEngine::Instance().SetTopology(zeroLatency);
   AudioEngine::Instance().ProcessOffline(buffer);
   const Peak doubled = FindPeak(chan0.data(), kNumFrames);
   MarkDuplicateDeviceTerminals(zeroLatency.terminalBufferIndices);
   const bool markOk = zeroLatency.terminalBufferIndices[0].mixToDevice && !zeroLatency.terminalBufferIndices[1].mixToDevice;
   AudioEngine::Instance().SetTopology(zeroLatency);
   AudioEngine::Instance().ProcessOffline(buffer);
   const Peak deduped = FindPeak(chan0.data(), kNumFrames);
   const bool dedupeOk = markOk && doubled.value > dryAlone.value * 1.9f &&
                         std::abs(deduped.value - dryAlone.value) <= dryAlone.value * 0.001f;
   printf("AUDIOPDCTEST same source on two terminals mixes once: alone %.4f  doubled %.4f  deduped %.4f  %s\n",
          dryAlone.value, doubled.value, deduped.value, dedupeOk ? "OK" : "FAIL");

   const bool ok = hasLatency && dryOffsetOk && wetOffsetOk && alignedOk && amplitudeOk && zeroAllocOk && dedupeOk;
   printf("%s\n", ok ? "AUDIOPDCTEST OK" : "AUDIOPDCTEST FAIL");
   return ok;
}

// ======================================================= INFINITE_MIDIPARSETEST
// Linux only: feeds synthetic snd_seq_event_t values straight into
// MidiLinux.cpp's pure event->table/ring translator (no real ALSA sequencer
// handle, no /dev/snd/seq) - see docs/plans/linux/phase-02-audio-midi.md
// 2.5.2. This is the primary MIDI proof on CI, since real end-to-end ALSA
// devices are unreachable on GitHub Actions/OrbStack (validation.md's P0
// spike). Gated as an early exit before glfwInit(), same as INFINITE_DSPTEST.
#if defined(__linux__)
int RunMidiParseTest()
{
   PlatformLinuxTestHooks::MidiParseTestResetState();

   const unsigned int devA = PlatformLinuxTestHooks::MidiParseTestDeviceId("Test Port A");
   const unsigned int devB = PlatformLinuxTestHooks::MidiParseTestDeviceId("Test Port B");

   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      if (!cond)
      {
         printf("MIDIPARSETEST FAIL: %s\n", what);
         ok = false;
      }
   };

   // A throwaway note first, so the ring's write position is not literally
   // zero when we capture noteStreamStart below. MidiReadNotesSince treats
   // cursor==0 as "fresh consumer, fast-forward to the current write
   // position, don't replay history" (matching MidiWin.cpp's
   // ReadNotesSince) - a real 0 is indistinguishable from that sentinel, so
   // capturing position 0 right after a from-scratch reset would always
   // read back zero regardless of whether the ring itself worked. Using a
   // channel/note pair (15/126) not touched anywhere else in this test
   // keeps it from perturbing the checks below.
   {
      snd_seq_event_t warm{};
      warm.type = SND_SEQ_EVENT_NOTEON;
      warm.data.note.channel = 15;
      warm.data.note.note = 126;
      warm.data.note.velocity = 1;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, warm);
      warm.type = SND_SEQ_EVENT_NOTEOFF;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, warm);
   }
   const unsigned long long noteStreamStart = Platform::MidiNoteStreamPosition();

   // Note On, device A, channel 0, note 60, velocity 100.
   {
      snd_seq_event_t ev{};
      ev.type = SND_SEQ_EVENT_NOTEON;
      ev.data.note.channel = 0;
      ev.data.note.note = 60;
      ev.data.note.velocity = 100;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, ev);
   }
   float v = 0.0f;
   check(Platform::MidiRead(devA, 0, 60, true, v) && std::fabs(v - 100.0f / 127.0f) < 0.001f,
        "note on value");

   Platform::MidiLastNote last{};
   check(Platform::MidiChannelLastNote(devA, 0, last) && last.note == 60 && last.hitSeq == 1,
        "channel last note");
   check(Platform::MidiNoteHitCount(devA, 0, 60) == 1, "note hit count");

   // Note Off, device A - same note, must zero the held-note table.
   {
      snd_seq_event_t ev{};
      ev.type = SND_SEQ_EVENT_NOTEOFF;
      ev.data.note.channel = 0;
      ev.data.note.note = 60;
      ev.data.note.velocity = 0;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, ev);
   }
   check(Platform::MidiRead(devA, 0, 60, true, v) && v == 0.0f, "note off zeroes value");

   // Velocity-0 Note On == Note Off (MIDI convention), on a second,
   // interleaved source port (device B) - proves per-device scoping and the
   // velocity-0 special case at once.
   {
      snd_seq_event_t on{};
      on.type = SND_SEQ_EVENT_NOTEON;
      on.data.note.channel = 1;
      on.data.note.note = 40;
      on.data.note.velocity = 80;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devB, on);

      snd_seq_event_t off{};
      off.type = SND_SEQ_EVENT_NOTEON;
      off.data.note.channel = 1;
      off.data.note.note = 40;
      off.data.note.velocity = 0; // velocity-0 note-on == note-off
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devB, off);
   }
   check(Platform::MidiRead(devB, 1, 40, true, v) && v == 0.0f,
        "velocity-0 note-on treated as note-off");
   // Device A's channel-0 note-60 state must be untouched by device B traffic.
   check(Platform::MidiRead(devA, 0, 60, true, v) && v == 0.0f, "device scoping unaffected");

   // Control Change, device A.
   {
      snd_seq_event_t ev{};
      ev.type = SND_SEQ_EVENT_CONTROLLER;
      ev.data.control.channel = 2;
      ev.data.control.param = 74;
      ev.data.control.value = 64;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, ev);
   }
   check(Platform::MidiRead(devA, 2, 74, false, v) && std::fabs(v - 64.0f / 127.0f) < 0.001f,
        "CC value");

   Platform::MidiCCValue touched{};
   check(Platform::MidiPollLastTouched(touched) && touched.device == devA && touched.controller == 74 &&
            !touched.isNote,
        "last touched CC");

   // Pitch bend: not part of the CC/note table contract on any platform
   // (Platform.h has no accessor for it) - must be accepted without
   // corrupting other table state, not necessarily produce a readable value.
   {
      snd_seq_event_t ev{};
      ev.type = SND_SEQ_EVENT_PITCHBEND;
      ev.data.control.channel = 0;
      ev.data.control.value = 0;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, ev);
   }
   check(Platform::MidiRead(devA, 2, 74, false, v) && std::fabs(v - 64.0f / 127.0f) < 0.001f,
        "CC table unaffected by pitch bend");

   // MIDI clock: Start, then a steady run of pulses at ~120 BPM (24 ppqn ->
   // ~20.8ms/pulse), must yield MidiClockIsPresent()==true and a plausible
   // BPM; Stop must reset it. distinct event types per
   // phase-02-audio-midi.md 2.4, not a raw status byte needing the
   // >=0xF0-before-masking care MidiWin.cpp SS3.2 required.
   {
      snd_seq_event_t start{};
      start.type = SND_SEQ_EVENT_START;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, start);

      for (int i = 0; i < 30; i++)
      {
         snd_seq_event_t clockEv{};
         clockEv.type = SND_SEQ_EVENT_CLOCK;
         PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, clockEv);
         std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
   }
   check(Platform::MidiClockIsPresent(), "clock present after pulses");
   const float bpm = Platform::MidiClockBpm();
   check(bpm > 80.0f && bpm < 160.0f, "clock bpm in plausible range");

   {
      snd_seq_event_t stopEv{};
      stopEv.type = SND_SEQ_EVENT_STOP;
      PlatformLinuxTestHooks::MidiParseTestFeedEvent(devA, stopEv);
   }
   check(Platform::MidiClockBpm() == 0.0f, "clock bpm reset after stop");

   // Live note stream: on, off, velocity-0-as-off above should all have
   // landed on the ring, readable from the position captured before they
   // were published.
   unsigned long long cursor = noteStreamStart;
   Platform::MidiNoteMessage msgs[16];
   const int n = Platform::MidiReadNotesSince(cursor, msgs, 16);
   check(n >= 3, "note stream ring captured on/off/off events");

   printf("%s\n", ok ? "MIDIPARSETEST OK" : "MIDIPARSETEST FAIL");
   return ok ? 0 : 1;
}
#endif

// ======================================================= INFINITE_CVRECTEST
// CV Recorder: record a ramp against the beat clock, replay it, check speed,
// loop/hold, and the save/load round trip.
int RunCVRecorderTest()
{
   bool ok = true;
   auto check = [&](bool c, const char* what) {
      if (!c) { printf("CVRECTEST FAIL: %s\n", what); ok = false; }
   };
   Transport& tp = Transport::Instance();
   ConstantNode src;
   CVRecorderNode rec;
   rec.input = &src;

   tp.SeekBeats(0.0);
   rec.StartRecording();
   for (int i = 0; i <= 64; i++) // 4 beats, 16 samples/beat, input ramps 0..1
   {
      tp.SeekBeats(i / 16.0);
      src.value = i / 64.0f;
      rec.Value01();
      rec.Value01(); // idempotent within a tick
   }
   rec.StopRecording();
   check(rec.SampleCount() == 65, "sample count");
   check(rec.playing, "playback starts after stop");

   auto at = [&](double beats) { tp.SeekBeats(beats); return rec.Value01(); };
   at(10.0); // playback starts from the top of the take here
   check(std::fabs(at(10.0 + 2.0) - 0.5f) < 0.03f, "midpoint at speed 1");
   rec.speed = 2.0f;
   check(std::fabs(at(10.0 + 2.5) - 0.75f) < 0.03f, "speed change continues from the playhead, no jump");
   check(rec.Value01() == rec.Value01(), "idempotent playback");
   rec.speed = 1.0f;
   check(std::fabs(at(13.5) - 1.0f) < 0.03f, "reaches the end of the take");
   check(std::fabs(at(14.0) - 7.0f / 64.0f) < 0.03f, "loop wraps");
   rec.low = 0.5f;
   rec.high = 1.0f;
   check(std::fabs(at(15.0) - (0.5f + 0.5f * 23.0f / 64.0f)) < 0.03f, "low/high maps the range");
   rec.low = 0.0f;
   rec.high = 1.0f;

   {
      // round trip through the ParamVisitor text path
      CVRecorderNode copy;
      struct Grab : ParamVisitor
      {
         std::string got;
         void Float(const char*, float&) override {}
         void Int(const char*, int&) override {}
         void Bool(const char*, bool&) override {}
         void Text(const char* n, std::string& v) override { if (std::string(n) == "data") got = v; }
         void Color(const char*, float*) override {}
      } grab;
      rec.VisitParams(grab);
      struct Put : ParamVisitor
      {
         std::string put;
         void Float(const char*, float&) override {}
         void Int(const char*, int&) override {}
         void Bool(const char*, bool&) override {}
         void Text(const char* n, std::string& v) override { if (std::string(n) == "data") v = put; }
         void Color(const char*, float*) override {}
      } putv;
      putv.put = grab.got;
      copy.VisitParams(putv);
      check(copy.SampleCount() == rec.SampleCount() && copy.Samples() == rec.Samples(), "save/load round trip");
   }
   rec.Clear();
   check(rec.SampleCount() == 0 && !rec.playing, "clear");

   printf("%s\n", ok ? "CVRECTEST OK" : "CVRECTEST FAIL");
   return ok ? 0 : 1;
}

// ======================================================= INFINITE_MIDICC14TEST
// Pure header test (platform/common/MidiCC14.h): a Pioneer-style 14-bit
// knob (coarse CC n, fine CC n+32) must publish one smooth value under the
// coarse controller and never as a separate 0..1 sweep on the fine one.
int RunMidiCC14Test()
{
   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      if (!cond)
      {
         printf("MIDICC14TEST FAIL: %s\n", what);
         ok = false;
      }
   };
   auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

   MidiCC14::Tracker t;
   // Coarse then fine within a millisecond, CC 1 / CC 33.
   MidiCC14::Event e = t.OnCC(1, 0, 1, 64, 1000);
   check(e.controller == 1 && near(e.value01, 64.0f / 127.0f), "lone coarse byte publishes 7-bit value");
   e = t.OnCC(1, 0, 33, 100, 1001);
   check(e.controller == 1, "fine byte folds into the coarse controller");
   check(near(e.value01, (float)((64 << 7) | 100) / 16383.0f), "coarse+fine combine to 14 bits");
   check(t.Resolve(1, 0, 33) == 1, "fine controller resolves to coarse once paired");

   // Slow knob: every coarse step comes with fine bytes sweeping 0..127. The
   // published stream must be monotonic - the bug was a sawtooth.
   int64_t now = 2000;
   t.OnCC(1, 0, 1, 62, now);
   e = t.OnCC(1, 0, 33, 0, now + 1);
   float prev = e.value01;
   bool monotonic = true;
   for (int v14 = 8000; v14 < 8600; ++v14)
   {
      const int msb = v14 >> 7, lsb = v14 & 0x7F;
      e = t.OnCC(1, 0, 1, msb, now);
      float out = e.value01;
      if (out < prev - 1e-6f) monotonic = false;
      prev = out;
      e = t.OnCC(1, 0, 33, lsb, now + 1);
      if (e.value01 < prev - 1e-6f) monotonic = false;
      prev = e.value01;
      now += 5;
   }
   check(monotonic, "slow 14-bit sweep publishes a monotonic stream");

   // A plain 7-bit fine-range CC with no coarse partner stays untouched.
   MidiCC14::Tracker u;
   e = u.OnCC(1, 0, 40, 90, 5000);
   check(e.controller == 40 && near(e.value01, 90.0f / 127.0f), "unpaired CC 40 stays a plain CC");
   check(u.Resolve(1, 0, 40) == 40, "unpaired CC 40 does not remap");

   // A stale coarse byte (outside the pairing window) does not claim the CC.
   u.OnCC(1, 0, 8, 10, 6000);
   e = u.OnCC(1, 0, 40, 5, 6500);
   check(e.controller == 40, "fine byte long after coarse is not paired");

   // Channel / device isolation.
   e = t.OnCC(2, 0, 33, 7, 9000);
   check(e.controller == 33, "pairing is per device");
   e = t.OnCC(1, 1, 33, 7, 9000);
   check(e.controller == 33, "pairing is per channel");

   printf("%s\n", ok ? "MIDICC14TEST OK" : "MIDICC14TEST FAIL");
   return ok ? 0 : 1;
}
}
