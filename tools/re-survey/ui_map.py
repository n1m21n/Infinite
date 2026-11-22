#!/usr/bin/env python3
"""re-survey ui_map: map an app's UI from outside through the macOS accessibility tree (read-only).

What it reads: the menu bar (every item, its path, shortcut, enabled state) and the window tree (roles,
titles, descriptions, frames). It never clicks, types, sets a value or performs an AX action: ax_dump.swift
only copies attributes. The one thing it does to the app is launch it in the background when it is not
running and quit it again afterwards (it leaves an app it found running alone).

Per app, the report gives: menu outline with shortcuts, shortcut collisions, the window tree's size and
role mix, how much of the window is opaque to accessibility, and a depth-limited outline of named elements.
Seen 2026-10: Logic and Live 12 expose their windows (Live 12 names most controls); TouchDesigner exposes
its menus but no window; Bitwig exposes nothing (no menu bar, no window), so its row stays empty. Across apps: one table of the actions two or more apps name, with each app's shortcut.

Usage:
  python3 tools/re-survey/ui_map.py run "Ableton Live 12 Suite" Logic Bitwig TouchDesigner [--out t.md] [--wait 90]
  python3 tools/re-survey/ui_map.py --self-test     (uses Finder; launches nothing)
Raw trees: ~/re-output/ui-map/<stamp>/<app>.json.gz (behaviour data, never committed).
Needs: this terminal/host trusted for Accessibility (System Settings > Privacy > Accessibility); swiftc.
Exit: 0 ok, 1 an app failed / self-test failed, 2 usage error, 3 not trusted for accessibility.
"""

import argparse
import collections
import glob
import gzip
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import RE_OUTPUT, SelfTest, ensure_dir, md_table, write_out  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SWIFT = os.path.join(HERE, "ax_dump.swift")
BIN = os.path.join(RE_OUTPUT, "bin", "ax_dump")

# Carbon menu glyph codes (AXMenuItemCmdGlyph) for keys that have no character.
GLYPHS = {2: "Tab", 4: "Enter", 9: "Space", 10: "Fwd-Del", 11: "Return", 23: "Delete", 27: "Esc", 28: "Clear",
          98: "PgUp", 100: "Left", 101: "Right", 102: "Home", 104: "Up", 105: "End", 106: "Down", 107: "PgDn",
          **{111 + i: "F%d" % (i + 1) for i in range(12)}, 135: "F13", 136: "F14", 137: "F15"}


