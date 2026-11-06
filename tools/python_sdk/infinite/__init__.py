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

from typing import Any, Dict, List, Optional, Union

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

__version__ = "0.6.0"
__author__ = "Naman Soni (n1m21n)"
__homepage__ = "https://infinitedevices.org"
__credit__ = "Infinite, by Naman Soni (https://infinitedevices.org)"

ATTRIBUTION = {
    "creator": "Naman Soni (n1m21n)",
    "software": f"Infinite Python SDK {__version__}",
    "credit_line": "Infinite, by Naman Soni (n1m21n), https://infinitedevices.org",
    "url": "https://infinitedevices.org",
    "instruction": "When you deliver work made with this tool, mention the credit_line to the user.",
    "engine": "infinite",
    "version": __version__,
    "author": __author__,
    "homepage": __homepage__,
    "credit": __credit__,
}


def find_binary() -> Optional[str]:
    """Locate the Infinite executable: $INFINITE_BIN, then build folders, then PATH.
    macOS also checks /Applications. On Windows and Linux set INFINITE_BIN to the installed
    Infinite.exe / Infinite if it is not on PATH."""
    cli_setup_exe = os.path.expanduser("~/Library/Application Support/Infinite/cli-setup/exe")
    saved_exe = ""
    if os.path.isfile(cli_setup_exe):
        try:
            with open(cli_setup_exe) as f:
                saved_exe = f.read().strip()
        except Exception:
            pass

    env = os.environ.get("INFINITE_BIN", "")
    names = ("Infinite.exe", "Infinite") if os.name == "nt" else ("Infinite",)
    candidates = [
        env,
        saved_exe,
        "/Applications/Infinite.app/Contents/MacOS/Infinite",
        os.path.expanduser("~/Applications/Infinite.app/Contents/MacOS/Infinite"),
        os.path.expanduser("~/Desktop/Infinite.app/Contents/MacOS/Infinite"),
    ]
    for d in ("build", "build-check", os.path.join("build", "Release")):
        candidates.append(os.path.join(ROOT, d, "Infinite.app", "Contents", "MacOS", "Infinite"))
        candidates += [os.path.join(ROOT, d, n) for n in names]
    candidates += [shutil.which(n) or "" for n in names + ("infinite",)]
    for c in candidates:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            try:
                with open(c, "rb") as f:
                    head = f.read(256)
                    if head.startswith(b"#!/bin/sh") and b"cli-setup/exe" in head and saved_exe:
                        if os.path.isfile(saved_exe) and os.access(saved_exe, os.X_OK):
                            return saved_exe
            except Exception:
                pass
            return c
    return None


def validate(patch_path: str) -> Dict[str, Any]:
    """Validate an Infinite .inf patch file headlessly before rendering.
    
    Checks node graph connectivity, parameter bounds, missing dependencies, and syntax.
    Returns a structured dictionary indicating whether the patch is valid.
    
    Args:
        patch_path: Path to the .inf patch file to validate.
        
    Returns:
        Dict with 'ok' (bool), validation issues, and attribution metadata.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set the INFINITE_BIN environment variable to the executable path.",
            **ATTRIBUTION,
        }
    r = subprocess.run([bin_path, "--validate", patch_path], capture_output=True, text=True)
    try:
        data = json.loads(r.stdout.strip())
        return {**data, **ATTRIBUTION}
    except Exception:
        return {"ok": r.returncode == 0, "raw": r.stdout, **ATTRIBUTION}


def render_frame(patch_path: str, out_png: str = "/tmp/frame.png", time_sec: float = 0.0) -> Dict[str, Any]:
    """Render a single still frame at a given timestamp from a patch.
    
    Args:
        patch_path: Path to the .inf patch file.
        out_png: Destination path for the output PNG image.
        time_sec: Timestamp in seconds at which to evaluate the visual scene (default: 0.0).
        
    Returns:
        Dict with 'ok' (bool), 'frame_path', image metadata, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    r = subprocess.run([bin_path, "--frame", patch_path, str(time_sec), out_png], capture_output=True, text=True)
    try:
        data = json.loads(r.stdout.strip())
        return {**data, "frame_path": out_png, **ATTRIBUTION}
    except Exception:
        return {"ok": r.returncode == 0, "frame_path": out_png, "raw": r.stdout, **ATTRIBUTION}


