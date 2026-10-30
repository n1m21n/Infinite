#!/usr/bin/env python3
"""Smoke test for the headless side of the `infinite` command. Usage: terminal_cli_smoke.py <binary>
Live verbs need a running app and are checked by hand or by scripts/live_rpc_smoke.py's server."""
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

binary = sys.argv[1]
work = Path(tempfile.mkdtemp(prefix="infinite-cli-"))
failures = []


def cli(*args):
    return subprocess.run([binary, "--cli", *args], capture_output=True, text=True, timeout=180)


def check(name, ok, detail=""):
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else f": {detail}"))
    if not ok:
        failures.append(name)


cli("help")  # the first start of a new binary pays for the OS checking it
started = time.time()
run = cli("help")
check("help is fast", run.returncode == 0 and time.time() - started < 0.5, f"{time.time() - started:.3f}s")

broken = {
    "sketch": ("a.sketch.js", "function draw(t) {\n  circle(;\n}\n", "E_SKETCH_COMPILE", 2),
    "sketch3d": ("a.sketch3d.js", "function draw(t) {\n  box(;\n}\n", "E_SKETCH_COMPILE", 2),
    "shadertoy": ("a.glsl", "void mainImage(out vec4 c, in vec2 p) {\n  c = nope;\n}\n", "E_GLSL_COMPILE", 2),
    "field-pixel": ("a.field", "col = nope(\n", "E_FIELD_COMPILE", None),
}
for kind, (name, code, expected, line) in broken.items():
    good = work / ("good-" + name)
    made = cli("code", "new", kind, str(good))
    run = cli("code", "check", str(good))
    check(f"{kind} starter compiles", made.returncode == 0 and run.returncode == 0, run.stdout + run.stderr)
    bad = work / name
    bad.write_text(code)
    run = cli("code", "check", str(bad))
    reply = json.loads(run.stdout) if run.stdout.strip() else {}
    errors = reply.get("errors", [])
    check(f"{kind} broken is caught", run.returncode == 3 and errors and errors[0]["code"] == expected, run.stdout + run.stderr)
    if line is not None and errors:
        check(f"{kind} names the line", errors[0]["line"] == line, errors[0])

for kind in ("sketch", "sketch3d", "field-pixel", "field-synth", "field-modifier", "formula"):
    listed = cli("code", "examples", kind).stdout.strip().splitlines()
    check(f"{kind} examples listed", len(listed) > 0)

for language in ("field", "sketch", "sketch3d", "glsl", "expressions", "patch"):
    check(f"docs {language}", len(cli("code", "docs", language).stdout) > 200)

patch = work / "p.inf"
run = cli("patch", "new", "--recipe", "first-image", str(patch))
check("patch new", run.returncode == 0, run.stderr)
run = cli("patch", "layout", str(patch))
check("patch layout", run.returncode == 0, run.stdout + run.stderr)
run = cli("patch", "check", str(patch))
reply = json.loads(run.stdout.strip().splitlines()[-1])
check("patch check", reply.get("ok") is True, run.stdout[-600:])
run = cli("patch", "check", str(patch), "--want", "moving-picture")
reply = json.loads(run.stdout.strip().splitlines()[-1])
check("patch check fails a still picture", reply.get("ok") is False, run.stdout[-300:])

code = work / "new.sketch.js"
cli("code", "new", "sketch", str(code))
synth = work / "s.inf"
synth.write_text('infinite-patch 1\nnode 3 Source Sketch\n  s code x\nend\n')
run = cli("code", "push", str(code), "--node", "3", "--patch", str(synth))
pulled = cli("code", "pull", "3", "--patch", str(synth)).stdout
check("push and pull a patch file", run.returncode == 0 and pulled == code.read_text(), run.stdout + pulled)

print("FAILED: " + ", ".join(failures) if failures else "all passed")
sys.exit(1 if failures else 0)
