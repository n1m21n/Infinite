#!/usr/bin/env python3
"""One command: inbox MIDI -> gated pieces -> train both scopes -> report what moved.
    train_all.py [--synth-max 400] [--skip-ingest]
Steps: (1) midi_ingest.py reads ~/Infinite/training-data/inbox/<source>/**/*.mid into store/inbox.jsonl
       (2) a data report: files per source, licence, family, how many pieces passed each gate and why the rest failed
       (3) learn.py for `local` and `shippable`; each keeps a model only if it beats the last kept one
Safe to run any number of times: ingest rewrites inbox.jsonl from scratch, learn.py never lowers a kept model."""
import argparse, collections, glob, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.expanduser("~/Infinite/training-data")
NEED = 60  # drum bars per family before the family-specific table means anything


def run(*args, isolated=True):
    cmd = [sys.executable, *(("-I",) if isolated else ()), os.path.join(HERE, args[0]), *args[1:]]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def report():
    inbox = os.path.join(ROOT, "inbox")
    srcs = sorted(d for d in glob.glob(inbox + "/*") if os.path.isdir(d))
    if not srcs:
        print("inbox is empty: put files in %s/<source>/ (see README.txt there)" % inbox)
        return
    print("%-28s %6s %-22s %s" % ("source", "files", "licence", "family"))
    for d in srcs:
        n = len(glob.glob(d + "/**/*.mid*", recursive=True))
        meta = {}
        f = os.path.join(d, "SOURCE.txt")
        if os.path.exists(f):
            for ln in open(f, errors="replace"):
                m = re.match(r"\s*(family|license|licence)\s*:\s*(.+?)\s*$", ln, re.I)
                if m:
                    meta["family" if m[1].lower() == "family" else "license"] = m[2]
        print("%-28s %6d %-22s %s" % (os.path.basename(d)[:28], n, meta.get("license", "UNKNOWN (local only)"), meta.get("family", "guessed from names")))
    p = os.path.join(ROOT, "store", "inbox.jsonl")
    if not os.path.exists(p):
        return
    rows = [json.loads(l) for l in open(p)]
    print("\npieces from the inbox")
    print("%-16s %7s %7s  %s" % ("type", "total", "passed", "top rejection reasons"))
    for t in ("drum_groove", "melody_contour", "note_expression"):
        r = [x for x in rows if x["type"] == t]
        why = collections.Counter(h for x in r for h in x["hard"])
        print("%-16s %7d %7d  %s" % (t, len(r), sum(x["ok"] for x in r), "; ".join("%s x%d" % kv for kv in why.most_common(3)) or "-"))
    fam = collections.Counter(x["family"] for x in rows if x["type"] == "drum_groove" and x["ok"])
    print("\ndrum bars passed per family (need %d each):" % NEED)
    for f, n in sorted(fam.items(), key=lambda kv: -kv[1]):
        print("  %-14s %5d %s" % (f, n, "" if n >= NEED else "<- thin"))
    if fam.get("unlabeled"):
        print("  'unlabeled' = no family in SOURCE.txt or the path; add `family: house` to the SOURCE.txt so it trains the right table")
    bad = [x for x in rows if x["type"] == "drum_groove" and not x["ok"]]
    if bad:
        print("\nrejected drum bars: %d (gates are tuned on house/techno cards; if a whole genre is refused, tell me and I'll check the gate, not the data)" % len(bad))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--synth-max", type=int, default=400)
    ap.add_argument("--skip-ingest", action="store_true")
    ap.add_argument("--skip-mix", action="store_true")
    ap.add_argument("--export-cpp", action="store_true", default=True)
    a = ap.parse_args()
    if not a.skip_ingest:
        _, out_ing = run("midi_ingest.py", "--cap", "0", "--budget", "40")
        print(out_ing.strip().splitlines()[-1])
    report()
    for scope in ("local", "shippable"):
        print("\n" + "=" * 70 + "\nTRAIN scope=%s" % scope)
        if not a.skip_mix:
            _, mix_out = run("mix.py", "--scope", scope, isolated=False)
            for ln in mix_out.splitlines():
                if ln.startswith(("K=", "best K=", "KEPT", "REJECTED")):
                    print("  [mix] " + ln)
        _, out = run("learn.py", "--scope", scope, "--synth-max", str(a.synth_max))
        keep = False
        for ln in out.splitlines():
            if ln.startswith(("scope", "  train", "  test", "model", "drum ", "melody ", "params ", "chord ", "bass ", "  drum", "  melody", "  bass", "KEPT", "REJECTED")) or ln.startswith("held-out"):
                print(ln)
            keep |= ln.startswith("KEPT")
        print("-> model %s" % ("UPDATED" if keep else "unchanged"))

    print("\n" + "=" * 70 + "\nBENCHMARK VERIFICATION (6 GATES)")
    rc, bench_out = run("benchmark.py", "--scope", "shippable")
    for ln in bench_out.splitlines():
        if ln.startswith(("[B", "  ", "OVERALL:")):
            print(ln)
    if rc == 0 and a.export_cpp:
        _, exp_out = run("export_critic_inc.py")
        print(exp_out.strip())
    elif rc != 0:
        print("WARNING: benchmark gate failed; C++ critic header NOT updated.")
        sys.exit(rc)


if __name__ == "__main__":
    main()
