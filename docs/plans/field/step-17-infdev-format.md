# Field build step 17 — `.infdev` device file format (save / load / export / import)

You are implementing **build step 17 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). Self-
contained brief; no prior context assumed beyond "Files to read first".
Line numbers are from `src/` at the commit this was written against
(`feature/field-step-15-fieldgraph-encapsulation` tip, commit `f59820c`) —
re-grep the symbol if a number has drifted; the symbol wins.

**Prerequisite:** none. This step is independent of step 15/16's
`FieldGraphNode` encapsulation work and of step 18 (device faceplate mode,
its companion doc). See §0.3 for the sequencing argument.

**Origin:** this doc rewrites an owner-supplied informal brief. The brief's
claims were checked against the actual code before writing anything below;
§0 documents every place the brief did not match reality and why the plan
here differs.

---

## 0. What the brief got right, and what it didn't

### 0.1 Verified true

- `FieldElementNode`, `FieldPixelNode`, and `FormulaNode` ship hardcoded
  C++ `static` preset tables today, with no user save/load path:
  - `FieldElementNode::Presets()` — `src/nodes/FieldElementNode.cpp:8-19`,
    5 presets, `LoadPreset(int)` at `:31-38`.
  - `FieldPixelNode::Presets()` — `src/nodes/FieldPixelNode.cpp:94-102`
    (declared `:66`, defined near `:94`), 4 presets.
  - `FormulaNode::BuildPresetNames()` — `src/nodes/FormulaNode.cpp:350-363`.
  - None of the three offers Save, Export, or Import. The only way to get
    a custom program into the node is to type over the preset in the
    editor; it is lost the moment a different preset is picked or the
    patch closes without the enclosing `.inf` patch being saved.
