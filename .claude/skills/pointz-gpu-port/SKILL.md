---
name: pointz-gpu-port
description: Procedure for moving an Infinite-Turbo PointZ operator (src/pop/PopOps*.cpp) from the CPU to the GPU, or for adding a GPU PointZ operator - kernel specs, the compute and transform-feedback backends, async readback rules, the equivalence check required by rule R5, perf measurement and the P11 status table. Use for any P-item of docs/plans/NEXT-BACKLOG.md (P1-P10), when an operator is "too slow", "runs on the CPU", "stalls", or when writing a new PointZ operator.
---

# pointz-gpu-port

Read first: `docs/adr/0001-pointz-compute-backend.md` (the kernel backends), `docs/pointz-gpu-status.md`
(P11: where each operator stands), rules R3 and R5 in `docs/plans/NEXT-BACKLOG.md`.

## Where things are

- Operators: `src/pop/PopOps*.cpp`. `Simple(...)` defs run GLSL kernels; `Cpu(...)` defs cook on
  the CPU (often `Load(*in)` = download every attribute).
- Kernels: `pop::RunKernel(KernelSpec, in, uniforms, env, out, error)` in `src/pop/PopGpu.cpp`.
  `KernelSpec`: `body` (GLSL statements run per point; every read/write attribute is a local of its
  own name), `decls` (helpers), `reads`, `writes`, `lookups` (`fetch_<name>(int i)`, random access,
  index clamped), `usesImage` (`sampleImage(uv)`). Built-ins: `id`, `uCount`, `uTime`, `uBeat`,
  `uDelta`, `uFrame`, `uSeed`; preamble has fbm / curl / eulerDeg.
- Backends: transform feedback (default today, max 4 writes) and compute
  (`INFINITE_POP_BACKEND=compute`, no write limit). `INFINITE_POP_BACKEND=both` runs both and prints
  `POPEQUIV ok|FAIL max <diff> :: <body>` per distinct kernel.
- Async CPU access: `pop::AsyncRows` (rows of attributes, a frame late), `PopData::BoundsAsync`
  (bounds), `GLUtil::AsyncReadback` (textures). Never `->Cpu()`, `.Read(`, `Load(` or
  `glGetBufferSubData` on a per-frame path for data a kernel just wrote (R3).

## Procedure

1. Row in `docs/pointz-gpu-status.md`: current path, what it reads on the CPU and why.
2. Keep the CPU path as the fallback and as the reference. A new GPU path goes next to it, chosen
   when the input is on the GPU (`!attr.buffer->OnCpu()`), like Copy PointZ.
3. Prefer one `KernelSpec` per pass. Random writes, compaction, scans, sorts, topology: wait for
   P2 (`src/pop/GpuAlgo`) instead of hand-rolled atomics in one operator.
4. Equivalence (R5): an env var that runs both paths and compares attribute by attribute (pattern:
   Copy PointZ), max difference 0 or a documented tolerance. Kernel-level backends are covered by
   `POPEQUIVTEST`; an operator-level CPU->GPU port needs its own check, wired into POPTEST or a
   unit test.
5. RNG: GPU randomness is hashed from `id` / `uSeed`. If the CPU path used a sequential RNG, results
   change: document it, keep the legacy CPU mode reachable (a param added at the END of
   VisitParams, default = legacy for old patches, R1).
6. Measure: `test-windows.bat -PerfOnly -SkipBuild` (A/B against `build\perf-ref`) and, for detail,
   the Tracy build (`-DINFINITE_ENABLE_TRACY=ON`, zones "PointZ cook" per operator and
   "PointZ kernel"). Check `INFINITE_GLDEBUG=1` shows no new per-frame performance warnings
   (`INFINITE_GLDEBUG=sync` names the code line).
7. Gate: `turbo-branch-workflow`; update the P11 row (path, equivalence, cost) and the changelog.

## Traps seen here

- std430 pads `vec3[]` to 16 bytes: SSBOs are `float[]` with an explicit stride.
- A buffer with fewer components than the GLSL type reads 0, 0, 0, 1 for the missing ones (vertex
  attribute rule); the compute generator copies that rule - keep it if you write SSBO reads by hand.
- Reusing a just-freed buffer stalls the GPU: the pool (`AcquireGpuBuffer`) already delays reuse by
  two frames; do not bypass it with your own `glGenBuffers` per frame.
- One shared scratch/result buffer for many calls in a frame serialises them (the bounds sync fixed
  in 0.70): give each async user its own buffer.
- Functions that run their own passes may leave no program bound (`glUseProgram(0)`): after calling
  one in the middle of setting uniforms, bind your program again.
