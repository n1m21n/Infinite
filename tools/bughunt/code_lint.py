#!/usr/bin/env python3
"""Static checks for bug classes the 2026-10-22 bug hunt found by hand.

  code_lint.py [--update-baseline] [--all]

stack-control   A float widget that registers its pointer as ParamRef::value
                (ModSlider, ModKnob, AudioSlider, AudioKnob) given `&local`, where
                `local` is a plain float/int/bool declared in the same function.
                Modulation reads and writes it every frame after the local is
                gone. B-A-004/B-K-018. The *Int and Checkbox widgets keep a
                persistent shadow and accept temporaries; the raw *Float widgets
                (KnobFloat, AudioSliderFloat) do not register.

loaded-index    `v.Int("key", m)` in a VisitParams, then `m` used as an array
                index (`[m]`, `[n->m]`, `[node->m]`, `Names()[...m]`) in that
                file or in src/app/bodies, with no clamp of `m` (std::clamp,
                std::min/max, `%`, a `m = ` assignment) within 4 lines after the
                load. A patch file sets ints with a bare atoi. B-K-015/016/019.
                Heuristic: an index guarded at the use site is a false positive,
                so known hits live in code_lint_baseline.txt and only NEW hits
                fail. Review a new hit; if it is guarded, --update-baseline.

unbounded-int   An int widget with a range (ModSliderInt, KnobInt, AudioSliderInt
                given `&n->m, lo, hi`) whose member `m` is never bounded outside
                src/app: no std::clamp/min/max of `m` or of its audio-side copy
                `mM`, and no `m < x ?` range check. The widget's range binds
                only the UI; a patch file or remote set stores any int. Molder's
                generation looped 2^31 Mutates on the main thread (B-K-029);
                Note Echo's repeats past 15 shared voice ids (B-K-030). The int
                sweep missed both because the loops need a file or notes.
                Bounded deeper (Mesh::Screw caps steps) is a false positive:
                review it and --update-baseline.

loaded-loop     `v.Int("key", m)` in a VisitParams, then `m` (or `n.m`, `node->m`)
                as a for-loop bound (`i < m`, `i <= m`) in that file, with no
                clamp of `m` anywhere in the file. 2^31 iterations from a patch
                file is a hang. Molder replayed one Mutate per generation on
                the main thread (B-K-029).

shader-loop     A GLSL loop (inside a C++ string literal) whose bound is not an
                integer literal. A bound fed by a uniform runs as many times as
                the CPU side uploads: dither looped uMode + 1 times and a patch
                value of 1e9 hung the GPU, then the app aborted (B-K-035). Trace
                the bound to its upload and check it is clamped there (or that
                its node clamps the param on load), then --update-baseline.

clamp-bounds    std::clamp(v, lo, hi) in a node, its body or the DSP where lo and
                hi are both runtime values (start..end, minPos..maxPos). A patch
                can store start > end, and std::clamp with hi < lo is undefined:
                release returns a bound, but the hardened library in sanitizer
                builds aborts (Sampler's clamp turned Linux ASan CI red). Order
                the bounds first, e.g. std::max(start + eps, end), or check the
                pair can't invert, then --update-baseline.

Exit 1 on any hit not in the baseline (--all: on any hit). Test: `code_lint.py selftest`.
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BASELINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "code_lint_baseline.txt")

# Only the float widgets store the pointer itself (ParamRef::value). The *Int
# and Checkbox ones keep their own persistent shadow and are built to take a
# temporary (`bool tmp = n->Get(); if (ModCheckbox(.., &tmp)) n->Set(tmp);`).
REGISTERING = r"\b(?:ModSlider|ModKnob|AudioSlider|AudioKnob)"
CALL = re.compile(REGISTERING + r"\s*\(\s*[^,]+,\s*&\s*([A-Za-z_]\w*)\s*[,)]")
FUNC_START = re.compile(r"^\s{0,3}[A-Za-z_][\w:<>,\s\*&]*\b[\w:~]+\s*\([^;]*\)\s*(const)?\s*(\{|$)")
RANGED = re.compile(r'\b(?:ModSliderInt|KnobInt|AudioSliderInt)\(\s*"[^"]*"\s*,\s*&\s*\w+(?:->|\.)(?:\w+(?:->|\.))*(\w+)\s*,\s*([^,)]+),\s*([^,)]+)')
LOAD = re.compile(r'\bv\.Int\(\s*"([^"]+)"\s*,\s*&?\s*([A-Za-z_]\w*(?:\.\w+)?)\s*\)')


def src_files():
    out = []
    for ext in ("cpp", "h", "mm", "hpp"):
        out += glob.glob(os.path.join(ROOT, "src", "**", "*." + ext), recursive=True)
    return sorted(out)


def rel(p):
    return os.path.relpath(p, ROOT)


def stack_control(path, lines):
    hits = []
    for i, line in enumerate(lines):
        for m in CALL.finditer(line):
            name = m.group(1)
            decl = re.compile(r"^\s*(?:const\s+)?(?:float|int|bool|double)\s+(?:[\w\s,=\.\(\)\-\+\*/]*,\s*)?" + name + r"\s*(=|;|\{|,)")
            for j in range(i, max(-1, i - 80), -1):
                if j < i and FUNC_START.match(lines[j]) and not lines[j].lstrip().startswith(("if", "for", "while", "switch", "return")):
                    break
                if decl.search(lines[j]) and "static" not in lines[j]:
                    hits.append(("stack-control", f"{rel(path)}:{i + 1}", f"&{name} (declared line {j + 1})"))
                    break
    return hits


def loaded_index(files, texts):
    bodies = "\n".join(t for p, t in texts.items() if "/src/app/bodies/" in p)
    hits = []
    for path in files:
        lines = texts[path].splitlines()
        for i, line in enumerate(lines):
            for m in LOAD.finditer(line):
                member = m.group(2).split(".")[-1]
                after = "\n".join(lines[i:i + 5])
                clamped = re.search(r"(std::clamp|std::min|std::max|%)[^\n]*\b" + member + r"\b", after) or \
                    re.search(r"\b" + member + r"\s*=(?!=)", "\n".join(lines[i + 1:i + 5]))
                # a clamp anywhere in the file (often in Cook) also counts
                clamped = clamped or re.search(r"\b" + member + r"\s*=\s*(std::clamp|std::min|std::max)\s*\(\s*" + member + r"\b", texts[path])
                if clamped:
                    continue
                # an index, not a lambda capture `[m](`
                idx = re.compile(r"\[\s*(?:\(\w+\)\s*)?(?:\w+(?:->|\.))?" + member + r"\s*\](?!\s*\()")
                where = [p for p, t in ((path, texts[path]), ("src/app/bodies", bodies)) if idx.search(t)]
                if where:
                    hits.append(("loaded-index", f"{rel(path)}:{i + 1}", f"{m.group(1)} -> [{member}] in {rel(where[0]) if where[0] != 'src/app/bodies' else where[0]}"))
    return hits


def loaded_loop(files, texts):
    hits = []
    for path in files:
        text = texts[path]
        for m in LOAD.finditer(text):
            member = m.group(2).split(".")[-1]
            # only an upper bound stops a runaway loop: std::max(0, m - 1) does not count,
            # a local of the same name taken from a clamped atomic (`n = std::clamp(mN...`) does
            if re.search(r"(std::clamp|std::min|Clamp)\s*\([^;]*\b" + member + r"\b", text) or \
                    re.search(r"\b" + member + r"\s*=\s*(std::clamp|std::min|Clamp)\s*\(", text):
                continue
            loop = re.search(r"for\s*\([^;]*;[^;]*<=?\s*(?:\w+(?:->|\.))?" + member + r"\s*;", text)
            if loop:
                line = text.count("\n", 0, loop.start()) + 1
                hits.append(("loaded-loop", f"{rel(path)}:{line}", f"{m.group(1)} -> loop bound {member}"))
    return hits


def unbounded_int(files, texts):
    node_side = "\n".join(t for p, t in texts.items() if "/src/app/" not in p)
    hits, seen = [], set()
    for path in files:
        if "/src/app/bodies/" not in path:
            continue
        for i, line in enumerate(texts[path].splitlines()):
            for m in RANGED.finditer(line):
                member = m.group(1)
                if member in seen:
                    continue
                seen.add(member)
                names = member + "|m" + member[:1].upper() + member[1:]
                bounded = re.search(r"(std::clamp|std::min|std::max|Clamp)\s*\([^;]*\b(" + names + r")\b", node_side) or \
                    re.search(r"\b(" + names + r")\s*[<>]=?\s*[\w.:]+\s*\?", node_side)
                if not bounded:
                    hits.append(("unbounded-int", f"{rel(path)}:{i + 1}", f"{member} [{m.group(2).strip()},{m.group(3).strip()}] not bounded outside the UI"))
    return hits


SHADER_LOOP = re.compile(r'^\s*R?"[^"\n]*\bfor\s*\(\s*int\s+(\w+)\s*=[^;]*;\s*\1\s*<=?\s*([^;]+?)\s*;')


def shader_loop(path, text):
    hits = []
    for i, line in enumerate(text.splitlines()):
        m = SHADER_LOOP.search(line)
        if m and not re.fullmatch(r"\d+", m.group(2)):
            hits.append(("shader-loop", f"{rel(path)}:{i + 1}", f"{m.group(1)} < {m.group(2)}"))
    return hits


CLAMP_CALL = re.compile(r"std::clamp\s*\(")
CLAMP_BOUND = re.compile(r"^\(?(\(\w+\))?\s*(n->|this->)?[a-z]\w*(\.\w+|->\w+)?\)?$")
CLAMP_DIRS = ("/nodes/", "/audio/", "/app/bodies/")


def call_args(s):
    """Top-level comma-separated arguments of a call whose '(' was just consumed."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            if depth == 0:
                break
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        cur += ch
    out.append(cur.strip())
    return out


