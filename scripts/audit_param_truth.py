#!/usr/bin/env python3
"""Cross-check what a node's DSP does to a param against what its knob/slider
tells the user is possible - the two get written independently (a knob call
in src/main.cpp, a clamp or formula in the node's own .cpp/.h) and nothing
enforces that they agree once written.

This exists because of two real, shipped bugs in AnalogNode: the "pw" knob
was disabled unless a waveform was Square even though the DSP secretly only
ever wired it to osc1 (so it lied in *two* directions - looked inert when it
wasn't, and looked live for osc2 when it was actually a no-op); and the "fm"
knob was declared and documented 0..1 while the DSP clamped its consumer to
0..2, a full octave of dead, unreachable headroom nobody could ever dial in.
Neither is a crash, a save/load bug, or a modulation-registration gap - the
existing sweeps (audio-node-sweep, node-param-audit) don't look at *numeric
agreement* between a control's declared range and what its own DSP consumer
actually does with that range. This script automates the mechanical half of
that (range agreement); the SKILL.md covers the half that needs a reader.

    python3 scripts/audit_param_truth.py [--node NodeName]

Finds, per file pair (a node's UI draw code in src/main.cpp, its DSP in
src/nodes/*.{h,cpp}):

  1. RANGE MISMATCH - a knob/slider declares [lo, hi] but the DSP-side
     std::clamp on the same mailbox param (traced Push(kX, p.field) ->
     std::clamp(mMailbox.SmoothedValue(kX), loD, hiD)) uses different
     literal bounds. Reported both directions:
       - DSP narrower than the knob -> the knob can dial in a value the DSP
         silently clips (an audible lie: the knob visibly moves, the sound
         stops changing).
       - DSP wider than the knob -> dead headroom nobody can reach (the
         pre-fix "fm" bug: not audibly wrong, just a knob whose declared
         range doesn't match what its own consumer claims to accept).

  2. UNTRACEABLE CLAMP - a clamp on a mailbox param whose bounds aren't bare
     float literals (e.g. `20.0f, (float)mSampleRate * 0.48f`) or whose value
     passes through an affine transform before the clamp (e.g.
     `0.5f + SmoothedValue(...) * 8.0f`). Not a defect by itself - most of
     these are deliberate safety clamps (Nyquist guards) or intentional
     tapers - but the script can't verify them, so they're listed for a
     human to eyeball rather than silently skipped.

  3. BACKEND TRANSFORM - clamp is only the mechanically-checkable special
     case of the real defect: any function the DSP applies to a mailbox
     value reshapes what the knob's motion actually means, not just clamp.
     `powf(2.0f, SmoothedValue(kFilePitchParam) / 12.0f)` (semitones ->
     ratio), `DspMath::DbToLinear(SmoothedValue(kGainDbParam))` (dB ->
     linear), and `SmoothedValue(kLoopParam) > 0.5f` (a float mailbox slot
     read as a bool) are all real examples already in this codebase. None of
     these are clamps, so the RANGE MISMATCH check never looks at them - but
     each one still makes a claim (a unit, a curve, a threshold) that the
     knob's caption/format string either backs up or contradicts. This
     script can't verify the claim (that needs the reader - see SKILL.md),
     but it finds every such line and prints it next to the matching knob's
     caption/range/format so the comparison takes one glance instead of a
     manual grep per node.

Static analysis only (regex over source, like audit_node_params.py) - no
build, no launch.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

import appsrc  # src/main.cpp + src/app/**/*.cpp (main.cpp was split)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NODES_DIR = os.path.join(ROOT, "src", "nodes")
# The AudioEffects category (Delay, Reverb, Chorus, ... - 23 effects) is a
# second, separate architecture: AudioEffectNode doesn't use ParamMailbox
# itself, it holds a std::unique_ptr<IEffectKernel> and each concrete kernel
# (DelayKernel, ReverbKernel, ...) has ITS OWN private ParamMailbox, fed by
# `node.Param("name")` in that kernel's own PushParams() override - not the
# `p.field`/`node.field` struct-member push src/nodes/*.{h,cpp} uses. See
# PUSH_NODEPARAM_RE below.
EFFECTS_DSP_DIR = os.path.join(ROOT, "src", "audio", "dsp")
EFFECT_DEFS_CPP = os.path.join(ROOT, "src", "audio", "EffectDefs.cpp")

# `mMailbox.Push(kFoo, p.field);` / `mMailbox.Push(kFoo, node.field);` / bare
# `mMailbox.Push(kFoo, field);` -> paramId "kFoo" consumes "field". Several
# real shapes exist here, not just the struct-member one: AnalogSynthCore
# uses `p.field`, MetallicNode uses `node.field`, but AnalyzeNodes/AudioNodes/
# SamplerNode/DrumSequencerNode push a bare local parameter instead (e.g.
# `mMailbox.Push(kFilePitchParam, pitchSemitones)`) - no struct, no dot. The
# trailing identifier is taken as the field name either way; this assumes the
# local/parameter name conventionally matches the UI struct field it came
# from (true everywhere checked so far - e.g. SamplerNode's `pitch` param
# both here and as `n->pitch` in main.cpp) - a real but unverified assumption
# for any node not yet spot-checked.
PUSH_RE = re.compile(r"mMailbox\.Push\(\s*(\w+)\s*,\s*(?:\w+\.)?(\w+)\s*\)")

# AudioEffects kernels: `mMailbox.Push(kFoo, node.Param("feedback"))` - the
# pushed value is a string-keyed lookup on the AudioEffectNode, not a struct
# field, so the "field name" is the string literal itself (no aliasing risk
# the way PUSH_RE's bare-identifier case has, since there's only ever one
# spelling of the string).
PUSH_NODEPARAM_RE = re.compile(r'mMailbox\.Push\(\s*(\w+)\s*,\s*node\.Param\(\s*"([^"]+)"\s*\)\s*\)')

# `std::clamp(mMailbox.SmoothedValue(kFoo), LO, HI)` - LO/HI captured raw;
# only kept as a real finding when both are bare numeric literals.
CLAMP_RE = re.compile(
    r"std::clamp\(\s*mMailbox\.SmoothedValue\(\s*(\w+)\s*\)\s*,\s*([^,]+?)\s*,\s*([^)]+?)\s*\)"
)
# Same, but the smoothed value passes through an affine transform first -
# still traced back to a field, but not range-compared (see UNTRACEABLE).
CLAMP_AFFINE_RE = re.compile(
    r"std::clamp\(\s*[^,()]*mMailbox\.SmoothedValue\(\s*(\w+)\s*\)[^,()]*,\s*([^,]+?)\s*,\s*([^)]+?)\s*\)"
)

NUMERIC_RE = re.compile(r"^-?\d+(\.\d+)?f?$")

# Knob/slider call families that carry (label, &n->field, lo, hi, "fmt", ...)
# in that order - same families node-param-audit already knows about, minus
# the widgets with no declared range (checkbox, dropdown, colour). The format
# string (group 5) is optional to capture since not every call site keeps it
# on the same textual argument position, but when present it's the UI's only
# stated claim about unit/precision (`"%.1f st"`, `"%.0f dB"`) - exactly what
# a BACKEND TRANSFORM finding needs to be checked against.
RANGED_WIDGET_RE = re.compile(
    r'(?:row\.Knob|row\.Fader|row2\.Knob|ModKnob|ModSlider|AudioSlider)\(\s*'
    r'"([^"]+)"\s*,\s*&?\w*->(\w+)\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*(?:"([^"]*)")?'
)
# DropdownKnob's *second* half is a ranged knob too: (..., "label", &n->field, lo, hi, ...)
DROPDOWNKNOB_RE = re.compile(
    r'DropdownKnob\([^;]*?,\s*"([^"]+)"\s*,\s*&n->(\w+)\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*(?:"([^"]*)")?',
    re.DOTALL,
)

# Every mailbox read, clamped or not - used to find BACKEND TRANSFORM lines
# (a SmoothedValue() this script's clamp regexes didn't already claim).
SMOOTHED_RE = re.compile(r"mMailbox\.SmoothedValue\(\s*(\w+)\s*\)")
# A mailbox read used bare (assigned straight to a variable, no wrapping
# function) isn't a transform - it's the identity function, nothing to
# eyeball. Only flag a read when something other than assignment/whitespace/
# a binary op with another bare read sits around it, e.g. `powf(2.0f, X /
# 12.0f)`, `DbToLinear(X)`, `X > 0.5f`, `X + Y / 100.0f`.
TRIVIAL_READ_RE = re.compile(
    r"^\s*(?:const\s+)?(?:float|bool)?\s*\w+(?:\[\w+\])?(?:\.\w+)?\s*=\s*"
    r"mMailbox\.SmoothedValue\(\s*\w+\s*\)\s*;\s*$"
)


def is_numeric(tok):
    return bool(NUMERIC_RE.match(tok.strip()))


def to_float(tok):
    return float(tok.strip().rstrip("f"))


def find_field_pushes(text):
    """paramId -> set of field names, from every Push(kFoo, ...) in this
    file. A param id can legitimately be pushed from more than one call site
    under different local names - e.g. SamplerNode's primary audio-thread
    push uses `pitch` but its `SetClipPitchOverride(float semitones)` pushes
    the same kPitchParam under `semitones`. Keeping only the last name seen
    (a plain dict) silently drops the real UI-facing field name whenever an
    override/setter is textually last in the file, which was exactly wrong
    here: the actual `n->pitch` knob never got checked. Every distinct name
    is kept and tried."""
    out = defaultdict(set)
    for m in PUSH_RE.finditer(text):
        out[m.group(1)].add(m.group(2))
    for m in PUSH_NODEPARAM_RE.finditer(text):
        out[m.group(1)].add(m.group(2))
    return out


def find_dsp_clamps(path, text):
    """field name -> (lo, hi, traceable, line) for every mailbox clamp. A
    param id with more than one pushed alias (see find_field_pushes) yields
    one entry per alias, so a UI-range lookup under any of its names finds
    it - cheap since it's at most a couple of extra dict lookups downstream,
    never a false positive since find_ui_ranges only reports a field that's
    actually bound by a knob call in that node's own Draw*Body."""
    field_of = find_field_pushes(text)
    out = []
    for m in CLAMP_RE.finditer(text):
        param_id, lo_raw, hi_raw = m.group(1), m.group(2), m.group(3)
        fields = field_of.get(param_id)
        if not fields:
            continue
        line = text.count("\n", 0, m.start()) + 1
        traceable = is_numeric(lo_raw) and is_numeric(hi_raw)
        for field in fields:
            out.append((field, lo_raw.strip(), hi_raw.strip(), traceable, path, line))
    # Anything CLAMP_RE missed because of a pre-clamp transform still shows
    # up via the affine pattern; dedupe by (field, line).
    seen = {(f, ln) for f, *_, ln in out}
    for m in CLAMP_AFFINE_RE.finditer(text):
        param_id, lo_raw, hi_raw = m.group(1), m.group(2), m.group(3)
        fields = field_of.get(param_id)
        if not fields:
            continue
        line = text.count("\n", 0, m.start()) + 1
        for field in fields:
            if (field, line) in seen:
                continue
            out.append((field, lo_raw.strip(), hi_raw.strip(), False, path, line))
    return out


