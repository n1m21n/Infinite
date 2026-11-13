#!/usr/bin/env python3
"""Fetch and prepare the real-world assets for website/piano (Nocturne).

Everything comes from Poly Haven (CC0) except the water normal map, which is
from the three.js examples (MIT). Sources are written to
website/assets/nocturne/SOURCES.md.

    python3 tools/website/fetch_nocturne_assets.py            # all
    python3 tools/website/fetch_nocturne_assets.py --only sky,ground
    python3 tools/website/fetch_nocturne_assets.py --trees    # also bake tree cards (needs Blender)

Raw downloads are cached outside the repo (--cache); only web-sized files land
in website/assets/nocturne.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

from PIL import Image

Image.MAX_IMAGE_PIXELS = None

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, 'website', 'assets', 'nocturne')
API = 'https://api.polyhaven.com'
UA = 'asset-fetch/1.0'

SKIES = {'midnight': 'qwantani_moonrise_puresky', 'sunset': 'belfast_sunset_puresky'}
GROUNDS = {'grass': 'forrest_ground_01', 'shore': 'ganges_river_pebbles'}
MODELS = ['rock_moss_set_01', 'fern_02', 'shrub_03']
TREES = {'fir': 'fir_sapling', 'pine': 'pine_sapling_small'}
WATER_NORMALS = 'https://cdn.jsdelivr.net/gh/mrdoob/three.js@r160/examples/textures/waternormals.jpg'
BLENDER = '/Applications/Blender.app/Contents/MacOS/Blender'


def get_json(url):
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def download(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    tmp = dest + '.part'
    with urllib.request.urlopen(req, timeout=300) as r, open(tmp, 'wb') as f:
        shutil.copyfileobj(r, f)
    os.replace(tmp, dest)
    print('  got', os.path.basename(dest), round(os.path.getsize(dest) / 1e6, 1), 'MB')
    return dest


def save_jpg(src, dest, size=None, quality=80):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    im = Image.open(src).convert('RGB')
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    im.save(dest, 'JPEG', quality=quality, optimize=True, progressive=True)


def do_sky(cache):
    for name, asset in SKIES.items():
        files = get_json(f'{API}/files/{asset}')
        url = files['tonemapped']['url']
        raw = download(url, os.path.join(cache, asset + '_tonemapped.jpg'))
        save_jpg(raw, os.path.join(OUT, f'sky_{name}.jpg'), (4096, 2048), 84)


def do_ground(cache):
    for name, asset in GROUNDS.items():
        files = get_json(f'{API}/files/{asset}')
        for key, short in (('Diffuse', 'diff'), ('nor_gl', 'nor'), ('Rough', 'rough')):
            url = files[key]['1k']['jpg']['url']
            raw = download(url, os.path.join(cache, f'{asset}_{short}_1k.jpg'))
            save_jpg(raw, os.path.join(OUT, f'ground_{name}_{short}.jpg'), None, 82)


def fetch_gltf(asset, cache):
    files = get_json(f'{API}/files/{asset}')
    g = files['gltf']['1k']['gltf']
    root = os.path.join(cache, asset)
    download(g['url'], os.path.join(root, asset + '.gltf'))
    for rel, info in g['include'].items():
        download(info['url'], os.path.join(root, rel))
    return root


def do_models(cache):
    for asset in MODELS:
        root = fetch_gltf(asset, cache)
        dest = os.path.join(OUT, 'models', asset)
        for dirpath, _, names in os.walk(root):
            for n in names:
                src = os.path.join(dirpath, n)
                rel = os.path.relpath(src, root)
                out = os.path.join(dest, rel)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                if n.lower().endswith('.jpg'):
                    save_jpg(src, out, None, 80)
                else:
                    shutil.copyfile(src, out)


def do_water(cache):
    raw = download(WATER_NORMALS, os.path.join(cache, 'waternormals.jpg'))
    os.makedirs(OUT, exist_ok=True)
    shutil.copyfile(raw, os.path.join(OUT, 'water_normals.jpg'))


def do_trees(cache):
    """Bake each scanned tree into a flat-lit alpha card (the scans are 400k+ triangles)."""
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bake_tree_card.py')
    cuts = []
    for name, asset in TREES.items():
        png = os.path.join(cache, f'tree_{name}.png')
        if not os.path.exists(png):
            root = fetch_gltf(asset, cache)
            subprocess.run([BLENDER, '-b', '--factory-startup', '-P', script, '--',
                            os.path.join(root, asset + '.gltf'), png], check=True)
        im = Image.open(png).convert('RGBA')
        # each scan holds several trees side by side: split on empty columns
        cols = im.getchannel('A').resize((im.width, 1), Image.BOX).load()
        runs, start = [], None
        for x in range(im.width + 1):
            on = x < im.width and cols[x, 0] > 0
            if on and start is None:
                start = x
            if not on and start is not None:
                if x - start > 40:
                    runs.append((start, x))
                start = None
        for a, b in runs:
            tree = im.crop((a, 0, b, im.height))
            cuts.append(tree.crop(tree.getbbox()))
    # one atlas, TREE_CELLS equal cells, each tree bottom-centred in its cell
    cw, ch = 512, 1024
    atlas = Image.new('RGBA', (cw * len(cuts), ch), (0, 0, 0, 0))
    for i, tree in enumerate(cuts):
        s = min(cw / tree.width, ch / tree.height)
        tree = tree.resize((max(1, round(tree.width * s)), max(1, round(tree.height * s))), Image.LANCZOS)
        atlas.alpha_composite(tree, (i * cw + (cw - tree.width) // 2, ch - tree.height))
    # fill empty texels with the mean foliage colour so mipmaps do not fringe dark
    alpha = atlas.getchannel('A')
    mean = atlas.resize((1, 1), Image.BOX).getpixel((0, 0))
    k = 255.0 / max(1, mean[3])
    fill = Image.new('RGB', atlas.size, tuple(min(255, round(c * k)) for c in mean[:3]))
    rgb = Image.composite(atlas.convert('RGB'), fill, alpha.point(lambda v: 255 if v > 8 else 0))
    rgb.putalpha(alpha)
    png = os.path.join(cache, 'trees_atlas.png')
    rgb.save(png)
    webp = os.path.join(OUT, 'trees.webp')
    subprocess.run(['cwebp', '-quiet', '-q', '84', '-alpha_q', '90', '-exact', png, '-o', webp], check=True)
    with open(os.path.join(OUT, 'trees.json'), 'w') as f:
        json.dump({'cells': len(cuts)}, f)
    for old in ('tree_fir.webp', 'tree_pine.webp'):
        if os.path.exists(os.path.join(OUT, old)):
            os.remove(os.path.join(OUT, old))
    print('  atlas', len(cuts), 'trees', round(os.path.getsize(webp) / 1e3), 'kB')


def write_sources():
    rows = []
    for name, a in SKIES.items():
        rows.append((f'sky_{name}.jpg', a, 'hdri'))
    for name, a in GROUNDS.items():
        rows.append((f'ground_{name}_*.jpg', a, 'texture'))
    for a in MODELS:
        rows.append((f'models/{a}/', a, 'model'))
    for name, a in TREES.items():
        rows.append(('trees.webp', a, 'model, baked to a card'))
    lines = ['# Nocturne world assets', '',
             'Made by `tools/website/fetch_nocturne_assets.py`. Do not hand-edit.', '',
             '| File | Source | Kind | Licence |', '|---|---|---|---|']
    for f, a, kind in rows:
        lines.append(f'| `{f}` | [Poly Haven: {a}](https://polyhaven.com/a/{a}) | {kind} | CC0 |')
    lines.append('| `water_normals.jpg` | three.js examples `textures/waternormals.jpg` | normal map | MIT |')
    with open(os.path.join(OUT, 'SOURCES.md'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default='sky,ground,models,water')
    ap.add_argument('--trees', action='store_true')
    ap.add_argument('--cache', default=os.path.join(tempfile.gettempdir(), 'nocturne-assets'))
    args = ap.parse_args()
    steps = {'sky': do_sky, 'ground': do_ground, 'models': do_models, 'water': do_water}
    for key in args.only.split(','):
        if key:
            print(key)
            steps[key](args.cache)
    if args.trees:
        print('trees')
        do_trees(args.cache)
    write_sources()
    total = sum(os.path.getsize(os.path.join(d, n)) for d, _, ns in os.walk(OUT) for n in ns)
    print('website/assets/nocturne:', round(total / 1e6, 1), 'MB')


if __name__ == '__main__':
    sys.exit(main())
