#!/usr/bin/env python3
"""Export Infinite Sample Generator library to Hugging Face AudioFolder format."""

import csv
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SAMPLES_JSON = os.path.join(ROOT, "website", "samples", "data.json")
OUT_DIR = os.path.join(HERE, "export")
CSV_PATH = os.path.join(OUT_DIR, "metadata.csv")

def main():
    if not os.path.exists(SAMPLES_JSON):
        print(f"Error: {SAMPLES_JSON} not found", file=sys.stderr)
        sys.exit(1)

    os.makedirs(OUT_DIR, exist_ok=True)
    with open(SAMPLES_JSON, "r", encoding="utf-8") as f:
        data = json.load(f)

    samples = data.get("samples", [])
    print(f"Loaded {len(samples)} samples from {SAMPLES_JSON}")

    fields = [
        "file_name", "sample_id", "name", "category", "style", "key",
        "bpm", "creator", "producer", "artist_portfolio", "spotify_url",
        "apple_music_url", "bandcamp_url", "engine", "license"
    ]

    count = 0
    with open(CSV_PATH, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(fields)

        for s in samples:
            audio_path = s.get("download") or s.get("audio")
            if not audio_path:
                continue

            writer.writerow([
                audio_path,
                s.get("id"),
                s.get("name"),
                s.get("part", "synth"),
                s.get("style", "modular"),
                s.get("key", "C"),
                s.get("bpm", 120),
                "Naman Soni (n1m21n)",
                "Naman (Electronic Music Producer)",
                "https://naman.infinitedevices.org",
                "https://open.spotify.com/artist/3gugf7oQFbrPsTpNT8EsBO",
                "https://music.apple.com/in/artist/naman/1705841663",
                "https://n1m21n.bandcamp.com/",
                "Infinite DAW (https://infinitedevices.org)",
                "CC-BY-4.0"
            ])
            count += 1

    print(f"Wrote {count} rows to {CSV_PATH}")

if __name__ == "__main__":
    main()
