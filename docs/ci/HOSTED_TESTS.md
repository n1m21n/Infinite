# Hosted tests, goldens and release assets (2.0 gap audit items 1 to 7)

What runs where, what does not, and what has to happen at release time. Each row names the workflow
or script that owns it. Nothing here has run on a hosted runner yet: the "needs a CI run" column says what
the first green run must prove.

## 1. Release assets (attach-release-assets.yml)

`verify-assets` in `.claude/skills/ship-infinite/driver.sh` requires `Infinite.dmg` and `Infinite.pkg`.
No Build job makes them (the DMG and PKG come from `package.sh`, universal and ad-hoc signed), so:

- `.github/workflows/macos-installers.yml` (manual) runs `./package.sh` on `macos-15` and uploads the
  artifact `Infinite-macos-installers` (`Infinite.dmg`, `Infinite.pkg`, 90 days).
- `attach-release-assets.yml` takes a new required input `mac_run_id` (the macos-installers run), downloads
  that artifact, checks both files are non-empty and uploads them with the other assets.

Needs a CI run: a macos-installers run, then attach with its id. `package.sh` has never run on a hosted
runner; if it fails there, the owner's local `driver.sh` release is the fallback (it uploads the same files).

## 2. macOS auval (build.yml, macos job)

"Audio Unit validation (auval)" builds `InfiniteInstrument_au` and `InfiniteFX_au`, then runs
`tools/plugin/validate.sh --build build-plugin --skip-pluginval --licensed`, which runs the M-3 auval section.
If no `build-plugin/AU/*.component` exists it prints `SKIP` and exits 0. Logs go to the
`plugin-logs-macos` artifact.

Needs a CI run: auval against the hosted-built component (AU registration on a hosted runner is unproven).

## 3. Pillar-parity regex (pillar-parity-audit/static-checks.sh)

The declaration scan read every line of `src/platform/Platform.h`, so the members of the header struct
`TextCurveBuilder` (`Move`, `Line`, `Cubic`, `Quad`, `Close`) looked like portable `Platform::` functions and
failed the macOS-definition check. The scan now reads only namespace-scope lines (brace depth 1). Locally the
static lane passes the declaration checks with 219 portable declarations and no struct members.

## 4. Test suites on hosted runners

| Suite | Where it runs | Why / why not |
|---|---|---|
| `tests/platform/single_instance_test.cpp` | build.yml `hosted-suites` (ubuntu) | Standalone C++17, no app build. Passes locally. |
| `tests/website` (ui_audit, ui_scale, ui_type, ui_contrast) | not in build.yml | The website is its own product; run these by hand from tests/website when changing the site. |
| `tests/chat` (find evals) | build.yml `linux` job (clang), step "Chat finder evals" | Needs the built `find-eval` target, which the Linux build compiles. Gate thresholds are the 2026-11-05 scores in `src/core/chat/FindEval.cpp`. |
| `tests/plugin` (daw checklists, soak, baseline) | not in CI | The soak and baseline need a built plugin and a real audio device; the DAW runs are manual by design (`tools/plugin/daw_kit.sh`). The plugin build and validate steps already run in CI (pluginval, auval). |

Needs a CI run: `hosted-suites` and the chat thresholds
(`find_nodes` at 254 is the current floor; the release doc records 254/312 on a stale binary).

## 5. Homebrew cask and winget (update-package-managers.yml, tools/spread/pkg)

- Both the local mirrors are now at 2.0.0 (`tools/spread/pkg/homebrew/Casks/infinite.rb`,
  `tools/spread/pkg/winget/manifests/n/n1m21n/Infinite/2.0.0/`).
- The sha256 values are zero placeholders, marked `PLACEHOLDER` in the cask. Nothing commits them as real.
- Release-time fill, verified by reading the workflow:
  - **Homebrew tap**: the `homebrew` job downloads `Infinite.dmg` from the published release, computes its
    sha256 and rewrites `version` and `sha256` in the tap's `Casks/infinite.rb` (skipped without `TAP_TOKEN`).
  - **winget**: `vedantmgoyal9/winget-releaser@v2` downloads the installers matching
    `Infinite-windows-(x64|ARM64)\.zip$` and computes their hashes itself (skipped without `WINGET_TOKEN`).
  - **Local mirrors**: `python3 tools/spread/pkg/bump.py v2.0.0` downloads the three release assets, writes the
    real hashes into both mirrors and sets the winget `ReleaseDate`. It submits nothing.
- `ReleaseDate` is left out of the 2.0.0 installer manifest; `bump.py` sets it.

Needs a CI run or a release: the tap fill (`TAP_TOKEN`) and the winget PR (`WINGET_TOKEN`) only run on a
published release. Before that, run `bump.py v2.0.0` after attaching the assets.

## 6. npm infinite-engine 2.0.0

Bumped from 1.0.1 because the library surface changed: three MCP tools were added (`infinite_compare_frames`,
`infinite_find_knob`, `infinite_node_effects`), so the server now exposes twelve tools. Changed in
`package.json`, `server.json` (both the server and package version), and the `serverInfo` version in
`lib/server.js`. `npm test` passes (9 pass, 1 skipped: the app-dependent case runs only with `INFINITE_BIN`).
`publish-packages.yml` is manual and has no version logic: publish after this lands on `main`.

## 7. Windows and Linux UI goldens

- Generation already runs in CI: the Linux job (clang) renders `gallery-light.png` and `gallery-dark.png` under
  Xvfb and uploads `ui-golden-linux`; the Windows job renders the same gallery and uploads `ui-golden-windows`.
  Both are uploaded with `if: always()`.
- Nothing is compared until a golden is committed. The new "Note when ... not committed" steps print a
  `::warning::` in each run until then, and `attach-release-assets.yml` skips a platform with no committed golden.
- The Windows step is informational (`continue-on-error`) because the hosted Windows runner has no GPU; it may
  produce no PNGs. If so, the Windows golden must come from a Windows machine that has a GPU (see
  `docs/WINDOWS_VERIFICATION.md`).

**Adopting a golden** (after a green run that produced the artifact):

```
gh run list --workflow build.yml --branch release/2.0-preci --limit 5
tools/design/adopt_golden.sh <run-id> linux
tools/design/adopt_golden.sh <run-id> windows   # only if the artifact has PNGs
git add tests/ui-golden && git commit -m "UI goldens: linux from run <run-id>" -m "Co-Authored-By: Claude Haiku 5.5 <noreply@anthropic.com>"
```

Review the PNGs before committing them. Goldens are per platform because text rasterisation differs by OS.

Needs a CI run: the linux and windows gallery renders, and a decision on whether the Windows render works on the
hosted runner at all.

## Attribution and push

Nothing in this document pushes, tags or uploads anything. The commands above are for the release owner.
