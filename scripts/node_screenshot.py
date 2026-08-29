#!/usr/bin/env python3
"""Spawn one Infinite node and save a cropped screenshot of it, named after
the node type.

Drives the app over its built-in loopback JSON-RPC control server
(src/core/RemoteControl.cpp) rather than any OS-level UI automation - it asks
the app to crop its own just-rendered framebuffer to the node's on-screen
rect (the "screenshot_node" RPC method), so there's no coordinate guessing
across process/DPI boundaries.

Usage:
    python3 scripts/node_screenshot.py "Wavetable"
    python3 scripts/node_screenshot.py "Audio Filter" --category AudioEffects
    python3 scripts/node_screenshot.py "Sphere" --out docs/node_screenshots --app build/Infinite.app
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time

DEFAULT_PORT = 7777
TOKEN_PATH = os.path.expanduser("~/Library/Application Support/Infinite/control_token")


class RpcClient:
    def __init__(self, host, port, token):
        self.token = token
        self.sock = socket.create_connection((host, port), timeout=10)
        self.buf = b""
        self.next_id = 1

    def call(self, method, **params):
        req = {"jsonrpc": "2.0", "id": self.next_id, "method": method,
               "params": params, "token": self.token}
        self.next_id += 1
        self.sock.sendall((json.dumps(req) + "\n").encode("utf-8"))
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("connection closed by Infinite while waiting for a reply")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        reply = json.loads(line)
        if "error" in reply:
            raise RuntimeError(f"{method} failed: {reply['error'].get('message')}")
        return reply["result"]

    def close(self):
        self.sock.close()


def wait_for_token_and_port(port, timeout=20.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(TOKEN_PATH):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                    return
            except OSError:
                pass
        time.sleep(0.2)
    raise TimeoutError("Infinite's control server never came up - is the app launching correctly?")


def launch_app(app_path):
    binary = os.path.join(app_path, "Contents", "MacOS", "Infinite")
    if not os.path.exists(binary):
        raise FileNotFoundError(f"no Infinite binary at {binary} - build it first")
    # -ApplePersistenceIgnoreState skips the "do you want to reopen windows?"
    # modal AppKit shows after an unclean exit (e.g. this same script crashing
    # a prior run) - that modal blocks the main thread before RemoteControl
    # ever starts listening, so the port/token wait below would hang forever.
    return subprocess.Popen([binary, "-ApplePersistenceIgnoreState", "YES"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def already_running(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


def find_category(client, type_name):
    categories = client.call("list_node_types")
    for cat, names in categories.items():
        if type_name in names:
            return cat
    return None


def capture_one(client, node_type, category, out_dir, padding=12, settle_seconds=1.0,
                 keep_node=False, no_expand=False, no_wire=False):
    """Spawn `node_type`, optionally auto-wire/expand it, screenshot it to
    `out_dir/<node_type>.png`, and clean up. Shared by the single-node CLI
    below and scripts/screenshot_all_nodes.py."""
    category = category or find_category(client, node_type)
    if category is None:
        raise ValueError(f"unknown node type '{node_type}' (check exact spelling/capitalization)")

    created = client.call("create_node", typeName=node_type, category=category, x=40.0, y=40.0)
    index = created["index"]

    feeder_indices = []
    if not no_wire:
        wired = client.call("auto_wire_inputs", index=index)
        feeder_indices = wired.get("feederIndices", [])
        if wired.get("wiredSlots"):
            print(f"  Auto-wired input slot(s) {wired['wiredSlots']} with {len(feeder_indices)} feeder node(s).")

    if not no_expand:
        client.call("expand_node", index=index)

    client.call("fit_view_node", index=index)
    time.sleep(settle_seconds)

    out_path = os.path.abspath(os.path.join(out_dir, f"{node_type}.png"))
    result = client.call("screenshot_node", index=index, path=out_path, padding=padding)
    print(f"  Saved {out_path} ({result['width']}x{result['height']})")

    if not keep_node:
        client.call("delete_node", index=index)
        for feeder_index in feeder_indices:
            client.call("delete_node", index=feeder_index)

    return out_path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("node_type", help='exact node type name, e.g. "Wavetable" or "Audio Filter"')
    ap.add_argument("--category", help="skip the type->category lookup if you already know it")
    ap.add_argument("--out", default="docs/node_screenshots", help="output directory (default: docs/node_screenshots)")
    ap.add_argument("--app", default="build/Infinite.app", help="path to Infinite.app (default: build/Infinite.app)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--padding", type=int, default=12, help="pixels of padding around the cropped node")
    ap.add_argument("--settle-seconds", type=float, default=1.0,
                     help="time to let the view fit and the node finish cooking before capture")
    ap.add_argument("--keep-open", action="store_true",
                     help="leave the app running afterward even if this script launched it")
    ap.add_argument("--keep-node", action="store_true",
                     help="don't delete the spawned node (and any auto-wired feeder nodes) afterward "
                          "(default: delete them, so repeated runs against a reused instance don't "
                          "pile nodes on top of each other and bleed through into the next screenshot)")
    ap.add_argument("--no-expand", action="store_true",
                     help="don't force the node's collapsed params sections open before capture")
    ap.add_argument("--no-wire", action="store_true",
                     help="don't auto-connect feeder nodes into the node's unconnected input slots")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    launched_here = False
    proc = None
    if not already_running(args.port):
        print(f"Launching {args.app} ...")
        proc = launch_app(args.app)
        launched_here = True
        wait_for_token_and_port(args.port)
    else:
        print("Infinite is already running - reusing it.")
        if not os.path.exists(TOKEN_PATH):
            raise TimeoutError("control server is up but no auth token file was found yet")

    with open(TOKEN_PATH) as f:
        token = f.read().strip()

    client = RpcClient("127.0.0.1", args.port, token)
    try:
        capture_one(client, args.node_type, args.category, args.out,
                    padding=args.padding, settle_seconds=args.settle_seconds,
                    keep_node=args.keep_node, no_expand=args.no_expand, no_wire=args.no_wire)
    finally:
        client.close()
        if launched_here and not args.keep_open and proc is not None:
            proc.terminate()
        elif launched_here:
            print("Left Infinite running (--keep-open).")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)
