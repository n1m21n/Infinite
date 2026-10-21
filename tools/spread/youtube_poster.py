#!/usr/bin/env python3
"""Infinite Automated YouTube Channel Uploader (YouTube Shorts & Videos).

What: Uploads rendered .mp4 audiovisual loops directly to the official
      infinite.devices YouTube channel as YouTube Shorts.
Why:  YouTube Shorts generate immense algorithmic discovery for audio-visual
      and creative coding workstations.

Setup:
1. Go to Google Cloud Console (https://console.cloud.google.com).
2. Create or select your project (e.g. "Infinite Devices").
3. Enable "YouTube Data API v3".
4. Go to Credentials > Create Credentials > OAuth client ID > Desktop Application.
5. Download client JSON and save it as:
   tools/spread/youtube_client_secrets.json
6. Run this script once: it will open a browser window to authorize your infinite.devices channel.
   It saves a persistent token in tools/spread/youtube_token.json so it never asks again.
"""

import argparse
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLIENT_SECRETS_FILE = os.path.join(ROOT, "tools", "spread", "youtube_client_secrets.json")
TOKEN_FILE = os.path.join(ROOT, "tools", "spread", "youtube_token.json")
SCOPES = ["https://www.googleapis.com/auth/youtube.upload"]


def get_authenticated_service():
    """Authenticates using stored token or initiates one-time OAuth 2.0 flow."""
    try:
        from google.auth.transport.requests import Request
        from google.oauth2.credentials import Credentials
        from google_auth_oauthlib.flow import InstalledAppFlow
        from googleapiclient.discovery import build
    except ImportError:
        print("[!] Missing required Google API libraries. Install via:")
        print("    pip install google-api-python-client google-auth-oauthlib google-auth-httplib2")
        return None

    creds = None
    if os.path.exists(TOKEN_FILE):
        creds = Credentials.from_authorized_user_file(TOKEN_FILE, SCOPES)

    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            if not os.path.exists(CLIENT_SECRETS_FILE):
                print(f"[!] {CLIENT_SECRETS_FILE} not found.")
                print("    Download your OAuth 2.0 client credentials from Google Cloud Console.")
                return None
            flow = InstalledAppFlow.from_client_secrets_file(CLIENT_SECRETS_FILE, SCOPES)
            creds = flow.run_local_server(port=0)

        with open(TOKEN_FILE, "w") as token:
            token.write(creds.to_json())

    return build("youtube", "v3", credentials=creds)


def upload_video(file_path, title, description, tags=None, category_id="10", privacy_status="public"):
    """Uploads a video to YouTube and returns the video ID."""
    from googleapiclient.http import MediaFileUpload

    youtube = get_authenticated_service()
    if not youtube:
        return None

    # Ensure title contains #Shorts for YouTube algorithmic Shorts distribution
    if "#Shorts" not in title and "#shorts" not in title:
        title = f"{title} #Shorts"

    body = {
        "snippet": {
            "title": title[:100],
            "description": f"{description}\n\nMade with Infinite (open-source audiovisual DAW): https://infinitedevices.org\n#Shorts #creativecoding #generativeart",
            "tags": tags or ["Infinite", "creativecoding", "modular", "synth", "audiovisual"],
            "categoryId": category_id
        },
        "status": {
            "privacyStatus": privacy_status,
            "selfDeclaredMadeForKids": False
        }
    }

    media = MediaFileUpload(file_path, chunksize=-1, resumable=True, mimetype="video/mp4")
    request = youtube.videos().insert(part="snippet,status", body=body, media_body=media)

    print(f"[*] Uploading {file_path} to YouTube ({title})...")
    response = None
    while response is None:
        status, response = request.next_chunk()
        if status:
            print(f"    Upload progress: {int(status.progress() * 100)}%")

    video_id = response.get("id")
    video_url = f"https://youtu.be/{video_id}"
    print(f"[✓] Upload successful! Live at: {video_url}")
    return video_url


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Infinite YouTube Channel Uploader")
    parser.add_argument("--video", required=True, help="Path to .mp4 video file")
    parser.add_argument("--title", required=True, help="Video title")
    parser.add_argument("--desc", default="Generated in real time with Infinite", help="Description")
    parser.add_argument("--tags", nargs="*", default=["generativeart", "creativecoding", "modular"], help="Tags")
    parser.add_argument("--privacy", default="public", choices=["public", "unlisted", "private"], help="Privacy")
    args = parser.parse_args()

    upload_video(args.video, args.title, args.desc, args.tags, privacy_status=args.privacy)
