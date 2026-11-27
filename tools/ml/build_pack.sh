#!/bin/bash
# Builds a macOS ML extension pack zip matching build_inpaint_pack.sh:
#   pack.json, models/<file>..., lib/libonnxruntime.dylib, LICENSE-*, NOTICE.txt
# Usage:
#   build_pack.sh <pack_id> <models_dir> <ort_dylib> <ort_licence> <notice_file> <out.zip> [version]
# Example:
#   tools/ml/build_pack.sh ocr /tmp/ocr_stage .../libonnxruntime.dylib .../LICENSE .../NOTICE.txt ocr-macos.zip 0.1.0
set -euo pipefail
if [ "$#" -lt 6 ]; then
   echo "usage: $0 <pack_id> <models_dir> <ort_dylib> <ort_licence> <notice_file> <out.zip> [version]" >&2
   exit 2
fi
PACK_ID="$1"
MODELS_DIR="$2"
ORT="$3"
ORTLIC="$4"
NOTICE="$5"
OUT="$6"
VER="${7:-0.1.0}"

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/models" "$W/lib"

cp "$MODELS_DIR"/*.onnx "$W/models/"
for extra in "$MODELS_DIR"/*.onnx_data "$MODELS_DIR"/*.txt "$MODELS_DIR"/*.json; do
   [ -f "$extra" ] && cp "$extra" "$W/models/"
done
for lic in "$MODELS_DIR"/LICENSE*; do
   [ -f "$lic" ] && cp "$lic" "$W/"
done
cp "$ORT" "$W/lib/libonnxruntime.dylib"
cp "$ORTLIC" "$W/LICENSE-onnxruntime"
cp "$NOTICE" "$W/NOTICE.txt"
printf '{"id":"%s","version":"%s"}\n' "$PACK_ID" "$VER" > "$W/pack.json"

(cd "$W" && rm -f "$OUT" && zip -qr -X "$OUT" .)
echo "$OUT $(du -h "$OUT" | cut -f1) sha256 $(shasum -a 256 "$OUT" | cut -d' ' -f1)"
