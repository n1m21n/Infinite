# How to find bugs fast (read after COMMON.md; apply in every slot)

Highest yield first. Spend the first half of each slot on 1-3.

## 1. Department contract first (best yield)
Work by department (DEPARTMENTS.md): write down what it must do, run its harness, check the tests really assert that, probe the edges. A bug is "the code does not do what the department is supposed to do"; a test gap is "nothing would notice if it stopped". Closed GitHub issues are user reports only, not the bugs found while developing, so they are a hint about classes, not the main source. The development fixes live in `git log -i --grep='^fix' -n 60 --stat`; read those diffs for classes, then check the sibling code with a grep. Record classes in lessons.md.

## 2. Churn and fix hot spots
`git log --since=90.days --name-only --format= -- src | sort | uniq -c | sort -rn | head -30`. Files edited most often, and files with the most "fix" commits, hold the most bugs. Read those first when the day's area contains them.

## 3. Stored-but-never-used (dead wiring)
For every serialized param/key in a node: grep that it is read AND applied in the DSP or render path (issue #5: `useGlobalScale` stored, never applied). Also the reverse: a UI control with no effect, a control shown but not saved, a saved key never loaded. Tools: `python3 scripts/audit_param_truth.py`, `python3 scripts/audit_node_params.py`.

## 4. Edge-state checklist (apply to every node or feature you read)
- value 0, -1, 1e9, NaN, inf, empty string, very long string, non-ASCII path
- no input connected; input then disconnected; node bypassed; node deleted while playing or rendering
- sample rate or buffer size change; tempo 0 or huge; project length 0
- first frame, loop boundary, loop wrap (clicks, stuck notes, double triggers; issue #51)
- undo then redo; save, reload, save again (round trip must be identical)
- window minimised, resized tiny, moved between monitors (DPI)
- canvas panned or zoomed: anything drawn or hit-tested in the wrong coordinate space (issue #49)
- auto-wire and drag/drop: is the new connection actually made (issue #48)
- user-pasted input (Shadertoy GLSL, text, paths) with unusual headers or characters (issue #50)

## 5. Cross-checks that find real bugs by machine
- Per-OS `#if` branches: every Platform:: function has three sides (`.claude/skills/pillar-parity-audit/static-checks.sh`).
- Help text vs real params: node help tables and `--describe` vs the node's actual controls.
- Shaders: loop bound not a literal; division by a value that can be 0.
- Audio thread: new, malloc, vector push_back, locks, file I/O, logging inside the audio callback.
- Integers: a loaded or received int used as array index, loop bound or allocation size without a clamp.

## 6. Learn (each bug makes the next hunt better)
After each issue, append ONE line to /Users/namansoni/Infinite/bughunt-state/lessons.md: `<date> | class | grep command | where found`. Keep a section "Static grep patterns" in that file: every greppable class gets its command there, and DAILY.md step 1.0 runs the whole list each day, so a class once found is checked everywhere forever.
Never edit tools/bughunt/README.md yourself. When a class has 2+ findings, write in your summary "propose lint rule: <class>" for the owner.

## 7. Stay on what users feel
Crash, hang, NaN or hot audio, data loss, then wrong or degraded output. Dead code and naming are not worth an issue. If unsure a bug is real, test the claim (small script, `--validate`, headless render) before filing; if you can't test it, leave it out.

## 8. Repo skills: read them, apply them
The skills live in `.claude/skills/<name>/SKILL.md`. You cannot invoke them as commands; open and read the file, then follow it. Each department block in DEPARTMENTS.md names the ones to read first. Always-on ones:
- `bug-blast-radius`: run its nine questions on every candidate bug BEFORE filing. The issue body's "Blast radius" must come from its greps, not from reasoning (cite file:line).
- `invariant-interaction-audit`: when code establishes a guarantee (in key, normalised, clamped, finite, sorted), look at everything that runs AFTER it and could undo it (a later clamp, re-scale, cache re-derive, copy-pasted sibling). This is the relational-bug finder.
- `codebase-navigation` and `codebase-lenses`: find every implementer, registration site and platform path before concluding anything; use the hotspot map to know which shared engines matter.
- `infinite-code-review`: the four review standards (accuracy, experimentality, instrument-grade design, real-time quality) as a lens on any code you read.
- `param-truth-audit`, `data-accuracy-sweep`, `node-param-audit`: does the DSP do what the control claims; does a value stay right along the chain, copy/paste, save/load, bypass.
- `tool-first`: if a check repeats, use the existing script in tools/ or scripts/ instead of reading by hand.

## 9. Relational bugs (one defect, many surfaces)
A defect usually sits in a shared helper but shows in one node. For every candidate bug:
1. Find the shared function it runs through (`grep -rn "<helper>(" src`): every caller is a suspect. Read each caller's use, not only the first.
2. Find copy-pasted siblings (same idiom in other nodes; `AudioBouncingBallsNode` and `CVToPitchNode` shared a bug with the node that was fixed).
3. Find registration sites (node factory, serializer keys, help tables, describe, manual, presets, undo, copy/paste, bypass, the three Platform:: sides). A thing added in one place and missing in another is a bug.
4. Find what runs after it (invariant-interaction-audit) and what runs before it (stale cache, memo, revision counters that were not bumped).
5. Ask what else consumes the output: downstream nodes, modulation destinations, export, preview, the 3D composer, the arranger.
Group findings by root cause: one issue listing every affected caller.

## 10. UI/UX consistency (every UI-touching slot)
Read `node-ui-pillars`, `audio-node-ui`, `infinite-design-system` first. Check, by reading Draw*Body code and screenshots from tools/design/gallery:
- Layout: every control sits on the knob-row grid (`AudioKnobRow`, equal cells); no free-floating checkbox, dropdown or toggle; cell counts match between a knob row and the row below; mod-dot sits beside its own control; the `mix` slot is bottom-right; symmetric padding and alignment between nodes of the same family.
- Same thing, same look: the same parameter kind (mix, gain, rate, sync, mode) has the same widget, label, range, unit and position across nodes; filter-mode names identical everywhere; dropdowns show a field well and value, never a chevron; labels only, no hint lines.
- Tokens: no literal colours, radii, sizes or durations at call sites. Everything goes through components and role tokens (tokens.json). Run the design ratchet, icon lint, motion lint, contrast (light and dark), and `tools/brand/brand_lint.py`; a count that rose is a finding.
- Behaviour: knobs show value on hover or drag, reset on double-click, respect min/max and units, are modulatable only if the `node_param_audit.md` says so; disabled states dim and block; tooltips do not blank rows; text never clips at small sizes or in the other theme.
- Release builds: no debug UI (UI Debugger, Style Editor) in a shipped build.