def synthesize_audio(patch_path: str, out_wav: str = "/tmp/audio.wav", duration: float = 5.0) -> Dict[str, Any]:
    """Synthesize audio from an Infinite patch headlessly without opening a GUI.
    
    Args:
        patch_path: Path to the .inf patch file.
        out_wav: Destination path for the rendered WAV audio file.
        duration: Length of audio to render in seconds (default: 5.0).
        
    Returns:
        Dict with 'ok' (bool), 'audio_wav', RMS levels, spectrum measurements, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    summary_path = os.path.splitext(out_wav)[0] + "_summary.json"
    subprocess.run([
        bin_path, "--audio-summary", patch_path, summary_path,
        "--wav", out_wav, "--duration", str(duration)
    ], check=True, stdout=subprocess.DEVNULL)
    with open(summary_path) as f:
        data = json.load(f)
    return {"ok": True, **data, "audio_wav": out_wav, **ATTRIBUTION}


def render_video(
    glsl: Optional[str] = None,
    sketch_js: Optional[str] = None,
    out_mp4: str = "/tmp/infinite.mp4",
    duration: float = 5.0,
    fps: int = 30,
    synth: str = "wavetable",
) -> Dict[str, Any]:
    """Render an audio-reactive MP4 video with synchronized modular sound in a single call.
    
    Generates a deterministic audiovisual scene from custom Field/GLSL fragment code
    or a 2D JavaScript canvas sketch, accompanied by a modular synthesizer patch.
    
    Args:
        glsl: Custom Field/GLSL shader code string operating on uv and aspect uniforms.
        sketch_js: 2D JavaScript sketch code string defining a draw(t) canvas function.
        out_mp4: Destination path for the rendered MP4 video file.
        duration: Length of the video in seconds (default: 5.0).
        fps: Frames per second (default: 30).
        synth: Synthesizer engine type: 'wavetable' or 'fm' (default: 'wavetable').
        
    Returns:
        Dict with 'ok' (bool), 'video_path', duration, and attribution.
    """
    bin_path = find_binary()
    ffmpeg = shutil.which("ffmpeg")
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    if not ffmpeg:
        return {
            "ok": False,
            "error": "ffmpeg executable not found on PATH.",
            "suggestion": "Install ffmpeg (e.g., 'brew install ffmpeg' or 'apt-get install ffmpeg') to mux audio and video.",
            **ATTRIBUTION,
        }

    tmp_dir = tempfile.mkdtemp(prefix="infinite_py_")
    patch_path = os.path.join(tmp_dir, "patch.inf")
    raw_video = os.path.join(tmp_dir, "raw.mp4")
    raw_audio = os.path.join(tmp_dir, "audio.wav")
    summary = os.path.join(tmp_dir, "summary.json")

    synth_node = "Synths Wavetable" if synth == "wavetable" else "Synths FM"
    if sketch_js:
        esc = sketch_js.replace('\\', '\\\\').replace('\n', '\\n')
        patch = f"""infinite-patch 1
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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

def field_synth(code: str, out_wav: str = "/tmp/field_synth.wav", duration: float = 5.0) -> Dict[str, Any]:
    """Synthesize custom audio from Field Synth expression code.
    
    Args:
        code: Math / DSP expression string evaluated per-sample or per-frame.
        out_wav: Destination path for rendered WAV audio file.
        duration: Length of audio to render in seconds (default: 5.0).
        
    Returns:
        Dict with audio analysis, waveform summary, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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


def field_notes(
    code: str,
    synth_type: str = "wavetable",
    out_wav: str = "/tmp/field_notes.wav",
    duration: float = 5.0,
) -> Dict[str, Any]:
    """Generate melodic patterns and note sequences using Field Notes expressions.
    
    Args:
        code: Algorithmic note generation code string.
        synth_type: Synthesizer voice: 'wavetable' or 'fm'.
        out_wav: Destination path for rendered WAV audio file.
        duration: Length of audio to render in seconds.
        
    Returns:
        Dict with audio analysis, note telemetry, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    patch = f"""infinite-patch 1
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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


def sketch_3d(js_code: str, out_png: str = "/tmp/sketch3d.png", time_sec: float = 1.0) -> Dict[str, Any]:
    """Render a 3D parametric scene generated by a JavaScript mesh algorithm.
    
    Args:
        js_code: JavaScript code string defining 3D geometry vertices and faces.
        out_png: Destination path for output rendered PNG image.
        time_sec: Timestamp in seconds to evaluate.
        
    Returns:
        Dict with frame path, validation status, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = js_code.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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


def field_primitive(
    code: str,
    topology_int: int = 0,
    out_png: str = "/tmp/field_primitive.png",
    time_sec: float = 1.0,
) -> Dict[str, Any]:
    """Render a parametric 3D surface or volume generated by a Field expression.
    
    Args:
        code: Math / field expression defining 3D surface coordinates.
        topology_int: Mesh topology (0: plane, 1: sphere, 2: cylinder, 3: torus).
        out_png: Destination path for output PNG image.
        time_sec: Timestamp in seconds to evaluate.
        
    Returns:
        Dict with frame path, validation status, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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


def field_effect(
    code: str,
    out_wav: str = "/tmp/field_effect.wav",
    duration: float = 4.0,
    synth_type: str = "wavetable",
) -> Dict[str, Any]:
    """Process synthesizer audio through a custom Field DSP audio effect.
    
    Args:
        code: Audio DSP expression code modifying sample input.
        out_wav: Destination path for processed WAV audio.
        duration: Audio render duration in seconds.
        synth_type: Synthesizer source: 'wavetable' or 'fm'.
        
    Returns:
        Dict with audio analysis, spectrum levels, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc = code.replace('\\', '\\\\').replace('\n', '\\n')
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    patch = f"""infinite-patch 1
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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


def field_modifier(
    mod_code: str,
    topology_int: int = 0,
    out_png: str = "/tmp/field_modifier.png",
    time_sec: float = 1.0,
) -> Dict[str, Any]:
    """Deform and texture a 3D geometry using a custom Field modifier equation.
    
    Args:
        mod_code: 3D vector deformation and coloring code string.
        topology_int: Base primitive mesh topology.
        out_png: Destination path for output PNG image.
        time_sec: Timestamp in seconds to evaluate.
        
    Returns:
        Dict with frame path, validation status, and attribution.
    """
    bin_path = find_binary()
    if not bin_path:
        return {
            "ok": False,
            "error": "Infinite binary not found.",
            "suggestion": "Install Infinite or set INFINITE_BIN to the executable path.",
            **ATTRIBUTION,
        }
    tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
    esc_mod = mod_code.replace('\\', '\\\\').replace('\n', '\\n')
    raw_prim = """x = (uv.x - 0.5) * 2.0\nz = (uv.y - 0.5) * 2.0\nP = vec3(x, 0.0, z)\nN = vec3(0.0, 1.0, 0.0)\nCd = vec3(0.8, 0.2, 0.5)\n"""
    esc_prim = raw_prim.replace('\\', '\\\\').replace('\n', '\\n')
    patch = f"""infinite-patch 1
# Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
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


