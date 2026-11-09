# Spike 1.0: plugin shell + engine helper over shared memory

The throwaway proof for `docs/plans/vst-au/README.md` step 1.0. It answers one question:
can a VST3/AU shell in the host process spawn its own engine process and swap audio with
it over shared memory, fast enough and safely enough to build chunk 1 on?

```
 host process                         helper process
 +---------------------------+        +-------------------------+
 | InfiniteSpike.vst3 / .component |  | infinite-spike-engine   |
 |  setActive(true) -> posix_spawn ---> maps shm, sets attached  |
 |  process():                |        |  waits on postedSeq     |
 |   write inRing [P, P+n)    |  shm   |  cooks each BlockDesc   |
 |   post desc, wake ---------+------->|  writes outRing         |
 |   read outRing [R-L, R-L+n)|<-------+  doneFrames, wake       |
 |   (L = max block latency)  |        |  exits with parent      |
 +---------------------------+        +-------------------------+
```

The helper's "graph" is `0.5 * in + 0.05 * sin(440 Hz)` so a test can prove it ran.

## Files

| File | What |
|---|---|
| `spike_shm.h/.cpp` | shm layout, os_sync wait/wake (polling below macOS 14.4) |
| `spike_engine.cpp` | the helper: kqueue parent watch, Mach realtime thread |
| `spike_shell.cpp` | VST3 FX: spawn on activate, one block latency, offline waits, realtime bounded at 60 % of the block |
| `spike_bench.cpp` | B-5 round trip, paced at 48 kHz |
| `spike_hosttest.cpp` | loads the .vst3 like a DAW: 4 checks below |
| `spike_auoop.mm` | instantiates the AU in or out of process (R-1) |
| `au-info.plist.in` | AU v2 entry for the SDK auwrapper |

## Build and run

```bash
python3 scripts/ibuild.py run -- cmake -S tools/plugin/spike -B build-spike -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_DIR=$PWD/external/vst3sdk
```

```bash
python3 scripts/ibuild.py run -- cmake --build build-spike --config Release
```

```bash
build-spike/bin/Release/spike_hosttest build-spike/VST3/Release/InfiniteSpike.vst3
```

For the AU, copy `build-spike/AU/Infinite Spike.component` to `~/Library/Audio/Plug-Ins/Components`,
then `auval -strict -v aufx Ispk Infn` and `build-spike/bin/Release/spike_auoop out`. Remove it after.

## Results (2026-11-07, M2 8 GB, macOS 27.2)

| Check | Result |
|---|---|
| Steinberg validator | 47 passed, 0 failed; no helper left running |
| auval -v and -strict | AU VALIDATION SUCCEEDED (14 helper spawns, none left) |
| Offline vs expected | max error 1.6e-08 |
| Two offline instances | bit-identical |
| Realtime after attach | exact |
| Crash drill (SIGKILL helper) | host alive, plugin output 0 |
| AU out of process (AUHostingServiceXPC) | helper spawns from the sandboxed service, latency 512, tone residual 0.0500 |

B-5 round trip (post, wake, reply), µs:

| Block | p50 | p99 | p99 % of block |
|---|---|---|---|
| 32 | 21 | 150 | 22 % |
| 128 | 37 | 108 | 4.1 % |
| 512 | 62 | 165 | 1.2 % |
| 2048 | 112 | 309 | 0.7 % |

Stop condition S1-b (p99 > 25 % of a 128 block) is not hit. Helper attach takes 3-40 ms
warm, about 400 ms on the first cold launch.

## What it did not prove

| Gap | Where it goes |
|---|---|
| Reaper and Logic / GarageBand runs | owner DAW trials (no DAW installed here) |
| Desc ring overflows if the engine stalls for 256 blocks | step 1.3 adds a resync |
| Windows, Linux | steps 1.2 / 3.x |
| Real engine, MIDI, parameters, GUI | steps 1.4-1.5, chunk 2 |
