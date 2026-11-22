"""Compute CIELAB grid targets for every THINGS-EEG2 image, in EEG order.

Each image: sRGB -> linear RGB -> exact area average to 48x48 -> block mean to N x N
(48 is divisible by every N in {1,2,3,4,6,8,16}, so each N grid is an exact area average of light)
-> CIELAB (D65). Images are read straight from the zips (never extracted).

Usage: python make_grids.py DOWNLOAD_DIR OUT.npz
"""
import io
import sys
import zipfile

import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from colour import linear_rgb_to_lab, srgb_to_linear  # noqa: E402
from safe_npy import load_npy_path  # noqa: E402

NS = (1, 2, 3, 4, 6, 8, 16)
BASE = 48


def area_matrix(src, dst):
    edges_out = np.linspace(0, src, dst + 1)
    A = np.zeros((dst, src))
    for i in range(dst):
        lo, hi = edges_out[i], edges_out[i + 1]
        j = np.arange(int(np.floor(lo)), int(np.ceil(hi)))
        A[i, j] = np.minimum(j + 1, hi) - np.maximum(j, lo)
    return A / A.sum(1, keepdims=True)


_cache = {}


def to_base(img):
    rgb = srgb_to_linear(np.asarray(img.convert("RGB"), dtype=np.float64) / 255.0)
    h, w, _ = rgb.shape
    Ah = _cache.setdefault(h, area_matrix(h, BASE))
    Aw = _cache.setdefault(w, area_matrix(w, BASE))
    return np.einsum("ih,hwc,jw->ijc", Ah, rgb, Aw)


def grids_from_base(base):  # base: (n, 48, 48, 3) linear
    out = {}
    n = base.shape[0]
    for N in NS:
        k = BASE // N
        lin = base.reshape(n, N, k, N, k, 3).mean((2, 4))
        out[N] = linear_rgb_to_lab(lin).astype(np.float32)
    return out


def read_split(zpath, files, concepts):
    with zipfile.ZipFile(zpath) as z:
        index = {n.rsplit("/", 1)[-1]: n for n in z.namelist() if n.lower().endswith((".jpg", ".jpeg", ".png"))}
        base = np.zeros((len(files), BASE, BASE, 3))
        sizes = set()
        for i, f in enumerate(files):
            with z.open(index[f]) as fh:
                img = Image.open(io.BytesIO(fh.read()))
                sizes.add(img.size)
                base[i] = to_base(img)
            if i % 2000 == 0:
                print(f"  {i}/{len(files)}", flush=True)
    return base, sizes


def main(dl, out):
    meta = load_npy_path(f"{dl}/image_metadata.npy")
    print({k: (len(v) if hasattr(v, "__len__") else v) for k, v in meta.items()})
    res = {}
    for split in ("train", "test"):
        files = list(meta[f"{split}_img_files"])
        zname = "training_images.zip" if split == "train" else "test_images.zip"
        base, sizes = read_split(f"{dl}/{zname}", files, meta[f"{split}_img_concepts"])
        print(split, len(files), "image sizes:", sizes)
        if split == "test":
            res["test_base48"] = base.astype(np.float32)  # local only, for the contact sheet
        for N, g in grids_from_base(base).items():
            res[f"{split}_N{N}"] = g
        res[f"{split}_concepts"] = np.array(meta[f"{split}_img_concepts"])
        res[f"{split}_files"] = np.array(files)
    np.savez_compressed(out, **res)
    print("saved", out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