def clamp_bounds(path, text):
    if not any(d in path for d in CLAMP_DIRS):
        return []
    hits = []
    for i, line in enumerate(text.splitlines()):
        for m in CLAMP_CALL.finditer(line):
            a = call_args(line[m.end():])
            if len(a) < 3:
                continue
            lo, hi = a[1], a[2]
            if all(CLAMP_BOUND.match(b) and not re.match(r"^\(?(\(\w+\))?\s*k[A-Z]", b) for b in (lo, hi)):
                hits.append(("clamp-bounds", f"{rel(path)}:{i + 1}", f"{lo} .. {hi}"))
    return hits


def run(paths=None):
    files = paths or src_files()
    texts = {p: open(p, encoding="utf-8", errors="replace").read() for p in files}
    hits = []
    for p in files:
        if "/app/" in p:
            hits += stack_control(p, texts[p].splitlines())
    hits += loaded_index(files, texts)
    hits += unbounded_int(files, texts)
    hits += loaded_loop(files, texts)
    for p in files:
        hits += shader_loop(p, texts[p])
        hits += clamp_bounds(p, texts[p])
    return hits


def key(h):
    # line numbers drift; the baseline matches on rule, file and detail
    return f"{h[0]}\t{h[1].rsplit(':', 1)[0]}\t{h[2].split(' (declared')[0]}"


