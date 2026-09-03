# Field build step 18 — device faceplate polish for Field node bodies

You are implementing **build step 18 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). Self-
contained brief; no prior context assumed beyond "Files to read first".
Line numbers are from `src/` at the commit this was written against
(`feature/field-step-15-fieldgraph-encapsulation` tip, commit `f59820c`) —
re-grep the symbol if a number has drifted; the symbol wins.

**Prerequisite:** none — independent of step 17 (`.infdev` format). See
step 17 §0.3 for the sequencing argument; this doc does not repeat it.

**Read `.claude/skills/node-ui-pillars/SKILL.md` before touching any of the
functions below.** Symmetry and dark-mode contrast are the acceptance
checklist, not a suggestion.

**Origin:** this doc rewrites an owner-supplied informal brief modeled on
Max for Live's device/edit-view split. The brief's central premise — that
Field node bodies currently show code inline and need a mode toggle built
to hide it — is **false**, verified against the actual code in §0. That
finding changes what this step actually needs to build.

---

## 0. What the brief got wrong, verified against the code

### 0.1 The premise: "toggle a `bool showCodeEditor` to switch between inline code and a compact faceplate"

**This already exists, and it is not inline.** Grepping for how the four
Field node types actually draw:

| Function | Line | What it draws |
|---|---|---|
| `DrawFieldElementParams` | `main.cpp:5339` | preset dropdown, **"Edit Field..." button**, error/notice/cost text, `max elements`/`generate count` sliders, one `ModSlider` per declared `param` |
| `DrawFieldPixelParams` | `main.cpp:5486` | preset dropdown, **"Edit Field..." button**, error text, branch-count/state-cell-count text, `width`/`height`/`animate`, one `ModSlider` per declared `param` |
| `DrawFieldSampleParams` | `main.cpp:5395` | **"Edit Field..." button**, error/notice/fault-count/rms text, `max voices`, one `ModSlider` per declared `param` |
| `DrawFieldGraphParams` | `main.cpp:5444` | **"Edit Field..." button**, **"Regenerate" button**, error/notice text, one `ModSlider` per declared `param` |

**None of these four functions contains an `InputTextMultiline` or any
other code-editing widget.** The multi-line source editor lives entirely
in a *separate floating ImGui window* per domain — `"Field element editor"`
/ `"Field pixel editor"` / `"Field sample editor"` / `"Field graph editor"`
— drawn at `main.cpp:57161-57404`, opened by the "Edit Field..." button
setting `gFieldElementEditorOpen = true` (and the sibling globals,
declared `main.cpp:971-983`) and closed by the window's own title-bar close
control (the `bool*` passed to `ImGui::Begin`, ImGui's standard pattern —
functionally already a "Done" affordance, just ImGui's default one rather
than a custom button).

So the **mode split the brief asks for already exists**, has for as long as
these four node types have (this is not new from step 15/16 — `git log -p`
on `DrawFieldElementParams` shows this shape predates the encapsulation
work). The node body already is a compact, code-free "Device Mode." What
does *not* exist is:

