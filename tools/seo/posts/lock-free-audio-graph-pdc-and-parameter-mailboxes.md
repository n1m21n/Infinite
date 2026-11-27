---
title: Lock-free audio graph scheduling, PDC, and parameter mailboxes
seo_title: Lock-Free Audio Graph, PDC, and Mailboxes | Infinite
description: How Infinite compiles audio graphs on the main thread, schedules parallel DAG execution, aligns branches with idempotent PDC, and smooths parameters per sample.
date: 2026-11-27
category: product
tags: [audio dsp, lock-free, signal graph, pdc, c++]
keywords: lock-free audio graph, plugin delay compensation dsp, audio parameter smoothing mailbox, parallel audio dag, c++ real-time audio engine
image: assets/blog_lock-free-audio-graph-pdc-and-parameter-mailboxes.png
image_alt: Architecture diagram of Infinite's lock-free audio engine showing main-thread topology compilation, atomic ProcessList swap, parallel RunClaims DAG workers, idempotent CompensationDelay PDC, and ParamMailbox per-sample ramping
status: published
---

Real-time audio engines fail in predictable ways when graph edits, multi-core scheduling, plugin latency, and UI automation collide on the audio callback thread. Taking a mutex inside the callback risks priority inversion and buffer underruns; reallocating delay lines on every cable edit produces audible clicks; reading raw UI knob values per block introduces staircase zipper noise.

