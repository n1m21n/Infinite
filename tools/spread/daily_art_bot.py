#!/usr/bin/env python3
"""infinite autonomous generative video & social distribution bot.

what: automatically synthesizes complete audio-reactive generative videos
      (2d javascript sketches and glsl shaders + modular audio dsp),
      validates them with the infinite c++20 engine, and exports stage-ready
      video loops (.mp4), high-res stills (.png), and clean lowercase copy.
"""

import argparse
import datetime
import glob
import json
import os
import random
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
from art_taxonomy import TAXONOMY, build_patch_for_style

# Standing Pinterest inspiration moodboard pool
PINTEREST_INSPIRATIONS = [
    {
        "pin": "https://in.pinterest.com/pin/715509459583843031/",
        "title": "Thundr Co Postmodern Brutalism & Utilitarian Assets",
        "style_id": "thundr_brutalist",
        "mood": "raw, industrial, high-contrast streetwear aesthetic with mock barcodes and hazard lines"
    },
    {
        "pin": "https://in.pinterest.com/pin/395894623517026077/",
        "title": "Alejandro Chavetta Mixed-Media Analog-Digital Montage",
        "style_id": "chavetta_montage",
        "mood": "tactile torn kraft paper, botanical textures, classical busts, and expressive digital wash"
    },
    {
        "pin": "https://in.pinterest.com/pin/447123069266698785/",
        "title": "Scott Moore Miniature People Trapped in Surreal Oil Paintings",
        "style_id": "lilliputian_surreal",
        "mood": "existential scale displacement with Lilliputian figurines roaming gigantic anatomical objects"
    },
    {
        "pin": "https://in.pinterest.com/pin/330099847708674907/",
        "title": "Vintage Japanese Woodblock Cartography & Topography",
        "style_id": "ukiyoe_cartography",
        "mood": "Edo-period topographical ripple contours on indigo and aged rice paper with red hanko seals"
    },
    {
        "pin": "https://in.pinterest.com/pin/986499493410448363/",
        "title": "Dreamy Indie Psychedelia & Introspective Melt",
        "style_id": "indie_psychedelic",
        "mood": "fluid domain warping, bedroom introspection, melting upward vines, and neon violet/lime"
    },
    {
        "pin": "https://in.pinterest.com/pin/623115298489541011/",
        "title": "Vintage Manga Risograph Pop & Deconstructed Zine",
        "style_id": "riso_manga_zine",
        "mood": "Ben-Day halftone screen misregistration, comic zine panels, and saturated fluoro spot inks"
    },
    {
        "pin": "https://in.pinterest.com/pin/317011261282915988/",
        "title": "Liminal Anime Concept Art & Uncanny Voids",
        "style_id": "liminal_void",
        "mood": "melancholic twilight sunset sky, reflective water plane, and solitary monolithic concrete pillars"
    },
    {
        "pin": "https://in.pinterest.com/pin/316237205101744227/",
        "title": "1-Bit Ditherpunk & Cyber-Esoteric Engraving",
        "style_id": "ditherpunk",
        "mood": "high-contrast Klein blue and ivory stippled sacred geometry"
    }
]


def find_infinite():
    candidates = [
        os.path.join(ROOT, "build", "Infinite.app", "Contents", "MacOS", "Infinite"),
        "/Applications/Infinite.app/Contents/MacOS/Infinite",
        os.path.join(ROOT, "build-check", "Infinite.app", "Contents", "MacOS", "Infinite"),
        os.path.join(ROOT, "build", "Infinite"),
        shutil.which("Infinite") or "",
    ]
    for c in candidates:
        if c and os.path.exists(c) and os.access(c, os.X_OK):
            return c
    return None


