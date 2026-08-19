# Restore VST3 plugin hosting with a rebuilt bundle-resolution architecture

You are working on **Infinite**, a node-based audiovisual workstation for macOS
(`/Users/namansoni/infinite`). This is a compiled C++/Objective-C++ app. Read
`ARCHITECTURE.md` and `docs/CODE_STANDARDS.md` before you start.

Your job is to bring back VST3 plugin hosting, which previously existed and was
deleted, **and to replace the part of it that never worked** — bundle
resolution. Do not simply revert the deletion. The old resolution chain is the
bug; restoring it unchanged reproduces the failure.

---

## 0. Licensing gate — confirm before writing any code

The Steinberg VST3 SDK is **GPLv3-or-commercial**. Infinite's own source is MIT
(see `LICENSE`). Linking the SDK means the distributed binary must be GPLv3
unless a commercial Steinberg license is held.

This is why VST3 was removed last time. Commit `58d5c48` reverted this addendum
out of `LICENSE`:

> Infinite's own source code is licensed under the MIT License above.
> Optionally building with `INFINITE_ENABLE_VST3=ON` links the Steinberg VST3
> SDK, which is licensed under the GNU General Public License version 3
> (GPLv3). When distributed with VST3 support enabled, the combined binary must
> be distributed under the terms of the GPLv3.

**Ask the user to confirm they accept this before proceeding.** If they do,
restore that addendum to `LICENSE` as part of this work. The pre-VST3 comment
in `src/platform/Platform.h` also states the constraint explicitly ("the VST3
SDK is GPLv3-or-proprietary and this codebase is MIT") — update it rather than
leaving it contradicting the build.

---

## Background: what exists and what happened

- `42ab78d` (Aug 16) added VST3 hosting: `src/platform/PluginVST3.mm` (1833
  lines, a real Steinberg `IPluginFactory`/`IComponent`/`IEditController` host
  with a working editor window), `src/platform/PluginVST3.h`, additions to
  `src/platform/PluginHandleInternal.h`, scanner integration, CMake wiring, and
  `external/vst3sdk` as a git submodule.
- `58d5c48` (Aug 16, ~3 hours later) deleted all of it.

**Recover the old implementation, don't rewrite it from scratch:**

```bash
git show 42ab78d:src/platform/PluginVST3.mm > src/platform/PluginVST3.mm
git show 42ab78d:src/platform/PluginVST3.h  > src/platform/PluginVST3.h
```

The DSP/editor/parameter half of that file is sound and worth keeping. Only the
resolution half (item 3 below) needs replacing.

### What was verified about the failure

The reported symptom is **"VST3 bundle not found on disk"**, emitted at
`PluginVST3.mm:1025` in the recovered file. It is the terminal state of a
four-step chain inside `PluginVST3Create`:

1. `desc.path` if non-empty and `fs::exists`
2. `GetCachedVST3BundlePath(desc.identifier)` — an in-process
   `std::unordered_map` (`gVST3BundleMap`)
3. probe `<root>/<desc.name>.vst3`
4. full rescan of all VST3 folders, then re-check the cache

Confirmed causes, in order of importance:

- **Step 3 is unsound.** `desc.name` is the *factory class name*, which
  frequently differs from the bundle filename. It produces both misses and
  wrong hits.
- **Step 2's cache is in-process only**, seeded solely by a full scan or by
  `LoadFromDisk`. It starts empty every launch.
- **`PluginScanner::LoadFromDisk` hard-filters VST3 out.** At
  `src/audio/PluginScanner.cpp:172` the current code reads
  `if (!e.identifier.empty() && e.format == "au")`. Any `vst3` entry in the
  persisted index is silently dropped on load, so the cache can never be
  re-seeded from disk. **This alone reproduces the bug on every launch after
  the first.**
- **Step 4 is fragile enough to never complete.** `DescribeVST3Bundle` does
  `CFBundleCreate` → `CFBundleLoadExecutable` → `bundleEntry` → read factory →
  `bundleExit` → `CFRelease` **in-process, for every bundle**. This machine has
  176 bundles in `/Library/Audio/Plug-Ins/VST3`. The recovered file's own
  comment at line ~720 records a plugin that raises `Resource 'One.fil' not
  found` on unload and "takes the host down with it." One hostile bundle kills
  the scan thread, the index is never written, and the next launch has nothing.

### What was ruled out

**Architecture is not the cause.** Of the 176 installed bundles, 168 are
arm64-capable. Only these 8 are Intel-only and will legitimately fail to load
in an arm64 host: `Auto-Tune Vocal Compressor`, `Auto-Tune Vocal EQ`,
`BPB Saturator`, `EZkeys 2`, `Filterstation2`, `Ozone Imager 2`,
`Transient Master`, `VocalSynth Pro`. These must surface a clear per-plugin
error, not a scan-wide failure. (`build/CMakeCache.txt` has
`CMAKE_OSX_ARCHITECTURES=arm64;x86_64`; the app runs arm64 on this machine.)

### What already works and must not be re-plumbed

`AudioPluginNode` is **already format-agnostic**. `pluginFormat` is a saved
patch field (`src/nodes/AudioPluginNode.cpp:733`, `v.Text("plugin_format", ...)`),
set from `desc.format` at `:384`, and read back at `:427` defaulting to `"au"`.
`src/nodes/AudioPluginNode.h:146` documents this as deliberately pre-built for a
second backend. **No patch-format change is needed and none should be made.**

---

## The work

### 1. Restore the SDK and the build wiring

Re-add the submodule and CMake option (recover from `42ab78d:CMakeLists.txt`,
lines 34–48 and 172–186):

```bash
git submodule add https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk
git submodule update --init --recursive external/vst3sdk
```

Keep the existing shape: `option(INFINITE_ENABLE_VST3 ... OFF)`, a
`FATAL_ERROR` if the option is ON but
`external/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h` is missing, the
include dirs, `PluginVST3.mm` + `coreiids.cpp` + `vstinitiids.cpp` in
`target_sources`, `-fobjc-arc` on `PluginVST3.mm`, and
`INFINITE_ENABLE_VST3=1` as a compile definition.

**Judgment call, flagged:** the option defaulting to `OFF` is why a clean
checkout silently builds the stub that reports *"VST3 plugins are disabled in
this build"*. Leave the default `OFF` (it is the correct default given the
GPLv3 constraint), but **make the disabled state visible in the UI** — see item
6. Do not flip the default without asking.