def find_ui_ranges(bodies):
    """struct field name -> list of (label, lo, hi, fmt, line) from every knob
    call found across the given (body_text, start_line) pairs (one node's
    Draw*Body function(s), scoped via dispatch_map so unrelated nodes that
    happen to share a field name like "detune" or "mix" never collide - see
    the KNOWN LIMITATION note on function_bodies for when that scoping can
    still leak). `line` is the real line in src/main.cpp, not an offset into
    the extracted body text - start_line makes that conversion possible.
    fmt is "" when the format string wasn't on the captured argument
    position."""
    out = defaultdict(list)
    for text, start_line in bodies:
        for regex in (RANGED_WIDGET_RE, DROPDOWNKNOB_RE):
            for m in regex.finditer(text):
                label, field, lo_raw, hi_raw = m.group(1), m.group(2), m.group(3), m.group(4)
                fmt = m.group(5) or ""
                line = start_line + text.count("\n", 0, m.start())
                out[field].append((label, lo_raw.strip(), hi_raw.strip(), fmt, line))
    return out


def find_dsp_transforms(text, claimed_lines):
    """field name -> list of (dsp_line_text, line) for every mailbox read
    that isn't a bare assignment and isn't already reported as a clamp
    (`claimed_lines`, from find_dsp_clamps on the same text) - i.e. every
    other function (powf, DbToLinear, a threshold compare, a combine with a
    second param) that reshapes what the knob's motion means. See BACKEND
    TRANSFORM in the module docstring."""
    field_of = find_field_pushes(text)
    lines = text.split("\n")
    out = defaultdict(list)
    for m in SMOOTHED_RE.finditer(text):
        param_id = m.group(1)
        fields = field_of.get(param_id)
        if not fields:
            continue
        line_no = text.count("\n", 0, m.start()) + 1
        if line_no in claimed_lines:
            continue
        line_text = lines[line_no - 1]
        if TRIVIAL_READ_RE.match(line_text):
            continue
        for field in fields:
            out[field].append((line_text.strip(), line_no))
    return out


