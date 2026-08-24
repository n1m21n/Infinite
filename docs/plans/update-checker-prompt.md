# Implement an in-app update checker for Infinite

Add a background check that compares the running build's version against the
latest GitHub Release, shows an unobtrusive badge in the menu bar when a newer
version exists, and opens the website's download section when clicked.

Everything below was verified against `origin/main` before this prompt was
written — file paths, line numbers, and existing patterns are real, not
guesses. Line numbers are from `origin/main` at the time of writing; if a
region has moved, the named function/pattern still identifies it.

Start with:

```bash
git checkout main && git pull && git checkout -b feature/update-checker
```

---

## Background: what exists and what doesn't

**There is no HTTP client and no open-URL call anywhere in this codebase.**
Confirmed by grep across `src/`. `RemoteControl.cpp` and `OscNodes.cpp` use
raw BSD sockets via `src/platform/NetCompat.h`, with no TLS — they cannot be
reused for an HTTPS request. Both capabilities have to be added to the
`Platform` namespace as native per-platform implementations.

**Both platforms already link what's needed except one library:**
- macOS: `AppKit` is already in the frameworks list (`CMakeLists.txt`, in the
  `-framework` block around line 332) → `NSWorkspace` and `NSURLSession` are
  available with no CMake change.
- Windows: `shell32` is already linked (`CMakeLists.txt:378-379`) →
  `ShellExecuteW` is free. **`winhttp` is not linked and must be added.**

**`nlohmann::json` is vendored** at `external/json` and already used in
`src/core/PatchJson.cpp` (`using json = nlohmann::json;`). Use it to parse
the API response — do not hand-roll string scanning.

**The version number is currently duplicated and has already drifted** — see
item 1. This is a real pre-existing bug, not a hypothetical.

---

## 1. Single source of truth for the version string

**Confirmed problem.** Three places disagree today:

| Source | Value |
|---|---|
| `CMakeLists.txt:602-603` (`MACOSX_BUNDLE_BUNDLE_VERSION`, `MACOSX_BUNDLE_SHORT_VERSION_STRING`) | `"0.1"` |
| `src/platform/win/Infinite.rc:17-18` (`INFINITE_VERSION`, `INFINITE_VERSION_STR`) | `0,2,0,0` / `"0.2.0.0"` |
| Latest GitHub Release tag (`gh release list`) | `v0.2-preview` |

An update checker is meaningless while the baked-in version is wrong, so fix
this first.

Do this:

- Near the top of `CMakeLists.txt` (right after `project(Infinite ...)` on
  line 15), add `set(INFINITE_VERSION "0.2.0")`.
- Drive the macOS bundle properties from it — replace the two literal `"0.1"`
  values at `CMakeLists.txt:602-603` with `"${INFINITE_VERSION}"`.
- Expose it to C++:
  `target_compile_definitions(Infinite PRIVATE INFINITE_VERSION_STRING="${INFINITE_VERSION}")`.
- For the Windows resource: rename `src/platform/win/Infinite.rc` to
  `Infinite.rc.in`, replace the two `#define` values on lines 17-18 with
  CMake substitutions, and `configure_file` it into the build dir, pointing
  the existing `.rc` reference (around `CMakeLists.txt:301`) at the generated
  file. `INFINITE_VERSION` is three components but `VERSIONINFO` wants four —
  generate the comma form as `${INFINITE_VERSION}.0` split appropriately, or
  keep a separate `set(INFINITE_VERSION_RC "0,2,0,0")` beside it with a
  comment that the two must be bumped together. Your call; the important part
  is that no *other* file carries a hardcoded version any more.
- Add a fallback in the C++ so a build without the define still compiles:
  ```cpp
  #ifndef INFINITE_VERSION_STRING
  #define INFINITE_VERSION_STRING "0.0.0"
  #endif
  ```

Set the initial value to `0.2.0` — matching the shipped `v0.2-preview`
release, so existing users of that build correctly read as up to date.

---

## 2. `Platform::OpenExternalUrl` — open a URL in the default browser

Declare in `src/platform/Platform.h`, at the end of the namespace (the file
is 737 lines; append before the closing `}`), with a short comment matching
the house style of the surrounding declarations:

