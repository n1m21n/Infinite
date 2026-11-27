#!/usr/bin/env python3
"""Render-trained ML data engine (internal/plans/2d-view/render-trained/PLAN.md §8).

Generates clean-room training pairs directly from Infinite's headless 3D renderer
(`Infinite --frame --batch --lenient`) with 2x supersampling for soft alpha edges:
  - composite.png  (512x512 RGB subject composited over clean background)
  - alpha.png      (512x512 L soft alpha matte from 2x supersampled mask pass)
  - depth.png      (512x512 L normalized depth)
  - normal.png     (512x512 RGB world/view-space surface normals)
  - DATA.md        (full provenance & licence ledger)
  - sample_sheet.png (visual contact sheet: Composite | Alpha | Depth | Normal)

Usage:
  ~/Infinite/tools/.aievalvenv/bin/python tools/render-trained/make_data.py \
      --bin build-dev/Infinite.app/Contents/MacOS/Infinite \
      --out ~/Infinite/datasets/render-trained/sample --count 8
"""
import argparse
import json
import math
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

PASSES = {"color": 0, "depth": 1, "normal": 3, "mask": 12}
SHAPES = ["Torus Knot", "Sphere", "Cube", "Torus", "Cone", "Cylinder"]


def make_patch(shape_type, cam_az, cam_el, light_az, light_el, rpass):
   return "\n".join([
      "infinite-patch 1",
      f"node 1 3D {shape_type}",
      "  id subj",
      "  pos 0 200",
      "end",
      "node 2 3D Camera",
      "  id cam",
      "  pos 0 500",
      f"  f azimuth {cam_az:.2f}",
      f"  f elevation {cam_el:.2f}",
      "  f orbitPerBeat 0.0",
      "end",
      "node 3 3D Light",
      "  id sun",
      "  pos 0 800",
      f"  f azimuth {light_az:.2f}",
      f"  f elevation {light_el:.2f}",
      "end",
      "node 4 3D Render 3D",
      "  id view",
      "  pos 400 200",
      f"  i renderPass {rpass}",
      "end",
      "node 5 Utility Output",
      "  id out",
      "  pos 750 200",
      "end",
      "geo view 0 subj",
      "geo view camera cam",
      "geo view light_1 sun",
      "cable out 0 view",
      "",
   ])


