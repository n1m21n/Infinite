#!/usr/bin/env python3
"""Generates a clean, human-readable OUTREACH_SHEET.md for Naman.
Matches queued threads against Infinite's articles, nodes, and codebases,
producing ready-to-paste lowercase technical responses.
"""
import json, os, re, sys, html, urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LEDGER = os.path.join(ROOT, "tools", "seo", "ledger.json")
POSTS_DIR = os.path.join(ROOT, "tools", "seo", "posts")
SHEET = os.path.join(ROOT, "tools", "seo", "OUTREACH_SHEET.md")

# Load knowledge base
POSTS = {}
if os.path.exists(POSTS_DIR):
    for f in os.listdir(POSTS_DIR):
        if f.endswith(".md"):
            with open(os.path.join(POSTS_DIR, f)) as fp:
                content = fp.read()
                POSTS[f] = content

def match_knowledge(title, snippet):
    combined = (title + " " + snippet).lower()
    
    if any(k in combined for k in ["touchdesigner", "vj", "visualizer", "reactive", "glsl", "generative visuals"]):
        return {
            "topic": "audio-reactive visuals & glsl",
            "guide": "free-touchdesigner-alternative-for-audio-reactive-visuals.md",
            "recipe": "Audio In -> Audio Analyze (8-band FFT) -> Field Pixel (uniforms: audio_bass, audio_mid)",
            "limit": "desktop only (opengl/glsl), no browser-based web canvas yet",
            "draft": (
                "for real-time audio-reactive visuals and shaders, you can check out how we structured infinite (open source, https://github.com/n1m21n/infinite).\n\n"
                "instead of bridging a daw and touchdesigner over syphon/osc with frame latency, it runs audio dsp and a glsl fragment pipeline in true lockstep on one canvas. "
                "audio buffers feed an 8-band fft that writes into atomic uniforms read directly by glsl shader nodes.\n\n"
                "full disclosure: i make infinite. honest limitation: it's desktop c++20/opengl right now, so no pure web canvas."
            )
        }
    elif any(k in combined for k in ["vst", "modular daw", "audio engine", "dsp", "juce", "synth"]):
        return {
            "topic": "modular daw & audio dsp",
            "guide": "dsp-without-juce.md",
            "recipe": "MIDI Notes -> Poly Synth -> Saturator -> Delay -> Audio Out",
            "limit": "no aax format support, young project",
            "draft": (
                "if you're looking into modular node-based audio or hosting au/vst3 plugins with deep modulation, take a look at infinite (https://infinitedevices.org).\n\n"
                "it lets you modulate any plugin parameter directly from lfos, physics nodes, 3d scene coordinates, or audio envelope followers without latency penalty.\n\n"
                "i make infinite (mit/gplv3, https://github.com/n1m21n/infinite). limit: no aax support, strictly vst3/au."
            )
        }
    elif any(k in combined for k in ["headless", "cli", "render", "automate", "ffmpeg", "script"]):
        return {
            "topic": "headless audio/visual cli rendering",
            "guide": "render-video-and-audio-from-a-text-file.md",
            "recipe": "Infinite --validate patch.inf --render out.mp4 --audio-summary stats.json",
            "limit": "headless video rendering on linux requires xvfb",
            "draft": (
                "if you need offline or automated rendering of modular patches from a terminal/ci script, you can run infinite completely headless:\n\n"
                "```bash\n"
                "Infinite --validate patch.inf --render out.mp4 --audio-summary stats.json\n"
                "```\n\n"
                "it renders 48khz audio buffers and glsl frames deterministically without opening a window server.\n\n"
                "i make infinite (open source c++20, https://github.com/n1m21n/infinite). on linux, headless rendering currently needs xvfb-run."
            )
        }
    else:
        return {
            "topic": "creative coding workstation",
            "guide": "one-graph-for-sound-and-pictures.md",
            "recipe": "JavaScript Sketch (Canvas 2D) <-> Modular Sound Graph",
            "limit": "young open source project",
            "draft": (
                "for procedural creative coding combining sound and pictures on one canvas, take a look at infinite (https://infinitedevices.org).\n\n"
                "it couples 2d javascript sketches and glsl shaders directly with a sample-accurate audio graph.\n\n"
                "full disclosure: i make infinite (https://github.com/n1m21n/infinite). it's a young project, but free and open source."
            )
        }

def generate():
    if not os.path.exists(LEDGER):
        print("No ledger found at", LEDGER)
        return
    with open(LEDGER) as f:
        data = json.load(f)
    
    queue = data.get("outreach", {}).get("queue", {})
    open_items = [(v.get("score", 0), u, v) for u, v in queue.items() if v.get("status") == "open"]
    open_items.sort(key=lambda x: -x[0])
    
    md = []
    md.append("# Infinite Community Outreach Tracker & Response Sheet\n")
    md.append("> **Rules:** Max 3 replies per day total, max 1 per platform. Smallcase, human tone, answer first, honest limit stated.\n")
    md.append("> **Workflow:** Pick 1–2 items below, review the tailored lowercase draft, paste/tweak on the thread, then run:\n")
    md.append("> `python3 tools/seo/radar_queue.py mark <id> posted --url <your_post_url>`\n\n")
    md.append("---\n\n")
    
    # Filter out self posts or irrelevant ones
    valid_items = []
    for sc, u, v in open_items:
        title = v.get("title", "")
        # skip own show hn
        if "Show HN: Infinite" in title:
            continue
        valid_items.append((sc, u, v))
        if len(valid_items) >= 15:
            break
            
    for sc, u, v in valid_items:
        vid = v.get("id", "?")
        src = v.get("src", "web")
        title = v.get("title", "Untitled")
        match = match_knowledge(title, "")
        
        md.append(f"### [#{vid}] [{src.upper()}] {title}\n")
        md.append(f"- **URL**: [{u}]({u})\n")
        md.append(f"- **Score**: `{sc}` | **Matched Topic**: `{match['topic']}`\n")
        md.append(f"- **Reference Post**: `tools/seo/posts/{match['guide']}`\n")
        md.append(f"- **Node Recipe**: `{match['recipe']}`\n")
        md.append(f"- **Honest Limit**: *{match['limit']}*\n\n")
        md.append("**Ready Human Reply (Lowercase & Value-First):**\n")
        md.append("```text\n" + match["draft"] + "\n```\n\n")
        md.append(f"*Action: `python3 tools/seo/radar_queue.py mark {vid} posted --url <url>` or `mark {vid} skipped`*\n\n")
        md.append("---\n\n")
        
    with open(SHEET, "w") as f:
        f.write("".join(md))
    print(f"Generated {SHEET} with top {len(valid_items)} opportunities.")

if __name__ == "__main__":
    generate()
