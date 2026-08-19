# Fix: "Audio File" carries no signal into the audio graph

## What was verified (do not re-derive)

`AudioFileNode` is **not part of Infinite's DSP graph at all**. It is a plain
`INode` (`src/nodes/AnalyzeNodes.h:175` — `class AudioFileNode : public INode`,
with no `IAudioSource`, no `GetAudioNode()`, no `AudioNode` member). Its entire
sound path is a *private, second* audio engine living in the platform layer:

- `src/platform/Platform.mm:2413-2425` — `struct AudioPlayerHandle` owns its own
  `AVAudioEngine` + `AVAudioPlayerNode` + `Analyser`.
- `src/platform/Platform.mm:2554-2600` — `AudioFileOpen()` builds that engine,
  connects the player straight to `[engine mainMixerNode]` (i.e. straight to the
  hardware), installs a tap on bus 0 that feeds **only** the band `Analyser`,
  and calls `startAndReturnError:` immediately.
- `src/nodes/AnalyzeNodes.cpp:656-694` — every `AudioFileNode` method is a
  one-line forward to `Platform::AudioFile*`.

Consequences, all confirmed:

1. **It is audible with `AudioEngine` (the "start audio engine" button) off.**
   Its `AVAudioEngine` is started inside `AudioFileOpen` and is unrelated to
   `AudioEngine::Instance()`. This is a real, reproducible symptom, not a
   side effect.
2. **It produces zero samples for any audio pin.** `AudioNodeOfAny()`
   (`src/main.cpp:16655-16671`) resolves a node to an `AudioNode` via
   `IAudioSource` / `INoteSource` / three explicit sink casts / 
   `AudioNodeForNotePorts()`. `AudioFileNode` matches none, so
   `AudioBufferIndexOf()` returns `-1`, which `AudioTopologyEntry` defines as
   "read as silence". `CollectAudioChain()` also never adds an entry for it.
3. **But the cable is still allowed to be drawn.** `IsInputSlotCompatible()`
   at `src/main.cpp:2989-2990` says:
   ```cpp
   if (dstNode->node->AudioInputSlot(slot) != nullptr)
      return srcIsAudioNode || srcAudioFile != nullptr;
   ```
   The `srcAudioFile` clause exists for `OutputNode`'s recording slot (slot 1),
   where `StartRecording()` special-cases it and hands the **file path**
   straight to the muxer instead of capturing samples
   (`src/nodes/OutputNode.cpp:59-77`). That clause is not scoped to
   `OutputNode`, so it silently applies to **every** node with an
   `AudioInputSlot`: Audio Displacement (`src/nodes/AudioDisplacementNode.h:45`,
   slot 1), Mixer, Audio Texture, Audio Ribbon, Granular, PaulStretch, every
   `AudioEffectNode`, Sampler's "record in", Oscillator/Wavetable FM, and Audio
   Out itself. All of those accept the cable and then get silence.
   The reject-reason ladder at `src/main.cpp:32679-32683` carries the same
   `srcAudioFile == nullptr` carve-out, so the drag isn't even flagged.

So `Audio File -> Audio Displacement` connects, looks correct, sounds (through
the private engine) — and the displacement mesh never moves, because
`AudioDisplacementNode::CookIfNeeded` (`src/nodes/AudioDisplacementNode.cpp:137-170`)
reads *only* `mAudioSink->ReadSamples(...)`, which the topology never fills.
By contrast an Audio Out / Mixer / any `IAudioSource` upstream works, because
those do resolve through `AudioNodeOfAny`.

**Working alternative that already exists today:** `SamplerNode`
("Sample Player", `src/nodes/SamplerNode.h:34`) is `INode, IAudioSource`,
decodes a file via `Platform::DecodeAudioFileToBuffer` (`src/platform/Platform.h:274`)
into a `Platform::SampleBuffer`, and free-runs off the transport when no note
cable is connected. Patching Sample Player -> Audio Displacement works right now.

## The choice this fix has to make (decide before writing code)

Two clocks cannot both own file playback. Pick one:

- **(A) Move Audio File into the DSP graph (recommended).** Decode the file
  once with `Platform::DecodeAudioFileToBuffer`, hold the buffer in a new
  `AudioNode`, and retire the private `AVAudioEngine`. Audio File becomes a
  real `IAudioSource`; everything downstream (Audio Displacement, Mixer,
  effects, Audio Out) then works with no further changes, and the node stops
  being audible while the engine is off. Cost: `monitor`/`volume` change
  meaning (audibility now comes from patching to Audio Out), and the
  `Platform::AudioLevels` taps that `AudioAnalyzeNode::fileSource` and the node
  body read must be recomputed inside the new `AudioNode` instead of by the
  platform `Analyser`.
- **(B) Keep the private engine and tap it.** Add a lock-free sample ring to
  `AudioPlayerHandle`, filled by the `installTapOnBus:` block that already runs
  at `Platform.mm:2583-2588`, and drain it from a new `AudioNode`. Much smaller
  diff, but two independent device clocks feeding one graph — expect drift,
  underruns and clicks. **Not recommended.**

Do (A). If you conclude mid-way that (A) is too large for one session, stop and
implement step 1 below only (it is independently correct and shippable), and
say so rather than shipping (B).

---

## 1. Stop the silent lie (do this first, standalone-correct)

Scope the `srcAudioFile` allowance to the recording slot it was written for.