def selftest():
    import tempfile
    d = tempfile.mkdtemp()
    a = os.path.join(d, "src", "app", "bodies", "X.cpp")
    os.makedirs(os.path.dirname(a))
    open(a, "w").write("""void DrawX(XNode* n)
{
   float u = (float)n->unison;
   AudioSlider("unison", &u, 1, 8, "%.0f", 10.0f);
   AudioSliderInt("ok", &n->seed, 1, 9, 10.0f);
   int tmpI = n->count;
   ModSliderInt("int temp is fine", &tmpI, 1, 9);
   static float s = 1;
   ModSlider("s", &s, 0, 1, "%.2f");
   KnobFloat("raw", &u, 0, 1, "%.2f");
   names[n->mode];
   row.KnobInt("repeats", &n->repeats, 1, 8);
   ModSliderInt("voices", &n->voices, 1, 16);
   ModSliderInt("steps", &n->steps, 1, 64);
}
""")
    b = os.path.join(d, "src", "nodes", "XNode.h")
    os.makedirs(os.path.dirname(b))
    open(b, "w").write("""void VisitParams(V& v) {
   v.Int("mode", mode);
   v.Int("count", count);
   count = std::clamp(count, 1, 8);
   v.Int("free", freeInt);
}
void Push(const X& n) { mVoices.store(std::clamp(n.voices, 1, 16)); }
int Steps() const { return steps < 1 ? 1 : steps; }
void Build() { v.Int("generation", gen); for (int i = 0; i < gen; ++i) Mutate(); }
float t[4]; float g() { return t[count]; }
void Play(float& p) { p = std::clamp(p, start, end); p = std::clamp(p, 0.0f, end); p = std::clamp(p, (int)kLo, (int)kHi); }
const char* kFrag = "uniform int uMode;\\n"
    "   for (int i = 0; i < uMode; i++) {\\n"
    "   for (int k = 0; k < 16; k++) {\\n";
""")
    hits = run([a, b])
    rules = sorted((h[0], h[2].split(" ")[0]) for h in hits)
    assert ("stack-control", "&u") in rules, hits
    assert not any(r[0] == "stack-control" and ("&s" == r[1] or "seed" in r[1]) for r in rules), hits
    assert ("loaded-index", "mode") in rules, hits
    assert not any(r[1] in ("count", "free") for r in rules), hits
    assert len([r for r in rules if r[0] == "stack-control"]) == 1, hits
    assert ("unbounded-int", "repeats") in rules, hits
    assert not any(r[0] == "unbounded-int" and r[1] == "tmpI" for r in rules), hits
    assert ("loaded-loop", "generation") in rules, hits
    assert not any(r[0] == "loaded-loop" and r[1] == "count" for r in rules), hits
    assert not any(r[0] == "unbounded-int" and r[1] in ("voices", "steps") for r in rules), hits
    assert ("shader-loop", "i") in rules, hits
    assert len([r for r in rules if r[0] == "shader-loop"]) == 1, hits
    assert [r for r in rules if r[0] == "clamp-bounds"] == [("clamp-bounds", "start")], hits
    print("code_lint selftest ok")


def main():
    if sys.argv[1:2] == ["selftest"]:
        return selftest()
    hits = run()
    if "--update-baseline" in sys.argv:
        with open(BASELINE, "w") as f:
            f.write("# code_lint.py accepted hits: rule<TAB>file<TAB>detail. Each was reviewed and is guarded at the use site.\n")
            for k in sorted({key(h) for h in hits}):
                f.write(k + "\n")
        print(f"baseline: {len(hits)} hits written to {rel(BASELINE)}")
        return
    known = set()
    if os.path.exists(BASELINE) and "--all" not in sys.argv:
        known = {l.rstrip("\n") for l in open(BASELINE) if l.strip() and not l.startswith("#")}
    new = [h for h in hits if key(h) not in known]
    for h in new:
        print(f"{h[1]}: [{h[0]}] {h[2]}")
    print(f"code_lint: {len(hits)} hits, {len(new)} new")
    sys.exit(1 if new else 0)


if __name__ == "__main__":
    main()
