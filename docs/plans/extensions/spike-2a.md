# Spike 2a: hand models in ONNX Runtime (macOS, Apple M2)

Date: 2026-10-10. Scripts: `tools/tracking-spike/`. Models: MediaPipe `hand_landmarker.task` (float16, Apache-2.0), converted with tf2onnx 1.16.1, opset 15, ONNX Runtime 1.19.2.

## Result: the approach works on macOS

| Check | Result |
|---|---|
| Conversion | Both models convert cleanly (palm detector, hand landmark) |
| Fidelity vs TFLite (CPU ONNX) | Worst output difference 1.4e-4 on landmarks in a 224 px space; scores match to 1e-6 |
| Fidelity, Core ML vs CPU | Max landmark difference 0.24 px in 224 px space (about 0.1 %), fine for modulation |
| Size | **15.5 MB for hands alone** as fp32 ONNX (detector 4.6, landmark 10.9) |

## Latency (model run only, 300 runs, warmed up, M2)

| Model | CPU p50 / p95 | Core ML p50 / p95 |
|---|---|---|
| Palm detector (192x192) | 13.9 / 17.5 ms | 11.9 / 12.8 ms (35 partitions, 106 of 144 nodes supported: no real gain) |
| Hand landmark (224x224) | 15.3 / 32.5 ms | **0.8 / 1.0 ms** |

What it means per frame (add roughly 1-2 ms for crop and resize, not yet implemented):

| Frame type | CPU only | With Core ML |
|---|---|---|
| Tracking frame (landmark only; the common case) | about 16 ms | **about 2 ms** |
| Detection frame (start or after loss: detector + landmark) | about 30 ms | about 14 ms |

Against the plan's budget (processing <= 30 ms GPU, <= 50 ms CPU): met with margin on this machine. The camera's own 33-80 ms is the larger part of the total, as predicted.

## Not yet known

- **Windows and Linux:** not measured. DirectML and the Linux CPU path need their own runs (CI or a VM). The CPU p95 of 32 ms on the landmark model shows jitter even here; check thread settings.
- **Slow hardware:** an Intel laptop could be several times slower on CPU. The 20 fps CPU floor is not proven.
- **Pipeline cost:** anchor decoding, rotated ROI crop and smoothing are not written, so the end-to-end number is an estimate (model time + 1-2 ms).
- **Size:** 15.5 MB is hands only. Pose and face will push the pack past the plan's 15-20 MB estimate unless the weights are stored as fp16 (halves the size; needs a fidelity re-check) or the pack is split per capability.
- **Shipping runtime:** this used the pip ONNX Runtime. The app needs the official macOS universal2 build with the Core ML provider, and a check that it loads from the pack folder.
- **Real camera latency:** the glass-to-glass check from the plan is not done.

## Next
1. fp16 weights: size and fidelity.
2. Windows (DirectML) and Linux (CPU) numbers.
3. Official macOS universal2 ONNX Runtime packaged in a test pack and loaded by the app.
4. Then the tracking pipeline and `INFINITE_TRACKINGTEST`.
