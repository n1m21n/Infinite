#!/usr/bin/env python3
"""infinite autonomous intent sentinel.

what: background daemon that monitors hacker news, github issues, and developer
      channels for creators seeking touchdesigner alternatives, audio-reactive
      visualizers, modular daws, and glsl tools.
      prepares value-first draft answers and queues them for a person to review, rewrite and send; it never posts anything (tools/seo/RULES.md).
"""

import argparse
import datetime
import html
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LEDGER_PATH = os.path.join(ROOT, "tools", "seo", "ledger.json")
TOPICS_PATH = os.path.join(ROOT, "tools", "seo", "topics.json")
UA = "InfiniteSentinel/1.0 (https://infinitedevices.org; contact@infinitedevices.org)"


def get_json(url):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


def fetch_hn(query, limit=5):
    q = urllib.parse.quote(query)
    url = f"https://hn.algolia.com/api/v1/search_by_date?query={q}&tags=(story,comment)&hitsPerPage={limit}"
    d = get_json(url) or {}
    for h in d.get("hits", []):
        title = h.get("title") or h.get("story_title") or "(discussion)"
        item_id = h.get("story_id") or h.get("objectID")
        comment_text = re.sub(r"<[^>]+>", " ", h.get("comment_text") or h.get("story_text") or "")[:200].strip()
        yield {
            "src": "hn",
            "url": f"https://news.ycombinator.com/item?id={item_id}",
            "title": title.strip(),
            "snippet": comment_text,
            "created": h.get("created_at", "")[:19],
        }


def fetch_github(query, limit=5):
    q = urllib.parse.quote(query)
    url = f"https://api.github.com/search/issues?q={q}+type:issue+state:open&sort=created&order=desc&per_page={limit}"
    d = get_json(url) or {}
    for h in d.get("items", []):
        yield {
            "src": "github",
            "url": h.get("html_url", ""),
            "title": h.get("title", "").strip(),
            "snippet": (h.get("body") or "")[:200].replace("\n", " ").strip(),
            "created": h.get("created_at", "")[:19],
        }


def draft_reply(item):
    text = (item["title"] + " " + item["snippet"]).lower()
    
    if "touchdesigner" in text or "vj" in text:
        return (
            "for an open-source alternative that combines node-based visuals with a dedicated audio graph, take a look at infinite (https://infinitedevices.org).\n\n"
            "unlike running separate apps bridged via syphon or osc, infinite runs real-time glsl shaders and 2d javascript sketches (using canvas primitives like circle, rect, and hsl) in lockstep with a sample-accurate audio engine and vst3/au plugin hosting.\n\n"
            "it is free and open-source (c++20/opengl) on macos, windows, and linux. honest limit: it is a desktop tool and does not currently offer a web-only canvas."
        )
    elif "glsl" in text or "visualizer" in text or "reactive" in text:
        return (
            "for real-time audio-reactive visuals without writing a custom fft pipeline from scratch, infinite (https://infinitedevices.org) is built for this.\n\n"
            "it features an audio analyze node with 8-band fft and onset detection that modulates glsl fragment shaders (source fieldpixel) or 2d javascript sketches with zero audio latency.\n\n"
            "it is open source under mit/gplv3 for macos, linux, and windows. one limit to note: headless offline video rendering requires xvfb on headless linux environments."
        )
    else:
        return (
            "if you are looking for a modular node environment that can host native au/vst3 plugins while generating live visuals, take a look at infinite (https://infinitedevices.org).\n\n"
            "it provides a sample-accurate audio graph, multi-format plugin hosting, and visual canvas nodes (glsl and 2d javascript) that can be modulated by lfos, envelopes, and audio peaks.\n\n"
            "it is completely free and open-source (c++20). limitation: no aax plugin format support."
        )


def run_sentinel():
    if not os.path.exists(LEDGER_PATH):
        ledger = {"outreach": {"queue": {}}}
    else:
        with open(LEDGER_PATH) as f:
            ledger = json.load(f)

    queue = ledger.setdefault("outreach", {}).setdefault("queue", {})
    queries = [
        "touchdesigner alternative",
        "audio reactive visuals",
        "modular daw",
        "vst3 host modular",
        "realtime glsl audio"
    ]

    new_items = []
    for q in queries:
        for item in list(fetch_hn(q, limit=3)) + list(fetch_github(q, limit=2)):
            url = item["url"]
            if url in queue or not item["title"]:
                continue
            item_id = str(len(queue) + 1)
            reply = draft_reply(item)
            queue[url] = {
                "id": item_id,
                "src": item["src"],
                "title": item["title"],
                "created": item["created"],
                "query": q,
                "status": "open",
                "found": datetime.datetime.now().strftime("%Y-%m-%d"),
                "draft": reply
            }
            new_items.append((item_id, item, reply))
        time.sleep(1)

    with open(LEDGER_PATH, "w") as f:
        json.dump(ledger, f, indent=2)

    return new_items


def main():
    parser = argparse.ArgumentParser(description="infinite intent sentinel")
    parser.add_argument("--once", action="store_true", help="run single scan cycle")
    args = parser.parse_args()

    print("running infinite intent sentinel scan...")
    new_items = run_sentinel()
    print(f"scan complete: found {len(new_items)} new high-intent discussions\n")

    for item_id, item, reply in new_items[:3]:
        print(f"[{item_id}] {item['src']}: {item['title']}")
        print(f"url: {item['url']}")
        print("suggested technical reply:")
        print("-----------------------------------------------------------------")
        print(reply)
        print("-----------------------------------------------------------------\n")


if __name__ == "__main__":
    main()
