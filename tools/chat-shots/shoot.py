#!/usr/bin/env python3
"""Chat review shots: open the real app on a fixture chat in a throw-away HOME and save one PNG per scene.

    python3 tools/chat-shots/shoot.py                       # every scene, dev build, dark
    python3 tools/chat-shots/shoot.py --scene queue --theme light
    python3 tools/chat-shots/shoot.py --app ~/Desktop/Infinite.app --out /tmp/shots

Each scene = a thread (fixtures below), optional queued lines / typed input, an optional Settings page, a panel
width. Your own chats, config and sign-ins are never touched: the app runs with HOME set to a temp folder.
Shots land in --out (default build-dev/chat-shots) with an index.html contact sheet to review them side by side.
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "assets", "templates", "synths.inf")


def wav(path, seconds=1.5):
    with wave.open(path, "wb") as w:
        w.setnchannels(1), w.setsampwidth(2), w.setframerate(22050)
        w.writeframes(b"".join(struct.pack("<h", int(9000 * math.sin(i * 0.06) * math.exp(-i / 9000)))
                               for i in range(int(22050 * seconds))))


def threads(rec):
    long_name = rec + "/recording_generated-breakbeat-synths_20261105_104114_906_1.wav"
    return {
        "greeting": [
            {"kind": "user", "text": "hi"},
            {"kind": "agent", "agent": "antigravity", "tokens": [18234, 12000],
             "text": "Hi there! How can I help you in your patch today?\n\n[Open Nodes](open:nodes) · "
                     "[Open Piano Roll](open:piano-roll) · [Open Sample Studio](open:studio)"},
        ],
        "audio": [
            {"kind": "user", "text": "generate a sample for me please"},
            {"kind": "steps", "agent": "antigravity", "ok": False, "marks": [1, 0, 0, 0],
             "lines": ["infinite studio generate --help", "infinite studio generate synths", "infinite studio export",
                       "infinite context"]},
            {"kind": "agent", "agent": "antigravity",
             "text": "Generated a synths loop and loaded it into @Wavetable#2.\n\n" + long_name + "\n\n"
                     "[Reveal Sample](reveal:" + long_name + ") · [Go to Sampler](go:@sample player#1) · "
                     "[Open Studio](open:samplestudio)"},
        ],
        "changes": [
            {"kind": "user", "text": "make @Wavetable#2 brighter and slow the notes", "files": [rec + "/a-very-long-reference-file-name-for-wrapping.png", rec + "/b.wav", rec + "/c.mid"]},
            {"kind": "steps", "agent": "claude", "lines": ["infinite context", "infinite set 2 cutoff 0.8", "infinite set 1 rateBeats 1"]},
            {"kind": "change", "agent": "claude", "changes": [
                {"text": "@Wavetable#2.cutoff 0.35 → 0.8", "node": 2, "key": "cutoff", "after": "0.8"},
                {"text": "@Random Note Generator#1.rateBeats 0.5 → 1", "node": 1, "key": "rateBeats", "after": "1"}]},
            {"kind": "agent", "agent": "claude", "tokens": [9400, 7000],
             "text": "Raised the **cutoff** on @Wavetable#2 and halved the note rate on @Random Note Generator#1.\n\n"
                     "- `cutoff` 0.35 → 0.8\n- `rateBeats` 0.5 → 1\n\nChanged: @Wavetable#2.cutoff, @Random Note Generator#1.rateBeats"},
        ],
        "errors": [
            {"kind": "user", "text": "export a video of this"},
            {"kind": "error", "agent": "codex", "text": "Codex is not signed in. Sign in from Settings > Agents, then send again."},
            {"kind": "note", "text": "Not allowed: ffprobe /Users/you/Desktop/out.mp4"},
            {"kind": "agent", "agent": "codex", "text": "A long reply with a path that never breaks: /Users/you/Library/Application Support/Infinite/Recordings/some/really/deep/folder/structure/that/keeps/going/output_final_v2_master.mp4 and an unbroken token " + "x" * 120},
        ],
    }


SCENES = {
    # id: (thread, queue, input, extra OPENPANELS, panel width)
    "greeting": ("greeting", "", "", "", 0),
    "audio": ("audio", "", "", "", 0),
    "changes": ("changes", "", "", "", 0),
    "errors": ("errors", "", "", "", 0),
    "queue": ("greeting", "hi|and then make the drums louder with a much longer queued line that has to cut off", "Add to the queue", "", 0),
    "settings-agents": ("greeting", "", "", "settingswin,settingsplace=agents", 0),
    "float": ("audio", "", "", "chatfloat", 0),
}


def shoot(app, sid, theme, out_dir, timeout):
    thread, queue, typed, panels, _ = SCENES[sid]
    home = tempfile.mkdtemp(prefix="chatshot-")
    try:
        sup = os.path.join(home, "Library", "Application Support", "Infinite")
        rec = os.path.join(sup, "Recordings")
        os.makedirs(os.path.join(sup, "chats")), os.makedirs(os.path.join(sup, "cli-setup")), os.makedirs(rec)
        for n in ("recording_generated-breakbeat-synths_20261105_104114_906_1.wav", "b.wav"):
            wav(os.path.join(rec, n))
        open(os.path.join(rec, "c.mid"), "wb").close()
        shutil.copy(os.path.join(ROOT, "assets", "brand", "icon.png") if os.path.exists(os.path.join(ROOT, "assets", "brand", "icon.png"))
                    else PATCH, os.path.join(rec, "a-very-long-reference-file-name-for-wrapping.png"))
        json.dump({"agent": "claude", "face": "chat", "introduced": True, "welcomed": True, "folderAccess": False},
                  open(os.path.join(sup, "cli-setup", "config.json"), "w"))
        json.dump({"version": 1, "id": "shot", "title": sid, "updated": 1, "sessions": {},
                   "entries": threads(rec)[thread]}, open(os.path.join(sup, "chats", "shot.json"), "w"))
        out = os.path.join(out_dir, f"{sid}-{theme}.png")
        if os.path.exists(out):
            os.remove(out)
        env = dict(os.environ, HOME=home, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_NO_UPDATE_CHECK="1",
                   INFINITE_NOSPLASH="1", INFINITE_HIDE_TELEMETRY="1", IMAGERESYNTH_SCREENSHOT=out,
                   INFINITE_SCREENSHOT_FRAME="45",
                   INFINITE_OPENPANELS=",".join(x for x in (theme, "terminal", panels) if x))
        if queue:
            env["INFINITE_CHATSHOT_QUEUE"] = queue
        if typed:
            env["INFINITE_CHATSHOT_INPUT"] = typed
        try:
            subprocess.run([os.path.join(app, "Contents", "MacOS", "Infinite"), PATCH], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout)
        except subprocess.TimeoutExpired:
            pass
        return out if os.path.exists(out) else None
    finally:
        shutil.rmtree(home, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", default=os.path.join(ROOT, "build-dev", "Infinite.app"))
    ap.add_argument("--scene", action="append", help="scene id (repeatable); default all: " + ", ".join(SCENES))
    ap.add_argument("--theme", default="dark", choices=["dark", "light", "both"])
    ap.add_argument("--out", default=os.path.join(ROOT, "build-dev", "chat-shots"))
    ap.add_argument("--timeout", type=int, default=60)
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    os.makedirs(a.out, exist_ok=True)
    themes = ["dark", "light"] if a.theme == "both" else [a.theme]
    shots, failed = [], []
    for sid in a.scene or list(SCENES):
        for th in themes:
            p = shoot(a.app, sid, th, a.out, a.timeout)
            (shots if p else failed).append(p or f"{sid}-{th}")
            print(("ok  " if p else "FAIL ") + (p or f"{sid}-{th}"), flush=True)
    with open(os.path.join(a.out, "index.html"), "w") as f:
        f.write("<body style='background:#222;color:#ddd;font:14px sans-serif'>")
        for p in shots:
            f.write(f"<h3>{os.path.basename(p)}</h3><img src='{os.path.basename(p)}' style='max-width:100%'>")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
