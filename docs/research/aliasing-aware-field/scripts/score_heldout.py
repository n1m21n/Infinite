#!/usr/bin/env python3
"""Scores phase 4 (README section 13): H1b per held-out set and H3 against two simple rules.

What: reads the CSV the app writes with INFINITE_FIELDHELDOUT (one row per kernel: status, memoryless flag, measured
      and predicted alias at 1x-32x, measured and predicted factor) plus the kernels' source under corpus/heldout/,
      and the phase-0 table (data/phase1-predictions.csv) for rule B. Prints coverage per set, the H1b numbers per set,
      the H3 table, the bound-soundness counterexamples, and writes a per-kernel CSV ($OUT/phase4-scored.csv).
Scoring is phase 1's, unchanged: both alias values floored at -100 dB, pooled over kernels x the six factors;
      Spearman rho with average ranks for ties; median absolute error; the factor is the smallest of 1..32 reaching
      -80 dB ("0" = none within 32x). Linear kernels (no nonlinear op) are left out, as in phase 1.
      Informative = the kernel has memory (state, delay or table): memoryless kernels are exact by construction.
Rules (fixed before scoring, README section 13):
  A  8x if the kernel has any nonlinear op, else 1x.
  B  the kernel's smoothness class -> that class's median factor over phase 0's corpus. The class is read off the
     source, worst first: feedback (a state cell assigned from an expression with a nonlinear op that reads the cell)
     > jump (floor ceil % if comparisons) > kink (abs clamp min max) > analytic (exp log sqrt sin cos tan, division)
     > poly (^, or a product of two signals).
Phase 5 (README section 15) adds H4 on the kernels that use delay(x, N): coverage over every delay kernel in L, G and D
      (>= 80% scored; kill under 50%), H1b's bars on the informative delay kernels of L, G and D separately, and H3's
      rule on the pooled delay kernels. The per-kernel CSV is named after the input (phaseN-heldout -> phaseN-scored).
Usage: python3 scripts/score_heldout.py <heldout csv> [corpus dir]
"""
import csv, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
FACTORS = [1, 2, 4, 8, 16, 32]
FLOOR, TARGET = -100.0, -80.0

JUMP = re.compile(r"\b(floor|ceil|if)\s*\(|%|[<>]=?|==|!=")
KINK = re.compile(r"\b(abs|clamp|min|max)\s*\(")
ANALYTIC = re.compile(r"\b(exp|log|sqrt|sin|cos|tan|pow)\s*\(|/")
POLY = re.compile(r"\^")


def strip(code):
    return "\n".join(l.split("#", 1)[0] for l in code.splitlines())


def factor(dbs):
    for f, d in zip(FACTORS, dbs):
        if d <= TARGET:
            return f
    return 0


def products_of_signals(code):
    # a product where both sides mention a signal name (anything that is not a number) - crude but fixed
    for line in code.splitlines():
        if "=" not in line or line.strip().startswith("state"):
            continue
        rhs = line.split("=", 1)[1]
        if re.search(r"([A-Za-z_]\w*|\))\s*\*\s*(\(|[A-Za-z_]\w*)", rhs):
            return True
    return False


def is_nonlinear(code):
    return bool(JUMP.search(code) or KINK.search(code) or ANALYTIC.search(code) or POLY.search(code)
                or products_of_signals(code))


def _group(text, i):
    """The innermost parenthesised group around position i (the whole text if none)."""
    depth, lo = 0, 0
    for j in range(i - 1, -1, -1):
        if text[j] == ")":
            depth += 1
        elif text[j] == "(":
            if depth == 0:
                lo = j
                break
            depth -= 1
    depth, hi = 0, len(text)
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            if depth == 0:
                hi = j
                break
            depth -= 1
    return text[lo:hi]


def nonlinear_in(text, name):
    """True if some nonlinear op in `text` acts on an expression that mentions `name`: a call whose arguments
    mention it, or an operator (^ / % comparison, signal product) inside a group that mentions it."""
    for m in re.finditer(r"\b(abs|clamp|min|max|floor|ceil|if|exp|log|sqrt|sin|cos|tan|pow)\s*\(", text):
        if re.search(rf"\b{name}\b", _group(text, m.end())):
            return True
    for m in re.finditer(r"[\^/%<>]|==|!=|([A-Za-z_]\w*|\))\s*\*\s*(\(|[A-Za-z_])", text):
        g = _group(text, m.start())
        if re.search(rf"\b{name}\b", g) and (m.group(0)[0] in "^/%<>=!" or products_of_signals("_=" + g)):
            return True
    return False


