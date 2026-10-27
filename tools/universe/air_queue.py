#!/usr/bin/env python3
"""Sequential preset-render queue for the Air (run from Terminal so AU/VST3 plug-ins get a GUI session):
u-he text presets, Vital, TAL, then every NKS library (probe once per plug-in). Resumable: rerun it and it carries on."""
import json, os, subprocess, sys
H = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
STATES = HOME + "/Infinite/training-data/audio/plugin_states_key.jsonl"
ALL = HOME + "/Infinite/training-data/audio/plugin_states.jsonl"
AR = "/Library/Arturia/%s/Third Party/Native Instruments/presets"
ST = [json.loads(l) for f in (STATES, ALL) if os.path.exists(f) for l in open(f)]
NAMES = {r["name"].lower(): r["name"] for r in ST if r.get("state_b64") and r["format"] == "vst3"}
MERGED = HOME + "/Infinite/training-data/audio/plugin_states_merged.jsonl"
with open(MERGED, "w") as f:
    seen = set()
    for r in ST:
        if r.get("state_b64") and r["format"] == "vst3" and r["name"] not in seen:
            seen.add(r["name"]); f.write(json.dumps(r) + "\n")


def run(args):
    while True:
        p = subprocess.run([sys.executable, "-I", H + "/preset_state.py", "--states", MERGED, "--budget", "25", "--resume"] + args, capture_output=True, text=True)
        tail = (p.stdout.strip().splitlines() or ["?"])[-1]
        print(args[:2], tail, flush=True)
        if not tail.startswith("done:") or tail.startswith("done: 0 ok"):
            return


def name_for(folder):
    k = folder.lower()
    for n in NAMES:
        if n == k or n.replace("-", " ") == k.replace("-", " "):
            return NAMES[n]
    return None


JOBS = [("Hive", "h2p", "/Library/Audio/Presets/u-he/Hive"), ("Repro-1", "h2p", "/Library/Audio/Presets/u-he/Repro-1"),
        ("Repro-5", "h2p", "/Library/Audio/Presets/u-he/Repro-5"), ("Vital", "vital", HOME + "/Music/Vital"),
        ("TAL BassLine 101", "bassline", HOME + "/Library/Application Support/ToguAudioLine/TAL-BassLine-101"),
        ("TAL-J-8", "pjp8", HOME + "/Library/Application Support/ToguAudioLine/TAL-J-8"),
        ("TAL-NoiseMaker", "noisemakerpreset", HOME + "/Library/Application Support/ToguAudioLine/TAL-NoiseMaker"),
        ("TAL Mod", "talmod", HOME + "/Library/Application Support/ToguAudioLine/TAL-Mod"),
        ("TAL U-No-LX-V2", "pjunoxl", HOME + "/Library/Application Support/ToguAudioLine/TAL-U-No-LX"),
        ("Massive X", "nksf", "/Users/Shared/Massive X Factory Library/Presets"), ("Pigments", "nksf", AR % "Pigments")]
for d in sorted(os.listdir("/Library/Arturia")) if os.path.isdir("/Library/Arturia") else []:
    if os.path.isdir(AR % d):
        n = name_for(d)
        if n:
            JOBS.append((n, "nksf", AR % d))
        else:
            print("no vst3 state for", d, flush=True)
JOBS.append(("SubLab", "nksf", "/Library/Application Support/FAW/SubLab/ThirdParty/NativeInstruments/Presets"))
for plugin, ext, root in JOBS:
    if ext == "nksf" and plugin.lower() not in json.load(open(H + "/nks_variants.json")) if os.path.exists(H + "/nks_variants.json") else ext == "nksf":
        subprocess.run([sys.executable, "-I", H + "/nks_state.py", "--probe", plugin, "--presets", root, "--states", MERGED])
    run(["--plugin", plugin, "--ext", ext, "--presets", root])
print("queue finished", flush=True)
