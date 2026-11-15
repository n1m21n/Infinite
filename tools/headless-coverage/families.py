#!/usr/bin/env python3
"""H9: the headless claim per node family, for the release (plan section 7, "Headless claim, per family").

A family is claimed when every param is covered (effective, mod-only, conditional) or declared (inactive,
live-only, machine-dependent, no-output, text), no panel-visible param is no-effect, no default render
failed, and every type renders the same twice (determinism.json).

  families.py <coverage.json> [--determinism determinism.json] [--out headless-coverage.md]
Writes the markdown (attached to each GitHub release as headless-coverage.md) and a .json beside it.
Exit code is 0 either way: the report is published as it stands, claimed or not.
"""
import argparse, collections, json, os

COVERED = ("effective", "mod-only", "conditional")
DECLARED = ("inactive", "live-only", "machine-dependent", "no-output", "text")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("coverage")
    ap.add_argument("--determinism")
    ap.add_argument("--out")
    a = ap.parse_args()
    rows = json.load(open(a.coverage))
    safe = lambda n: "".join(c if c.isalnum() else "_" for c in n)
    differ = set()  # type dir names (sweep.safe) whose two runs differ
    if a.determinism and os.path.exists(a.determinism):
        det = json.load(open(a.determinism))
        tol = det.get("tol", 1e-3)
        differ = {r["type"] for r in det.get("rows", []) if r.get("diff") is None or r["diff"] > tol}
    fam = collections.defaultdict(lambda: {"types": 0, "params": 0, "covered": 0, "declared": 0, "silent": 0,
                                           "no_effect_visible": [], "no_effect_hidden": 0, "errors": [],
                                           "nondeterministic": []})
    for r in rows:
        f = fam[r["cat"]]
        f["types"] += 1
        if r.get("error"):
            f["errors"].append(r["type"])
        if safe(r["type"]) in differ:
            f["nondeterministic"].append(r["type"])
        for p in r.get("params", []):
            f["params"] += 1
            c = p["class"]
            if c in COVERED:
                f["covered"] += 1
            elif c in DECLARED:
                f["declared"] += 1
            elif c == "silent-default":
                f["silent"] += 1
            elif c == "no-effect":
                if p.get("ui"):
                    f["no_effect_visible"].append(f"{r['type']}.{p['key']}")
                else:
                    f["no_effect_hidden"] += 1
    for f in fam.values():
        f["claimed"] = not (f["no_effect_visible"] or f["errors"] or f["nondeterministic"] or f["silent"]
                            or f["no_effect_hidden"])
    claimed = sorted(c for c, f in fam.items() if f["claimed"])
    L = ["# Infinite headless coverage, per family", "",
         "Every param of every node is changed in a saved patch and rendered headless; a param counts when the",
         "picture or sound changes. Declared params (another mode's fields, live inputs, plugins) are listed, not swept.",
         "A family is **claimed** when every param is covered or declared, no panel knob is dead, and every type",
         "renders the same twice.", "",
         f"Claimed: {', '.join(claimed) or 'none yet'}", "",
         "| Family | Types | Params | Covered | Declared | Silent by default | Dead panel knobs | Nondeterministic | Claimed |",
         "|---|---|---|---|---|---|---|---|---|"]
    for c in sorted(fam):
        f = fam[c]
        L.append(f"| {c} | {f['types']} | {f['params']} | {f['covered']} | {f['declared']} | {f['silent']} | "
                 f"{len(f['no_effect_visible'])} | {len(f['nondeterministic'])} | {'yes' if f['claimed'] else 'no'} |")
    L += ["", "## Open rows", ""]
    for c in sorted(fam):
        f = fam[c]
        if f["claimed"]:
            continue
        bits = []
        if f["no_effect_visible"]:
            bits.append("dead panel knobs: " + ", ".join(f["no_effect_visible"][:40]) +
                        (" …" if len(f["no_effect_visible"]) > 40 else ""))
        if f["no_effect_hidden"]:
            bits.append(f"{f['no_effect_hidden']} hidden params with no effect")
        if f["silent"]:
            bits.append(f"{f['silent']} params untested (silent default)")
        if f["errors"]:
            bits.append("default render failed: " + ", ".join(f["errors"]))
        if f["nondeterministic"]:
            bits.append("differs between runs: " + ", ".join(f["nondeterministic"]))
        L.append(f"- **{c}**: " + "; ".join(bits))
    out = a.out or os.path.join(os.path.dirname(a.coverage), "headless-coverage.md")
    open(out, "w").write("\n".join(L) + "\n")
    json.dump({c: dict(f) for c, f in fam.items()}, open(os.path.splitext(out)[0] + ".json", "w"), indent=1)
    print(f"claimed {len(claimed)}/{len(fam)} families -> {out}")


if __name__ == "__main__":
    main()
