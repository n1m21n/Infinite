# Infinite UI system: analysis and approach

Status: analysis, awaiting sign-off on sections 1–7 (2026-10-08). No code changes until signed off.
Tracker: the STATUS table in `docs/plans/iconography/README.md` stays the single tracker; this doc is the architecture behind it.
Skills: `infinite-design-system`, `node-ui-pillars`, `codebase-navigation`, `windows-parity`, `linux-parity`.

## 1. Goal

A native Infinite UI system: every pixel the user sees comes from Infinite's own tokens, type, layout, components and motion, at Logic Pro quality, on macOS, Windows and Linux, and any future UI change is a small edit in one place.

## 2. Problem

The owner sees a cluttered top bar, icon and text sizes that don't match, no spacing between sections, nothing that reads as modern pro software. Measured causes (counts from `src/`, 2026-10-08):

### 2a. Limitations of the current UI system

| # | Limitation | Evidence | Effect the user sees |
|---|---|---|---|
| L1 | **One type size, one weight** | `PushFont` = 0 uses; one font baked at 15 pt (`src/core/UiScale.h`); hierarchy done by grey (`TextDisabled` 254, `TextColored` 91) | No caption/value/title hierarchy; everything same size, "dim" is the only emphasis |
| L2 | **Fonts are pre-baked bitmaps** | ImGui 1.90.9 atlas; Inter + Noto CJK + Lucide + glyph font merged at one size; `SetWindowFontScale(1.8f)` (`NoteBodies.cpp:598`) scales a bitmap | Extra sizes multiply atlas memory (CJK); scaled text is soft |
| L3 | **No layout owner** | `SameLine` 281, `Dummy` 242, `SetCursorScreenPos` 224, `SetNextItemWidth` 121, `CalcTextSize` 72, `ImVec2(n,n)` literals 346, float literals in UI code ~9.9k | Every call site invents spacing and size; sections never line up |
| L4 | **Styling by push/pop at call sites** | `PushStyleColor` 253, `PushStyleVar` 55 | Same control looks different per surface; easy to leak a style |
| L5 | **Stock widgets show through** | `ImGui::Button` 267, `Separator` 97, `MenuItem` 207, tables 92 cells | "Looks like ImGui" |
| L6 | **Only a thin component layer** | `components/` holds `IconTile.h` only; tokens used in 34 files | Design rules can't be enforced at one point |
| L7 | **No interaction model of our own** | `IsItemHovered` 187, `IsItemActive` 114 read ad hoc; `UiAnim` exists but few users | Hover/press/on/focus differ per control; snaps instead of eases |
| L8 | **Widget IDs come from translated labels** | 320 widgets take `T()`/`L()` text as label (= ImGui ID) | Switching language resets widget state and open popups; fragile for tests |
| L9 | **Canvas is a third-party editor** | `imgui-node-editor` (45 files use `ed::`); zoom scales draw lists | Node text at zoom is a scaled bitmap (verify in baseline); node look bounded by its style API |
| L10 | **446 `g*` globals in `src/app`** | panel open flags, UI state as globals | Hard to test a surface in isolation or render it in a gallery |
| L11 | **No accessibility surface** | ImGui exposes no semantic tree | No VoiceOver / Narrator / Orca; no path today |
| L12 | **Verification is manual** | `context_shot.py` crops exist; no goldens per component | Regressions found by eye |

### 2b. What is not the problem

| Suspected | Reality |
|---|---|
| ImGui can't draw Logic's look | It can: Logic's chrome is flat rounded shapes, text and glyphs. The branch already draws our glyphs and tokens with it |
| ImGui is too slow | Canvas pan p50 8.4 ms, p95 12.2 ms (perf Block 2, B6). UI drawing is not the bottleneck |
| Rewriting gives freedom | Freedom comes from owning type, layout, components and state; that's achievable without swapping the window/input/text engine |

## 3. Scope

| In | Out (for now) |
|---|---|
| Type engine, layout engine, interaction model, component API, all chrome surfaces, node shell, node controls, visualizers, menus, tables, feedback, gallery, goldens | Screen-reader bridge (architected for, built later), new features, node DSP, Field |

## 4. Existing constraints

| Constraint | Why it binds |
|---|---|
| Three platforms, one code path (`windows-parity`, `linux-parity`) | Rules out native-per-OS chrome (AppKit-only would break parity) |
| MIT licence, clean room | Rules out JUCE (AGPL/commercial) and any GPL toolkit; Qt only under LGPL dynamic linking |
| OpenGL shared with canvas, projectors, previews | A second UI toolkit would need GL context sharing with the canvas and projector windows |
| Live instrument: perf budget | Canvas B6 p50 ≤ 17.2 ms, p95 ≤ 22.8 ms must hold; UI may not steal frames from projectors |
| ~300k lines; ~6,000 `ImGui::` calls in 64 files; 324 knobs, 609 sliders, 1,237 draw-list calls | Any change is incremental, surface by surface, app always shippable |
| `node-ui-pillars` owns node layout | The UI system renders controls; pillars place them |
| i18n (CJK wrap patch in vendored ImGui) | Upgrades must re-apply `docs/plans/i18n/imgui-patches.md`; text sizes must fit CJK |
| Release cadence | Each block merges to main working; no long-lived fork |

