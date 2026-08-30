#!/usr/bin/env bash
# Audio pipeline sweep. See SKILL.md.
SWEEP_NAME="audio pipeline sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # --- headless DSP (no GL, no window, no device): runs identically on CI ---
  "DSPTEST:1"              # oscillator amplitude/frequency, gain smoothing, filter
                           # passband + sweep attenuation, meter rings
  "AUDIOPCMTEST:1"         # the platform's PCM conversion, per-OS implementation
  "AUDIOPDCTEST:1"         # plugin delay compensation: branches aligned, the
                           # slowest branch left alone, zero-latency allocates nothing
  "RESONATORTEST:1"
  "CYCLESHAPERTEST:1"
  "SPECBLURTEST:1"
  "MOLDERTEST:1"
  "GRAINMOLDERTEST:1"

  # --- registry-wide: does every audio param actually reach the audio thread ---
  "AUDIOPARAMSWEEPTEST:1"

  # --- graph lifecycle, needs a window ---
  "AUDIOGRAPHTEST:8"       # patch round trip of an audio chain + mid-chain delete
  "AUDIOLIFECYCLETEST:8"   # spawn/wire/cook/delete every audio node type
  "AUDIORECOVERYTEST:8"    # device loss and re-open
  "AUDIOTEARDOWNSWEEPTEST:10" # delete each audio node type mid-playback, cables cleared
  "NOTEFANOUTTEST:8"       # one note source into several consumers; delete a consumer
                           # mid-playback; modulated filter cutoff torn down
  "TRANSPORTCLOCKTEST:32"  # Transport::Seconds() advances off the audio sample
                           # counter and freezes when paused
)

OBSERVE=()

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
