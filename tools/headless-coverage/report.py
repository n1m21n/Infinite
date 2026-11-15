#!/usr/bin/env python3
"""coverage.json -> report.md: headline numbers, per-category table, review lists."""
import collections, json, os, sys

CLASSES = ["effective", "mod-only", "conditional", "no-effect", "inactive", "silent-default", "error", "live-only", "machine-dependent", "no-output", "text"]
DECLARED = ("text", "inactive", "live-only", "machine-dependent", "no-output")  # not swept: declared


def main(path):
    rows = json.load(open(path))
    tot = collections.Counter()
    mod = collections.Counter()
    cats = collections.defaultdict(collections.Counter)
    for r in rows:
        for p in r.get("params", []):
            tot[p["class"]] += 1
            cats[r["cat"]][p["class"]] += 1
            if "mod" in p:
                mod[p["mod"]] += 1
    n = sum(tot.values())
    swept = n - sum(tot[c] for c in DECLARED)
    covered = tot["effective"] + tot["mod-only"] + tot["conditional"]
    L = ["# Headless coverage", "",
         f"{len(rows)} node types, {n} params. Swept (not declared: {', '.join(DECLARED)}): {swept}.", "",
         "| Class | Params | Share of swept |", "|---|---|---|"]
    for c in CLASSES:
        share = f"{100 * tot[c] / swept:.1f} %" if swept and c not in DECLARED else "–"
        L.append(f"| {c} | {tot[c]} | {share} |")
    testable = swept - tot["silent-default"] - tot["error"]
    L += ["", f"Covered (effective, mod-only or conditional): **{covered} / {swept} = {100 * covered / max(swept, 1):.1f} %**",
          f"Of testable params (not silent-default, not error): **{covered} / {testable} = {100 * covered / max(testable, 1):.1f} %**", "",
          "| Modulation | Params |", "|---|---|"] + [f"| {k} | {v} |" for k, v in mod.most_common()]
    L += ["", "## By category", "", "| Category | " + " | ".join(CLASSES) + " |", "|---|" + "---|" * len(CLASSES)]
    for c in sorted(cats):
        L.append(f"| {c} | " + " | ".join(str(cats[c][k]) for k in CLASSES) + " |")
    bad = [r for r in rows if r.get("error") or r.get("batch_leak")]
    if bad:
        L += ["", "## Types with a failed default render or batch state leak", "", "| Type | Problem |", "|---|---|"]
        L += [f"| {r['type']} | {r.get('error') or 'batch leak ' + str(r.get('batch_leak'))} {r.get('error_detail', '')} |" for r in bad]
    sil = [r for r in rows if r.get("silent_default")]
    if sil:
        L += ["", "## Types with a silent or blank default (need content or setup to test)", "",
              "| Type | Channel | Untested params |", "|---|---|---|"]
        L += [f"| {r['type']} | {r['channel']} | {sum(p['class'] == 'silent-default' for p in r['params'])} |" for r in sil]
    L += ["", "## No-effect params (review list)", "", "| Type | Params |", "|---|---|"]
    for r in rows:
        ks = [p["key"] for p in r.get("params", []) if p["class"] == "no-effect"]
        if ks:
            L.append(f"| {r['type']} | {', '.join(ks)} |")
    out = os.path.join(os.path.dirname(path), "report.md")
    open(out, "w").write("\n".join(L) + "\n")
    print(f"covered {covered}/{swept} ({100 * covered / max(swept, 1):.1f} %), of testable {covered}/{testable} "
          f"({100 * covered / max(testable, 1):.1f} %) -> {out}")


if __name__ == "__main__":
    main(sys.argv[1])
