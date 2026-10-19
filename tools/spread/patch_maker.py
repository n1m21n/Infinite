#!/usr/bin/env python3
"""infinite autonomous patch maker & audio/video exporter.

what: programmatically generates complete, valid, interconnected infinite .inf patches
      (synths, sequencers, modulation, shaders, audio effects), checks them with
      headless infinite, and exports high-quality wav, mp3, png, and mp4.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


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


PRESETS = {
    "ambient-synth": """infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 Synths Wavetable
  id synth
end
node 3 Utility Audio Out
  id speaker
end
node 4 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code p = (uv - 0.5) * aspect; r = length(p); a = atan2(p.y, p.x); col = vec3(0.5 + 0.5 * sin(r * 12.0 - t * 2.0 + a * 3.0)) * vec3(0.9, 0.5, 0.7);
end
node 5 Utility Output
  id out
end
note synth 0 notes
aud speaker 0 synth
cable out 0 visuals
""",

    "generative-acid": """infinite-patch 1
node 1 Notes Arpeggiator
  id arp
end
node 2 Synths FM
  id fm_synth
end
node 3 Utility Audio Out
  id speaker
end
node 4 Source FieldPixel
  id visuals
  f width 720
  f height 720
  s code p = (uv - 0.5) * 16.0; id = floor(p); f = fract(p) - 0.5; d = length(f); border = smoothstep(0.02, 0.06, abs(d - 0.4)); hue = sin(id.x * 1.5 + id.y * 2.3 + t); col = (0.5 + 0.5 * sin(hue + vec3(0.0, 1.0, 2.0))) * border;
end
node 5 Utility Output
  id out
end
note fm_synth 0 arp
aud speaker 0 fm_synth
cable out 0 visuals
"""
}


def make_patch(preset_name, out_dir, duration=6):
    os.makedirs(out_dir, exist_ok=True)
    patch_text = PRESETS.get(preset_name, PRESETS["ambient-synth"])
    patch_file = os.path.join(out_dir, f"{preset_name}.inf")

    with open(patch_file, "w") as f:
        f.write(patch_text)

    # layout nodes
    layout_script = os.path.join(ROOT, "tools", "patch-layout.py")
    if os.path.exists(layout_script):
        subprocess.run([sys.executable, layout_script, patch_file], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    infinite_bin = find_infinite()
    if not infinite_bin:
        print("[!] infinite binary not found.")
        return

    # 1. validate
    val = subprocess.run([infinite_bin, "--validate", patch_file], capture_output=True, text=True)
    if val.returncode != 0:
        print(f"[!] validation failed:\n{val.stdout}")
        return

    print(f"created and validated patch: {patch_file}")

    # 2. render audio (wav)
    wav_file = os.path.join(out_dir, f"{preset_name}.wav")
    summary_file = os.path.join(out_dir, f"{preset_name}_summary.json")
    print("synthesizing audio with infinite engine...")
    subprocess.run([
        infinite_bin, "--audio-summary", patch_file, summary_file,
        "--wav", wav_file, "--duration", str(duration)
    ], check=True)

    # 3. transcode to mp3 via ffmpeg if available
    ffmpeg = shutil.which("ffmpeg")
    mp3_file = os.path.join(out_dir, f"{preset_name}.mp3")
    if ffmpeg and os.path.exists(wav_file):
        subprocess.run([
            ffmpeg, "-y", "-i", wav_file, "-codec:a", "libmp3lame",
            "-b:a", "320k", mp3_file
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"exported high-bitrate mp3: {mp3_file}")

    # 4. render visual preview (png)
    print("rendering visual frame...")
    subprocess.run([
        infinite_bin, "--frame", patch_file, "0", out_dir
    ], check=True)

    # 5. render video (mp4)
    mp4_file = os.path.join(out_dir, f"{preset_name}.mp4")
    print(f"rendering {duration}s video with audio to mp4...")
    subprocess.run([
        infinite_bin, "--render", patch_file, mp4_file,
        "--duration", str(duration), "--fps", "30"
    ], check=True)

    print()
    print("completed generation:")
    print(f"patch: {patch_file}")
    print(f"audio wav: {wav_file}")
    if os.path.exists(mp3_file):
        print(f"audio mp3: {mp3_file}")
    print(f"video mp4: {mp4_file}")


def main():
    parser = argparse.ArgumentParser(description="infinite autonomous patch maker")
    parser.add_argument("--preset", default="ambient-synth", choices=list(PRESETS.keys()), help="preset name")
    parser.add_argument("--out-dir", default="dist/generated_patches", help="output directory")
    parser.add_argument("--duration", type=int, default=6, help="render duration in seconds")

    args = parser.parse_args()
    make_patch(args.preset, args.out_dir, args.duration)


if __name__ == "__main__":
    main()
