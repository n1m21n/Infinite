#!/usr/bin/env python3
"""Phase 3 Python ONNX proof & contact sheet generator for Block 5A 2D ML models.

Evaluates on M2 CPU (Core ML EP off per ai-tools.md §16):
  - MODNet (modnet_fp16.onnx) on S2 (30 COCO subjects + person portrait subset)
  - PP-OCRv4 mobile (ppocr_det.onnx + ppocr_rec.onnx) on S6_ocr (20 seeded text posters)
  - D-FINE-S COCO (dfine_s_coco_fp16.onnx) on S1 (60 COCO val objects)

Writes JSON results and visual contact sheets to ~/Infinite/datasets/ai-eval/results/.
Usage:
  ~/Infinite/tools/.aievalvenv/bin/python -I tools/re-survey/ml_models_eval.py
"""
import gc
import json
import os
import random
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageDraw, ImageFont

DATA = Path(os.path.expanduser("~/Infinite/datasets/ai-eval"))
STAGE = DATA / "ml-models-onnx"
RESULTS = DATA / "results"
RESULTS.mkdir(parents=True, exist_ok=True)

MEAN = np.array([0.485, 0.456, 0.406], np.float32)
STD = np.array([0.229, 0.224, 0.225], np.float32)


def rss_mb():
   return int(subprocess.run(["ps", "-o", "rss=", "-p", str(os.getpid())], capture_output=True, text=True).stdout.strip()) / 1024.0


def iou_mask(a, b):
   u = np.logical_or(a, b).sum()
   return float(np.logical_and(a, b).sum() / u) if u else 1.0


def iou_box(a, b):
   x0 = max(a[0], b[0]); y0 = max(a[1], b[1])
   x1 = min(a[2], b[2]); y1 = min(a[3], b[3])
   inter = max(0.0, x1 - x0) * max(0.0, y1 - y0)
   aa = max(0.0, a[2] - a[0]) * max(0.0, a[3] - a[1])
   bb = max(0.0, b[2] - b[0]) * max(0.0, b[3] - b[1])
   u = aa + bb - inter
   return inter / u if u > 0 else 0.0


def make_sheet(tiles, cols, tile_w, tile_h, out_path):
   rows = (len(tiles) + cols - 1) // cols
   sheet = Image.new("RGB", (cols * tile_w, rows * tile_h), (20, 22, 26))
   for idx, im in enumerate(tiles):
      r, c = divmod(idx, cols)
      sheet.paste(im.resize((tile_w, tile_h), Image.BILINEAR), (c * tile_w, r * tile_h))
   sheet.save(out_path)


# ---------------------------------------------------------------- 1. MODNet
def eval_modnet():
   print("\n=== 1. MODNet (modnet_fp16.onnx, 512x512, CPU) ===")
   gc.collect()
   r0 = rss_mb()
   sess = ort.InferenceSession(str(STAGE / "modnet_fp16.onnx"), providers=["CPUExecutionProvider"])
   rss_delta = rss_mb() - r0
   s2 = json.loads((DATA / "S2/set.json").read_text())
   all_ious, person_ious, times = [], [], []
   tiles = []
   for it in s2:
      im = Image.open(DATA / "S2" / f"{it['id']}.jpg").convert("RGB")
      gt = np.asarray(Image.open(DATA / "S2" / f"{it['id']}_gt.png")) > 127
      x = np.asarray(im.resize((512, 512), Image.BILINEAR), np.float32) / 255.0
      x = (x - 0.5) / 0.5
      inp = x.transpose(2, 0, 1)[None].astype(np.float32)
      t0 = time.perf_counter()
      matte = sess.run(None, {"input": inp})[0][0, 0]
      times.append((time.perf_counter() - t0) * 1000.0)
      up = np.asarray(Image.fromarray(np.clip(matte * 255, 0, 255).astype(np.uint8)).resize((it["w"], it["h"]), Image.BILINEAR)) / 255.0
      sc = iou_mask(up > 0.5, gt)
      all_ious.append(sc)
      if it["cat"] == "person":
         person_ious.append(sc)
      if len(tiles) < 12:
         # Side-by-side composite: input | matte
         comp = Image.new("RGB", (it["w"] * 2, it["h"]))
         comp.paste(im, (0, 0))
         comp.paste(Image.fromarray((up * 255).astype(np.uint8)).convert("RGB"), (it["w"], 0))
         tiles.append(comp)
   make_sheet(tiles, 3, 360, 160, RESULTS / "modnet_sheet.png")
   del sess
   gc.collect()
   out = {
      "model": "modnet_fp16",
      "s2_all_iou_mean": float(np.mean(all_ious)),
      "s2_all_iou_median": float(np.median(all_ious)),
      "s2_person_iou_mean": float(np.mean(person_ious)) if person_ious else 0.0,
      "s2_person_n": len(person_ious),
      "p50_ms": float(np.median(times)),
      "p95_ms": float(np.percentile(times, 95)),
      "rss_delta_mb": float(rss_delta),
      "sheet": str(RESULTS / "modnet_sheet.png"),
   }
   print(json.dumps(out, indent=2))
   return out


