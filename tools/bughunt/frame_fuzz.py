#!/usr/bin/env python3
"""Driver for INFINITE_FRAMEFUZZ (hostile params on every picture / 3D node).

  frame_fuzz.py BIN OUT.log [--only Type,Type] [--row-sec 30] [--rss-gb 3] [--budget MIN]
  frame_fuzz.py selftest

The app prints "FRAMEFUZZ try <row>" before each row. This watches that line
and the process: a row that runs past --row-sec is a HANG, a process past
--rss-gb resident is an OOM (an 8 GB Mac swaps to a stop well before the
kernel kills it), an exit without the summary line is a CRASH. Each is
logged, the process is killed, and the app restarts with
INFINITE_FRAMEFUZZ_AFTER=<row>, so one run covers every row. Flags the app
prints itself (NANMESH, BIGMESH, BIGTEX, GLERR, SLOW) are copied through.
Exit 1 when anything was flagged.
"""
import os
import re
import subprocess
import sys
import time

TRY = re.compile(r"^FRAMEFUZZ try (.+)$")
FLAG = re.compile(r"^FRAMEFUZZ (NANMESH|BIGMESH|BIGTEX|GLERR|SLOW) ")
DONE = re.compile(r"^FRAMEFUZZ \d+ types, ")


def rss_gb(pid):
    try:
        out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True).stdout
        return int(out.strip() or 0) / (1024 * 1024)
    except ValueError:
        return 0.0


def run(binary, log, only="1", row_sec=30.0, rss_limit=3.0, budget_min=20.0, home=None):
    findings = []
    after = ""
    t_end = time.time() + budget_min * 60
    with open(log, "w") as lf:
        while time.time() < t_end:
            env = dict(os.environ, INFINITE_FRAMEFUZZ=only)
            if home:
                env["HOME"] = home
            if after:
                env["INFINITE_FRAMEFUZZ_AFTER"] = after
            else:
                env.pop("INFINITE_FRAMEFUZZ_AFTER", None)
            p = subprocess.Popen([binary], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            os.set_blocking(p.stdout.fileno(), False)
            row, row_t0, done, buf, why = None, time.time(), False, "", None
            while True:
                chunk = (p.stdout.read() or b"").decode("utf-8", "replace")
                buf += chunk
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    lf.write(line + "\n")
                    m = TRY.match(line)
                    if m:
                        row, row_t0 = m.group(1), time.time()
                    elif FLAG.match(line):
                        findings.append(line)
                    elif DONE.match(line):
                        done = True
                if p.poll() is not None:
                    break
                if row and time.time() - row_t0 > row_sec:
                    why = "HANG"
                elif rss_gb(p.pid) > rss_limit:
                    why = "OOM"
                elif time.time() > t_end:
                    why = "BUDGET"
                if why:
                    p.kill()
                    p.wait()
                    break
                time.sleep(0.2)
            lf.flush()
            if done:
                break
            if why == "BUDGET":
                findings.append(f"FRAMEFUZZ BUDGET stopped at {row}")
                break
            kind = why or f"CRASH(exit {p.returncode})"
            findings.append(f"FRAMEFUZZ {kind} {row}")
            lf.write(f"FRAMEFUZZ {kind} {row}\n")
            if row is None or row == after:
                break  # no progress: the same row again
            after = row
    for f in findings:
        print(f)
    print(f"frame_fuzz: {len(findings)} finding(s), log {log}")
    return 1 if findings else 0


def selftest():
    assert TRY.match("FRAMEFUZZ try Plane.detail=1000000000").group(1) == "Plane.detail=1000000000"
    assert FLAG.match("FRAMEFUZZ NANMESH Twist.angle=1e9  verts 3")
    assert not FLAG.match("FRAMEFUZZ try X.y=1")
    assert DONE.match("FRAMEFUZZ 352 types, 9000 rows (500 with a mesh), 0 flagged  OK")
    print("frame_fuzz selftest OK")
    return 0


def main(argv):
    if len(argv) >= 2 and argv[1] == "selftest":
        return selftest()
    if len(argv) < 3:
        print(__doc__)
        return 2
    args = argv[3:]
    def opt(name, default):
        return args[args.index(name) + 1] if name in args else default
    return run(argv[1], argv[2], only=opt("--only", "1"), row_sec=float(opt("--row-sec", 30)),
               rss_limit=float(opt("--rss-gb", 3)), budget_min=float(opt("--budget", 20)),
               home=opt("--home", None))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
