"""Move files from v1 colours to the v2 brand, using the mapping in docs/brand/brand.json.

What: rewrites hex and rgb()/rgba() colours in place: every retired colour to its `replace`, every entry in
colour.migrate to its target, and (with --snap) any colour within deltaE OK 1.0 of a brand colour to that colour.
Alpha is kept (8-digit hex suffix, rgba alpha); the hex case of the source is kept.
Why: the website, films and slides hand-copied v1 values. One mapping, applied the same way everywhere, then
tools/brand/brand_lint.py proves the result.

Usage:
    python3 tools/brand/migrate_colours.py PATH [PATH ...]            # dry run: prints every change
    python3 tools/brand/migrate_colours.py --apply [--snap] PATH ...  # writes the files

Exit codes: 0 done (or nothing to change), 2 usage error.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import brand_lint as L  # noqa: E402
import colour as K  # noqa: E402

ROOT = L.ROOT
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))


def mapping(snap_pool=None):
    m = {L.norm(r["hex"]): L.norm(r["replace"]) for r in B["colour"]["retired"] if r.get("replace")}
    m.update({L.norm(k): L.norm(v) for k, v in B["colour"].get("migrate", {}).items() if not k.startswith("_")})
    return m


def target(h, m, pool):
    if h in m:
        return m[h]
    if pool is not None and h not in pool:
        best = min(pool, key=lambda c: K.delta_e(h, c))
        if K.delta_e(h, best) <= 1.0:
            return best
    return None


def rewrite(text, m, pool):
    changes = []

    def hex_sub(mo):
        raw = mo.group(0)
        t = target(L.norm(raw), m, pool)
        if not t:
            return raw
        body = raw[1:]
        alpha = body[6:8] if len(body) == 8 else (body[3] * 2 if len(body) == 4 else "")
        new = "#" + t[1:] + alpha
        new = new if raw[1:].isupper() or not raw[1:].isalpha() and raw == raw.upper() else new.lower()
        changes.append((raw, new))
        return new

    def rgb_sub(mo):
        r, g, b = (int(x) for x in mo.groups())
        t = target("#%02X%02X%02X" % (r, g, b), m, pool)
        if not t:
            return mo.group(0)
        nr, ng, nb = (int(t[i:i + 2], 16) for i in (1, 3, 5))
        new = mo.group(0).replace(mo.group(1), str(nr), 1)
        new = re.sub(r"^(rgba?\(\s*\d+[\s,]+)\d+", lambda q: q.group(1) + str(ng), new)
        new = re.sub(r"^(rgba?\(\s*\d+[\s,]+\d+[\s,]+)\d+", lambda q: q.group(1) + str(nb), new)
        changes.append((mo.group(0), new))
        return new

    text = L.HEX.sub(hex_sub, text)
    text = L.RGB.sub(rgb_sub, text)
    return text, changes


def main():
    args = sys.argv[1:]
    flags = {a for a in args if a.startswith("--")}
    paths = [a for a in args if not a.startswith("--")]
    if not paths or flags - {"--apply", "--snap"}:
        print(__doc__)
        return 2
    allowed, retired, _ = L.brand_colours()
    pool = sorted(allowed) if "--snap" in flags else None
    m = mapping()
    total = 0
    for f in L.files(paths):
        src = open(f, errors="ignore").read()
        out, ch = rewrite(src, m, pool)
        if not ch:
            continue
        total += len(ch)
        rel = os.path.relpath(f, ROOT)
        seen = {}
        for a, b in ch:
            seen[(a, b)] = seen.get((a, b), 0) + 1
        print(f"{rel}: {len(ch)} changes  " + ", ".join(f"{a}->{b}" + (f" x{n}" if n > 1 else "") for (a, b), n in seen.items()))
        if "--apply" in flags:
            open(f, "w").write(out)
    print(f"{'applied' if '--apply' in flags else 'dry run'}: {total} changes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