def klass(code):
    """Worst smoothness class read off the source. Feedback: a state cell's update, with the locals it reads
    inlined one level deep, applies a nonlinear op to something that mentions the cell itself."""
    defs = {}
    for line in code.splitlines():
        m = re.match(r"^\s*([A-Za-z_]\w*)\s*=(?!=)(.*)$", line)
        if m:
            defs.setdefault(m.group(1), m.group(2).strip())
    states = re.findall(r"^\s*state\s+float\s+(\w+)", code, re.M)
    for line in code.splitlines():
        m = re.match(r"^\s*([A-Za-z_]\w*)\s*=(?!=)(.*)$", line)
        if not m or m.group(1) not in states:
            continue
        s, rhs = m.group(1), m.group(2)
        inlined = re.sub(r"\b[A-Za-z_]\w*\b",
                         lambda t: f"({defs[t.group(0)]})" if t.group(0) in defs and t.group(0) not in states
                         and t.group(0) not in ("in", "out") else t.group(0), rhs)
        if nonlinear_in(inlined, s):
            return "feedback"
    if JUMP.search(code):
        return "jump"
    if KINK.search(code):
        return "kink"
    if ANALYTIC.search(code):
        return "analytic"
    return "poly"


def rule_b_table():
    per = {}
    for r in csv.DictReader(open(os.path.join(ROOT, "data", "phase1-predictions.csv"), encoding="utf-8")):
        per.setdefault((r["label"], r["class"]), {})[int(r["factor"])] = float(r["alias_db"])
    by = {}
    for (label, k), d in per.items():
        if k in ("linear", "preset", "osc", "chain"):
            continue
        f = factor([d[x] for x in FACTORS])
        by.setdefault(k, []).append(64 if f == 0 else f)
    return {k: int(statistics.median_low(sorted(v))) for k, v in by.items()}


def ranks(v):
    idx = sorted(range(len(v)), key=lambda i: v[i])
    r = [0.0] * len(v)
    i = 0
    while i < len(idx):
        j = i
        while j + 1 < len(idx) and v[idx[j + 1]] == v[idx[i]]:
            j += 1
        for k in range(i, j + 1):
            r[idx[k]] = 0.5 * (i + j)
        i = j + 1
    return r


