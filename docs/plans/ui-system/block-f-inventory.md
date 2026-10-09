# Block F: errors, recovery, busy states, diagnostics

Wording rule used everywhere: **what happened, what is safe, what to do next**, in that order, calm, no jargon.
Error = red + `Error` glyph, warning = amber + `Warning` glyph; colour is never the only cue.

Shared pieces added in this block:

| Piece | Where | Use |
|---|---|---|
| Notice queue (toast) | `src/core/Notices.h/.cpp`, drawn by `src/app/panels/NoticeStack.cpp` | Anything that happened that is not tied to one node. Same key replaces, errors stay until dismissed, warnings/info expire. Errors and warnings are also written to `log.txt`. |
| Node badge | `NodeIssue` (`src/core/NodeIssue.h`), `INode::Issue()`/`Relink()`, `NodeHeader::IssueBadge`, click handled by `RunNodeIssueFix` in `StageNodeBodies.cpp` | Anything tied to one node. Sits on the title row, so it shows at every zoom and on collapsed nodes. |
| Busy line | `components/BusyLine.h` | Spinner (still ring under Reduce motion) + what is happening + count or progress + Cancel only where the task can stop. |
| Diagnostics | Menu > Copy system info, Menu > Reveal logs; `Diagnostics.cpp`, `Platform::RevealLogs()` on macOS, Windows, Linux | Nothing is sent anywhere. |

## State inventory

| State | Surfaced before | Wording before | Now |
|---|---|---|---|
| Patch fails to open: corrupt / not a patch / empty | `gPatchStatus`, shown only inside the File menu | "Open failed: not an Infinite patch" | Error notice: "X isn't a patch Infinite can read" + "Your current patch is untouched. ... try a backup or the autosave." |
| Patch from a newer version | same | "Open failed: patch was written by a newer version of Infinite" | Error notice: "X was made with a newer Infinite" + "Your current patch is untouched. Update Infinite to open this one." |
| Patch names a node type this build lacks | `stderr` only (silent) | none | Warning notice: "Opened X without N nodes" + which types, the rest is intact, Save As keeps the original whole. |
| Save fails | `gPatchStatus` only | "Save failed: ..." | Error notice with the reason, "still open and unchanged", disk/folder hint, Save As. |
| Autosave unreadable | `gPatchStatus` only | "Autosave found but could not be read" | Warning notice: left on disk, saved patches unaffected. |
| Crash recovery | Modal "Recover Autosave" (exists) | "Infinite closed unexpectedly." | Same modal, reworded: saved automatically, Recover or Discard, saved patch files untouched. |
| Plugin missing / did not load / crashed | Node body status line only | plugin status text | Node badge (error), hover says which plugin and that settings are kept, click opens the plugin list. |
| Media file missing | Node body text, tiny | raw loader error | Node badge: warning when the file is gone ("patch keeps its path"), error when unreadable; click = Relink (file dialog). |
| Audio device lost / start failed / gave up after retries | Red dot in the top bar + tooltip | raw `gAudioStartError` | Error notice "Audio isn't running" + reason + patch unaffected + "Open audio settings"; clears itself when audio is back. |
| Audio input missing / permission refused | Status text on the Audio In body | raw status | Node badge (warning) on Audio In, rest of the patch keeps running, click opens settings. |
| Sample-rate mismatch | Handled by `AUDIORECOVERYTEST` logic | no wording | Covered by the audio notice when the engine gives up; no separate wording needed (the engine re-prepares at the new rate). |
| Camera permission denied | Body text on Video In | raw error | Node badge (error): camera access is off, where to turn it on, patch unchanged. Not-started camera = warning badge. |
| Screen-recording permission | No capture node in this build reads it | n/a | Not applicable in this build; add through `NodeIssue` when a screen source exists. |
| Field compile error | Body text under the editor button | raw `line N, col M: ...` | Node badge (error): "This Field can't run yet: ..." click = find the node. |
| Export failure (render queue) | Modal "Render failed" with raw encoder text | "The timeline render did not start." | Modal reworded ("stopped before it finished, patch not changed") plus a next step picked from the reason: disk full / codec missing / folder not writable. |

## Long tasks

Flag: anything that holds the UI thread for more than about 100 ms. Findings are from reading the code; they are not timed.

| Task | Where it runs | Verdict |
|---|---|---|
| Sample folder scan | worker thread (`SampleScanner`) | Not blocking. Shows a count; cannot be cancelled. |
| Plugin scan | worker thread (`PluginScanner`, child process) | Not blocking. Shows a count; cannot be cancelled. |
| Plugin load | async handle, `IsLoading()` | Not blocking. |
| Patch load | UI thread (`ApplyPatchData`) | Finding for large patches; timed by `ScopedPerfTimer("ApplyPatchData")` in the Perf panel. No progress shown. |
| Image / model / video decode on import | UI thread (see `codebase-navigation`) | Finding: large files freeze the frame while decoding. Needs a worker plus a placeholder; not done here. |
| Field compile | UI thread inside `Apply()` | Finding for large programs; small ones are well under 100 ms. |
| Export / offline render | frame-pumped job with its own progress row in the render queue | Not frozen; already cancellable. |

BusyLine replaces the plain "scanning..." text in the plugin and sample panels. The three UI-thread findings are listed in the final report as remaining work because the fix is a worker-thread decode path, which is larger than a UI block.