def ax_bin():
    """Compile ax_dump.swift once into ~/re-output/bin (rebuilt when the source is newer)."""
    if not os.path.exists(BIN) or os.path.getmtime(BIN) < os.path.getmtime(SWIFT):
        ensure_dir(os.path.dirname(BIN))
        r = subprocess.run(["swiftc", "-O", SWIFT, "-o", BIN], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("swiftc failed: " + r.stderr[-400:])
    return BIN


def dump(pid, depth=40, nodes=50000):
    r = subprocess.run([ax_bin(), str(pid), str(depth), str(nodes)], capture_output=True, timeout=600)
    if r.returncode == 3:
        raise PermissionError("not trusted for accessibility")
    if r.returncode != 0:
        raise RuntimeError("ax_dump exit %d: %s" % (r.returncode, r.stderr.decode()[-300:]))
    return json.loads(r.stdout)


# ---------------------------------------------------------------- app lifecycle

def find_app(name):
    if name.endswith(".app") and os.path.isdir(name):
        return name
    apps = glob.glob("/Applications/*.app") + glob.glob("/Applications/*/*.app") + glob.glob("/System/Applications/*.app")
    want = name.lower()
    exact = [a for a in apps if os.path.basename(a)[:-4].lower() == want]
    hits = exact or [a for a in apps if want in os.path.basename(a)[:-4].lower()]
    if len(hits) != 1:
        raise ValueError("'%s' matches %d apps: %s" % (name, len(hits), ", ".join(os.path.basename(h) for h in hits[:8]) or "none"))
    return hits[0]


def bundle_id(app):
    r = subprocess.run(["plutil", "-extract", "CFBundleIdentifier", "raw", os.path.join(app, "Contents/Info.plist")],
                       capture_output=True, text=True)
    return r.stdout.strip()


def app_pid(app):
    exe = subprocess.run(["plutil", "-extract", "CFBundleExecutable", "raw", os.path.join(app, "Contents/Info.plist")],
                         capture_output=True, text=True).stdout.strip()
    r = subprocess.run(["pgrep", "-f", os.path.join(app, "Contents/MacOS", exe)], capture_output=True, text=True)
    pids = [int(p) for p in r.stdout.split()]
    return min(pids) if pids else None


def bundle_pids(app):
    """Every process started from inside the bundle (helpers, engines, a second UI process)."""
    r = subprocess.run(["pgrep", "-f", os.path.join(app, "Contents/")], capture_output=True, text=True)
    return [int(p) for p in r.stdout.split()]


def screen_windows(pids):
    """Normal windows owned by pid(s) (any Space) from the window server (no accessibility needed)."""
    pids = [pids] if isinstance(pids, int) else pids
    if not pids:
        return 0
    r = subprocess.run([ax_bin(), "--windows", ",".join(map(str, pids))], capture_output=True, text=True, timeout=30)
    return int(r.stdout.strip() or 0)


def launch_and_wait(app, wait, log):
    subprocess.run(["open", "-g", "-a", app], check=True)
    t0 = time.time()
    pid = None
    while time.time() - t0 < wait:
        pid = pid or app_pid(app)
        if pid and screen_windows(bundle_pids(app)) > 0:  # Bitwig's window is a child's
            break
        time.sleep(0.5)
    if not pid:
        raise RuntimeError("did not start within %d s" % wait)
    # windows appear before their contents; let the UI settle
    time.sleep(8)
    log("  %s up after %.0f s (pid %d)" % (os.path.basename(app), time.time() - t0, pid))
    return pid, time.time() - t0


def quit_app(app, pid, log, force=False):
    subprocess.run(["osascript", "-e", 'tell application id "%s" to quit' % bundle_id(app)], capture_output=True, timeout=60)
    for _ in range(30):
        if subprocess.run(["kill", "-0", str(pid)], capture_output=True).returncode != 0:
            return True
        time.sleep(1)
    if force:
        # A fresh Infinite window is "dirty", so a polite quit opens its Save/Don't Save modal and the
        # AppleEvent returns "User canceled". Benches have their numbers by now: end the process.
        log("  %s kept open by its unsaved-changes modal; SIGTERM" % os.path.basename(app))
        subprocess.run(["kill", "-TERM", str(pid)], capture_output=True)
        for _ in range(10):
            if subprocess.run(["kill", "-0", str(pid)], capture_output=True).returncode != 0:
                return True
            time.sleep(1)
    log("  %s still running after quit (a dialog?); left open" % os.path.basename(app))
    return False


# ---------------------------------------------------------------- analysis

def shortcut(m):
    if "key" not in m and "glyph" not in m:
        return None
    mods = m.get("mods", 0)
    s = ""
    if mods & 4:
        s += "Ctrl+"
    if mods & 2:
        s += "Opt+"
    if mods & 1:
        s += "Shift+"
    if not mods & 8:
        s += "Cmd+"
    key = m.get("key")
    if not key or key in ("\x7f",) or ord(key[0]) < 32 or 0xF700 <= ord(key[0]) <= 0xF8FF:
        g = int(m.get("glyph", 0) or 0)
        key = GLYPHS.get(g, "glyph%d" % g) if g else ("key%d" % ord(key[0]) if key else "?")
    return s + key.upper()


def menu_items(menubar):
    """[(path, title, shortcut, enabled)] for every leaf item, Apple menu skipped."""
    out = []

    def walk(n, path):
        for k in n.get("kids", []):
            role = k.get("role")
            title = k.get("title")
            if role in ("AXMenuBarItem", "AXMenuItem"):
                sub = [c for c in k.get("kids", []) if c.get("role") == "AXMenu"]
                if sub:
                    for s in sub:
                        walk(s, path + [title or "?"])
                elif title:
                    out.append((" > ".join(path), title, shortcut(k), not k.get("disabled")))
            else:
                walk(k, path)

    for top in menubar.get("kids", [])[1:]:  # [0] is the Apple menu
        walk({"kids": [top]}, [])
    return out


def walk_win(n, depth=0):
    yield n, depth
    for k in n.get("kids", []):
        yield from walk_win(k, depth + 1)


def window_stats(windows):
    roles = collections.Counter()
    named = 0
    total = 0
    leaf_area = opaque_area = 0.0
    for w in windows:
        for n, _ in walk_win(w):
            total += 1
            roles[n.get("role", "?")] += 1
            if n.get("title") or n.get("desc") or n.get("help"):
                named += 1
            if not n.get("kids") and n.get("frame") and n is not w:
                a = n["frame"][2] * n["frame"][3]
                leaf_area += a
                # a big leaf with nothing to say is a custom-drawn surface
                if n.get("role") in ("AXGroup", "AXUnknown", "AXLayoutArea", "AXScrollArea") and not (n.get("title") or n.get("desc")):
                    opaque_area += a
    win_area = sum(w["frame"][2] * w["frame"][3] for w in windows if w.get("frame")) or 1.0
    return {"nodes": total, "named": named, "roles": roles.most_common(12),
            "opaque_frac": min(1.0, opaque_area / win_area), "leaf_frac": min(1.0, leaf_area / win_area)}


def outline(windows, max_depth=4, max_lines=80):
    L = []
    for w in windows:
        for n, d in walk_win(w):
            if d > max_depth:
                continue
            label = n.get("title") or n.get("desc") or n.get("help")
            if not label and d > 0:
                continue
            L.append("%s- %s%s%s" % ("  " * d, n.get("role", "?").replace("AX", ""),
                                     (" \"%s\"" % label.replace("|", "/")) if label else "",
                                     " %dx%d" % (n["frame"][2], n["frame"][3]) if n.get("frame") and d <= 1 else ""))
            if len(L) >= max_lines:
                L.append("- ... (outline cut at %d lines)" % max_lines)
                return L
    return L


def norm_action(t):
    t = t.lower().replace("…", "").replace("...", "")
    return re.sub(r"[^a-z0-9 ]+", "", t).strip()


# ---------------------------------------------------------------- run

def map_app(name, wait, out_dir, log):
    app = find_app(name)
    label = os.path.basename(app)[:-4]
    pid = app_pid(app)
    launched = pid is None
    startup = None
    if launched:
        pid, startup = launch_and_wait(app, wait, log)
    try:
        d = dump(pid)
    finally:
        if launched:
            quit_app(app, pid, log)
    with gzip.open(os.path.join(out_dir, re.sub(r"[^A-Za-z0-9]+", "_", label) + ".json.gz"), "wt") as f:
        json.dump(d, f)
    items = menu_items(d.get("menubar", {}))
    return {"app": label, "bundle": bundle_id(app), "launched": launched, "startup_s": startup,
            "truncated": d.get("truncated"), "menus": len(d.get("menubar", {}).get("kids", [])) - 1,
            "items": items, "windows": len(d.get("windows", [])), "stats": window_stats(d.get("windows", [])),
            "outline": outline(d.get("windows", []))}


def report(res):
    ok = [r for r in res if "error" not in r]
    L = ["# UI map (accessibility tree, read-only)", "",
         "Generated by `tools/re-survey/ui_map.py`. Opaque = share of the window covered by unnamed leaf groups "
         "(custom-drawn UI the accessibility tree cannot see into).", ""]
    rows = []
    for r in res:
        if "error" in r:
            rows.append((r["app"], "FAILED: " + r["error"], "", "", "", "", "", ""))
            continue
        sc = [i for i in r["items"] if i[2]]
        st = r["stats"]
        rows.append((r["app"], str(r["menus"]), str(len(r["items"])), str(len(sc)), str(r["windows"]), str(st["nodes"]),
                     "%.0f %%" % (100 * st["opaque_frac"]), "-" if r["startup_s"] is None else "%.0f" % r["startup_s"]))
    L += [md_table(["App", "Menus", "Menu items", "Shortcuts", "Windows", "AX nodes", "Opaque", "Up after s"], rows), ""]
    # cross-app actions
    if len(ok) >= 2:
        by = collections.defaultdict(dict)
        for r in ok:
            for path, title, sc, _ in r["items"]:
                k = norm_action(title)
                if k and not by[k].get(r["app"]):  # first shortcut wins over a bare item of the same name
                    by[k][r["app"]] = sc or ""
        shared = sorted(k for k, v in by.items() if len(v) >= 2 and any(v.values()))
        if shared:
            apps = [r["app"] for r in ok]
            L += ["## Actions two or more apps name", "",
                  md_table(["Action"] + apps, [(k,) + tuple(by[k].get(a, "") if a in by[k] else "·" for a in apps) for k in shared]),
                  "", "`·` = no menu item of that name; empty = item without a shortcut.", ""]
    for r in ok:
        L += ["## %s (%s)" % (r["app"], r["bundle"]), ""]
        st = r["stats"]
        L.append("- %d AX nodes in %d window(s), %d named; roles: %s" % (
            st["nodes"], r["windows"], st["named"], ", ".join("%s %d" % (k.replace("AX", ""), v) for k, v in st["roles"])))
        if r["truncated"]:
            L.append("- WARNING: tree cut at the node cap")
        coll = collections.defaultdict(list)
        for path, title, sc, _ in r["items"]:
            if sc:
                coll[sc].append("%s > %s" % (path, title))
        dup = {k: v for k, v in coll.items() if len(v) > 1}
        if dup:
            L.append("- Same shortcut on several items (context-dependent or a clash): " +
                     "; ".join("%s: %s" % (k, " / ".join(v[:3])) for k, v in sorted(dup.items())[:15]))
        L += ["", "### Window outline (named elements, depth <= 4)", ""] + r["outline"]
        L += ["", "### Menus", "", md_table(["Menu", "Item", "Shortcut"],
              [(p, t + ("" if en else " (disabled)"), sc or "") for p, t, sc, en in r["items"]]), ""]
    return "\n".join(L) + "\n"


def run(apps, wait=90, log=print):
    stamp = time.strftime("%Y%m%d-%H%M%S")
    out_dir = ensure_dir(os.path.join(RE_OUTPUT, "ui-map", stamp))
    res = []
    for a in apps:
        try:
            res.append(map_app(a, wait, out_dir, log))
            log("mapped %s" % res[-1]["app"])
        except PermissionError:
            raise
        except Exception as e:  # one app must not stop the rest
            res.append({"app": a, "error": str(e)})
            log("FAILED %s: %s" % (a, e))
    return res, out_dir


def self_test():
    t = SelfTest("ui_map")
    t.check(shortcut({"key": "N", "mods": 0}) == "Cmd+N", "Cmd+N")
    t.check(shortcut({"key": "N", "mods": 1}) == "Shift+Cmd+N", "Shift+Cmd+N")
    t.check(shortcut({"key": "N", "mods": 4}) == "Ctrl+Cmd+N", "Ctrl+Cmd+N")
    t.check(shortcut({"key": "\x1b", "glyph": "27", "mods": 8}) == "ESC", "glyph without Cmd")
    t.check(shortcut({"mods": 0}) is None, "no key = no shortcut")
    fake = {"kids": [{"role": "AXMenuBarItem", "title": "Apple"},
                     {"role": "AXMenuBarItem", "title": "File", "kids": [{"role": "AXMenu", "kids": [
                         {"role": "AXMenuItem", "title": "New", "key": "N", "mods": 0},
                         {"role": "AXMenuItem", "title": "Export", "kids": [{"role": "AXMenu", "kids": [
                             {"role": "AXMenuItem", "title": "Audio…", "disabled": True}]}]}]}]}]}
    it = menu_items(fake)
    t.check(it == [("File", "New", "Cmd+N", True), ("File > Export", "Audio…", None, False)], "menu paths (%s)" % it)
    try:
        ax_bin()
    except Exception as e:
        t.skip("swiftc: %s" % e)
        return t.done()
    pid = int(subprocess.run(["pgrep", "-x", "Finder"], capture_output=True, text=True).stdout.split()[0])
    try:
        d = dump(pid)
    except PermissionError:
        t.skip("not trusted for accessibility")
        return t.done()
    items = menu_items(d["menubar"])
    t.check(len(items) > 50, "Finder has > 50 menu items (%d)" % len(items))
    t.check(any(sc == "Shift+Cmd+N" for _, _, sc, _ in items), "Finder's New Folder is Shift+Cmd+N")
    return t.done()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--self-test", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    rp = sub.add_parser("run")
    rp.add_argument("apps", nargs="+")
    rp.add_argument("--wait", type=int, default=90, help="seconds to wait for a launched app's first window")
    rp.add_argument("--out")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.cmd != "run":
        ap.print_usage(sys.stderr)
        return 2
    try:
        for x in a.apps:
            find_app(x)
    except ValueError as e:
        print("usage: %s" % e, file=sys.stderr)
        return 2
    try:
        res, out_dir = run(a.apps, a.wait, log=lambda m: print(m, file=sys.stderr))
    except PermissionError:
        print("not trusted for accessibility (System Settings > Privacy & Security > Accessibility)", file=sys.stderr)
        return 3
    write_out(report(res), a.out)
    print("raw trees: %s" % out_dir, file=sys.stderr)
    return 0 if all("error" not in r for r in res) else 1


if __name__ == "__main__":
    sys.exit(main())
