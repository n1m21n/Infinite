#!/usr/bin/env python3
"""S5 agent painting eval (internal/plans/2d-view/ai-tools.md sections 6-7, metric M8).

  build   write the 12 S5 references: 4 flat graphics, 4 painterly studies (both drawn here, ours),
          4 photo edits (COCO val from the S1 set, eval only: the edit is made with the COCO mask)
  run     for each reference x model x condition x seed: a fresh 512 px 2D document in its own app
          instance, `claude -p` with only Read and a view2d wrapper, the same turn and dollar budget;
          condition `blind` refuses look/undo/redo (today's stroke/fill-only loop), `look` allows them
  score   SSIM (grey) and mean colour error of the final picture against the reference, plus batches,
          tokens, cost and wall time -> results/s5.json and a contact sheet results/s5_sheet.png

Run with the eval venv:  ~/Infinite/tools/.aievalvenv/bin/python -I s5_agent.py build|run|score
LPIPS from the plan is not scored: it needs PyTorch, too heavy for the 8 GB machine. SSIM + colour error stand in.
"""
import argparse
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

DATA = Path(os.path.expanduser("~/Infinite/datasets/ai-eval"))
SET = DATA / "s5"
RES = DATA / "results"
RUNS = RES / "s5_runs"
SIZE = 512
MODELS = ["gemini-4-hydrogen-medium"]
AGY = Path.home() / ".local/bin/agy"
CONDITIONS = ["blind", "look"]


# ---------------------------------------------------------------- build
def grad(w, h, top, bottom):
   t = np.linspace(0, 1, h)[:, None, None]
   a = np.array(top, np.float32)[None, None] * (1 - t) + np.array(bottom, np.float32)[None, None] * t
   return Image.fromarray(np.repeat(a, w, 1).astype(np.uint8))


