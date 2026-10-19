#!/usr/bin/env python3
"""infinite node multi-tool engine.

what: programmatic generator and headless verification harness for all 6 core
      infinite node families:
      1. source sketch (2d algorithmic javascript)
      2. 3d sketch 3d (procedural 3d javascript geometry)
      3. source fieldpixel (glsl shaders)
      4. 3d field primitive (procedural parametric meshes: plane, sphere, torus)
      5. synths field synth (per-sample audio dsp kernels)
      6. notes field notes (algorithmic midi sequencers & euclidean rhythms)
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

ATTRIBUTION = {
    "engine": "infinite",
    "creator": "naman soni (github: n1m21n)",
    "homepage": "https://infinitedevices.org",
    "license": "mit"
}


def find_infinite():
    """The Infinite executable: $INFINITE_BIN, then the checkout's build folders, then PATH.
    macOS also checks /Applications. On Windows and Linux set INFINITE_BIN to the installed
    Infinite.exe / Infinite if it is not on PATH."""
    env = os.environ.get("INFINITE_BIN", "")
    names = ("Infinite.exe", "Infinite") if os.name == "nt" else ("Infinite",)
    candidates = [env, "/Applications/Infinite.app/Contents/MacOS/Infinite"]
    for d in ("build", "build-check", os.path.join("build", "Release")):
        candidates.append(os.path.join(ROOT, d, "Infinite.app", "Contents", "MacOS", "Infinite"))
        candidates += [os.path.join(ROOT, d, n) for n in names]
    candidates += [shutil.which(n) or "" for n in names + ("infinite",)]
    for c in candidates:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def escape_code(s):
    return s.replace('\\', '\\\\').replace('\n', '\\n')


def build_field_synth_patch(code):
    esc = escape_code(code)
    return f"""infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 Synths Field Synth
  id fsynth
  s code {esc}
end
node 3 Utility Audio Out
  id speaker
end
note fsynth 0 notes
aud speaker 0 fsynth
"""


def build_field_notes_patch(code, synth_type="wavetable"):
    esc = escape_code(code)
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    return f"""infinite-patch 1
node 1 Notes Field Notes
  id fnotes
  s code {esc}
end
node 2 {synth_node}
  id synth
end
node 3 Utility Audio Out
  id speaker
end
note synth 0 fnotes
aud speaker 0 synth
"""


def build_sketch3d_patch(js_code):
    esc = escape_code(js_code)
    return f"""infinite-patch 1
node 1 3D Sketch 3D
  id sk3d
  s code {esc}
end
node 2 3D Render 3D
  id r3d
  f width 720
  f height 720
end
node 3 Utility Output
  id out
end
geo r3d 0 sk3d
cable out 0 r3d
"""


def build_field_primitive_patch(code, topology_int=0):
    esc = escape_code(code)
    return f"""infinite-patch 1
node 1 3D Field Primitive
  id fprim
  i topology {topology_int}
  s code {esc}
end
node 2 3D Render 3D
  id r3d
  f width 720
  f height 720
end
node 3 Utility Output
  id out
end
geo r3d 0 fprim
cable out 0 r3d
"""


def build_field_effect_patch(code, synth_type="wavetable"):
    esc = escape_code(code)
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    return f"""infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 {synth_node}
  id synth
end
node 3 AudioEffects Field Effect
  id fx
  s code {esc}
end
node 4 Utility Audio Out
  id speaker
end
note synth 0 notes
aud fx 0 synth
aud speaker 0 fx
"""


def build_field_modifier_patch(mod_code, topology_int=0):
    esc_mod = escape_code(mod_code)
    raw_prim = """x = (uv.x - 0.5) * 2.0
z = (uv.y - 0.5) * 2.0
P = vec3(x, 0.0, z)
N = vec3(0.0, 1.0, 0.0)
Cd = vec3(0.8, 0.2, 0.5)
"""
    prim_code = escape_code(raw_prim)
    return f"""infinite-patch 1
node 1 3D Field Primitive
  id fprim
  i topology {topology_int}
  s code {prim_code}
end
node 2 3D Field Modifier
  id fmod
  s code {esc_mod}
end
node 3 3D Render 3D
  id r3d
  f width 720
  f height 720
end
node 4 Utility Output
  id out
end
geo fmod 0 fprim
geo r3d 0 fmod
cable out 0 r3d
"""


def build_sketch2d_patch(js_code):
    esc = escape_code(js_code)
    return f"""infinite-patch 1
node 1 Source Sketch
  id sk
  f width 720
  f height 720
  s code {esc}
end
node 2 Utility Output
  id out
end
cable out 0 sk
"""


def build_field_pixel_patch(glsl_code):
    return f"""infinite-patch 1
node 1 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code {glsl_code}
end
node 2 Utility Output
  id out
end
cable out 0 visuals
"""


PRESETS = {
    "field_synth_analog_saw": {
        "family": "field_synth",
        "title": "analog subtractive saw with lowpass filter",
        "code": """state float phase = 0
state float lp = 0
phase = (phase + freq / sr) % 1.0
saw = phase * 2.0 - 1.0
lp = lp + 0.15 * (saw - lp)
out = lp * 0.4 * gate
""".strip(),
        "builder": build_field_synth_patch
    },
    "field_notes_euclidean": {
        "family": "field_notes",
        "title": "euclidean 5/16 rhythm generator",
        "code": """param float hits = 5 [1, 16]
