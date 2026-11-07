#!/usr/bin/env python3
"""Blind kit-coherence pages: per family, the kit the genre prior drew (INFINITE_NOCOMPAT=1) against the kit the compat
models picked, full mix and drums only, loudness-matched and order-shuffled (ab_page.py).
    python3 tools/universe/compat_pages.py [--app build-dev/Infinite.app/Contents/MacOS/Infinite] [--out DIR]
Renders through INFINITE_GENSAMPLEBATCH (seeds 1-3, the node's critic redraw included). A row is kept only when both
renders landed on the same seed (same groove) and the audio differs (the kit changed); the rest are counted, not shown.
Writes DIR/<family>/index.html + pairs.json, and DIR/rows.json (what each page holds)."""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
FAMILIES = {"house": ["deep_house", "tech_house"], "techno": ["minimal_techno", "detroit_techno"],
            "dnb": ["liquid_dnb", "jungle"], "hiphop": ["lofi_beats"], "garage": ["uk_garage", "future_garage"],
            "ambient": ["ambient", "dark_ambient"]}
PARTS = ["full", "drums"]


def render(app, out, styles, compat):
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, INFINITE_GENSAMPLEBATCH=out, INFINITE_GENSAMPLEONLY=",".join(styles) + ":" + ",".join(PARTS))
    if not compat:
        env["INFINITE_NOCOMPAT"] = "1"
    r = subprocess.run([app], env=env, capture_output=True, text=True, timeout=1200)
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:])
    return {p["file"]: p for p in json.load(open(os.path.join(out, "pool.json")))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default=os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
    ap.add_argument("--out", default=os.path.expanduser("~/Infinite/training-data/compat/pages"))
    a = ap.parse_args()
    styles = [s for v in FAMILIES.values() for s in v]
    before = render(a.app, os.path.join(a.out, "render-before"), styles, False)
    after = render(a.app, os.path.join(a.out, "render-after"), styles, True)
    rows, summary = {}, []
    for fam, sts in FAMILIES.items():
        args, same, moved = [], 0, 0
        for name, b in sorted(before.items()):
            if b["style"] not in sts or name not in after:
                continue
            if after[name]["seed"] != b["seed"]:
                moved += 1
                continue
            fb, fa = (os.path.join(a.out, d, name + ".wav") for d in ("render-before", "render-after"))
            if open(fb, "rb").read() == open(fa, "rb").read():
                same += 1
                continue
            args.append("%s=%s,%s" % (name, fb, fa))
        rows[fam] = [x.split("=")[0] for x in args]
        summary.append((fam, len(args), same, moved))
        if args:
            subprocess.run([sys.executable, os.path.join(HERE, "ab_page.py"), os.path.join(a.out, fam)] + args, check=True)
    json.dump(rows, open(os.path.join(a.out, "rows.json"), "w"), indent=1)
    print("family   rows  same-kit  other-seed")
    for fam, n, same, moved in summary:
        print("%-8s %4d  %8d  %10d" % (fam, n, same, moved))


if __name__ == "__main__":
    main()