Also restore the `#if INFINITE_ENABLE_VST3` dispatch arms in
`src/platform/Platform.mm` and the declarations in `src/platform/Platform.h`
(`EnumerateVST3Plugins`, `DescribeVST3Bundle`, `CacheVST3BundlePath`,
`SetVST3SearchFolders`, plus the `format`/`path` field comments). Recover from
`git show 58d5c48 -- src/platform/Platform.h src/platform/Platform.mm` and read
the `-` lines. In the recovered `Platform.mm` the arms sit at roughly lines
3394–4036 (`PluginCreate`, `PluginPoll`, `PluginPrepare`, `PluginDestroy`,
`PluginRender`, `PluginScheduleMIDIEvent`, `PluginParameterCount`,
`PluginParameterInfo`, `PluginParameterInfoByAddress`, `PluginSetParameter` —
and any others that branch on `h->desc.format == "vst3"`; grep for that string
in the recovered version and make sure **every** arm comes back, not just the
ones listed here).

### 2. Make the persisted index the single source of truth

This is the core fix. `PluginIndex.json` already round-trips `path` in both
directions (`src/audio/PluginScanner.cpp:169` on load, `:206` on save) — the
plumbing is there and is not the problem.

- **`src/audio/PluginScanner.cpp:172` — delete the `e.format == "au"` filter.**
  Accept `au` and `vst3`. Reject unknown formats.
- **In `LoadFromDisk`, re-seed the resolver cache**: for each loaded entry with
  `format == "vst3"` and a non-empty `path`, call
  `Platform::CacheVST3BundlePath(e.identifier, e.path)`. Commit `42ab78d` had
  this at `PluginScanner.cpp:205-206`; it went away with the removal.
- **Bump `kIndexSchemaVersion`** in `src/audio/PluginScanner.h:20` from `4` to
  `5`. The user's live index at
  `~/Library/Application Support/Infinite/PluginIndex.json` is currently
  schema 4 with 266 AU entries and zero VST3 entries; a bump forces one clean
  rescan rather than half-populating from a stale file.
- **Validate on load.** If a persisted `path` no longer exists on disk, keep the
  entry but mark it stale rather than silently seeding a dead path into the
  cache.

> There is a known crash trap here. `42ab78d:PluginScanner.cpp:187-189` carries
> a comment recording that `crude_json`'s `operator[]` **inserts on missing
> keys rather than returning null**, and that a bare `v["path"]` killed the app
> at launch as soon as the index held one AU. The current code guards this with
> `v.contains(key) && v[key].is_string()`. **Preserve that guard pattern for
> every field you touch.**

### 3. Replace the resolution chain in `PluginVST3Create`

