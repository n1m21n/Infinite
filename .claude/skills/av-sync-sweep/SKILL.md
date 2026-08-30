---
name: av-sync-sweep
description: Sweep Infinite's audio/video sync for bugs on both macOS and Windows - exported movies where audio and video drift apart, frames reported but never written to disk, a tone missing from the recorded audio track, wrong movie duration, playback speed and reverse scrubbing, plugin latency smearing parallel branches, and crashes when the recorder is torn down mid-take. Use after touching OutputNode recording, the PBO readback, the platform recorder or muxer, the video decoder, playback speed, or plugin delay compensation; when an exported movie's sound lags or leads the picture, when a long take drifts progressively, when the file is shorter or longer than the take, or before a release that ships export.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/av-sync-sweep/driver.sh
```

`--skip-build` to reuse the existing binary. Exit 0 means every fixture passed.
Several of these write and re-decode a real movie under the temp dir, so the
run takes noticeably longer than the static sweeps.

## Why drift happens at all

A live-audio take has **two independent clocks**, and this is the whole subject:

- The video track's PTS is a plain frame counter over `recordFps`
  (`frameIndex/fps` on macOS, `FrameNumberToHns(frameCount, fps)` on Windows).
- Live audio is stamped from the **real sample count actually captured**.

Emitting one video frame per *rendered* frame therefore gives the movie a video
duration of `renderedFrames/recordFps` against an audio duration of real
elapsed time. Unless the render loop happens to hold exactly `recordFps`, the
two drift **linearly apart across the take** - invisible while monitoring,
because playback is real-time either way, and visible only in the written file.
That is exactly how it shipped once.

`OutputNode::PacedRepeat(audioFrames, rate, fps, emitted, finalDrain)` is the
arithmetic that locks them: it returns how many video frames to write for the
frame just captured so that `emitted/fps` tracks `audioFrames/rate` - 0 when
rendering faster than `recordFps` (decimate), >1 when rendering slower (pad).
Pacing against the same sample count the muxer stamps the audio with also
avoids trusting a wall clock, which drifts from the audio device clock on its
own over a long take.

Two things follow that are easy to get wrong when editing this code:

- **Only live audio needs pacing.** With an audio *file* source the platform
  recorders slave the audio to the video's synthetic clock instead
  (`AppendAudioUpTo` / `WriteFileAudioTrack`), so that path is in sync by
  construction and must keep **every** rendered frame.
- **`recordFps` is latched for the whole take** (`mRecordFps`), because the
  recorder fixes its video PTS denominator at `RecorderStart` and never
  re-reads it. Pacing against a live-draggable slider would stretch the video
  against real audio.

## What each fixture owns

| fixture | frames | asserts |
| --- | --- | --- |
| `RECSYNCTEST` | headless | `PacedRepeat` over whole simulated takes at 7 / 20 / 30 / 60 / 240 fps render rates, plus a mid-take two-second render freeze. Invariant: video timeline tracks audio timeline to within 2 frames at every point, 1 frame at the end. Also asserts a stall caps its catch-up instead of bursting 300 frames at once, and that a no-live-audio take passes frames through unpaced |
| `AUDIOPDCTEST` | headless | parallel branches aligned to the slowest; the already-slowest branch untouched; a zero-latency patch allocating no delay |
| `RECEXPORTTEST` | headless-ish | writes a real movie with markers on both tracks and measures A/V drift by re-decoding it |
| `RECTEST` | 42 | frames *reported* by `StopRecording` vs frames that actually landed on disk (`RECFRAMES OK` / `SUSPECT - frame count mismatch`), walking the real encoded stream rather than trusting duration. Also checks Transport freezes while paused mid-take |
| `VIDEOAUDIOTEST` | 70 | a 440Hz tone survives record -> mux -> decode and is present in the movie's audio track, measured against a 5000Hz control band |
| `AUDIORECTEST` | 124 | recording *with an audio file source*: both tracks present, movie duration matches expectation; then the video-only take |
| `VIDEOSPEEDTEST` | 186 | playback rate: 60 frames at speed -1 move the playhead backwards correctly, 60 at +4 forwards |
| `RECTEARDOWNTEST` | 14 | deleting the Output node mid-recording survives, with the queue drained |
| `RECTEARDOWNTEST=quit` | 14 | quitting mid-recording: the destructor path, where `StopRecording` is never called explicitly |

`RECSYNCTEST` is the one to run while iterating - it is pure arithmetic with no
GL context, device or file, which is what makes it checkable on Windows CI too:

```bash
INFINITE_RECSYNCTEST=1 build/Infinite.app/Contents/MacOS/Infinite
```

## Windows parity

This is the sweep where the two platforms diverge most, so a macOS pass says
the least:

- The recorders are entirely separate implementations. macOS stamps video PTS
  as `frameIndex/fps`; Windows as `FrameNumberToHns(frameCount, fps)`. Both are
  fed by the same `PacedRepeat`, which is why that function is asserted
  separately from the file-writing fixtures.
- The file-writing fixtures (`RECTEST`, `RECEXPORTTEST`, `VIDEOAUDIOTEST`,
  `AUDIORECTEST`) exercise the platform muxer, so **they must be run on
  Windows too**. A frame-count mismatch or a missing audio track on one OS only
  is a real, shippable bug and this sweep is how it gets caught before a user
  finds it.
- The teardown pair is where the async readback fences and encoder queue differ
  in lifetime. Run both variants on both OSes.
- Audio capture feeding the recorder comes through CoreAudio vs WASAPI; the
  sample counts that `PacedRepeat` is handed originate there.

## Reading a failure

- `FAIL` rows inside `RECSYNCTEST` name the render rate and print worst/end
  drift in frames. Growing end drift means the pacing arithmetic broke; a large
  *worst* drift with a small end drift means catch-up is being deferred to the
  drain instead of spread across the take.
- `SUSPECT - frame count mismatch` in `RECTEST` means the reported count and
  the file disagree: a PBO readback that was never drained, or the counter
  drifting from what the encoder accepted. Read the `dropped=` number in the
  same line first.
- `TONE MISSING` / `VIDEOAUDIOTEST FAIL - BUG` means the audio track is absent
  or silent in the written file - a muxer problem, not a pacing one.
- `[CRASH]` on a teardown variant is the fixture doing its job: the recorder
  was freed while the readback fences or encoder queue were still live.

## What this sweep does not cover

- **No perceptual sync check.** Everything is measured in frames and sample
  counts against markers or a tone; nothing verifies a human would call the
  result lip-synced.
- **No long-take soak.** The fixtures record seconds, not minutes. A drift that
  needs ten minutes to become visible would pass here - though `RECSYNCTEST`
  simulates 30-second takes arithmetically, which partly covers this.
- **No variable-fps source footage.** Playback is checked at speed -1 and +4
  against a file the fixture itself wrote.
- **Monitoring latency is unmeasured.** How far the audio you hear lags the
  frame you see *while patching* is a different question from what lands in the
  file, and nothing here answers it.