def spearman(a, b):
    ra, rb = ranks(a), ranks(b)
    n = len(a)
    ma, mb = sum(ra) / n, sum(rb) / n
    sab = sum((x - ma) * (y - mb) for x, y in zip(ra, rb))
    saa = sum((x - ma) ** 2 for x in ra)
    sbb = sum((y - mb) ** 2 for y in rb)
    return sab / (saa * sbb) ** 0.5 if saa > 0 and sbb > 0 else 0.0


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    corpus = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "corpus", "heldout")
    table = rule_b_table()
    print("rule B table (class -> median phase-0 factor, 64 = beyond 32x):", table)
    rows = list(csv.DictReader(open(sys.argv[1], encoding="utf-8")))
    out_rows, sets = [], {}
    for r in rows:
        code = strip(open(os.path.join(corpus, r["set"], r["label"] + ".field"), encoding="utf-8").read())
        s = sets.setdefault(r["set"], {"n": 0, "ok": 0, "declined": {}, "rows": []})
        s["n"] += 1
        has_delay = "delay(" in code
        s.setdefault("delay_n", 0)
        s.setdefault("delay_ok", 0)
        s["delay_n"] += int(has_delay)
        if r["status"] != "ok":
            key = r["status"] + ": " + re.sub(r"[\d.]+", "#", r["reason"])[:60]
            s["declined"][key] = s["declined"].get(key, 0) + 1
            continue
        s["ok"] += 1
        s["delay_ok"] += int(has_delay)
        meas = [float(r[f"meas_{f}"]) for f in FACTORS]
        pred = [float(r[f"pred_{f}"]) for f in FACTORS]
        nl = is_nonlinear(code)
        k = klass(code) if nl else "linear"
        fm, fp = int(r["factor_meas"]), int(r["factor_pred"])
        fa = 8 if nl else 1
        fb = table.get(k, 8) if nl else 1
        fb = 0 if fb == 64 else fb
        over = max(m - p for m, p in zip(meas, pred))
        e = {"set": r["set"], "label": r["label"], "class": k, "memoryless": r["memoryless"], "nonlinear": int(nl), "has_delay": int(has_delay),
             "factor_meas": fm, "factor_pred": fp, "rule_a": fa, "rule_b": fb,
             "median_abs_err": round(statistics.median(abs(max(m, FLOOR) - max(p, FLOOR)) for m, p in zip(meas, pred)), 2),
             "max_meas_over_pred_db": round(over, 2), "meas": meas, "pred": pred}
        s["rows"].append(e)
        out_rows.append(e)

    def h1(es):
        a, b, errs = [], [], []
        for e in es:
            for m, p in zip(e["meas"], e["pred"]):
                a.append(max(m, FLOOR)); b.append(max(p, FLOOR)); errs.append(abs(a[-1] - b[-1]))
        return (spearman(a, b), statistics.median(errs), len(es)) if es else (float("nan"), float("nan"), 0)

    primary_pass = True
    for name in sorted(sets):
        s = sets[name]
        inf = [e for e in s["rows"] if e["nonlinear"] and e["memoryless"] == "0"]
        mem = [e for e in s["rows"] if e["nonlinear"] and e["memoryless"] == "1"]
        rho, med, n = h1(inf)
        rho_m, med_m, n_m = h1(mem)
        ok = n > 0 and rho >= 0.8 and med <= 10.0
        print(f"\nset {name}: {s['n']} kernels, {s['ok']} scored ({100 * s['ok'] / max(1, s['n']):.0f}% coverage)")
        for k, v in sorted(s["declined"].items(), key=lambda kv: -kv[1]):
            print(f"  not scored {v:3d}  {k}")
        print(f"  H1b informative: n={n} kernels, Spearman rho {rho:.3f} (>= 0.8), median |err| {med:.1f} dB (<= 10)"
              f"  {'PASS' if ok else 'FAIL'}")
        print(f"  memoryless (exact by construction, reported only): n={n_m}, rho {rho_m:.3f}, median |err| {med_m:.1f} dB")
        if name in ("L", "G"):
            primary_pass &= ok

    pool = [e for e in out_rows if e["set"] in ("L", "G") and e["nonlinear"] and e["memoryless"] == "0"]
    if pool:
        n = len(pool)
        hit = lambda key: sum(e[key] == e["factor_meas"] for e in pool)
        an, ra, rb = hit("factor_pred"), hit("rule_a"), hit("rule_b")
        h3 = an / n >= 0.8 and an > ra and an > rb
        print(f"\nH3 on L+G informative (n={n}): minimal factor exact - analysis {an} ({100 * an / n:.0f}%), "
              f"rule A {ra} ({100 * ra / n:.0f}%), rule B {rb} ({100 * rb / n:.0f}%)  {'PASS' if h3 else 'FAIL'}")
    bad = [e for e in out_rows if e["max_meas_over_pred_db"] > 3.0 and e["nonlinear"]]
    print(f"\nmeasured above predicted by > 3 dB at some factor: {len(bad)} kernels "
          f"(counterexamples only for classes a phase-4c lemma covers)")
    for e in sorted(bad, key=lambda e: -e["max_meas_over_pred_db"])[:15]:
        print(f"  {e['set']}/{e['label']:<28} {e['class']:<9} +{e['max_meas_over_pred_db']:.1f} dB")
    print(f"\nH1b on the primary sets: {'PASS' if primary_pass else 'FAIL'}")

    dn = sum(s.get("delay_n", 0) for k, s in sets.items() if k in ("L", "G", "D"))
    if dn:
        dok = sum(s.get("delay_ok", 0) for k, s in sets.items() if k in ("L", "G", "D"))
        cov = dok / dn
        print(f"\nH4 (delay lines): coverage {dok} of {dn} delay kernels in L, G, D ({100 * cov:.0f}%; bar 80%, kill < 50%)"
              f"  {'PASS' if cov >= 0.8 else ('KILL' if cov < 0.5 else 'FAIL')}")
        h4 = cov >= 0.8
        for name in ("L", "G", "D"):
            if name not in sets:
                continue
            es = [e for e in sets[name]["rows"] if e["nonlinear"] and e["has_delay"] and e["memoryless"] == "0"]
            rho, med, n = h1(es)
            ok = n > 0 and rho >= 0.8 and med <= 10.0
            h4 &= ok
            print(f"  {name}-delay: {sets[name].get('delay_ok', 0)} of {sets[name].get('delay_n', 0)} scored; informative n={n},"
                  f" rho {rho:.3f} (>= 0.8), median |err| {med:.1f} dB (<= 10)  {'PASS' if ok else 'FAIL'}")
        dpool = [e for e in out_rows if e["set"] in ("L", "G", "D") and e["nonlinear"] and e["has_delay"]
                 and e["memoryless"] == "0"]
        if dpool:
            n = len(dpool)
            hit = lambda key: sum(e[key] == e["factor_meas"] for e in dpool)
            an, ra, rb = hit("factor_pred"), hit("rule_a"), hit("rule_b")
            ok = an / n >= 0.8 and an > ra and an > rb
            h4 &= ok
            print(f"  H3 rule on delay kernels (n={n}): analysis {an} ({100 * an / n:.0f}%), rule A {ra}, rule B {rb}"
                  f"  {'PASS' if ok else 'FAIL'}")
        print(f"H4: {'PASS' if h4 else 'FAIL'}")

    out = os.environ.get("OUT") or os.path.join(ROOT, "data-rerun")
    os.makedirs(out, exist_ok=True)
    keys = [k for k in out_rows[0] if k not in ("meas", "pred")] if out_rows else []
    name = os.path.basename(sys.argv[1]).replace("heldout", "scored")
    name = name if name != os.path.basename(sys.argv[1]) else "phase4-scored.csv"
    with open(os.path.join(out, name), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction="ignore")
        w.writeheader(); w.writerows(out_rows)
    print("wrote", os.path.join(out, name))


if __name__ == "__main__":
    main()