In [Infinite](https://infinitedevices.org/), `AudioEngine` avoids all three failure modes by separating graph compilation from graph execution across a strict wait-free boundary. The main thread builds an immutable `ProcessList`, pre-allocates its buffer pool, and resolves Plugin Delay Compensation (PDC) before publishing it atomically; the audio callback and its worker pool execute the DAG without locks, allocations, or shared mutable indices.

![Architecture diagram of Infinite's lock-free audio engine showing main-thread topology compilation, atomic ProcessList swap, parallel RunClaims DAG workers, idempotent CompensationDelay PDC, and ParamMailbox per-sample ramping](https://infinitedevices.org/assets/blog_lock-free-audio-graph-pdc-and-parameter-mailboxes.png)

## Main-thread topology compilation and atomic swap

Whenever a cable is connected, a node is added, or an arrangement clip changes, `RebuildAudioTopology` walks the editor graph on the main thread and produces a topologically sorted `AudioTopology` (`sources` before `consumers`). `AudioEngine::SetTopology` wraps that topology in a `ProcessList` and pre-allocates every `PooledBuffer` up front, bounded by compile-time ceilings (`kAudioMaxBlockFrames = 4096`, `kAudioMaxChannels = 8`, `kAudioMaxNodeInputs = 32`, `kAudioMaxNodeOutputs = 12`).

Inside `RebuildAudioTopology`, `PrepareToPlay` is only called when `entry.node->preparedForSampleRate != sampleRate`. Unconditionally calling `PrepareToPlay` on every topology rebuild would reset filter, delay, and reverb state inside already-running nodes whenever an unrelated cable changes elsewhere in the graph.

Once the `ProcessList` is ready, `SetTopology` increments `mPublishedGeneration`, computes the parallel execution schedule (`BuildSchedule`), and swaps `mCurrent.exchange(fresh, std::memory_order_acq_rel)`. Superseded `ProcessList` objects are pushed onto `mRetiring` and freed by `DrainRetired()` only after the audio thread's `CompletedGeneration()` has moved past them, guaranteeing the audio callback never touches a deleted topology and never frees memory itself.

## Parallel DAG execution with atomic work claiming

A modular patch with independent synth and effect chains should scale across cores without spin-lock contention or per-block thread creation. When a topology's graph width exceeds one (`list->width > 1`), `AudioEngine::RunTopology` dispatches work across `AudioWorkerPool` alongside the audio thread itself:

- **Precomputed dependency counts**: `BuildSchedule` stores each node's upstream dependency count in `depCount[p]` and its downstream indices in `succ[succStart[p]..succStart[p+1])`. At the start of each block, `pending[p]` is initialised from `depCount[p]`.
- **Single-atomic work claiming**: `mClaim` packs the 32-bit block sequence number `seq`, the total node count, and the next unclaimed index into one 64-bit `std::atomic`. Both the audio thread and woken workers loop in `RunClaims`, claiming the next schedule slot with `compare_exchange_weak`.
- **Deadlock-free dependency waiting**: Because `runOrder` is topologically valid, any dependency of slot `p` sits at an earlier position and is already claimed by an active thread. A worker only waits while `list->pending[p].load(std::memory_order_acquire) > 0`, runs `RunEntry`, decrements each successor's `pending` counter with `fetch_sub(1, std::memory_order_acq_rel)`, and increments `mDone`.
- **Consistent floating-point state**: Workers synchronise their hardware floating-point control register (`AudioWorkerPool::ReadFpControl()` / `WriteFpControl()`) to match the audio callback's denormal mode (`mJob.fpControl`) before processing any DSP.

If a topology has no parallel width (`width <= 1`), `RunTopology` skips the atomic dispatch and runs `list->topology.order` serially on the audio thread.

## Idempotent plugin delay compensation at merge points

Latent nodes, such as lookahead limiters, spectral processors, or [out-of-process VST3 and AU plugins](https://infinitedevices.org/blog/out-of-process-vst3-au-plugin-shared-memory-ipc.html), report their delay via `LatencySamples()`. Without alignment, parallel branches merging into a **Mixer** or **Dynamics** sidechain input would comb-filter or smear transients.

Because `order` is already topologically sorted, `RebuildAudioTopology` computes cumulative latency in one forward pass:

1. For each node, `maxUpstream` is the maximum `cumulativeLatencyByBuffer` across its connected `inputBufferIndices`. Note-only inputs do not enter `inputBufferIndices` and add zero latency.
2. The node's output latency is `entry.node->LatencySamples() + maxUpstream`, written to `cumulativeLatencyByBuffer` for every output pin.
3. At every node input pin, the required delay is `maxAmongConnected - cumulativeLatencyByBuffer[idx]`. The same calculation runs across non-live `AudioTerminal` outputs feeding the device mix (`maxAmongTerminals`).

Crucially, `CompensationDelay` instances live on persistent owners (`AudioNode::inputCompensation` for input pins, `AudioCaptureRing::compensation` for canvas **Audio Out** terminals, and main-thread-managed slots for arrangement clip terminals) rather than inside the transient `AudioTopologyEntry` struct. `CompensationDelay::Prepare(delaySamples, numChannels)` is idempotent: if `delaySamples` and `numChannels` match what the ring buffer already holds, `Prepare` returns immediately without reallocating or zeroing in-flight audio. Branches with `0` samples of delay allocate nothing (`mBuf` stays empty) and run as a passthrough.

## Per-sample parameter smoothing with ParamMailbox

UI sliders and modulation updates arrive once per visual frame, whereas DSP kernels like [Reverb](https://infinitedevices.org/blog/16-line-hadamard-fdn-reverb-dsp.html) and **Analog** read parameters at audio sample rate. `ParamMailbox` bridges the two threads using a flat array of `std::atomic<float> mTarget[128]` (`kMaxParams = 128`) instead of a shared head/tail ring buffer, eliminating any consumer-index race under concurrent updates.

Before pushing a frame's parameter values, the main thread calls `ParamMailbox::PublishFrame(stampSeconds, delaySeconds)` to record the engine-time stamp and expected frame interval. On the audio thread, `SmoothedValue(paramId)` loads the latest atomic target:

- **First-value priming**: When the first `Push` arrives (`mPushed`), `r.primed` snaps `cur` and `target` straight to the initial value rather than ramping up from `0.0f` over the first block.
- **Timestamped linear ramps**: Subsequent changes compute a sample count clamped between `kMinRampSamples = 16` and `kMaxRampSeconds = 0.25 s` (defaulting to `kFallbackRampSeconds = 0.005 s` in headless tests before any frame stamp exists) and step linearly per sample.
- **Single-slot ramp queueing**: If a new target arrives while a ramp is still running and its end time is later than the current `r.endTime`, `OnNewTarget` queues it in `pendingValue` / `pendingEnd` so the smoother reaches the previous frame's value before turning toward the next.
- **Block-rate hoisting**: Nodes that read a parameter once per block call `BlockValue(paramId, numFrames)`, which advances the sample ramp by `numFrames`, and kernels can check `IsSettled(paramId)` to hoist coefficient updates out of the inner sample loop.

## Exploring Infinite's signal graph

You can inspect how these audio-graph primitives behave in real patches by downloading [Infinite](https://infinitedevices.org/) or browsing ready-made synth, mixing, and audiovisual patches in the [Infinite library](https://infinitedevices.org/library.html).

There are honest limits to keep in mind. Infinite is a young, MIT-licensed open-source project: desktop plugin hosting supports VST3 and AU without AAX, live hardware audio inputs are refused in headless mode, and the pooled audio graph enforces fixed upper bounds of `4096` frames per block, `8` channels, `32` inputs per node, and `128` parameters per `ParamMailbox`.
