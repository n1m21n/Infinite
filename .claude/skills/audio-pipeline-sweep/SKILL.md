---
name: audio-pipeline-sweep
description: Sweep Infinite's audio pipeline for bugs on both macOS and Windows - DSP correctness, whether every audio parameter actually reaches the audio thread, note fan-out to several consumers, plugin delay compensation, device loss and recovery, deleting an audio node mid-playback without a crash or a dangling cable, PCM conversion, and the transport clock running off the audio sample counter. Use after adding or changing any audio or note node, when sound is silent, distorted, clicking, stuck or one block late, when a knob does nothing audible, when deleting a node while it plays crashes or leaves a cable behind, when a plugin chain sounds phasey, or before a release as an audio regression gate.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/audio-pipeline-sweep/driver.sh
```

`--skip-build` to reuse the existing binary. Exit 0 means every fixture printed
a passing verdict.

The first nine fixtures are **headless** - no GL, no window, no audio device -
so they are the half of this sweep that is meaningful on a CI box and identical
on both OSes. Run just those while iterating on DSP:

```bash
INFINITE_DSPTEST=1 build/Infinite.app/Contents/MacOS/Infinite
```

## The four layers, and which fixture owns each

```
node params (UI thread)                    AUDIOPARAMSWEEPTEST
   |  written from the UI, read on the audio thread
   v
DSP block render (audio thread)            DSPTEST, RESONATORTEST, CYCLESHAPERTEST,
   |  per-node process(), never allocating   SPECBLURTEST, MOLDERTEST, GRAINMOLDERTEST
   v
graph topology + cables                    AUDIOGRAPHTEST, AUDIOLIFECYCLETEST,
   |  AudioCable / NoteCable, PDC            AUDIOTEARDOWNSWEEPTEST, NOTEFANOUTTEST,
   v                                         AUDIOPDCTEST
device + clock                             AUDIORECOVERYTEST, AUDIOPCMTEST,
                                             TRANSPORTCLOCKTEST
```

## The bug classes these actually catch

**A parameter that never reaches the audio thread.** The most common audio bug
here is not bad DSP - it is a knob wired to a member the render loop never
reads, so it moves on screen and changes nothing. `AUDIOPARAMSWEEPTEST` walks
the node registry, drives each audio node's parameters, and asserts the change
is observable in the rendered block within one block. Rows can legitimately
read `[pass] ... is UI-only (no DSP effect by design)` or `[SKIP] ... no
synthetic input available` for hardware/external-driven note sources.

**A note reaching only the first consumer.** One note source feeding three
synths must give all three the note-on. `NOTEFANOUTTEST` asserts the
three-consumer case, the fan-out + FM case from the original bug report, and -
the hard part - deleting one consumer mid-playback while the survivors keep
voicing.

**Deleting a node while the audio thread is inside it.** `AUDIOTEARDOWNSWEEPTEST`
spawns, wires, plays and deletes *each* audio node type mid-playback, and
asserts the app kept rendering and the downstream cable was cleared rather than
left dangling. It also prints the engine's `xruns=` count. `AUDIOGRAPHTEST`
does the narrower mid-chain case: delete the Gain out of Osc -> Gain -> Out and
confirm the Audio Out's cable was cleared generically (through
`AudioInputSlot`/`DisconnectAllTo`, not a hand-written special case) and that
the next cook does not touch freed memory.

**Latency mismatch between parallel branches.** A plugin with reported latency
on one branch and none on the other makes the sum phasey.
`AUDIOPDCTEST` asserts the branches get aligned, that the already-slowest
branch is left untouched, and that a zero-latency patch allocates no
compensation delay at all.

**A clock that is not the audio clock.** `TRANSPORTCLOCKTEST` asserts
`Transport::Seconds()` advances monotonically off the audio engine's sample
counter over 18 frames (`ADVANCING SMOOTHLY OK`), then freezes exactly while
paused (`FROZEN OK`). On a machine with no audio device it verifies the
fallback clock instead, and says which it measured in its first line - read
that line before trusting the result as an *audio*-clock check.

## Windows parity

- Every headless fixture is platform-neutral C++ and must give the same verdict
  on both OSes. A divergence there is a real cross-platform DSP bug.
- `AUDIOPCMTEST` is the deliberate exception: it calls
  `Platform::AudioPcmConversionSelfTest()`, which has two separate
  implementations (`src/platform/Platform.mm` for CoreAudio,
  `src/platform/win/AudioDeviceWin.cpp` for WASAPI). It is the check that the
  two conversions agree in behaviour, so it must be run on both - a pass on
  macOS says nothing about the Windows path.
- `AUDIORECOVERYTEST` exercises device loss and re-open, which is where the two
  backends differ most. Run it on Windows explicitly rather than assuming.
- Audio node code under `src/nodes/` must contain no `_WIN32`; the device
  difference lives behind `Platform::`. See `new-audio-node`.

## Reading a failure

- `[FAIL]` on a headless fixture - real DSP or logic regression, and the log
  names the specific assertion with its measured number ("expected 0.500 got
  ...").
- `[CRASH]` - the process died before printing any verdict. In this sweep that
  usually means a teardown fixture hit the freed-node path it exists to catch.
- `[NO VERDICT]` - the frame budget in `driver.sh` is below the frame the
  verdict prints on (the windowed fixtures assert at `frameId == 4`, except
  `TRANSPORTCLOCKTEST` which asserts at 20 and 30). Raise the budget.
- `xruns=` in the teardown log is printed, not asserted. A rising xrun count on
  a machine that is otherwise idle is worth chasing; on a loaded laptop it is
  noise. Do not turn it into a graded threshold without deciding what the
  threshold means.

## What this sweep does not cover

- **No audible-quality assertion.** Nothing here says a filter sounds right,
  only that it attenuates in the measured band.
- **No sample-rate matrix.** Everything runs at whatever the device or fixture
  default is; 44.1k vs 48k vs 96k behaviour is unasserted.
- **VST3 hosting is only partly covered.** `AUDIOPDCTEST` covers latency
  reporting; plugin scanning has its own fixture (`INFINITE_PLUGINSCANTEST`)
  and is not run here because it depends on what is installed on the machine.
- **A/V sync against a written movie is a different sweep** - see
  `av-sync-sweep`.