1. A persisted `showCodeEditor` bool is **not needed** — the existing
   `gFieldXxxEditorOpen` globals already serve that role. They are
   deliberately *not* persisted via `VisitParams` (same as
   `gFormulaEditorOpen`, `main.cpp:970` — none of the sibling editor-open
   flags are node state, they're ephemeral UI session state), which is
   correct: whether a user happened to have the code editor open is not
   part of the patch's saved behavior, the same way no window's open/closed
   state is saved. **Do not add persistence here** — doing so would be
   adding a per-node entry to a generic path for no behavioral gain, the
   exact anti-pattern `field-integration` skill §5 warns against.
2. **The floating editor window is a genuinely separate ImGui window**,
   not an in-place body expansion the way Max for Live's device/edit split
   swaps content within the same panel. This is the real, valid design
   question for the owner (§5) — not "build a toggle" (already built) but
   "should the editor become an in-place expansion of the node body
   instead of a separate floating window."
3. The **faceplate** itself (the "Device Mode" body) does not yet follow
   `node-ui-pillars`' knob-grid grammar — every `ModSlider`/`ModSliderInt`
   call in the four functions above is a bare stacked call, not wrapped in
   an `AudioKnobRow`. This is real, unaddressed work — §2.
4. **Inline live preview is inconsistent across the four domains** and, for
   two of them, does not reach the node body at all today — §3.

### 0.2 What the brief's own hedges resolved correctly

The brief already told the implementer to "reuse whatever existing
visualizer pattern the step-15 agent found for `FieldGraphNode`'s inline
preview card... read that doc's relevant section." Checked:
`step-15-fieldgraph-encapsulation.md` does **not** describe an inline
preview card for `FieldGraphNode` — it describes the opposite. Quoting the
step-15 commit this branch already shipped
(`git show b50294b --stat`, message: "FieldGraphNode: hide its vestigial
output pin/preview (meta-node, no picture to show)"): `FieldGraphNode` is a
generator/meta-node with no picture to show, and its preview was
deliberately removed, not added. There is nothing to copy from
`FieldGraphNode` for the other three domains — see §3 for what actually
exists per domain instead.

### 0.3 What "Edit Mode" already contains, corrected against the brief's description

The brief describes Edit Mode as "full code editor + preset bar + compiler
error readout." Checked against the four floating editor windows
(`main.cpp:57161-57404`):

| Domain | Has code editor | Has error readout | Has preset bar | Has live preview |
|---|---|---|---|---|
| Element | yes (`InputTextMultiline`, `:57177`) | yes (`:57188`) | **no** | yes — orbit-able solo geometry viewport (`:57196-57220`) |
| Pixel | yes (`:57238`) | yes (`:57252`) | **no** | yes — output texture, same texture the node's `GetOutputTexture()` exposes (`:57265-57281`) |
| Sample | yes (`:57323`) | yes (`:57337`) | **no** | yes — `DrawFieldSampleScope` waveform (`main.cpp:8411`, called at `:57344`) |
| Graph | yes | yes | **no** | **no** (§0.2 — intentional) |

None of the four editor windows has a preset bar today — that is real,
correctly-scoped new work for this step (Element and Pixel already have a
preset *dropdown*, but only in the node body's params function, not
inside the editor window itself). Each already has a per-domain live
preview matched to what that domain actually produces (geometry viewport
for element, texture for pixel, scope for sample) — the brief's ask to
"apply the same pattern to Element/Pixel/Sample if they don't already have
one" turns out to already be satisfied for all three, just inside the
editor window rather than the compact body (§3 covers moving/duplicating
this into the body).

---

## 1. Corrected scope for this step

Given §0, "build a Device/Edit mode toggle" is not the work. The real,
verified gaps are three independent, additive changes:

1. **Faceplate discipline** — regrid the four `DrawField*Params` functions'
   knobs onto `AudioKnobRow`, per `node-ui-pillars` P1/P2/P3/P6. (§2)
2. **Inline preview in the compact body** — today the live preview only
   exists inside the floating editor window; add a small always-visible (or
   opt-in, matching the existing `showMiniViewport` convention — see §3.1)
   preview to the compact body for Pixel and Sample, and make Element's
   existing generic mini-viewport mechanism actually reachable and obvious
   for `FieldElementNode` specifically. (§3)
3. **A preset bar inside the editor window**, so a user editing code can
   switch preset/device without closing the editor first. (§4)

Whether the floating editor window becomes an in-place body expansion
instead (closer to the brief's literal Max-for-Live framing) is a real
design fork with cost on both sides — flagged as an open question for the
owner, not decided here (§5).

---

## 2. Faceplate discipline — regrid the four params functions

Every knob in `DrawFieldElementParams`/`DrawFieldPixelParams`/
`DrawFieldSampleParams`/`DrawFieldGraphParams` is currently a bare
sequential `ModSlider`/`ModSliderInt` call — no `AudioKnobRow`, so P1
("every control sits on the row grid") is violated by all four functions
today. Concretely, `DrawFieldPixelParams` (`main.cpp:5486-5528`) calls
`ModSlider("width", ...)`, `ModSlider("height", ...)`, `ModCheckbox
("animate", ...)` and then a loop of `ModSlider` per declared param, each
one full-width and stacked — the checkbox in particular is a textbook P1
violation (a lone `ModCheckbox` outside any `AudioKnobRow`, exactly the
"free-floating widget" pattern `node-ui-pillars` names as the bug to never
write again).

Per node type:

- **`FieldPixelNode`**: `width`/`height` in a 2-cell `AudioKnobRow`,
  `animate` via `AudioKnobRow::Checkbox` in its own row (or folded into a
  3rd cell of the same row if width allows — P6 prefers a filled grid).
  Declared `param`s below, grouped into rows of however many fit
  `gAudioContentW` (`AudioKnobRow`'s own cell-count logic already handles
  this — do not hardcode a cell count that assumes a fixed param count,
  since Field's whole premise is a variable, user-authored param list).
- **`FieldElementNode`**: `max elements` and `generate count` (the latter
  conditionally drawn, `if (!n->input)`) in one row — this is exactly the
  "a row that changes shape must not change the grid" case P5 names
  (`generate count`'s cell must stay reserved, drawn empty via
  `row.Skip()`, when an input is connected, not removed — a cell that
  appears and disappears as cables connect/disconnect is the kind of
  layout jump P5 exists to prevent). Declared params below, same
  variable-row-count handling as Pixel.
- **`FieldSampleNode`**: `max voices` alone in a 1-cell (or wider, with
  `row.Skip()` for the remaining cells per P6) row, declared params below.
- **`FieldGraphNode`**: no domain-specific knobs beyond declared params —
  those alone, grid-regridded the same way.

**A variable-count param list is the one layout wrinkle none of the
existing `AudioKnobRow` call sites elsewhere in the codebase have to
solve** (every other node's knob count is fixed at compile time). Confirm
`AudioKnobRow`'s constructor (`main.cpp` ~7511, per the `node-ui-pillars`
skill's citation table) accepts a cell count computed at draw time from
`n->GetParamTable().Params().size()` — it does, since it already takes an
`int` — and chunk the declared-param loop into rows of a fixed
per-domain cell width (match the existing `kParamWidth`/knob sizing so a
3-param Field program's row looks like any other node's 3-knob row, not
narrower or wider).

Apply P4 (`mix` bottom-right) only if a future Field convention introduces
a canonical `mix`-equivalent param name — none exists today (Field has no
reserved "mix" semantic), so this rule does not apply to these four
bodies as they stand; do not invent a fake `mix` slot to satisfy the
letter of a rule whose actual concern (a predictable last-knob location)
doesn't have a real user-facing counterpart here yet.

---

## 3. Inline preview in the compact body

### 3.1 Element already has this — via a generic mechanism, underused here

`FieldElementNode` implements `IGeometrySource`. Every `IGeometrySource`
node in this codebase already gets an opt-in solo-render mini-viewport,
independent of Field entirely: `GraphNode::showMiniViewport`
(`main.cpp:21291`'s comment: "Off by default per node... only drawn/
rendered at all when a node opts in"), toggled by a monitor-icon button
(`main.cpp:53728-53729`), dispatched at `main.cpp:53550-53552`:
```cpp
else if (gn.showMiniViewport && geoSourceForViewport != nullptr &&
         HasUsefulMiniViewport(gn.node.get()))
   DrawMiniViewport(gn, geoSourceForViewport);
```
`HasUsefulMiniViewport` (`main.cpp:21391-21394`) excludes only
`ParticleSystemNode` — `FieldElementNode` already qualifies, today, with
zero new code. **This is not a gap; the brief's ask ("apply the same
pattern to Element... if they don't already have one") is already
satisfied for Element** via the app-wide mechanism, not a Field-specific
one. The only real polish item here: confirm the monitor-icon toggle
button is itself laid out per `node-ui-pillars` (it is a shared widget,
not Field-specific, so this is a spot-check, not new work) and leave it —
do not build a second, Field-specific preview toggle that would compete
with the existing one.

### 3.2 Pixel and Sample do not have this in the body — real, scoped work

`FieldPixelNode` does not implement `IGeometrySource` (it's a 2D texture
source, not geometry), so it never reaches the `showMiniViewport` dispatch
at all. `CanShowInViewportPanel` (`main.cpp:21396-21411`) — the function
gating a *different* existing preview mechanism, the docked viewport
panel — would need checking for whether `FieldPixelNode` already renders
there; if it does, that is a second existing avenue to point a user at
rather than building a third. Regardless, neither avenue puts a preview
**inside the compact node body**, which is what the brief is actually
asking for (a preview visible without opening any other panel or window).

Add, inside `DrawFieldPixelParams` (`main.cpp:5486`, after the existing
error/branch-count/state-cell text, before the `width`/`height` knob row):
a small (e.g. 96×96, deliberately smaller than the 190px `kPreviewSize`
used by the editor window's own preview, since the compact body has less
room and other node types' inline thumbnails in this codebase are
similarly modest) `dl->AddImage` of `n->GetOutputTexture()` — the exact
same texture and the exact same "no separate render path, updates the
instant `Apply()` recompiles" property the editor window's own preview
already documents at `main.cpp:57262-57264`. This is a copy-and-shrink of
existing, working code, not a new render path.

Add, inside `DrawFieldSampleParams` (`main.cpp:5395`, after the existing
error/notice/fault/rms text): a small `DrawFieldSampleScope(n, 40.0f,
kPreviewSize)` call — `DrawFieldSampleScope` (`main.cpp:8411`) already
takes a node pointer, a height, and a width, and already works from inside
the editor window; calling it a second time from the compact body with a
smaller height is not new code, it's a second call site of an existing
function. Confirm `ReadScope` (`FieldSampleNode`, called inside
`DrawFieldSampleScope`) is cheap enough to call from two draw sites in the
same frame when both the body and the editor window are visible
simultaneously — it reads a ring buffer already written on the audio
thread each block, not something the draw call itself computes, so this
should be free, but verify rather than assume (`field-realtime` skill's
cross-thread-read discipline applies to anything touching audio-thread
state from two UI call sites in the same frame).

### 3.3 Graph gets nothing — confirmed intentional, not a gap

Per §0.2, `FieldGraphNode`'s preview was deliberately removed in the
commit this branch already includes. Do not add one back. Its compact body
stays: preset-free (no `Presets()` today, per step 17 §0.2), "Edit
Field..." + "Regenerate" buttons, error/notice text, declared params.

---

## 4. Preset bar inside the editor window

Per §0.3, none of the four floating editor windows currently has a preset
selector — only the compact body does, and only for Element and Pixel.
Add, at the top of each editor window (`main.cpp:57164` `Field element
editor`, `:57237` `Field pixel editor`, `:57311` `Field sample editor`,
`:57367` `Field graph editor`), the same `DropdownButton("preset", ...)`
call already used in the compact body (`main.cpp:5343-5344` for Element,
`:5490-5491` for Pixel) — for Sample and Graph, this row shows only user
`.infdev` devices if step 17 has landed (§0.3's sequencing note: this
row's *shape* doesn't depend on step 17, but its *content* is richer if
step 17 shipped first). Selecting a preset here should update `editBuf`
(the static per-window text buffer that mirrors the node's live `code`,
e.g. `main.cpp:57177-57181`) the same way opening the editor on a
different node already does via the `lastEdited` staleness check — reuse
that exact mechanism rather than adding a second buffer-sync path.

---

## 5. Open question for the owner — in-place expansion vs. separate window

The brief's Max-for-Live framing (edit view replaces device view in the
same panel) is a real, different design from what exists today (a
separate floating window). Both are defensible:

- **Keep the separate window** (this step's default, per §1-4 above): zero
  risk to the existing, working editor windows; smaller diff; a user can
  see the compact faceplate and the editor at once (useful when tuning a
  param while watching the code, or vice versa — genuinely not possible
  in an in-place swap).
- **Collapse into one panel, in-place**, matching Max for Live exactly:
  requires the compact body to grow to editor size when active (a real
  ImGui layout change to the node's on-canvas footprint, which interacts
  with `ed::` node-editor sizing and every other node's layout
  expectations — non-trivial, and not something to build speculatively
  without owner sign-off, per this step's own no-hand-waving mandate).

This doc does not pick one — build the separate-window version (§1-4),
which is strictly additive and lower-risk, and flag this as the one
decision the owner should make explicitly before anyone builds the
in-place variant.

---

## 6. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte
cmake --build build -j"$(sysctl -n hw.ncpu)"

.claude/skills/run-infinite-hygiene/driver.sh
.claude/skills/node-ui-sweep/driver.sh 2>/dev/null || true   # if present; otherwise manual per checklist below
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

Since this step is UI-only (no new save-format surface, no new cross-
thread channel), its acceptance is the `node-ui-pillars` checklist,
applied to all four `DrawField*Params` functions:

1. Screenshot each of the four node bodies in light, default dark, and one
   high-contrast theme — checkbox/dropdown chrome must sit within the P10
   luminance budget against the node body background.
2. Attach a modulation cable to a declared `param` knob in each of the
   four bodies and confirm the mod dot is centered on the knob (P2) after
   the regrid — this is the one thing most likely to regress silently
   when `ModSlider` calls move from bare stacking into `AudioKnobRow`.
3. Add and remove a `param` declaration in the floating editor, click
   Apply, and confirm the compact body's knob row re-flows without a
   frame of visible jump or a stale cell (this exercises the
   variable-row-count handling in §2 — the one layout case unique to
   Field among all node types in this codebase).
4. For `FieldElementNode`: confirm the monitor-icon mini-viewport toggle
   (§3.1) still works unmodified.
5. For `FieldPixelNode`/`FieldSampleNode`: confirm the new compact-body
   preview (§3.2) updates live while the editor window is simultaneously
   open and being edited — both must reflect the same underlying
   texture/scope with no visible desync.
6. `.claude/skills/node-param-audit/SKILL.md` — confirm no param stopped
   being modulatable as a side effect of the regrid.
