# Infinite as a VST3 + AU plugin: ship plan

Status: chunk 1 started on branch `plugin/c1-engine-shell` (2026-11-07). Done: 1.0 spike (macOS, minus DAW runs), 1.1 licence, 1.2 `Ipc::` layer (mac proven by `ipc-check`; Linux/Windows compile in CI), 1.3 shm protocol `PluginIpc` (mac proven by `ipc_bench --check`: offline exact + bit-identical, MIDI/params/transport, 5 MB messages, stall resync with no stale audio, hang detect, kill, quit), 1.4 engine plugin mode (`--plugin-engine`, host audio backend, `Transport::FollowHost`, host MIDI on a 32-sample grid, Audio In host sources, Plugin node + Arrange render refused inside the plugin; mac proven by `plugin_engine_check`: FX pass-through exact, note on its grid point, one jump on relocate, repeat render bit-identical), 1.5 VST3 shell (`src/plugin/shell`, own CMake project; `Infinite Instrument.vst3` + `Infinite FX.vst3` from one source; buses, bypass, latency = max block, infinite tail, 32 slots + 130 hidden MIDI-mapping params, offline wait for the engine's ready; engine starts on a build-specific starting patch in `assets/plugin/`; mac proven by Steinberg validator 47/0 on both and `plugin_shell_check` 15/15: scan spawns nothing, FX offline exact with random block sizes, two instances bit-identical, note plays, bypass silences, realtime exact, crash drill, no engine left), 1.6 AU (`Infinite Instrument.component` `aumu InIn Infn`, `Infinite FX.component` `aumf InFx Infn`, built from the VST3 by the SDK's `auwrapper` on AudioUnitSDK 1.2.0 fetched at configure; mac proven by `auval -strict` PASS on both and `au_check` in process and out of process (AUHostingService): FX offline exact, FX realtime exact, note plays and releases, no engine left. Found on the way: the App Sandbox denies `kill(pid, 0)`, so `Ipc::ChildAlive` now counts EPERM as alive and `ChildKill` never blocks on a child it cannot signal; auwrapper resets `setupProcessing` to realtime on AU Initialize, so the shell also reads the per-block `processMode`; auval's one warning is Tail Time, which auwrapper does not report), 1.7 validation (`tools/plugin/validate.sh`: Steinberg validator + pluginval + auval with temporary install, one table; `plugin_shell_check --metrics` runs R1/R2 through the real shell and engine via the dev-only `$INFINITE_PLUGIN_PATCH`: M-4 bit-identical, M-5 null exact at 32/64/512/2048, M-6 0 samples; `tools/plugin/reaper_rig.py` builds and renders the same in Reaper offline, again, online and reopened (M-4, M-5, M-6 via delay compensation, M-11), written but not yet run: an unlicensed Reaper's evaluation window blocks unattended runs). S1 still open: Reaper licence (owner purchase, $60) to run the rig, and R1/R2 heard in Reaper and Logic. 2.1 state (blob = `INFS` + header JSON `{format, v, appVersion, build, latencyMode, bypass, slotValues[32]}` + the patch's .inf text, zlib by miniz; getState asks the engine over the control channel with a 2 s wait, else returns the last known patch; setState before the engine runs is kept and handed over at spawn with `--state`; setState while running swaps the patch; a block-size restart carries the patch; no licence data in state; scan still spawns nothing; mac proven by `plugin_shell_check` 9 new state checks, all 23 PASS; also validate.sh, `--metrics` and `au_check` in/out of process re-run green), 2.2 host slots (32 slots + bypass; a Macro's "Host slot" field, none or 1-32, shown in plugin mode only, XY binds two; a moving slot is a one-pole 10 ms ramp per 32-sample sub-block, Toggle/Trigger/Radio/StepGate stepped; a bound slot takes the Macro's name (`kParamTitlesChanged`); turning a bound Macro in Infinite sends begin/perform/end to the host; state header gains `slotsSet` so only slots the host has set are pushed back to the engine, and a new binding with no host value sends its own value to the host (sync) instead of resetting the Macro to 0; offline bounces with a moving bound slot park the engine on each 800-sample grid point so two bounces are bit-identical. CC device bindings in plugin mode (deferred from 1.4): host MIDI now arrives as device `Host` (id 1, id 0 means unbound) and the engine starts MIDI at spawn, so a MIDI CC node learned on hardware reattaches to the host by name. Found on the way: the UI-to-controller relay ran a main-queue block after the host unloaded the bundle, crashing auval (exit 139); it is now a CFRunLoop source invalidated on teardown (mac) and a module-owned window class (Windows). Mac proven by `plugin_shell_check` 7 slot checks + 1 CC check, all PASS (fresh instance keeps the patch value, automation drives the Macro, two parked offline bounces bit-identical with 127 parks and 0 timeouts, offline vs realtime null -42.5 dBFS, saved slot comes back, title + value restarts, begin/perform/end at 0.25 = -45 dB; host CC7 lifts the CC-bound gain from -71.9 to -25.3 dBFS); validate.sh, `--metrics` and `au_check` re-run green. Known limits: the slot path is modulation-rate, so its realtime null is -42.5 dBFS, not exact; offline bounces with a moving bound slot run at about frame rate; the Linux relay calls the controller on the reader thread; edits back to the host through auwrapper are not yet checked in an AU host (2.8); rebinding a hardware CC to Host marks the patch dirty, and the app rebinds a `Host` device back to hardware by name). 2.3 pixel pipe (engine reads the GL framebuffer back with two PBOs: issued at the end of frame N, mapped at the start of N+1, flipped to top-down BGRA into a lock-free shared-memory triple buffer `infF`, `src/plugin/ipc/PluginFrames.h`; the region's name reaches the view through processor -> controller messages; mac view `ShellViewMac.mm` is a layer-backed NSView that copies each new frame into one of two IOSurfaces and sets it as layer contents on a 120 Hz timer; mouse, scroll, keys, text, focus and cursor go back as `InputEvent`s over the control channel; resize and backing scale go to the engine's hidden window, so ImGui lays out in points as in the app; opening a view starts the engine if none runs; a closed view costs nothing (no readback, region and PBOs freed); with no frames the view draws "Starting" or "Engine stopped" + Restart natively; view chrome approved in Infinite's own top bar, 12.4). Mac proven by `plugin_shell_check` 7 view checks, all 31 PASS (frames at 3200x2000 for 1600x1000 @2x within 0.7-2.1 s, 20/20 keys reach Infinite, resize to 1280x800, close stops readback, reopen works; M-10 input to pixels p95 33-43 ms, pixel pipe 8.8-9.8 % of one core at 30 fps: B-6 readback 1.5-1.9 ms + view blit 1.2-1.5 ms per frame) and `au_check` in process now opens auwrapper's Cocoa UI and sees engine frames (2560x1600, 0.4 s); validate.sh, `--metrics` and `au_check` in/out re-run green. Known limits: the engine draws into a hidden window, not an FBO, so its backing scale follows the engine's screen; M-10 CPU is close to its 10 % line, and the planned upgrade (9.3) is sharing IOSurfaces from the engine to drop one copy; no IME (text comes from keyDown characters); Cmd+S/Q/W/H/M go to the host, other key equivalents to Infinite; the top-bar chrome items (switch, Slots, xruns, pop-out) are built across 2.3-2.5; one CC check run in four failed under load and passed on rerun; Windows and Linux views come in chunk 3 (createView returns none there). 2.4 pop-out, projectors, no graphics (pop-out shows the engine's own window, so there is one UI: a "Pop out" tile in the top bar, plugin mode only, new glyph `PopOut`; popped out, readback and view input stop and the view says "In its own window" + "Bring back"; the window's close button and File > Quit bring it back instead of quitting; a new or dead engine resets it. Projector windows open visible in plugin mode (the engine is otherwise an accessory process with no windows). An engine that cannot get an OpenGL 3.3 window (glfwInit, window or loader failure) sends `kMsgNoGraphics` and quits instead of showing a dialog no one sees; the view says "No graphics on this machine" with no Restart; `INFINITE_PLUGIN_NO_GL` forces the path for checks). Mac proven by `plugin_shell_check` 3 new view checks, all 42 PASS (popped out: engine window 0 -> 1 on screen, view label + Bring back; Bring back: frames return at 2560x1600 and the window leaves the screen about 240 ms later; forced no-GL: label shown, no button, engine gone); validate.sh, `--metrics` and `au_check` in/out re-run green (`--metrics` M-5 R1 block 32 realtime missed once under load at -58 dBFS and passed on rerun, as tracked in M-5; the CC check failed once more under load and passed on rerun). Known limits: no graphics means no sound too; an engine that plays audio without GL comes with chunk 3, where Linux servers and VMs make it likely; projector windows are checked by code, not by the check tool; the picture switch, Slots and xruns top-bar items came in 2.5. 2.5 failure modes (heartbeat watchdog in the shell's reader thread: once the engine's audio-thread beat has moved, 3 s with no change kills the engine (SIGKILL / TerminateProcess), the plugin plays silence and the view shows "Engine stopped" + Restart; crash restart opens the patch the engine last pushed, since it now sends its patch every 2 s when it changed (`kMsgStateAuto`, id 11), so Restart brings back what was on screen, not the project's last save; a host suspend (`setActive(false)` or `setProcessing(false)`) flags the next block `kRelease`, and the engine lets go of every host note it holds plus the sustain pedal on all 16 channels; message ids are now one list per direction, after a collision of the auto push with `kMsgFrames` broke the view). 12.4 top-bar chrome finished: Picture tile (the view shows the first Output letterboxed under the top bar; the choice is saved in the state header as `picture`), Slots tile (popup of bound host slots), "N xruns" only when the host has played late blocks as silence. `tools/plugin/crash_drill.py`: `--self` runs the drill without a DAW, `kill` / `hang` drill an engine inside a running DAW (`--host` picks one) and check the DAW survives. Mac proven by `plugin_shell_check` 5 new drill checks, all 47 PASS (crash: patch 2535 bytes before the kill and after Restart, note plays; hang: engine gone after 3.0 s, silent last second; Restart after a hang keeps the patch; suspend: held note rms 0.166 -> 0; picture round-trips); validate.sh, `--metrics` and `au_check` in/out re-run green. The pop-out hide check now skips its window-list part when the screen is locked, as the window server stops updating that list. Known limits: the heartbeat comes from the audio thread, so a hang of only the engine's main thread (UI frozen, audio fine) is not killed; a sandboxed out-of-process AU engine may refuse the kill, and is then marked dead and exits when its host quits; crash_drill.py does not cover Windows yet. 2.6 licence (Monocypher 4.0.2 vendored in `external/monocypher`; `src/core/License.{h,cpp}` shared by shell and engine: Ed25519 over `INF1.<payload>`, `v == 1`, major, cached refund list; key text pulled out of pasted mail text or a dropped file by `License::Extract`, which skips line breaks and quote marks inside the key; stored atomically in the per-user folder, so a key entered in one instance starts every other within about a second. Unlicensed: the shell spawns no engine, Instrument outputs silence with silence flags, FX passes its input dry delayed by exactly the reported latency, getState returns what setState got; the view shows only the licence sheet: message, key field (Return), Paste key, Buy, Weekly refund check, and drop of a key file or text. Refund check: the engine fetches the list on a background thread at most once a week, fail open, and a key on the list stops the engine at once: plugin unlicensed with "This key was refunded". Mac proven by `plugin_shell_check` 10 new licence checks, all 57 PASS (extract, signature/major/version/revoked units, no key: Instrument silent and no engine, FX dry by exactly L, state kept; sheet shown, bad and older-major keys refused with their text, a valid key from the sheet stored and engine started, a key entered in another instance starts this one, a refunded key stops the engine and FX plays dry); every check mode now runs licensed with a key it signs itself (`INFINITE_LICENSE_TEST_KEY` build, `--write-license <dir>` for the AU out-of-process run); validate.sh (unlicensed: auval passes on the dry/silent path), `--metrics`, `crash_drill.py --self` and `au_check` in/out re-run green. Known limits: Windows and Linux have no sheet until their views come in chunk 3 (the key file can be copied into the folder by hand); a refund takes effect at the next weekly check, not at once; the licence line and refund toggle are on the sheet only, not yet in the engine's Settings; a packaging build must configure `INFINITE_LICENSE_TEST_KEY` off). 2.7 soak, done (`tools/plugin/soak.py` drives `plugin_shell_check --soak`: one realtime host thread at 48 kHz / 256 plays 2 R1 + 2 R2 after a 20 s idle, reads each shell's xrun counter through a new `stats` message, and samples each engine's physical footprint and system swap; M-10 open time added to `--view`; a mixed Instrument + FX check added to the full run). Fixed on the way: the Instrument and FX bundles both named shared memory `/infp.<pid>.0`, so an FX loaded beside an Instrument got no engine (`Ipc::SharedMemoryUniqueName` now carries a module tag); the R1 engine missed 45 blocks a minute because its realtime audio thread waited on graph workers that are not realtime without a device workgroup, so the plugin engine now runs one audio thread (`INFINITE_AUDIO_THREADS=1`, the DAW spreads instances across cores) and `hardware_concurrency` is cached instead of a sysctl per block; each idle engine held 122 MB of hidden-window drawables, so with no view open the hidden window shrinks to 400x250 (idle footprint 364 -> 186-210 MB). M-9 reads physical footprint, since `ps` RSS reads ~30 MB once the compressor has the pages. Also fixed: every envelope panel (Analog, FM, ...) appended its rect to the Wavetable drag-test list `gWtTestRects` each frame, but only the Wavetable body cleared it, so any patch without a Wavetable grew ~0.9 MB/min, in the app as well as the plugin; it is now cleared once per frame in `DrawNodeBodies` (found by a heap diff of one R1 engine; dev-only `INFINITE_PLUGIN_NO_WATCHDOG` keeps the shell from killing an engine paused by `heap`). Mac proven by the 60-min soak (`tests/plugin/soak.json`, 2 R1 + 2 R2, 48 kHz / 256): M-7 0 missed blocks, 0 host overruns, longest call 162 us; M-8 worst 0.24 % of one core; M-9 idle footprint 185-202 MB (limit 341), footprint -27.7 MB summed while playing, worst growth +5.5 MB, R1 flat at ~201 MB from minute 4 to 60; full check 58/58, `--view` 12/12 (M-10 open cold 0.89 s, warm 0.05 s, p95 32.7 ms, 9.3 % core; frames come back at 3200x2000 after the idle shrink), `--metrics`, validate.sh, `crash_drill.py --self` and `au_check` in/out all green. 2.8 DAW matrix, macOS, headless part done (owner 2026-11-08: no time for hand checklists, so every common check that a host does not add to was made automatic, and the owner runs Ableton once at the end). `tools/plugin/daw_kit.sh` builds the test kit (bundles in the packaged layout with the engine inside, test licence, R patches, checklists `tests/plugin/daw/{logic,ableton,bitwig}.md`, `crash_drill.py`, validator, `au_check`, install / uninstall). `plugin_shell_check` gained C1 (editor open/close x10: frames every time, engine footprint 327 -> 325 MB) and C6 (44.1 -> 48 -> 96 kHz on one instance, re-prepared, note 60 at 260.9 / 262.3 / 261.6 Hz); the headless map of C1-C10 is in 8.8. Run on the second Mac (MacBook Air M-series, macOS 27.2, a clean machine) from the kit: validator 47/0 on both, `auval -strict` PASS on both, `au_check` in and out of process ALL PASS. Real Logic Pro 12.3.1 on the Air: AU scan and load (L1), out-of-process hosting with the engine as a grandchild (L4), engine kill with Logic alive and the engine gone in 0.2 s (C10). Full check 60/60 on this Mac (one run in four had a single failure under load that passed on rerun, as the CC check before). Still open in 2.8: Ableton (owner, kit installed on the Air), Bitwig sandbox modes, and the hosts not on either Mac; pluginval strictness 10 (not installed on either Mac). Owner decisions 2026-11-07: **no plugin trial** (the free app is the way to try Infinite; the plugin needs a key), **every DAW on every OS** is the target, and existing nodes and settings change wherever the plugin needs it. Waiting on owner: DAWs to test in, spending (12.4). Lane 3 (Major: new platform surface, new process model, new file format inside DAW projects).
Chunk 3 (2026-11-08, same branch, since chunk 2 never merged separately): 3.1 Windows view `ShellViewWin.cpp` (child HWND, 8 ms timer, StretchDIBits top-down, native Restart / Bring back / licence sheet with key field, Paste key, Buy, Weekly refund check; VK to GLFW keys, WM_CHAR with surrogate pairs, mouse capture, file drop; content scale from `GetDpiForWindow` or the host's `setContentScaleFactor`) and MMCSS "Pro Audio" for the shell's realtime threads (avrt.dll). 3.2 Linux view `ShellViewX11.cpp` (own Display, `IRunLoop` fd + 8 ms timer from the host frame, XPutImage of the shm frame, core-font overlay and licence sheet, CLIPBOARD paste as UTF8_STRING, xdg-open for Buy, detectable auto-repeat; engine messages arrive on the reader thread on Linux, so the controller holds `viewMutex_` and the view queues every Set* for its timer) and SCHED_FIFO with an rtkit fallback over dlopen'd libdbus. 3.3 CI: Windows x64 and Linux gcc jobs build the test-key plugin and run `validate.sh --strictness 10 --licensed` (validator + pluginval 1.0.4, Linux under xvfb), logs uploaded as `plugin-logs-<os>`, failure gates the job; Windows (both arches) and the AppImage job build the shipping bundles with `build_bundles.sh` (test key forced off) and carry them in the package; `plugin_test_license` writes the check licence on every OS. 3.6: no Linux tarball and no `install.sh` route (Linux ships only as the AppImage, which carries the plugins); the app's copier (Settings > DAW plugin, or `Infinite --install-plugins`) is the one install route on every OS; the exe is resolved through symlinks so Homebrew's `infinite` link finds the bundle; the cask gets `uninstall delete:` + a caveat, not `vst3_plugin` / `audio_unit_plugin` stanzas. 3.7: `website/plugin.html` (four steps, folders per OS, FAQ: DAW does not list it, app need not be open, Gatekeeper, SmartScreen, two computers, updating, lost key, refunds), brand lint 0, linked from the buy page; the buy page's pre-order lines and the index link flip on launch day (12.2). 3.8: `POST /api/license/notify-preorders` (admin token, dry run by default, refuses before `RELEASED = "1"`, 40 per call, `download_emailed_at` makes each key get one mail; revoked and sandbox keys skipped) driven by `tools/license/notify_preorders.mjs`; worker test covers it; `DOWNLOAD_URL` fixed to `/#download` (the site has no `/download` page). 3.9: `tools/plugin/install_check.sh` (direct, update over an old copy, uninstall, symlinked exe; mac 7/7 PASS). Mac re-run 2026-11-08 on a quiet machine: `--view` M-10 p95 36.9 ms, 8.9 % core, open cold 1.66 s / warm 0.04 s; the closed-view readback check missed its 6-frame allowance once under load (14) and passed on rerun. CI results for Windows and Linux: CI_RESULTS_PENDING.
Chunk 3 follow-up (2026-11-08): host slots went from 32 visible "Slot NN" to 128 hidden until bound (owner: show only what the user configured, with the right name, in every DAW). Right-click "Host automation" on any knob or param binds it to the next free slot; the host sees it by the node's Title Case name; names ride in the state header so they survive a reopen before the engine runs (`plugin_shell_check`: 259 params, 0 visible + bypass until bound; S2 patch: host automation drives a plain Gain knob -60 to +12 dB, host sees "Gain"). Plugin-mode hide pass (owner: hide what does not work or does not belong inside a DAW): no audio device/format, app & updates, DAW plugin or Plugins settings; no play/rewind (and the shortcuts do nothing), no audio power, no update badge, Check for updates or Quit; tempo and meter are the host's, read-only ("Set by the DAW"); no Plugin node spawn, Plugins browser segment or plugin-bundle drop (hosting is deferred); no offline Render on Output, Arrange render chip or Render Track, 3D Render Animation; no update check or plugin recopy at start. Mac re-run after both: `plugin_shell_check` 65/65, validate.sh PASS (validator 47/0 both, pluginval 5, `auval -strict` both, FX's one Tail Time warning). Deferred and still hidden in the plugin: hosting other plugins, embedding samples in the DAW project (samples load by absolute path). Logic's handling of the non-realtime unbound AU params and Ableton's count of hidden params are for the owner's Ableton run.

Read first: `docs/DECISIONS.md`, `infinite-lifecycle`. Skills that own the detail: `build-discipline`, `ci-preflight`, `windows-parity`, `linux-parity`, `tool-first`, `ship-infinite`, `plugin-host-hardening`, `audio-pipeline-sweep`, `av-sync-sweep`, `release-notes-audit`.
Every decision below is settled. Items that need the owner's approval under the "new features need approval" rule are listed once, in 12.4, each with the recommended answer.

Note on location: private plans moved to `internal/plans/` in `458a1dd3`. This file sits where the owner asked (`docs/plans/vst-au/`); it holds no secrets, prices only what the public buy page already shows.

---

## 1. Goal

| | |
|---|---|
| What ships | **Infinite Instrument** (MIDI in, audio out) and **Infinite FX** (audio in, audio out), each as VST3 (macOS, Windows, Linux) and AU (macOS) |
| Who it is for | The pre-order buyers on Paddle ($99, $79 with LAUNCH79) and every DAW user after them |
| What "done" means | A pre-order buyer downloads, installs without a developer account workaround they cannot follow, drops in `license.inflic`, and plays an Infinite patch inside Logic, Ableton, Reaper, FL or Bitwig; the bounce sounds like the playback; the project reopens with the patch |
| Pillar / release goal | Income (owner order income > reach > art > learning, 2026-10-11). Pre-order money is already taken: the release goal of this work is "deliver what was sold" |

## 2. Problem

- Infinite is a standalone app (GLFW window, OpenGL, miniaudio device, its own transport). A DAW wants a plugin: the host owns audio, MIDI, time, the window and the project file.
- The app was never built to be instantiated twice in one process (see 4.1: ~500 app-wide globals, singleton `Transport`).
- Modulation is stepped once per UI frame on the main thread, so an offline bounce (no UI frames, faster than real time) would not match live playback unless the plugin drives that clock from audio time.
- No paid Apple Developer account: no notarization. Unnotarized plugins downloaded through a browser are blocked by Gatekeeper on first load.
- Money is taken before the product exists: the refund window (14 days after the download email) starts on ship day, so the first impression must be right.

## 3. Scope

### 3.1 Include in v1

| Item | Reason |
|---|---|
| VST3 on macOS, Windows, Linux | One SDK (bundled, MIT since 3.8), covers every VST3 host on every OS (8.8) |
| AU (v2) on macOS | Logic and GarageBand load AU only. Built from the same VST3 code through the SDK's `auwrapper` |
| Two builds: Instrument and FX | DAWs place instruments and effects in different slots; one binary pretending to be both breaks Logic and FL |
| Audio in/out, MIDI in (notes, CC, pitch bend, channel and poly aftertouch) | The core of "Infinite in a DAW" |
| Host transport and tempo follow | A plugin that ignores the host clock is useless in an arrangement |
| 128 automatable host slots, shown only once bound | Automation is the second thing every DAW user tries |
| Full patch saved in the DAW project | Reopening a project must bring the patch back |
| Every DAW that loads VST3 or AU, on every OS it runs on | Owner goal 2026-11-07: a working plugin in all hosts, not a short certified list (8.8) |
| Changes to existing nodes and settings | Owner 2026-11-07: change nodes, settings and menus wherever the plugin needs it (Audio In host sources, Macro host slot, Settings > DAW plugin, plugin-mode menus) |
| Live visual window inside the plugin, resizable, plus pop-out and projector outputs | Infinite is an audiovisual instrument (DECISIONS 2026-10-14: design and live, balanced) |
| Many instances | Every DAW user will put two on two tracks |
| Offline-safe audio: bounce, freeze, export = live | Users bounce; a different bounce is a bug report on day one |

### 3.2 Exclude from v1

| Item | Later or never | Reason |
|---|---|---|
| AAX / Pro Tools | Chunk 4, after v1 ships | The one major DAW VST3/AU cannot reach. Needs the owner to sign the Avid developer agreement and a PACE (iLok) signing account; the shell is format-agnostic, so AAX is a new wrapper over the same IPC client |
| CLAP | Chunk 4, after v1 ships | Every CLAP host also loads VST3, so it adds no DAW; easy follow-up once the shell exists |
| AUv3 / iOS | Never (for now) | No paid Apple account (memory `no-apple-developer-account`); AUv3 needs an app extension in a signed container app |
| Loading other plugins inside Infinite | Later | In-plugin hosting of plugins doubles the crash surface (`plugin-host-hardening` tier 3). The Audio Plugin node shows "not available in the plugin" and passes silence |
| Video recording from inside the DAW | Later | Export/Render Take menus are hidden in plugin mode. Users who need a film open the patch in the free app |
| MIDI out to the host | Later | Audit: `MidiOutNode` exists and drives hardware ports and the virtual port. It keeps working in the plugin (the engine is the app), but sending MIDI back to the DAW's track needs a VST3 event output bus and AU MIDI output callbacks: later |
| Account or online activation | Never | The source is MIT; activation would only punish honest buyers (see 8.5) |
| Brand-new node types | Not needed | Plugin mode reuses and extends Audio In, Audio Out, MIDI Notes, MIDI CC, MIDI Trigger and the Macro nodes; if a host need turns up that no existing node can carry, it is shown to the owner first |
| Multi-out (more than one stereo out) | Later | Most users never route it; it multiplies bus-arrangement bugs in FL and Ableton |
| Surround buses (5.1 and up) | Never in v1 | Infinite's audio graph is stereo |

## 4. Existing constraints (the repo audit)

### 4.1 What exists now

| Area | What is there | Where | Use for the plugin |
|---|---|---|---|
| VST3 **hosting** (Infinite loads plugins) | Full macOS host, Windows host (HWND editor, out-of-process scanner), Linux host | `src/platform/PluginVST3.mm` (3378 lines), `src/platform/win/PluginVST3Win.cpp`, `src/platform/linux/PluginHostLinux.cpp`, `src/nodes/AudioPluginNode.cpp`, `src/audio/PluginScanner.cpp` | Not reused as plugin code. Reused as a **test host** (`tools/re-survey/host_audit.py` already loads one plugin per process and runs `auval`) |
| AU **hosting** | AudioUnit / AudioComponent use in the macOS platform layer | `src/platform/Platform.mm` (46 hits) | Same: test host only |
| Crash containment for hosted plugins | `RunPluginCallGuarded`, tiers 1-3 | `src/platform/PluginCrashGuard.cpp`, `plugin-host-hardening` | Its lesson drives 9.1: the only full fix is a separate process |
| Infinite **as** a plugin | **Nothing.** No `IPluginFactory`, no `AudioEffect`, no AU entry point anywhere in `src/` or `cmake/` | grep of `src/`, `cmake/` | All new |
| VST3 SDK | `external/vst3sdk` at **v3.8.1_build_84, MIT licence** (`external/vst3sdk/LICENSE.txt`). Contains `public.sdk/source/vst/auwrapper`, `vstsinglecomponenteffect`, the `testsuite` (validator), `moduleinfo` | submodule | Plugin SDK, AU wrapper and the Steinberg validator, no new dependency |
| Licence statement | `LICENSE` still says a default build links a GPLv3 SDK and so the binary is GPLv3 | `LICENSE`, `CMakeLists.txt:265,380` | **Stale**: the SDK is MIT since 3.8. Chunk 1 fixes the text (fact fix, agent-decidable) |
| Windowing and GPU | GLFW + OpenGL 3 + ImGui (`imgui_impl_glfw`, `imgui_impl_opengl3`); window made in `src/app/Startup.cpp:1108` | `src/main.cpp`, `src/app/Startup.cpp` | A plugin cannot own a GLFW window inside a host's view. See 8.2 (pixel pipe) |
| App-wide state | ~500 `extern`/`inline` globals in `src/app/AppShared.h`; `Transport::Instance()` is a function-static singleton with a `thread_local` `ScopedTransport` override | `src/app/AppShared.h`, `src/core/Transport.cpp:139` | Two instances in one process would share every global. Drives the out-of-process choice (9.1) |
| Clock | `Transport`: tempo, time signature, loop, `SeekBeats`, `AdvanceAudioClock`, `SetOfflineMode`, `BeginOfflineAudioBlock` | `src/core/Transport.h` | Host-follow sets these per block; offline mode already exists for exports |
| Modulation | Evaluated on the main thread once per UI frame (`ApplyModulationAndPalette`), pushed to audio through `ParamMailbox` (latest value wins) | `src/core/Modulation.*`, `src/audio/ParamMailbox.*`, `docs/ARCHITECTURE_FLOWS.md` §2-3 | Must be clocked from audio time in plugin mode (8.1) |
| Offline render | `OfflineGraph` (private patch rendered to a buffer), headless CLI `--render` / `--audio-summary`, deterministic output | `src/app/graph/OfflineGraph.cpp`, `docs/headless/GUIDE.md` | Proves the engine runs with no device and no window; the plugin engine mode builds on it |
| Nodes the plugin needs | `Audio In`, `Audio Out`, `Output`, `MIDI Notes`, `MIDI CC`, `MIDI Trigger`, `MIDI Out`, 9 Macro nodes | `src/app/graph/NodeRegistry.cpp:159-294` | No new nodes needed |
| Note events | `NoteEvent` with `frameOffset` in block, no channel field | `src/audio/NoteEvent.h` | Sample-accurate notes already possible |
| MIDI in | CoreMIDI / WinMM / ALSA seq, `Platform::MidiRead` | `src/platform/` | Host MIDI enters at the same point, hardware MIDI in is off in plugin mode |
| Ring buffers | `AudioCaptureRing`, `MeterRing`, `ClipPeakRing`, `NoteEventQueue`, SPSC retire ring | `src/audio/` | Patterns for the lock-free audio-to-UI feeds |
| Fades | `PassFade.h`, `CompensationDelay.h` | `src/audio/` | Click-free patch swap; PDC |
| Self-update check | `src/core/SelfUpdate.cpp` | | Reused for "update available" in the plugin |
| Packaging | `package.sh` ad-hoc signs (`codesign --sign -`), no notarization; website already explains "Open Anyway" and `xattr -dr com.apple.quarantine`; Windows signing via SignPath Foundation workflow `sign-windows.yml`; AppImage on Linux; Homebrew tap, winget, AUR in `tools/spread/pkg/` | | Base for 8.6 |
| CI | `.github/workflows/build.yml` builds macOS, Windows x64 + ARM64, Linux | | New plugin jobs added there (8.7) |
| Linux rig | `tools/linux/local.sh` (OrbStack container), `tools/linux/xvfb-harness.sh` (Xvfb + llvmpipe) | `linux-parity` | Headless plugin host rig (8.7) |
| Build | `scripts/ibuild.py` (machine lock, adaptive -j, dev/asan/release/linux modes) | `build-discipline` | Every plugin build goes through it |
| Licence backend | Worker `worker/license/` (Paddle webhook, D1, Resend), `PUBLIC_KEY.txt`, `GET /api/license/revoked` (1 h cache), `POST /api/license/resend`, `RELEASED` and `DOWNLOAD_URL` vars | `worker/license/index.js` | Plugin verifies offline; ship day flips `RELEASED` |
| Ship-day mail to pre-order buyers | **No tool exists.** The Worker emails at purchase time only; `emailed_at` is purchase mail | `worker/license/schema.sql` | New tool in chunk 3 (tool-first) |
| App version | v1.1.0 latest (`gh release list`, 2026-11-05); `INFINITE_VERSION "1.1.0"` | `CMakeLists.txt:22` | Plugin version = app version; licence `major 1` covers 1.x |
| DAWs on this Mac | **None installed**; `~/Library/Audio/Plug-Ins/` has `Components`, `VST`, `Digidesign` | | DAW demo versions needed for testing (owner column: downloads, accounts) |
| Machine | M2, 8 GB RAM, macOS 27.2 | `machine-is-m2-8gb` | Instance memory budget and one build at a time |

### 4.2 What a plugin needs that the app does today

| Need | App today | Gap |
|---|---|---|
| Run without its own audio device | Headless render does | Live, callback-driven by an outside clock: new `HostAudioBackend` |
| Follow an outside clock | Follows MIDI clock in (`MidiClockBpm`) | Follow host PPQ, tempo, signature, play, loop per block |
| Draw into someone else's window | Draws into its own GLFW window | Offscreen FBO + pixel pipe + input injection (8.2) |
| Save state into a blob | Saves `.inf` files | Same JSON into the host's state stream |
| Bounce = live | Exports are sample-clocked | Modulation clocked from audio time in plugin mode |
| Exist twice | No | Separate engine process per instance |

### 4.3 Rules that bind this plan

| Rule | Effect here |
|---|---|
| `build-discipline` | Every build via `scripts/ibuild.py`, one at a time; plugin targets added as `--target` names; release (universal) builds only for packaging |
| `ci-preflight`, `ci-failure-triage` | Read previous runs first; batch work into one push per chunk end |
| `windows-parity`, `linux-parity` | Every new `Platform::` function has three sides; Windows proven by CI compile + VM run, Linux by the container + Xvfb rig |
| `tool-first` | Every repeated check is a script under `tools/plugin/` (8.7) |
| `ship-infinite`, `release-notes-audit` | Release only when the owner says go; notes match code |
| No new nodes or modes without approval | Plugin mode is a build of the app, not a new node; user-visible UI additions are listed in 12.4 |
| No debug tools in release | The engine helper is a Release build: UI Debugger, Style Editor and friends are compiled out (`#ifndef NDEBUG`) |
| Secrets | Nothing from `~/.paddle-live*.env` is read, printed or committed by any step; Worker changes use `wrangler secret` already in place |
| Clean room | Never read GPL plugin-framework source (JUCE is GPL/commercial, iPlug2 is fine but not used). Only the MIT VST3 SDK and Apple's Apache-2.0 AudioUnitSDK |
| Scale to user base | Smallest design that ships; no compatibility flags for old patches |

## 5. Hypothesis

> A thin native plugin shell (VST3, wrapped to AU) that runs one Infinite engine **process** per instance, with audio, MIDI and time over shared memory, one block of reported latency, and the GUI sent as pixels, gives a plugin that (a) passes every validator, (b) bounces the same as it plays, (c) survives many instances, and (d) cannot crash the host when the engine crashes, in about 9-11 weeks, without refactoring the app's ~500 globals.

What would prove it wrong (each is a stop condition in 12.1):

- A target host blocks spawning a helper process or shared memory (sandboxed GarageBand is the main suspect).
- The pixel pipe costs more than its budget (7.1) at 1600x1000 @2x.
- Per-instance memory makes four instances impossible on the 8 GB Mac.

## 6. Baseline (recorded once, in chunk 1 step 1.0; never re-measured before the final build)

| # | Measure | Known now | Recorded in 1.0 with |
|---|---|---|---|
| B-1 | App bundle size | 63 MB (`build/Infinite.app`, universal) | `du -sh` |
| B-2 | Engine idle RSS, default patch, no window drawn | not measured | `tools/plugin/baseline.py` (`ps -o rss`) |
| B-3 | Engine cold start to first audio block | not measured | same tool, wall clock |
| B-4 | Engine CPU on reference patch R1 (synth + 2 FX + 1 visual) at 48 kHz / 256 | not measured | headless `--audio-summary` timing + live run |
| B-5 | Shared-memory round trip (post, wake, reply) for one block, p50 / p99 | not measured | `tools/plugin/ipc_bench` |
| B-6 | GL readback of a 3200x2000 RGBA frame with PBO, ms | not measured | `tools/plugin/readback_bench` |
| B-7 | Headless render determinism (same patch twice) | Identical, one known exception: Field Notes + modulation, ~0.1 dB (`docs/headless/GUIDE.md`) | `tools/determinism-check.py` |
| B-8 | Validators on an empty SDK "again" sample built with our CMake | not measured | `tools/plugin/validate.sh` |

Recorded 2026-11-07 (M2 8 GB, app 1.1.0) in `tests/plugin/baseline.json` by `tools/plugin/baseline.py`:

| # | Value | Note |
|---|---|---|
| B-1 | 62.8 MB | |
| B-2 | 195 MB headless, ~310 MB plugin mode | 195 MB = peak RSS of headless 10 s render; ~310 MB = peak RSS of the engine in plugin mode during `plugin_engine_check` (hidden window, GL context up). 4 instances fit in 8 GB, S1-c not hit; trimming the hidden-window cost is a 2.x item |
| B-3 | 0.34 s | headless start to 0.1 s rendered |
| B-4 | 0.056 core | offline, stand-in patch `first-music-image.inf`; R1 value waits for 1.4 |
| B-5 | `ipc_bench` (1.3): p50 28 µs, p99 61 µs at 128 (2.3 % of the block); 32: 17/47 µs; 2048: 72/155 µs; 0 xruns at every size | S1-b not hit. Host thread pays p50 12 µs per `Process()` at 128 (0.45 % of a core, M-8 budget 3 %) |
| B-6 | 1.5-1.9 ms per 3200x2000 frame | measured in the real path (2.3): two PBOs, map one frame later, flip + copy into shared memory, Release engine on M2; view blit into IOSurface 1.2-1.5 ms more |
| B-7 | identical | stand-in patch; known exception unchanged |
| B-8 | validator 47/0, auval PASS | spike shell; the real 1.5 shell also gives validator 47/0 on Instrument and FX; the 1.6 AUs pass `auval -strict` (1 warning on FX: Tail Time not reported by auwrapper). pluginval 1.0.4 PASS at strictness 5 and 10 on both (1.7) |

R1 and R2 are frozen in `tests/plugin/patches/` (1.4): `R1-instrument.inf` (MIDI Notes -> Analog -> Delay -> Reverb -> out, Audio Analyze -> Shape -> Output) and `R2-fx.inf` (Audio In -> Audio Filter -> Delay -> out). R3 and R4 freeze with the view work in chunk 2. CC device bindings in plugin mode and 32-sample parameter sub-blocks, deferred from 1.4, landed in 2.2. Spike details: `tools/plugin/spike/README.md`.

Reference patches R1-R4 are picked in 1.0 from `patch-universe-library` and frozen in `tests/plugin/patches/`: R1 instrument (synth, notes from host), R2 FX (Audio In -> filter -> delay -> Audio Out), R3 audio-reactive visual (Audio In -> analysis -> Field Pixel), R4 heavy (Field Synth + 3D).

## 7. Success metric (fixed now)

### 7.1 Numbers

| # | Metric | Pass |
|---|---|---|
| M-1 | Steinberg validator (`public.sdk` testsuite), both builds, 3 OSes | 0 failures |
| M-2 | pluginval | Strictness 5 in CI on every push; **strictness 10** on the release candidate, macOS + Windows + Linux |
| M-3 | `auval -v aumu` (Instrument) and `auval -v aumf` (FX), plus `auval -strict` | PASS, both, Intel and arm64 slices |
| M-4 | Offline determinism: same project bounced twice | Bit-identical for R1, R2, R4 (R3 has no audio out change) |
| M-5 | Bounce = live: offline bounce vs. real-time render in Reaper, null residual | <= -60 dBFS RMS for R1, R2, R4 at 32/64/512/2048 buffers; R-patches with Field Notes + modulation: <= -40 dBFS (known B-7 exception) |
| M-6 | Reported latency is true | Impulse through R2: measured offset = reported latency, 0 samples error |
| M-7 | Xruns | 0 engine deadline misses in a 1-hour soak, 4 instances of R1 + R2, 48 kHz / 256, M2 8 GB |
| M-8 | Shell + IPC overhead per instance | <= 3 % of one core above the engine's own cost (B-4) |
| M-9 | Memory per instance, idle | <= B-2 + 10 %, and 4 instances of R1 fit with Reaper on 8 GB with no swap growth |
| M-10 | GUI | Open <= 1.5 s warm, <= 4 s cold; pixel pipe <= 10 % of one core at 30 fps, 1600x1000 @2x; input to pixels <= 50 ms p95 |
| M-11 | State | Save, close, reopen in every v1 DAW: patch JSON equal after canonical sort; slot values equal; on a machine with no licence it loads and plays |
| M-12 | Crash isolation | Killing the engine process mid-playback never takes down the host; the plugin outputs silence and shows "Engine stopped, Restart" |
| M-13 | Host survival | 0 host crashes or hangs across the DAW matrix (8.8) |
| M-14 | Install | A tester with no Terminal knowledge installs on macOS following `/download` in <= 3 minutes (owner or one friend, timed) |

### 7.2 What success is not

- Not "bit-exact live vs offline" for every patch: free-running visuals and random nodes differ by design (owner 2026-10-13: patches evolve). M-5 is measured on transport-synced patches.
- Not copy protection (8.5).

## 8. Best and optimised methodology

### 8.1 How "transform" works (scope item B)

The patch graph is the transform. The host gives audio, MIDI and time; the graph cooks; the plugin returns audio and frames.

```
 HOST (DAW)                         PLUGIN SHELL                     ENGINE PROCESS (Infinite, plugin mode)
 ----------                         ------------                     --------------------------------------
 audio in  ----+                                                     +--> Audio In node(s)
 MIDI events --+--> process(block N) --> pack into shm ring ---------+--> MIDI Notes / CC / Trigger
 transport  ---+     (audio thread)      (block N + host time)       +--> Transport.FollowHost(ppq,tempo,sig,play,loop)
 param pts  ---+                                                     +--> host slots -> Macro nodes
                                                                     |
                                                                     |   graph cooks block N in sub-blocks of 32
                                                                     |   (audio thread of the engine)
                                                                     v
 audio out <--- process(block N+1) <-- read output of block N <---- Audio Out / Output
                 (one block later = reported latency L)
                                                                         main thread of the engine, ~60 Hz:
 plugin view <-- blit pixels <-------- shm frame triple buffer <---- ImGui canvas + visuals into FBO
```

| Question | Answer |
|---|---|
| When does the graph cook audio | On the engine's audio thread, once per host block, woken by the shell's post. Inside a block it runs sub-blocks of at most 32 samples so parameter points and modulation land on time |
| What is sample-accurate | Note on/off, CC, pitch bend, aftertouch (`frameOffset` per event, as `NoteEvent` already carries). Host parameter points: applied at the 32-sample sub-block boundary nearest the point (<= 0.67 ms at 48 kHz) and smoothed (8.3) |
| What is per frame | Visuals: cooked on the engine's main thread (pull model, `CookIfNeeded(frameId)`), 60 Hz when the view is visible, 0 Hz when the plugin window is closed and no projector is open |
| Modulation clock | Plugin mode evaluates modulators at audio-time ticks (every 256 samples of host time, from the block's sample position), not wall-clock frames. Live and offline compute the same modulator value at the same sample. Only delivery jitter differs, which is why M-5 allows -60 dBFS, not bit-exact |
| Host time to Infinite time | Each block: `ppq` -> `Transport::SeekBeats` only on a jump; `tempo` -> `SetTempo`; `timeSig` -> `SetTimeSignature`; `playing` -> `SetPlaying`; loop (`cycleStart/End`) -> `SetLoop`. Seconds come from the host sample position, never wall clock |
| Jump detection | Expected ppq = last ppq + block length in beats. A difference of more than 1 sample's worth = jump (scrub, locate, loop wrap) |
| On a jump | **Hold state, relocate time.** Sequencers, clips and MIDI File nodes re-seek; envelopes, delays, reverbs, feedback and visual state are kept; all sounding notes get note-off (no hanging notes). No reset of node DSP state |
| On stop | Notes off, transport-driven nodes stop; free-running generators and visuals keep running on the free clock (`Transport::FreeSeconds`) |
| Variable block sizes | Any size 1..maxBlock. The engine has no fixed block; it processes exactly what the host gave, split into 32-sample sub-blocks |
| Sample-rate change | Host calls `setActive(false)` -> `setupProcessing(newRate)` -> `setActive(true)`. Shell forwards `Prepare(rate, maxBlock)`; engine re-prepares the graph the way it does on an audio-device change (`audio-pipeline-sweep` device-loss path) and re-reports latency |
| Patch reload on the fly (user loads a patch, or host `setState` while playing) | Engine builds the new graph on the main thread, then the audio thread fades the old out over 30 ms (`PassFade`), swaps, fades the new in over 30 ms. No click; 60 ms dip accepted |

Decision: one engine process per instance; graph cooks on the engine audio thread once per host block in 32-sample sub-blocks; visuals per frame on the engine main thread.
Decision: modulation in plugin mode is clocked from host sample time, live and offline alike.
Decision: a transport jump relocates time and releases notes but holds DSP and visual state.
Decision: reload swaps with a 30 ms + 30 ms fade.
Open risk: patches whose modulators read wall-clock time (free LFOs not synced to transport) will not null in M-5; accepted, documented in the plugin help.
Open risk: Ableton's freeze renders faster than real time with an `kInfiniteTail` plugin until the clip ends; the engine must keep up offline without dropping blocks (covered by the unbounded wait in offline mode, 8.4).

### 8.2 How in and out work (scope item C)

| Bus | Direction | Channels | Build | What it feeds / is fed by |
|---|---|---|---|---|
| Main audio in | In | Stereo; mono accepted | FX | `Audio In` node, source "Host main" |
| Sidechain | In | Stereo; mono accepted; inactive by default | FX and Instrument (aux) | `Audio In` node, source "Host sidechain" |
| MIDI in | In | 1 event bus, 16 channels | Both | Host MIDI enters the same input stream as hardware MIDI, so `MIDI Notes`, `MIDI CC`, `MIDI Trigger` and their channel filters work unchanged. Hardware MIDI in is off in plugin mode |
| Host slots 1-128 | In and out (automation read and write) | 128 fixed parameters, hidden until bound | Both | Bound to a Macro or any knob (see below) |
| Bypass | In | 1 parameter (`kIsBypass`) | Both | Engine fades out and passes input (FX) or silence (Instrument) |
| Main audio out | Out | Stereo | Both | `Audio Out` / `Output` node |
| Multi-out | Out | none in v1 | | Later |
| Visual: plugin view | Out | Pixels | Both | The canvas or the Output node's picture, user's choice in the view |
| Visual: pop-out window | Out | Real GPU window | Both | The engine's own GLFW window, full frame rate, same as the app |
| Visual: projector / second display | Out | Fullscreen windows | Both | The app's existing Output/projector path; the engine is a normal GUI process, so displays work as in the app |

Channel cases:

| Host gives | Behaviour |
|---|---|
| Stereo | As is |
| Mono | Copied to both sides of `Audio In`; output stays stereo (host downmixes if the track is mono) |
| 5.1 or wider | `setBusArrangements` returns false; hosts fall back to stereo |
| No input (instrument track, or FX with nothing playing) | `Audio In` reads silence; generators still run |

Parameters:

| Topic | Decision |
|---|---|
| How many | 128 host slots + 1 bypass. Fixed count and fixed IDs forever (FL, Ableton and Cubase cache parameter lists per plugin ID; changing the count breaks old projects). An unbound slot is hidden (`kIsHidden`; through auwrapper it is non-realtime and not automatable), so a fresh plugin shows an empty parameter list (owner 2026-11-08: no "Slot 01..32") |
| Naming | Unbound: "Unused NN" (hidden). Bound: the node's name in Title Case, "<Node> <Param>" ("Gain" for a one-knob node), and the flags flip to `kCanAutomate`, sent with `restartComponent(kParamTitlesChanged)` (VST3) / ParameterList changed (AU). Names are saved in the state header (`names`), so a host reading the list before the engine runs already sees them |
| Fixed vs dynamic | Fixed slots, dynamic binding |
| Binding | Right-click any knob or param > "Host automation" takes the next free slot; "Remove host automation (N)" frees it; plugin mode only; saved in the patch as `hostparam slot node param`. Macro nodes keep their "Host slot" field (none or 1-128; XY binds two) |
| Learn / map | Right-click "Host automation" on any knob (built 2026-11-08). Hosts that add their own map (Ableton Configure, Logic, Bitwig) see the bound params by name |
| Smoothing | One-pole, 10 ms, applied per 32-sample sub-block in the engine. Toggle, Trigger and Radio slots are not smoothed (stepped) |
| Infinite -> host | Turning a bound Macro in the plugin view sends begin/perform/end edit through the shell's message thread, so automation recording works |
| VST3 CC quirk | VST3 delivers CC, pitch bend and channel pressure only as parameters (`IMidiMapping`). The shell adds 130 hidden, non-automatable parameters (not read-only: the host writes them from CC) (CC 0-127, pitch bend, channel pressure) mapped omni, and turns them back into MIDI messages for the engine. Poly pressure arrives as `PolyPressureEvent`. Per-channel CC on VST3 is later; AU gets full raw MIDI |

Latency and tail:

| Topic | Decision |
|---|---|
| Plugin latency L | L = host `maxSamplesPerBlock` (one block). Reported through `getLatencySamples` / `kAudioUnitProperty_Latency`. Changes on `setupProcessing` trigger `kLatencyChanged` |
| Patch latency | Graph PDC inside the engine (`CompensationDelay`) is added to L when non-zero |
| Tail | `kInfiniteTail` (VST3) / `TailTime` = infinite (AU) for both builds: Infinite patches self-generate and visuals react; a host that stops processing on silence would freeze them |

Decision: stereo main in (FX), optional stereo sidechain, stereo out, mono accepted, wider refused.
Decision: 128 fixed slots + bypass, hidden until bound through right-click "Host automation" on any knob or a Macro's "Host slot" field; 130 hidden omni MIDI-mapping parameters on VST3.
Decision: latency = one host block, reported; tail infinite.
Decision: plugin view, pop-out window and projector outputs all ship in v1.
Open risk: FL Studio and Ableton show the 130 hidden parameters in some menus despite `kIsHidden`; accepted, the names are "MIDI CC 0".."MIDI CC 127".
Open risk: a 2048-sample host buffer means 43 ms latency at 48 kHz on an FX track used for live monitoring; PDC covers playback, live monitoring at 2048 is not a v1 use case.

### 8.3 How the signal flows (scope item D)

```
 THREADS                       HOST PROCESS                                  ENGINE PROCESS (one per instance)
 =======                       ============                                  =================================

 HOST AUDIO THREAD (RT)        Shell::process(block N)
   no alloc, no lock,            1 write audio in, MIDI, transport, param
   no file, no GPU                 points into shm IN ring (SPSC, block N)
                                 2 post semaphore  ------------------------->  ENGINE AUDIO THREAD (RT priority,
                                 3 read shm OUT ring: output of block N-1        mac: audio work interval;
                                   (ready: copy out; late: wait up to             win: MMCSS "Pro Audio";
                                    deadline in realtime, forever offline;        linux: SCHED_FIFO via rtkit)
                                    missed: silence + xrun counter)                 wake, read IN block N
                                 4 return                                          apply params per 32-sample sub-block
                                                                                   cook graph, write OUT block N
                                                                                   push meters + analysis frames into
                                                                                     lock-free rings (MeterRing,
                                                                                     AudioCaptureRing pattern)
                                                                                         |
 HOST MESSAGE / UI THREAD      Shell controller + view                                   v
                                 getState / setState  <-- IPC control channel -->  ENGINE MAIN THREAD
                                 param edits from GUI <---------------------------   reads rings (audio-reactive data),
                                 view: mouse/keys/size ------------------------->     modulation at audio-time ticks,
                                 view: blit newest frame <-- shm frame triple buf --  CookIfNeeded, ImGui, FBO render
                                 timer 60 Hz (VST3 IRunLoop on Linux)                         |
                                                                                               v
 GPU                           (shell draws only a blit: CALayer contents /           ENGINE GL CONTEXT (own process)
                                StretchDIBits / XShmPutImage)                          FBO + PBO async readback,
                                                                                       pop-out and projector windows
```

Data paths:

| Path | Mechanism | RT rule |
|---|---|---|
| Host audio/MIDI/time -> engine | shm IN ring, fixed-size slots sized for maxBlock, versioned header, sequence numbers | Shell writes with plain memcpy; one semaphore post (async-signal-safe) |
| Engine audio -> host | shm OUT ring | Shell copies; waits only when late |
| Audio thread -> visuals (audio-reactive) | Inside the engine, same as the app: lock-free rings read by the main thread | Unchanged from the app |
| Host param change -> engine | Timed points in the IN ring per block | Applied on the engine audio thread |
| Engine GUI edit -> host automation | Control channel (pipe / socket) to the shell's message thread, never the audio thread | Message thread only |
| Frames -> plugin view | shm triple buffer of RGBA frames + "newest" index (atomic); shell blits the newest on its timer | Never touches the host audio thread |

State save/load:

| Topic | Decision |
|---|---|
| What is serialized | A header `{format: "infinite-plugin-state", v: 1, appVersion, build: "instrument"/"fx", latencyMode, slotValues[32]}` + the patch as the same JSON the app writes to `.inf` (Patch::Data), compressed with miniz |
| Versioning | `v` is bumped only on a header change. The patch part follows the app's own patch loading (patches evolve, no legacy flags: `feedback_scale_to_user_base`) |
| When | `getState` asks the engine through the control channel (message thread, 2 s timeout). If the engine is not running (host saved before activation) the shell returns the last state it received or loaded, so a save never loses a patch |
| Machine with no licence | Loads, plays, saves in full. The licence never touches state (8.5) |
| Machine with no engine running yet | `setState` stores the blob; the engine gets it when it starts |
| Files and samples a patch references | Stored by absolute path, as in the app. Missing files: the app's existing missing-media handling. Embedding samples into the project is later |
| The licence key | **Never** written into the DAW project (projects get shared; the key would leak) |

Failure modes:

| Failure | Behaviour |
|---|---|
| No GPU context in the engine (driver, VM, Xvfb without GL) | Engine runs audio-only (the headless path). Plugin view draws a native text "No graphics on this machine: audio only". Pop-out and projector disabled |
| Host suspends (`setProcessing(false)`, `setActive(false)`) | Engine stays alive and idle; notes off; on resume re-prepare if rate or block size changed |
| Host stops calling process (track disarmed, plugin sleep) | Engine idles after 2 s without blocks; visuals keep the last frame or free-run if the view is open |
| Plugin removed mid-render | `terminate()` sends quit, waits 500 ms on the message thread, then kills the engine. The audio thread never waits for the engine to exit |
| Host crashes or is force-quit | Engine watches the parent (kqueue `NOTE_EXIT` on macOS, process handle wait on Windows, `pidfd_open` + poll on Linux with a `getppid` fallback; not `PR_SET_PDEATHSIG`, which fires when the spawning thread exits) and exits |
| Engine crashes | Shell sees the dead pid / heartbeat stop, outputs silence, view shows "Engine stopped, Restart". Restart respawns with the last state. The host never crashes (M-12) |
| Engine hangs | Heartbeat counter in shm stops for 3 s: same as a crash, the shell kills and offers Restart |
| Two instances sharing one GPU | Separate processes, separate GL contexts; the GPU schedules them. Each engine caps its texture caches the same way the app does. Hidden plugin views render nothing |
| Host scans plugins | Scanning instantiates and queries; the engine is **not** spawned until the first `setActive(true)` or view open, so scans stay fast |

Decision: shared-memory rings + one semaphore per direction for audio, a control channel for state and edits, a shm triple buffer for frames.
Decision: state = header + `.inf` JSON, compressed; files by path; licence never in the project.
Decision: engine spawned lazily; host and engine each watch the other die.
Open risk: macOS `getState` timeouts in Logic if the engine is stuck; mitigated by returning the last known state, but a patch edited in the last 2 s before a hang can be lost.
Open risk: memory, each instance carries a full app; checked by M-9 and stop condition S1-c.

### 8.4 Real-time safety

| Topic | Rule |
|---|---|
| Shell audio thread | No allocation (all rings and scratch sized in `setupProcessing`), no locks, no syscalls except one semaphore post per block and, only when late, a timed wait |
| Realtime deadline | Wait at most 60 % of the block period for a late engine, then output silence for that block, increment the xrun counter shown in the plugin view. Never wait longer: the host's own deadline is sacred |
| Offline render (`kOffline` process mode / AU `OfflineRender` property) | Shell waits for every block with no deadline, engine runs in `Transport::SetOfflineMode(true, rate)`; this is what makes bounce = live (M-4, M-5) |
| Engine audio thread | Same rules as the app's audio callback (`docs/ARCHITECTURE_FLOWS.md` §2): no alloc, no lock, no file I/O, no ImGui |
| Priority | macOS: `AudioWorkIntervalCreate` work interval joined by the engine audio thread; Windows: MMCSS "Pro Audio"; Linux: rtkit SCHED_FIFO, fallback nice -10 |
| Latency reporting | 8.2; verified by M-6 |
| Sample-rate and buffer changes | 8.1; buffers re-sized only inside `setupProcessing`, never in `process` |
| Denormals | The engine already handles them per kernel (`windows-parity` 3.6); the shell does no DSP |
| Process mode changes mid-session (Ableton freeze, Reaper render) | Offline flag read per block from the host's process mode; switching flips the wait policy at the next block |

Decision: realtime waits are bounded at 60 % of the block, offline waits are unbounded.
Open risk: a priority inversion if the OS demotes the engine's audio thread under load (macOS App Nap on a background helper). Mitigation: engine helper declares `NSAppSleepDisabled` and holds an `NSProcessInfo` activity while blocks arrive.

### 8.5 Licensing in the plugin

The source is MIT. The VST3 SDK is MIT since 3.8 (`external/vst3sdk/LICENSE.txt`, v3.8.1), and Apple's AudioUnitSDK is Apache-2.0, so the plugin binary is **MIT too**, not GPLv3. The `LICENSE` text that says "GPLv3 binary by default" is out of date and chunk 1 corrects it. Either way anyone can build the plugin without the check: **the key is a convenience and a thank-you, not copy protection.**

| Topic | Decision |
|---|---|
| Verify | Offline Ed25519 over the payload of `INF1.<b64url payload>.<b64url sig>` with the 32-byte public key from `worker/license/PUBLIC_KEY.txt`, compiled into the engine as a constant by CMake (one source of truth). Library: Monocypher (vendored single file, CC0/BSD-2). JSON via the bundled nlohmann json |
| Checks | Signature valid; `v == 1`; `major` >= the plugin's major (1); `id` not in the cached revoked list |
| Where the code lives | `src/core/License.{h,cpp}`, built into the shell (`infinite_shell_ipc`) and the engine; the app never calls it. The shell checks the key before it spawns an engine and draws the sheet; the engine only reads the key's id for the weekly refund check and tells the shell (`kMsgLicense`) when that id is on the list |
| Entering the key | Drop `license.inflic` on the plugin view, or paste the `INF1...` text into a paste box on the licence sheet. Same sheet in both builds |
| Where it is stored | One per user, shared by both builds and all instances: macOS `~/Library/Application Support/Infinite/license.inflic`, Windows `%APPDATA%\Infinite\license.inflic`, Linux `$XDG_CONFIG_HOME/infinite/license.inflic` |
| 2 seats without an account | Not enforced by code. The key shows "Licensed to <email> · INF-XXXX" in the plugin's About; the terms say two computers. Nothing counts machines |
| Revoked list | `GET /api/license/revoked` from the engine main thread, at most once per 7 days, only while licensed, no identifiers sent (the list is downloaded, not a lookup). Cached on disk. Network failure, timeout or bad JSON = keep the last cached list (**fail open**). Off switch in the licence sheet |
| Trial | **None** (owner 2026-11-07). The free standalone app is the full instrument to try; the plugin is what people buy |
| Unlicensed | The plugin loads, keeps and saves the patch (getState returns exactly what setState got, so no project is ever damaged), and its view shows only the licence sheet: "Enter key · Buy". Instrument outputs silence, FX passes its input through dry with the reported latency, so a track never jumps in time. The engine is not spawned until a key is valid |
| Licensed | Everything; a valid key never expires (perpetual for its major), so bounce = live holds for every licensed user |
| Revoked key | Unlicensed, with the text "This key was refunded" |
| Wrong major (future 2.x with a 1.x key) | Unlicensed, with "Your key covers 1.x" |
| Projects on another machine | Always open with the patch intact; play once that machine has a key (M-11 is tested licensed) |

| Test keys | CMake option `INFINITE_LICENSE_TEST_KEY` (off by default, prints a warning when on): the build also trusts `$INFINITE_LICENSE_PUBKEY` or `<dir>/license-test-key.hex`, so the checks sign their own keys. Development only; a packaging build must configure it off. `$INFINITE_LICENSE_DIR` moves the licence folder and `$INFINITE_LICENSE_REVOKED_URL` the refund list in every build |

Decision: offline Ed25519 with Monocypher, key file in the user's config folder, revoked list weekly and fail-open, no trial: unlicensed = licence sheet only, Instrument silent, FX dry, patch kept.
Open risk: people who want to try in their DAW first cannot. Accepted (owner): the free app is the trial and the refund window covers a host that does not work for them.
Open risk: a sandboxed host (GarageBand) may not let the engine read the user's config folder; fallback is the host-container folder, found in the chunk 1 spike.

### 8.6 Packaging and distribution

Bundles:

| OS | Files | Install location |
|---|---|---|
| macOS | `Infinite Instrument.vst3`, `Infinite FX.vst3`, `Infinite Instrument.component`, `Infinite FX.component`; each contains (or points to) `Infinite Engine.app` (LSUIElement helper, no Dock icon); universal arm64 + x86_64 | `~/Library/Audio/Plug-Ins/VST3`, `~/Library/Audio/Plug-Ins/Components` (per user, no admin) |
| Windows | `Infinite Instrument.vst3`, `Infinite FX.vst3` bundles (`Contents/x86_64-win`, `Contents/arm64-win`), each with `infinite-engine.exe` and its DLLs | `C:\Program Files\Common Files\VST3` |
| Linux | Same bundles with `Contents/x86_64-linux` (+ `aarch64-linux`), engine binary and libs at the AppImage glibc floor | `~/.vst3` |

One engine copy per OS, not per build: the Instrument and FX bundles share `Infinite Engine` in a common folder (`~/Library/Application Support/Infinite/Engine/<version>/` on macOS, the VST3 folder's `Infinite Engine` bundle elsewhere) to halve the download.

Signing without a paid Apple account:

| Problem | Route |
|---|---|
| Notarization needs a paid account | Not available. Bundles are ad-hoc signed (`codesign --sign -`, as `package.sh` does), which Apple silicon requires |
| Browser downloads carry the quarantine flag; DAWs then refuse an unnotarized plugin | **Primary route: Terminal installer.** `/download` shows one line: `curl -fsSL https://infinitedevices.org/install.sh \| bash`. curl adds no quarantine flag; the script downloads the app release from GitHub, checks its SHA-256 against the value baked into the script, installs the app and copies the plugin bundles (`--install-plugins`), done. No dialog |
| Users who will not use Terminal | Zip download + the existing "Open Anyway" / `xattr -dr com.apple.quarantine` steps the website already shows for the app, rewritten for the two plugin folders |
| Homebrew users | The existing app cask in `n1m21n/homebrew-infinite` gains `vst3_plugin` + `audio_unit_plugin` stanzas and a postflight that clears quarantine on the installed bundles |
| `.pkg` installer | Not used: an unsigned `.pkg` hits the same Gatekeeper wall and adds admin rights |

Windows and Linux:

| OS | Route |
|---|---|
| Windows | Zip of the two bundles + a one-page "copy to Common Files\VST3" guide, and a PowerShell one-liner that does the copy. DAWs load unsigned VST3 DLLs. SignPath Foundation signs the app today; it is used for the plugin only if its terms allow a paid binary (checked in chunk 3, see risk R-6). Inno Setup installer is later, when there is a certificate |
| Linux | `.tar.gz` of the bundles + `install.sh` copying to `~/.vst3` |

Updates and pages:

| Topic | Decision |
|---|---|
| Auto-update | **Through the app.** The app's `SelfUpdate` updates the app; the app re-copies the plugin on next launch (8.11). The engine only shows "Update in the Infinite app". No self-replacing binaries inside a running DAW |
| Where binaries live | Inside the normal app release `vX.Y.Z` on `n1m21n/Infinite`, public (the binary is MIT; the free app is the demo; reach feeds income). No separate plugin release (8.11) |
| `/download` page | `website/download.html`: per-OS buttons, the Terminal line for macOS, the first-run steps (drop the key), links to buy and support. Built from the site's tools (`tools/website/`), brand-linted (`tools/brand/brand_lint.py`) |
| `/download` URL | Worker var `DOWNLOAD_URL = https://infinitedevices.org/download`; the purchase email already prints it when `RELEASED = "1"` |
| Buy page | Stays; copy changes from "pre-order" to "buy" on ship day (`paddle.json` `released: true`) |

Decision: ad-hoc signed bundles shipped inside the app; Terminal installer as the main macOS route, DMG + Open Anyway as the second, the app's Homebrew cask as the third.
Decision: Windows zip, Linux tarball, updates through the app, public binaries on the app's GitHub release.
Open risk: Smart App Control on fresh Windows 11 installs blocks unsigned DLLs and exes outright; affected users cannot load the plugin until a code-signing route exists (R-6).

### 8.7 Validation

Tools (tool-first; each is one command, lives in `tools/plugin/`, has a `--help`):

| Tool | What it does |
|---|---|
| `baseline.py` | Records B-1..B-8 once into `tests/plugin/baseline.json` |
| `ipc_bench` (C++ target) | Shell <-> engine round trip p50/p99 for 32..2048-sample blocks |
| `validate.sh` | Builds nothing; runs the Steinberg validator, pluginval at a given strictness, and `auval` (macOS) on the built bundles; one summary table |
| `reaper_rig.py` | Writes Reaper `.rpp` projects (plain text) for R1-R4 with automation, renders them offline and online (1x) through Reaper's command line, nulls the two (M-5), measures impulse latency (M-6), checks state round trip by save + reopen + re-render (M-11) |
| `soak.py` | DAW-free 1-hour run of 4 instances (2 R1 + 2 R2) through `plugin_shell_check --soak` on a realtime host thread; reads the shells' xrun counters, host CPU and engine physical footprint (M-7, M-8, M-9) |
| `crash_drill.py` | Kills the engine mid-render; checks the host lives and the plugin shows Restart (M-12) |
| `install_check.sh` | Fresh-user install of each route in a clean macOS user account / Windows VM / container |

Validator settings:

| Validator | Where | Gate |
|---|---|---|
| Steinberg validator (SDK testsuite) | CI, 3 OSes, both builds | Every push on plugin branches |
| pluginval | CI strictness 5 (fast); release candidate strictness 10 locally on mac, in the Windows VM, in the Linux container | M-2 |
| `auval -v` + `auval -strict` | macOS CI runner + local | M-3 |

Rigs:

| Rig | Covers |
|---|---|
| macOS local: Reaper + `reaper_rig.py` | Every M-metric except DAW-specific quirks |
| Linux container + Xvfb (`tools/linux/local.sh`, `xvfb-harness.sh`): Reaper for Linux headless + validators; llvmpipe for the GL path | Linux VST3 end to end, GL fallback path, the no-GPU failure mode |
| Windows 11 ARM in UTM on the Mac (free evaluation image), Reaper ARM64 + x64 | Windows runtime (CI only proves compiling, `windows-parity` §4) |
| Infinite's own host (`tools/re-survey/host_audit.py`) | A second, independent loader on macOS: load, process, editor open, out-of-process |

CI changes (`.github/workflows/build.yml`, one push per chunk end, after `ci-preflight`):

- New option `INFINITE_BUILD_PLUGIN` (default ON in CI, OFF in `ibuild` dev mode unless `--target` names a plugin target).
- Jobs: build both bundles per OS/arch, run the validator + pluginval 5 (+ auval on macOS), upload bundles as artifacts.
- AudioUnitSDK fetched at configure time by `FetchContent` at a pinned tag (never committed).
- `attach-release-assets.yml` adds the plugin artifacts to its list for the normal `vX.Y.Z` release (8.11).

Per-DAW matrix: section 8.8.

Decision: Reaper is the automated rig on all three OSes; real DAWs are tested by hand with the checklist in 8.8.
Open risk: the UTM Windows VM needs ~4 GB of the 8 GB Mac; Windows runs happen with nothing else running and no build in progress.

### 8.8 DAW matrix (scope item E)

Every host below is **v1**: a failure in any of them blocks release (owner 2026-11-07: all DAWs, all platforms). Depth: **full** = all common checks C1-C10 every release candidate; **core** = C1, C2, C3, C5, C7, C10 every release candidate, full checks once per major.

| Host | Tier | Format used | Instrument / FX quirks | GUI / GPU quirks | What to test beyond the common list | Pass criteria |
|---|---|---|---|---|---|---|
| Logic Pro | v1 full | AU only | Instrument = `aumu`; FX = `aumf` (music effect, so it gets MIDI); sidechain via Logic's sidechain menu | Logic may host AUs out of process (AUHostingService); the engine is then a grandchild. View is a Cocoa view | `auval` in Logic's Plug-in Manager, MIDI FX before the FX build, bounce in place, Track freeze | auval PASS in Logic; all common checks |
| GarageBand | v1 core | AU only | Same AU types | Sandboxed; spike 1.0 proved the engine spawns and shm works from the sandboxed AUHostingService | Load, play, save, export | Common checks |
| MainStage | v1 core | AU only | Instrument and FX in patches; live use, so C7 at 64 and C10 matter most | Cocoa view | Patch switch while playing, 1-hour concert soak | Common checks; no xrun on patch switch |
| Ableton Live 12 (mac, Windows) | v1 full | VST3 and AU (both tested) | Instrument on MIDI track; FX on audio track with MIDI routed from another track; freeze / flatten | Plugin window scaling on Windows (Ableton's "auto-scale plugin window" option); Ableton caches parameters per plugin ID | Freeze and flatten = live, Configure parameter mode with slots, window at 100 / 150 / 200 % on Windows | All common checks; freeze null M-5 |
| Reaper 7 (mac, Windows, Linux) | v1 full | VST3 and AU | Strict threading: calls from any thread; "run FX in separate process" option | Reaper's own bridge and sandbox modes; HiDPI on Windows | The automated rig runs here; also Reaper's "dedicated process" mode | Rig green; all common checks; works in "separate process" mode |
| FL Studio 2025 (mac, Windows) | v1 full | VST3 | Wrapper: parameter list fixed at load; Instrument in Channel rack, FX in Mixer; MIDI to FX through "MIDI out" plugin | Wrapper window frame; resizing through FL's own handle | Parameter learn through FL's "Link to controller", project save and reopen (FL's own demo cannot reopen projects, so M-11 needs a licensed FL) | All common checks |
| Bitwig Studio 5 (mac, Windows, Linux) | v1 full | VST3 | Plugin sandboxing: each plugin in a Bitwig host process; the engine is spawned from that process | View is embedded across Bitwig's process boundary by Bitwig; our view is a normal native view inside it | Sandbox modes "Within Bitwig", "Together", "By manufacturer", "Individually": all four | All common checks in all four sandbox modes |
| Cubase / Nuendo 14 (mac, Windows) | v1 full (reference host) | VST3 | Strictest on the spec: bus arrangements, `kIsBypass`, `IProcessContextRequirements`, latency restart | Content scale factor on Windows | Steinberg validator inside Cubase (Plug-in Manager), latency change while playing | All common checks; validator inside Cubase green |
| Studio One 7 (mac, Windows) | v1 full | VST3 (AU on macOS) | Instrument via Instruments panel; sidechain via its routing | Content scale on Windows | Bounce, transform to audio | Common checks |
| Ardour 8 / Mixbus (mac, Windows, Linux) | v1 core | VST3 (AU on macOS) | Strict VST3 host, plugin latency compensation always on | X11 embed on Linux | Export, latency compensation with 2 instances | Common checks |
| Reason 13 (mac, Windows) | v1 core | VST3 | Rack wrapper ("Rack Extension" style VST3 shell) | Separate window | Load, play, save, export | Common checks |
| Tracktion Waveform 13 (mac, Windows, Linux) | v1 core | VST3 (AU on macOS) | Racks; freeze | Standard | Freeze = live | Common checks |
| Cakewalk Sonar (Windows) | v1 core | VST3 | Per-plugin "upsample on render" option must stay off for M-5 | Content scale | Render, freeze | Common checks |
| Digital Performer 11 (mac, Windows) | v1 core | AU on macOS, VST3 on Windows | Instrument tracks; chunks | Standard | Bounce | Common checks |
| Renoise 3.5 (mac, Windows, Linux) | v1 core | VST3 (AU on macOS) | Tracker: MIDI from pattern, short blocks | Separate window | Render song, block sizes from Renoise's own scheduler | Common checks |
| LUNA (mac, Windows) | v1 core | VST3 / AU | Standard | Standard | Bounce | Common checks |
| Zrythm (Linux) | v1 core | VST3 | Strict host | X11 embed | Export | Common checks |
| Audacity 3.7 (all OS) | v1 core, FX only | VST3 (AU on macOS) | Effects only, offline "apply" and realtime effects | Separate window | Apply effect offline = realtime | C1, C3, C5, C10 |
| Pro Tools | chunk 4 | AAX | Not reachable by VST3/AU (3.2) | | | |

Common checks for every host (one checklist file per DAW, `tests/plugin/daw/<host>.md`, filled by hand, date + version + result):

| # | Check | Pass |
|---|---|---|
| C1 | Window open, close, reopen, 10 times | No leak above 20 MB, no crash, picture returns |
| C2 | Multiple instances: 4 (2 Instrument + 2 FX) | All play, each its own patch |
| C3 | Offline bounce vs live recording of R1 / R2 | M-5 |
| C4 | Automation recording from the plugin view, playback of written automation | Curve recorded, played back on the bound macro |
| C5 | Project save, close host, reopen | M-11 |
| C6 | Sample rate change 44.1 -> 48 -> 96 kHz while the project is open | Re-prepares, no crash, correct pitch |
| C7 | Buffer sizes 32, 64, 512, 2048 | No xrun at 64 and up with R1 on M2 (32 may xrun; recorded) |
| C8 | Transport: play, stop, loop, scrub | Notes released, sequencers relocate |
| C9 | 1-hour soak with R1 + R3 view open | M-7, memory flat |
| C10 | Engine kill (`crash_drill.py` or Activity Monitor) | M-12 |

What runs without a DAW (2.8, `plugin_shell_check` unless named):

| # | Headless check | What a DAW adds |
|---|---|---|
| C1 | `--view`: 10 open/close cycles, frames each time, engine footprint before and after | The host's own window frame |
| C2 | Instrument + FX + Instrument in one host, one engine each; soak plays 2 R1 + 2 R2 | Host's plugin process layout |
| C3 | `--metrics` M-5 null at 32 / 64 / 512 / 2048, offline vs realtime | Host's bounce path |
| C4 | Slot checks: automation drives the Macro; begin/perform/end from a Macro turn | Host's automation lanes |
| C5 | State checks: setState before and while running, block-size restart keeps the patch | Host project file (M-11 rig) |
| C6 | One instance at 44.1, 48, 96 kHz: re-prepared, note 60 within 1.2 % of 261.6 Hz | Host's rate-change call order |
| C7 | M-5 at four block sizes; soak M-7 at 256 | Host scheduler |
| C8 | `plugin_engine_check` relocate; suspend releases held notes | Host loop / scrub calls |
| C9 | `soak.py` 60 min (M-7, M-8, M-9) | View open during the soak |
| C10 | Crash and hang drills, Restart keeps the patch; `crash_drill.py kill` in a running DAW | Host survives (Logic proven) |

Which needs real hardware:

| Host | Coverage |
|---|---|
| Reaper (mac, Linux, Windows VM) | Rig (automated) |
| Logic, GarageBand, MainStage, Ableton, Bitwig, FL, Cubase, Studio One, Reason, DP, Renoise, LUNA, Waveform, Audacity on macOS | Real hardware: this Mac, the DAWs' own demo versions for testing |
| Ableton, FL, Bitwig, Cubase, Studio One, Sonar, Reason, Waveform, Renoise on Windows | Windows VM on this Mac (ARM64 Windows, x64 hosts under emulation); a real x64 PC test by the owner or a beta tester before ship |
| Reaper, Bitwig, Ardour, Waveform, Renoise, Zrythm, Audacity on Linux | Container + Xvfb for the automated parts; one real Linux desktop run (owner's other laptop or a beta tester) before ship |

Decision: every VST3/AU host on every OS it runs on is v1; eight get full checks, the rest core checks. Pro Tools comes with AAX in chunk 4.
Decision: Reaper is the automated rig; every other host is hand-tested with the checklist, using the DAWs' own demo versions.
Open risk: DAW demos expire (30 days typical); each is installed only when its matrix step starts. The longer host list adds about 25 agent hours plus owner time to chunks 2 and 3.
Open risk: Windows x64 hosts under ARM64 emulation can hide x64-only bugs; one real x64 PC run is required before ship (12.2).

### 8.9 What is shared with the app and what is plugin-only

| Part | Shared with the app | Plugin-only |
|---|---|---|
| Nodes, DSP, Field, visuals, patch format | Yes, all | |
| `Transport` | Yes | `FollowHost()` entry point |
| Audio backend | | `HostAudioBackend` (shm, no device) |
| MIDI | Input stream and nodes | Host MIDI feed; hardware MIDI in off |
| GUI | ImGui canvas, panels | Offscreen FBO + frame pipe + input injection; Export / Render Take / Audio Plugin node disabled |
| Licence | `src/core/License.*` built into both, used only in plugin mode | Licence sheet, strip |
| Shell (`src/plugin/`) | | VST3 processor, controller, view (mac / win / linux), AU via `auwrapper`, IPC client, spawner |
| Platform | | New three-sided `Platform::` functions: shared memory, spawn helper, watch parent, RT priority |

Code layout: `src/plugin/` (shell), `src/app/plugin/` (engine plugin mode), `src/core/License.*`, `external/monocypher/`, `tools/plugin/`, `tests/plugin/`, CMake targets `InfiniteInstrument_vst3`, `InfiniteFX_vst3`, `InfiniteInstrument_au`, `InfiniteFX_au`, `InfiniteEngine`.

### 8.10 What is inside the plugin: nodes, features, panels

Rule: the plugin is the app. Everything stays on unless it needs a device the DAW owns, writes a film, hosts other plugins, or is a debug tool. Nothing new is added (AGENTS: no new nodes or modes).

Nodes (205 registered, plus the audio effects in `EffectDefs` and 49 filters in `src/core/FilterDefs.cpp`):

| Group | Count | In the plugin | Change in plugin mode |
|---|---|---|---|
| 3D | 49 | All | None. Bake still writes files |
| Compositing, filters, Resynthesize | 20 + 49 | All | None |
| Audio effects (EffectDefs, Field Effect) | 28 + 1 | All | None |
| Audio Plugin node | 1 | **Out** | Shows "Not available in the plugin", passes silence (no hosting inside a host) |
| Synths (Piano, MPC, Looper, Wind, Generate Samples ...) | 23 | All | Looper and MPC follow host transport |
| Notes (Piano Roll, MIDI File, Field Notes, Keyboard ...) | 25 | All | MIDI Notes read the host MIDI feed; Keyboard plays only while the plugin view has focus |
| Macros | 9 | All | "Host slot" field binds a macro to one of 128 host slots; any knob binds by right-click "Host automation" (8.2) |
| Modulators (LFOs, MIDI CC, MIDI Trigger, trackers ...) | 33 | All | MIDI CC / Trigger read the host feed; Face, Hand, Pose Track use the camera as in the app |
| Stub Predictor | 1 | Out | Already `#ifndef NDEBUG` + env var; never in a release |
| Prediction (green) | 8 | All | Training runs on its worker thread as in the app |
| Source (Video In, Video, Shadertoy, VMPC ...) | 16 | All | Video In uses the camera as in the app |
| Audio In / Audio Out | 2 | All | Sources: "Host main", "Host sidechain"; out goes to the host bus. Device pickers hidden |
| MIDI Out | 1 | All | Hardware and virtual ports only; to the DAW track later (3.2) |
| Spatial Mixer | 1 | All | Folds to the stereo bus (no surround in v1) |
| NDI, Syphon, OSC, Projection, Output | 7 | All | None: live shows are the point (DECISIONS 2026-10-14) |
| Gain, Mixer, Splitter, Meter, Blend Audio, Field Graph, Comment, Group, Null | rest | All | None |

Features:

| Feature | In the plugin | Why |
|---|---|---|
| Patch library, templates, presets, undo/redo, history | In | Same as app; saved in the DAW project with the patch |
| Field language (all four Field nodes) | In | Compiles off the audio thread already |
| Projector outputs, pop-out window | In | Live visual use |
| Host transport, tempo, 128 host slots, MIDI in | In (plugin-only) | 8.2 |
| Licence sheet, unlicensed strip | In (plugin-only) | 8.5 |
| Export, Render Take, video recording | Out | Films are made in the free app (3.2) |
| Audio and MIDI device setup | Out | The host owns devices |
| Self-update inside the DAW | Out | The app updates; the plugin follows (8.11) |
| Agent Terminal, Chat, CLI setup, live-session RPC | Out | Typing in hosts is fragile (R-4) and a shell child of a DAW is a new crash surface; agents edit in the app and the patch moves by file |
| UI Debugger, UI Style Editor | Out | Debug-only, `#ifndef NDEBUG` (memory `feedback_no_debug_tools_in_release`), checked by `check_release_binary.py` |

Panels (`src/app/panels/`):

| Panel | In the plugin | Change in plugin mode |
|---|---|---|
| Canvas, Minimap, Find, Notice stack | In | None |
| Viewport, 3D View | In | None |
| Mod Matrix, Performance matrix (PerfPanel) | In | MIDI learn learns from the host feed |
| Piano Roll | In | Playhead = host song position |
| Arrange (timeline) and Session grid | In | Follow host transport; own play/stop disabled; clip launch quantised to host bars |
| Sample Studio | In | Records from "Host main"; results leave as nodes or files as in the app |
| Library, Templates | In | None |
| History | In | None |
| Help windows, node help | In | Plugin help lines added to the four help tables (12.2) |
| Settings | In, cut down | Inside a DAW: no audio device/format, app & updates, DAW plugin or Plugins tab |
| Cook times overlay | In | Off by default, as in the app |
| Copy system info (Diagnostics) | In | Adds host name, host version, buffer size, plugin format |
| Terminal, Chat, CLI setup | Out | See Features |
| UI Debugger, Style Editor | Out | Debug-only |

Decision: every node, filter, effect and panel ships, minus the Audio Plugin node, Export / Render Take, device setup, in-DAW self-update, the agent Terminal / Chat, and debug tools.
Decision: changed behaviour is limited to inputs (host audio, host MIDI, host transport); no node gets a plugin-only mode.
Open risk: camera nodes inside a DAW: macOS asks the responsible process for camera access, which may be the DAW, not Infinite Engine; spike in chunk 2, and if a DAW refuses, those three trackers and Video In show "Camera not allowed in this host".

### 8.11 One release for app and plugin

Rule: the plugin never has its own version, tag, build, page, cask or update check. It is part of the app.

```
  one push to main (version bumped)                build.yml (one run)
  ───────────────────────────────── ──▶  app + Instrument/FX VST3 + AU, per OS
                                                   │
                         ship-infinite `release` ──┘  (same driver, same step)
                                  │
                                  ▼
        GitHub release vX.Y.Z (the one "latest") ── Infinite.dmg  (plugins inside the .app)
                                  │                Infinite-windows-*.zip (Plugins\ folder)
                                  │                Infinite-x86_64.AppImage + plugins tarball
                                  ▼
        user runs the app once ── Settings > DAW plugin > Install
                                  │  copies bundles to the user plugin folders
                                  ▼
        app updates (SelfUpdate) ─▶ next app launch re-copies when the version differs
                                  ▼
        DAW reloads project ─────▶ new plugin, same version as the app
```

| Topic | Decision |
|---|---|
| Version | One `INFINITE_VERSION`; app and plugin always equal |
| Tag and release | The normal `vX.Y.Z` app release; no `plugin-v*` tag (it would become GitHub "latest" and break `UpdateCheck.cpp` and the extensions catalogue URL) |
| CI | The existing `build.yml` run builds the plugin targets too; `attach-release-assets.yml` adds the plugin artifacts to its list |
| Release command | `ship-infinite` `release` step unchanged in shape: it already pulls CI binaries for the commit; it now also runs the validator + `auval` gate on the bundles it ships |
| Where the bundles live | macOS: inside `Infinite.app/Contents/Resources/Plugins/`; Windows: `Plugins\` in the zip; Linux: `Infinite-plugins-x86_64.tar.gz` next to the AppImage (an AppImage mount path changes each run) |
| Install | Settings > DAW plugin > Install copies to `~/Library/Audio/Plug-Ins/{VST3,Components}`, `%LOCALAPPDATA%\Programs\Common\VST3` (per-user, no admin), `~/.vst3`. One button, one code path, copy-then-rename |
| Update | The app's existing `SelfUpdate` replaces the app; on the next app launch the copier sees an older plugin version and re-copies. Files fetched by the app carry no quarantine flag, so updates never meet Gatekeeper |
| Locked file on Windows | A loaded DLL cannot be overwritten; the copier retries on next launch and shows "Close your DAW to finish the plugin update" |
| Update notice in the DAW | The engine runs the app's `UpdateCheck` (same code, same once-a-day, same off switch) and shows "Update in the Infinite app" in About |
| Homebrew | The existing app cask gains `vst3_plugin` and `audio_unit_plugin` stanzas; no second cask |
| winget, npm, Docker | Unchanged: they carry the app, and the app carries the plugin |
| Terminal installer | `install.sh` installs the app and runs the same copier with `--install-plugins`; no separate plugin installer |
| `/download` page | One page: "Download Infinite", then "In a DAW: Settings > DAW plugin > Install, then drop your key" |

Owner's ship work for a release that touches the plugin = the normal app release, nothing added.

Decision: one version, one tag, one CI run, one release command, one update check; the plugin rides inside the app download.
Open risk: some Windows DAWs do not scan the per-user `%LOCALAPPDATA%\Programs\Common\VST3`; the install sheet shows the folder and a "Copy path" button so the user can add it in the DAW, and the system folder (admin) is a documented manual route.

## 9. Proposed solution trajectories

### 9.1 Where the engine lives

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| **A. Out-of-process engine per instance (chosen)** | No refactor of ~500 globals; host survives engine crashes (M-12); separate GL contexts; projector windows work as in the app; same engine on 3 OSes | One block of latency; memory per instance; pixel pipe for the embedded view; sandboxed hosts may refuse | Chosen |
| B. In-process, globals refactored into an instance context | Zero added latency; native GL view in the host window | Months of refactor across every subsystem; any Infinite crash kills the DAW; GL context fights with hosts that use their own GL/Metal | Rejected for v1; becomes the plan only if A fails spike 1.0 in Logic |
| C. In-process, one shared engine for all instances in a process | Small change | Second instance either cannot exist or shares the first patch: breaks C2 | Rejected |
| D. Plugin as a remote control for the running app (Link/OSC-style) | Tiny | Not a plugin: no bounce, no state, no PDC | Rejected |

### 9.2 Other choices

| Choice | Options | Chosen | Why |
|---|---|---|---|
| AU route | `auwrapper` from the VST3 SDK / own AUv2 on AudioUnitSDK / JUCE | `auwrapper` | One implementation; no GPL/commercial framework. Fallback: own AUv2 shell on AudioUnitSDK if auval fails on something the wrapper cannot express |
| Embedded GUI | Pixel pipe (CPU copy) / IOSurface sharing (macOS) / native GL view in host | Pixel pipe first | Portable to 3 OSes, no GPU interop. IOSurface upgrade on macOS only if M-10 misses |
| Latency | One host block / synchronous wait with no latency | One block | Never blocks the host audio thread in the normal case |
| Ed25519 library | Monocypher / TweetNaCl / libsodium | Monocypher | Single file, permissive, audited, fast |
| Parameters | 32 fixed / 64 fixed / dynamic | 32 fixed | Cached-list hosts need fixed; 32 covers macro-driven patches |

### 9.3 What-if scenarios

| What if | Then |
|---|---|
| Logic hosts AUs out of process and blocks spawning | Engine spawned through `NSWorkspace` / launchd as a user agent instead of a child; if that is blocked too, AU falls to option B for Logic only, VST3 stays on A |
| GarageBand's sandbox blocks shm | GarageBand is best effort: write "not supported" on `/download`, move on |
| Pixel pipe misses M-10 on Retina | macOS: share IOSurface frames (zero copy); Windows/Linux keep the pipe and cap the view to 1x backing scale above 1600x1000 |
| Per-instance RSS makes 4 instances swap | Engine plugin mode drops caches it does not need (library thumbnails, preset previews, learned models loaded lazily); if still over, the plugin view warns at the 5th instance |
| Windows Smart App Control blocks unsigned files | Document; push a code-signing route (R-6) |
| A host only offers 4096+ blocks offline | Offline waits are unbounded; latency is still one block; no change |
| Buyers ask for refunds because of install friction | The Terminal installer exists for this; support reply template points at it; M-14 is the gate |

## 10. Benchmarking against the baseline

Filled when each chunk ends; every row points back to section 6 and 7. Spike 1.0 already shows M-1 0 failures, M-3 PASS (in and out of process), M-4 identical (two offline instances) and M-12 host lives (helper SIGKILL) on the stand-in shell; the chunk 1 column is filled with the real shell.

| Metric | Baseline (6) | Target (7) | Chunk 1 | Chunk 2 | Chunk 3 / RC |
|---|---|---|---|---|---|
| M-1 validator | B-8 | 0 failures | mac: 47/0 Instrument, 47/0 FX (`validate.sh`) | | |
| M-2 pluginval | B-8 | 5 CI / 10 RC | mac: pluginval 1.0.4 PASS at 5 and at 10, both builds | | |
| M-3 auval | B-8 | PASS | mac arm64: `auval -strict` PASS both (FX: 1 warning, Tail Time) | | |
| M-4 determinism | B-7 | identical | mac, shell level: R1 + R2 bit-identical (`plugin_shell_check --metrics`); Reaper run pending licence | | |
| M-5 bounce = live | B-7 | <= -60 dBFS | mac, shell level: R1 + R2 null exact (-inf) at 32/64/512/2048, paced realtime vs offline; under a concurrent build (load 7-11) block 32/64 realtime renders sometimes miss (null -20 to -60 dBFS), 1.7 and 2.1 shells interleaved on the same engine 6/6 PASS, so tracked as load, not state; Reaper run pending licence | | |
| M-6 latency truth | n/a | 0 samples | mac, shell level: R2 impulse offset = reported latency, 0 samples, offline and realtime, all 4 block sizes | | |
| M-7 xruns | B-4, B-5 | 0 / hour | | mac, DAW-free soak: 0 missed blocks in 60 min, 4 instances (2 R1 + 2 R2) at 48 kHz / 256, 0 host overruns (`soak.py`, `tests/plugin/soak.json`); DAW runs in 2.8 | |
| M-8 overhead | B-4 | <= 3 % core | | mac: shell + IPC worst 0.24 % of one core per instance, longest call 162 us (soak) | |
| M-9 memory | B-2 | <= B-2 + 10 % | | mac: idle engine physical footprint 185-202 MB (limit 341); summed footprint -27.7 MB while playing; worst growth +5.5 MB/hour after the `gWtTestRects` fix (soak) | |
| M-10 GUI | B-6 | 10 % core, 50 ms | mac, shell level: pixel pipe 8.8-9.8 % of one core at 30 fps, input to pixels p95 33-43 ms (20 key trials; single outliers 66-73 ms), 1600x1000 @2x; open time and DAW runs pending | mac, shell level: open cold 0.89 s, warm 0.05 s; input to pixels p95 32.7 ms; pixel pipe 9.3 % of one core (`--view`); DAW runs in 2.8 | |
| M-11 state | n/a | equal | mac, shell level: VST3 getState/setState round trip equal, restored R2 renders bit-identical, survives block-size restart and a dead engine (`plugin_shell_check` state checks); DAW reopen pending | |  |
| M-12 crash isolation | n/a | host lives | mac: helper SIGKILL, host lives (1.5 crash drill) | mac: `crash_drill.py --self`: crash Restart keeps the patch, hang killed in 3.0 s with silence, suspend releases notes; in-DAW drill in 2.8 | |
| M-13 host survival | n/a | 0 crashes | | | |
| M-14 install | n/a | <= 3 min | | | |

## 11. Critique

| Of | Weak point | What we do about it |
|---|---|---|
| The problem | It assumes DAW users want the full canvas inside a plugin window; many will want "a visual synth that just works" | The plugin view can show the Output picture only (8.2); preset patches for the plugin ship with it (existing library, no new nodes) |
| The architecture | One process per instance is heavy on 8 GB machines; competitors run in-process | Measured by M-9 with a stop condition; the crash isolation is a selling point, not only a cost |
| The architecture | One block of latency is unusual for an instrument | At 128-sample buffers it is 2.7 ms; PDC hides it in playback; documented |
| The architecture | The pixel pipe adds input lag to a canvas that needs precise dragging | M-10 gate; the pop-out window is full speed for heavy editing |
| The GUI | Keyboard focus inside hosts (Ableton, Cubase) is unreliable; Field code editing needs typing | Click-to-focus in the view; the pop-out window is the recommended place for typing; documented |
| Licensing | No enforcement means some people will never pay | Honest by design; MIT source makes enforcement theatre; the free app converts, refund window protects buyers |
| Packaging | A curl-to-bash line looks scary to some users | The script is short, readable on the site, checks a SHA-256; the zip route exists too |
| Validation | The automated rig is Reaper only; other hosts rely on a hand checklist | The checklist is a file per DAW with dates; release blocks on all v1 rows |
| Scope | MIDI out to the host and multi-out are common asks | Listed as later; the shell keeps bus slots for them |
| Results | Every number in 10 is empty | By design: baseline first (step 1.0), no moving goalposts |

## 12. Next steps

### 12.1 Milestones: three chunks, one branch each, one commit per step

Wall-clock budget assumes agent work in sessions plus owner time for DAW demo installs and the real-card test. Builds only through `scripts/ibuild.py`, one at a time; release (universal) builds only in chunk 3.

**Chunk 1: Engine in a shell (macOS, Reaper)** - branch `plugin/c1-engine-shell` - about 3 weeks, ~60 agent hours

| Step | Commit | Hours |
|---|---|---|
| 1.0 | Spike: a 200-line VST3 + AU shell spawns a helper and passes audio over shm in Reaper and Logic/GarageBand (demo versions); record B-1..B-8 with `tools/plugin/baseline.py`; freeze R1-R4 | 8 |
| 1.1 | `LICENSE` + CMake comments corrected (SDK is MIT); `THIRD_PARTY_NOTICES` adds AudioUnitSDK | 1 |
| 1.2 | `Platform::` shm, spawn, watch-parent, RT priority (mac side + Windows and Linux stubs that compile) | 6 |
| 1.3 | shm protocol + `ipc_bench` | 6 |
| 1.4 | Engine plugin mode: `HostAudioBackend`, `Transport::FollowHost`, host MIDI feed, audio-time modulation ticks, Export/Audio Plugin node disabled | 14 |
| 1.5 | VST3 shell (processor + controller, Instrument and FX builds, buses, bypass, latency, tail, 130 MIDI-mapping params, offline wait) | 10 |
| 1.6 | AU via `auwrapper` + AudioUnitSDK fetch | 5 |
| 1.7 | `validate.sh` + `reaper_rig.py`; M-1, M-3, M-4, M-5, M-6 on macOS | 10 |

Stop condition S1: **go** when R1 and R2 play in Reaper (VST3) and in Logic (AU), M-1, M-3, M-4, M-5, M-6 pass on macOS, and pluginval 5 passes. **Stop and re-plan** (switch Logic to option B, 9.1) if (a) spike 1.0 shows Logic refuses both spawn routes, (b) B-5 p99 round trip exceeds 25 % of a 128-sample block, or (c) B-2 means four instances cannot fit in 8 GB beside Reaper.

**Chunk 2: The plugin as a product (macOS)** - branch `plugin/c2-product-mac` - about 3-4 weeks, ~80 agent hours + owner DAW-install time

| Step | Commit | Hours |
|---|---|---|
| 2.1 | State save/load (header + JSON + miniz), lazy engine start, last-known-state fallback | 8 |
| 2.2 | Host slots + Macro "Host slot" field + smoothing + edits back to host | 10 |
| 2.3 | Pixel pipe: FBO, PBO readback, triple buffer, mac view blit, input injection, resize, backing scale | 16 |
| 2.4 | Pop-out window, projector outputs in plugin mode, no-GPU fallback text | 6 |
| 2.5 | Failure modes: heartbeat, crash restart, hang kill, suspend/resume; `crash_drill.py` | 8 |
| 2.6 | Licence: Monocypher, `License.*`, sheet, drop + paste, unlicensed mode (sheet only, Instrument silent, FX dry), revoked cache | 10 |
| 2.7 | `soak.py`; M-7..M-12 on macOS | 6 |
| 2.8 | DAW matrix on macOS: Logic, Ableton, Reaper, FL, Bitwig, Cubase (v1) + GarageBand, Studio One (BE); checklists filled; fixes | 12 |

Stop condition S2: **go** when every v1 macOS row of 8.8 passes C1-C10 and M-2 strictness 10 passes on macOS. **Stop** if M-10 fails twice after the IOSurface upgrade (then the embedded view shows the Output picture only and the canvas lives in the pop-out window; re-plan the GUI, owner sees it).

**Chunk 3: Windows, Linux, packaging, ship-ready** - branch `plugin/c3-crossplatform-ship` - about 3 weeks, ~55 agent hours

| Step | Commit | Hours |
|---|---|---|
| 3.1 | Windows sides of the `Platform::` functions, HWND view, MMCSS, content scale | 10 |
| 3.2 | Linux sides, X11 embed view with `IRunLoop`, rtkit | 8 |
| 3.3 | CI jobs (`ci-preflight` first, one push), artifacts for 3 OSes x arch | 6 |
| 3.4 | Windows VM run: validator, pluginval 10, Reaper rig, Ableton / FL / Bitwig / Cubase checklists | 8 |
| 3.5 | Linux container run: validator, pluginval 10, Reaper rig, Ardour | 4 |
| 3.6 | Packaging: plugins inside the app DMG/zip, Linux plugins tarball, Settings > DAW plugin copier, `install.sh --install-plugins` (SHA-256 baked), cask stanzas | 6 |
| 3.7 | `website/download.html`, plugin help page, FAQ (Gatekeeper, SmartScreen, seats, refunds), brand lint | 5 |
| 3.8 | Ship tooling: D1 column `download_emailed_at`, `tools/license/notify_preorders.mjs` (dry-run count, then send, idempotent, logs per id), Worker test extended | 5 |
| 3.9 | `install_check.sh` on all routes; M-14 timed with one person | 3 |

Stop condition S3: **go** (ready for the owner's release call) when CI is green on 3 OSes for the exact commit, M-1..M-14 are filled in section 10 and pass, every v1 row of 8.8 has a dated pass on its OS, and one real x64 Windows PC run passed. **Stop** if SignPath refuses the plugin and Smart App Control blocks it on the test PC: ship Windows as "unsigned, Smart App Control must be off" only after the owner reads that line.

Total: about 9-11 weeks wall clock, ~185 agent hours.

### 12.2 Ship day checklist (only when the owner says go)

| When | Step | Who |
|---|---|---|
| T-7 days | Real card purchase on live Paddle with LAUNCH79, key arrives, drops into the plugin and verifies | Owner (an agent never enters card details) |
| T-7 days | Refund that purchase in Paddle; check the id appears in `GET /api/license/revoked`; plugin shows "This key was refunded" after a cache clear | Owner + agent checks |
| T-5 days | Real x64 Windows PC run (8.8) | Owner or beta tester |
| T-3 days | `release-notes-audit`; plugin help text in the four help tables (`project_node_help_coverage`) | Agent |
| T-1 day | Bump `INFINITE_VERSION` in the same push that starts the release CI run (`feedback_bump_version_before_binary_ci`); CI green for that exact commit (`ci-preflight`) | Agent |
| Day | Normal app release through `ship-infinite` (tag `vX.Y.Z`); the plugin is inside the app assets (mac `Contents/Resources/Plugins`, Windows zip `Plugins\`, AppImage `usr/share/infinite/Plugins`); SHA-256 list | Agent, owner says go |
| Day | Worker: `RELEASED = "1"`, `DOWNLOAD_URL = "https://infinitedevices.org/#download"` in `wrangler.toml`; `npx wrangler deploy` (one-time before: the `download_emailed_at` ALTER in `schema.sql`, `wrangler secret put ADMIN_TOKEN`) | Agent |
| Day | `website/paddle.json` `"released": true`; buy page copy from pre-order to buy ("macOS first, Windows and Linux to follow" and "Pre-order" lines); link `plugin.html` from `index.html#download`; push | Agent |
| Day | `node tools/license/notify_preorders.mjs` (dry run: count only, no addresses printed), then `--send`; `download_emailed_at` set per licence: **this starts each buyer's 14-day refund window** | Agent runs, owner approves the send in chat |
| Day | App cask in `n1m21n/homebrew-infinite` from `tools/spread/pkg/homebrew/Casks/infinite.rb`: `uninstall delete:` for the copied plugins and the install caveat. No `vst3_plugin` / `audio_unit_plugin` stanzas: they would fight the app's own copier and its update record (3.6) | Agent |
| Due with the next app release (memory `remind-release-publish-steps`) | Bump `tools/npm/infinite-engine/package.json` and `server.json` together -> `npm publish` (0.6.1) -> `mcp-publisher login github` + `mcp-publisher publish`; bump the app cask in the tap; winget manifest PR; Docker Hub image if set up | Owner confirms, agent runs |
| Day +1 | Check the Worker logs for failed mails; resend through `POST /api/license/resend` | Agent |

Support plan:

| Channel | Use |
|---|---|
| Email (the Worker's `FROM` address) | Licence, refund, install problems; answer within 48 h |
| Discord, a `#plugin` channel | Community help, bug reports |
| `plugin.html` FAQ | Gatekeeper / Open Anyway, SmartScreen, "two computers", key lost (use `/api/license/resend`), refund window |
| Bug intake | GitHub issues labelled `plugin`; each crash goes through `bug-blast-radius`; DAW-specific reports are added to that DAW's checklist file |
| Refunds | Paddle dashboard; the webhook revokes automatically; within 14 days of `download_emailed_at` always granted |
| First two weeks | Daily triage of email + issues; any host crash is a release-blocking fix for a 1.x.1 |

### 12.3 Risks and decisions

| # | Risk | Decision |
|---|---|---|
| R-1 | Sandboxed or out-of-process AU hosting in Logic/GarageBand blocks the helper | Spike 1.0 first; fallbacks in 9.3; GarageBand is best effort |
| R-2 | Per-instance memory on 8 GB machines | M-9 gate; lean plugin mode; warn at the 5th instance |
| R-3 | Pixel pipe lag or CPU | M-10 gate; IOSurface on macOS; pop-out window for heavy editing |
| R-4 | Keyboard focus in hosts | Click-to-focus; pop-out window for typing; documented |
| R-5 | Gatekeeper friction without notarization | Terminal installer primary; zip + Open Anyway; Homebrew cask; M-14 gate |
| R-6 | Windows signing: SignPath Foundation may not cover a paid binary; Smart App Control blocks unsigned files | Check SignPath terms in 3.6; if refused, ship unsigned zip with the documented limitation; a paid certificate is an owner spending call after launch sales |
| R-7 | Hosts caching parameter lists | Fixed 128 slots and fixed IDs forever, hidden until bound |
| R-8 | VST3 CC only through parameters | 130 hidden omni MIDI-mapping params; per-channel later |
| R-9 | Bounce differs from live for wall-clock patches | Audio-time modulation clock; M-5 measured on synced patches; documented |
| R-10 | Engine hang during `getState` loses the newest edit | Last-known-state fallback; heartbeat kills hung engines |
| R-11 | Key leaks or is shared | Accepted: the key is a convenience; revocation only for refunds |
| R-12 | DAW demos expire mid-test | Install each at step 2.8 / 3.4, not before |
| R-13 | No Windows x64 hardware | UTM ARM VM for the bulk; one real x64 PC run before ship |
| R-14 | Refund wave if first impressions are bad | Ship only at S3 go; daily triage for two weeks |
| R-15 | `LICENSE` claims GPLv3 while the SDK is MIT | Corrected in 1.1; the honest licensing paragraph in 8.5 goes on `/download` |
| R-16 | Both laptops: only the Pro merges and pushes | Chunks run on the Pro; the Air may take the website step 3.7 on an `air/` branch |
| R-17 | App download grows by the plugin bundles | Accepted; bundles share the engine binary build, measured at step 3.6 and noted in the release notes |
| R-18 | Camera nodes refused by a host's privacy settings | Spike in chunk 2; message on the node, no crash (8.10) |

### 12.4 Owner approval needed before these steps are built (new user-visible behaviour)

| Item | Step | Recommended |
|---|---|---|
| "Host slot" field on Macro nodes | 2.2 | **Approved 2026-11-07** (owner: change nodes and settings wherever required) |
| Licence sheet; unlicensed = sheet only, Instrument silent, FX dry; no trial | 2.6 | **Decided by owner 2026-11-07** (8.5) |
| Plugin view chrome: picture/canvas switch, pop-out button, slot map, xrun counter, "Engine stopped, Restart" | 2.3-2.5 | **Approved 2026-11-08**: in Infinite's own top bar (switch, Slots, xruns only when > 0, pop-out); the shell draws natively only with no frames ("Starting" / "Engine stopped" + Restart). **Built** 2.3-2.5 |
| `Audio In` sources "Host main" / "Host sidechain" in plugin mode | 1.4 | **Approved 2026-11-07** |
| `/download` page and FAQ copy | 3.7 | Approve after brand lint |
| Settings > DAW plugin section (Install button, folder paths, re-copy on update) in the app | 3.6 | **Approved 2026-11-07** |
| Spending: DAW licences beyond demos (Reaper $60, FL for M-11), any Windows certificate, Avid/PACE for AAX | 2.8, 3.6, chunk 4 | Reaper licence yes; FL when its matrix step starts; certificate after launch sales |

### 12.5 Ship / iterate / abandon

**Iterate into chunk 1**, starting with spike 1.0, as soon as the owner approves this plan. The plan is abandoned only if S1 fails on both the out-of-process route and the in-process fallback for Logic, in which case the honest move is a VST3-only release and an offer of refunds to buyers who need Logic.
