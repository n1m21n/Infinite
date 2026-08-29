#!/usr/bin/env python3
"""Screenshot every node type in Infinite, replacing docs/node_screenshots/.

Launches the app once, asks it for the full node type catalog (list_node_types),
then reuses node_screenshot.py's capture_one() for each one in turn against the
same running instance. Any node that fails to capture is reported at the end
rather than aborting the whole run.

Usage:
    python3 scripts/screenshot_all_nodes.py
    python3 scripts/screenshot_all_nodes.py --out docs/node_screenshots --app build/Infinite.app
"""

import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from node_screenshot import (  # noqa: E402
    DEFAULT_PORT, TOKEN_PATH, RpcClient, capture_one,
    already_running, launch_app, wait_for_token_and_port,
)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="docs/node_screenshots", help="output directory (default: docs/node_screenshots)")
    ap.add_argument("--app", default="build/Infinite.app", help="path to Infinite.app (default: build/Infinite.app)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--padding", type=int, default=12)
    ap.add_argument("--settle-seconds", type=float, default=1.0)
    ap.add_argument("--keep-open", action="store_true", help="leave the app running afterward")
    args = ap.parse_args()

    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
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

    with open(TOKEN_PATH) as f:
        token = f.read().strip()

    client = RpcClient("127.0.0.1", args.port, token)
    failures = []
    node_list = []
    try:
        categories = client.call("list_node_types")
        node_list = [(cat, name) for cat, names in categories.items() for name in names]
        print(f"Found {len(node_list)} node types across {len(categories)} categories.")

        for i, (category, name) in enumerate(node_list, 1):
            print(f"[{i}/{len(node_list)}] {category} / {name}")
            try:
                capture_one(client, name, category, args.out,
                            padding=args.padding, settle_seconds=args.settle_seconds)
            except Exception as e:
                print(f"  FAILED: {e}", file=sys.stderr)
                failures.append((category, name, str(e)))
    finally:
        client.close()
        if launched_here and not args.keep_open and proc is not None:
            proc.terminate()

    print(f"\nDone. {len(node_list) - len(failures)}/{len(node_list)} screenshots saved to {args.out}.")
    if failures:
        print(f"{len(failures)} failed:")
        for cat, name, err in failures:
            print(f"  - {cat} / {name}: {err}")
        sys.exit(1)


if __name__ == "__main__":
    main()