def synth_bg(size, rng, bg_files):
   if bg_files:
      im = Image.open(rng.choice(bg_files)).convert("RGB")
      return im.resize((size, size), Image.BILINEAR)
   yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) / size
   c0 = np.array([rng.uniform(0.08, 0.85) for _ in range(3)], np.float32)
   c1 = np.array([rng.uniform(0.08, 0.85) for _ in range(3)], np.float32)
   ang = rng.uniform(0, math.tau)
   t = np.clip(0.5 + (xx - 0.5) * math.cos(ang) + (yy - 0.5) * math.sin(ang), 0.0, 1.0)
   arr = c0[None, None, :] * (1.0 - t[:, :, None]) + c1[None, None, :] * t[:, :, None]
   im = Image.fromarray(np.clip(arr * 255.0, 0, 255).astype(np.uint8))
   dr = ImageDraw.Draw(im)
   for _ in range(rng.randrange(3, 8)):
      r = rng.randrange(size // 10, size // 3)
      x, y = rng.randrange(0, size), rng.randrange(0, size)
      col = tuple(rng.randrange(20, 235) for _ in range(3))
      dr.ellipse([x - r, y - r, x + r, y + r], fill=col)
   im = im.filter(ImageFilter.GaussianBlur(rng.uniform(4.0, 14.0)))
   noise = np.random.default_rng(rng.randrange(1 << 30)).normal(0, rng.uniform(2.0, 7.0), (size, size, 3))
   return Image.fromarray(np.clip(np.asarray(im, np.float32) + noise, 0, 255).astype(np.uint8))


def main():
   ap = argparse.ArgumentParser(description=__doc__)
   ap.add_argument("--bin", required=True, help="Path to Infinite executable")
   ap.add_argument("--out", required=True, help="Output dataset directory")
   ap.add_argument("--count", type=int, default=8, help="Number of scenes to render")
   ap.add_argument("--size", type=int, default=512, help="Final training resolution (square)")
   ap.add_argument("--supersample", type=int, default=2, help="Render supersample factor for soft alpha")
   ap.add_argument("--seed", type=int, default=20261127)
   ap.add_argument("--bg-dir", default="", help="Optional directory of CC0/PD12M background images")
   a = ap.parse_args()

   out_dir = Path(os.path.expanduser(a.out))
   out_dir.mkdir(parents=True, exist_ok=True)
   bg_files = []
   if a.bg_dir:
      bd = Path(os.path.expanduser(a.bg_dir))
      bg_files = [p for p in bd.rglob("*") if p.suffix.lower() in (".jpg", ".jpeg", ".png", ".webp")]

   rng = random.Random(a.seed)
   render_sz = a.size * a.supersample
   scenes = []
   with tempfile.TemporaryDirectory(prefix="inf_rt_") as tmp:
      tmp_p = Path(tmp)
      jobs = []
      for i in range(a.count):
         sh = rng.choice(SHAPES)
         caz = rng.uniform(0.0, 360.0)
         cel = rng.uniform(-30.0, 45.0)
         laz = rng.uniform(0.0, 360.0)
         lel = rng.uniform(10.0, 75.0)
         scenes.append({"id": f"scene_{i:04d}", "shape": sh, "cam_az": caz, "cam_el": cel, "light_az": laz, "light_el": lel})
         for pname, pcode in PASSES.items():
            pf = tmp_p / f"s{i:04d}_{pname}.inf"
            od = tmp_p / f"s{i:04d}_{pname}"
            od.mkdir()
            pf.write_text(make_patch(sh, caz, cel, laz, lel, pcode))
            jobs.append((i, pname, str(pf), str(od) + "/"))

      # Render all scenes & passes in ONE headless Infinite process via --batch
      cmd = [a.bin, "--frame", jobs[0][2], "0", jobs[0][3], "--size", f"{render_sz}x{render_sz}", "--lenient", "--isolated"]
      if len(jobs) > 1:
         bf = tmp_p / "batch.txt"
         bf.write_text("".join(f"{pf}\t{od}\n" for _, _, pf, od in jobs[1:]))
         cmd += ["--batch", str(bf)]
      print(f"Rendering {a.count} scenes x 4 passes at {render_sz}x{render_sz} (1 headless process)...")
      r = subprocess.run(cmd, capture_output=True, text=True)
      if r.returncode != 0:
         sys.stderr.write(r.stdout[-1500:] + "\n" + r.stderr[-1500:] + "\n")
         sys.exit(f"make_data: Infinite render failed (exit {r.returncode})")

      tiles = []
      kept = 0
      for sc in scenes:
         i = int(sc["id"].split("_")[1])
         imgs = {}
         for pname in PASSES:
            od = tmp_p / f"s{i:04d}_{pname}"
            pngs = sorted(od.glob("*.png"))
            if not pngs:
               continue
            im = Image.open(pngs[0])
            imgs[pname] = im.resize((a.size, a.size), Image.LANCZOS)
         if len(imgs) < 4:
            continue
         fg = imgs["color"].convert("RGB")
         alpha_im = imgs["mask"].convert("L")
         depth_im = imgs["depth"].convert("L")
         norm_im = imgs["normal"].convert("RGB")
         bg = synth_bg(a.size, rng, bg_files)

         a_arr = np.asarray(alpha_im, np.float32)[:, :, None] / 255.0
         comp_arr = np.asarray(fg, np.float32) * a_arr + np.asarray(bg, np.float32) * (1.0 - a_arr)
         comp_im = Image.fromarray(np.clip(comp_arr, 0, 255).astype(np.uint8))

         sd = out_dir / sc["id"]
         sd.mkdir(exist_ok=True)
         comp_im.save(sd / "composite.png")
         alpha_im.save(sd / "alpha.png")
         depth_im.save(sd / "depth.png")
         norm_im.save(sd / "normal.png")
         (sd / "meta.json").write_text(json.dumps(sc, indent=1))
         kept += 1
         if len(tiles) < 8:
            row = Image.new("RGB", (a.size * 4, a.size))
            row.paste(comp_im, (0, 0))
            row.paste(alpha_im.convert("RGB"), (a.size, 0))
            row.paste(depth_im.convert("RGB"), (a.size * 2, 0))
            row.paste(norm_im, (a.size * 3, 0))
            tiles.append(row)

      if tiles:
         tw, th = 256 * 4, 256
         sheet = Image.new("RGB", (tw, th * len(tiles)), (18, 20, 24))
         for idx, row in enumerate(tiles):
            sheet.paste(row.resize((tw, th), Image.BILINEAR), (0, idx * th))
         sheet_path = out_dir / "sample_sheet.png"
         sheet.save(sheet_path)
         print(f"Wrote {kept} scenes + contact sheet -> {sheet_path}")

      (out_dir / "DATA.md").write_text(
         "# Render-Trained Dataset Provenance\n\n"
         f"- Generator: `tools/render-trained/make_data.py` (seed {a.seed})\n"
         f"- Scenes rendered: {kept} (resolution {a.size}x{a.size}, {a.supersample}x supersampled)\n"
         "- Foreground geometry & shading: Infinite 3D procedural primitives (MIT, 100% Infinite-owned)\n"
         f"- Backgrounds: {'CC0/PD12M directory ' + a.bg_dir if bg_files else 'Procedural gradients + Gaussian noise (100% Infinite-owned)'}\n"
         "- Zero NC datasets, zero scraped imagery.\n"
      )


if __name__ == "__main__":
   main()
