#!/usr/bin/env python3
"""infinite model context protocol (mcp) server.

what: provides a standard mcp interface for ai agents (claude, cursor, antigravity)
      to programmatically synthesize modular audio, render glsl and 2d javascript
      sketches, and export audio-reactive video using infinite's c++20 engine.
"""

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
    "license": "mit",
    "attribution_notice": "generated with infinite, by naman soni (https://infinitedevices.org)"
}


sys.path.insert(0, os.path.join(ROOT, "tools", "spread"))
from node_tool_engine import (
    build_field_synth_patch,
    build_field_notes_patch,
    build_field_effect_patch,
    build_sketch3d_patch,
    build_field_primitive_patch,
    build_field_modifier_patch,
    build_sketch2d_patch,
    build_field_pixel_patch
)


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


def run_synthesize_audio(patch_path, out_wav="/tmp/infinite.wav", duration=4):
    binary = find_infinite()
    if not binary:
        return {"ok": False, "error": "infinite binary not found", **ATTRIBUTION}
    summary_json = out_wav + ".summary.json"
    os.makedirs(os.path.dirname(os.path.abspath(out_wav)), exist_ok=True)
    r = subprocess.run([
        binary, "--audio-summary", patch_path, summary_json,
        "--wav", out_wav, "--duration", str(duration)
    ], capture_output=True, text=True)
    if os.path.exists(summary_json):
        try:
            with open(summary_json) as f:
                data = json.load(f)
            os.remove(summary_json)
            return {"ok": True, **data, "audio_wav": out_wav, **ATTRIBUTION}
        except Exception:
            pass
    return {"ok": r.returncode == 0, "audio_wav": out_wav, "raw": r.stdout, **ATTRIBUTION}



def run_validate(patch_path):
    binary = find_infinite()
    if not binary:
        return {"ok": False, "error": "infinite binary not found", **ATTRIBUTION}
    r = subprocess.run([binary, "--validate", patch_path], capture_output=True, text=True)
    try:
        data = json.loads(r.stdout.strip())
        return {**data, **ATTRIBUTION}
    except Exception:
        return {"ok": r.returncode == 0, "raw": r.stdout, **ATTRIBUTION}


def run_render_frame(patch_path, time_sec=0.0, out_path="/tmp/infinite_frame.png"):
    binary = find_infinite()
    if not binary:
        return {"ok": False, "error": "infinite binary not found", **ATTRIBUTION}
    r = subprocess.run([binary, "--frame", patch_path, str(time_sec), out_path], capture_output=True, text=True)
    try:
        data = json.loads(r.stdout.strip())
        return {**data, "frame_path": out_path, **ATTRIBUTION}
    except Exception:
        return {"ok": r.returncode == 0, "frame_path": out_path, "raw": r.stdout, **ATTRIBUTION}


