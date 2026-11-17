#!/usr/bin/env python3
"""Fast node shots: one app launch per batch, every node driven over the control port.

For each node type: create it, auto-wire its inputs, fit the view zoomed, grab the zoomed shot (closed, then
params open), delete the node and its feeders, next. The app is relaunched every --relaunch-every nodes (memory) or
after a crash, and the run resumes from docs/shots/manifest.json, so a crash loses at most one node.

Isolation: pass --app to a COPY of the release app and --home to a scratch folder. The app takes its settings,
control_token and instances folder from $HOME (src/platform/AppPaths.h), so a copy with its own HOME never touches
the gate's ~/Library/Application Support/Infinite state. Use --port for a control port that no other app uses.

  python3 tools/manual/fast_node_capture.py --app /scratch/Infinite.app --home /scratch/home --port 7795 --limit 20

States: c = closed, o = params open (expand). d (first dropdown open) is not in this runner yet: it needs a click
location per node, so it stays "conditional" in docs/release/shot-plan-2.0.json.
"""
import argparse
import os
import plistlib
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
STALL_S = 45      # seconds one RPC may take before the node is logged as a stall and skipped
# Neutral path params per node, set right after create so no real or scratch folder shows in a shot.
NEUTRAL_PATHS = {
    "Output": {"exportImagePath": "Movies/my-export.png", "recordVideoPath": "Movies/my-export.mp4"},
    "Audio Out": {"recordDirectory": "Music/Recordings"},        # default was the Desktop folder
    "Spatial Mixer": {"recordDirectory": "Music/Recordings"},
}
# Nodes that show a red error outline without an input (found by tools/manual/error_scan.py). Each entry: the feeder
# chain [(type, category)] and the input slot the last feeder goes into.
FEEDS = {
    # Each node: list of (feeder chain [(type, category)], input slot the chain's last node goes into).
    "Armature": [([("Curve", "3D")], 1)],                      # path input takes a curve
    "Voronoi Cells": [([("Sphere", "3D"), ("Mesh to Points", "3D")], 0)],   # points input takes a point cloud
    "Instance on Points": [([("Sphere", "3D"), ("Mesh to Points", "3D")], 0), ([("Sphere", "3D")], 1)],
    "Intersect": [([("Sphere", "3D")], 0), ([("Cube", "3D")], 1)],   # boolean: two geometry inputs
    "Union": [([("Sphere", "3D")], 0), ([("Cube", "3D")], 1)],
    "Metaballs": [([("Sphere", "3D"), ("Mesh to Points", "3D")], 0)],
    "Revolve": [([("Curve", "3D")], 0)],                        # profile takes a curve
    "Sweep": [([("Curve", "3D")], 0), ([("Curve", "3D")], 1)],  # profile and path take curves
    "Path": [([("Curve", "3D")], 0), ([("Sphere", "3D")], 1)],  # modulator path: curve, geo
}
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "tools", "docs-shots"))
import shot_config as cfg  # noqa: E402
import shot_plan  # noqa: E402
import common as C  # noqa: E402
import capture  # noqa: E402
from node_screenshot import RpcClient  # noqa: E402
from PIL import Image  # noqa: E402


def app_version(app):
    try:
        with open(os.path.join(app, "Contents", "Info.plist"), "rb") as f:
            return plistlib.load(f).get("CFBundleShortVersionString", "")
    except Exception:
        return ""


def wait_port(port, timeout=40):
    t = time.time()
    while time.time() - t < timeout:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.25)
    return False


