#!/bin/bash
# Download THINGS-EEG2 files listed in a manifest (name size md5 url) into a folder, md5-checked.
# Usage: fetch.sh MANIFEST OUT_DIR NAME [NAME ...]
set -euo pipefail
manifest=$1; out=$2; shift 2
for name in "$@"; do
  line=$(awk -v n="$name" '$1==n' "$manifest")
  [ -z "$line" ] && { echo "not in manifest: $name"; exit 1; }
  md5=$(echo "$line" | awk '{print $3}'); url=$(echo "$line" | awk '{print $4}')
  f="$out/$name"
  if [ -f "$f" ] && [ "$(md5 -q "$f")" = "$md5" ]; then echo "ok (cached) $name"; continue; fi
  curl -sSL --retry 5 -C - -o "$f" "$url" || curl -sSL --retry 5 -o "$f" "$url"
  got=$(md5 -q "$f"); [ "$got" = "$md5" ] && echo "ok $name" || { echo "MD5 MISMATCH $name"; exit 1; }
done