# --- node class -> Draw*Body dispatch, same technique audit_node_params.py
# uses: a `dynamic_cast<Klass*>` immediately followed (within a few lines) by
# a call to its Draw function. Reused here so a DSP file's fields are only
# ever compared against *that node's own* knob calls, never another node's.
#
# KNOWN LIMITATION (found, not yet fixed): a class with more than one
# dynamic_cast<Klass*> site in main.cpp - SamplerNode has 5, for different UI
# contexts (main body, drag-preview, a dev fixture, ...) - can have unrelated
# Draw*Body functions unioned into one scope by dispatch_map below, since it
# doesn't distinguish "the" canonical node-body draw from incidental casts.
# Confirmed effect: SamplerNode's real `finetune` knob (-100..100, line
# 14537) and an unrelated node's same-named `finetune` knob (-50..50, line
# 14360) both landed in one scope, so a BACKEND TRANSFORM finding showed two
# disagreeing UI ranges for what should be a single knob. Reader still needs
# to check the cited main.cpp line actually belongs to the node in question
# when more than one UI match is reported for the same field.
def function_bodies(text):
    """name -> (body_text, start_line) - start_line is the 1-based line of
    the opening brace, so callers can convert a within-body offset back to a
    real line number in the source file instead of the extracted substring."""
    out = {}
    for m in re.finditer(r"\n   (?:void|bool)\s+(Draw\w*(?:Body|Params))\s*\(", text):
        name = m.group(1)
        i = text.find("{", m.end())
        if i < 0:
            continue
        depth, j = 0, i
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        start_line = text.count("\n", 0, i) + 1
        out[name] = (text[i:j], start_line)
    return out


