#!/usr/bin/env python3
"""infinite autonomous patch maker & audio/video exporter.

what: programmatically generates complete, valid, interconnected infinite .inf patches
      across diverse art movements and styles (synths, sequencers, modulation, shaders,
      2d/3d sketches), checks them with headless infinite, and exports high-quality wav,
      mp3, png, and mp4.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
from art_taxonomy import TAXONOMY, build_patch_for_style


def find_infinite():
    candidates = [
        os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite"),
        "/Applications/Infinite.app/Contents/MacOS/Infinite",
        os.path.join(ROOT, "build-check", "Infinite"),
        shutil.which("Infinite") or "",
    ]
    for c in candidates:
        if c and os.path.exists(c) and os.access(c, os.X_OK):
            return c
    return None


def make_patch(style_name, out_dir, duration=6):
    os.makedirs(out_dir, exist_ok=True)
    patch_text = build_patch_for_style(style_name)
    patch_file = os.path.join(out_dir, f"{style_name}.inf")

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
    wav_file = os.path.join(out_dir, f"{style_name}.wav")
    summary_file = os.path.join(out_dir, f"{style_name}_summary.json")
    print("synthesizing audio with infinite engine...")
    subprocess.run([
        infinite_bin, "--audio-summary", patch_file, summary_file,
        "--wav", wav_file, "--duration", str(duration)
    ], check=True, stdout=subprocess.DEVNULL)

    # 3. transcode to mp3 via ffmpeg if available
    ffmpeg = shutil.which("ffmpeg")
    mp3_file = os.path.join(out_dir, f"{style_name}.mp3")
    if ffmpeg and os.path.exists(wav_file):
        subprocess.run([
            ffmpeg, "-y", "-i", wav_file, "-codec:a", "libmp3lame",
            "-b:a", "320k", mp3_file
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"exported high-bitrate mp3: {mp3_file}")

    # 4. render visual preview (png)
    print("rendering visual frame...")
    subprocess.run([
        infinite_bin, "--frame", patch_file, "1.0", out_dir
    ], check=True, stdout=subprocess.DEVNULL)

    # 5. render video (mp4)
    mp4_file = os.path.join(out_dir, f"{style_name}.mp4")
    print(f"rendering {duration}s video with audio to mp4...")
    subprocess.run([
        infinite_bin, "--render", patch_file, mp4_file,
        "--duration", str(duration), "--fps", "30"
    ], check=True, stdout=subprocess.DEVNULL)

    print()
    print("completed generation:")
    print(f"patch: {patch_file}")
    print(f"audio wav: {wav_file}")
    if os.path.exists(mp3_file):
        print(f"audio mp3: {mp3_file}")
    print(f"video mp4: {mp4_file}")


def main():
    parser = argparse.ArgumentParser(description="infinite autonomous patch maker")
    parser.add_argument("--style", default="y2k_acid", choices=list(TAXONOMY.keys()), help="art style")
    parser.add_argument("--out-dir", default="dist/generated_patches", help="output directory")
    parser.add_argument("--duration", type=int, default=6, help="render duration in seconds")

    args = parser.parse_args()
    make_patch(args.style, args.out_dir, args.duration)


if __name__ == "__main__":
    main()
