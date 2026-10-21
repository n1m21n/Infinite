#!/usr/bin/env python3
"""infinite autonomous growth & distribution status dashboard.

what: prints a real-time summary of generated social video assets,
      newly detected high-intent creator discussions, daily outreach caps,
      and active background daemon statuses.
"""

import datetime
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
QUEUE_DIR = os.path.join(ROOT, "dist", "social_queue")
LEDGER_PATH = os.path.join(ROOT, "tools", "seo", "ledger.json")


def get_social_queue():
    items = []
    if not os.path.exists(QUEUE_DIR):
        return items
    for meta_file in sorted(glob.glob(os.path.join(QUEUE_DIR, "*", "meta.json")), reverse=True):
        try:
            with open(meta_file) as f:
                items.append(json.load(f))
        except Exception:
            pass
    return items


def get_ledger_stats():
    if not os.path.exists(LEDGER_PATH):
        return {"total": 0, "open": 0, "posted": 0, "recent": []}
    with open(LEDGER_PATH) as f:
        data = json.load(f)
    queue = data.get("outreach", {}).get("queue", {})
    posted_today = [v for v in queue.values() if v.get("status") == "posted" and v.get("posted_at", "")[:10] == datetime.datetime.now().strftime("%Y-%m-%d")]
    open_items = []
    for url, v in queue.items():
        if v.get("status") == "open":
            entry = dict(v)
            entry["url"] = v.get("url") or url
            open_items.append(entry)
    recent = sorted(open_items, key=lambda x: int(x.get("id", 0)), reverse=True)[:5]
    return {
        "total": len(queue),
        "open": len(open_items),
        "posted_today": len(posted_today),
        "recent": recent
    }


def check_launchd(label):
    try:
        r = subprocess.run(["launchctl", "list", label], capture_output=True, text=True)
        return r.returncode == 0
    except Exception:
        return False


def main():
    print("=================================================================")
    print("infinite autonomous distribution dashboard")
    print(f"timestamp: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=================================================================\n")

    # 1. daemons status
    print("1. background daemons (macos launchd):")
    discord_active = check_launchd("org.infinitedevices.discordbot")
    art_active = check_launchd("org.infinitedevices.dailyartbot")
    sentinel_active = check_launchd("org.infinitedevices.intentsentinel")

    print(f"   - discord render bot:  {'active (running)' if discord_active else 'inactive'}")
    print(f"   - daily video art bot: {'active (scheduled)' if art_active else 'inactive'}")
    print(f"   - intent sentinel:     {'active (scheduled)' if sentinel_active else 'inactive'}\n")

    # 2. generated social videos queue
    videos = get_social_queue()
    print(f"2. generated social video queue ({len(videos)} ready in dist/social_queue/):")
    for v in videos[:4]:
        date = v.get("date", "")[:16].replace("T", " ")
        print(f"   - [{date}] {v.get('title')} ({v.get('category')})")
        print(f"     video:  {v.get('video')}")
        print(f"     poster: {v.get('poster')}")
    print()

    # 3. outreach ledger
    stats = get_ledger_stats()
    print(f"3. high-intent discussions ledger ({stats['total']} tracked, {stats['open']} open):")
    print(f"   - daily posted cap: {stats['posted_today']}/3 posted today")
    print("   - latest detected discussions ready for outreach:")
    for item in stats["recent"]:
        print(f"     * [id {item.get('id')}] {item.get('src')}: {item.get('title')[:75]}")
        print(f"       url: {item.get('url')}")

    print("\n=================================================================")
    print("quick commands:")
    print("  python3 tools/spread/daily_art_bot.py   -> generate fresh video & copy")
    print("  python3 tools/spread/intent_sentinel.py -> run fresh forum scan")
    print("=================================================================")


if __name__ == "__main__":
    main()
