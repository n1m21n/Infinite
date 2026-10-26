"""Social preview cards (Open Graph / X) for the website's own pages, in the Infinite brand.

What: renders one 1200x630 Midnight card per page with the brand channel renderer (tools/brand/channels.py:
      mark top left, mono kicker, Geist headline with one Caveat Ember word, one line, a 3D node render on the
      right) into website/assets/og/<slug>.jpg, then points that page's og:image and twitter:image at it.
      Embed pages get one card each, titled with the visual's name.
Why:  73 pages shared one UI screenshot as their preview, and samples/library used a nebula photo; links looked
      off and identical. Node, blog and library item pages keep their own specific images.
Usage: python3 tools/website/gen_og_cards.py [--only slug,slug] [--no-apply]
      Run it after the page generators (they write the generic image back), then rebuild the site.
"""
import argparse, glob, os, re, shutil, sys, tempfile, types

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "brand"))
import channels  # noqa: E402
from PIL import Image  # noqa: E402

W = os.path.join(ROOT, "website")
R = os.path.join(ROOT, "art", "brand", "3d", "renders")
BASE = "https://infinitedevices.org/assets/og/"
NODE, LFO, FIELD, PATCH, BALL, KIT = ("node_hero_cutout.png", "node_lfo_cutout.png", "node_field_pixel_cutout.png",
                                      "patch_cutout.png", "brand_ball_cutout.png", "kit_components.png")

# page file -> (slug, kicker, headline, hand word, line, image)
PAGES = {
    "index.html": ("home", "Infinite", "A DAW for you", "you", "Visuals, music and code on one canvas. Free.", NODE),
    "samples/index.html": ("samples", "Free sample generator", "Endless samples", "Endless", "Drums, bass, keys and pads in any key. No sign-up.", BALL),
    "samples/visuals/index.html": ("samples-visuals", "Free visual samples", "Visual samples", "Visual", "Looping visuals to drop into your own work.", BALL),
    "library.html": ("library", "Infinite library", "The library", "library", "Templates, shaders, sounds and skills.", PATCH),
    "blog/index.html": ("blog", "Infinite blog", "Notes from the canvas", "canvas", "Audio-reactive visuals, music and code.", NODE),
    "nodes/index.html": ("nodes", "Every node", "308 nodes", "nodes", "Controls and patches for each one.", LFO),
    "embeds/index.html": ("embeds", "Audio-reactive visuals", "Visuals that listen", "listen", "Free to embed on any page.", BALL),
    "musicians.html": ("musicians", "Infinite for musicians", "For musicians", "musicians", "Synths, effects and plugins on one canvas.", LFO),
    "artists.html": ("artists", "Infinite for artists", "For artists", "artists", "Images that move, made on one canvas.", FIELD),
    "vjs.html": ("vjs", "Infinite for VJs", "For VJs", "VJs", "Live visuals that follow the music.", BALL),
    "developers.html": ("developers", "Infinite for developers", "For developers", "developers", "CLI, Python SDK and MCP.", PATCH),
    "creative-coders.html": ("creative-coders", "Infinite for creative coders", "For coders", "coders", "Field, sketches and shaders.", FIELD),
    "benchmarks.html": ("benchmarks", "Benchmarks", "Made to be fast", "fast", "Audio, video and speed, measured.", NODE),
    "philosophy.html": ("philosophy", "Philosophy", "Built for you", "you", "Creative software, built for the person using it.", KIT),
    "system.html": ("system", "Design system", "One language", "language", "The tokens and components behind the site.", KIT),
    "extensions.html": ("extensions", "Extensions", "Make it yours", "yours", "Add nodes, tools and panels.", KIT),
}


def title_of(path):
    m = re.search(r"<title>([^<]*)", open(path, errors="ignore").read())
    t = re.sub(r"\s*[:|—-]\s*(audio-reactive visual|Infinite).*$", "", m.group(1)) if m else ""
    return re.sub(r"&amp;", "&", t).strip()


def pages():
    out = dict(PAGES)
    for f in sorted(glob.glob(os.path.join(W, "embeds", "*.html"))):
        rel = os.path.relpath(f, W)
        if rel == "embeds/index.html" or "audio-reactive visual" not in open(f, errors="ignore").read()[:4000]:
            continue
        name = title_of(f)
        if name:
            out[rel] = ("embed-" + os.path.basename(f)[:-5], "Audio-reactive visual", name, "", "Free to embed. Made in Infinite.", BALL)
    return out


def card(spec, dest, tmp):
    slug, kicker, head, hand, line, image = spec
    a = types.SimpleNamespace(headline=head, kicker=kicker, hand=hand, line=line, image=os.path.join(R, image))
    im = Image.open(a.image)
    if im.mode == "RGBA":  # trim to the visible part so the fit fills the frame (as channels.py does)
        bb = im.split()[3].point(lambda v: 255 if v > 8 else 0).getbbox()
        if bb:
            p = int(0.02 * max(im.size))
            bb = (max(0, bb[0] - p), max(0, bb[1] - p), min(im.width, bb[2] + p), min(im.height, bb[3] + p))
            a.image = os.path.join(tmp, slug + "-img.png")
            im.crop(bb).save(a.image)
    t = next(x for x in channels.B["render"]["targets"] if x["id"] == "og")
    hp, png = os.path.join(tmp, slug + ".html"), os.path.join(tmp, slug + ".png")
    open(hp, "w").write(channels.page(t, a))
    if not channels.render(hp, png, t["w"], t["h"]):
        return False
    Image.open(png).convert("RGB").save(dest, quality=88, optimize=True)
    return True


def apply(rel, slug):
    p = os.path.join(W, rel)
    s = open(p).read()
    url = BASE + slug + ".jpg"
    n = re.sub(r'((?:og|twitter):image" content=")[^"]*', lambda m: m.group(1) + url, s)
    if n != s:
        open(p, "w").write(n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--no-apply", action="store_true")
    a = ap.parse_args()
    only = set(filter(None, a.only.split(",")))
    os.makedirs(os.path.join(W, "assets", "og"), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="ogcards-")
    bad = 0
    for rel, spec in pages().items():
        if only and spec[0] not in only:
            continue
        ok = card(spec, os.path.join(W, "assets", "og", spec[0] + ".jpg"), tmp)
        print("ok " if ok else "BAD", spec[0])
        bad += not ok
        if ok and not a.no_apply:
            apply(rel, spec[0])
    shutil.rmtree(tmp, ignore_errors=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
