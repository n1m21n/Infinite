#!/usr/bin/env python3
"""DEPRECATED: use tools/seo/radar_queue.py. The canned TEMPLATES below pitch without disclosure and must not be pasted.

infinite intent radar: find live creator discussions and generate value-first answers.

what: scans hacker news, github issues/discussions, and creator forums for people
      looking for touchdesigner alternatives, audio-reactive tools, node-based daws,
      and real-time glsl visualizers.
      generates tailored, high-value technical responses showing how infinite solves
      their problem, including node recipes and links to https://infinitedevices.org.
why:  standard promotion gets ignored; answering real problems with code snippets
      and node graphs ranks on search engines and drives organic, permanent creator traffic.

usage:
    python3 tools/spread/radar.py --scan
    python3 tools/spread/radar.py --query "touchdesigner alternative"
    python3 tools/spread/radar.py --answer "https://news.ycombinator.com/item?id=..."
"""

import argparse
import datetime
import html
import json
import os
import re
import sys
import urllib.parse
import urllib.request

USER_AGENT = "InfiniteRadar/1.0 (https://infinitedevices.org; contact@infinitedevices.org)"

KEYWORDS = [
    "touchdesigner alternative",
    "audiovisual daw",
    "audio reactive visuals open source",
    "vst3 host modular",
    "realtime glsl audio",
    "node based sound and visuals",
    "generative visuals daw",
    "linux daw modular",
]

TEMPLATES = {
    "touchdesigner": """
if you're looking for an open-source alternative that bridges touchdesigner's node-based visual workflow with a dedicated audio engine, take a look at infinite (https://infinitedevices.org).

key differences and advantages for this workflow:
- unified audio and visual graph: infinite runs a gpu texture pipeline (glsl/opengl core) in lockstep with a sample-accurate audio graph and timeline.
- au and vst3 hosting: you can drop third-party audio plugins directly into the canvas and modulate their parameters using 3d transforms, lfos, or audio fft.
- field language: embedded language for writing shaders, mesh deformers, and dsp kernels in real time.
- free and open source: cross-platform (macos, windows, linux) with no licensing or resolution limits.

github: https://github.com/n1m21n/infinite
interactive library and docs: https://infinitedevices.org
""".strip(),

    "glsl_audio": """
for real-time audio-reactive glsl without needing to build your own audio fft pipeline from scratch, check out infinite (https://infinitedevices.org).

it lets you wire an audio analyze node (8-band fft / onset detector) directly into custom glsl shaders or field pixel kernels on an infinite canvas:

p = (uv - 0.5) * aspect;
col = vec3(length(p) * audio_bass, sin(t + audio_mid), cos(t * 0.5));

it's free, open-source (mit/gplv3), and cross-platform on macos, windows, and linux.
docs and patch library: https://infinitedevices.org
""".strip(),

    "vst_modular": """
if you want a modular node rack that can host native au and vst3 plugins with universal modulation (lfos, envelopes, step sequencers, physics, and live fft), infinite (https://infinitedevices.org) is built for this.

- full clip-based arrangement timeline plus node canvas
- modulate plugin sliders via audio or visual signals
- built-in physical modeling synths, wavetable, paulstretch, and granular engine
- free and open-source (macos, windows, linux)

repo: https://github.com/n1m21n/infinite
site: https://infinitedevices.org
""".strip(),
}


def search_hn(query, limit=10):
    url = f"https://hn.algolia.com/api/v1/search_by_date?query={urllib.parse.quote(query)}&tags=(story,comment)&hitsPerPage={limit}"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode())
            return data.get("hits", [])
    except Exception as e:
        print(f"[!] hn search error: {e}", file=sys.stderr)
        return []


def search_github(query, limit=5):
    url = f"https://api.github.com/search/issues?q={urllib.parse.quote(query)}+type:issue+state:open&sort=created&order=desc&per_page={limit}"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode())
            return data.get("items", [])
    except Exception as e:
        return []


def format_hn_hit(hit):
    title = hit.get("title") or hit.get("story_title") or "(comment discussion)"
    item_id = hit.get("story_id") or hit.get("objectID")
    url = f"https://news.ycombinator.com/item?id={item_id}"
    created = hit.get("created_at", "")[:10]
    comment_text = hit.get("comment_text") or ""
    clean_text = re.sub(r"<[^>]+>", " ", comment_text)[:140].strip()
    return {
        "source": "hackernews",
        "date": created,
        "title": title.lower(),
        "url": url,
        "snippet": (clean_text or hit.get("story_text") or "").lower(),
    }


