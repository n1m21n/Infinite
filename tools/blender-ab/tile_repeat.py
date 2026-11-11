"""How strongly a texture repeats on screen: the tallest off-centre peak of the autocorrelation of an image crop,
relative to the centre (1 = an exact repeat, near 0 = none). Used for tex_breakup, which Cycles has no twin for:
  python3 tools/blender-ab/tile_repeat.py OUT/tex_tile/inf_live.png OUT/tex_breakup/inf_live.png"""
import sys
import numpy as np
from PIL import Image


def repeat_score(path, crop=160):
    g = np.asarray(Image.open(path).convert("L"), np.float32)
    h, w = g.shape
    c = g[h - crop - 8:h - 8, (w - crop) // 2:(w + crop) // 2]   # the near, bottom-centre patch: least foreshortened
    c = (c < 60).astype(np.float32)    # the tile's dark spots only: the soft background repeats at every shift
    c = c - c.mean()
    f = np.fft.fft2(c, s=(2 * crop, 2 * crop))
    ac = np.fft.fftshift(np.real(np.fft.ifft2(f * np.conj(f))))
    ac /= ac.max()
    yy, xx = np.mgrid[0:2 * crop, 0:2 * crop]
    r = np.hypot(yy - crop, xx - crop)
    # Overlap shrinks with the shift, so each lag is normalised by how many pixels it compares.
    ov = (crop - np.abs(yy - crop)).clip(1) * (crop - np.abs(xx - crop)).clip(1) / (crop * crop)
    ring = (r > 12) & (r < crop * 0.6)
    return float((ac / ov)[ring].max())


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print("%-60s repeat %.3f" % (p, repeat_score(p)))
