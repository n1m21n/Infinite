"""Compare two patchcompat.ps1 runs (reference build vs candidate) and write a Markdown report.

   python compare-patchcompat.py <ref_dir> <new_dir> <report.md> [--img-tol 0.04] [--mod-tol 0.02]

Per patch it flags: a verdict change (ok -> crash / hang / nojson), a different node list,
and per node a different output size, a 4x4 thumbnail differing by more than --img-tol in
any channel, a different triangle / vertex / point count, or a modulator value differing by
more than --mod-tol. Unknown node types and unread param keys in the candidate are summed
over the whole corpus (how many patches use each), which is the upstream interop list.
"""
import argparse
import collections
import csv
import json
import os


def load_status(d):
    out = {}
    p = os.path.join(d, "status.csv")
    if os.path.exists(p):
        with open(p, encoding="utf-8-sig") as f:
            for row in csv.DictReader(f):
                out[row["patch"]] = row["verdict"]
    return out


def load_json(d, name):
    p = os.path.join(d, name + ".json")
    if not os.path.exists(p):
        return None
    try:
        with open(p, encoding="utf-8", errors="replace") as f:
            return json.load(f)
    except ValueError:
        return None


def node_diffs(a, b, img_tol, mod_tol):
    out = []
    for k in ("links", "mods"):
        if k in a and k in b and a[k] != b[k]:
            out.append("%s %s -> %s" % (k, a[k], b[k]))
    if [n["type"] for n in a["nodes"]] != [n["type"] for n in b["nodes"]]:
        ta = collections.Counter(n["type"] for n in a["nodes"])
        tb = collections.Counter(n["type"] for n in b["nodes"])
        out.append("node list differs: ref-only %s, new-only %s" % (dict(ta - tb), dict(tb - ta)))
        return out
    for na, nb in zip(a["nodes"], b["nodes"]):
        tag = "#%s %s" % (na["i"], na["type"])
        if (na.get("w"), na.get("h")) != (nb.get("w"), nb.get("h")):
            out.append("%s size %sx%s -> %sx%s" % (tag, na.get("w"), na.get("h"), nb.get("w"), nb.get("h")))
        ia, ib = na.get("img"), nb.get("img")
        if (ia is None) != (ib is None):
            out.append("%s image %s -> %s" % (tag, "yes" if ia else "none", "yes" if ib else "none"))
        elif ia and ib:
            d = max(abs((x or 0) - (y or 0)) for x, y in zip(ia, ib))
            if d > img_tol:
                out.append("%s image differs (max %.3f)" % (tag, d))
        for k in ("tris", "verts", "points"):
            if na.get(k) != nb.get(k):
                out.append("%s %s %s -> %s" % (tag, k, na.get(k), nb.get(k)))
        ma, mb = na.get("mod"), nb.get("mod")
        if (ma is None) != (mb is None) or (ma is not None and mb is not None and abs(ma - mb) > mod_tol):
            out.append("%s mod %s -> %s" % (tag, ma, mb))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("new")
    ap.add_argument("report")
    ap.add_argument("--img-tol", type=float, default=0.04)
    ap.add_argument("--mod-tol", type=float, default=0.02)
    ap.add_argument("--gate", action="store_true",
                    help="Turbo 0.70: exit 1 when a patch regresses (ok -> crash / hang / nojson)")
    args = ap.parse_args()

    sr, sn = load_status(args.ref), load_status(args.new)
    # Turbo 0.70: pair runs by patch name (without compat.ps1's "NNN_" order
    # prefix; the k-th patch of a repeated name with the k-th), so a patch
    # added to a corpus folder does not shift every pair after it.
    def keyed(status):
        seen, out = collections.Counter(), {}
        for run in sorted(status):
            head, _, tail = run.partition("_")
            base = tail if head.isdigit() and tail else run
            out[(base, seen[base])] = run
            seen[base] += 1
        return out
    kr, kn = keyed(sr), keyed(sn)
    keys = sorted(set(kr) | set(kn))
    names = [k[0] if k[1] == 0 else "%s (%d)" % (k[0], k[1] + 1) for k in keys]
    new_only = [n for k, n in zip(keys, names) if k not in kr]
    verdicts = collections.Counter()
    regressions, fixed, differing = [], [], []
    unknown_types = collections.Counter()
    unknown_keys = collections.Counter()
    ref_unknown_types = collections.Counter()
    clean = 0
    for key, name in zip(keys, names):
        rrun, nrun = kr.get(key), kn.get(key)
        vr = sr.get(rrun, "missing") if rrun else "no reference"
        vn = sn.get(nrun, "missing") if nrun else "missing"
        verdicts[vn] += 1
        if rrun is None:
            pass  # a patch added since the reference: listed below, not a regression
        elif vr == "ok" and vn != "ok":
            regressions.append("%s: %s -> %s" % (name, vr, vn))
        elif vr != "ok" and vn == "ok":
            fixed.append("%s: %s -> %s" % (name, vr, vn))
        jr = load_json(args.ref, rrun) if rrun else None
        jn = load_json(args.new, nrun) if nrun else None
        if jn:
            for t in jn.get("unknownTypes", []):
                unknown_types[t] += 1
            for k in set(jn.get("unknownKeys", [])):
                unknown_keys[k] += 1
        if jr:
            for t in jr.get("unknownTypes", []):
                ref_unknown_types[t] += 1
        if jr and jn:
            d = node_diffs(jr, jn, args.img_tol, args.mod_tol)
            if d:
                differing.append((name, d))
            else:
                clean += 1

    L = ["# Patch compatibility: reference vs candidate", ""]
    L.append("- ref: `%s`" % args.ref)
    L.append("- new: `%s`" % args.new)
    L.append("- patches: %d; candidate verdicts: %s" % (len(names), dict(verdicts)))
    L.append("- identical within tolerance: %d; differing: %d (img tol %.3f, mod tol %.3f)"
             % (clean, len(differing), args.img_tol, args.mod_tol))
    L.append("")
    L.append("## Regressions (ok in ref, not ok in new): %d" % len(regressions))
    L += ["- " + r for r in regressions]
    L.append("")
    L.append("## Fixed (not ok in ref, ok in new): %d" % len(fixed))
    L += ["- " + r for r in fixed]
    L.append("")
    L.append("## New patches, no reference yet (record one with -RecordRef): %d" % len(new_only))
    L += ["- " + n for n in new_only]
    L.append("")
    L.append("## Unknown node types in new (patches using each)")
    for t, c in unknown_types.most_common():
        L.append("- %s: %d" % (t, c))
    L.append("")
    L.append("## Unknown types now known (ref unknown, new known)")
    for t, c in ref_unknown_types.most_common():
        if t not in unknown_types:
            L.append("- %s: %d" % (t, c))
    L.append("")
    L.append("## Unread param keys in new (patches using each)")
    for k, c in unknown_keys.most_common():
        L.append("- %s: %d" % (k, c))
    L.append("")
    L.append("## Per-patch differences")
    for name, d in differing:
        L.append("### " + name)
        L += ["- " + x for x in d]
    with open(args.report, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print("report: %s (%d regressions, %d differing, %d unknown types, %d unknown keys)"
          % (args.report, len(regressions), len(differing), len(unknown_types), len(unknown_keys)))
    if args.gate and regressions:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