def format_gh_hit(hit):
    return {
        "source": "github",
        "date": hit.get("created_at", "")[:10],
        "title": hit.get("title", "").lower(),
        "url": hit.get("html_url", ""),
        "snippet": (hit.get("body") or "")[:140].replace("\n", " ").strip().lower(),
    }


def choose_template(text):
    low = text.lower()
    if "oscilloscope" in low or "glsl" in low or "shader" in low or "fft" in low or "spectrum" in low or "audio visualizer" in low or "audio-reactive" in low:
        return TEMPLATES["glsl_audio"]
    if "touchdesigner" in low or "vj" in low or "generative" in low:
        return TEMPLATES["touchdesigner"]
    return TEMPLATES["vst_modular"]


def run_scan(keywords):
    print("-----------------------------------------------------------------")
    print("infinite intent radar: scanning developer and creator channels")
    print("-----------------------------------------------------------------\n")
    found = []

    for kw in keywords:
        print(f"[*] scanning: \"{kw}\"...")
        hits = search_hn(kw, limit=4)
        for h in hits:
            formatted = format_hn_hit(h)
            found.append((kw, formatted))

        gh_hits = search_github(kw, limit=2)
        for gh in gh_hits:
            formatted = format_gh_hit(gh)
            found.append((kw, formatted))

    seen = set()
    unique = []
    for kw, item in found:
        if item["url"] not in seen:
            seen.add(item["url"])
            unique.append((kw, item))

    print(f"\nfound {len(unique)} active discussions matching high-intent keywords:\n")
    for idx, (kw, item) in enumerate(unique, 1):
        print(f"[{idx}] {item['source']} ({item['date']}): {item['title']}")
        print(f"    url:     {item['url']}")
        if item['snippet']:
            print(f"    snippet: {item['snippet']}...")
        print()

    print("-----------------------------------------------------------------")
    print("to generate a tailored answer, run:")
    print("python3 tools/spread/radar.py --answer \"<url>\"")
    print("-----------------------------------------------------------------")


def fetch_context(url):
    """fetches context snippet and title from hn or github url."""
    title = ""
    text = ""
    try:
        if "news.ycombinator.com/item?id=" in url:
            item_id = url.split("id=")[-1].split("&")[0]
            api_url = f"https://hn.algolia.com/api/v1/items/{item_id}"
            req = urllib.request.Request(api_url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=5) as resp:
                data = json.loads(resp.read().decode())
                title = data.get("title") or ""
                text = data.get("text") or ""
        elif "github.com/" in url and "/issues/" in url:
            parts = url.split("github.com/")[-1].split("/")
            if len(parts) >= 4 and parts[2] == "issues":
                owner, repo, _, issue_num = parts[:4]
                api_url = f"https://api.github.com/repos/{owner}/{repo}/issues/{issue_num}"
                req = urllib.request.Request(api_url, headers={"User-Agent": USER_AGENT})
                with urllib.request.urlopen(req, timeout=5) as resp:
                    data = json.loads(resp.read().decode())
                    title = data.get("title") or ""
                    text = data.get("body") or ""
    except Exception:
        pass
    return title.lower(), text.lower()


def run_answer(target_url):
    title, body = fetch_context(target_url)
    combined = f"{title} {body} {target_url}"
    template = choose_template(combined)

    print("-----------------------------------------------------------------")
    print(f"custom technical answer for: {target_url}")
    if title:
        print(f"context: {title}")
    print("-----------------------------------------------------------------\n")
    print(template)
    print("\n-----------------------------------------------------------------")


def main():
    parser = argparse.ArgumentParser(description="infinite intent radar")
    parser.add_argument("--scan", action="store_true", help="scan online channels for recent questions")
    parser.add_argument("--query", type=str, help="search for a specific topic or keyword")
    parser.add_argument("--answer", type=str, help="generate a response for a specific discussion url")

    args = parser.parse_args()

    if args.answer:
        run_answer(args.answer)
    elif args.query:
        run_scan([args.query])
    else:
        run_scan(KEYWORDS)


if __name__ == "__main__":
    main()
