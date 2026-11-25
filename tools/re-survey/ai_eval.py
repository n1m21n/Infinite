#!/usr/bin/env python3
"""AI selection eval (internal/plans/2d-view/ai-tools.md, sections 5-6).

  build   pick the eval sets from COCO val2017 and write masks + prompts
  models  run the pack's ONNX models (SAM 2.1 tiny, BEN2) in Python and score them
  app     score masks written by the app's INFINITE_AIEVAL=1 hook
  report  print the table for every result file present

Sets (under --data, default ~/Infinite/datasets/ai-eval):
  S1  60 COCO val objects, one per picture, area 2-60% of the frame,
      prompts: box (the COCO box) and click (the deepest point inside the mask)
  S2  30 COCO val pictures with one main person or animal (>= 15% of frame)

Scores are IoU against the COCO mask (M1) and wall time per call (M2).
Run with the eval venv: ~/Infinite/tools/.aievalvenv/bin/python -I ai_eval.py ...
"""
import argparse
import json
import os
import random
import sys
import time
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

DATA = Path(os.path.expanduser("~/Infinite/datasets/ai-eval"))
PACK = Path(os.path.expanduser("~/Library/Application Support/Infinite/extensions/select/models"))
SUBJECT_CATS = {"person", "dog", "cat", "horse", "bird", "cow", "sheep", "elephant", "bear", "zebra", "giraffe"}
MEAN = np.array([0.485, 0.456, 0.406], np.float32)
STD = np.array([0.229, 0.224, 0.225], np.float32)


# ---------------------------------------------------------------- build
def poly_mask(segs, w, h):
   im = Image.new("L", (w, h), 0)
   d = ImageDraw.Draw(im)
   for s in segs:
      if len(s) >= 6:
         d.polygon([(s[i], s[i + 1]) for i in range(0, len(s), 2)], fill=255)
   return np.array(im) > 127


def deepest_point(m):
   """Point inside the mask farthest from its edge (repeated 4-neighbour erosion)."""
   cur = m.copy()
   last = cur
   while cur.any():
      last = cur
      e = cur.copy()
      e[1:, :] &= cur[:-1, :]
      e[:-1, :] &= cur[1:, :]
      e[:, 1:] &= cur[:, :-1]
      e[:, :-1] &= cur[:, 1:]
      cur = e
   ys, xs = np.nonzero(last)
   i = len(xs) // 2
   return int(xs[i]), int(ys[i])


def fetch(url, dst):
   if dst.exists():
      return
   with urllib.request.urlopen(url, timeout=60) as r:
      dst.write_bytes(r.read())


def cmd_build(a):
   coco = json.loads((a.data / "coco/annotations/instances_val2017.json").read_text())
   cats = {c["id"]: c["name"] for c in coco["categories"]}
   imgs = {i["id"]: i for i in coco["images"]}
   by_img = {}
   for an in coco["annotations"]:
      by_img.setdefault(an["image_id"], []).append(an)
   rng = random.Random(20261125)
   ids = sorted(by_img)
   rng.shuffle(ids)
   s1, s2 = [], []
   for iid in ids:
      im = imgs[iid]
      area = im["width"] * im["height"]
      anns = [x for x in by_img[iid] if not x["iscrowd"] and isinstance(x["segmentation"], list)]
      if not anns:
         continue
      if len(s1) < 60:
         ok = [x for x in anns if 0.02 <= x["area"] / area <= 0.60]
         if ok:
            s1.append((im, rng.choice(ok)))
            continue
      if len(s2) < 30:
         subj = [x for x in anns if cats[x["category_id"]] in SUBJECT_CATS]
         big = sorted(subj, key=lambda x: -x["area"])
         if big and big[0]["area"] / area >= 0.15 and (len(big) == 1 or big[1]["area"] < 0.25 * big[0]["area"]):
            s2.append((im, big[0]))
      if len(s1) >= 60 and len(s2) >= 30:
         break
   for name, rows in (("S1", s1), ("S2", s2)):
      d = a.data / name
      d.mkdir(parents=True, exist_ok=True)
      out = []
      for im, an in rows:
         stem = Path(im["file_name"]).stem
         fetch(im["coco_url"], d / f"{stem}.jpg")
         m = poly_mask(an["segmentation"], im["width"], im["height"])
         Image.fromarray((m * 255).astype(np.uint8)).save(d / f"{stem}_gt.png")
         x, y, bw, bh = an["bbox"]
         cx, cy = deepest_point(m)
         out.append({"id": stem, "w": im["width"], "h": im["height"], "cat": cats[an["category_id"]],
                     "box": [x, y, x + bw, y + bh], "click": [cx, cy], "licence": im["license"]})
      (d / "set.json").write_text(json.dumps(out, indent=1))
      # The app's INFINITE_AIEVAL hook reads this plain list: id x0 y0 x1 y1 cx cy
      (d / "list.txt").write_text("".join(f"{o['id']} {o['box'][0]:.1f} {o['box'][1]:.1f} {o['box'][2]:.1f} {o['box'][3]:.1f} "
                                          f"{o['click'][0]} {o['click'][1]}\n" for o in out))
      print(name, len(out), "items ->", d)


