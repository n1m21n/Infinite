# Language switch — execution brief

**Languages:** English (en, source) · Español (es) · Deutsch (de) · 中文简体 (zh) · 日本語 (ja) · Русский (ru)
**Status:** done (2026-10-09, owner: language switch is pushed) · **Branches:** `feature/i18n-core`, `feature/i18n-help`, `feature/i18n-sweep` (merged into `main`) · **Date:** 2026-10-07

---

## 1. What the user gets

- Settings → Appearance → **Language** dropdown, next to *Interface font*.
- Switch applies **live** (no restart): menus, panels, popups, tooltips, dialogs, help.
- First launch picks the OS language if it is one of the six, else English.
- Patches open identically in every language; a German user's `.inf` loads on a Japanese machine.

## 2. Decisions (all decided 2026-10-07)

| # | Decision | Why |
|---|---|---|
| D1 ✅ | **English source string is the key** (gettext style: `T("Save")`), optional context `TC("menu", "Open")` | Wrapping ~800 existing literals is mechanical; a missing translation silently falls back to English |
| D2 ✅ | **Decided.** Node type names, param names, category names and dropdown values (filter modes, blend modes) **display in English** in every language. Menus, panels, settings, tooltips, dialogs and help prose are translated. Help text for a node is translated, so a user can still learn what `Cutoff` does in their language. Internal keys are never translated under any future option | Pro tools converge here: Ableton Live translates the UI but keeps device names; Blender ships a separate, default-off "translate new data" switch; TouchDesigner/Max/Resolume Fusion nodes stay English. These names are shared vocabulary across `.inf`, Field, modulation bindings, CLI, tutorials, forums and the website, so a translated `LFO → Rate` would break "search the forum for the answer". The display path still goes through one function (`NodeDisplayName()`), so a default-off "Translate node names" toggle can be added later without touching keys |
| D3 ✅ | Tables are plain files `resources/lang/<code>.tsv` (`key<TAB>text`), English needs no file | Diffable, editable by a translator without a compiler |
| D4 ✅ | CJK fonts: **Noto Sans SC** (zh) and **Noto Sans JP** (ja), OFL 1.1, **subset at build time** to the glyphs the tables use | Full fonts are 8–16 MB each; subset ≈ 300–600 KB. Separate SC/JP faces because Han glyph shapes differ |
| D5 ✅ | **Decided.** Translations drafted by Claude against a glossary (§5), shown as **"(beta)"** in the dropdown until a native speaker signs off that language | Honest about quality; unblocks shipping |
| D6 ✅ | Numbers always use `.` and English formatting; **never call `setlocale` / `std::locale::global`** | `Patch.cpp:174` (`atof`) and `Patch.cpp:856` (`strtof`) are locale-sensitive: a `de` locale would read `0.5` as `0` and corrupt every patch |

## 3. How it works

```
 resources/lang/de.tsv ──load──▶ I18n::Table (unordered_map<key, text>)
                                        │
   UI code:  ImGui::Button(L("Save"))  ─┤  L() → "Speichern###Save"   (widget label, ID unchanged)
             ImGui::Text("%s", T("Gain")) ┘  T() → "Speichern"          (plain text, no ###)
                                        │
 Settings → Language ─▶ SetLanguage("de") ─▶ save to appearance file ─▶ UiScale::RequestRescale()
                                                                              │
 RebuildFonts (Startup.cpp) ◀──────────────────────────────────────────────────┘
   Inter (Latin + Cyrillic, already baked)  +  merge Noto SC/JP glyphs for zh/ja only
```

**The ID rule (most important trap).** ImGui uses the label as the widget/window ID.
Translating `"Settings"` → `"Einstellungen"` would change the ID: `imgui.ini` loses window
positions/docking, tab state resets, two widgets translated to the same word collide.
`L()` therefore always returns `"<translated>###<original id>"`. If the source already has
`##suffix`, keep its suffix. Net effect: **every ID is byte-identical to today in every language**.

`T()` is for text that is not an ID (`Text`, `TextWrapped`, `SetTooltip`, `SeparatorText`
hint text, format strings). Never pass `L()` output to `Text` — `###` would be printed.

