#!/usr/bin/env python3
"""Drives a real, running Infinite over its RPC port (R475).

Launches the app (or reuses one already listening), builds a small patch through the RPC, then
checks that `explain` names controls by key, `render_frame` writes a real PNG, and
`batch` is all-or-nothing. A GUI is required, so this is not part of headless_smoke.sh.

    python3 scripts/live_rpc_smoke.py [--app build/Infinite.app] [--port 7777]
"""
import argparse
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import node_screenshot as ns  # noqa: E402

failed = 0


def check(label, ok):
    global failed
    print(("[pass] " if ok else "[FAIL] ") + label)
    if not ok:
        failed += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="build/Infinite.app")
    ap.add_argument("--port", type=int, default=ns.DEFAULT_PORT)
    args = ap.parse_args()

    proc = None
    if not ns.already_running(args.port):
        proc = ns.launch_app(args.app)
    try:
        ns.wait_for_token_and_port(args.port)
        c = ns.RpcClient("127.0.0.1", args.port, open(ns.TOKEN_PATH).read().strip())
        c.call("new_patch")
        shape = c.call("create_node", typeName="Shape", category="Source")["index"]
        out = c.call("create_node", typeName="Output", category="Utility")["index"]
        c.call("connect", srcIndex=shape, srcOutput=0, dstIndex=out, dstSlot=0)
        c.call("set_param", index=shape, name="sizeX", value=0.6)
        c.call("set_param", index=shape, name="shapeType", value=6)
        time.sleep(2.0)  # let the nodes draw so their controls register

        r = c.call("explain")
        text = r["text"]
        check("explain: cable names both ends and the slot", "cable Output (%d) slot 0 [in] <- Shape (%d)" % (out, shape) in text)
        check("explain: changed param listed by key", "    sizeX 0.6\n" in text)
        # Known gap (quest filed with R475): the live join is address-only, so a dropdown shows its number
        # where the CLI shows "Star (6)".
        check("explain: dropdown listed by key", "    shapeType 6\n" in text or "    shapeType Star (6)\n" in text)
        check("explain: graph json has both nodes", len(r["graph"]["nodes"]) == 2)

        path = os.path.join(tempfile.gettempdir(), "infinite_live_rpc_smoke.png")
        if os.path.exists(path):
            os.remove(path)
        f = c.call("render_frame", path=path)
        check("render_frame: writes a PNG with stats", os.path.getsize(path) > 0 and f["width"] > 0 and "mean_luma" in f["frame_stats"])
        try:
            c.call("render_frame")
            check("render_frame: no path is an error", False)
        except RuntimeError:
            check("render_frame: no path is an error", True)

        before = len(c.call("get_graph")["nodes"])
        try:
            c.call("batch", calls=[{"method": "create_node", "params": {"typeName": "Shape", "category": "Source"}},
                                   {"method": "delete_node", "params": {"index": 99999}}])
            check("batch: failing call fails the batch", False)
        except RuntimeError:
            check("batch: failing call fails the batch", True)
        check("batch: nothing applied after a failure", len(c.call("get_graph")["nodes"]) == before)
        c.close()
    finally:
        if proc is not None:
            proc.terminate()
    print("LIVE RPC SMOKE %s" % ("FAIL" if failed else "OK"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