- `src/main.cpp:2968-2990` — `IsInputSlotCompatible()`. Change the audio-pin
  branch so an `AudioFileNode` source is accepted **only** when
  `dynamic_cast<OutputNode*>(dstNode->node.get()) != nullptr` (that is the only
  destination that consumes it, via the path-muxing branch in
  `OutputNode::StartRecording`). Everything else must require `srcIsAudioNode`.
  Note `AudioOutputNode` (`src/nodes/AudioNodes.h:232,248`) has an audio slot
  too and does *not* special-case a file path — it must reject as well.
- `src/main.cpp:32679-32683` — mirror it in the reject-reason ladder so the
  refused drag explains itself. Suggested text, in the house style of the
  neighbouring strings:
  `"Audio File plays outside the audio graph - use Sample Player to feed an audio pin"`.
- `src/main.cpp:3202` / `3259` / `32610` / `33965` compute `srcAudioFile` for
  the four call sites; they need no change, only the shared predicate does.
- Check `RecommendedNodeTypesForOutput()` (`src/main.cpp:3246-3292`) after the
  change: dragging out of Audio File should stop suggesting Mixer/Audio
  Displacement/etc. That is the intended effect, not a regression.
- **Migration:** existing patches may already hold one of these dead cables.
  Loading is fine (the cable just reads silence, as today) — do not add a
  loader that rewrites patches. Verify a patch containing
  `AudioFile -> AudioDisplacement` still loads without crashing.

## 2. Make Audio File a real audio source (option A)

- `src/nodes/AnalyzeNodes.h:175` — `class AudioFileNode : public INode, public IAudioSource`.
  Add `AudioNode* GetAudioNode() override;` and a
  `std::unique_ptr<AudioFilePlayerAudioNode> mAudioNode` (forward-declared in
  the header, defined in the .cpp — mirror how `SamplerNode.h:34-46` does
  exactly this, including the out-of-line constructor/destructor).
- New `AudioFilePlayerAudioNode : public AudioNode` in
  `src/nodes/AnalyzeNodes.cpp`. Mirror `SamplerNode`'s free-running "self lane":
  hold a `Platform::SampleBuffer`, advance a read cursor per block, honour
  `loop`, `volume`, `gain`, and gate on the transport when `followTransport`.
  Handoff of the decoded buffer to the audio thread must go through the same
  mailbox pattern `SamplerNode` uses — **do not** allocate, free, or touch a
  `std::string`/`std::vector` resize on the audio thread.
- `AudioFileNode::Open()` (`src/nodes/AnalyzeNodes.cpp:656`) — call
  `Platform::DecodeAudioFileToBuffer` and publish to the audio node. Remove the
  `Platform::AudioFileOpen` engine path (and its `Play/Pause/Restart/IsPlaying/
  Duration/Position` forwards at `:689-694`), reimplementing each against the
  new `AudioNode`'s own cursor so the node body UI keeps working unchanged.
- `Levels()` — `AudioAnalyzeNode::fileSource` (wired at `src/main.cpp:2949`) and
  the node body both read `Platform::AudioLevels`. Compute the same struct
  (rms/peak/low/mid/high/16 bands/onset) inside the new `AudioNode` and drain it
  in `CookIfNeeded`, so `AudioAnalyzeNode` and `DrawAudioFileParams`
  (`src/main.cpp:13397`) need no signature change.
- `src/nodes/OutputNode.cpp:59-77` — the recording special-case still wants
  `file->FilePath()`/`file->loop` for direct muxing. Keep it; it does not
  depend on the playback mechanism. But now that Audio File also emits samples,
  confirm the `else` branch (live capture ring) is not additionally triggered
  for the same source, or recorded audio will double.
- `RequiresAudioProcessing()` — override it to return true while the file is
  playing, so the node still runs when it has no path to an Audio Out
  (`src/main.cpp:16803-16811` is the seed loop that uses it). Without this,
  `AudioFile -> AudioDisplacement` with no Audio Out anywhere in the patch is
  silent again, which is exactly the reported case.
- Once Audio File is an `IAudioSource`, **revert step 1's carve-out to a plain
  `return srcIsAudioNode;`** — `srcAudioFile` then satisfies it naturally, and
  the whole `AudioFileNode* srcAudioFile` parameter threading through
  `IsInputSlotCompatible` can be deleted (4 call sites listed above). Do this
  cleanup; leaving both mechanisms in place is how this bug happened.
- `src/main.cpp:34950-34963` — the harness prints Audio File under its own
  "audio source" branch placed *before* the `IAudioSource` branch. After the
  change the second branch covers it; collapse them.

## 3. Verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Then:

- `.claude/skills/audio-node-sweep/` — run it. `AudioFileNode` gaining a
  `VisitParams`-visible audio path and a new `AudioNode` is exactly what the
  param round-trip and teardown sweeps cover.
- `.claude/skills/geometry-transform-sweep/driver.sh` — Audio Displacement is an
  `IGeometrySource`-consuming node; the existing fixtures at
  `src/main.cpp:22126` (`RunAudioDisplacementFixture`), `:28707`, `:28934` and
  `:29049` must all still pass.
- Manual, in the app: build a patch `Audio File -> Audio Displacement -> Render 3D`
  with **no** Audio Out anywhere. Confirm (a) the mesh moves, (b) nothing is
  audible until the audio engine is started, (c) starting the engine makes it
  audible only if it is also patched to an Audio Out.

## Out of scope

- Do not touch `SamplerNode` — it already works and is the current
  recommended path.
- Do not change `AudioAnalyzeNode`'s `fileSource` pointer mechanism into an
  `AudioCable`; that is a separate migration.
- Do not add a patch-upgrade/rewrite step for old patches holding dead cables.