def flat(i):
   im = Image.new("RGB", (SIZE, SIZE), (240, 234, 220))
   d = ImageDraw.Draw(im)
   if i == 0:   # sunset poster
      im = grad(SIZE, SIZE, (255, 140, 90), (250, 210, 150))
      d = ImageDraw.Draw(im)
      d.ellipse((176, 150, 336, 310), fill=(255, 236, 170))
      d.polygon([(0, 380), (150, 230), (300, 380)], fill=(60, 50, 90))
      d.polygon([(180, 380), (360, 200), (512, 330), (512, 380)], fill=(90, 70, 120))
      d.rectangle((0, 380, 512, 512), fill=(30, 40, 70))
   elif i == 1:   # bauhaus
      d.rectangle((40, 40, 250, 250), fill=(220, 50, 40))
      d.ellipse((230, 120, 470, 360), fill=(30, 60, 150))
      d.polygon([(60, 470), (200, 280), (340, 470)], fill=(245, 190, 30))
      d.rectangle((380, 40, 400, 480), fill=(20, 20, 20))
   elif i == 2:   # logo mark
      im = Image.new("RGB", (SIZE, SIZE), (250, 250, 250))
      d = ImageDraw.Draw(im)
      d.ellipse((106, 106, 406, 406), outline=(20, 120, 110), width=36)
      d.polygon([(256, 170), (340, 330), (172, 330)], fill=(240, 100, 70))
   else:   # stripes and star
      for k in range(7):
         d.rectangle((0, k * SIZE // 7, SIZE, (k + 1) * SIZE // 7), fill=(200, 40, 50) if k % 2 == 0 else (250, 250, 245))
      d.rectangle((0, 0, 220, 220), fill=(30, 50, 110))
      import math
      pts = []
      for k in range(10):
         r = 80 if k % 2 == 0 else 32
         a = -math.pi / 2 + k * math.pi / 5
         pts.append((110 + r * math.cos(a), 110 + r * math.sin(a)))
      d.polygon(pts, fill=(250, 250, 245))
   return im


def dabs(im, rng, n, box, colours, rmin, rmax, blur):
   layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
   d = ImageDraw.Draw(layer)
   x0, y0, x1, y1 = box
   for _ in range(n):
      cx, cy = rng.uniform(x0, x1), rng.uniform(y0, y1)
      rx, ry = rng.uniform(rmin, rmax), rng.uniform(rmin, rmax) * rng.uniform(0.4, 1.0)
      c = colours[rng.randrange(len(colours))]
      j = [max(0, min(255, v + rng.randint(-14, 14))) for v in c]
      d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=(*j, rng.randint(90, 200)))
   layer = layer.filter(ImageFilter.GaussianBlur(blur))
   im.alpha_composite(layer)


def painterly(i):
   rng = random.Random(500 + i)
   if i == 0:   # landscape
      im = grad(SIZE, SIZE, (150, 190, 225), (235, 220, 200)).convert("RGBA")
      dabs(im, rng, 260, (0, 0, 512, 230), [(170, 200, 230), (230, 230, 240), (200, 215, 235)], 10, 40, 3)
      dabs(im, rng, 300, (0, 260, 512, 380), [(90, 120, 80), (110, 140, 90), (70, 100, 70)], 12, 36, 2)
      dabs(im, rng, 320, (0, 380, 512, 512), [(160, 140, 90), (180, 160, 100), (130, 110, 70)], 10, 30, 2)
   elif i == 1:   # still life: bowl and fruit
      im = grad(SIZE, SIZE, (90, 70, 60), (60, 45, 40)).convert("RGBA")
      dabs(im, rng, 160, (0, 330, 512, 512), [(150, 120, 90), (170, 140, 100)], 14, 40, 3)
      dabs(im, rng, 140, (150, 280, 360, 380), [(220, 220, 210), (200, 200, 195)], 10, 30, 2)
      dabs(im, rng, 90, (180, 220, 250, 300), [(220, 80, 50), (240, 120, 60)], 8, 22, 1.5)
      dabs(im, rng, 90, (260, 230, 330, 300), [(230, 200, 60), (240, 220, 90)], 8, 22, 1.5)
   elif i == 2:   # seascape
      im = grad(SIZE, SIZE, (240, 180, 150), (120, 150, 190)).convert("RGBA")
      dabs(im, rng, 200, (0, 0, 512, 200), [(250, 200, 170), (240, 170, 160), (220, 190, 200)], 12, 46, 4)
      dabs(im, rng, 380, (0, 250, 512, 512), [(40, 80, 120), (60, 110, 150), (90, 140, 170), (230, 230, 230)], 6, 30, 1.5)
   else:   # figure study: head and shoulders blocks
      im = grad(SIZE, SIZE, (200, 205, 190), (160, 170, 160)).convert("RGBA")
      dabs(im, rng, 200, (120, 330, 400, 512), [(50, 60, 90), (70, 80, 110)], 14, 40, 3)
      dabs(im, rng, 160, (200, 140, 320, 300), [(220, 170, 140), (200, 150, 120), (235, 190, 160)], 8, 26, 2)
      dabs(im, rng, 120, (190, 110, 330, 180), [(60, 40, 30), (80, 55, 40)], 8, 24, 2)
   return im.convert("RGB")


EDITS = [
   ("darken", "Cut out the main object and darken everything else to about 40% brightness. Keep the object as it is."),
   ("desat", "Make the background black and white and keep the main object in full colour."),
   ("blur", "Blur the background (as if shot at a wide aperture) and keep the main object sharp."),
   ("warm", "Make the main object brighter and warmer (more orange); leave the background as it is."),
]


def photo_edit(src, gt, kind):
   a = np.asarray(src, np.float32)
   m = (np.asarray(gt.convert("L"), np.float32) / 255.0)[..., None]
   if kind == "darken":
      b = a * 0.4
   elif kind == "desat":
      g = a @ np.array([0.299, 0.587, 0.114], np.float32)
      b = np.repeat(g[..., None], 3, 2)
   elif kind == "blur":
      b = np.asarray(src.filter(ImageFilter.GaussianBlur(8)), np.float32)
   else:
      w = np.clip(a * 1.25 + np.array([30, 10, -20], np.float32), 0, 255)
      return Image.fromarray((w * m + a * (1 - m)).astype(np.uint8))
   return Image.fromarray((a * m + b * (1 - m)).astype(np.uint8))


def square(im, gt):
   """Crop the square round the object, then 512 px."""
   ys, xs = np.nonzero(np.asarray(gt.convert("L")) > 127)
   w, h = im.size
   side = min(w, h)
   cx = int(np.clip((xs.min() + xs.max()) / 2 - side / 2, 0, w - side))
   cy = int(np.clip((ys.min() + ys.max()) / 2 - side / 2, 0, h - side))
   box = (cx, cy, cx + side, cy + side)
   return im.crop(box).resize((SIZE, SIZE), Image.LANCZOS), gt.crop(box).resize((SIZE, SIZE), Image.NEAREST)


def cmd_build(a):
   SET.mkdir(parents=True, exist_ok=True)
   items = []
   for i in range(4):
      flat(i).save(SET / f"flat{i}.png")
      items.append({"id": f"flat{i}", "kind": "flat", "ref": f"flat{i}.png", "start": None,
                    "task": "Recreate the reference picture on the blank document as closely as you can."})
   for i in range(4):
      painterly(i).save(SET / f"paint{i}.png")
      items.append({"id": f"paint{i}", "kind": "painterly", "ref": f"paint{i}.png", "start": None,
                    "task": "Paint a study of the reference picture on the blank document, as close to it as you can."})
   # photo edits: the S1 pictures whose object is 15-50% of the square crop, the first four
   s1 = sorted((DATA / "S1").glob("*_gt.png"))
   k = 0
   for gtp in s1:
      if k == 4:
         break
      src = Image.open(str(gtp).replace("_gt.png", ".jpg")).convert("RGB")
      gt = Image.open(gtp)
      sq, sg = square(src, gt)
      frac = (np.asarray(sg.convert("L")) > 127).mean()
      if not 0.15 <= frac <= 0.5:
         continue
      kind, task = EDITS[k]
      sq.save(SET / f"photo{k}_src.png")
      photo_edit(sq, sg, kind).save(SET / f"photo{k}.png")
      items.append({"id": f"photo{k}", "kind": "photo", "ref": f"photo{k}.png", "start": f"photo{k}_src.png",
                    "task": task + " The photo is already in the document as layer 1."})
      k += 1
   json.dump(items, open(SET / "set.json", "w"), indent=1)
   print(f"{len(items)} references in {SET}")


# ---------------------------------------------------------------- run
APP = Path(__file__).resolve().parents[2] / "build-dev/Infinite.app/Contents/MacOS/Infinite"

WRAPPER = """#!/bin/bash
# S5 view2d wrapper ({mode}). Logs every call; blind refuses look, undo and redo.
LOG="{log}"
echo "$(date +%s) $*" >> "$LOG"
if [ "{mode}" = "blind" ]; then
  case "$1" in look|undo|redo) echo '{{"ok":false,"error":"not available in this run"}}'; exit 1;; esac
  if printf '%s' "$*" | grep -q '"op" *: *"look"'; then echo '{{"ok":false,"error":"look is not available in this run"}}'; exit 1; fi
fi
if [ "$1" = "-" ]; then exec env HOME="{home}" "{app}" --cli view2d -; fi
exec env HOME="{home}" "{app}" --cli view2d "$@"
"""


def cli(home, *args, inp=None, timeout=60):
   r = subprocess.run(["env", f"HOME={home}", str(APP), "--cli", *args], input=inp, capture_output=True, text=True,
                      timeout=timeout)
   return r.stdout.strip()


class Instance:
   def __init__(self, n):
      self.home = Path(tempfile.mkdtemp(prefix=f"s5i{n}_"))
      # Off-screen like tools/ui/shoot_view2d.py: IMAGERESYNTH_SCREENSHOT hides the window and Dock icon (the
      # far-off frame means it never exits), INFINITE_SHOT_REQUEST keeps the control port that headless runs drop.
      env = dict(os.environ, HOME=str(self.home), INFINITE_NOSPLASH="1", INFINITE_NO_UPDATE_CHECK="1",
                 INFINITE_HIDE_TELEMETRY="1", IMAGERESYNTH_SCREENSHOT=str(self.home / "unused.png"),
                 INFINITE_SCREENSHOT_FRAME="999999999", INFINITE_SHOT_REQUEST=str(self.home / "req"))
      if os.environ.get("S5_VISIBLE"):
         env = dict(os.environ, HOME=str(self.home))
      self.proc = subprocess.Popen([str(APP)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
      for _ in range(90):
         time.sleep(1)
         try:
            if cli(self.home, "status", timeout=5).startswith("{"):
               break
         except subprocess.TimeoutExpired:
            pass
      else:
         raise RuntimeError("app did not answer")

   def fresh(self, start):
      cli(self.home, "add", "Scene 2D")
      cli(self.home, "set", "Scene 2D", "doc", f"next 1\ndoc {SIZE} {SIZE} 0 1 1 1 0 0 0 1 1 100 4 60 10 1 1\n")
      cli(self.home, "panel", "2D View", "open")
      if start:
         cli(self.home, "import", str(SET / start), "into", "Scene 2D")
         time.sleep(1)
         cli(self.home, "view2d-add", "Image Source")
      time.sleep(2)
      lay = json.loads(cli(self.home, "view2d", "layers") or "{}")
      return lay.get("doc") and lay.get("size") == [SIZE, SIZE]

   def final(self, dst):
      r = json.loads(cli(self.home, "view2d", json.dumps({"op": "look", "max": SIZE})) or "{}")
      if r.get("ok") and r.get("path"):
         shutil.copy(r["path"], dst)
         return True
      return False

   def close(self):
      try:
         cli(self.home, "quit", timeout=10)
      except Exception:
         pass
      time.sleep(2)
      self.proc.kill()
      shutil.rmtree(self.home, ignore_errors=True)


PROMPT = """You are painting in Infinite's 2D View through a command line tool. {task}

Reference picture (read it with the Read tool): {ref}
The document is {size} x {size} px. Drive it only with: {wrapper} <args>
{mode_note}

The tool's manual:
{docs}

Work in batches: one call of {wrapper} '<json array of calls>' is one gated batch. When the picture is done, stop and
reply DONE."""

BLIND = ("In this run you cannot see the document: `look`, `undo` and `redo` are refused. Plan from the reference and "
         "your own coordinates.")
LOOK = "You can see the document with `{wrapper} look` (read the PNG path it answers) and undo your own batches."


def run_one(inst, item, model, cond, seed, a):
   out = RUNS / f"{item['id']}_{model}_{cond}_{seed}"
   if (out / "result.json").exists():
      return
   out.mkdir(parents=True, exist_ok=True)
   if not inst.fresh(item["start"]):
      json.dump({"error": "setup failed"}, open(out / "result.json", "w"))
      return
   wrapper = out / "v2d"
   wrapper.write_text(WRAPPER.format(mode=cond, log=out / "calls.log", home=inst.home, app=APP))
   wrapper.chmod(0o755)
   docs = subprocess.run([str(APP), "--cli", "docs", "view2d"], capture_output=True, text=True).stdout
   prompt = PROMPT.format(task=item["task"], ref=SET / item["ref"], size=SIZE, wrapper=wrapper,
                          mode_note=BLIND if cond == "blind" else LOOK.format(wrapper=wrapper), docs=docs)
   (out / "prompt.txt").write_text(prompt)
   t0 = time.time()
   if model.startswith("gemini"):
      # Antigravity's CLI: no tool allowlist or turn/dollar cap, so the wrapper is the only gate and the
      # wall-clock timeout the only budget. The prompt still says to use nothing but the wrapper.
      cmd = [str(AGY), "-p", prompt, "--model", model, "--output-format", "json", "--dangerously-skip-permissions",
             "--disable-slash-commands", "--print-timeout", f"{a.timeout - 30}s", "--add-dir", str(SET),
             "--add-dir", str(out)]
   else:
      cmd = ["claude", "-p", prompt, "--model", model, "--output-format", "json", "--max-turns", str(a.turns),
             "--max-budget-usd", str(a.budget), "--tools", "Bash,Read", "--allowedTools", f"Bash({wrapper}:*)", "Read",
             "--add-dir", str(SET), str(inst.home), "--strict-mcp-config", "--no-session-persistence",
             "--setting-sources", "", "--permission-mode", "dontAsk"]
   try:
      r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.timeout, cwd=out)
      (out / "claude.json").write_text(r.stdout)
      (out / "claude.err").write_text(r.stderr)
   except subprocess.TimeoutExpired:
      (out / "claude.err").write_text("timeout")
   wall = time.time() - t0
   ok = inst.final(out / "final.png")
   meta = {}
   try:
      j = json.loads((out / "claude.json").read_text())
      u = j.get("usage", {})
      meta = {"cost": j.get("total_cost_usd"), "turns": j.get("num_turns"), "subtype": j.get("subtype") or j.get("status"),
              "tokens_in": u.get("input_tokens", 0) + u.get("cache_read_input_tokens", 0) + u.get("cache_creation_input_tokens", 0),
              "tokens_out": u.get("output_tokens", 0) + u.get("thinking_tokens", 0)}
   except Exception as e:
      meta = {"error": f"no claude json: {e}"}
   calls = (out / "calls.log").read_text().splitlines() if (out / "calls.log").exists() else []
   batches = sum(1 for c in calls if c.split(" ", 1)[-1].lstrip().startswith(("[", "{", "-")) and '"op":"look"' not in c.replace(" ", ""))
   json.dump({"id": item["id"], "kind": item["kind"], "model": model, "cond": cond, "seed": seed, "wall": wall,
              "final": ok, "batches": batches, "calls": len(calls), **meta}, open(out / "result.json", "w"), indent=1)
   print(f"{out.name}: {wall:.0f}s cost {meta.get('cost')} batches {batches}", flush=True)


def cmd_run(a):
   items = json.load(open(SET / "set.json"))
   if a.only:
      items = [i for i in items if i["id"] in a.only.split(",")]
   models = a.models.split(",")
   jobs = [(it, m, c, s) for s in range(a.seeds) for it in items for m in models for c in CONDITIONS]
   RUNS.mkdir(parents=True, exist_ok=True)
   lock = threading.Lock()
   pending = list(jobs)

   def worker(n):
      # A fresh app per run: `infinite new` leaves the old nodes in place, and the agent's brush must not carry over.
      while True:
         with lock:
            if not pending:
               return
            job = pending.pop(0)
         it, m, c, s = job
         if (RUNS / f"{it['id']}_{m}_{c}_{s}" / "result.json").exists():
            continue
         inst = Instance(n)
         try:
            run_one(inst, *job, a)
         finally:
            inst.close()

   with ThreadPoolExecutor(a.parallel) as ex:
      list(ex.map(worker, range(a.parallel)))


# ---------------------------------------------------------------- score
def cmd_score(a):
   from skimage.metrics import structural_similarity as ssim
   rows = []
   for rj in sorted(RUNS.glob("*/result.json")):
      r = json.load(open(rj))
      if "model" not in r:
         continue
      ref = np.asarray(Image.open(SET / f"{r['id']}.png").convert("RGB"), np.float32)
      fp = rj.parent / "final.png"
      if fp.exists():
         fin = np.asarray(Image.open(fp).convert("RGB").resize((SIZE, SIZE)), np.float32)
         g = lambda x: x @ np.array([0.299, 0.587, 0.114], np.float32)
         r["ssim"] = float(ssim(g(ref), g(fin), data_range=255))
         r["colour_err"] = float(np.abs(ref - fin).mean())
      rows.append(r)
   summary = {}
   for m in sorted({r["model"] for r in rows}):
      for c in CONDITIONS:
         sel = [r for r in rows if r["model"] == m and r["cond"] == c and "ssim" in r]
         if sel:
            summary[f"{m} {c}"] = {"n": len(sel), "ssim": float(np.mean([r["ssim"] for r in sel])),
                                   "colour_err": float(np.mean([r["colour_err"] for r in sel])),
                                   "batches": float(np.mean([r["batches"] for r in sel])),
                                   "cost": float(np.mean([r.get("cost") or 0 for r in sel])),
                                   "wall": float(np.mean([r["wall"] for r in sel]))}
      b, l = summary.get(f"{m} blind"), summary.get(f"{m} look")
      if b and l:
         summary[f"{m} ssim gain"] = (l["ssim"] - b["ssim"]) / max(1e-6, b["ssim"])
   json.dump({"rows": rows, "summary": summary}, open(RES / "s5.json", "w"), indent=1)
   for k, v in summary.items():
      print(k, json.dumps(v) if isinstance(v, dict) else f"{v:+.1%}")
   sheet(rows)


def sheet(rows):
   items = json.load(open(SET / "set.json"))
   models = sorted({r["model"] for r in rows})
   cols = ["ref"] + [f"{m}|{c}" for m in models for c in CONDITIONS]
   t = 128
   im = Image.new("RGB", (t * len(cols), t * len(items)), (30, 30, 30))
   for y, it in enumerate(items):
      im.paste(Image.open(SET / it["ref"]).convert("RGB").resize((t, t)), (0, y * t))
      for x, col in enumerate(cols[1:], 1):
         m, c = col.split("|")
         p = RUNS / f"{it['id']}_{m}_{c}_0" / "final.png"
         if p.exists():
            im.paste(Image.open(p).convert("RGB").resize((t, t)), (x * t, y * t))
   im.save(RES / "s5_sheet.png")
   print("sheet:", RES / "s5_sheet.png", "columns:", cols)


def main():
   ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
   ap.add_argument("cmd", choices=["build", "run", "score"])
   ap.add_argument("--models", default=",".join(MODELS))
   ap.add_argument("--seeds", type=int, default=3)
   ap.add_argument("--only", default="")
   ap.add_argument("--parallel", type=int, default=3)
   ap.add_argument("--turns", type=int, default=30)
   ap.add_argument("--budget", type=float, default=2.0)
   ap.add_argument("--timeout", type=int, default=1200)
   a = ap.parse_args()
   {"build": cmd_build, "run": cmd_run, "score": cmd_score}[a.cmd](a)


main()
