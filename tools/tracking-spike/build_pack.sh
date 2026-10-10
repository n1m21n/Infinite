#!/bin/bash
# Builds the macOS Tracking pack: tracking-macos.zip with pack.json, the two
# hand, face and pose ONNX models and the official ONNX Runtime dylib.
# usage: build_pack.sh <models_dir> <ort_dir> <out.zip> [version]
set -euo pipefail
MODELS="$1"; ORT="$2"; OUT="$3"; VER="${4:-0.1.0}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/models" "$W/lib"
for m in hand_detector hand_landmarks_detector face_detector face_landmarks_detector pose_landmarks_detector; do
   cp "$MODELS/$m.onnx" "$W/models/"
done
cp "$ORT/lib/libonnxruntime.1.20.1.dylib" "$W/lib/libonnxruntime.dylib"
cp "$ORT/LICENSE" "$W/LICENSE-onnxruntime" 2>/dev/null || true
printf '{"id":"tracking","version":"%s"}\n' "$VER" > "$W/pack.json"
(cd "$W" && rm -f "$OUT" && zip -qr -X "$OUT" .)
echo "$OUT $(du -h "$OUT" | cut -f1) sha256 $(shasum -a 256 "$OUT" | cut -d' ' -f1)"