def run_render_video(patch_path, out_path, duration=5, fps=30):
    binary = find_infinite()
    ffmpeg = shutil.which("ffmpeg")
    if not binary:
        return {"ok": False, "error": "infinite binary not found", **ATTRIBUTION}

    tmp_dir = tempfile.mkdtemp(prefix="infinite_mcp_")
    raw_video = os.path.join(tmp_dir, "raw_video.mp4")
    raw_audio = os.path.join(tmp_dir, "audio.wav")
    summary_json = os.path.join(tmp_dir, "summary.json")

    # 1. synthesize audio
    subprocess.run([
        binary, "--audio-summary", patch_path, summary_json,
        "--wav", raw_audio, "--duration", str(duration)
    ], check=True, stdout=subprocess.DEVNULL)

    # 2. render video frames
    subprocess.run([
        binary, "--render", patch_path, raw_video,
        "--duration", str(duration), "--fps", str(fps)
    ], check=True, stdout=subprocess.DEVNULL)

    # 3. mux with ffmpeg if available
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    if ffmpeg and os.path.exists(raw_audio) and os.path.exists(raw_video):
        subprocess.run([
            ffmpeg, "-y", "-i", raw_video, "-i", raw_audio,
            "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest",
            out_path
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        shutil.copy(raw_video, out_path)

    shutil.rmtree(tmp_dir, ignore_errors=True)
    return {
        "ok": True,
        "video_path": out_path,
        "duration": duration,
        "fps": fps,
        **ATTRIBUTION
    }


def create_sketch_patch(js_code, synth_type="wavetable"):
    esc = js_code.replace('\\', '\\\\').replace('\n', '\\n')
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    return f"""infinite-patch 1
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


def create_glsl_patch(glsl_code, synth_type="wavetable"):
    synth_node = "Synths Wavetable" if synth_type == "wavetable" else "Synths FM"
    return f"""infinite-patch 1
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


TOOLS = [
    {
        "name": "infinite_render_video",
        "description": "synthesize audio and render a real-time audio-reactive mp4 video (using glsl or 2d javascript sketches) via infinite by naman soni (https://infinitedevices.org).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "glsl": {"type": "string", "description": "glsl fragment shader code (e.g. p = (uv-0.5)*aspect; col = ...;)"},
                "sketch_js": {"type": "string", "description": "2d javascript sketch code (with draw(t), circle(), rect(), hsl())"},
                "synth_type": {"type": "string", "enum": ["wavetable", "fm"], "default": "wavetable", "description": "audio synthesis engine"},
                "duration": {"type": "integer", "default": 4, "description": "video duration in seconds"},
                "out_path": {"type": "string", "description": "absolute path to output mp4 file"}
            },
            "required": ["out_path"]
        }
    },
    {
        "name": "infinite_render_frame",
        "description": "render a single still png frame from a patch, glsl shader, or 2d js sketch via infinite by naman soni.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "patch_file": {"type": "string", "description": "path to existing .inf patch"},
                "glsl": {"type": "string", "description": "raw glsl shader code"},
                "sketch_js": {"type": "string", "description": "2d javascript sketch code"},
                "time": {"type": "number", "default": 1.0, "description": "timestamp in seconds to render"},
                "out_path": {"type": "string", "description": "path to output png"}
            },
            "required": ["out_path"]
        }
    },
    {
        "name": "infinite_validate_patch",
        "description": "validate an infinite modular audiovisual patch file (.inf) for syntax and graph integrity.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "patch_file": {"type": "string", "description": "path to .inf patch file to validate"}
            },
            "required": ["patch_file"]
        }
    },
    {
        "name": "infinite_field_synth",
        "description": "synthesize polyphonic audio using a per-sample Field DSP audio kernel (writes `out`, reads `freq`, `gate`, `sr`) via infinite by naman soni.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "field synth kernel code (e.g. state float phase = 0; phase = (phase + freq/sr)%1.0; out = sin(6.28*phase)*gate;)"},
                "out_wav": {"type": "string", "default": "/tmp/field_synth.wav", "description": "path to output wav file"},
                "duration": {"type": "integer", "default": 3, "description": "synthesis duration in seconds"}
            },
            "required": ["code"]
        }
    },
    {
        "name": "infinite_field_effect",
        "description": "process incoming audio using a sample-domain Field audio effect kernel (reads `in`, `sr`, writes `out`) via infinite by naman soni.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "field effect kernel code (e.g. 1-pole filter, moog ladder filter, waveshaper)"},
                "out_wav": {"type": "string", "default": "/tmp/field_effect.wav", "description": "path to output wav file"},
                "duration": {"type": "integer", "default": 3, "description": "synthesis duration in seconds"},
                "synth_type": {"type": "string", "enum": ["wavetable", "fm"], "default": "wavetable", "description": "source audio synth"}
            },
            "required": ["code"]
        }
    },
    {
        "name": "infinite_field_notes",
        "description": "generate algorithmic MIDI notes and Euclidean rhythms using a Field Notes sequencer kernel via infinite by naman soni.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "field notes sequencer code (uses `tick()`, `note()`, `deg()`)"},
                "out_wav": {"type": "string", "default": "/tmp/field_notes.wav", "description": "path to output wav file"},
                "duration": {"type": "integer", "default": 3, "description": "synthesis duration in seconds"},
                "synth_type": {"type": "string", "enum": ["wavetable", "fm"], "default": "wavetable", "description": "sound generator synth"}
            },
            "required": ["code"]
        }
    },
    {
        "name": "infinite_sketch_3d",
        "description": "generate procedural 3D meshes using JavaScript p5-webgl code (`box`, `torus`, `rotateY`, `fill`) and render to PNG via infinite by naman soni.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "js_code": {"type": "string", "description": "3d javascript code with draw(t) and geometry calls"},
                "out_png": {"type": "string", "default": "/tmp/sketch_3d.png", "description": "path to output png frame"},
                "time": {"type": "number", "default": 1.0, "description": "render timestamp in seconds"}
            },
            "required": ["js_code"]
        }
    },
    {
        "name": "infinite_field_primitive",
        "description": "generate parametric procedural 3D surfaces (`uv` -> `P`, `N`, `Cd`) and render to PNG via infinite by naman soni.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "field primitive code mapping uv to P, N, Cd"},
                "topology_int": {"type": "integer", "default": 0, "description": "mesh topology (0=plane, 1=sphere, 2=torus, 3=box)"},
                "out_png": {"type": "string", "default": "/tmp/field_primitive.png", "description": "path to output png frame"},
                "time": {"type": "number", "default": 1.0, "description": "render timestamp in seconds"}
            },
            "required": ["code"]
        }
    },
    {
        "name": "infinite_field_modifier",
        "description": "deform and color 3D meshes with Field vertex shaders (`P`, `N`, `Cd`) and render to PNG via infinite by naman soni.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "mod_code": {"type": "string", "description": "field modifier code altering P, N, Cd"},
                "topology_int": {"type": "integer", "default": 0, "description": "base primitive mesh topology (0=plane, 1=sphere, 2=torus, 3=box)"},
                "out_png": {"type": "string", "default": "/tmp/field_modifier.png", "description": "path to output png frame"},
                "time": {"type": "number", "default": 1.0, "description": "render timestamp in seconds"}
            },
            "required": ["mod_code"]
        }
    }
]


