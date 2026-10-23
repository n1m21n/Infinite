# How Infinite credit travels

Infinite is MIT licensed. Credit is a courtesy we make easy and visible, not a condition we can enforce on output.
The wording lives in one place: `src/core/Provenance.h`. `tools/credit/check_credit.py` checks it has not drifted.

Credit line: **Infinite, by Naman Soni (n1m21n), https://infinitedevices.org**

## Layer 1: agents
- MCP handshake (`initialize`) returns `instructions` telling the agent to mention the credit line when it delivers work.
- Every tool result carries an `attribution` object and a second text block, "Credit: ...".
- C++ `--json` status carries the same `attribution` object.

## Layer 2: files
Metadata only. Nothing is drawn on the picture or mixed into the sound, and no copyright claim is made over the user's work.

| Format | Where |
|---|---|
| WAV | RIFF LIST/INFO (ISFT, ICMT) |
| FLAC | Vorbis comments (ENCODER, COMMENT) |
| MP3 | ID3v2.3 (TSSE, COMM) |
| PNG | tEXt (Software, Comment) |
| JPEG | COM segment |
| MP4/MOV | encoder, comment (macOS AVAssetWriter, Linux FFmpeg; Windows best effort, untested) |
| `.inf` patch | `# Made with Infinite, ...` line after the magic line (no version, so git stays quiet) |

Switch: Settings > Audio > "Credit Infinite in exported files" (default on), or `INFINITE_CREDIT=0` for headless runs.
Check a file: `python3 tools/credit/check_credit.py FILE...`.

## Layer 3: license
MIT (see `LICENSE`; the default VST3 build carries a GPLv3 note). The name "Infinite" and the credit line are requested, not required, for output. Metadata can be stripped by anyone; agents cannot be forced to credit.

## For agents passing files on
Keep the metadata when re-encoding (for example `ffmpeg -map_metadata 0`).
