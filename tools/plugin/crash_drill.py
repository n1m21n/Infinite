#!/usr/bin/env python3
"""Failure drill for the Infinite plugin engine (docs/plans/vst-au/README.md step 2.5).

    tools/plugin/crash_drill.py --self [--build build-plugin] [--engine PATH]
    tools/plugin/crash_drill.py kill [--host NAME] [--wait S]
    tools/plugin/crash_drill.py hang [--host NAME] [--wait S]

--self   runs plugin_shell_check --drill: crash restart with the last pushed patch, a
         SIGSTOPped engine killed by the shell's watchdog, notes released on suspend, the
         picture choice saved in the state. No DAW needed.
kill     SIGKILLs the engine of a plugin loaded in a running DAW (crash). The DAW must keep
         running; the editor shows "Engine stopped" with Restart, and Restart opens what was
         on screen (the engine pushes its patch every 2 s).
hang     SIGSTOPs the engine instead (a deadlock). The shell's watchdog must kill it within
         3-6 s; the DAW keeps running and the plugin plays silence.

--host limits kill/hang to engines whose parent process name contains NAME (e.g. REAPER,
Logic, Live), when several DAWs run at once. Exit 0 when the DAW is alive and the engine is
gone at the end, 1 otherwise. macOS and Linux (ps, kill); Windows is not covered yet.
"""

import argparse
import os
import signal
import subprocess
import sys
import time


def ps_rows():
    """(pid, ppid, stat, command) for every process."""
    out = subprocess.run(["ps", "-axo", "pid=,ppid=,stat=,command="], capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4:
            rows.append((int(parts[0]), int(parts[1]), parts[2], parts[3]))
    return rows


def engines(host):
    rows = ps_rows()
    by_pid = {r[0]: r for r in rows}
    found = []
    for pid, ppid, stat, cmd in rows:
        if "--plugin-engine" not in cmd or stat.startswith("Z"):
            continue
        parent = by_pid.get(ppid)
        parent_cmd = parent[3] if parent else ""
        if host and host.lower() not in parent_cmd.lower():
            continue
        found.append((pid, ppid, parent_cmd))
    return found


def alive(pid):
    for p, _, stat, _ in ps_rows():
        if p == pid:
            return not stat.startswith("Z")
    return False


def drill(mode, host, wait):
    targets = engines(host)
    if not targets:
        print("no plugin engine running" + (f" under a host matching '{host}'" if host else ""))
        return 1
    ok = True
    for pid, ppid, parent_cmd in targets:
        name = os.path.basename(parent_cmd.split(" ")[0]) if parent_cmd else "?"
        sig = signal.SIGKILL if mode == "kill" else signal.SIGSTOP
        print(f"{mode}: engine {pid} (host {ppid} {name})")
        os.kill(pid, sig)
        t0 = time.time()
        gone_at = None
        while time.time() - t0 < wait:
            if not alive(pid):
                gone_at = time.time() - t0
                break
            time.sleep(0.1)
        host_ok = alive(ppid)
        if gone_at is None:
            print(f"  FAIL  engine still running after {wait:.0f} s")
            if mode == "hang":
                os.kill(pid, signal.SIGKILL)  # never leave a stopped engine behind
            ok = False
        elif mode == "hang" and not (2.0 < gone_at < 6.0):
            print(f"  FAIL  watchdog stopped it after {gone_at:.1f} s (want 3-6 s)")
            ok = False
        else:
            print(f"  PASS  engine gone after {gone_at:.1f} s")
        print(f"  {'PASS' if host_ok else 'FAIL'}  host {'alive' if host_ok else 'gone'}")
        ok = ok and host_ok
    if ok:
        print("Now press Restart in the plugin's editor: it should come back with the same patch.")
    return 0 if ok else 1


def self_test(build, engine):
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    check = os.path.join(root, build, "bin", "Release", "plugin_shell_check")
    inst = os.path.join(root, build, "VST3", "Release", "Infinite Instrument.vst3")
    engine = engine or os.path.join(root, "build", "Infinite.app", "Contents", "MacOS", "Infinite")
    for path in (check, inst, engine):
        if not os.path.exists(path):
            print(f"missing: {path}")
            return 1
    env = dict(os.environ, INFINITE_PLUGIN_ENGINE=engine)
    return subprocess.run([check, "--drill", inst], env=env).returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", nargs="?", choices=["kill", "hang"], help="drill a plugin loaded in a running DAW")
    ap.add_argument("--self", action="store_true", help="run plugin_shell_check --drill (no DAW)")
    ap.add_argument("--host", default="", help="only engines whose host process name contains this")
    ap.add_argument("--wait", type=float, default=8.0, help="seconds to wait for the engine to go")
    ap.add_argument("--build", default="build-plugin", help="plugin build directory (for --self)")
    ap.add_argument("--engine", default="", help="Infinite binary for --self (default build/Infinite.app)")
    a = ap.parse_args()
    if a.self:
        return self_test(a.build, a.engine)
    if not a.mode:
        ap.print_help()
        return 2
    return drill(a.mode, a.host, a.wait)


if __name__ == "__main__":
    sys.exit(main())
