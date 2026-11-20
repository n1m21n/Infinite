#!/usr/bin/env python3
"""warm.py - keep one headless Infinite open and feed it renders (no start-up or shader compile per call).

  warm.py start [--size WxH] [--time T] [--bin PATH]   start a session (env such as INFINITE_LIVE_ENGINE is kept)
  warm.py render PATCH OUT_DIR/ [--timeout S]           render one patch in the running session, wait, print its JSON
  warm.py status                                        is a session running, with which size / time / engine
  warm.py stop                                          end it (it also ends itself after 10 idle minutes)

The session is `Infinite --frame WARMUP T dir/ --batch queue.txt --serve`: every line appended to
queue.txt ("patch<TAB>out_dir/") renders in the same process, and the job leaves <out_dir>.json
with its frame_stats and errors. look.py uses a running session on its own when the size, time and
engine match.
"""
import argparse, json, os, signal, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN_DEFAULT = os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")
HOME = os.path.join(os.environ.get("TMPDIR", "/tmp"), "infinite-warm")
QUEUE, INFO = os.path.join(HOME, "queue.txt"), os.path.join(HOME, "session.json")
WARMUP = """infinite-patch 1
node 1 3D Cube
  id c
end
node 2 3D Camera
  id cam
end
node 3 3D Render 3D
  id r
  f width 64
  f height 64
end
node 4 Utility Output
  id out
end
geo r geo_a c
geo r camera cam
cable out 0 r
"""


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def info():
    """The running session's record, or None."""
    try:
        d = json.load(open(INFO))
    except Exception:
        return None
    return d if alive(d.get("pid", -1)) else None


def matches(size, t, engine=None):
    """The running session if it renders at this size / time / engine, else None."""
    d = info()
    if d is None:
        return None
    engine = os.environ.get("INFINITE_LIVE_ENGINE", "") if engine is None else engine
    if d["size"] == (size or "") and float(d["time"]) == float(t) and d["engine"] == engine:
        return d
    return None


def start(binp, size, t):
    if info():
        stop()
    os.makedirs(HOME, exist_ok=True)
    open(QUEUE, "w").close()
    wp = os.path.join(HOME, "warmup.inf")
    open(wp, "w").write(WARMUP)
    wd = os.path.join(HOME, "warmup") + "/"
    side = wd.rstrip("/") + ".json"
    if os.path.exists(side):
        os.unlink(side)
    cmd = [binp, "--frame", wp, str(t), wd, "--lenient", "--batch", QUEUE, "--serve", "--timeout", "900"]
    if size:
        cmd += ["--size", size]
    log = open(os.path.join(HOME, "session.log"), "w")
    p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    d = {"pid": p.pid, "size": size or "", "time": str(t), "engine": os.environ.get("INFINITE_LIVE_ENGINE", ""), "bin": binp}
    json.dump(d, open(INFO, "w"))
    t0 = time.time()
    while not os.path.exists(side):          # the warm-up job is the start-up and shader compile
        if p.poll() is not None:
            sys.exit(f"warm: session ended during start (exit {p.returncode}); see {HOME}/session.log")
        if time.time() - t0 > 120:
            sys.exit("warm: no warm-up result after 120 s")
        time.sleep(0.1)
    d["start_s"] = round(time.time() - t0, 2)
    return d


def submit(patch, out_dir, timeout=900):
    """Render PATCH into OUT_DIR in the running session; returns its job JSON (with wall_s)."""
    d = info()
    if d is None:
        sys.exit("warm: no session running (warm.py start)")
    patch, out_dir = os.path.abspath(patch), os.path.abspath(out_dir) + "/"
    side = out_dir.rstrip("/") + ".json"
    if os.path.exists(side):
        os.unlink(side)
    t0 = time.time()
    with open(QUEUE, "a") as q:
        q.write(f"{patch}\t{out_dir}\n")
    while not os.path.exists(side):
        if not alive(d["pid"]):
            sys.exit(f"warm: session died on {patch}; see {HOME}/session.log")
        if time.time() - t0 > timeout:
            sys.exit(f"warm: {patch} not done after {timeout} s")
        time.sleep(0.05)
    for _ in range(50):                       # the writer may still be filling the file it just created
        try:
            r = json.load(open(side))
            break
        except ValueError:
            time.sleep(0.02)
    else:
        sys.exit(f"warm: unreadable {side}")
    r["wall_s"] = round(time.time() - t0, 2)
    return r


def stop():
    d = info()
    if d is None:
        return False
    with open(QUEUE, "a") as q:
        q.write("quit\n")
    for _ in range(100):
        if not alive(d["pid"]):
            break
        time.sleep(0.1)
    if alive(d["pid"]):
        os.kill(d["pid"], signal.SIGTERM)
    os.unlink(INFO)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["start", "render", "status", "stop"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--size")
    ap.add_argument("--time", default="0")
    ap.add_argument("--timeout", type=float, default=900)
    ap.add_argument("--bin", default=BIN_DEFAULT)
    a = ap.parse_args()
    if a.cmd == "start":
        print(json.dumps(start(a.bin, a.size, a.time)))
    elif a.cmd == "render":
        if len(a.args) != 2:
            ap.error("render PATCH OUT_DIR/")
        print(json.dumps(submit(a.args[0], a.args[1], a.timeout)))
    elif a.cmd == "status":
        print(json.dumps(info() or {"running": False}))
    else:
        print(json.dumps({"stopped": stop()}))


if __name__ == "__main__":
    main()
