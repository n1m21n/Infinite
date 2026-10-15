#!/usr/bin/env python3
"""Release findings tracker: one markdown file per finding, counts, and the exit criteria.

Findings live in docs/release/findings/<version>/<ID>.md, made from docs/release/BUG_TEMPLATE.md. The tracker reads only
the header block (the `Key: value` lines before the first blank line), so the files stay plain markdown.

Usage:
  findings.py new   --version V --category A..L --title T [--severity S2] [--priority P2] [--found-by X] [--build B]
                    [--regression unknown]      create the next ID's file, print its path
  findings.py list  --version V [--open] [--category C] [--severity S]
  findings.py counts --version V                counts by category x severity, open and closed
  findings.py exit  --version V [--cap 10]      PROTOCOL.md exit criteria 1, 2 and 8 from the tracker
  findings.py --self-test

Open = status not in verified, waived, wontfix. A waived finding must carry a Waiver line; an S0/S1 that is fixed or
verified must carry a Test line (the S0/S1 regression-test rule).
Exit: 0 ok, 1 exit criteria not met / self-test failed, 2 usage error or unreadable finding.
"""
import argparse
import datetime
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ROOT = os.path.join(REPO, "docs", "release", "findings")
CATS = "ABCDEFGHIJKL"
SEVS = ["S0", "S1", "S2", "S3", "S4"]
STATUSES = ["new", "triaged", "fixing", "fixed", "verified", "waived", "wontfix"]
CLOSED = {"verified", "waived", "wontfix"}
KEY = re.compile(r"^(ID|Title|Severity|Priority|Category|Status|Found by|Date|Build|Regression|Fix|Verified by|Waiver|Test):\s*(.*?)\s*$")
EMPTY = {"", "-", "none", "n/a"}


def vdir(version, root=None):
    return os.path.join(root or ROOT, version)


def parse(path):
    """Header block of one finding as a dict. Raises ValueError when a required key is missing or invalid."""
    f = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if not line.strip():
                if f:
                    break
                continue
            m = KEY.match(line.strip("`").rstrip("\n"))
            if m and m.group(1) not in f:
                f[m.group(1)] = m.group(2)
    for k in ("ID", "Severity", "Category", "Status"):
        if k not in f:
            raise ValueError("%s: missing %s" % (path, k))
    if f["Severity"] not in SEVS:
        raise ValueError("%s: bad Severity %r" % (path, f["Severity"]))
    if f["Category"] not in list(CATS):
        raise ValueError("%s: bad Category %r" % (path, f["Category"]))
    if f["Status"] not in STATUSES:
        raise ValueError("%s: bad Status %r" % (path, f["Status"]))
    return f


def load(version, root=None):
    d = vdir(version, root)
    out, errs = [], []
    if not os.path.isdir(d):
        return out, errs
    for name in sorted(os.listdir(d)):
        if not name.startswith("B-") or not name.endswith(".md"):
            continue
        try:
            out.append(parse(os.path.join(d, name)))
        except (ValueError, OSError) as e:
            errs.append(str(e))
    return out, errs


def is_open(f):
    return f["Status"] not in CLOSED


def have(f, key):
    return f.get(key, "").strip().lower() not in EMPTY


def exit_check(findings, cap=10):
    """List of (criterion, ok, detail) for exit criteria 1, 2 and 8."""
    res = []
    o01 = [f["ID"] for f in findings if f["Severity"] in ("S0", "S1") and is_open(f)]
    res.append(("1 no open S0/S1", not o01, ", ".join(o01) or "none open"))
    o2 = [f for f in findings if f["Severity"] == "S2" and is_open(f)]
    res.append(("2 open S2 <= %d" % cap, len(o2) <= cap, "%d open" % len(o2)))
    abc = [f["ID"] for f in o2 if f["Category"] in "ABC"]
    res.append(("2 no open S2 in A, B, C", not abc, ", ".join(abc) or "none"))
    nowaiver = [f["ID"] for f in findings if f["Status"] == "waived" and not have(f, "Waiver")]
    res.append(("3 every waiver has a Waiver line", not nowaiver, ", ".join(nowaiver) or "ok"))
    notest = [f["ID"] for f in findings
              if f["Severity"] in ("S0", "S1") and f["Status"] in ("fixed", "verified") and not have(f, "Test")]
    res.append(("8 S0/S1 fixes have a Test", not notest, ", ".join(notest) or "ok"))
    unverified = [f["ID"] for f in findings
                  if f["Severity"] in ("S0", "S1") and f["Status"] == "fixed"]
    res.append(("  (info) S0/S1 fixed but not yet verified", True, ", ".join(unverified) or "none"))
    return res


def counts_table(findings):
    lines = ["| Cat | " + " | ".join(SEVS) + " | open | total |", "|---|" + "---|" * (len(SEVS) + 2)]
    for c in CATS:
        row = [f for f in findings if f["Category"] == c]
        cells = []
        for s in SEVS:
            n = [f for f in row if f["Severity"] == s]
            o = [f for f in n if is_open(f)]
            cells.append("%d/%d" % (len(o), len(n)) if n else "-")
        lines.append("| %s | %s | %d | %d |" % (c, " | ".join(cells), len([f for f in row if is_open(f)]), len(row)))
    lines.append("")
    lines.append("Cells are open/total.")
    return "\n".join(lines)


