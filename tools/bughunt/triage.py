#!/usr/bin/env python3
"""Group fuzz failures into distinct bugs and draft a finding for each.

  triage.py OUTDIR [OUTDIR...] [--stubs DIR] [--md FILE]

Reads every results-*.tsv and *.san.* log that param_fuzz.py left in OUTDIR.
A BAD row with a sanitizer log is keyed by the report's kind plus its first two
frames in our own source (src/...), with numbers stripped from the message, so
the 40 rows one bad index produces become one group. A BAD row without a log is
keyed by its outcome (crash code, timeout, new error codes, NaN/hot audio).

Prints a table: group, rows, node types, one repro .inf, the top frames. With
--stubs, writes one finding draft per group (docs/release/findings format,
Status: new, ID left as B-?-???) for a human to check, number and file.
Test: `triage.py selftest`.
"""
import collections
import glob
import os
import re
import sys

FRAME = re.compile(r"#\d+ 0x[0-9a-f]+ in (.+?) (\S+?\.(?:cpp|h|mm|c|hpp)):(\d+)")
UBSAN = re.compile(r"^(\S+?\.(?:cpp|h|mm|c|hpp)):(\d+):\d+: runtime error: (.*)$", re.M)
ASAN = re.compile(r"ERROR: (\w+Sanitizer): ([\w-]+)")


def own(path):
    """Our source: under src/, or a bare basename (UBSan's short stacks), not the SDK or a system lib."""
    if "/src/" in path or path.startswith("src/"):
        return True
    return "/" not in path


def short(path):
    return path.split("/src/", 1)[-1] if "/src/" in path else path


def signature(text):
    """(key, kind, frames[]) of one sanitizer log, or None if it has no report."""
    frames = [(fn.split("(")[0], short(p), int(ln)) for fn, p, ln in FRAME.findall(text)
              if own(p) and not fn.startswith("std::")]
    m = ASAN.search(text)
    if m:
        kind = f"{m.group(1)}: {m.group(2)}"
    else:
        u = UBSAN.search(text)
        if not u:
            return None
        msg = u.group(3)
        # "signed integer overflow: 5 + 9 ..." -> the class only, so + and * on one line are one bug
        msg = msg.split(":", 1)[0] if ":" in msg else msg
        kind = "UBSan: " + re.sub(r"0x[0-9a-f]+|-?\d+(\.\d+)?(e[+-]?\d+)?", "N", msg)
        if not frames:
            frames = [("?", short(u.group(1)), int(u.group(2)))]
    top = frames[:2]
    key = kind + " @ " + " <- ".join(f"{fn} {p}:{ln}" for fn, p, ln in top)
    return key, kind, frames[:6]


def collect(dirs):
    groups = collections.OrderedDict()
    for d in dirs:
        for res in sorted(glob.glob(os.path.join(d, "results-*.tsv"))):
            kind = os.path.basename(res)[8:-4]
            for line in open(res):
                f = line.rstrip("\n").split("\t")
                if len(f) < 7 or f[-1] != "BAD":
                    continue
                typ, label, rc, codes = f[0], f[1], f[2], f[3]
                safe = re.sub(r"[^A-Za-z0-9]+", "_", typ)
                tag = f"{kind}-{safe}-{label.lstrip('-').replace('.', '')}{'n' if label.startswith('-') else ''}"
                logs = glob.glob(os.path.join(d, tag + ".san*"))
                sig = None
                for lg in logs:
                    sig = signature(open(lg, errors="replace").read())
                    if sig:
                        break
                if sig:
                    key, what, frames = sig
                else:
                    if rc == "TIMEOUT":
                        what = "timeout"
                    elif rc.lstrip("-").isdigit() and (int(rc) < 0 or int(rc) >= 128):
                        what = "crash (signal %s)" % rc
                    elif rc not in ("0", "3"):
                        what = "exit %s" % rc
                    else:
                        what = "new codes " + (codes.strip(",") or "(none)")
                    key, frames = what + " @ " + typ, []
                g = groups.setdefault(key, {"what": what, "frames": frames, "rows": [], "types": set()})
                g["rows"].append((kind, typ, label, os.path.join(d, tag + ".inf")))
                g["types"].add(typ)
    return groups


