---
name: codebase-navigation
description: How to search Infinite's codebase efficiently and completely — trace an interface to every implementer and every hand-maintained registration site, not just the first grep hit; check both platform paths; check the living map of known connective-tissue hotspots below. Use at the start of ANY investigation, sweep, or plan-writing task that touches src/ — before reporting "the code does X" or handing findings to a planning/implementation agent. This skill accumulates a growing map of the codebase's cross-cutting wiring points; add to it whenever a task turns up a new one.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

This skill exists because Infinite has a lot of code that looks local but
isn't: a node's behavior is split across its class, a shader string literal,
a hand-maintained registration list in `main.cpp`, a platform-specific
counterpart in `src/platform/`, and sometimes a cache keyed by something
none of those files mention. An agent that greps once, reads the first hit,
and reports back will miss the second and third places the real answer
lives — and a plan built on that report will be wrong in a way nobody
catches until implementation.

## The method

1. **Never stop at the first match.** A `grep` for a symbol tells you where
   it's *mentioned*, not where it's *wired*. For any node, interface, or
   system, chase all of:
   - The interface/base class declaration and **every** class implementing
     it (grep the interface name as a base-class token, not just as a
     free-text word).
   - The **registration site** — Infinite has several hand-maintained lists
     in `src/main.cpp` (node type→constructor tables, drag-drop extension
     allowlists like `kModelExt`, the four cable-wiring chains documented in
     `cable-logic-sweep`, palette/category tables). A class existing is not
     the same as it being reachable — check it's actually registered.
   - **Shader/GLSL string literals** (grep for `uniform`, `sampler2D`, or the
     specific uniform name) — a param can be plumbed through `VisitParams`,
     a struct, and a cable, and still do nothing if the shader never
     declares or samples the matching uniform.
   - **Both platform implementations.** Anything under `src/platform/` (or
     called through a `Platform::` function) has a macOS body (often
     `.mm`) and a Windows body (often `*Win.cpp`) that can diverge or be
     entirely unimplemented on one side. Read both before claiming a
     capability exists.
   - **Serialization** (`VisitParams`) vs. **runtime state** — a field can
     be real at runtime and silently dropped on save/load, or vice versa.
   - **CMakeLists.txt** for anything involving a new file, a new external
     dependency, or a new source added to a target — code that isn't in the
     build isn't real.

2. **Prefer reading a whole file over reading grep context lines** once a
   file is confirmed relevant — struct layouts, enums, and switch tables in
   this codebase are usually short enough to read in full, and half-context
   is exactly what causes missed connections (e.g. seeing a `Material`
   struct without noticing the `MaterialMap` enum sitting 20 lines above
   it).

3. **State what you did NOT find, not just what you found.** "No
   submesh/material-index field exists in `Mesh.h` (confirmed by grep,
   zero hits)" is a finding. Silence on a question is not the same as
   having checked and found nothing — say which you did.

4. **When reporting to another agent (plan, implementation, review),
   cite file:line for every claim.** A downstream agent should be able to
   act on your report without re-deriving it from scratch.

## The living map

This section is a growing index of connective-tissue hotspots already
discovered — places where behavior fans out across files in a way a single
grep won't surface. **When a task turns up a new one, add it here** (short
entry: what it connects, where each end lives, one line on why it's easy to
miss). Keep entries terse — this is an index, not a report. Delete/update an
entry if a refactor makes it stale.

- **Node type registration**: a new node class also needs a constructor
  entry in `main.cpp`'s node-type table and (for file-droppable types) an
  extension allowlist like `kModelExt` (`main.cpp:41352-41354`) — see
  `new-*-node` skills per node category for the full checklist.
- **The four cable-wiring chains** (what can connect to what) are
  hand-maintained in `main.cpp`, independent of what pins a node's header
  declares — see `cable-logic-sweep`.
- **`IGeometrySource`** (`src/nodes/Geometry3DNodes.h:118-200`): terminal
  and pass-through 3D nodes implement this. `Material` (one per source, no
  array) and `MaterialMap` (8-channel enum: albedo/roughness/metallic/
  normal/ao/emission/clearcoat/sheen) live in the same header. `Mesh`
  (`src/core/Mesh.h`) has no submesh/material-index concept — one material
  and one UV channel is a hard assumption in the data structure itself, not
  just something individual nodes ignore.
- **`MaterialNode`** (`src/nodes/UtilityNodes.h:209-336`) is the real
  multi-texture-channel node (8 `ImageCable`s, one per `MaterialMap`).
  `ImageCable` (`src/core/ImageCable.h`) always wraps a graph `INode*` —
  there is no way to hand it a raw texture or in-memory buffer without a
  node backing it.
- **`AssetCache<T>`** (`src/core/AssetCache.h`) is keyed by file path +
  mtime/size, one cache instance per decoded-value type. It has no notion
  of "multiple sub-assets embedded in one file" — anything that decodes N
  things from one path (e.g. a model file with embedded textures) needs to
  bundle them into one cached struct, not invent a sub-key.
- **Model loading**: `Platform::LoadModel` (`Platform.h:78-93`) is macOS
  ModelIO (`Platform.mm:808-944`) on one side and hand-rolled OBJ/STL/PLY
  parsers (`platform/win/MediaDecodeWin.cpp:189-923`) on the other — not a
  shared implementation, so a format supported on macOS may not be on
  Windows and vice versa.