## 5. Hypothesis

The look-and-feel problems come from **missing ownership** (L1, L3, L4, L6, L7), not from ImGui's renderer. An Infinite UI system layered on ImGui (1.92, for dynamic fonts), with its own type, layout, interaction and component layers, reaches Logic-level quality at a fraction of a rewrite's cost, and creates a boundary behind which ImGui could later be replaced without touching surfaces.

### Target architecture

```
 L6  Surfaces        top bar · arrange · panels · menus · canvas · node bodies
 ─────────────────── only components below this line ───────────────────────
 L5  Components      IconTile PillGroup Readout TextButton Knob DotSlider Switch
                     Dropdown Segmented Table Menu Toast EmptyState Pin NodeFrame …
 L4  Interaction     stable IDs · hover/press/drag/focus state · keyboard focus ring
                     · UiAnim · semantic tree (role, label, value) per frame
 L3  Layout          rect engine: rows, columns, stacks, groups, gaps from tokens
 L2  Tokens          tokens.json → Tokens.gen.h (colour, type, size, space, radius, motion)
 L1  Render          ImDrawList shapes + text engine (sizes, weights) + glyph font
 L0  Platform        GLFW window · ImGui input/IO · OpenGL · popups · IME
```

Rules: a surface never calls `ImGui::Button/Separator/SameLine/PushStyle*`; a component never names a literal; only L0–L1 include ImGui internals. The semantic tree in L4 is what a future VoiceOver/Narrator bridge reads (L11).

## 6. Baseline (record once, at the start of block 1; never re-measured)

| Metric | Source | Value now |
|---|---|---|
| Raw ImGui widget/flow calls in surfaces (`Button`, `SameLine`, `Dummy`, `SetCursorScreenPos`, `PushStyleColor`, `Separator`) | `tools/design/inventory.py` (extend) | ~1,365; ratchet subset (`Button`, `SmallButton`, `Separator`, `SameLine`, `PushStyleColor`, `PushStyleVar`) = **959 in 32 files** (2026-10-08) |
| Float / `ImVec2` literals in UI files | inventory | ~9.9k / 346 |
| Type sizes / weights in use | `PushFont` count | 1 / 1 |
| Components | `components/` | 1 |
| Widgets with label-derived IDs | grep `T(`/`L(` labels | 320 |
| Canvas B6 pan p50 / p95 | perf bench | 8.4 / 12.2 ms (unpaced, perf plan). 2026-10-08 A/B, main `4b5a0a30` vs `feature/ui-engine` after 1g, 300 nodes, visible, vsync paced: frame p50 16.67 / 16.67, p95 17.22 / 16.89, pan/zoom stage 16.67 both; canvas stages equal (node_bodies 2.03 / 2.02 ms, imgui_render 0.43 / 0.44 ms); footprint 951.5 / 948.7 MB. Within +5 %. Re-run after the top bar (1h, same fixture, `ab.sh auto`): frame p50 2.71 / 2.68 ms, p95 11.51 / 11.25, pan p50 2.87 / 2.85, node_bodies 1.02 / 1.00, imgui_render 0.27 / 0.24, footprint 934.5 / 931.3 MB. Within noise |
| Font atlas size (px, MB) | log at startup | not measured on the old bake; footprint above is the proxy (no increase). Dynamic atlas now |
| Node text sharpness at 2× canvas zoom | headless shot | record |
| Golden shots of every surface, light + dark | `context_shot.py` | record |

## 7. Success metrics

| Metric | Target |
|---|---|
| Raw ImGui widget/flow calls in surfaces | 0 (all inside `components/`); ratchet only goes down |
| Literals in migrated surfaces | 0 |
| Type scale | ≥ 4 sizes × 3 weights, all from tokens; icons optically matched to text |
| Components | Every class in the iconography plan's section 0 exists, in the gallery, with goldens |
| Stable IDs | 0 widgets keyed by translated text |
| Canvas B6 p50 / p95 | Within +5 % of baseline |
| Atlas memory | ≤ baseline (dynamic fonts) |
| Node text at 2× zoom | Sharp (rendered at zoomed size) |
| Behaviour | All hygiene self-tests pass; preserved interactions covered by tests |
| Platforms | macOS, Windows, Linux CI green each merge |
| Owner | Each surface approved from a real-app shot before the next starts |