def next_id(version, cat, root=None):
    d = vdir(version, root)
    n = 0
    if os.path.isdir(d):
        for name in os.listdir(d):
            m = re.fullmatch(r"B-%s-(\d{3})\.md" % cat, name)
            if m:
                n = max(n, int(m.group(1)))
    return "B-%s-%03d" % (cat, n + 1)


def cmd_new(a, root=None):
    if a.category not in list(CATS) or a.severity not in SEVS:
        print("bad --category or --severity", file=sys.stderr)
        return 2
    d = vdir(a.version, root)
    os.makedirs(d, exist_ok=True)
    fid = next_id(a.version, a.category, root)
    text = (
        "ID:            %s\nTitle:         %s\nSeverity:      %s\nPriority:      %s\nCategory:      %s\nStatus:        new\n"
        "Found by:      %s\nDate:          %s\nBuild:         %s\nRegression:    %s\nFix:           -\nVerified by:   -\n"
        "Waiver:        -\nTest:          -\n\n## Repro\n\n1. \n2. \n3. Frequency: \n\n## Expected\n\n## Actual\n\n"
        "## Blast radius\n\n(the 9 questions from the bug-blast-radius skill)\n\n## Notes\n"
    ) % (fid, a.title, a.severity, a.priority, a.category, a.found_by, datetime.date.today().isoformat(),
         a.build or "-", a.regression)
    path = os.path.join(d, fid + ".md")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(path)
    return 0


def cmd_list(a):
    fs, errs = load(a.version)
    for e in errs:
        print("warning:", e, file=sys.stderr)
    for f in fs:
        if a.open and not is_open(f):
            continue
        if a.category and f["Category"] != a.category:
            continue
        if a.severity and f["Severity"] != a.severity:
            continue
        print("%-9s %s %s %-8s %s" % (f["ID"], f["Severity"], f.get("Priority", "-"), f["Status"], f.get("Title", "")))
    return 2 if errs else 0


def cmd_counts(a):
    fs, errs = load(a.version)
    for e in errs:
        print("warning:", e, file=sys.stderr)
    print(counts_table(fs))
    return 2 if errs else 0


def cmd_exit(a):
    if not os.path.isdir(vdir(a.version)):
        print("no tracker for %s (%s missing): initialise it before judging exit" % (a.version, vdir(a.version)),
              file=sys.stderr)
        return 2
    fs, errs = load(a.version)
    for e in errs:
        print("unreadable:", e, file=sys.stderr)
    if errs:
        return 2
    print("Exit criteria from the tracker, %s (%d findings)" % (a.version, len(fs)))
    bad = 0
    for name, ok, detail in exit_check(fs, a.cap):
        print("  %-4s %-42s %s" % ("PASS" if ok else "FAIL", name, detail))
        bad += 0 if ok else 1
    print("Criteria 3-7 (pass bars, final run, Windows, notes audit, quality gate) are read from the run table, not here.")
    return 1 if bad else 0


def self_test():
    root = tempfile.mkdtemp(prefix="findings-selftest-")
    ns = argparse.Namespace(version="t", category="D", title="x", severity="S1", priority="P1",
                            found_by="test", build="b", regression="unknown")
    p = None
    for _ in range(2):
        import io
        so, sys.stdout = sys.stdout, io.StringIO()
        try:
            cmd_new(ns, root)
            p = sys.stdout.getvalue().strip()
        finally:
            sys.stdout = so
    ok = os.path.basename(p) == "B-D-002.md"
    fs, errs = load("t", root)
    ok &= len(fs) == 2 and not errs
    ok &= not dict((n, o) for n, o, _ in exit_check(fs))["1 no open S0/S1"]
    for f in fs:
        f["Status"] = "fixed"
    ok &= not dict((n, o) for n, o, _ in exit_check(fs))["8 S0/S1 fixes have a Test"]
    for f in fs:
        f["Test"] = "FOOTEST"
        f["Status"] = "verified"
    res = dict((n, o) for n, o, _ in exit_check(fs))
    ok &= all(res.values())
    fs[0]["Status"] = "waived"
    ok &= not dict((n, o) for n, o, _ in exit_check(fs))["3 every waiver has a Waiver line"]
    s2 = [{"ID": "B-A-%03d" % i, "Severity": "S2", "Category": "A", "Status": "new"} for i in range(1, 3)]
    r = dict((n, o) for n, o, _ in exit_check(s2, 10))
    ok &= not r["2 no open S2 in A, B, C"] and r["2 open S2 <= 10"]
    print("findings self-test", "OK" if ok else "FAIL")
    return 0 if ok else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    n = sub.add_parser("new")
    n.add_argument("--version", required=True)
    n.add_argument("--category", required=True)
    n.add_argument("--title", required=True)
    n.add_argument("--severity", default="S2")
    n.add_argument("--priority", default="P2")
    n.add_argument("--found-by", default="Claude")
    n.add_argument("--build", default="")
    n.add_argument("--regression", default="unknown")
    for name in ("list", "counts", "exit"):
        s = sub.add_parser(name)
        s.add_argument("--version", required=True)
        if name == "list":
            s.add_argument("--open", action="store_true")
            s.add_argument("--category")
            s.add_argument("--severity")
        if name == "exit":
            s.add_argument("--cap", type=int, default=10)
    a = ap.parse_args(argv)
    return {"new": cmd_new, "list": cmd_list, "counts": cmd_counts, "exit": cmd_exit}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
