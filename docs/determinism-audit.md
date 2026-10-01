# Determinism audit: clock and rand hits (R504)

Offline renders (`--frame`, `--frames-dir`, `--render`, `--audio-summary`) must depend on the
patch and the Transport time only. Checked with
`grep -rn "glfwGetTime|ImGui::GetTime|steady_clock|system_clock|random_device|rand("` over
`src/nodes`, `src/audio`, `src/core`, each hit classified by whether it can reach a rendered frame
or sample.

| Hit | Where | Reaches output? | Verdict |
|---|---|---|---|
| `std::random_device` seed | `MolderNode.cpp` (load file / finish recording) | No: the seed is saved (`seed`, `generation` in `VisitParams`) and replayed from the patch. Only a *new* load or recording draws a fresh one, which is a user action. | fine |
| `rand()` | `DrumSequencer` | No: a user clicking randomise; the result is saved as steps. | fine |
| uid generation | `FieldGraph` | No: identity only, not in any kernel input. | fine |
| clock reads | `AudioMeter`, `AudioEngine` | No: UI refresh and device watchdog; not in the render path. | fine |
| clock reads | `AudioDisplacement`, `WaveTerrain` | They have an offline branch that uses the Transport sample clock. | fine, keep the branch |
| GLSL `rand` hashes | effect shaders | Hash of position/time uniforms, no state: deterministic. | fine |

Nothing here needs a by-design exception, so `tools/determinism-by-design.txt` is empty.

## How it is guarded

- `tools/determinism-check.py <patch> [--bisect]` renders twice in separate processes and
  compares bytes. With `--bisect`, a differing frame is followed by a per-node render
  (`--frame --node <index>`) to name the first node, in patch order, whose own image differs.
- `tests/render/*.inf` (2D compositing, 3D render, Field Pixel, audio synth + reverb, modulation)
  are all bit-identical twice on macOS today.
- `tools/render-check.py` compares them against `tests/render/golden/` with a tolerance. The macOS
  goldens also pass on the Linux (Mesa software GL) build to within 0.11/255 mean difference.
  Wired into the macOS and Linux (Xvfb, clang) jobs in `.github/workflows/build.yml`.
  Update goldens deliberately: `python3 tools/render-check.py --update`.

## Not done

- "In-process twice": rendering the same patch twice inside one process (catches leaked static
  state that two fresh processes hide). Needs a headless mode in `HeadlessJob`; tracked as a new quest.
- Windows is not in the golden job yet (no Windows machine to produce a first run).