## 8. Methodology

1. **Engine before surfaces.** Build L1–L5 with no visible change except sharper text, prove them in a gallery, then move surfaces.
2. **One class at a time, app-wide.** All knobs at once, never node by node, so old and new never sit side by side.
3. **Mockup → approve → build → real-app shot → approve** for every surface (owner gate).
4. **Ratchets, not promises.** Inventory counts (raw widgets, literals, label IDs) may only go down; CI enforces.
5. **Goldens per component** in light and dark; pixel changes need the golden updated in the same commit.
6. **Perf bench before and after** each block (keep-only-if-better gate from `run-infinite-hygiene`).
7. **Commit per step**, explicit paths, bisectable.

## 9. Solution trajectories

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A. Keep stock ImGui, restyle | Cheapest | Fixes nothing in L1–L8 | No |
| **B. Infinite UI system on ImGui 1.92** | Incremental; app stays shippable; 3 platforms free; GL sharing untouched; swap boundary for the future | We build layout + components ourselves; no screen reader yet | **Recommended** |
| C. Qt | Mature widgets, accessibility | Rewrite every surface and node body; LGPL packaging; GL sharing with canvas/projectors; heavier; still needs our design system on top | No |
| D. Web UI (Electron/WebView) | CSS freedom | Latency for a live instrument; GL canvas interop; memory; two runtimes | No |
| E. Own engine (Skia + Yoga/Clay) | Total freedom | 6–12 months before parity; we'd own text, IME, input, popups | Not now; B keeps this door open |
| F. Native chrome per OS (AppKit etc.) + ImGui canvas | Truly native on macOS | Three UIs to maintain; breaks parity | No |

### What-if scenarios

| What if | Then |
|---|---|
| ImGui 1.92 breaks `imgui-node-editor` | Pin a node-editor revision compatible with 1.92, or patch it (record in `imgui-patches.md`); fallback: stay on 1.91 and bake 3 Latin sizes, CJK at one size |
| The row layout helper isn't enough (panels, dialogs) | Adopt Clay (MIT, single header) behind L3; surfaces don't change |
| Node text at zoom stays soft | Render node text at zoomed font size (1.92 makes this possible); else LOD hides small text |
| Accessibility becomes required | Bridge the L4 semantic tree to NSAccessibility / UIA / AT-SPI |
| ImGui ever blocks us | Re-implement L0–L1 (and L4 input) on another backend; L5–L6 untouched |
| A surface migration regresses behaviour | Its tests fail before merge; revert that one commit |

## 10. Benchmarking against the baseline

Filled per block: each row of section 7 with its measured value and the commit that achieved it.

## 11. Critique

| Risk | Mitigation |
|---|---|
| Building an engine nobody sees for a while | Block 1 ends with the top bar as its proof surface, so the owner sees the result |
| ImGui upgrade churn (1.90 → 1.92 changed the font API) | Done first, alone, behind the existing tests and goldens, before any design work |
| Component API grows ad hoc | Gallery + typed API review per component; one file per class |
| 446 globals make gallery rendering hard | Components take state as arguments; surfaces keep globals for now |
| Owner approval becomes a bottleneck | Batch approvals per surface with one real-app crop + one mockup |
| Hidden behaviour in old code (BPM drag, popups, drop-off) | Write behaviour tests before migrating each surface |

## 12. Next steps: three blocks

| Block | What | Branch | Done when |
|---|---|---|---|
| **1. Engine** | Upgrade ImGui → 1.92 (re-apply CJK patch, node-editor compat) · type engine (sizes × weights from tokens, icon/text optical match) · layout engine (L3) · interaction model with stable IDs + semantic tree (L4) · first components (IconTile, PillGroup, TextButton, Readout, Divider) · gallery (debug only) + goldens · ratchet on raw widgets · **proof surface: top bar** | `feature/ui-engine` | Baseline recorded; top bar approved; B6 within +5 %; 3-platform CI green; merged |
| **2. Chrome** | Arrange header (snap magnet redraw), panels, mod matrix, perf mode, viewport panel, menus, tables, Settings, feedback (toasts, empty states), cursors | `feature/ui-chrome` | Raw widgets in chrome = 0; surfaces approved; merged |
| **3. Canvas + nodes** | Node frame/pins/cables on components, controls C1–C12 class by class app-wide, visualizers, zoom LOD + sharp zoomed text, node-family icons | `feature/ui-canvas` | Raw widgets in surfaces = 0; `node-ui-pillars` + sweeps pass; merged |

Each block may take several sessions; each session ends with its steps committed and the STATUS table updated. `feature/design-foundations` (tokens, glyphs, `UiAnim`) merges to main before block 1 starts.