class App:
    """One running copy of the app, with its RPC client."""

    def __init__(self, binary, home, port, log):
        self.port = port
        # A leftover autosave plus session marker makes the app open its "didn't close properly" dialog over
        # every shot. Clear both in the scratch HOME before each launch (never the gate's HOME).
        appsup = os.path.join(home, "Library", "Application Support", "Infinite")
        for stale in ("Infinite.autosave.inf", "Infinite.session-active"):
            p = os.path.join(appsup, stale)
            if os.path.exists(p):
                os.remove(p)
        env = dict(os.environ, HOME=home, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_NOSPLASH="1",
                   INFINITE_NO_UPDATE_CHECK="1", INFINITE_HIDE_TELEMETRY="1", INFINITE_CONTROL_PORT=str(port))
        self.proc = subprocess.Popen([binary, "-ApplePersistenceIgnoreState", "YES"], env=env,
                                     stdout=log, stderr=subprocess.STDOUT)
        if not wait_port(port):
            raise RuntimeError(f"no control port {port} from pid {self.proc.pid}")
        tok = cfg.read_token(self.proc.pid, os.path.join(home, "Library", "Application Support", "Infinite"), port)
        if not tok:
            raise RuntimeError("no control token for this app")
        self.rpc = RpcClient("127.0.0.1", port, tok)
        self.rpc.sock.settimeout(STALL_S)   # per-RPC limit: a wedged shot raises instead of hanging the run
        time.sleep(3)   # first frames: let the canvas come up before the first node

    def close(self):
        try:
            self.rpc.call("quit")
        except Exception:
            pass
        try:
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()


def node_done(manifest, sid):
    """Done only when the manifest entry AND both files it names (raw and framed) are on disk."""
    e = manifest["shots"].get(sid)
    if not e or not e.get("raw") or not e.get("framed"):
        return False
    return os.path.exists(os.path.join(ROOT, e["raw"])) and os.path.exists(os.path.join(ROOT, e["framed"]))


def shoot_one(app, cat, name, manifest, force, settle):
    """Create the node once, shoot closed then open, delete it. Returns the list of finished sids."""
    done = []
    sid_c = f"node/{cat}/{name}/c"
    sid_o = f"node/{cat}/{name}/o"
    need_c = force or not node_done(manifest, sid_c)
    need_o = force or not node_done(manifest, sid_o)
    if not (need_c or need_o):
        return ["skip"]
    created = app.rpc.call("create_node", typeName=name, category=cat, x=40.0, y=40.0)
    idx = created["index"]
    for key, value in NEUTRAL_PATHS.get(name, {}).items():
        # Replace defaults that come from the Desktop / scratch HOME with neutral paths before any shot.
        app.rpc.call("set_param", index=idx, name=key, value=value)
    feeders = []
    try:
        wired = app.rpc.call("auto_wire_inputs", index=idx)
        feeders = wired.get("feederIndices", [])
    except Exception:
        pass
    for chain, slot in FEEDS.get(name, []):
        # The node needs valid inputs to show no error (red outline / "needs" tooltip): build a minimal chain per slot.
        prev = None
        for ftype, fcat in chain:
            fi = app.rpc.call("create_node", typeName=ftype, category=fcat, x=-200.0, y=40.0)["index"]
            feeders.append(fi)
            if prev is not None:
                app.rpc.call("connect", srcIndex=prev, srcOutput=0, dstIndex=fi, dstSlot=0)
            prev = fi
        app.rpc.call("connect", srcIndex=prev, srcOutput=0, dstIndex=idx, dstSlot=slot)
    try:
        app.rpc.call("fit_view_node", index=idx, zoom=True)
        time.sleep(settle)
        if need_c:
            done.append(snap(app, idx, cat, name, "c", manifest))
        if need_o:
            app.rpc.call("expand_node", index=idx, advanced=True)
            app.rpc.call("fit_view_node", index=idx, zoom=True)
            time.sleep(settle)
            done.append(snap(app, idx, cat, name, "o", manifest))
    finally:
        try:
            app.rpc.call("delete_node", index=idx)
            for f in feeders:
                app.rpc.call("delete_node", index=f)
        except Exception:
            pass
    return done


