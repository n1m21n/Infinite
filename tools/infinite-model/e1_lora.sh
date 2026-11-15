#!/bin/sh
# E1 LoRA run on the M2 (plan section 8): one small SFT run on the train split, then the same harness as B2.
# Usage: tools/infinite-model/e1_lora.sh <hf-or-local model> <tag> [iters]
#   tools/infinite-model/e1_lora.sh mlx-community/Qwen3.5-0.8B-8bit 0.8b 600
# One heavy job at a time: never beside a build or a bench.
set -e
MODEL="$1"; TAG="$2"; ITERS="${3:-600}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/e1"
python3 "$ROOT/tools/infinite-model/e1_eval.py" --export-train
mkdir -p "$OUT/adapters"
python3 -m mlx_lm.lora --model "$MODEL" --train --data "$OUT/lora-data" \
  --adapter-path "$OUT/adapters/$TAG" --iters "$ITERS" --batch-size 4 --num-layers 8 \
  --learning-rate 1e-4 --mask-prompt --steps-per-eval 100 --save-every 200 --max-seq-length 1024 \
  2>&1 | tee "$OUT/adapters/lora-$TAG.log"
python3 "$ROOT/tools/infinite-model/e1_eval.py" --arm lora --model "$MODEL" --adapter "$OUT/adapters/$TAG" --tag "$TAG" \
  --compare "b2-$TAG" "lora-$TAG"
python3 "$ROOT/tools/infinite-model/e1_sheet.py" "lora-$TAG"