In the recovered `PluginVST3.mm`, the resolution block runs roughly lines
946–1030 (from `VST3Trace("resolve begin: ...")` to the
`"VST3 bundle not found on disk"` assignment). Replace it with:

1. **`desc.path`**, if non-empty and `fs::exists`. Cache it and use it.
2. **`GetCachedVST3BundlePath(desc.identifier)`**, now reliably seeded from the
   persisted index at launch (item 2).
3. **A targeted rescan**, only if both miss — and it must be crash-safe (item 4).

**Delete step 3 of the old chain — the `<root>/<desc.name>.vst3` probe —
entirely.** Matching a factory class name against a bundle filename is the
false-hit source and it buys nothing once the index is authoritative.

Keep `VST3Trace` and the `INFINITE_VST3TRACE` env gate. It is the only
diagnostic surface this path has and it is what made this diagnosis possible.

**Improve the terminal error message.** `"VST3 bundle not found on disk"` gives
the user nothing. It should name the identifier, say whether the index had an
entry, and say whether the path existed — e.g.
`VST3 not resolvable: <identifier> (indexed at <path>, which no longer exists —
rescan plugins)`.

### 4. Make the scan crash-safe with a sentinel + blocklist

A single bad `bundleExit` must not be able to destroy the whole scan.

**Recommended approach (in-process sentinel).** This is what shipping DAWs do,
it needs no new build target, and it reuses scaffolding that is already in the
tree:

- Before probing each bundle, write its path to a sentinel file (e.g.
  `~/Library/Application Support/Infinite/PluginScanSentinel.txt`) and flush.
- Clear the sentinel after the bundle probes successfully.
- On scanner startup, if the sentinel is non-empty, the app died probing that
  bundle last time → append it to a persisted blocklist and skip it from then
  on.
- Surface blocklisted bundles in the Plugins panel with a way to clear the
  blocklist and retry.

**There is already a scaffold for the reporting half.**
`PluginScanner::FailedBundles()` exists at `src/audio/PluginScanner.h:48`, with
`mFailed` at `:65` and `mPendingFailed` at `:70`, and `ScanThreadMain` already
declares a `failed` vector and moves it into `mPendingFailed`
(`PluginScanner.cpp:78-91`). **It is currently never populated and never
displayed anywhere** — grep confirms zero references to `FailedBundles` in
`src/main.cpp`. Populate it and render it.

Also: the 8 Intel-only bundles listed above should be reported as a clean
per-plugin "unsupported architecture" failure, not as a crash.

**Alternative considered and not recommended:** an out-of-process scan helper is
more robust but adds a second build target and IPC. Only reach for it if the
sentinel approach proves insufficient in testing.

### 5. Restore the VST3 folder plumbing in the scanner

The removal stripped folder handling but left dead stubs:

- `PluginScanner::StartScan(const std::string& folder)` at
  `src/audio/PluginScanner.cpp:65` still takes the parameter and immediately
  does `(void)folder;`. Restore real per-folder scanning.
- `ScanThreadMain()` at `:78` takes no folder list. Restore the
  `ScanThreadMain(std::vector<std::string> vst3Folders)` signature from
  `42ab78d` and the standard-path defaults (`/Library/Audio/Plug-Ins/VST3` and
  `$HOME/Library/Audio/Plug-Ins/VST3`, plus `mFolders`).
- `AddFolder`/`RemoveFolder`/`Folders()`/`SaveFoldersToDisk` all still exist and
  work, but **nothing in the Plugins panel calls them**. `AddFolder` is only
  reached from `DrawLibrarySearchPanel` (`src/main.cpp:8775`), which serves
  Samples/Media via `SampleScanner` — a different class.
- Call `Platform::SetVST3SearchFolders(...)` whenever the folder list changes,
  so on-demand resolution searches user folders too.

The bundle walk in `EnumerateVST3Plugins` already recurses into non-`.vst3`
subdirectories, which it must — `/Library/Audio/Plug-Ins/VST3/Antares` is a
plain directory containing nested bundles. Keep that behavior.

### 6. Wire VST3 into the two spawn paths in `main.cpp`

Both plugin entry points are currently AU-only.

**Finder drop.** At `src/main.cpp:26456`:
```cpp
static const std::vector<std::string> kPluginBundleExt = { "component" };
```
Add `"vst3"`. Then at `:26499-26530`, the branch calls
`Platform::DescribeAudioUnitBundle` unconditionally (`:26502`). Dispatch on the
extension: `.component` → `DescribeAudioUnitBundle`, `.vst3` →
`DescribeVST3Bundle`. Keep the existing "prefer what the scanner already knows"
logic at `:26513-26520` — including its `desc.path` preservation, which matters
more for VST3 than for AU.