# ---------------------------------------------------------------- 2. PP-OCRv4
def build_s6_ocr():
   d = DATA / "S6_ocr"
   d.mkdir(parents=True, exist_ok=True)
   meta_path = d / "set.json"
   rng = random.Random(20261127)
   words = [
      ("INFINITE STUDIO", "LIVE 2026"),
      ("AUDIO ENGINE", "48 KHZ DSP"),
      ("OPEN SOURCE", "MIT LICENSE"),
      ("SYNTHESIS", "MODULATION"),
      ("COLOR GRADE", "LOOK STACK"),
      ("VECTOR TRACE", "BEZIER PATH"),
      ("SELECT TEXT", "TYPE LAYER"),
      ("DARK MATTER", "SIGNAL FLOW"),
      ("FRAME RATE", "SIXTY FPS"),
      ("STEREO MIX", "MASTER BUS"),
      ("PATCH CABLE", "NODE GRAPH"),
      ("LIGHT FIELD", "DEPTH BLUR"),
      ("BRUSH ENGINE", "RELIEF MAP"),
      ("NOISE FLOOR", "ANALOG WARM"),
      ("TIMELINE", "ARRANGEMENT"),
      ("EXPORT MOV", "H264 AUDIO"),
      ("CAMERA 3D", "PERSPECTIVE"),
      ("MASK REFINE", "EDGE ALPHA"),
      ("SAMPLE RATE", "ZERO XRUNS"),
      ("CLEAN ROOM", "LOCAL MODEL"),
   ]
   font_paths = ["/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"]
   fp = next((p for p in font_paths if os.path.exists(p)), None)
   items = []
   for i, (line1, line2) in enumerate(words):
      W, H = 512, 320
      bg = tuple(rng.randrange(18, 60) for _ in range(3)) if i % 2 == 0 else tuple(rng.randrange(210, 248) for _ in range(3))
      fg = (240, 244, 248) if i % 2 == 0 else (20, 24, 30)
      im = Image.new("RGB", (W, H), bg)
      dr = ImageDraw.Draw(im)
      # Add a subtle decorative accent box
      ac = tuple(rng.randrange(70, 190) for _ in range(3))
      dr.rectangle([20, 20, 492, 300], outline=ac, width=3)
      f1 = ImageFont.truetype(fp, rng.randrange(30, 44)) if fp else ImageFont.load_default()
      f2 = ImageFont.truetype(fp, rng.randrange(22, 32)) if fp else ImageFont.load_default()
      x1, y1 = rng.randrange(40, 100), rng.randrange(55, 110)
      x2, y2 = rng.randrange(40, 130), y1 + rng.randrange(65, 95)
      dr.text((x1, y1), line1, fill=fg, font=f1)
      dr.text((x2, y2), line2, fill=fg, font=f2)
      b1 = list(dr.textbbox((x1, y1), line1, font=f1))
      b2 = list(dr.textbbox((x2, y2), line2, font=f2))
      p = d / f"ocr_{i:02d}.png"
      im.save(p)
      items.append({"id": f"ocr_{i:02d}", "w": W, "h": H, "lines": [{"text": line1, "box": b1}, {"text": line2, "box": b2}]})
   meta_path.write_text(json.dumps(items, indent=1))
   return items


def ctc_decode(probs, keys):
   # probs: [T, C]
   idx = probs.argmax(axis=1)
   chars = []
   prev = 0
   for k in idx:
      k = int(k)
      if k != 0 and k != prev:
         if k - 1 < len(keys):
            chars.append(keys[k - 1])
         elif k - 1 == len(keys):
            chars.append(" ")
      prev = k
   return "".join(chars).strip()


def edit_dist(a, b):
   dp = list(range(len(b) + 1))
   for i, ca in enumerate(a, 1):
      ndp = [i] + [0] * len(b)
      for j, cb in enumerate(b, 1):
         ndp[j] = min(dp[j] + 1, ndp[j - 1] + 1, dp[j - 1] + (ca != cb))
      dp = ndp
   return dp[-1]