- Infinite already has a JSON library vendored and in use:
  `nlohmann::json` (`json.hpp`), consumed today by
  `src/core/PatchJson.h/.cpp` (one-directional `Patch::Data → json`, for
  the `RemoteControl` RPC's `get_graph()` — not a save/load path) and by
  `src/core/UpdateCheck.cpp`/`src/core/RemoteControl.cpp`. Adding JSON
  read/write for a new file kind is not introducing a new dependency.
- Infinite's own patch save format (`.inf`) is **not** JSON — it is the
  hand-rolled line grammar in `src/core/Patch.h`/`Patch.cpp` (`node`,
  `mod`, `expr`, `glob`, `s <name> <value>` lines, one node/param per
  line, `EscapeLine`/`UnescapeLine` for embedded newlines). The brief's
  own hedge — "verify whether JSON is even the right call" — resolves in
  favor of JSON for `.infdev`, but for a reason the brief didn't have:
  **a `.infdev` file is not a patch fragment.** It is never written by
  `Patch::Write`, never parsed by `Patch::Read`, and never touches undo.
  It is a standalone, user-facing, portable file meant to be inspected,
  hand-edited, and shared outside Infinite — exactly the shape JSON is
  for, and exactly *unlike* the line grammar, which exists to be an
  efficient, unambiguous serialization of a live `Patch::Data` graph.
  Reusing the line grammar for this would gain nothing (there is no
  graph structure to encode — one node, one program) and would cost
  human-readability and third-party tooling for zero benefit. **JSON is
  correct here; this is a genuine two-format codebase (line grammar for
  patches, JSON for portable device files) and that is fine — they solve
  different problems.**

### 0.2 Verified wrong or unverified — corrected below

| Brief's claim | Reality | Correction |
|---|---|---|
| `FieldSampleNode` and `FieldGraphNode` also ship hardcoded presets | **False.** Neither has a `Presets()`/`PresetNames()`/`LoadPreset()` — grep of `src/nodes/FieldSampleNode.*` and `src/nodes/FieldGraphNode.*` is zero hits, and neither `DrawFieldSampleParams` (`main.cpp:5395`) nor `DrawFieldGraphParams` (`main.cpp:5444`) draws a preset dropdown. | This step adds `.infdev` save/load/export/import uniformly to all five node types (`FieldElementNode`, `FieldPixelNode`, `FieldSampleNode`, `FieldGraphNode`, `FormulaNode`), but the **factory-preset dropdown** stays absent for Sample and Graph unless the owner also wants `Presets()` tables added to those two — a separate, smaller task not in scope here (§9 flags it). |
| `~/Documents/Infinite/Devices/<Domain>/` as the user directory, and "verify whether `Platform::GetUserDataPath()` exists" | No `Platform::GetUserDataPath()` exists, and `Documents` is not used anywhere in this codebase for app-owned files. | Use the existing header-only `AppPaths::AppSupportDir()` (`src/platform/AppPaths.h:73-96`) — macOS `~/Library/Application Support/Infinite`, Windows `%APPDATA%\Infinite`, both auto-created via `EnsureDir`. Devices live at `AppPaths::AppSupportDir() + "/Devices/<Domain>/"`. This is not a new abstraction; it is the one path helper this exact problem (an app-owned per-user directory that isn't a patch) already uses for the theme file and recents list. |
| Drag-and-drop from Finder/Explorer is "the highest-risk/most-novel part" | **False — it is already fully built and in daily use.** `glfwSetDropCallback` is wired at `main.cpp:42073` to `OnFilesDropped` (`main.cpp:1076-1081`), which pushes every dropped path into `gDroppedFiles`/`gDropPos`. Those are consumed at `main.cpp:45593-45906` in a large per-extension dispatch that already has the exact pattern this feature needs: `FindNodeUnderCanvasPoint<T>(canvasPos)` to find which node the file was dropped *onto*, then a direct load call on it (e.g. `dropTargetSampler->LoadFile(path)`, `dropTargetDrum->LoadFileToLane(...)`). | This is the **lowest**-risk part of the whole step: extend the existing dispatch with a `.infdev` branch and four `FindNodeUnderCanvasPoint<FieldXxxNode>` calls. See §4. |
| Save/Export/Import should use "a file dialog" (unspecified) | `Platform::` already has named, both-platform dialog functions for exactly this shape of feature (`OpenPatchDialog()`, `SavePatchDialog(suggestedName)` at `src/platform/Platform.h:105-106`, implemented on macOS via `NSOpenPanel`/`NSSavePanel` in `Platform.mm` and on Windows via `IFileOpenDialog`/`IFileSaveDialog` in `src/platform/win/PlatformWin.cpp:48-175`). | Add `Platform::OpenDeviceDialog()` / `Platform::SaveDeviceDialog(suggestedName)` following the exact same pattern, on both platforms — see §5. |
| UI additions live "in main.cpp per Field node type" with a preset dropdown, Save/Export/Import buttons | Broadly right, but the brief assumed the code editor is inline in the node body. It is not (see step 18's §0 for the full finding) — `DrawFieldElementParams`/`DrawFieldPixelParams`/`DrawFieldSampleParams`/`DrawFieldGraphParams` (`main.cpp:5339`, `5486`, `5395`, `5444`) already draw a compact, code-free params list; the actual multi-line editor is a separate floating ImGui window per domain (`main.cpp:57146-57404`, gated by `gFieldElementEditorOpen` etc.). | The Save/Export/Import/preset-dropdown controls in this step belong in the **params function** (next to the existing "Edit Field..." button and, for Element/Pixel, the existing preset `DropdownButton`), not inside the floating editor window. See §6. |
| A `FieldDevice` struct with `SaveToInfdevFile`/`LoadFromInfdevFile` | Reasonable shape, kept, but placed to match the codebase's actual per-domain ownership rather than as a single monolithic struct that would need to know about four unrelated `ParamTable`/`code`/`nodeSettings` shapes — see §2. | `FieldDevice` becomes a thin serialization struct plus free functions; each node's own field values are read into/written out of it by that node's own code (the same "the node owns its data, the format is dumb" split `Patch.h`'s writer/reader already uses). |

### 0.3 Sequencing vs. step 18

Step 18 (device faceplate / edit-mode UI) and this step are **independent
and can ship in either order** — verified, not assumed:

- Step 18's scope (per its own investigation, done as part of writing that
  doc) is UI-only: knob-grid layout discipline in the existing
  `DrawField*Params` functions, plus adding an inline live preview to the
  params body for `FieldPixelNode`/`FieldSampleNode` (the geometry domain
  already gets one for free via the generic `showMiniViewport` mini-
  viewport system — see step 18 §0.4). None of that reads or writes an
  `.infdev` file.
- This step's scope is entirely: a new file format, three buttons
  (Save/Export/Import — see §6 for why Export and Save collapse to nearly
  the same call), a preset-dropdown addition for two node types that
  currently lack one, and drag-and-drop wiring. None of that touches
  layout, knob grids, or the floating editor window's contents.
- The only shared surface is that both steps add UI to the same
  `DrawField*Params` functions. Building step 17 first means step 18's
  layout pass places one more row (the Save/Export/Import strip); building
  step 18 first means step 17 slots its row into an already-regridded
  body. Either is a small, mechanical adjustment — **this doc recommends
  building step 17 first** only because a working save/load format is
  useful on its own (a user can start collecting devices immediately)
  where a faceplate/edit-mode split has no payoff until there's something
  worth hiding, but the owner can reverse the order at no real cost.

---

## 1. Scope

Add a **portable `.infdev` file** — one Field program plus its param values
plus enough node-specific settings to reproduce behavior — that a user can
Save (to the app's device library), Export (to an arbitrary location via a
save dialog), Import (via an open dialog), or drag onto a matching node from
Finder/Explorer to hot-swap. Applies uniformly to `FieldElementNode`,
`FieldPixelNode`, `FieldSampleNode`, `FieldGraphNode`, and `FormulaNode`.

Out of scope for this step (flagged, not built): a fully-fledged device
*browser* UI (search/tags/thumbnails) beyond a flat preset dropdown; sharing
devices via any network path; versioned migration beyond the `version`
field's presence; adding factory `Presets()` tables to `FieldSampleNode`/
`FieldGraphNode` (§0.2's table).

---

## 2. The `.infdev` JSON shape

```json
{
  "format": "infdev",
  "version": 1,
  "meta": {
    "name": "Ripple Field",
    "author": "",
    "description": "",
    "domain": "element"
  },
  "device": {
    "code": "P.y += sin(P.x * 4.0 + t * 2.0) * 0.25\n",
    "params": { "amount": 0.5, "freq": 6.0 },
    "nodeSettings": { "generateCount": 64, "maxElements": 65536 }
  }
}
```

Corrections from the brief's draft:

- **No `category` field.** Grepped for any existing "category" concept on
  a device/preset — none exists; `NodeFactory`'s `category` string
  (`"Source"`, `"3D"`, `"Synths"`, `"Utility"` — the four current Field
  node registrations, `main.cpp:3810/3879/3967/3968`) is the *node's*
  palette category, not a property of a saved program, and conflating the
  two would be misleading (a `.infdev` file has no palette location).
  Dropped. `meta.domain` is the field that actually matters for filtering
  compatible files on import/drop.
- **`device.params` keys are param *names*, not indices.** `ParamTable`
  (`src/core/field/ParamTable.h`) already keys every declared `param` by
  name for exactly this reason — `ParamTable::Find(const std::string&)`
  (`ParamTable.h:43`) and the `mParamIdByName` map
  (`ParamTable.cpp:151-158`, `SerializeParamMap`/`DeserializeParamMap`)
  allocate a monotonically increasing internal `id` per name **the first
  time that name is ever seen for a node**, persisted across edits and
  patch reloads, and never reused — this is exactly option (b) from the
  `field-integration` skill's "OPEN — what is a Field `param`'s
  `paramIndex`?" question, already implemented (`ParamTable.cpp:118-135`
  looks the name up in `mParamIdByName` first, only allocates a fresh id
  on a true miss). A `.infdev` file's `params` map should therefore be
  loaded by **matching name against the target node's already-reconciled
  `ParamTable`** after `code` is applied and `Reconcile()` has run — not
  by id, which is per-node-instance internal state with no meaning
  outside that node.
- **`nodeSettings` is domain-specific and small**, not a generic bag:
  - `element`: `generateCount` (int, only meaningful when the node has no
    upstream input — see the `if (!n->input)` guard at `main.cpp:5384`),
    `maxElements` (int).
  - `pixel`: `width`, `height` (float, node's canvas resolution),
    `animate` (bool).
  - `sample`: `maxVoices` (int).
  - `graph`: none currently exposed as a plain field beyond `param`
    declarations — omit `nodeSettings` entirely for this domain (empty
    object or field absent, either is valid — the loader must accept
    both).
  - `formula` (FormulaNode, pixel-shader domain, distinct from
    `FieldPixelNode`): no extra settings beyond `code`; `formula` params
    are typed `=` expressions per `Expression.cpp`, not Field `param`
    declarations — see §7 for why `FormulaNode` needs a slightly
    different `device.params` reader.
- **`meta.domain` values**: `"element"`, `"pixel"`, `"sample"`, `"graph"`,
  `"formula"` — one string per node type, used both for the file's own
  documentation value and, on drag-and-drop, to refuse a mismatched drop
  (dropping a `pixel` device onto a `FieldSampleNode` is a no-op with a
  status-line message, not a crash or a silent wrong-load).

---

## 3. `FieldDevice` — where it lives and what it actually does

New files: `src/core/field/FieldDevice.h` / `.cpp`. Registered in
`CMakeLists.txt` next to the other `src/core/field/*.cpp` entries
(`ParamTable.cpp` is at line 231 today — add alphabetically nearby).

Per §0.2's correction, this is **not** a struct that reaches into four
unrelated node classes' internals. It is a serialization-only type plus
free functions the node classes call into, matching how `Patch.h`'s line
grammar keeps "what a line means" (the writer/reader) separate from "what
a node does with its own fields" (each node's own `VisitParams`):

```cpp
// src/core/field/FieldDevice.h
namespace Field
{
   struct DeviceFile
   {
      int version = 1;
      std::string name, author, description, domain;
      std::string code;
      std::map<std::string, float> params;
      std::map<std::string, double> nodeSettings; // int/bool stored as double, per §2
   };

   // Pure (de)serialization - no INode knowledge, no file I/O.
   std::string ToJsonString(const DeviceFile& device);
   bool FromJsonString(const std::string& jsonText, DeviceFile& outDevice, std::string& outError);

   // File I/O wrappers - the only place FieldDevice.cpp touches disk.
   bool SaveToInfdevFile(const std::string& path, const DeviceFile& device);
   bool LoadFromInfdevFile(const std::string& path, DeviceFile& outDevice, std::string& outError);
}
```

Each node type gets two small non-member helpers next to its existing
`Presets()`/`LoadPreset()` (or, for Sample/Graph, added fresh) that convert
between the node's live fields and a `Field::DeviceFile`:

```cpp
// FieldElementNode.h, next to LoadPreset(int)
Field::DeviceFile ToDeviceFile() const;   // reads code, GetParamTable(), generateCount, maxElements
void LoadDeviceFile(const Field::DeviceFile& device); // sets code, calls Apply(), then
                                                        // writes matching params into GetParamTable()
                                                        // by name, sets generateCount/maxElements
```

This mirrors the existing `LoadPreset(int index)` shape exactly — a
`.infdev` load is functionally "load a preset that came from a file
instead of the static table," and should end with the same call
`LoadPreset` already makes: `Apply()`, so a bad file surfaces through the
existing `LastError()` path (`FormulaNode`/`FieldPixelNode`'s
compile-keeps-last-working-program behavior, already documented in the
`field-integration` skill §8) rather than a new one.

---

## 4. Drag-and-drop — extending the existing mechanism, not building one

Per §0.2, the OS-level drop plumbing already exists end to end. The only
change is inside the per-extension dispatch at `main.cpp:45593-45906`:

1. Add `".infdev"` to a new `kInfdevExt` list next to the existing
   `kAudioExt`/`kModelExt`/`kGltfExt` (`main.cpp:45596-45608`).
2. Add four `FindNodeUnderCanvasPoint<T>` calls next to the existing
   `dropTargetSampler`/`dropTargetDrum`/etc. block
   (`main.cpp:45610-45619`): `FieldElementNode*`, `FieldPixelNode*`,
   `FieldSampleNode*`, `FieldGraphNode*` (and `FormulaNode*` if the owner
   wants drag-and-drop for that node type too — it already has the
   `PresetNames()`/`LoadPreset()` shape this reuses).
3. In the per-path loop, before the existing `.inf`/`.infinite` branch
   (which loads a whole *patch* and would otherwise never let a
   `.infdev` extension reach a useful branch — extensions are checked in
   order, so `.infdev` must be its own `HasExtension` check, not folded
   into `.inf`), add:
   ```cpp
   if (HasExtension(path, kInfdevExt))
   {
      Field::DeviceFile device;
      std::string err;
      if (Field::LoadFromInfdevFile(path, device, err))
      {
         if (dropTargetElement != nullptr && device.domain == "element")
         { ensureDroppedCheckpoint(); dropTargetElement->LoadDeviceFile(device); gPatchDirty = true; continue; }
         if (dropTargetPixel != nullptr && device.domain == "pixel")
         { ensureDroppedCheckpoint(); dropTargetPixel->LoadDeviceFile(device); gPatchDirty = true; continue; }
         // ...sample, graph, formula, each domain-gated the same way
      }
      // mismatched domain, unreadable file, or no matching node under the
      // drop point: status-line message, no crash, no partial mutation -
      // matches the existing branches' behavior when e.g. an audio file is
      // dropped with no matching node under the cursor.
   }
   ```
4. **No new drop target when nothing matches under the cursor.** Every
   existing branch in this dispatch either mutates a node found under the
   drop point or falls through silently; a `.infdev` with no matching node
   underneath should do the same — it is not this step's job to spawn a
   fresh node of the right type from a dropped device file (that would be
   a reasonable follow-up, not required here; flagged in §9).

This is additive to an existing, working dispatch chain — no new
`glfwSetDropCallback`, no new global state, no new platform code.

---

## 5. `Platform::` additions — both platforms, both required

New declarations in `src/platform/Platform.h`, next to
`OpenPatchDialog()`/`SavePatchDialog()` (`:105-106`):

```cpp
std::string OpenDeviceDialog();
std::string SaveDeviceDialog(const std::string& suggestedName);
```

**macOS** (`src/platform/Platform.mm`, next to the existing
`SavePatchDialog`/`OpenPatchDialog` implementations near `:568`/`:590`):
`NSSavePanel`/`NSOpenPanel` with `allowedContentTypes`/extension filter set
to `infdev`, same pattern as the patch dialogs — copy their structure, do
not invent a new one.

**Windows** (`src/platform/win/PlatformWin.cpp`, next to
`OpenPatchDialog`/`SavePatchDialog` around `:279-297`): `RunOpenDialog` /
`RunSaveDialog` (already-generic helpers at `:50`/`:127`) with a
`FilterSpec` for `*.infdev`, same call shape as the existing
`OpenPatchDialog`/`SavePatchDialog` wrappers at those line numbers. This is
a two-line addition per function — `RunOpenDialog`/`RunSaveDialog` already
take the filter list as a parameter, so no new dialog machinery is needed
on Windows either. Per `windows-parity`, this Platform:: function is
**not done** until both bodies exist — do not ship a macOS-only stub.

The **user device library directory**
(`AppPaths::AppSupportDir() + "/Devices/" + domain + "/"`, per §0.2) is
created lazily (`AppPaths::EnsureDir`, already header-only and portable —
`src/platform/AppPaths.h:64-71`) the first time a node's params function
scans it or a Save-to-library action runs. No new `Platform::` function
needed for this part; `AppPaths` already covers it.

---

## 6. UI additions — in `DrawField*Params`, not the floating editor

Per §0.2's correction, these controls belong in the compact params
functions (`main.cpp:5339` `DrawFieldElementParams`, `:5395`
`DrawFieldSampleParams`, `:5444` `DrawFieldGraphParams`, `:5486`
`DrawFieldPixelParams`), next to the existing "Edit Field..." button — not
inside the separate floating editor windows (`main.cpp:57146` onward),
which are step 18's territory.

Per node type, inside the existing `if (!gParamRegisterOnly)` guard (these
are display-only controls, exactly like the existing preset dropdown and
"Edit Field..."/"Regenerate" buttons already inside that same guard):

- **Devices dropdown**: factory presets (where they exist — Element,
  Pixel, and Formula only, per §0.2) followed by a separator, followed by
  every `.infdev` file found under
  `AppPaths::AppSupportDir() + "/Devices/<domain>/"` (scanned once per
  session on first draw, cached — not re-scanned every frame; invalidate
  the cache with an explicit "Save" action, matching how the existing
  static preset tables are effectively cached forever). For Sample and
  Graph, this dropdown shows only user devices (no factory section) until
  the separate, out-of-scope task in §9 adds factory presets there too.
- **Save button** — writes to the user's device directory, prompting for
  a name via `ImGui::InputText` in a small popup (matching the existing
  "rename" pattern used elsewhere in the app, e.g. node/group renaming;
  grep `ImGui::OpenPopup` sites near node rename for the exact idiom to
  copy) — not a full save dialog, since the destination directory is
  fixed.
- **Export button** — `Platform::SaveDeviceDialog(suggestedName)` (§5),
  writes anywhere the user picks.
- **Import button** — `Platform::OpenDeviceDialog()` (§5), reads from
  anywhere, then calls the node's `LoadDeviceFile`.

Layout note for whoever builds step 18 after this: these four controls
(dropdown + 3 buttons) are exactly the kind of control row `node-ui-pillars`
P1/P3/P6 govern (a dropdown left, buttons filling the row, no free-floating
widgets) — build them as an `AudioKnobRow`-equivalent row from the start
rather than stacked `ImGui::Button` calls one under another, even though
this step's job is not the full faceplate pass.

---

## 7. `FormulaNode` is a slightly different shape — handle separately

`FormulaNode`'s "params" are not Field `param` declarations — they are
typed `=` expressions bound through `Expression::Evaluate`
(`src/core/Expression.cpp`), a completely different mechanism (see the
`field-integration` skill §1's five-mini-languages table — `FormulaNode`
predates Field and is not itself a Field node). Its `.infdev` writer/reader
therefore does not go through `ParamTable`; `device.params` for a
`FormulaNode` device is empty or omitted (its GLSL body has no `param`
syntax), and its `code` field alone carries everything meaningful. Keep
`FormulaNode`'s `ToDeviceFile()`/`LoadDeviceFile()` minimal — this is
intentional, not an oversight, and should be called out as a one-line
comment at the call site so a future reader doesn't "fix" it by trying to
force `FormulaNode` through `ParamTable`.

---

## 8. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte
cmake --build build -j"$(sysctl -n hw.ncpu)"

env INFINITE_FIELDDEVICETEST=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/FIELDDEVICETEST.log | tail -5
grep -c FAIL /tmp/FIELDDEVICETEST.log

.claude/skills/run-infinite-hygiene/driver.sh
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`INFINITE_FIELDDEVICETEST` (new) must assert, for each of
`FieldElementNode`/`FieldPixelNode`/`FieldSampleNode`/`FieldGraphNode`/
`FormulaNode`:

1. `ToDeviceFile()` → `ToJsonString()` → `FromJsonString()` →
   `LoadDeviceFile()` round-trips `code` byte-for-byte and every declared
   `param`'s value within float epsilon;
2. a `.infdev` file written by `SaveToInfdevFile` and re-read by
   `LoadFromInfdevFile` round-trips identically (the file-I/O layer adds
   nothing lossy);
3. `LoadDeviceFile` on a device whose `params` map names a param the
   target node's current `code` does not declare silently ignores that
   entry (no crash, no phantom param) — the reconcile-by-name step in §2
   must be checked, not assumed;
4. `LoadDeviceFile` with deliberately malformed `code` leaves the node's
   *previous* `code`/compiled program in place and populates `LastError()`
   — the same keep-last-working-program contract `Apply()` already
   provides for a bad preset;
5. dropping a `.infdev` file with `meta.domain: "pixel"` onto a
   `FieldSampleNode` (mismatched domain) is a no-op — asserted via the
   existing drop-dispatch test harness pattern, not by manual UI drive
   (per the `feedback_no_control_your_mac` memory note — do not UI-script
   Infinite's canvas to verify this; drive `OnFilesDropped` and the
   dispatch function directly in the test).

---

## 9. Deferred (flagged, not built here)

- Factory `Presets()` tables for `FieldSampleNode` and `FieldGraphNode` —
  the brief assumed these exist; they don't (§0.2). Adding them is a
  content task (write good example programs), not a plumbing task, and is
  independent of this step's file-format work.
- Spawning a fresh node from a `.infdev` dropped onto empty canvas (rather
  than only hot-swapping an existing node under the drop point) — a
  reasonable follow-up, not required for the exit criterion above.
- A real device *browser* panel (thumbnails, search, tags) beyond the flat
  dropdown in §6 — out of scope; the dropdown is the v1 UI.