```cpp
// Hands a URL to the OS's default browser. Fire-and-forget: there is no
// success callback, and a malformed or non-http(s) URL is dropped rather
// than passed through, so this can never be used to launch a local
// executable by way of a file:// or shell URL.
void OpenExternalUrl(const std::string& url);
```

Implement twice:
- `src/platform/Platform.mm` — `[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:...]]`.
- `src/platform/win/PlatformWin.cpp` — `ShellExecuteW(nullptr, L"open", wideUrl, nullptr, nullptr, SW_SHOWNORMAL)`.
  Note this file already has a `namespace Platform` block starting at line 203.

**Both implementations must reject anything that doesn't start with
`https://` or `http://`.** Only fixed compile-time URLs are passed today, but
this function is the kind of thing that later gets called with a string from
a config file, and `ShellExecuteW` on an arbitrary string is a code-execution
primitive. Guard at the bottom, not at the call site.

---

## 3. `Platform::HttpGet` — a blocking HTTPS GET

Declare in `src/platform/Platform.h` alongside the above:

```cpp
// Blocking HTTPS GET. Call from a worker thread, never the render or audio
// thread. Returns false on any transport, TLS, or non-2xx failure and fills
// outError; outBody is only valid when it returns true. Bounded by
// timeoutSeconds and a hard response-size cap so a hung or hostile endpoint
// can't stall or balloon the caller.
bool HttpGet(const std::string& url, const std::string& userAgent,
             std::string& outBody, std::string& outError,
             int timeoutSeconds = 10);
```

- **macOS** (`Platform.mm`): `NSURLSession` `dataTaskWithRequest:` driven to
  completion with a `dispatch_semaphore_t`, since the contract is blocking.
  Set the `User-Agent` header from the parameter and
  `timeoutIntervalForRequest`. Check `((NSHTTPURLResponse*)response).statusCode`
  is 2xx.
- **Windows** (`PlatformWin.cpp`): `WinHttpOpen` / `WinHttpConnect` /
  `WinHttpOpenRequest` (with `WINHTTP_FLAG_SECURE`) / `WinHttpSendRequest` /
  `WinHttpReceiveResponse`, then loop `WinHttpQueryDataAvailable` +
  `WinHttpReadData`. Set timeouts with `WinHttpSetTimeouts`. Query the status
  code with `WinHttpQueryHeaders(WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER)`.
  Every handle needs `WinHttpCloseHandle` on every exit path — use a small
  RAII wrapper rather than goto-cleanup.
- Cap the accumulated body at something like 1 MB and fail past that.
- Add `winhttp` to the Windows `target_link_libraries` list at
  `CMakeLists.txt:378-388`, with a comment in the style of the ones already
  there explaining what it's for.

---

## 4. The update checker itself — new `src/core/UpdateCheck.{h,cpp}`

**Mirror the threading shape of `PluginScanner`** — it is the established
pattern in this codebase for "background worker hands a result to the main
thread". See `src/audio/PluginScanner.h:71-77`: a `std::thread`, a
`std::mutex` guarding the pending result, `std::atomic<bool> mResultReady`,
and a `PollResults()` that the main thread calls once per frame using
`std::unique_lock(mutex, std::try_to_lock)` so it never blocks the render
loop (`PluginScanner.cpp:141`).

API, roughly:

```cpp
namespace UpdateCheck
{
   // Spawns the worker. Safe to call once at startup; a second call while
   // one is in flight is a no-op.
   void Start();
   // Main thread, once a frame. Cheap; try_lock, never blocks.
   void Poll();
   // True once Poll() has seen a result reporting a newer version that the
   // user hasn't dismissed.
   bool UpdateAvailable();
   // Empty until UpdateAvailable() is true.
   const std::string& LatestVersion();
   // Writes the current latest version into the dismissal pref so the badge
   // stays hidden until a newer one appears.
   void Dismiss();
   // Joins the worker if running. Call before the process exits.
   void Shutdown();
}
```

### Endpoint

`https://api.github.com/repos/n1m21n/Infinite/releases/latest`

Two things that will silently break this if missed:

- **GitHub returns 403 for requests with no `User-Agent`.** Pass something
  like `"Infinite/" INFINITE_VERSION_STRING`.