## 4. Where it touches the code

| Area | File | Change |
|---|---|---|
| Runtime | new `src/core/I18n.h/.cpp` | `T`, `TC`, `L`, `LC`, `SetLanguage`, `CurrentLanguage`, `Languages()`, `GlyphsForCurrentLanguage()`; returns stable `const char*` (cache strings, never temporaries) |
| Persistence | `src/core/CategoryColors.cpp:737` (`GetUiFont`/`SetUiFont` pattern) | add `GetLanguage`/`SetLanguage`, saved by `SaveAppearanceOverrides()` |
| OS language | `src/platform/` | new `Platform::PreferredLanguage()` — macOS `NSLocale preferredLanguages` (Platform.mm), Windows `GetUserDefaultLocaleName` (win/), Linux `LC_ALL`/`LC_MESSAGES`/`LANG` (linux/). **All three sides** (`windows-parity`, `linux-parity`) |
| Fonts | `src/app/Startup.cpp:48–71` | for zh/ja: `ImFontGlyphRangesBuilder` over every string in the active table (+ kana `0x3040–0x30FF`, CJK punct `0x3000–0x303F`, fullwidth `0xFF00–0xFFEF`), merge Noto face with `MergeMode=true`. Bake only the active language's face (atlas stays small on 1.90.9's static atlas) |
| Font bundling | `CMakeLists.txt:1045–1101` (3 copy lists) | add `external/fonts/Noto/NotoSansSC-Subset.otf`, `NotoSansJP-Subset.otf` + OFL licence; third-party notice |
| Subsetter | new `tools/i18n/subset_fonts.py` | `pyftsubset` with the union of glyphs in `zh.tsv`/`ja.tsv`; run when tables change, output committed |
| Picker | `src/app/panels/SettingsWindow.cpp:219` | Language combo after Interface font; labels in their own script (`Deutsch`, `日本語`) — always baked |
| CJK wrap | `external/imgui/imgui_draw.cpp:3820` `CalcWordWrapPositionA` | only breaks on blanks → Chinese/Japanese paragraphs never wrap. Small vendor patch: treat codepoints ≥ `0x3000` as break points (and don't start a line with `。、，」`). Record in the imgui patch notes |
| Strings | ~670 widget calls (`Button` 197, `MenuItem` 184, `Checkbox` 130, `Text` 67, `SetTooltip` 27, `BeginMenu` 25, `SeparatorText` 24, `Selectable` 11) + 19 `ImGui::Begin` titles | wrap with `L()` / `T()` |
| Help | `src/app/panels/HelpWindows.cpp` + the 4 hand-kept help tables (see node-help-coverage) | Block 3 |
| Debug UI | `UI Style Editor` etc. | **not** translated (debug-only, `#ifndef NDEBUG`) |

**Never translated:** `.inf` content, node/param/category keys (`NodeFactory` names), Field keywords, CLI/headless output, logs, `RemoteControl`/OSC addresses, file extensions, shortcut key names in code (display string only).

## 5. Glossary (fixed before any table is drafted)

Kept in English in all languages: node names, param names, `Field`, `LFO`, `ADSR`, `BPM`, `MIDI`, `OSC`, `NDI`, `Syphon`, `Spout`, `VST3`, `AU`, `FPS`, file formats.
Translated consistently (one row per term in `resources/lang/glossary.tsv`, 6 columns): patch, node, cable, canvas, timeline, clip, lane, modulation, macro, bypass, render, export, preset, viewport, projector, sample, bar, beat, …
Rules: es/de use formal *usted*/*Sie*-free imperative UI style ("Guardar", "Speichern"); ja uses です/ます-free UI noun style; zh Simplified only.

## 6. Execution — three blocks, one branch each, commit per step

### Block 1 — `feature/i18n-core` (end-to-end on a pilot)

1. `I18n.h/.cpp` + TSV loader + fallback to English + `L()` ID rule. Unit self-test.
2. `Platform::PreferredLanguage()` × 3 platforms; first-run default only (an explicit choice always wins).
3. `GetLanguage`/`SetLanguage` in appearance file; Language combo in Settings.
4. Noto SC/JP subset + bundling (3 CMake copy lists) + glyph builder in `RebuildFonts`.
5. ImGui CJK wrap patch.
6. Pilot: wrap **Settings window + main menu bar** (`StageMenuBar.cpp`) and draft all 5 tables for those strings.
7. `I18NTEST` self-test (see §7). Build, copy to `~/Desktop/Infinite.app`.

Exit: switching language live re-renders Settings + menu bar in all six, CJK shows no `?`, window positions survive a switch and a relaunch.

### Block 2 — `feature/i18n-sweep` (all UI)

1. `tools/i18n/extract.py`: scans `src/` for `T/TC/L/LC(...)`, writes `en` key list, reports missing/unused keys per language.
2. `tools/i18n/lint.py`: flags raw string literals in ImGui label/text calls in `src/app/` (allowlist for `##ids`, debug UI, format-only strings). Target 0.
3. Wrap by area, one commit each: panels (Arrange, PerfPanel, ModMatrix, Library, Viewport, Minimap) → popups (`StagePopupsA/B`) → node bodies' non-param text (dropdown *values* like filter modes stay English per D2) → dialogs/modals → tooltips.
4. Draft tables for es/de/zh/ja/ru, regenerate font subsets.
5. **Layout pass:** German runs ~30 % longer. Replace fixed widths (`SetNextItemWidth(200.0f)`, fixed button widths) with `CalcTextSize`-based or `max(fixed, text+pad)` in clipped spots; check every panel in all four docks (`panels-sweep`) and node bodies (`node-ui-pillars`, `node-ui-sweep`).

Exit: lint = 0, extract reports 0 missing for all five languages, panels-sweep + node-ui-sweep pass in `de` and `ja`.

### Block 3 — `feature/i18n-help`

1. Help window + node help prose + shortcuts window descriptions (`shortcuts-sweep` still cross-checks rows).
2. Node browser search matches **both** English and translated help/category text (`FoldForSearch` already exists; fold CJK unchanged).
3. Release notes line + website node list note (languages, beta status).

## 7. Tests (`I18NTEST`, part of the self-test harness)

| Check | Fails when |
|---|---|
| Every key in `en` exists in each table (or is explicitly marked `=en`) | missing translation |
| `printf` spec parity (`%d %s %.2f` count/order) per key | crash/garbage in formatted text |
| Every codepoint in the active table has a glyph (`FindGlyphNoFallback`) after rebake | `?` boxes |
| `L()` output ID part == source ID for every key | `imgui.ini` / state loss |
| Round-trip: save patch in `ja`, reload in `en`, compare bytes | language leaked into `.inf` |
| `ROUNDTRIPTEST` + `AUDIOPARAMSWEEPTEST` run with language = `de` | locale leaked into parsing (D6) |
| Switch language 6× live, atlas texture count stable | GL texture leak on rebake |

Platform: run Block 1 exit on macOS, Windows (CI zip), Linux (container+Xvfb rig — check fontconfig isn't consulted; we bundle).

## 8. Risks

| Risk | Mitigation |
|---|---|
| Atlas too big for CJK at Retina bake scale | Only glyphs from the active table (≈2–3 k for zh) → fits 2048²; assert atlas size in `I18NTEST` |
| User-typed CJK (patch names, text nodes) shows `?` | Out of v1 scope; follow-up: also feed open-patch strings to the glyph builder on load |
| Machine-drafted translations read wrong | "(beta)" label (D5); native review per language before removing it |
| Strings built by concatenation (`"Delete " + name`) can't be reordered in ja/de | Lint flags them; convert to one format string `T("Delete %s")` |
| `L()` returning pointer to a temporary | Table owns all strings for app lifetime; reload replaces table only between frames |
| Translated label width breaks a node body | Node bodies mostly keep English (D2); the rest covered by `node-ui-sweep` in `de` |

## 9. Open questions (one at a time)

1. ~~D2~~ decided: English node/param names.
2. ~~D5~~ decided: all five ship as "(beta)" until a native speaker signs off each.