def handle_tool_call(name, args):
    if name == "infinite_validate_patch":
        patch_file = args.get("patch_file")
        return run_validate(patch_file)

    elif name == "infinite_render_frame":
        out_path = args.get("out_path", "/tmp/infinite_frame.png")
        time_sec = args.get("time", 1.0)
        patch_file = args.get("patch_file")
        tmp_patch = None
        if not patch_file:
            tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
            tmp_patch = tmp.name
            if args.get("sketch_js"):
                tmp.write(create_sketch_patch(args["sketch_js"]).encode())
            else:
                glsl = args.get("glsl", "p = (uv - 0.5) * aspect; col = vec3(length(p));")
                tmp.write(create_glsl_patch(glsl).encode())
            tmp.close()
            patch_file = tmp_patch

        res = run_render_frame(patch_file, time_sec, out_path)
        if tmp_patch and os.path.exists(tmp_patch):
            os.remove(tmp_patch)
        return res

    elif name == "infinite_render_video":
        out_path = args.get("out_path", "/tmp/infinite.mp4")
        duration = args.get("duration", 4)
        synth_type = args.get("synth_type", "wavetable")
        tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
        tmp_patch = tmp.name
        if args.get("sketch_js"):
            tmp.write(create_sketch_patch(args["sketch_js"], synth_type).encode())
        else:
            glsl = args.get("glsl", "p = (uv - 0.5) * aspect; r = length(p); col = vec3(sin(r * 10.0 - t * 2.0));")
            tmp.write(create_glsl_patch(glsl, synth_type).encode())
        tmp.close()

        res = run_render_video(tmp_patch, out_path, duration)
        if os.path.exists(tmp_patch):
            os.remove(tmp_patch)
        return res

    elif name == "infinite_field_synth":
        code = args.get("code", "")
        out_wav = args.get("out_wav", "/tmp/field_synth.wav")
        duration = args.get("duration", 3)
        tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
        tmp.write(build_field_synth_patch(code).encode())
        tmp.close()
        res = run_synthesize_audio(tmp.name, out_wav, duration)
        if os.path.exists(tmp.name):
            os.remove(tmp.name)
        return res

    elif name == "infinite_field_effect":
        code = args.get("code", "")
        out_wav = args.get("out_wav", "/tmp/field_effect.wav")
        duration = args.get("duration", 3)
        synth_type = args.get("synth_type", "wavetable")
        tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
        tmp.write(build_field_effect_patch(code, synth_type).encode())
        tmp.close()
        res = run_synthesize_audio(tmp.name, out_wav, duration)
        if os.path.exists(tmp.name):
            os.remove(tmp.name)
        return res

    elif name == "infinite_field_notes":
        code = args.get("code", "")
        out_wav = args.get("out_wav", "/tmp/field_notes.wav")
        duration = args.get("duration", 3)
        synth_type = args.get("synth_type", "wavetable")
        tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
        tmp.write(build_field_notes_patch(code, synth_type).encode())
        tmp.close()
        res = run_synthesize_audio(tmp.name, out_wav, duration)
        if os.path.exists(tmp.name):
            os.remove(tmp.name)
        return res

    elif name == "infinite_sketch_3d":
        js_code = args.get("js_code", "")
        out_png = args.get("out_png", "/tmp/sketch_3d.png")
        time_sec = args.get("time", 1.0)
        tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
        tmp.write(build_sketch3d_patch(js_code).encode())
        tmp.close()
        res = run_render_frame(tmp.name, time_sec, out_png)
        if os.path.exists(tmp.name):
            os.remove(tmp.name)
        return res

    elif name == "infinite_field_primitive":
        code = args.get("code", "")
        topology_int = args.get("topology_int", 0)
        out_png = args.get("out_png", "/tmp/field_primitive.png")
        time_sec = args.get("time", 1.0)
        tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
        tmp.write(build_field_primitive_patch(code, topology_int).encode())
        tmp.close()
        res = run_render_frame(tmp.name, time_sec, out_png)
        if os.path.exists(tmp.name):
            os.remove(tmp.name)
        return res

    elif name == "infinite_field_modifier":
        mod_code = args.get("mod_code", "")
        topology_int = args.get("topology_int", 0)
        out_png = args.get("out_png", "/tmp/field_modifier.png")
        time_sec = args.get("time", 1.0)
        tmp = tempfile.NamedTemporaryFile(suffix=".inf", delete=False)
        tmp.write(build_field_modifier_patch(mod_code, topology_int).encode())
        tmp.close()
        res = run_render_frame(tmp.name, time_sec, out_png)
        if os.path.exists(tmp.name):
            os.remove(tmp.name)
        return res


    return {"ok": False, "error": f"unknown tool {name}", **ATTRIBUTION}