# ---------------------------------------------------------------- models
def load(path):
   import onnxruntime as ort
   prov = ["CoreMLExecutionProvider", "CPUExecutionProvider"] if os.environ.get("AIEVAL_COREML") else ["CPUExecutionProvider"]
   return ort.InferenceSession(str(path), providers=prov)


def sam_pre(rgb):
   x = np.asarray(Image.fromarray(rgb).resize((1024, 1024), Image.BILINEAR), np.float32) / 255.0
   x = (x - MEAN) / STD
   return x.transpose(2, 0, 1)[None].astype(np.float32)


def sam_decode(dec, emb, w, h, points=None, labels=None, box=None):
   sx, sy = 1024.0 / w, 1024.0 / h
   if points is None:
      pts = np.zeros((1, 1, 0, 2), np.float32)
      lab = np.zeros((1, 1, 0), np.int64)
   else:
      pts = np.array([[[[p[0] * sx, p[1] * sy] for p in points]]], np.float32)
      lab = np.array([[labels]], np.int64)
   bx = np.zeros((1, 0, 4), np.float32) if box is None else np.array([[[box[0] * sx, box[1] * sy, box[2] * sx, box[3] * sy]]], np.float32)
   feeds = {"input_points": pts, "input_labels": lab, "input_boxes": bx,
            "image_embeddings.0": emb[0], "image_embeddings.1": emb[1], "image_embeddings.2": emb[2]}
   iou, masks, _ = dec.run(None, feeds)
   k = int(np.argmax(iou[0, 0]))
   if box is None and points is not None and len(points) == 1:
      k = int(np.argmax([(masks[0, 0, j] > 0).sum() for j in range(masks.shape[2])]))   # lone click: whole object
   low = masks[0, 0, k]
   up = np.asarray(Image.fromarray(low.astype(np.float32), ).resize((w, h), Image.BILINEAR))
   return up > 0.0, float(iou[0, 0, k])


def ben2_mask(sess, rgb):
   h, w = rgb.shape[:2]
   x = np.asarray(Image.fromarray(rgb).resize((1024, 1024), Image.BILINEAR), np.float32) / 255.0
   out = sess.run(None, {sess.get_inputs()[0].name: x.transpose(2, 0, 1)[None]})[0][0, 0].astype(np.float32)
   up = np.asarray(Image.fromarray(out, ).resize((w, h), Image.BILINEAR))
   return up


def iou(a, b):
   u = np.logical_or(a, b).sum()
   return float(np.logical_and(a, b).sum() / u) if u else 1.0


