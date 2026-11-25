#!/usr/bin/env python3
"""AI selection eval (internal/plans/2d-view/ai-tools.md, sections 5-6).

  build   pick the eval sets from COCO val2017 and write masks + prompts
  models  run the pack's ONNX models (SAM 2.1 tiny, BEN2) in Python and score them
  app     score masks written by the app's INFINITE_AIEVAL=1 hook
  ep      EP gate (section 15): CoreML vs CPU masks on S1/S2 must agree (IoU >= 0.99 per picture)
          and loading the CoreML sessions must add <= 600 MB of resident memory
  s4      trace baseline (ai-tools step 2.0): make 20 own logos / line art + 10 own photo-like
          pictures, trace each with VTracer and potrace, score SSIM (grey, against the picture),
          path count and time -> results/s4_baseline.json
          (needs vtracer, potracer, cairosvg, scikit-image in the venv and
           DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib for cairo)
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
def load(path, coreml=None):
   import onnxruntime as ort
   if coreml is None:
      coreml = bool(os.environ.get("AIEVAL_COREML"))
   prov = ["CoreMLExecutionProvider", "CPUExecutionProvider"] if coreml else ["CPUExecutionProvider"]
   return ort.InferenceSession(str(path), providers=prov)


def rss_mb():
   """Current resident memory of this process in MB (ps, so it is the live figure, not the peak)."""
   import subprocess
   return int(subprocess.run(["ps", "-o", "rss=", "-p", str(os.getpid())], capture_output=True, text=True).stdout.strip()) / 1024.0


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


def cmd_ep(a):
   """Section 15 EP gate: the CoreML execution provider must give the CPU answer (mask IoU >= 0.99 on every
   picture) and cost at most 600 MB of resident memory to load. Fails with exit code 1 when either bar is missed."""
   # One model set in memory at a time (the 8 GB Mac cannot hold CPU and CoreML copies together): the CPU answers
   # first, then each CoreML model alone, its load cost measured on its own; the bar is their sum (both open).
   import gc
   s1 = json.loads((a.data / "S1/set.json").read_text())[: a.limit]
   s2 = json.loads((a.data / "S2/set.json").read_text())[: a.limit]
   rgb1 = {it["id"]: np.asarray(Image.open(a.data / "S1" / f"{it['id']}.jpg").convert("RGB")) for it in s1}
   rgb2 = {it["id"]: np.asarray(Image.open(a.data / "S2" / f"{it['id']}.jpg").convert("RGB")) for it in s2}
   # The SAM decoder stays on CPU: the CoreML EP refuses its zero-sized prompt tensors (a box with no clicks), and it
   # is a few ms anyway. The app opens every model on CPU today (AiSelect.cpp OpenModel(..., false, ...)).
   dec = load(PACK / "sam_dec.onnx", False)
   cpu = {}
   enc = load(PACK / "sam_enc.onnx", False)
   for it in s1:
      cpu["S1_" + it["id"]], _ = sam_decode(dec, enc.run(None, {"pixel_values": sam_pre(rgb1[it["id"]])}), it["w"], it["h"], box=it["box"])
   del enc
   ben = load(PACK / "ben2.onnx", False)
   for it in s2:
      cpu["S2_" + it["id"]] = ben2_mask(ben, rgb2[it["id"]]) > 0.5
   del ben
   gc.collect()
   agree, rss = [], 0.0
   before = rss_mb()
   enc = load(PACK / "sam_enc.onnx", True)
   rss += rss_mb() - before
   for it in s1:
      mm, _ = sam_decode(dec, enc.run(None, {"pixel_values": sam_pre(rgb1[it["id"]])}), it["w"], it["h"], box=it["box"])
      agree.append(["S1_" + it["id"], iou(cpu["S1_" + it["id"]], mm)])
   del enc
   gc.collect()
   before = rss_mb()
   ben = load(PACK / "ben2.onnx", True)
   rss += rss_mb() - before
   for it in s2:
      agree.append(["S2_" + it["id"], iou(cpu["S2_" + it["id"]], ben2_mask(ben, rgb2[it["id"]]) > 0.5)])
   del ben
   worst = min(agree, key=lambda r: r[1])
   ok = worst[1] >= 0.99 and rss <= 600.0
   (a.data / "results").mkdir(exist_ok=True)
   (a.data / "results/ep.json").write_text(json.dumps({"EP_agree": agree, "rss_delta_mb": rss, "pass": ok}, indent=1))
   print(f"EP gate: worst CPU/CoreML IoU {worst[1]:.4f} ({worst[0]}), {len(agree)} pictures; CoreML load +{rss:.0f} MB RSS"
         f" -> {'PASS' if ok else 'FAIL'}")
   return 0 if ok else 1


# ---------------------------------------------------------------- S4 trace baseline
def s4_pictures(dst):
   """30 own pictures: 0-9 flat logos, 10-19 line art, 20-29 photo-like. Seeded, so the set is fixed."""
   from PIL import ImageFilter
   dst.mkdir(parents=True, exist_ok=True)
   rng = random.Random(4)
   S = 384
   out = []
   for i in range(30):
      path = dst / f"s4_{i:02d}.png"
      out.append(path)
      if path.exists():
         continue
      if i < 10:
         bg = tuple(rng.randrange(200, 256) for _ in range(3))
         im = Image.new("RGB", (S, S), bg)
         d = ImageDraw.Draw(im)
         pal = [tuple(rng.randrange(0, 200) for _ in range(3)) for _ in range(rng.randrange(2, 5))]
         for _ in range(rng.randrange(3, 7)):
            c = rng.choice(pal)
            x, y, r = rng.randrange(40, S - 40), rng.randrange(40, S - 40), rng.randrange(20, 110)
            k = rng.randrange(3)
            if k == 0:
               d.ellipse([x - r, y - r, x + r, y + r], fill=c)
            elif k == 1:
               d.rounded_rectangle([x - r, y - r // 2, x + r, y + r // 2], radius=r // 4, fill=c)
            else:
               n = rng.randrange(3, 8)
               pts = [(x + r * np.cos(2 * np.pi * j / n), y + r * np.sin(2 * np.pi * j / n)) for j in range(n)]
               d.polygon(pts, fill=c)
      elif i < 20:
         im = Image.new("RGB", (S, S), (255, 255, 255))
         d = ImageDraw.Draw(im)
         for _ in range(rng.randrange(6, 14)):
            pts = [(rng.randrange(20, S - 20), rng.randrange(20, S - 20)) for _ in range(rng.randrange(2, 6))]
            d.line(pts, fill=(0, 0, 0), width=rng.randrange(2, 9), joint="curve")
         for _ in range(rng.randrange(1, 4)):
            x, y, r = rng.randrange(60, S - 60), rng.randrange(60, S - 60), rng.randrange(15, 60)
            d.ellipse([x - r, y - r, x + r, y + r], outline=(0, 0, 0), width=rng.randrange(2, 6))
      else:
         yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
         a = np.stack([rng.random() * xx + rng.random() * yy + rng.random() * 0.3 for _ in range(3)], -1)
         im = Image.fromarray(np.clip(a / a.max() * 255, 0, 255).astype(np.uint8))
         d = ImageDraw.Draw(im)
         for _ in range(rng.randrange(4, 9)):
            c = tuple(rng.randrange(0, 256) for _ in range(3))
            x, y, r = rng.randrange(0, S), rng.randrange(0, S), rng.randrange(30, 140)
            d.ellipse([x - r, y - r, x + r, y + r], fill=c)
         im = im.filter(ImageFilter.GaussianBlur(6))
         n = np.random.default_rng(i).normal(0, 6, (S, S, 3))
         im = Image.fromarray(np.clip(np.asarray(im, np.float32) + n, 0, 255).astype(np.uint8))
      im.save(path)
   return out


def s4_raster(svg, w, h):
   import io
   import cairosvg
   png = cairosvg.svg2png(bytestring=svg.encode(), output_width=w, output_height=h, background_color="white")
   return np.asarray(Image.open(io.BytesIO(png)).convert("L"), np.float32)


def s4_potrace_svg(grey):
   import potrace
   bm = potrace.Bitmap(grey >= 128)  # potracer inverts: pass the light pixels to trace the dark ones
   plist = bm.trace(turdsize=2, alphamax=1.0, opticurve=True, opttolerance=0.2)
   h, w = grey.shape
   parts = []
   for curve in plist:
      sp = curve.start_point
      d = [f"M{sp.x:.2f},{sp.y:.2f}"]
      for seg in curve.segments:
         if seg.is_corner:
            d.append(f"L{seg.c.x:.2f},{seg.c.y:.2f}L{seg.end_point.x:.2f},{seg.end_point.y:.2f}")
         else:
            d.append(f"C{seg.c1.x:.2f},{seg.c1.y:.2f} {seg.c2.x:.2f},{seg.c2.y:.2f} {seg.end_point.x:.2f},{seg.end_point.y:.2f}")
      parts.append("".join(d) + "Z")
   svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
          f'<path fill="black" fill-rule="evenodd" d="{"".join(parts)}"/></svg>')
   return svg, len(plist)


def cmd_s4(a):
   import vtracer
   from skimage.metrics import structural_similarity
   pics = s4_pictures(a.data / "s4")
   kinds = ["logo"] * 10 + ["line"] * 10 + ["photo"] * 10
   res = {"vtracer": [], "potrace": []}
   for path, kind in zip(pics, kinds):
      im = Image.open(path).convert("RGB")
      w, h = im.size
      grey = np.asarray(im.convert("L"), np.float32)
      t = time.perf_counter()
      svg = vtracer.convert_pixels_to_svg(list(im.convert("RGBA").getdata()), size=(w, h), colormode="color")
      dt = time.perf_counter() - t
      n = svg.count("<path")
      ss = structural_similarity(grey, s4_raster(svg, w, h), data_range=255)
      res["vtracer"].append([path.name, kind, float(ss), n, dt])
      t = time.perf_counter()
      svg, n = s4_potrace_svg(grey)
      dt = time.perf_counter() - t
      ss = structural_similarity(grey, s4_raster(svg, w, h), data_range=255)
      res["potrace"].append([path.name, kind, float(ss), n, dt])
      print(f"{path.name} {kind:5s}  vtracer SSIM {res['vtracer'][-1][2]:.3f} paths {res['vtracer'][-1][3]:4d}"
            f"  potrace SSIM {ss:.3f} paths {n:4d}")
   out = a.data / "results" / "s4_baseline.json"
   out.parent.mkdir(parents=True, exist_ok=True)
   out.write_text(json.dumps({"trace": res}, indent=1))
   s4_print(res)
   return 0


def s4_print(res):
   for tool, rows in res.items():
      for kind in ("logo", "line", "photo"):
         r = [x for x in rows if x[1] == kind]
         if r:
            print(f"  {tool:8s} {kind:5s} SSIM mean {np.mean([x[2] for x in r]):.3f}  paths median "
                  f"{np.median([x[3] for x in r]):6.0f}  time p50 {np.median([x[4] for x in r]) * 1000:7.1f} ms")


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
         elif k == "trace":
            s4_print(v)
         elif isinstance(v, list) and v:
            print(f"  {k:22s} IoU {stats([x[1] for x in v])}")


def main():
   ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
   ap.add_argument("cmd", choices=["build", "models", "app", "ep", "s4", "report"])
   ap.add_argument("--data", type=Path, default=DATA)
   ap.add_argument("--save", action="store_true", help="write model masks next to the pictures")
   ap.add_argument("--limit", type=int, default=None, help="ep: only the first N pictures of each set")
   a = ap.parse_args()
   return {"build": cmd_build, "models": cmd_models, "app": cmd_app, "ep": cmd_ep, "s4": cmd_s4, "report": cmd_report}[a.cmd](a)


if __name__ == "__main__":
   sys.exit(main())