- **File-import decode is synchronous on the main thread** for both images
  (`ImageSourceNode::Load`) and models (`ModelSourceNode::Load`, called
  inline from the drop handler at `main.cpp:41508-41519`). The only
  precedent for async/worker-thread loading in this codebase is audio-side
  (`ARCHITECTURE.md:90-168`, e.g. SampleScanner) — don't assume an async
  pattern exists elsewhere just because it would make sense.
- **Vendoring pattern**: single-header/small C libraries (stb, miniz,
  tinyexr, dr_libs) live under `external/<name>/` with one
  `include_directories` line in `CMakeLists.txt` — no FetchContent, no
  submodule. Larger deps (GLFW, libFLAC) use `FetchContent`; huge prebuilt
  binaries (ONNX Runtime) are fetched as zips; VST3 SDK is a submodule.
  Match the existing tier when proposing a new dependency.
- **sRGB/linear convention**: textures are uploaded as plain `GL_RGBA8`
  (no `GL_SRGB8_ALPHA8` anywhere in the codebase) — sRGB decode happens
  in-shader via `toLinear()` for albedo/emission; roughness/metallic/ao/
  normal maps are sampled as linear single-channel (`.r`) in
  `Geometry3DNodes.cpp`. A new texture path must match this or colors will
  be double-corrected or not corrected at all.
- **`SpawnNode` returns a dangling-prone `GraphNode*`**: it points into
  `gNodes` (`std::vector<GraphNode>`), which any *later* `SpawnNode` call
  can reallocate. Holding a `GraphNode*` from one `SpawnNode` call across a
  second `SpawnNode` call (e.g. spawning a Model 3D, then a Material, then
  wiring them) is a silent use-after-free — no crash, just garbage
  `->index`/`->node` reads that make `ConnectNodes` fail unpredictably
  depending on the vector's current capacity (small/early-session spawns
  fail more often than later ones, since a bigger already-grown vector is
  less likely to reallocate on the next push_back). Capture the stable
  `int index` field immediately after each `SpawnNode` call instead, and
  re-resolve via `FindNodeByIndex(index)` whenever the node is needed again
  after another `SpawnNode` in between. Found and fixed in the glTF
  drag-drop importer's multi-node spawn (`main.cpp`'s `kGltfExt` drop
  branch) — the bug was invisible in casual testing because it only
  reliably reproduced on the *first* multi-node spawn of a session, before
  `gNodes`' capacity had grown from earlier spawns.
- **Querying `ed::GetNodePosition`/`GetNodeSize` right after spawning (or
  loading) a node reads a stale/zero result** — the node editor only
  syncs a `GraphNode`'s `ed::` position/size from `spawnX`/`spawnY` later
  in the same frame's draw pass (`main.cpp` ~line 47870,
  `ed::SetNodePosition(gn.NodeId(), ImVec2(gn.spawnX, gn.spawnY))`), and
  size is only known after the node has actually been drawn once
  (`FindFreeSpawnPosition`'s comment: "never laid out ... nothing to avoid
  yet"). Code that needs a freshly-spawned/loaded node's real screen
  rect (e.g. a test fixture driving `gDroppedFiles`/`gDropPos` to target
  an existing node) has to wait at least one full frame after the spawn/
  load before reading its position, not query it the same frame.
- **`gDroppedFiles`/`gDropPos`** (`main.cpp:948-949`) is the same queue a
  real OS file-drop populates (`OnFilesDropped`) and is processed
  unconditionally every frame (`main.cpp`'s `if (!gDroppedFiles.empty())`
  block, ~line 41346) — a self-test fixture can drive the real drop-handler
  code path (extension routing, drop-onto-existing-node vs. fresh-spawn,
  undo checkpointing) by pushing a real path into this queue and a real
  screen-space point into `gDropPos`, then checking results one frame
  later, instead of needing OS-level UI automation. See
  `INFINITE_GLTFDROPTEST` for a worked example including the position-
  timing gotcha above.

- **`main.cpp`'s `getenv("INFINITE_...")`-gated self-test fixtures are not all
  wired into `run-infinite-hygiene`'s `driver.sh`.** A fixture existing in
  `main.cpp` (e.g. `FIELDGRAPHTEST`, `FIELDGRAPHRATETEST`, `FIELDGRAPHUNDOTEST`,
  `FIELDGRAPHBLASTTEST`) is not the same as `driver.sh` ever running it —
  check its `TIER1_CHECKS`/`GROUP_*`/`FULL_TESTS` arrays before assuming a
  named test is part of the hygiene gate, and before adding anything to
  `known-test-failures.txt` (which only takes effect for tests `driver.sh`
  actually invokes). A prior investigation hand-ran `INFINITE_FIELDGRAPHTEST=1`
  outside the harness, got a real SIGSEGV, and misattributed the same crash to
  the unrelated `FIELDGRAPHBLASTTEST` fixture next to it — re-running the
  crashing test repeatedly never caught the misattribution because nobody
  diffed the log against source per-test. The real bug (fixed) was two
  independent issues stacked in the `FIELDGRAPHTEST` fixture itself, not the
  app: `main.cpp:48193` declared `param int voices` when
  `src/core/field/FieldParse.cpp:404-407` only ever allowed `float` params
  (causing an unguarded `ParamTable::Find("voices")->value` null-deref at
  `main.cpp:48224`), and its for-loop used `i` as the loop variable, which
  `i` is reserved as the element-domain per-element index in Field (see
  `field-language`) — every sibling fixture in the file already used `k`.

## Adding to this map

At the end of a task that touched `src/`, if you found a cross-file wiring
point that isn't listed above, add a short entry before finishing — that's
what keeps this skill useful as the codebase grows instead of going stale.
