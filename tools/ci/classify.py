#!/usr/bin/env python3
"""Name the cause of every failed job in a CI run, from tools/ci/signatures.json.

  tools/ci/classify.py <run-id | branch>          classify a run (reads logs with gh)
  tools/ci/classify.py <run-id> --record          also append rows to docs/ci/failure-log.tsv
  tools/ci/classify.py --log FILE --job NAME [--step STEP]    classify a saved log (offline)
  tools/ci/classify.py --summary                  failures by class, from docs/ci/failure-log.tsv

Exit: 0 nothing failed or only non-blocking failures, 1 a blocking failure, 3 a failure no
signature explains (UNKNOWN: read the log, then add a row to signatures.json so the next
one costs seconds).
"""
import argparse
import collections
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIGS = os.path.join(ROOT, "tools", "ci", "signatures.json")
LEDGER = os.path.join(ROOT, "docs", "ci", "failure-log.tsv")
REPO = "n1m21n/Infinite"
WEAK = {"compile-error", "test-fails-one-compiler"}  # explain little; a specific match wins
INFO = {"stale-baseline"}  # informational lines the drivers print even on a pass; never the primary cause


def gh(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("gh failed: " + r.stderr.strip()[:300])
    return r.stdout


def load_sigs():
    with open(SIGS) as f:
        return json.load(f)["signatures"]


def clean(log):
    return re.sub(r"^\d{4}-\d\d-\d\dT[\d:.]+Z ", "", log.replace("\r", ""), flags=re.M)


def match(sigs, steps, log):
    hits = []
    for s in sigs:
        step_re, log_re = s.get("step"), s.get("log")
        step_ok = bool(step_re) and any(re.search(step_re, st, re.I) for st in steps)
        log_ok = bool(log_re) and re.search(log_re, log, re.I | re.M) is not None
        if step_re and log_re:
            # both given: both must agree (offline logs with no known step match on the log alone)
            ok = log_ok and (step_ok or not steps)
        else:
            ok = step_ok or log_ok
        if ok:
            hits.append(s)
    hits.sort(key=lambda s: 2 if s["id"] in INFO else 1 if s["id"] in WEAK else 0)
    return hits


def failing_checks(log):
    names = set(re.findall(r"\[FAIL\]\s+([A-Z0-9_]{4,})", log))
    m = re.search(r"failing checks:\s*(.+)", log)
    if m:
        names |= set(re.findall(r"[A-Z0-9_]{4,}", m.group(1)))
    return names


def run_jobs(run):
    data = json.loads(gh("run", "view", str(run), "--json", "status,conclusion,jobs"))
    return data


def classify_run(arg, record):
    run = arg if arg.isdigit() else json.loads(
        gh("run", "list", "--branch", arg, "-L", "1", "--json", "databaseId"))[0]["databaseId"]
    data = run_jobs(run)
    print(f"run {run}  https://github.com/{REPO}/actions/runs/{run}  status={data['status']} conclusion={data.get('conclusion')}")
    jobs = data["jobs"]
    failed = [j for j in jobs if j.get("conclusion") == "failure"]
    if not failed:
        pending = [j["name"] for j in jobs if j["status"] != "completed"]
        print("no failed jobs" + (f" ({len(pending)} still running: {', '.join(pending)})" if pending else ""))
        return 0, run, []
    sigs, rows, blocking, unknown = load_sigs(), [], False, False
    per_check = collections.defaultdict(set)
    logs = {}
    for j in failed:
        log = clean(gh("api", f"repos/{REPO}/actions/jobs/{j['databaseId']}/logs"))
        steps = [s["name"] for s in j["steps"] if s.get("conclusion") == "failure"]
        logs[j["name"]] = (steps, log)
        for c in failing_checks(log):
            per_check[c].add(j["name"])
    ok_jobs = [j["name"] for j in jobs if j.get("conclusion") == "success"]
    for name, (steps, log) in logs.items():
        b, u = report(name, steps, log, sigs, rows, per_check, ok_jobs)
        blocking |= b
        unknown |= u
    if record:
        write_ledger(run, rows)
    return (3 if unknown else 1 if blocking else 0), run, rows


def report(name, steps, log, sigs, rows, per_check=None, ok_jobs=()):
    print(f"\n=== {name}   failed step: {', '.join(steps) or '?'}")
    hits = match(sigs, steps, log)
    if not hits:
        print("  UNKNOWN: no signature matches. Read the log, then add a row to tools/ci/signatures.json.")
        tail = [l for l in log.splitlines() if re.search(r"error|fail", l, re.I)][-6:]
        for l in tail:
            print("    " + l[:200])
        rows.append((name, ", ".join(steps), "UNKNOWN", "unknown", "true"))
        return True, True
    primary = hits[0]
    print(f"  cause [{primary['id']}, {primary['class']}]: {primary['cause']}")
    print(f"  fix: {primary['fix']}")
    print(f"  catch it locally with: {primary['preflight']}")
    print(f"  holds the release: {'yes' if primary.get('blocking', True) else 'no'}")
    if len(hits) > 1:
        print("  also matched: " + ", ".join(h["id"] for h in hits[1:]))
    if per_check:
        for c in sorted(failing_checks(log)):
            peers = per_check[c]
            sibs = [j for j in ok_jobs if j.split(" (")[0] == name.split(" (")[0]]
            if sibs:
                print(f"  note: {c} failed here but sibling job(s) passed ({', '.join(sibs)}): "
                      "suspect global state, test order or undefined behaviour, not the compiler")
    rows.append((name, ", ".join(steps), primary["id"], primary["class"], str(primary.get("blocking", True)).lower()))
    return bool(primary.get("blocking", True)), False


def write_ledger(run, rows):
    new = not os.path.exists(LEDGER)
    os.makedirs(os.path.dirname(LEDGER), exist_ok=True)
    day = datetime.date.today().isoformat()
    with open(LEDGER, "a") as f:
        if new:
            f.write("date\trun\tjob\tstep\tsignature\tclass\tblocking\n")
        for r in rows:
            f.write("\t".join([day, str(run), *r]) + "\n")
    print(f"\nrecorded {len(rows)} row(s) in {os.path.relpath(LEDGER, ROOT)}")


def summary():
    if not os.path.exists(LEDGER):
        print("no failure ledger yet")
        return 0
    rows = [l.rstrip("\n").split("\t") for l in open(LEDGER)][1:]
    by = collections.Counter(r[5] for r in rows if len(r) >= 7)
    sig = collections.Counter(r[4] for r in rows if len(r) >= 7)
    print(f"{len(rows)} recorded job failures")
    print("by class:     " + ", ".join(f"{k} {v}" for k, v in by.most_common()))
    print("by signature: " + ", ".join(f"{k} {v}" for k, v in sig.most_common()))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run", nargs="?")
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--log")
    ap.add_argument("--job", default="(offline log)")
    ap.add_argument("--step", default="")
    ap.add_argument("--summary", action="store_true")
    a = ap.parse_args()
    if a.summary:
        return summary()
    if a.log:
        rows = []
        steps = [a.step] if a.step else []
        b, u = report(a.job, steps, clean(open(a.log, errors="replace").read()), load_sigs(), rows)
        return 3 if u else 1 if b else 0
    if not a.run:
        ap.error("give a run id or branch (or --log / --summary)")
    return classify_run(a.run, a.record)[0]


if __name__ == "__main__":
    sys.exit(main())