def dispatch_map(text):
    out = {}
    lines = text.split("\n")
    for i, line in enumerate(lines):
        m = re.search(r"dynamic_cast<(\w+)\s*\*>", line)
        if not m:
            continue
        klass = m.group(1)
        window = "\n".join(lines[i:i + 4])
        for fn in re.findall(r"\b(Draw\w*(?:Body|Params))\s*\(", window):
            out.setdefault(klass, set()).add(fn)
    return out


def guess_node_class(dsp_path):
    """DSP filename -> best-guess node class name to look up in dispatch_map.
    Best-effort: AnalogSynthCore.h -> AnalogNode, MetallicNode.cpp -> MetallicNode."""
    base = os.path.splitext(os.path.basename(dsp_path))[0]
    if base.endswith("SynthCore"):
        return base[: -len("SynthCore")] + "Node"
    if base.endswith("Node"):
        return base
    return base + "Node"


# --- EffectDefs.cpp: a single function building `defs` as a sequence of
# `{ EffectDef def; def.name = "Delay"; def.params.push_back({...}); ...
# def.makeKernel = []() { return std::make_unique<DelayKernel>(); };
# defs.push_back(std::move(def)); }` blocks, one per AudioEffects node (23
# today). Unlike src/nodes/*.{h,cpp}'s knob calls in main.cpp, this table IS
# the single declared source for both the effect's UI range (it's what
# n->ParamPtr(name)'s caller in main.cpp is supposed to mirror) and the name
# a kernel's own `node.Param("name")` reads - so kernel-class -> {paramName:
# (lo, hi, line)} is unambiguous here with no dispatch_map-style scoping
# heuristic needed, and no scope-leak risk like the one found on SamplerNode.
DEF_NAME_RE = re.compile(r'def\.name\s*=\s*"([^"]+)"')
DEF_PARAM_RE = re.compile(
    r'def\.params\.push_back\(\s*\{\s*"([^"]+)"\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,'
)
DEF_KERNEL_RE = re.compile(r"std::make_unique<(\w+)>")
DEF_FLUSH_RE = re.compile(r"defs\.push_back\(std::move\(def\)\)")


