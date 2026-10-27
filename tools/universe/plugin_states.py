#!/usr/bin/env python3
"""Headless plug-in state templates: no GUI, no saved patch.

    plugin_states.py [--only diva] [--kinds instrument] [--formats au,vst3] [--out plugin_states.jsonl] [--resume]

For every installed plug-in (same listing as render_pass.py) build a Keyboard -> Plugin -> Audio Out patch, run it with the
headless `--plugin-state` flag (Infinite 0.6.7+) and keep the plug-in's own saved state. A row says whether the state decodes as
text (u-he .h2p, Vital JSON, XML: splice a preset in exactly, omni_render.py pattern) or binary (level 2 knob route only).
Rows: {id, name, format, instrument, ready, params, state_b64, bytes, kind, head}."""
import argparse, base64, json, os, re, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pass as rp  # noqa: E402


def kind_of(raw):
    body = raw[32:] if len(raw) > 32 else raw
    txt = sum(1 for c in body[:4000] if 32 <= c < 127 or c in (9, 10, 13))
    n = max(1, min(len(body), 4000))
    if body.lstrip()[:1] in (b"{", b"<") or txt / n > 0.9:
        return "text"
    return "xml-in-binary" if b"<?xml" in body or b"<" in body[:200] else "binary"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--kinds", default="instrument,effect")
    ap.add_argument("--formats", default="au,vst3")
    ap.add_argument("--retry-from", default="")
    ap.add_argument("--out", default="~/Infinite/training-data/audio/plugin_states.jsonl")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--timeout", type=int, default=60)
    a = ap.parse_args()
    out = os.path.expanduser(a.out)
    done = set()
    if a.resume and os.path.exists(out):
        done = {json.loads(l)["id"] for l in open(out) if l.strip()}
    binp = rp.infinite_bin()
    cache = {}
    with tempfile.TemporaryDirectory() as tmp:
        stim = os.path.join(tmp, "stim.wav")
        rp.write_wav24(stim, rp.stimulus())
        for t in rp.work_items(binp, a, cache):
            if t["id"] in done:
                continue
            inf, js = os.path.join(tmp, "p.inf"), os.path.join(tmp, "s.json")
            open(inf, "w").write(rp.patch_text(t, stim, []))
            row = {"id": t["id"], "name": t["name"], "format": t["format"], "instrument": t["instrument"], "ready": False}
            try:
                p = subprocess.run([binp, "--audio-summary", inf, js, "--duration", "2", "--sample-rate", str(rp.SR), "--plugin-state",
                                    "--lenient", "--timeout", str(max(5, a.timeout - 5))], capture_output=True, text=True, timeout=a.timeout)
                d = json.loads(p.stdout.strip().splitlines()[-1]) if p.stdout.strip() else {}
                pl = (d.get("plugins") or [{}])[0]
                row["ready"] = bool(pl.get("ready"))
                row["params"] = pl.get("param_count")
                st = pl.get("state") or ""
                if st:
                    raw = base64.b64decode(st + "=" * (-len(st) % 4))
                    row.update(state_b64=st, bytes=len(raw), kind=kind_of(raw), head=re.sub(rb"[^\x20-\x7e]", b".", raw[:120]).decode())
            except subprocess.TimeoutExpired:
                row["error"] = "timeout"
            except Exception as e:  # noqa: BLE001 - one bad plug-in must not stop the sweep
                row["error"] = str(e)[:100]
            with open(out, "a") as fo:
                fo.write(json.dumps(row, separators=(",", ":")) + "\n")
            print(row["name"], row["ready"], row.get("bytes"), row.get("kind"), row.get("error", ""), flush=True)


if __name__ == "__main__":
    main()