**Delete the rejection at `src/main.cpp:26446-26448`**, which currently prints
`"VST3 plugins are not supported; please use Audio Unit (.component) plugins"`.
When built with VST3 off, this path should say the build has VST3 disabled —
not that VST3 is unsupported.

**Library-panel drag.** `DrawPluginSearchPanel` at `src/main.cpp:9055` and the
drop resolver at `:33598-33619` are already format-agnostic — they pass a whole
`Platform::PluginDesc` (`gPluginDragDesc`, `:9123`) straight to
`LoadPlugin(...)`. **These need no change**, provided the scanner puts VST3
entries into `Index()` with their `path` populated. Verify rather than edit.

Add to `DrawPluginSearchPanel`: a format badge per row (AU vs VST3), folder
management for VST3 search paths, the failed/blocklisted list from item 4, and
— when `INFINITE_ENABLE_VST3` is not defined — a line stating VST3 support is
not compiled into this build.

**Patch load.** `src/main.cpp:3419-3423` calls
`AudioPluginNode::ReloadFromIdentity()` after `VisitParams` restores identity.
This resolves through the same chain, so item 3 fixes it. Confirm a saved patch
containing a VST3 node reloads after an app restart — that is the exact path
that was failing.

### 7. Fix the documentation, which currently lies

`README.md` and `ARCHITECTURE.md` already advertise VST3 as a shipping feature
and have done so since the removal:

- `README.md:3` — "with AU and VST3 plugin hosting"
- `README.md:29` — "hosts third-party **Audio Unit [AU]** and **VST3** plugins
  with native GUI windows and mapped modulatable params"
- `README.md:57-58` — a whole "Audio Unit (AU) & VST3 Plugin Hosting" section
  claiming drag-and-drop of VST3
- `README.md:144, 149` — `platform/` and `AudioPluginNode` described as
  AU/VST3
- `ARCHITECTURE.md:117` — "Both AU and VST3"

Once this lands, make these accurate — including the fact that VST3 is a
build-time option that is **off by default** and carries a GPLv3 obligation.
Do not leave them claiming an unconditional feature.

---

## Out of scope

- **Do not change the patch format.** `pluginFormat` already round-trips.
- **Do not touch the AU code path.** It works and is not implicated.
- **Do not flip `INFINITE_ENABLE_VST3` to `ON` by default** without asking the
  user — it is a licensing decision, not a technical one.
- **Do not rewrite the DSP, editor-window, or parameter-mapping half of
  `PluginVST3.mm`.** Restore it as-is; only the resolution block changes.
- The `src/core/Osc*` / `RemoteControl` files currently untracked in the working
  tree are unrelated in-flight work. Leave them alone.

---

## Verification

This is a compiled macOS app; reading the source is not a smoke test.

1. Build with VST3 **off** (the default) and confirm it compiles clean and the
   Plugins panel still lists AUs:
   ```bash
   cmake --build build -j"$(sysctl -n hw.ncpu)"
   ```
2. Configure and build with VST3 **on**:
   ```bash
   cmake -S . -B build -DINFINITE_ENABLE_VST3=ON && cmake --build build -j"$(sysctl -n hw.ncpu)"
   ```
3. Run the audio node sweeps, since this touches an audio node's load path:
   ```bash
   .claude/skills/audio-node-sweep/driver.sh
   ```
4. Run the full pre-commit hygiene pass:
   ```bash
   .claude/skills/run-infinite-hygiene/driver.sh
   ```
5. **Manual checks that specifically target the reported bug** — do all four:
   - Rescan with VST3 on. Confirm the scan **completes** across all 176 bundles
     and that `PluginIndex.json` contains `vst3` entries **with `path` set**.
   - Drag a VST3 from the Plugins panel onto the canvas. It must load, not
     report "bundle not found."
   - **Quit and relaunch, then drag the same plugin again.** This is the case
     that was failing — it exercises the `LoadFromDisk` re-seed from item 2.
   - Save a patch with a VST3 node, quit, relaunch, reopen the patch. It must
     reload via `ReloadFromIdentity`.
   - Confirm the 8 Intel-only bundles produce a clean per-plugin error and do
     not abort the scan.
6. Use `INFINITE_VST3TRACE=1` when anything fails — that env gate is the
   diagnostic surface for this whole path.

Per this repo's convention, copy the built `Infinite.app` to `~/Desktop` after
a successful build.
