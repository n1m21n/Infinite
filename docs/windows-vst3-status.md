# Windows VST3 Hosting — Status

Branch: `feature/windows-vst3-hosting` (identical tree to `bugfix/param-bound-honesty`;
the latter is a misnamed duplicate of the same tip — use the feature branch).

## Where it stands

**Phase 1 — Foundation. Done, CI-confirmed compiling/linking (Windows x64 + ARM64).**
`src/platform/win/PluginVST3Win.cpp` hosts the SDK's
`IComponent`/`IEditController`/`IAudioProcessor` directly: host glue classes,
create/init/process/parameter/state logic, DLL loading, UTF-16 handling.
`INFINITE_ENABLE_VST3` defaults `ON` (`CMakeLists.txt`; GPLv3 licensing
consequence accepted, see `LICENSE`). SEH crash guarding
(`RunPluginCallGuarded`) covers state save/restore.

**Phase 2 — Editor window (HWND) hosting. Done, CI-confirmed compiling/linking.**
Real top-level Win32 window embedding the plugin's `IPlugView` via
`kPlatformTypeHWND`. `HostPlugFrame` implements `IPlugFrame::resizeView`.
Soft-close semantics (`WM_CLOSE` hides rather than destroys, matching Mac's
`windowWillClose:`). `canResize()` respected for fixed vs. resizable window
style. SEH guarding extended to `createView`/`getSize`/`setFrame`/`attached`/
`onSize`, tracked by an `editorUnstable` flag independent from
`stateCallsUnstable`. Wired into `PluginHostWin.cpp`'s
`PluginOpenEditor`/`CloseEditor`/`EditorIsOpen`/`AnyPluginEditorOpen`/
`PumpPluginEditorEvents` dispatch.

**Phase 3 — Out-of-process scanning + loader hardening. Done, CI-confirmed
compiling/linking (Windows x64 + ARM64) — not yet runtime-verified on
Windows.**
All eleven gaps below are addressed in code. Landed in two pushes: the
initial implementation, then a fix-up commit for two build errors CI caught
(a forward-reference to `LoadVST3Module`/`UnloadVST3Module` that only the
scanner target's unbatched per-TU compile surfaced, and a `LoadCursorW`
ANSI/wide mismatch from `IDC_ARROW` under a non-`UNICODE` build). See "What
CI proves vs. what doesn't" at the bottom before relying on any of this at
runtime.

No local MSVC in this environment — verification is compile-only via GitHub
Actions CI (`.github/workflows/build.yml`, Windows x64 + ARM64 jobs). Never
claim runtime behavior (a window appearing, a plugin loading, a scan finding
something) beyond what CI proves.

## The eleven gaps — final status