def connected_boxes(prob, thr=0.3, min_area=30):
   # Simple 4-connected component bounding boxes on probability map (no cv2 needed)
   h, w = prob.shape
   mask = prob > thr
   vis = np.zeros_like(mask, dtype=bool)
   boxes = []
   ys, xs = np.nonzero(mask)
   for y0, x0 in zip(ys, xs):
      if vis[y0, x0]:
         continue
      stack = [(int(y0), int(x0))]
      vis[y0, x0] = True
      mnx = mxx = int(x0); mny = mxy = int(y0); area = 0
      while stack:
         cy, cx = stack.pop()
         area += 1
         if cx < mnx: mnx = cx
         if cx > mxx: mxx = cx
         if cy < mny: mny = cy
         if cy > mxy: mxy = cy
         for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not vis[ny, nx]:
               vis[ny, nx] = True
               stack.append((ny, nx))
      if area >= min_area:
         # DBNet unclip expansion (~4 px padding)
         pad_x = max(4, int((mxy - mny) * 0.25))
         pad_y = max(4, int((mxy - mny) * 0.28))
         boxes.append([max(0, mnx - pad_x), max(0, mny - pad_y), min(w - 1, mxx + pad_x), min(h - 1, mxy + pad_y)])
   return boxes


def eval_ppocr():
   print("\n=== 2. PP-OCRv4 Mobile (ppocr_det + ppocr_rec, CPU) ===")
   items = build_s6_ocr()
   keys = (STAGE / "ppocr_keys_v1.txt").read_text(encoding="utf-8").splitlines()
   gc.collect()
   r0 = rss_mb()
   det = ort.InferenceSession(str(STAGE / "ppocr_det.onnx"), providers=["CPUExecutionProvider"])
   rec = ort.InferenceSession(str(STAGE / "ppocr_rec.onnx"), providers=["CPUExecutionProvider"])
   rss_delta = rss_mb() - r0

   tp = fp = fn = 0
   cer_num = cer_den = 0
   times = []
   tiles = []

   for it in items:
      im = Image.open(DATA / "S6_ocr" / f"{it['id']}.png").convert("RGB")
      w, h = im.size
      # Pad/resize to 32-multiple (512x320 is already divisible by 32!)
      arr = np.asarray(im, np.float32) / 255.0
      # PP-OCR det expects BGR ImageNet-normalized
      bgr = arr[:, :, ::-1]
      inp = ((bgr - MEAN) / STD).transpose(2, 0, 1)[None].astype(np.float32)

      t0 = time.perf_counter()
      pmap = det.run(None, {"x": inp})[0][0, 0]
      boxes = connected_boxes(pmap, thr=0.3, min_area=40)
      rec_texts = []
      for bx in boxes:
         x0, y0, x1, y1 = bx
         if x1 - x0 < 6 or y1 - y0 < 6:
            continue
         crop = im.crop((x0, y0, x1, y1))
         cw, ch = crop.size
         rw = max(32, min(320, int(round(cw * (48.0 / max(1, ch))))))
         c_arr = np.asarray(crop.resize((rw, 48), Image.BILINEAR), np.float32) / 255.0
         c_bgr = (c_arr[:, :, ::-1] - 0.5) / 0.5
         c_inp = c_bgr.transpose(2, 0, 1)[None].astype(np.float32)
         probs = rec.run(None, {"x": c_inp})[0][0]
         txt = ctc_decode(probs, keys)
         rec_texts.append((bx, txt))
      times.append((time.perf_counter() - t0) * 1000.0)

      # Match predicted boxes to GT lines
      gt_matched = [False] * len(it["lines"])
      for bx, txt in rec_texts:
         best_j, best_iou = -1, 0.0
         for j, gl in enumerate(it["lines"]):
            v = iou_box(bx, gl["box"])
            if v > best_iou:
               best_iou, best_j = v, j
         if best_iou >= 0.45 and not gt_matched[best_j]:
            tp += 1
            gt_matched[best_j] = True
            gt_s = it["lines"][best_j]["text"].upper().replace(" ", "")
            pr_s = txt.upper().replace(" ", "")
            cer_num += edit_dist(pr_s, gt_s)
            cer_den += max(1, len(gt_s))
         else:
            fp += 1
      fn += sum(1 for m in gt_matched if not m)

      if len(tiles) < 9:
         vis = im.copy()
         dr = ImageDraw.Draw(vis)
         for bx, txt in rec_texts:
            dr.rectangle(bx, outline=(86, 220, 130), width=2)
            dr.text((bx[0], max(0, bx[1] - 12)), txt[:24], fill=(86, 220, 130))
         tiles.append(vis)

   make_sheet(tiles, 3, 360, 225, RESULTS / "ppocr_sheet.png")
   del det, rec
   gc.collect()
   prec = tp / max(1, tp + fp)
   recl = tp / max(1, tp + fn)
   f1 = 2 * prec * recl / max(1e-9, prec + recl)
   out = {
      "model": "ppocr_v4_mobile",
      "det_precision": prec,
      "det_recall": recl,
      "det_f1": f1,
      "rec_cer": cer_num / max(1, cer_den),
      "p50_ms": float(np.median(times)),
      "p95_ms": float(np.percentile(times, 95)),
      "rss_delta_mb": float(rss_delta),
      "sheet": str(RESULTS / "ppocr_sheet.png"),
   }
   print(json.dumps(out, indent=2))
   return out


