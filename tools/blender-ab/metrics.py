# Reads truth.png, bake.png (Blender) and inf.png (Infinite) from a case
# folder, prints one JSON line, writes sheet.png:
#   truth | Blender bake | Infinite bake | |Infinite - truth| x4 (red)
# Run inside Blender (for numpy and PNG io):
#   Blender -b --factory-startup --python metrics.py -- <case dir>
import bpy, sys, os, json
import numpy as np

case = sys.argv[sys.argv.index("--") + 1]


def load(name, raw=False):
    if not os.path.exists(os.path.join(case, name)):
        return None
    im = bpy.data.images.load(os.path.join(case, name))

    w, h = im.size
    px = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)  # stored bytes / 255, bottom-up
    if not raw:  # Blender wrote sRGB (Standard view): back to the AO value
        c = px[..., :3]
        px[..., :3] = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return px


# Infinite's picture holds the AO value itself: Render 3D decodes an emission
# map as sRGB colour and encodes the result again, so the two cancel.
truth, bake, inf = load("truth.png"), load("bake.png"), load("inf.png", raw=True)
has_bake = bake is not None
if not has_bake:
    bake = np.zeros_like(truth)
mask = truth[..., 3] > 0.99
# Skip the silhouette rim: one pixel of camera mismatch would dominate it.
inner = mask.copy()
for dy, dx in [(1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0), (0, 2), (0, -2)]:
    inner &= np.roll(np.roll(mask, dy, 0), dx, 1)


def luma(p):
    return p[..., 0] * 0.2126 + p[..., 1] * 0.7152 + p[..., 2] * 0.0722


lt, lb, li = luma(truth), luma(bake), luma(inf)
dark = inner & (lt < 0.5)
r = {
    "case": os.path.basename(case),
    "pixels": int(inner.sum()),
    "truth_mean": round(float(lt[inner].mean()), 3),
    "blender_bake_mean": round(float(lb[inner].mean()), 3) if has_bake else None,
    "infinite_mean": round(float(li[inner].mean()), 3),
    "blender_bake_err": round(float(np.abs(lb - lt)[inner].mean()), 3) if has_bake else None,
    "infinite_err": round(float(np.abs(li - lt)[inner].mean()), 3),
    # Where the truth is dark (crevices), how dark each bake gets.
    "crevice_truth": round(float(lt[dark].mean()), 3) if dark.any() else None,
    "crevice_blender": round(float(lb[dark].mean()), 3) if dark.any() and has_bake else None,
    "crevice_infinite": round(float(li[dark].mean()), 3) if dark.any() else None,
    "outside_mask_infinite": round(float(((li > 0.2) & ~mask).mean()), 4),
}
print("ABRESULT " + json.dumps(r))

h, w = lt.shape


def rgb(l, m):
    out = np.zeros((h, w, 4), np.float32); out[..., 3] = 1
    for c in range(3):
        out[..., c] = np.where(m, l, 0.0)
    return out


diff = np.zeros((h, w, 4), np.float32); diff[..., 3] = 1
diff[..., 0] = np.clip(np.abs(li - lt) * 4, 0, 1) * mask
diff[..., 1] = np.where(mask, 0.08, 0)
sheet = np.concatenate([rgb(lt, mask), rgb(lb, mask), rgb(li, np.ones_like(mask)), diff], axis=1)
im = bpy.data.images.new("sheet", sheet.shape[1], sheet.shape[0])
im.pixels = sheet.ravel()
im.filepath_raw = os.path.join(case, "sheet.png"); im.file_format = 'PNG'; im.save()
