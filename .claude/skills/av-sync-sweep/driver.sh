#!/usr/bin/env bash
# Audio/video sync sweep: recording, export, playback rate. See SKILL.md.
SWEEP_NAME="A/V sync sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # --- headless, no GL / device / movie file: the arithmetic half ---
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
)

OBSERVE=()

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
