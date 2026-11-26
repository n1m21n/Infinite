#!/bin/bash
# Builds the macOS Inpaint pack (opt-in MI-GAN for the 2D Remove tool): inpaint-macos.zip with pack.json, the
# MI-GAN 512 Places2 pipeline ONNX, its MIT licence, the training-data note, and the ONNX Runtime dylib.
# Licence check: internal/plans/2d-view/ai-tools-licences.md (weights MIT; Places2 is research/education only).
# usage: build_inpaint_pack.sh <migan_dir> <ort_dylib> <ort_licence> <out.zip> [version]
#   migan_dir: huggingface.co/andraniksargsyan/migan (the paper author's upload): migan_pipeline_v2.onnx + LICENSE
set -euo pipefail
MIGAN="$1"; ORT="$2"; ORTLIC="$3"; OUT="$4"; VER="${5:-0.1.0}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/models" "$W/lib"
cp "$MIGAN/migan_pipeline_v2.onnx" "$W/models/"
cp "$MIGAN/LICENSE" "$W/LICENSE-MI-GAN"
cp "$ORT" "$W/lib/libonnxruntime.dylib"
cp "$ORTLIC" "$W/LICENSE-onnxruntime"
cat > "$W/NOTICE.txt" <<'TXT'
MI-GAN (Picsart AI Research, MIT licence: LICENSE-MI-GAN). Sargsyan et al., "MI-GAN: A Simple Baseline for Image
Inpainting on Mobile Devices", ICCV 2023. https://github.com/Picsart-AI-Research/MI-GAN

These weights were trained on Places2 (places2.csail.mit.edu), which its authors offer for academic research and
education. Installing this pack is your choice; Infinite's Remove tool works without it.
TXT
printf '{"id":"inpaint","version":"%s"}\n' "$VER" > "$W/pack.json"
(cd "$W" && rm -f "$OUT" && zip -qr -X "$OUT" .)
echo "$OUT $(du -h "$OUT" | cut -f1) sha256 $(shasum -a 256 "$OUT" | cut -d' ' -f1)"