1. **ARM64 wrong bundle subfolder — fixed.** `LoadVST3Module` in
   `PluginVST3Win.cpp` now selects `arm64-win` / `arm64ec-win` / `x86_64-win`
   at compile time from the build target (`_M_ARM64EC`/`_M_ARM64`), with a
   fallback that scans the expected arch folder for any `.vst3` DLL, then
   falls back further to scanning every `Contents\*-win\` folder.

2. **Single-file `.vst3` DLLs — fixed.** `EnumerateVST3Plugins`'s folder walk
   now collects both a directory named `*.vst3` (not recursed into further)
   and a plain file named `*.vst3`.

3. **No altered DLL search path — fixed.** `LoadVST3Module` now calls
   `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)` with an absolute path
   (required by that flag), so a plugin's sibling DLLs resolve.

4. **No OLE init on the editor/UI thread — fixed, with one deliberate
   deviation.** `EnsureOleInitializedOnThisThreadOnce()` calls
   `OleInitialize` once per thread, called from `PluginVST3OpenEditor`. There
   is **no matching `OleUninitialize`** — this codebase has no app-lifetime
   shutdown hook anywhere in the Windows platform layer to pair one with
   (verified by grepping `src/platform/win/*` for existing shutdown pairs;
   only Media Foundation's own `MFShutdown` calls exist, unrelated). OLE is
   left initialized for the process lifetime and released by the OS at exit,
   matching the same pattern already used for this file's
   `RegisterEditorWindowClassOnce()`. This is a judgment call, not an
   oversight — flagging it explicitly since the original plan asked for a
   matching `OleUninitialize`.

5. **Text-mode stdout would corrupt the scan wire format — fixed.**
   `src/scanner_main_win.cpp` (new file, the Windows counterpart to
   `src/scanner_main.mm`) calls `_setmode(_fileno(stdout), _O_BINARY)` before
   printing. `main.cpp`'s `--vst3-scan-bundle` fallback child mode gets the
   same fix. `ParseProbeOutput` in `PluginVST3Win.cpp` also strips a trailing
   `\r` defensively.

6. **`infinite-vst3-scanner.exe` needed console subsystem — fixed.** The new
   CMake target has no `WIN32_EXECUTABLE` property, so it links as a console
   binary via plain `main()`.

7. **No Windows scanner target — fixed.** `CMakeLists.txt`'s `if(WIN32)`
   branch now has an `add_executable(infinite-vst3-scanner ...)` target
   (sources: `scanner_main_win.cpp`, `PluginVST3Win.cpp`, `PlatformWin.cpp`,
   `crude_json.cpp`, VST3 SDK iid sources), a post-build copy next to
   `Infinite.exe`, and `.github/workflows/build.yml` stages and CRT-checks it
   alongside `Infinite.exe` in the uploaded artifact. `package.ps1` (the local
   dev packaging script) copies it too.

8. **Stale comment: flag default — fixed.** `PluginHostWin.cpp`'s header
   comment now describes the real dispatch (out-of-process scanning,
   sentinel/blocklist) instead of the old "flag off is the default" claim.

9. **Stale comment: rescan gap — fixed**, alongside gap 11's real
   implementation (comment and code replaced together).

10. **Blocklist/search-folder accessors were unconditional stubs — fixed.**
    `EnumerateVST3Plugins`, `DescribeVST3Bundle`, `CacheVST3BundlePath`,
    `SetVST3SearchFolders`, `VST3Blocklist`, `ClearVST3Blocklist`, and
    `VST3ScanFailures` are now defined directly in the `Platform` namespace
    by `PluginVST3Win.cpp` when `INFINITE_ENABLE_VST3` is on;
    `PluginHostWin.cpp` keeps its stub definitions guarded behind
    `#if !INFINITE_ENABLE_VST3` so the two never collide at link time.

11. **No blocklist/sentinel persistence — fixed.** Ported from
    `PluginVST3.mm`'s `LoadBlocklistLocked`/`SaveBlocklistLocked`/
    `CheckSentinelForCrashLocked`/`EnsureSentinelCheckedOnce`/
    `IsBlocklistedPath`/`WriteSentinel`/`ClearSentinel`/`RecordScanFailure`/
    `AddToBlocklistLocked`, storing `PluginScanSentinel.txt` and
    `PluginVST3Blocklist.json` under `%APPDATA%\Infinite` (`crude_json` for
    the blocklist, `std::call_once` in place of `dispatch_once`, `_commit`
    in place of `fsync` for the durable sentinel write). The step-3 targeted
    rescan in `PluginVST3Win.cpp`'s resolve path is restored, mirroring
    `PluginVST3.mm` but with Windows default folders
    (`%COMMONPROGRAMFILES%\VST3`, `%LOCALAPPDATA%\Programs\Common\VST3`) plus
    `GetExtraVST3SearchFolders()`.

`ProbeVST3BundleOutOfProcess`/`ProbeVST3BundlesBatch` are also implemented
(`CreatePipe`+`CreateProcessW`+`STARTF_USESTDHANDLES`, stderr to `NUL`,
non-blocking drain via `PeekNamedPipe`, 10s single/60s batch deadlines,
`TerminateProcess` on timeout, `GetExitCodeProcess` mapped onto
`ProbeOutcome`), with the read handle marked non-inherited and the write
handle closed in the parent after spawn.

## What CI proves vs. what doesn't

CI (`.github/workflows/build.yml`) proves: `Infinite.exe` and
`infinite-vst3-scanner.exe` both compile and link on Windows x64 + ARM64,
neither imports the VC++ redistributable CRT, and the existing headless
self-tests still pass on x64 (ARM64 is compile-only, per the workflow's
existing comment — an x64 runner cannot execute an ARM64 binary).

CI does **not** prove, and none of the following should be claimed as
working until someone runs it on real Windows hardware:
- That `EnumerateVST3Plugins`'s folder walk actually finds real bundles.
- That `ProbeVST3BundleOutOfProcess`/batch actually drains a real child's
  pipe correctly, honors the timeout, or that `TerminateProcess` cleans up
  as expected.
- That the sentinel/blocklist files are actually written/read correctly
  under `%APPDATA%\Infinite`, or that a real plugin crash gets caught and
  blocklisted on the next launch.
- That `LOAD_WITH_ALTERED_SEARCH_PATH` actually resolves a real plugin's
  sibling DLLs.
- That `OleInitialize` actually fixes any real drag-drop/file-dialog
  behavior inside a plugin editor.
- That the ARM64 arch-folder selection actually finds the right module on a
  real ARM64 machine.

All of the above: **compiles and links, not runtime-verified.**

## Explicitly not a phase

**Tier 3 SEH guarding** — realtime `process()` calls and plugin instantiation
are not guardable via `__try`/`__except` the way state and editor calls are
(`/EHa` does not make a plugin's own corrupted stack recoverable, and
guarding the audio-thread call would add a filter to the hot path). The
identical limitation exists on Mac's `sigsetjmp` guard. Only genuine
out-of-process *hosting* — not just out-of-process scanning — fixes this,
and that is a far larger architectural change than anything scoped here.
Keep flagging it; do not implement it.