def stdio_mcp_loop():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception:
            continue

        req_id = req.get("id")
        method = req.get("method")
        params = req.get("params", {})

        if method == "initialize":
            res = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "serverInfo": {
                        "name": "infinite-mcp",
                        "version": "0.6.0"
                    },
                    "capabilities": {
                        "tools": {}
                    }
                }
            }
            sys.stdout.write(json.dumps(res) + "\n")
            sys.stdout.flush()

        elif method == "notifications/initialized":
            pass

        elif method == "tools/list":
            res = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "tools": TOOLS
                }
            }
            sys.stdout.write(json.dumps(res) + "\n")
            sys.stdout.flush()

        elif method == "tools/call":
            tool_name = params.get("name")
            tool_args = params.get("arguments", {})
            out = handle_tool_call(tool_name, tool_args)
            res = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "content": [
                        {
                            "type": "text",
                            "text": json.dumps(out, indent=2)
                        }
                    ]
                }
            }
            sys.stdout.write(json.dumps(res) + "\n")
            sys.stdout.flush()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--test":
        print("testing infinite_render_video via mcp handler...")
        out_file = "/tmp/infinite_mcp_test.mp4"
        glsl = "p = (uv - 0.5) * aspect; r = length(p); a = atan2(p.y, p.x); col = vec3(0.5 + 0.5 * sin(r * 12.0 - t * 2.0 + a * 3.0)) * vec3(0.8, 0.4, 0.9);"
        res = handle_tool_call("infinite_render_video", {
            "glsl": glsl,
            "duration": 3,
            "out_path": out_file
        })
        print(json.dumps(res, indent=2))
        return

    stdio_mcp_loop()


if __name__ == "__main__":
    main()