def parse_effect_defs(text):
    """list of {name, kernel, params: {paramName: (lo_raw, hi_raw, line)}},
    one per EffectDef block, in file order."""
    events = []
    for m in DEF_NAME_RE.finditer(text):
        events.append((m.start(), "name", m.group(1)))
    for m in DEF_PARAM_RE.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        events.append((m.start(), "param", (m.group(1), m.group(2).strip(), m.group(3).strip(), line)))
    for m in DEF_KERNEL_RE.finditer(text):
        events.append((m.start(), "kernel", m.group(1)))
    for m in DEF_FLUSH_RE.finditer(text):
        events.append((m.start(), "flush", None))
    events.sort(key=lambda e: e[0])

    out = []
    name, kernel, params = None, None, {}
    for _, kind, data in events:
        if kind == "name":
            name = data
        elif kind == "param":
            pname, lo, hi, line = data
            params[pname] = (lo, hi, line)
        elif kind == "kernel":
            kernel = data
        elif kind == "flush":
            if name and kernel:
                out.append({"name": name, "kernel": kernel, "params": params})
            name, kernel, params = None, None, {}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--node", help="Only report fields whose name contains this substring")
    args = ap.parse_args()

    main_text, locate = appsrc.read_indexed(ROOT)
    bodies = function_bodies(main_text)
    dispatch = dispatch_map(main_text)

    dsp_clamps = []
    dsp_transforms = []
    for fname in sorted(os.listdir(NODES_DIR)):
        if not (fname.endswith(".h") or fname.endswith(".cpp")):
            continue
        path = os.path.join(NODES_DIR, fname)
        text = open(path, encoding="utf-8", errors="replace").read()
        # Widened from requiring std::clamp(...SmoothedValue...) - a file
        # with mailbox reads but no clamp at all (DbToLinear, powf, a
        # threshold compare) still has BACKEND TRANSFORM findings even
        # though it has zero RANGE MISMATCH candidates.
        if "mMailbox.Push(" not in text or "mMailbox.SmoothedValue(" not in text:
            continue
        rel_path = os.path.relpath(path, ROOT)
        clamps = find_dsp_clamps(rel_path, text)
        for clamp in clamps:
            dsp_clamps.append((fname,) + clamp)
        claimed_lines = {ln for *_, ln in clamps}
        for field, transform_list in find_dsp_transforms(text, claimed_lines).items():
            for line_text, line in transform_list:
                dsp_transforms.append((fname, field, line_text, rel_path, line))

    mismatches = []
    untraceable = []
    transforms = []
    unscoped_files = set()
    for fname, field, lo_raw, hi_raw, traceable, path, line in dsp_clamps:
        if args.node and args.node.lower() not in field.lower():
            continue

        klass = guess_node_class(fname)
        fn_names = dispatch.get(klass)
        if not fn_names:
            unscoped_files.add((fname, klass))
            continue
        scope_bodies = [bodies[fn] for fn in fn_names if fn in bodies]
        ui_ranges = find_ui_ranges(scope_bodies)

        if not traceable:
            untraceable.append((field, lo_raw, hi_raw, path, line))
            continue
        dsp_lo, dsp_hi = to_float(lo_raw), to_float(hi_raw)
        for label, ui_lo_raw, ui_hi_raw, _fmt, ui_line in ui_ranges.get(field, []):
            if not (is_numeric(ui_lo_raw) and is_numeric(ui_hi_raw)):
                continue
            ui_lo, ui_hi = to_float(ui_lo_raw), to_float(ui_hi_raw)
            if abs(ui_lo - dsp_lo) > 1e-6 or abs(ui_hi - dsp_hi) > 1e-6:
                direction = "DSP NARROWER (knob can dial in values the DSP clips!)" \
                    if (dsp_lo > ui_lo or dsp_hi < ui_hi) else \
                    "DSP WIDER (dead headroom the knob can never reach)"
                mismatches.append((field, label, ui_lo, ui_hi, dsp_lo, dsp_hi, direction, path, line, ui_line))

    for fname, field, line_text, path, line in dsp_transforms:
        if args.node and args.node.lower() not in field.lower():
            continue
        klass = guess_node_class(fname)
        fn_names = dispatch.get(klass)
        if not fn_names:
            continue  # already reported once via unscoped_files above
        scope_bodies = [bodies[fn] for fn in fn_names if fn in bodies]
        ui_ranges = find_ui_ranges(scope_bodies)
        ui_matches = ui_ranges.get(field, [])
        transforms.append((field, line_text, path, line, ui_matches))

    # --- Second architecture: AudioEffects kernels (src/audio/dsp/*.{h,cpp})
    # against EffectDefs.cpp's declarative table - see parse_effect_defs.
    effect_defs = parse_effect_defs(
        open(EFFECT_DEFS_CPP, encoding="utf-8", errors="replace").read()
    )
    kernel_params = {d["kernel"]: d["params"] for d in effect_defs}

    effect_clamps = []
    effect_transforms_raw = []
    for fname in sorted(os.listdir(EFFECTS_DSP_DIR)):
        if not (fname.endswith(".h") or fname.endswith(".cpp")):
            continue
        path = os.path.join(EFFECTS_DSP_DIR, fname)
        text = open(path, encoding="utf-8", errors="replace").read()
        if "mMailbox.Push(" not in text or "mMailbox.SmoothedValue(" not in text:
            continue
        rel_path = os.path.relpath(path, ROOT)
        clamps = find_dsp_clamps(rel_path, text)
        for clamp in clamps:
            effect_clamps.append((fname,) + clamp)
        claimed_lines = {ln for *_, ln in clamps}
        for field, transform_list in find_dsp_transforms(text, claimed_lines).items():
            for line_text, line in transform_list:
                effect_transforms_raw.append((fname, field, line_text, rel_path, line))

    effect_mismatches = []
    effect_untraceable = []
    effect_transforms = []
    effect_unscoped = set()
    for fname, field, lo_raw, hi_raw, traceable, path, line in effect_clamps:
        if args.node and args.node.lower() not in field.lower():
            continue
        kernel_class = os.path.splitext(fname)[0]
        ui_params = kernel_params.get(kernel_class)
        if not ui_params:
            effect_unscoped.add((fname, kernel_class))
            continue
        if not traceable:
            effect_untraceable.append((field, lo_raw, hi_raw, path, line))
            continue
        ui_entry = ui_params.get(field)
        if not ui_entry:
            continue  # kernel field with no matching EffectDefs.cpp row - internal-only, not a UI claim to check
        ui_lo_raw, ui_hi_raw, def_line = ui_entry
        if not (is_numeric(ui_lo_raw) and is_numeric(ui_hi_raw)):
            continue
        dsp_lo, dsp_hi = to_float(lo_raw), to_float(hi_raw)
        ui_lo, ui_hi = to_float(ui_lo_raw), to_float(ui_hi_raw)
        if abs(ui_lo - dsp_lo) > 1e-6 or abs(ui_hi - dsp_hi) > 1e-6:
            direction = "DSP NARROWER (knob can dial in values the DSP clips!)" \
                if (dsp_lo > ui_lo or dsp_hi < ui_hi) else \
                "DSP WIDER (dead headroom the knob can never reach)"
            effect_mismatches.append(
                (field, kernel_class, ui_lo, ui_hi, dsp_lo, dsp_hi, direction, path, line, def_line))

    for fname, field, line_text, path, line in effect_transforms_raw:
        if args.node and args.node.lower() not in field.lower():
            continue
        kernel_class = os.path.splitext(fname)[0]
        ui_params = kernel_params.get(kernel_class)
        if not ui_params:
            continue  # already reported once via effect_unscoped above
        ui_entry = ui_params.get(field)
        effect_transforms.append((field, line_text, path, line, kernel_class, ui_entry))

    print(f"Scanned {len(dsp_clamps)} mailbox clamp(s) and {len(dsp_transforms)} "
          f"other mailbox transform(s) across src/nodes/*.{{h,cpp}}\n")
    print(f"Scanned {len(effect_clamps)} mailbox clamp(s) and {len(effect_transforms_raw)} "
          f"other mailbox transform(s) across src/audio/dsp/*.{{h,cpp}} "
          f"({len(effect_defs)} effects in EffectDefs.cpp)\n")

    if unscoped_files:
        print("(Could not map to a Draw*Body - skipped, verify these by hand:")
        for fname, klass in sorted(unscoped_files):
            print(f"    {fname} -> guessed class `{klass}`, not found in main.cpp's dispatch")
        print(")\n")

    if mismatches:
        print(f"=== {len(mismatches)} RANGE MISMATCH(ES) ===\n")
        for field, label, ui_lo, ui_hi, dsp_lo, dsp_hi, direction, path, line, ui_line in mismatches:
            print(f'  "{label}" (field `{field}`)')
            print(f"    UI  ({locate(ui_line)}):  {ui_lo:g}..{ui_hi:g}")
            print(f"    DSP ({path}:{line}):  {dsp_lo:g}..{dsp_hi:g}")
            print(f"    -> {direction}\n")
    else:
        print("No numeric range mismatches found.\n")

    if untraceable:
        print(f"=== {len(untraceable)} clamp(s) not mechanically comparable - eyeball these ===\n")
        for field, lo_raw, hi_raw, path, line in untraceable:
            print(f"  `{field}` clamped to ({lo_raw}, {hi_raw}) at {path}:{line}")
        print()

    if transforms:
        print(f"=== {len(transforms)} backend transform(s) - not a clamp, eyeball against the UI claim ===\n")
        for field, line_text, path, line, ui_matches in transforms:
            print(f"  `{field}`: {line_text}")
            print(f"    DSP ({path}:{line})")
            if ui_matches:
                for label, ui_lo, ui_hi, fmt, ui_line in ui_matches:
                    fmt_part = f' fmt="{fmt}"' if fmt else " fmt=? (not captured, check src/main.cpp by hand)"
                    print(f'    UI  ({locate(ui_line)}):  "{label}" {ui_lo}..{ui_hi}{fmt_part}')
            else:
                print("    UI: no matching knob/slider call found in this node's Draw*Body")
            print()

    if effect_unscoped:
        print("(Kernel file with no matching EffectDefs.cpp block - verify by hand:")
        for fname, klass in sorted(effect_unscoped):
            print(f"    {fname} -> class `{klass}`, no `std::make_unique<{klass}>` found in EffectDefs.cpp")
        print(")\n")

    if effect_mismatches:
        print(f"=== {len(effect_mismatches)} AudioEffects RANGE MISMATCH(ES) ===\n")
        for field, klass, ui_lo, ui_hi, dsp_lo, dsp_hi, direction, path, line, def_line in effect_mismatches:
            print(f'  `{field}` ({klass})')
            print(f"    EffectDefs.cpp:{def_line}:  {ui_lo:g}..{ui_hi:g}")
            print(f"    DSP ({path}:{line}):  {dsp_lo:g}..{dsp_hi:g}")
            print(f"    -> {direction}\n")
    else:
        print("No AudioEffects range mismatches found.\n")

    if effect_untraceable:
        print(f"=== {len(effect_untraceable)} AudioEffects clamp(s) not mechanically comparable - eyeball these ===\n")
        for field, lo_raw, hi_raw, path, line in effect_untraceable:
            print(f"  `{field}` clamped to ({lo_raw}, {hi_raw}) at {path}:{line}")
        print()

    if effect_transforms:
        print(f"=== {len(effect_transforms)} AudioEffects backend transform(s) - eyeball against EffectDefs.cpp ===\n")
        for field, line_text, path, line, klass, ui_entry in effect_transforms:
            print(f"  `{field}` ({klass}): {line_text}")
            print(f"    DSP ({path}:{line})")
            if ui_entry:
                ui_lo_raw, ui_hi_raw, def_line = ui_entry
                print(f"    EffectDefs.cpp:{def_line}:  declared {ui_lo_raw}..{ui_hi_raw}")
            else:
                print("    EffectDefs.cpp: no matching param row for this field name")
            print()

    return 1 if (mismatches or effect_mismatches) else 0


if __name__ == "__main__":
    sys.exit(main())