def snap(app, idx, cat, name, state, manifest):
    sid = f"node/{cat}/{name}/{state}"
    raw = os.path.join(C.SHOTS, "raw", C.slug(f"{cat}__{name}__{state}") + ".png")
    os.makedirs(os.path.dirname(raw), exist_ok=True)
    if os.path.exists(raw):
        os.remove(raw)
    res = app.rpc.call("screenshot_node", index=idx, path=raw, padding=12)
    if os.path.exists(raw):
        w0 = Image.open(raw).size[0]
        if w0 < cfg.ZOOM_MIN_WIDTH:
            # Narrow node (e.g. a short params column): widen the frame with background, never downscale.
            extra = (cfg.ZOOM_MIN_WIDTH - w0) // 2 + 8
            os.remove(raw)
            res = app.rpc.call("screenshot_node", index=idx, path=raw, padding=12 + extra)
    if not os.path.exists(raw):
        print("FAIL", sid, "no file", flush=True)
        manifest["shots"].pop(sid, None)   # never leave an entry pointing at a file that is not there
        C.save_manifest(manifest)
        return "fail"
    try:
        w, h = cfg.assert_pixels(raw, "zoom")
    except AssertionError as e:
        print("FAIL", sid, e, flush=True)
        manifest["shots"].pop(sid, None)
        C.save_manifest(manifest)
        return "fail"
    capture.finish(raw, sid, dict(kind="node", category=cat, name=name, state=state, theme="dark",
                                  source_px=[w, h]), manifest)
    C.save_manifest(manifest)
    print(f"ok   {sid}  {w}x{h}", flush=True)
    return "ok"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True, help="a copy of Infinite.app (never the gate's binary)")
    ap.add_argument("--home", required=True, help="scratch HOME for the app's settings and token")
    ap.add_argument("--port", type=int, default=7795)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--start", type=int, default=0, help="skip this many nodes in the plan order")
    ap.add_argument("--relaunch-every", type=int, default=50, help="nodes per launch (50 nodes = about 100 shots)")
    ap.add_argument("--settle", type=float, default=0.5)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default="", help="comma list of node names to shoot (for recaptures)")
    a = ap.parse_args()

    binary = os.path.join(a.app, "Contents", "MacOS", "Infinite")
    if not os.path.exists(binary):
        raise SystemExit(f"no binary {binary}")
    os.makedirs(os.path.join(a.home, "Library", "Application Support", "Infinite"), exist_ok=True)
    os.makedirs(os.path.join(a.home, "Desktop"), exist_ok=True)
    capture.PORT = a.port
    manifest = C.load_manifest()
    manifest["app"] = a.app
    manifest["version"] = app_version(a.app)
    nodes = shot_plan.nodes()[a.start:]
    if a.only:
        wanted = set(a.only.split(","))
        nodes = [n for n in nodes if n[1] in wanted]
    if a.limit:
        nodes = nodes[:a.limit]
    t0 = time.time()
    shots = fails = 0
    i = 0
    log = open(os.path.join(a.home, "app.log"), "a")
    while i < len(nodes):
        app = None
        try:
            app = App(binary, a.home, a.port, log)
            for _ in range(a.relaunch_every):
                if i >= len(nodes):
                    break
                cat, name = nodes[i]
                i += 1
                try:
                    res = shoot_one(app, cat, name, manifest, a.force, a.settle)
                except Exception as e:   # stall, crash or refusal: log the node as skipped, relaunch, carry on
                    kind = "stall" if isinstance(e, socket.timeout) else "crash"
                    print(f"{kind.upper():5} {cat}/{name}: {e}", flush=True)
                    manifest.setdefault("stalls", []).append(
                        dict(node=f"node/{cat}/{name}", kind=kind, error=str(e)[:200],
                             at=time.strftime("%Y-%m-%d %H:%M:%S"), app=a.app, pid=app.proc.pid))
                    C.save_manifest(manifest)
                    fails += 1
                    break
                shots += sum(1 for r in res if r == "ok")
                fails += sum(1 for r in res if r == "fail")
        except Exception as e:
            print("launch failed:", e, flush=True)
            time.sleep(3)
        finally:
            if app:
                app.close()
    mins = (time.time() - t0) / 60.0
    print(f"done: {shots} shots, {fails} failed, {mins:.1f} min, {shots / mins if mins else 0:.1f} shots/min", flush=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