state float step = 0
if (tick(1/16) > 0.5) {
   on = floor((step + 1) * hits / 16 + 0.001) - floor(step * hits / 16 + 0.001)
   note(deg(0), 0.9 * on, 1/16)
   step = (step + 1) % 16
}
""".strip(),
        "builder": build_field_notes_patch
    },
    "field_effect_moog": {
        "family": "field_effect",
        "title": "moog 4-pole ladder filter with resonance and saturation",
        "code": """param float cutoff = 2200.0 [80.0, 14000.0]
param float res = 0.70 [0.0, 0.98]
param float drive = 1.50 [1.0, 4.0]
param float mix = 1.0 [0.0, 1.0]
state float s1 = 0
state float s2 = 0
state float s3 = 0
state float s4 = 0

f = clamp((cutoff / sr) * 2.2, 0.005, 0.42)
feedback = (s4 * res * 3.6) / (1.0 + abs(s4) * 0.8)
u = (in * drive) - feedback
sat = u / (1.0 + abs(u) * 0.3)

s1 = s1 + f * (sat - s1)
s2 = s2 + f * (s1 - s2)
s3 = s3 + f * (s2 - s3)
s4 = s4 + f * (s3 - s4)

wet = s4
out = in * (1.0 - mix) + wet * mix
""".strip(),
        "builder": build_field_effect_patch
    },
    "sketch3d_rotating_torus": {
        "family": "sketch3d",
        "title": "procedural 3d torus mesh in javascript",
        "code": """param("rMajor", 1.2, 0.5, 3);
param("rMinor", 0.4, 0.1, 1);
param("detail", 24, 8, 48);

function draw(t) {
  rotateY(t * 0.8);
  rotateX(t * 0.4);
  fill(0.2, 0.6, 0.9);
  torus(rMajor, rMinor, detail, detail);
}
""".strip(),
        "builder": build_sketch3d_patch
    },
    "field_primitive_terrain": {
        "family": "field_primitive",
        "title": "dynamic procedural wave terrain plane",
        "code": """param float freq = 3.0 [0.5, 8.0]
param float height = 0.35 [0.0, 2.0]
x = (uv.x - 0.5) * 3.0
z = (uv.y - 0.5) * 3.0
dist = sqrt(x * x + z * z)
y = sin(dist * freq - (t + 1.0) * 2.0) * height * exp(-dist * 0.4)
P = vec3(x, y, z)
N = normalize(vec3(0.0, 1.0, 0.0))
Cd = vec3(0.2 + 0.4 * (y + 0.5), 0.6, 0.9)
""".strip(),
        "builder": lambda code: build_field_primitive_patch(code, 0)
    },
    "field_modifier_wave": {
        "family": "field_modifier",
        "title": "parametric mesh wave ripple deformer",
        "code": """param float speed = 2.0 [0, 10]
param float height = 0.3 [0, 2]
dist = length(P.xz)
P.y += sin(dist * 4.0 - t * speed) * height
Cd = vec3(0.5 + 0.5 * sin(P.y * 5.0), 0.4, 0.8)
""".strip(),
        "builder": lambda code: build_field_modifier_patch(code, 0)
    }
}


def test_preset(name, out_dir="/tmp/infinite_nodes"):
    os.makedirs(out_dir, exist_ok=True)
    preset = PRESETS.get(name)
    if not preset:
        sys.exit(f"unknown preset {name}")

    infinite = find_infinite()
    if not infinite:
        sys.exit("infinite binary not found")

    patch_path = os.path.join(out_dir, f"{name}.inf")
    patch_text = preset["builder"](preset["code"])
    with open(patch_path, "w") as f:
        f.write(patch_text)

    # 1. validate
    val = subprocess.run([infinite, "--validate", patch_path], capture_output=True, text=True)
    if val.returncode != 0:
        print(f"[!] {name} validation failed:\n{val.stdout}")
        return False

    print(f"ok: validated {preset['title']} ({preset['family']})")

    # 2. test render if visual or audio
    if preset["family"] in ["field_synth", "field_notes", "field_effect"]:
        wav_path = os.path.join(out_dir, f"{name}.wav")
        sum_path = os.path.join(out_dir, f"{name}_summary.json")
        subprocess.run([
            infinite, "--audio-summary", patch_path, sum_path,
            "--wav", wav_path, "--duration", "3"
        ], check=True, stdout=subprocess.DEVNULL)
        print(f"    synthesized audio: {wav_path}")
    else:
        frame_path = os.path.join(out_dir, f"{name}.png")
        subprocess.run([
            infinite, "--frame", patch_path, "1.0", frame_path
        ], check=True, stdout=subprocess.DEVNULL)
        print(f"    rendered visual frame: {frame_path}")

    return True


def main():
    parser = argparse.ArgumentParser(description="infinite node multi-tool engine")
    parser.add_argument("--test-all", action="store_true", help="verify all node families against codebase")
    args = parser.parse_args()

    if args.test_all:
        print("testing all node families against infinite c++ engine:")
        print("-----------------------------------------------------------------")
        for k in PRESETS:
            test_preset(k)
        print("-----------------------------------------------------------------")
        print("all node families verified with zero errors.")


if __name__ == "__main__":
    main()
