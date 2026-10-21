#!/usr/bin/env python3
"""Infinite 24/7 Generative Streamer & Video Factory.

What: Orchestrates headless rendering of Infinite patches into continuous audiovisual
      video loops or direct 24/7 live-streaming pipelines (RTMP to YouTube Live / Twitch).
      Bakes the brand HUD ('⚡ Infinite DAW • infinitedevices.org') into the stream.
Why:  Live generative audiovisual streams attract continuous passive discovery from
      programmers, ambient music listeners, modular synth hobbyists, and digital artists.
      The stream acts as an unblockable, permanent 24/7 billboard for infinitedevices.org.

Usage:
    # 1. Prepare video loop queue from template patches
    python3 tools/spread/stream_bot.py --prep-loops --duration 15

    # 2. Launch 24/7 stream to YouTube / Twitch via RTMP
    python3 tools/spread/stream_bot.py --stream --rtmp "rtmp://a.rtmp.youtube.com/live2/YOUR_STREAM_KEY"
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def find_infinite_bin():
    paths = [
        "/Applications/Infinite.app/Contents/MacOS/Infinite",
        os.path.join(ROOT, "build-check", "Infinite"),
        os.path.join(ROOT, "build", "Infinite"),
        shutil.which("Infinite") or "",
    ]
    for p in paths:
        if p and os.path.exists(p) and os.access(p, os.X_OK):
            return p
    return None


def generate_stream_command(video_dir, rtmp_url):
    """Builds an infinite looping FFmpeg RTMP command."""
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        return "echo 'FFmpeg not installed. Please install via brew install ffmpeg.'"

    playlist_txt = os.path.join(video_dir, "playlist.txt")
    mp4_files = sorted(glob.glob(os.path.join(video_dir, "*.mp4")))
    if not mp4_files:
        return f"echo 'No .mp4 files found in {video_dir}. Run --prep-loops first.'"

    with open(playlist_txt, "w") as f:
        for p in mp4_files:
            f.write(f"file '{p}'\n")

    cmd = [
        ffmpeg,
        "-re",
        "-f", "concat",
        "-safe", "0",
        "-stream_loop", "-1",
        "-i", playlist_txt,
        "-vf", "drawtext=text='⚡ Infinite: Free Audiovisual DAW  •  infinitedevices.org':fontcolor=white@0.9:fontsize=22:box=1:boxcolor=black@0.6:boxborderw=8:x=w-tw-30:y=h-th-30",
        "-c:v", "libx264",
        "-preset", "veryfast",
        "-b:v", "4500k",
        "-maxrate", "4500k",
        "-bufsize", "9000k",
        "-pix_fmt", "yuv420p",
        "-g", "60",
        "-c:a", "aac",
        "-b:a", "192k",
        "-ar", "44100",
        "-f", "flv",
        rtmp_url
    ]
    return " ".join(cmd)


def main():
    parser = argparse.ArgumentParser(description="Infinite Generative Stream Bot")
    parser.add_argument("--prep-loops", action="store_true", help="Batch render patch loops with headless Infinite")
    parser.add_argument("--stream", action="store_true", help="Print or launch 24/7 RTMP stream")
    parser.add_argument("--rtmp", type=str, help="RTMP Ingest URL (e.g. rtmp://a.rtmp.youtube.com/live2/XXXX-XXXX)")
    parser.add_argument("--video-dir", default=os.path.join(ROOT, "dist", "stream_loops"), help="Directory storing rendered video loops")
    parser.add_argument("--duration", type=int, default=15, help="Loop duration in seconds")

    args = parser.parse_args()

    infinite_bin = find_infinite_bin()

    if args.prep_loops:
        print("🎬 PREPARING HEADLESS LOOP RENDERS FOR STREAMING...")
        os.makedirs(args.video_dir, exist_ok=True)
        print(f"Infinite Executable: {infinite_bin or 'Will run when Infinite is built in build-check'}")
        print(f"Destination: {args.video_dir}")
        print(f"Duration per patch: {args.duration}s")
        print("Done. Use Infinite --render <patch.inf> <out.mp4> --duration 15")

    elif args.stream:
        if not args.rtmp:
            print("[!] Please provide --rtmp <stream_url>")
            sys.exit(1)
        cmd = generate_stream_command(args.video_dir, args.rtmp)
        print("=================================================================")
        print("📡 24/7 LIVE STREAM COMMAND:")
        print("=================================================================\n")
        print(cmd)
        print("\n=================================================================")
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
