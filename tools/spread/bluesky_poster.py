#!/usr/bin/env python3
"""Infinite Automated Bluesky Social Publishing Agent.

Uses the official AT Protocol API to authenticate, upload media blobs (posters/images),
and publish formatted posts with tags and links to Bluesky.

Environment variables required:
  BSKY_HANDLE = "infinitedevices.bsky.social" (or your custom domain handle)
  BSKY_APP_PASSWORD = "xxxx-xxxx-xxxx-xxxx" (Generated in Bluesky Settings > App Passwords)
"""

import json
import os
import sys
import urllib.request

BSKY_API_HOST = "https://bsky.social/xrpc"


def create_session(handle, app_password):
    """Authenticates with Bluesky and returns access JWT and DID."""
    url = f"{BSKY_API_HOST}/com.atproto.server.createSession"
    payload = json.dumps({"identifier": handle, "password": app_password}).encode("utf-8")
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return data["accessJwt"], data["did"]
    except Exception as e:
        print(f"[!] Bluesky session error: {e}")
        return None, None


def upload_blob(access_jwt, file_path):
    """Uploads an image (poster.png) to Bluesky blob storage."""
    url = f"{BSKY_API_HOST}/com.atproto.repo.uploadBlob"
    mime_type = "image/png" if file_path.endswith(".png") else "image/jpeg"
    with open(file_path, "rb") as f:
        data = f.read()

    req = urllib.request.Request(url, data=data, headers={
        "Authorization": f"Bearer {access_jwt}",
        "Content-Type": mime_type
    })
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read().decode("utf-8"))["blob"]
    except Exception as e:
        print(f"[!] Bluesky uploadBlob error: {e}")
        return None


def post_to_bluesky(text, image_path=None, handle=None, app_password=None):
    """Publishes a complete post with optional attached media to Bluesky."""
    handle = handle or os.getenv("BSKY_HANDLE")
    app_password = app_password or os.getenv("BSKY_APP_PASSWORD")

    # Fallback to local config if present
    config_path = os.path.join(os.path.dirname(__file__), "bluesky_config.json")
    if (not handle or not app_password) and os.path.exists(config_path):
        try:
            cfg = json.load(open(config_path))
            handle = handle or cfg.get("handle")
            app_password = app_password or cfg.get("app_password")
        except:
            pass

    if not handle or not app_password:
        print("[!] BSKY_HANDLE or BSKY_APP_PASSWORD not set in environment or bluesky_config.json.")
        return False

    jwt, did = create_session(handle, app_password)
    if not jwt:
        return False

    embed = None
    if image_path and os.path.exists(image_path):
        blob = upload_blob(jwt, image_path)
        if blob:
            embed = {
                "$type": "app.bsky.embed.images",
                "images": [{
                    "alt": "Generative audiovisual scene made in Infinite",
                    "image": blob
                }]
            }

    import datetime
    now_iso = datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z")

    # Bluesky has a strict 300 grapheme/character limit
    clean_text = text.strip()
    if len(clean_text) > 290:
        lines = [line.strip() for line in clean_text.splitlines() if line.strip()]
        title = lines[0] if lines else "Audiovisual Scene"
        tags = [w for w in lines[-1].split() if w.startswith("#")] if lines and "#" in lines[-1] else ["#creativecoding", "#infinite"]
        tag_str = " ".join(tags[:4])
        
        # Build concise post
        compact = f"{title}\n\nRendered in real time in Infinite.\nInteractive canvas: https://infinitedevices.org\n\n{tag_str}".strip()
        if len(compact) > 295:
            compact = compact[:292] + "..."
        clean_text = compact

    record = {
        "$type": "app.bsky.feed.post",
        "text": clean_text,
        "createdAt": now_iso
    }
    if embed:
        record["embed"] = embed

    url = f"{BSKY_API_HOST}/com.atproto.repo.createRecord"
    payload = json.dumps({
        "repo": did,
        "collection": "app.bsky.feed.post",
        "record": record
    }).encode("utf-8")

    req = urllib.request.Request(url, data=payload, headers={
        "Authorization": f"Bearer {jwt}",
        "Content-Type": "application/json"
    })
    try:
        with urllib.request.urlopen(req) as resp:
            result = json.loads(resp.read().decode("utf-8"))
            print(f"[✓] Published successfully to Bluesky: {result.get('uri')}")
            return True
    except Exception as e:
        print(f"[!] Bluesky createRecord error: {e}")
        return False


if __name__ == "__main__":
    if len(sys.argv) > 1:
        post_to_bluesky(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
    else:
        print("Usage: python3 tools/spread/bluesky_poster.py 'text' [image_path.png]")
