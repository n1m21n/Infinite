#!/usr/bin/env python3
"""Contact sheet of the renders an E1 arm was scored on: before | after, truth, the arm's answer, right or wrong.

Usage: tools/infinite-model/e1_sheet.py <arm> [--out build/e1] [--per-split 12] [--png sheet.png]
"""
import argparse, json, os, random
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
T = 96  # thumbnail px


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("arm")
    ap.add_argument("--out", default=os.path.join(ROOT, "build/e1"))
    ap.add_argument("--per-split", type=int, default=12)
    ap.add_argument("--png")
    a = ap.parse_args()
    res = json.load(open(os.path.join(a.out, "eval", a.arm + ".json")))
    rows = {}
    for l in open(os.path.join(a.out, "rows.jsonl")):
        r = json.loads(l)
        rows[r["id"]] = r
    rng = random.Random(7)
    picks = []
    for split in ("test_seen", "test_params", "test_types"):
        items = [i for i in res["items"] if i["split"] == split]
        right = [i for i in items if i["top1"]]
        wrong = [i for i in items if not i["top1"]]
        h = a.per_split // 2
        picks += [(split, i) for i in rng.sample(right, min(h, len(right))) + rng.sample(wrong, min(a.per_split - h, len(wrong)))]
    cols, cw, ch = 3, 2 * T + 230, T + 10
    nrow = (len(picks) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw, nrow * ch + 30), (18, 18, 22))
    d = ImageDraw.Draw(sheet)
    d.text((8, 8), f"E1 {a.arm}  " + "  ".join(f"{s}: top1 {v['top1']['rate']:.2f}" for s, v in res["splits"].items()),
           fill=(230, 230, 230))
    for k, (split, it) in enumerate(picks):
        r = rows[it["id"]]
        x, y = (k % cols) * cw, 30 + (k // cols) * ch
        for j, p in enumerate((r["before_png"], r["after_png"])):
            try:
                sheet.paste(Image.open(p).convert("RGB").resize((T, T)), (x + 4 + j * T, y))
            except OSError:
                pass
        ok = it["top1"]
        tx = x + 2 * T + 10
        d.text((tx, y), split.replace("test_", ""), fill=(140, 140, 150))
        d.text((tx, y + 14), "truth " + r["answer"][:30], fill=(230, 230, 230))
        d.text((tx, y + 28), "said  " + (it["pred"][0] if it["pred"] else "-")[:30],
               fill=(120, 220, 140) if ok else ((240, 190, 90) if it["equiv1"] else (240, 110, 110)))
        d.text((tx, y + 44), r["change_text"][:34], fill=(150, 150, 160))
        d.text((tx, y + 58), r["change_text"][34:68], fill=(150, 150, 160))
    out = a.png or os.path.join(a.out, "eval", a.arm + "-sheet.png")
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