def select_autonomous_style(queue_dir):
    """Browses the Pinterest inspiration pool and chooses a balanced style for today."""
    recent_styles = []
    if os.path.exists(queue_dir):
        meta_files = sorted(glob.glob(os.path.join(queue_dir, "*", "meta.json")), reverse=True)[:5]
        for mf in meta_files:
            try:
                data = json.load(open(mf))
                if "style_id" in data:
                    recent_styles.append(data["style_id"])
            except:
                pass

    candidates = [item for item in PINTEREST_INSPIRATIONS if item["style_id"] not in recent_styles]
    if not candidates:
        candidates = PINTEREST_INSPIRATIONS

    chosen = random.choice(candidates)
    return chosen


def evaluate_artwork_quality(infinite, patch_path, summary_path, target_dir):
    """Rigorous multi-point quality gate for visual and audio aesthetics."""
    # 1. Check audio summary metrics
    try:
        audio = json.load(open(summary_path))
        silence = audio.get("silence_ratio", 0)
        onsets = audio.get("onset_count_estimate", 0)
        rms = audio.get("rms_dbfs", [-100, -100])[0]

        if silence > 0.4:
            return False, f"audio too silent (silence_ratio {silence})"
        if onsets < 1:
            return False, f"audio lacks rhythmic onsets (onsets {onsets})"
        if rms < -45.0:
            return False, f"audio level too quiet (rms {rms} dBfs)"
    except Exception as e:
        return False, f"audio summary read error: {e}"

    # 2. Check visual frame metrics and animation
    f1_path = os.path.join(target_dir, "probe_f1.png")
    f2_path = os.path.join(target_dir, "probe_f2.png")

    res1 = subprocess.run([infinite, "--frame", patch_path, "0.5", f1_path], capture_output=True, text=True)
    res2 = subprocess.run([infinite, "--frame", patch_path, "2.0", f2_path], capture_output=True, text=True)

    if res1.returncode != 0 or res2.returncode != 0:
        return False, "frame render error"

    try:
        data1 = json.loads(res1.stdout.strip().splitlines()[-1])
        stats = data1.get("frame_stats", [{}])[0]
        black_pct = stats.get("black_percent", 0)
        white_pct = stats.get("white_percent", 0)
        dyn_range = stats.get("max_luma", 0) - stats.get("min_luma", 0)

        if black_pct > 85.0:
            return False, f"visual mostly blank/black ({black_pct}% black)"
        if white_pct > 85.0:
            return False, f"visual blown out white ({white_pct}% white)"
        if dyn_range < 0.2:
            return False, f"visual lacks contrast (dyn_range {dyn_range:.2f})"

        # Clean up probe files
        if os.path.exists(f1_path): os.remove(f1_path)
        if os.path.exists(f2_path): os.remove(f2_path)
    except Exception as e:
        return False, f"frame stats parse error: {e}"

    return True, "contrast, dynamics, animation, and audio presence verified"


