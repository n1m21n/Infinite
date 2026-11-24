#!/usr/bin/env python3
"""Node-body screenshots for a UI migration that touches Draw*Body / Draw*Params (ui-formalisation Block C): every
node type, a few per frame, params closed and open, from any binary; then a pixel diff of two runs.

  body_shots.py shoot OUT_DIR [--binary PATH] [--per 6] [--cats 3D,Source] [--theme dark]
  body_shots.py compare BEFORE_DIR AFTER_DIR [--diff-out DIR]

One launch per chunk, one at a time (the 8 GB machine): INFINITE_NODEGALLERY lays the chunk out on a grid and frames
it; INFINITE_GALLERYPARAMS opens each node's params panel. Shots are named <cat>__<chunk>__<c|o>.png with a .txt of
the node names beside them, so a DIFF names the nodes to look at. compare is panel_shots' (same noise floor):
previews that animate on wall time differ run to run, so run two baseline shoots and compare them first.
"""
import argparse, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import panel_shots  # noqa: E402

ROOT = panel_shots.ROOT
DEFAULT_BIN = os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite")


def node_list(binary):
    r = subprocess.run([binary], env=dict(os.environ, INFINITE_NODELIST="1", INFINITE_EXITAFTER="5",
                                          INFINITE_NO_UPDATE_CHECK="1"),
                       capture_output=True, text=True, timeout=120)
    out = []
    for line in r.stdout.splitlines():
        if line.startswith("NODELIST "):
            cat, name = line[len("NODELIST "):].strip().split("|", 1)
            out.append((cat, name))
    return out


def shoot(out, binary, per, cats, theme):
    os.makedirs(out, exist_ok=True)
    nodes = node_list(binary)
    by_cat = {}
    for cat, name in nodes:
        if not cats or cat in cats:
            by_cat.setdefault(cat, []).append(name)
    for cat, names in sorted(by_cat.items()):
        for k in range(0, len(names), per):
            chunk = names[k:k + per]
            stem = f"{cat}__{k // per:02d}"
            with open(os.path.join(out, stem + ".txt"), "w") as f:
                f.write("\n".join(chunk) + "\n")
            for st in ("c", "o"):
                shot = os.path.join(out, f"{stem}__{st}.png")
                env = dict(os.environ, INFINITE_NODEGALLERY=cat + "|" + ",".join(chunk),
                           INFINITE_GALLERYGRID="3,520,760", INFINITE_OPENPANELS=theme,
                           INFINITE_NO_UPDATE_CHECK="1", IMAGERESYNTH_SCREENSHOT=shot,
                           INFINITE_SCREENSHOT_FRAME="40")
                if st == "o":
                    env["INFINITE_GALLERYPARAMS"] = "1"
                try:
                    subprocess.run([binary], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   timeout=120, check=False)
                except subprocess.TimeoutExpired:
                    print(f"{stem}__{st}: timed out", flush=True)
                    continue
                print(f"{stem}__{st}: {'ok' if os.path.exists(shot) else 'no shot'}", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("shoot")
    s.add_argument("out")
    s.add_argument("--binary", default=DEFAULT_BIN)
    s.add_argument("--per", type=int, default=6)
    s.add_argument("--cats", default="")
    s.add_argument("--theme", default="dark")
    c = sub.add_parser("compare")
    c.add_argument("before")
    c.add_argument("after")
    c.add_argument("--diff-out", default="")
    a = ap.parse_args()
    if a.cmd == "shoot":
        shoot(a.out, a.binary, a.per, set(x for x in a.cats.split(",") if x), a.theme)
        return 0
    return panel_shots.compare(a.before, a.after, a.diff_out or os.path.join(a.after, "_diff"))


if __name__ == "__main__":
    sys.exit(main())