def cmd_models(a):
   res = {"S1_box": [], "S1_click": [], "S2_ben2": [], "times": {}}
   enc, dec = load(PACK / "sam_enc.onnx"), load(PACK / "sam_dec.onnx")
   te, td = [], []
   for it in json.loads((a.data / "S1/set.json").read_text()):
      rgb = np.asarray(Image.open(a.data / "S1" / f"{it['id']}.jpg").convert("RGB"))
      gt = np.asarray(Image.open(a.data / "S1" / f"{it['id']}_gt.png")) > 127
      t = time.perf_counter()
      emb = enc.run(None, {"pixel_values": sam_pre(rgb)})
      te.append(time.perf_counter() - t)
      t = time.perf_counter()
      mb, _ = sam_decode(dec, emb, it["w"], it["h"], box=it["box"])
      td.append(time.perf_counter() - t)
      mc, _ = sam_decode(dec, emb, it["w"], it["h"], points=[it["click"]], labels=[1])
      res["S1_box"].append([it["id"], iou(mb, gt)])
      res["S1_click"].append([it["id"], iou(mc, gt)])
      if a.save:
         Image.fromarray((mb * 255).astype(np.uint8)).save(a.data / "S1" / f"{it['id']}_sam_box.png")
   res["times"]["sam_encoder_s"] = te
   res["times"]["sam_decoder_s"] = td
   ben = load(PACK / "ben2.onnx")
   tb = []
   for it in json.loads((a.data / "S2/set.json").read_text()):
      rgb = np.asarray(Image.open(a.data / "S2" / f"{it['id']}.jpg").convert("RGB"))
      gt = np.asarray(Image.open(a.data / "S2" / f"{it['id']}_gt.png")) > 127
      t = time.perf_counter()
      m = ben2_mask(ben, rgb)
      tb.append(time.perf_counter() - t)
      res["S2_ben2"].append([it["id"], iou(m > 0.5, gt)])
      if a.save:
         Image.fromarray(np.clip(m * 255, 0, 255).astype(np.uint8)).save(a.data / "S2" / f"{it['id']}_ben2.png")
   res["times"]["ben2_s"] = tb
   (a.data / "results").mkdir(exist_ok=True)
   (a.data / "results/models.json").write_text(json.dumps(res, indent=1))
   cmd_report(a)


def cmd_app(a):
   """The app's hook writes <set>/<id>_<tag>.png masks and results/app_<tag>.json timings."""
   res = {}
   for name, tags in (("S1", ("classic_box", "app_sam_box", "app_sam_click")), ("S2", ("classic_subject", "app_subject"))):
      for it in json.loads((a.data / name / "set.json").read_text()):
         gt = np.asarray(Image.open(a.data / name / f"{it['id']}_gt.png")) > 127
         for tag in tags:
            p = a.data / name / f"{it['id']}_{tag}.png"
            if p.exists():
               m = np.asarray(Image.open(p).convert("L")) > 127
               res.setdefault(f"{name}_{tag}", []).append([it["id"], iou(m, gt)])
   (a.data / "results").mkdir(exist_ok=True)
   (a.data / "results/app.json").write_text(json.dumps(res, indent=1))
   cmd_report(a)


def stats(v):
   v = np.array(v)
   return f"mean {v.mean():.3f}  median {np.median(v):.3f}  worst {v.min():.3f}  n {len(v)}"


def cmd_report(a):
   for f in sorted((a.data / "results").glob("*.json")):
      r = json.loads(f.read_text())
      print(f"== {f.name}")
      for k, v in r.items():
         if k == "times":
            for tk, tv in v.items():
               tv = np.array(tv) * 1000
               print(f"  {tk:22s} p50 {np.median(tv):7.1f} ms  p95 {np.percentile(tv, 95):7.1f} ms")
         elif v:
            print(f"  {k:22s} IoU {stats([x[1] for x in v])}")


def main():
   ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
   ap.add_argument("cmd", choices=["build", "models", "app", "report"])
   ap.add_argument("--data", type=Path, default=DATA)
   ap.add_argument("--save", action="store_true", help="write model masks next to the pictures")
   a = ap.parse_args()
   {"build": cmd_build, "models": cmd_models, "app": cmd_app, "report": cmd_report}[a.cmd](a)


if __name__ == "__main__":
   sys.exit(main())
