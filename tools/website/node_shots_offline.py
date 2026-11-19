#!/usr/bin/env python3
"""Offline node pictures for the website: every node with its params open and something happening in its preview.

Runs an off-screen Infinite (no window, works with the screen locked), spawns each node, feeds every input with a
real source (Voronoi texture for pictures, the app's own auto-wire for 3D/audio/notes/modulators), opens the params,
zooms and crops the card.  Then tools/website/node_shots_to_webp.py turns the PNGs into website/assets/nodes/hd.

  python3 tools/website/node_shots_offline.py OUT_DIR [--nodes "Bloom,Delete"] [--app PATH]
"""
import argparse, os, subprocess, sys, tempfile, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import node_screenshot as ns  # noqa: E402

APP = os.path.join(ROOT, "build-dev", "Infinite.app", "Contents", "MacOS", "Infinite")
TOKFILE = os.path.expanduser("~/Library/Application Support/Infinite/control_token")
PORT = 7796
RICH = ("Texture", "Source")           # grey Voronoi cells: structure for every picture effect to act on


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--nodes", help="comma list of node type names (default: every node)")
    ap.add_argument("--app", default=APP)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="nodeshots_")
    env = dict(os.environ, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_CONTROL_PORT=str(PORT), INFINITE_NOSPLASH="1",
               INFINITE_NO_UPDATE_CHECK="1", INFINITE_HIDE_TELEMETRY="1",
               IMAGERESYNTH_SCREENSHOT=os.path.join(tmp, "unused.png"), INFINITE_SCREENSHOT_FRAME="999999999",
               INFINITE_SHOT_REQUEST=os.path.join(tmp, "req"))
    state = {}

    def start():
        state["proc"] = subprocess.Popen([a.app], env=env, stdout=open(os.path.join(tmp, "log"), "w"), stderr=subprocess.STDOUT)
        time.sleep(15)
        state["c"] = ns.RpcClient("127.0.0.1", PORT, open(TOKFILE).read().strip())

    def stop():
        try:
            state["c"].call("quit")
        except Exception:
            pass
        time.sleep(2)
        state["proc"].terminate()
        time.sleep(2)

    start()
    c = state["c"]
    fails = []
    try:
        cats = c.call("list_node_types")
        todo = [(cat, n) for cat, ns_ in cats.items() for n in ns_]
        if a.nodes:
            want = set(a.nodes.split(","))
            todo = [t for t in todo if t[1] in want]
        for cat, name in todo:
            if os.path.exists(os.path.join(a.out, name + ".png")):
                continue
            c = state["c"]
            try:
                idx = c.call("create_node", typeName=name, category=cat, x=40.0, y=40.0)["index"]
                w = c.call("auto_wire_inputs", index=idx)
                feeders = list(w.get("feederIndices", []))
                slots = w.get("wiredSlots", [])
                graph = c.call("get_graph")
                nodes = graph.get("nodes", graph) if isinstance(graph, dict) else graph
                kinds = {n.get("index"): n.get("typeName", n.get("type")) for n in nodes}
                if any(kinds.get(f) in ("Circle", "Shape") for f in feeders) and cat not in ("Source",):
                    rich = c.call("create_node", typeName=RICH[0], category=RICH[1], x=-8000.0, y=-6000.0)["index"]
                    feeders.append(rich)
                    for s in slots:
                        try:
                            c.call("connect", srcIndex=rich, srcOutput=0, dstIndex=idx, dstSlot=s)
                        except Exception:
                            pass
                c.call("expand_node", index=idx, advanced=True)
                c.call("fit_view_node", index=idx, zoom=True)
                time.sleep(3.0)
                p = os.path.abspath(os.path.join(a.out, name + ".png"))
                r = c.call("screenshot_node", index=idx, path=p, padding=12)
                print("ok  ", cat, name, r["width"], r["height"], flush=True)
                c.call("delete_node", index=idx)
                for f in feeders:
                    try:
                        c.call("delete_node", index=f)
                    except Exception:
                        pass
            except Exception as e:
                fails.append(name)
                print("FAIL", cat, name, e, flush=True)
                stop(); start()      # a stalled node must not desync the next replies
    finally:
        stop()
    print("failed:", fails)


if __name__ == "__main__":
    main()