# ---------------------------------------------------------------- 3. D-FINE-S
def eval_dfine():
   print("\n=== 3. D-FINE-S COCO (dfine_s_coco_fp16.onnx, 640x640, CPU) ===")
   gc.collect()
   r0 = rss_mb()
   sess = ort.InferenceSession(str(STAGE / "dfine_s_coco_fp16.onnx"), providers=["CPUExecutionProvider"])
   rss_delta = rss_mb() - r0
   s1 = json.loads((DATA / "S1/set.json").read_text())
   hits, best_ious, times = 0, [], []
   tiles = []
   for it in s1:
      im = Image.open(DATA / "S1" / f"{it['id']}.jpg").convert("RGB")
      w, h = it["w"], it["h"]
      x = np.asarray(im.resize((640, 640), Image.BILINEAR), np.float32) / 255.0
      inp = x.transpose(2, 0, 1)[None].astype(np.float32)
      t0 = time.perf_counter()
      logits, boxes = sess.run(None, {"pixel_values": inp})
      times.append((time.perf_counter() - t0) * 1000.0)
      # sigmoid scores over 80 COCO classes
      scores = 1.0 / (1.0 + np.exp(-np.clip(logits[0], -20, 20)))
      max_scores = scores.max(axis=1)
      keep = max_scores >= 0.35
      pred_b = boxes[0][keep]
      # cx, cy, bw, bh -> x0, y0, x1, y1 in image pixels
      px0 = (pred_b[:, 0] - pred_b[:, 2] * 0.5) * w
      py0 = (pred_b[:, 1] - pred_b[:, 3] * 0.5) * h
      px1 = (pred_b[:, 0] + pred_b[:, 2] * 0.5) * w
      py1 = (pred_b[:, 1] + pred_b[:, 3] * 0.5) * h
      gt_b = it["box"]
      bi = 0.0
      for b in zip(px0, py0, px1, py1):
         bi = max(bi, iou_box(b, gt_b))
      best_ious.append(bi)
      if bi >= 0.5:
         hits += 1
      if len(tiles) < 12:
         vis = im.copy()
         dr = ImageDraw.Draw(vis)
         dr.rectangle(gt_b, outline=(240, 180, 50), width=2)
         for b in zip(px0, py0, px1, py1):
            dr.rectangle(list(b), outline=(86, 168, 245), width=2)
         tiles.append(vis)

   make_sheet(tiles, 4, 280, 200, RESULTS / "dfine_sheet.png")
   del sess
   gc.collect()
   out = {
      "model": "dfine_s_coco_fp16",
      "s1_recall_at_0_5": hits / max(1, len(s1)),
      "s1_best_box_iou_mean": float(np.mean(best_ious)),
      "s1_n": len(s1),
      "p50_ms": float(np.median(times)),
      "p95_ms": float(np.percentile(times, 95)),
      "rss_delta_mb": float(rss_delta),
      "sheet": str(RESULTS / "dfine_sheet.png"),
   }
   print(json.dumps(out, indent=2))
   return out


def main():
   res = {
      "modnet": eval_modnet(),
      "ppocr": eval_ppocr(),
      "dfine": eval_dfine(),
   }
   out_file = RESULTS / "block5a_models.json"
   out_file.write_text(json.dumps(res, indent=2))
   print("\nSaved results ->", out_file)


if __name__ == "__main__":
   main()