def stub(g):
    kind, typ, label, inf = g["rows"][0]
    top = "\n".join(f"    {fn} {p}:{ln}" for fn, p, ln in g["frames"]) or "    (no sanitizer stack)"
    types = ", ".join(sorted(g["types"]))
    return f"""ID:            B-?-???
Title:         {g['what']} in {types}
Severity:      S?
Priority:      P?
Category:      K
Status:        new
Found by:      param_fuzz.py --kind {kind} (auto-drafted by triage.py; check before filing)
Date:
Build:
Regression:    unknown
Fix:           -
Verified by:   -
Waiver:        -
Test:          {inf}

## Repro

Run `{os.path.basename(inf)}` headless under the ASan+UBSan build ({len(g['rows'])} fuzz rows hit this; first: {typ} = {label}).

## Actual

{g['what']}

```
{top}
```

## Expected

The value is clamped or refused on load, and the node keeps running.
"""


def selftest():
    ub = ("/x/src/app/bodies/G.cpp:1585:88: runtime error: index 7 out of bounds for type 'float[7]'\n"
          "    #0 0x1 in app::DrawRamp(Node*) G.cpp:1585\n")
    k1 = signature(ub)
    k2 = signature(ub.replace("index 7", "index 9"))
    assert k1 and k2 and k1[0] == k2[0], (k1, k2)
    asan = ("==1==ERROR: AddressSanitizer: stack-use-after-scope on address 0x1\n"
            "    #0 0x1 in app::Apply(int) /r/src/app/CookFrame.cpp:200\n"
            "    #1 0x2 in libc_thing /usr/lib/x.c:3\n"
            "    #2 0x3 in app::Prime(int&) /r/src/app/Headless.cpp:583\n")
    key, kind, frames = signature(asan)
    assert kind == "AddressSanitizer: stack-use-after-scope" and len(frames) == 2, (key, frames)
    assert "app/CookFrame.cpp:200" in key and "x.c" not in key
    assert signature("nothing here") is None
    vec = ("/x/src/n/A.cpp:9:1: runtime error: addition of unsigned offset\n"
           "    #0 0x1 in std::vector<int>::operator[] /Library/SDK/usr/include/c++/v1/vector.h:420\n"
           "    #1 0x2 in app::Chorder(int) NoteNodes.cpp:77\n")
    assert "NoteNodes.cpp:77" in signature(vec)[0] and "vector.h" not in signature(vec)[0]
    print("triage selftest ok")


def main():
    a = sys.argv[1:]
    if a[:1] == ["selftest"]:
        return selftest()
    stubs = a[a.index("--stubs") + 1] if "--stubs" in a else None
    md = a[a.index("--md") + 1] if "--md" in a else None
    dirs = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or a[i - 1] not in ("--stubs", "--md"))]
    if not dirs:
        sys.exit(__doc__)
    groups = collect(dirs)
    out = ["| # | Rows | Types | What | Top frame | Repro |", "|---|---|---|---|---|---|"]
    for i, g in enumerate(groups.values(), 1):
        top = "%s %s:%d" % g["frames"][0] if g["frames"] else "-"
        types = ", ".join(sorted(g["types"]))
        out.append(f"| {i} | {len(g['rows'])} | {types} | {g['what']} | `{top}` | `{os.path.basename(g['rows'][0][3])}` |")
        if stubs:
            os.makedirs(stubs, exist_ok=True)
            with open(os.path.join(stubs, f"draft-{i:03d}.md"), "w") as f:
                f.write(stub(g))
    text = "\n".join(out) + f"\n\n{len(groups)} distinct groups from {sum(len(g['rows']) for g in groups.values())} BAD rows.\n"
    print(text)
    if md:
        open(md, "w").write(text)


if __name__ == "__main__":
    main()