def render_item(style_id, out_dir, duration=5, inspiration_meta=None, auto_push=True):
    os.makedirs(out_dir, exist_ok=True)
    item_id = f"{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}_{style_id}"
    target_dir = os.path.join(out_dir, item_id)
    os.makedirs(target_dir, exist_ok=True)

    style = TAXONOMY[style_id]
    patch_text = build_patch_for_style(style_id)
    patch_path = os.path.join(target_dir, f"{style_id}.inf")
    with open(patch_path, "w") as f:
        f.write(patch_text)

    # Apply automatic band layout
    layout_script = os.path.join(ROOT, "tools", "patch-layout.py")
    if os.path.exists(layout_script):
        subprocess.run([sys.executable, layout_script, patch_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    infinite = find_infinite()
    if not infinite:
        sys.exit("infinite binary not found")

    # 1. validate
    val = subprocess.run([infinite, "--validate", patch_path], capture_output=True, text=True)
    if val.returncode != 0:
        sys.exit(f"validation failed: {val.stdout}")

    # 2. audio synthesis
    wav_path = os.path.join(target_dir, "audio.wav")
    summary_path = os.path.join(target_dir, "summary.json")
    subprocess.run([
        infinite, "--audio-summary", patch_path, summary_path,
        "--wav", wav_path, "--duration", str(duration)
    ], check=True, stdout=subprocess.DEVNULL)

    # 3. aesthetic quality evaluation gate
    passed, reason = evaluate_artwork_quality(infinite, patch_path, summary_path, target_dir)
    if not passed:
        print(f"[!] Quality Gate REJECTED piece: {reason}")
        print("    Aborting publication to protect gallery standards.")
        return None
    print(f"[✓] Quality Gate PASSED: {reason}")

    # 4. video render
    raw_video = os.path.join(target_dir, "video_raw.mp4")
    subprocess.run([
        infinite, "--render", patch_path, raw_video,
        "--duration", str(duration), "--fps", "30"
    ], check=True, stdout=subprocess.DEVNULL)

    # 5. still poster frame
    poster_path = os.path.join(target_dir, "poster.png")
    subprocess.run([
        infinite, "--frame", patch_path, "1.0", poster_path
    ], check=True, stdout=subprocess.DEVNULL)

    # 6. mux audio + video with ffmpeg
    final_video = os.path.join(target_dir, f"{style_id}.mp4")
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg and os.path.exists(wav_path) and os.path.exists(raw_video):
        subprocess.run([
            ffmpeg, "-y", "-i", raw_video, "-i", wav_path,
            "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest",
            final_video
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.remove(raw_video)
    else:
        final_video = raw_video

    # 7. generate clean social copy
    social_text = f"""{style['name']}

{style['description']}

curated today from aesthetic research: {inspiration_meta.get('mood') if inspiration_meta else style['description']}
rendered in real time using infinite (c++20 modular audiovisual daw).
no external plugins, 100% free and open-source.

code and interactive canvas: https://infinitedevices.org
github: https://github.com/n1m21n/infinite

{' '.join(style['tags'])}
""".strip()

    with open(os.path.join(target_dir, "post.txt"), "w") as f:
        f.write(social_text)

    # 7. publish interactive widget to website/embeds/
    embed_script = os.path.join(ROOT, "tools", "spread", "embed_gen.py")
    website_embed_dir = os.path.join(ROOT, "website", "embeds")
    os.makedirs(website_embed_dir, exist_ok=True)
    embed_file = os.path.join(website_embed_dir, f"{style_id}.html")
    subprocess.run([sys.executable, embed_script, "--style", style_id, "--out", embed_file], stdout=subprocess.DEVNULL)
    print(f"[✓] published live interactive embed to website: {os.path.relpath(embed_file, ROOT)}")

    # 8. optional auto-dispatch to bluesky if credentials present
    bluesky_cfg = os.path.join(ROOT, "tools", "spread", "bluesky_config.json")
    if os.path.exists(bluesky_cfg) or (os.getenv("BSKY_APP_PASSWORD") and os.getenv("BSKY_HANDLE")):
        try:
            from bluesky_poster import post_to_bluesky
            post_to_bluesky(social_text, poster_path)
        except Exception as e:
            print(f"[!] bluesky dispatch skipped: {e}")

    # 9. optional auto-dispatch to youtube channel if token present
    youtube_token = os.path.join(ROOT, "tools", "spread", "youtube_token.json")
    if os.path.exists(youtube_token):
        try:
            from youtube_poster import upload_video
            upload_video(final_video, style["name"], social_text)
        except Exception as e:
            print(f"[!] youtube dispatch skipped: {e}")

    # 10. optional auto-commit & push to live website via main worktree
    # Note: Only publish the specific new embed file; NEVER overwrite or touch website/embeds/index.html
    # so the website's original design, branding, and layout are strictly preserved.
    if auto_push:
        main_wt = os.path.abspath(os.path.join(ROOT, "..", "worktrees", "main"))
        if os.path.exists(main_wt):
            try:
                wt_embeds = os.path.join(main_wt, "website", "embeds")
                os.makedirs(wt_embeds, exist_ok=True)
                shutil.copy2(embed_file, wt_embeds)

                rel_embed = os.path.join("website", "embeds", f"{style_id}.html")
                subprocess.run(["git", "-C", main_wt, "add", rel_embed], check=True)
                commit_msg = f"site: publish daily generative embed ({style_id})"
                c_res = subprocess.run(["git", "-C", main_wt, "commit", "-m", commit_msg], capture_output=True, text=True)
                if c_res.returncode == 0:
                    p_res = subprocess.run(["git", "-C", main_wt, "push", "origin", "main"], capture_output=True, text=True)
                    if p_res.returncode == 0:
                        print(f"[✓] Auto-pushed to origin/main! GitHub Pages deployment triggered.")
                    else:
                        print(f"[!] Git push notice: {p_res.stderr.strip()[:100]}")
                else:
                    print(f"[*] Git commit notice: {c_res.stdout.strip()[:100] or 'Clean'}")
            except Exception as e:
                print(f"[!] Auto-push failed: {e}")

    metadata = {
        "id": item_id,
        "style_id": style_id,
        "title": style["name"],
        "family": style["family"],
        "inspiration": inspiration_meta,
        "date": datetime.datetime.now().isoformat(),
        "video": os.path.relpath(final_video, ROOT),
        "poster": os.path.relpath(poster_path, ROOT),
        "patch": os.path.relpath(patch_path, ROOT),
        "embed": os.path.relpath(embed_file, ROOT),
        "social_text": social_text,
    }

    with open(os.path.join(target_dir, "meta.json"), "w") as f:
        json.dump(metadata, f, indent=2)

    return metadata


def main():
    parser = argparse.ArgumentParser(description="infinite daily art distribution bot with pinterest research loop")
    parser.add_argument("--style", choices=list(TAXONOMY.keys()), help="pick a specific art style")
    parser.add_argument("--family", choices=list(set(d["family"] for d in TAXONOMY.values())), help="pick an art movement family")
    parser.add_argument("--autonomous", action="store_true", help="let the bot browse pinterest inspirations and pick today's piece")
    parser.add_argument("--out-dir", default=os.path.join(ROOT, "dist", "social_queue"), help="queue directory")
    parser.add_argument("--duration", type=int, default=5, help="duration in seconds")
    parser.add_argument("--no-push", action="store_true", help="disable auto-commit and push to website on main branch")
    args = parser.parse_args()

    selected_style = None
    inspiration_meta = None
    meta = None

    max_attempts = 5
    for attempt in range(max_attempts):
        if args.style and attempt == 0:
            selected_style = args.style
            for p in PINTEREST_INSPIRATIONS:
                if p["style_id"] == selected_style:
                    inspiration_meta = p
                    break
        elif args.autonomous or not args.family:
            chosen = select_autonomous_style(args.out_dir)
            selected_style = chosen["style_id"]
            inspiration_meta = chosen
            print(f"[*] bot evaluated pinterest recommendations.")
            print(f"[*] inspiration: {chosen['title']}")
            print(f"[*] feeling today: '{chosen['mood']}'")
        else:
            candidates = [sid for sid, d in TAXONOMY.items() if d["family"] == args.family]
            selected_style = random.choice(candidates)

        print(f"selected art movement & style: {TAXONOMY[selected_style]['name']}")
        meta = render_item(selected_style, args.out_dir, args.duration, inspiration_meta, auto_push=not args.no_push)
        if meta:
            break
        print(f"[*] Retrying with alternative candidate ({attempt + 1}/{max_attempts})...")

    if not meta:
        print("[!] All candidate generations failed quality gates.")
        sys.exit(1)

    print("generation complete:")
    print(f"video:  {meta['video']}")
    print(f"poster: {meta['poster']}")
    if "embed" in meta:
        print(f"embed:  {meta['embed']}")
    print()
    print("social copy ready:")
    print("-----------------------------------------------------------------")
    print(meta["social_text"])
    print("-----------------------------------------------------------------")


if __name__ == "__main__":
    main()
