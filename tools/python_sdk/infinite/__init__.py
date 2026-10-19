"""infinite python sdk.

official python interface for the infinite c++20 modular audiovisual engine.
author: naman soni (github: n1m21n)
homepage: https://infinitedevices.org
license: mit
"""

import json
import os
import shutil
import subprocess
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

__version__ = "0.6.0"
__author__ = "Naman Soni (n1m21n)"
__homepage__ = "https://infinitedevices.org"
__credit__ = "Infinite, by Naman Soni (https://infinitedevices.org)"

ATTRIBUTION = {
    "engine": "infinite",
    "version": __version__,
    "author": __author__,
    "homepage": __homepage__,
    "credit": __credit__
}


def find_binary():
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


def validate(patch_path):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    r = subprocess.run([bin_path, "--validate", patch_path], capture_output=True, text=True)
    try:
        data = json.loads(r.stdout.strip())
        return {**data, **ATTRIBUTION}
    except Exception:
        return {"ok": r.returncode == 0, "raw": r.stdout, **ATTRIBUTION}


def render_frame(patch_path, out_png="/tmp/frame.png", time_sec=0.0):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    r = subprocess.run([bin_path, "--frame", patch_path, str(time_sec), out_png], capture_output=True, text=True)
    try:
        data = json.loads(r.stdout.strip())
        return {**data, "frame_path": out_png, **ATTRIBUTION}
    except Exception:
        return {"ok": r.returncode == 0, "frame_path": out_png, "raw": r.stdout, **ATTRIBUTION}


def synthesize_audio(patch_path, out_wav="/tmp/audio.wav", duration=5):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    summary_path = os.path.splitext(out_wav)[0] + "_summary.json"
    subprocess.run([
        bin_path, "--audio-summary", patch_path, summary_path,
        "--wav", out_wav, "--duration", str(duration)
    ], check=True, stdout=subprocess.DEVNULL)
    with open(summary_path) as f:
        data = json.load(f)
    return {"ok": True, **data, "audio_wav": out_wav, **ATTRIBUTION}


def render_video(glsl=None, sketch_js=None, out_mp4="/tmp/infinite.mp4", duration=5, fps=30, synth="wavetable"):
    bin_path = find_binary()
    ffmpeg = shutil.which("ffmpeg")
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")

    tmp_dir = tempfile.mkdtemp(prefix="infinite_py_")
    patch_path = os.path.join(tmp_dir, "patch.inf")
    raw_video = os.path.join(tmp_dir, "raw.mp4")
    raw_audio = os.path.join(tmp_dir, "audio.wav")
    summary = os.path.join(tmp_dir, "summary.json")

    synth_node = "Synths Wavetable" if synth == "wavetable" else "Synths FM"
    if sketch_js:
        esc = sketch_js.replace('\\', '\\\\').replace('\n', '\\n')
        patch = f"""infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 {synth_node}
  id synth
end
node 3 Utility Audio Out
  id speaker
end
node 4 Source Sketch
  id sk
  f width 720
  f height 720
  s code {esc}
end
node 5 Utility Output
  id out
end
note synth 0 notes
aud speaker 0 synth
cable out 0 sk
"""
    else:
        glsl_code = glsl or "p = (uv - 0.5) * aspect; r = length(p); col = vec3(0.5 + 0.5 * sin(r * 12.0 - t * 2.0));"
        patch = f"""infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 {synth_node}
  id synth
end
node 3 Utility Audio Out
  id speaker
end
node 4 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code {glsl_code}
end
node 5 Utility Output
  id out
end
note synth 0 notes
aud speaker 0 synth
cable out 0 visuals
"""

    with open(patch_path, "w") as f:
        f.write(patch)

    subprocess.run([
        bin_path, "--audio-summary", patch_path, summary,
        "--wav", raw_audio, "--duration", str(duration)
    ], check=True, stdout=subprocess.DEVNULL)

    subprocess.run([
        bin_path, "--render", patch_path, raw_video,
        "--duration", str(duration), "--fps", str(fps)
    ], check=True, stdout=subprocess.DEVNULL)

    os.makedirs(os.path.dirname(os.path.abspath(out_mp4)), exist_ok=True)
    if ffmpeg and os.path.exists(raw_audio) and os.path.exists(raw_video):
        subprocess.run([
            ffmpeg, "-y", "-i", raw_video, "-i", raw_audio,
            "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest",
            out_mp4
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        shutil.copy(raw_video, out_mp4)

def field_synth(code, out_wav="/tmp/field_synth.wav", duration=5):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
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
    tmp.write(patch.encode())
    tmp.close()
    res = synthesize_audio(tmp.name, out_wav, duration)
    os.remove(tmp.name)
    return res


def field_notes(code, synth_type="wavetable", out_wav="/tmp/field_notes.wav", duration=5):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    patch = f"""infinite-patch 1
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
    tmp.write(patch.encode())
    tmp.close()
    res = synthesize_audio(tmp.name, out_wav, duration)
    os.remove(tmp.name)
    return res


def sketch_3d(js_code, out_png="/tmp/sketch3d.png", time_sec=1.0):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = js_code.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
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
    tmp.write(patch.encode())
    tmp.close()
    res = render_frame(tmp.name, out_png, time_sec)
    os.remove(tmp.name)
    return res


def field_primitive(code, topology_int=0, out_png="/tmp/field_primitive.png", time_sec=1.0):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
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
    tmp.write(patch.encode())
    tmp.close()
    res = render_frame(tmp.name, out_png, time_sec)
    os.remove(tmp.name)
    return res


def field_effect(code, out_wav="/tmp/field_effect.wav", duration=4, synth_type="wavetable"):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    patch = f"""infinite-patch 1
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
    tmp.write(patch.encode())
    tmp.close()
    res = synthesize_audio(tmp.name, out_wav, duration)
    os.remove(tmp.name)
    return res


def field_modifier(mod_code, topology_int=0, out_png="/tmp/field_modifier.png", time_sec=1.0):
    bin_path = find_binary()
    if not bin_path:
        raise RuntimeError("Infinite binary not found.")
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc_mod = mod_code.replace('\\', '\\\\').replace('\n', '\\n')
    raw_prim = """x = (uv.x - 0.5) * 2.0\nz = (uv.y - 0.5) * 2.0\nP = vec3(x, 0.0, z)\nN = vec3(0.0, 1.0, 0.0)\nCd = vec3(0.8, 0.2, 0.5)\n"""
    esc_prim = raw_prim.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
node 1 3D Field Primitive
  id fprim
  i topology {topology_int}
  s code {esc_prim}
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
    tmp.write(patch.encode())
    tmp.close()
    res = render_frame(tmp.name, out_png, time_sec)
    os.remove(tmp.name)
    return res


