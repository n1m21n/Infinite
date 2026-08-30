#!/usr/bin/env bash
# Audio/video sync sweep: recording, export, playback rate. See SKILL.md.
SWEEP_NAME="A/V sync sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # --- headless, no GL / device / movie file: the arithmetic half ---
  # AudioCaptureRing stereo-pair integrity under a forced mid-call overflow
  # and an odd-sized Read(). The ring used to drop/return samples one at a
  # time, so an overflow (or an odd maxCount) could commit/return an odd
  # number of trailing floats from a call that always hands it whole L/R
  # pairs - permanently swapping every pair written after that point. That
  # doesn't shorten the file (the duration/sample-count checks below don't
  # catch it) and decodes as intermittent noise rather than silence - a much
  # better fit for a take reported as "sped up, then breaks" than a pure
  # sample-count shortfall.
  "AUDIORINGTEST:1"
  # OutputNode::PacedRepeat directly - decimate when rendering fast, pad when
  # rendering slow, pass through unpaced with no live audio, and cap a stall
  # instead of bursting 300 frames at once.
  "RECSYNCTEST:1"
  # Plugin delay compensation: parallel branches aligned to the slowest.
  # Latency here shows up as an echo/phase smear, not as drift.
  "AUDIOPDCTEST:1"

  # --- writes and re-decodes a real movie ---
  # Marker-based A/V drift measurement on an exported file.
  "RECEXPORTTEST:1"
  # Same, with the backpressure spin removed so the queue overruns and drops
  # frames on purpose - the drop path must not let video precede its audio.
  "RECEXPORTTEST=starved:1"
  # A resolution big enough for the byte-budgeted queue to overrun on its own,
  # unlike the 320x240 default.
  "RECEXPORTTEST=720p:1"
  # Reported frames vs frames that actually landed on disk. The regression the
  # async PBO + encoder-queue path is most likely to reintroduce.
  "RECTEST:42"
  # A 440Hz tone survives the record -> mux -> decode round trip and is present
  # in the movie's audio track at the right place.
  "VIDEOAUDIOTEST:70"
  # Recording with an audio file source: video/audio track presence and the
  # movie's duration against the expected duration; then the video-only case.
  "AUDIORECTEST:124"
  # Playback rate: 60 frames at speed -1 must move the playhead backwards by
  # the right amount, and 60 at speed +4 forwards.
  "VIDEOSPEEDTEST:186"

  # --- teardown while the recorder is live ---
  "RECTEARDOWNTEST:14"        # delete the Output node mid-take
  "RECTEARDOWNTEST=quit:14"   # quit mid-take: destructor path, StopRecording
                              # never called explicitly

  # --- offline render (non-realtime export) ---
  "OFFLINERENDERTEST:300"
  "OFFLINERENDERREFUSETEST:6"
  # A take long enough for the encoder queue's byte budget to matter: 1800
  # frames used to have 579 of them silently rejected by the queue, leaving a
  # 20s video track against a 30s audio track - the "audio is sped up" report.
  "OFFLINERENDERTEST,OFFLINERENDER_FPS=60,OFFLINERENDER_SECONDS=30:2200"
  # The same take against a deliberately tiny encoder-queue budget, so the
  # pump spends most of it waiting for room. That wait used to deadlock: the
  # writer holds video not-ready until the audio track leads it, and a pump
  # that has stopped rendering has also stopped producing audio. Stalled at
  # frame ~73 of 900 before the fix.
  "OFFLINERENDERTEST,OFFLINERENDER_FPS=60,OFFLINERENDER_SECONDS=15,OFFLINERENDER_QUEUEBYTES=8000000:2000"
  # Cancel on that same deep queue must abandon the file rather than drain it.
  "OFFLINERENDERTEST,OFFLINERENDER_FPS=60,OFFLINERENDER_SECONDS=30,OFFLINERENDER_CANCEL=300:600"
  # A shape closer to the real patch that first reported this bug: 1080p,
  # a Mixer between the synth and Output, a constrained encoder queue AND a
  # simulated heavy per-frame GPU cook (COOKDELAYMS) so video frames arrive
  # far slower than the encoder can drain them - the opposite backpressure
  # direction from the tiny-queue case above, and the one that changes
  # whether the encoder is ever really the bottleneck.
  "OFFLINERENDERTEST,OFFLINERENDER_FPS=60,OFFLINERENDER_SECONDS=30,OFFLINERENDER_RES=1920x1080,OFFLINERENDER_MIXER=1,OFFLINERENDER_QUEUEBYTES=8000000,OFFLINERENDER_COOKDELAYMS=15:8000"
)

OBSERVE=()

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