- Unauthenticated requests are rate-limited to 60/hour per IP. Once per
  launch is well within that, but do not add a retry loop or a polling timer.

Parse `tag_name` out of the response with `nlohmann::json`, wrapped in a
`try`/`catch` — a rate-limit or error response is valid JSON with a totally
different shape, and must be treated as "no result", not as a crash.

### Version comparison — do NOT use strict semver

The real tags are `v0.1` and `v0.2-preview`. Under strict semver rules
`0.2-preview` is a *prerelease of* `0.2` and therefore **older** than `0.2`,
which would give exactly the wrong answer for the currently-shipped release.

Use this instead:

1. Strip a leading `v`.
2. Truncate at the first character that is neither a digit nor `.` (so
   `0.2-preview` → `0.2`).
3. Split on `.` into numeric components.
4. Compare component-wise left to right, treating a missing component as 0.

Check these cases explicitly:

| Local | Remote tag | Expected |
|---|---|---|
| `0.2.0` | `v0.2-preview` | no update (this is the shipped state today) |
| `0.1` | `v0.2-preview` | update available |
| `0.2.0` | `v0.3` | update available |
| `0.3.0` | `v0.2-preview` | no update (local ahead — dev builds) |
| `0.2.0` | garbage / missing | no update, no error UI |

### Dismissal persistence

Follow the flat-file preference pattern already used for the theme —
`src/core/CategoryColors.cpp:255-259`, whose comment reads: *one flat
preference file next to the app's other Application Support state, not a
bundled settings format.*

Use `AppPaths::AppSupportDir()` (`src/platform/AppPaths.h`) +
`"/Infinite.update"`, holding a single line: the version string the user last
dismissed. On load, suppress the badge only if the dismissed version is
greater than or equal to the fetched one, so a *newer* release re-shows it.

### When not to run

The check must not fire during the self-test suite or CI — those run headless
and repeatedly, and a network call there is both slow and noise.

`src/main.cpp` gates behavior on a large family of `getenv("INFINITE_*TEST")`
variables plus `getenv("IMAGERESYNTH_SELFTEST")`. Rather than enumerating
them, have `Start()` return immediately when **either** of these is true:
- `getenv("INFINITE_NO_UPDATE_CHECK") != nullptr`
- `getenv("IMAGERESYNTH_SELFTEST") != nullptr`

and add `export INFINITE_NO_UPDATE_CHECK=1` near the top of
`.github/scripts/headless-tests.sh` and
`.claude/skills/run-infinite-hygiene/driver.sh`. The env var also gives users
a documented opt-out.

---

## 5. Wire it into `src/main.cpp`

Three sites, all confirmed:

**a) Start it at launch.** Put `UpdateCheck::Start();` near the other
startup-time background work — `gSampleScanner.LoadFromDisk()` /
`gPluginScanner.LoadFromDisk()` around `src/main.cpp:26026-26033`.

**b) Poll once a frame.** The frame loop begins at `src/main.cpp:27634`.
`PollAudioRecovery()` is called at line 27653 with the comment *"once a
frame, main thread only"* — put `UpdateCheck::Poll();` immediately after it,
in the same style.

**c) Draw the badge in the menu bar.** The right-aligned menu-bar items are
laid out at `src/main.cpp:28780-28857` as a right-to-left chain, each
position computed by subtracting the next item's width plus a gap:

```
readoutX (fps)  <-  searchX (search button)  <-  audioX (audio readout)
```

Add one more link to the left of `audioX`, following the same shape:

```cpp
const float updateWidth = ImGui::CalcTextSize("update available").x + ...;
const float updateX = audioX - updateWidth - itemGap;
```

Render it **only** when `UpdateCheck::UpdateAvailable()` — the slot collapses
entirely otherwise, so the common case costs nothing visually. Note the
existing code deliberately reserves width from a **worst-case template
string** rather than the live text (see the comment at 28780-28788) so items
don't jitter; the update badge is either present or absent rather than
changing width, so it doesn't need that treatment, but don't disturb the
existing reservations.

Make it a clickable `ImGui::Button` (or `Selectable`) in the accent color —
the existing palette here uses `ImVec4(0.95f, 0.75f, 0.35f, 1.0f)` for the
"attention, not an error" amber. On click:

```cpp
Platform::OpenExternalUrl("https://n1m21n.github.io/Infinite/#download");
```

That anchor is real — `<section id="download" class="section">` at
`website/index.html:423`.

Add a hover tooltip along the lines of
`"version X is available (you have Y) - click to download"`, using the
existing `ImGui::IsItemHovered()` / `ImGui::SetTooltip` pattern from the
audio readout right above it at 28833-28836.

Right-click (or a small `x`) should call `UpdateCheck::Dismiss()`.

**d) A menu item too.** `src/main.cpp:28613` has
`ImGui::MenuItem("Help / module reference")` followed by a separator and
`Quit` at 28617. Add a `"Check for updates"` item beside it that calls
`UpdateCheck::Start()` again — a manual re-check for anyone who dismissed the
badge or has been running the app for weeks.

**e) Shutdown.** Call `UpdateCheck::Shutdown()` alongside the other teardown
at the end of `main()` so the process doesn't exit with a detached thread
mid-request. Look at how `RemoteControl::Start(controlPort)`
(`src/main.cpp:26095`) is torn down and mirror it.

---

## 6. Add the new files to the build

`src/core/UpdateCheck.cpp` needs to go into the `Infinite` target's source
list in `CMakeLists.txt`, next to the other `src/core/*.cpp` entries.

---

## Design decisions already made (don't re-litigate)

- **Native HTTP per platform, not a vendored HTTP/TLS library.** Adding
  libcurl or OpenSSL to a project that currently vendors only header-only
  dependencies is a much larger change than the feature justifies, and both
  target OSes ship a perfectly good HTTPS client.
- **GitHub Releases API, not a version file on the website.** The release tag
  is already the thing `ship-infinite` treats as canonical (`driver.sh`'s
  `step_release` uploads assets to `gh release view --json tagName`), so it
  can't drift from what's actually published the way a hand-edited JSON file
  on the site would.
- **Click goes to the website's `#download`, not the GitHub Releases page.**
  This is what was asked for, and the site's download card is the friendlier
  landing spot.

## One thing to raise with the user before shipping

This makes Infinite contact a remote server at launch without asking. It's a
single unauthenticated GET carrying no user data — but the app has never made
an outbound connection before, so it should be documented rather than
silent. Add a line to `README.md` stating that Infinite checks GitHub for a
newer release on launch, that it sends nothing but a version string in the
User-Agent, and that `INFINITE_NO_UPDATE_CHECK=1` disables it. Flag to the
user whether they'd also like a first-run opt-in prompt instead of it being
on by default.

---

## Out of scope

- **No auto-download and no auto-install.** The badge links out to a browser;
  it never fetches or replaces the app bundle. In-place self-update on macOS
  means dealing with codesigning and quarantine, which is a much bigger piece
  of work.
- **Don't touch `Infinite_Node_Reference_Manual.pdf`** — it's hand-maintained
  and this change adds no nodes.
- **Don't bump the release tag or run `ship-infinite`.** This branch adds the
  mechanism; cutting the release that carries it is a separate step.

---

## Verify before considering it done

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean, not just "look right".

Then:

1. Run the self-test suite and confirm it still passes and makes no network
   call: `.claude/skills/run-infinite-hygiene/driver.sh`
2. Temporarily set `INFINITE_VERSION` to `0.0.1`, rebuild, launch, and
   confirm the badge appears and the click opens the download section in a
   browser.
3. Set it back to `0.2.0`, rebuild, and confirm the badge does **not** appear
   against the live `v0.2-preview` tag — this is the case naive semver gets
   wrong.
4. Confirm launching with no network (turn off Wi-Fi) shows no badge, no
   error dialog, no hang at startup, and no stall on quit.
5. Confirm `INFINITE_NO_UPDATE_CHECK=1 ./build/Infinite.app/Contents/MacOS/Infinite`
   never spawns the worker.

The Windows half cannot be built or tested on this machine — it's built by
`.github/workflows/build.yml` in CI. Make sure the Windows code compiles
there (push the branch and check the run) rather than assuming; treat any
WinHTTP handle-leak or missing-`winhttp`-link error as a real failure to fix,
not a CI quirk.
