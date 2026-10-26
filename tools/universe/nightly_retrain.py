#!/usr/bin/env python3
"""Self-improving nightly retrain loop for Generate Samples:
   1. Harvests any new user patches (patch_pieces.py) and refreshes theory-based synthetic pieces (synth_music.py).
   2. Runs train_all.py (mix.py -> learn.py -> 6-gate benchmark.py -> export_critic_inc.py).
   3. Records a timestamped audit entry in ~/Infinite/training-data/models/retrain_history.jsonl.
   4. Never overwrites a kept model or C++ Critic header unless all held-out scores and all 6 benchmark gates pass."""
import argparse, datetime, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MODELS = os.path.expanduser("~/Infinite/training-data/models")


def run_step(script, *args, isolated=True):
    cmd = [sys.executable, *(("-I",) if isolated else ()), os.path.join(HERE, script), *args]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ingest-new-midi", action="store_true", help="Also re-scan training-data/inbox for new MIDI files")
    ap.add_argument("--skip-mix", action="store_true", help="Skip re-running EM mixture if drum store did not change")
    a = ap.parse_args()

    t0 = datetime.datetime.now(datetime.timezone.utc).isoformat()
    print("=== Infinite Generate Samples: Nightly Self-Improving Retrain (%s) ===" % t0)

    # 1. Refresh user patches & synthetic pieces
    rc_p, out_p = run_step("patch_pieces.py")
    print("[1/3] patch_pieces.py:", out_p.strip().splitlines()[-1] if out_p.strip() else "ok")
    rc_s, out_s = run_step("synth_music.py")
    print("[2/3] synth_music.py:", out_s.strip().splitlines()[-1] if out_s.strip() else "ok")

    # 2. Run train_all.py (models + 6-gate benchmark + C++ header export)
    train_args = []
    if not a.ingest_new_midi:
        train_args.append("--skip-ingest")
    if a.skip_mix:
        train_args.append("--skip-mix")
    rc_t, out_t = run_step("train_all.py", *train_args)
    print("[3/3] train_all.py:\n" + out_t.strip())

    # 3. Append audit entry to retrain_history.jsonl
    os.makedirs(MODELS, exist_ok=True)
    ship_cur = os.path.join(MODELS, "shippable", "current.json")
    scores = {}
    if os.path.exists(ship_cur):
        try:
            scores = json.load(open(ship_cur)).get("scores", {})
        except Exception:
            scores = {}
    entry = {
        "timestamp": t0,
        "status": "PASS" if rc_t == 0 else "REJECTED",
        "scores": scores,
    }
    hist_path = os.path.join(MODELS, "retrain_history.jsonl")
    with open(hist_path, "a") as f:
        f.write(json.dumps(entry) + "\n")
    print("Audit logged to %s (status=%s)" % (hist_path, entry["status"]))
    sys.exit(rc_t)


if __name__ == "__main__":
    main()
