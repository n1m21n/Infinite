#!/usr/bin/env python3
"""Generate the Extensions download page and compact v0.5.0 release notes."""
from pathlib import Path
import hashlib, html, json, shutil, sys

repo, assets = map(Path, sys.argv[1:3])
site = repo / 'website'
catalog = json.loads((assets / 'extensions.json').read_text())
pack = catalog['packs'][0]
labels = {
    'macos': ('macOS', 'Intel and Apple Silicon'),
    'windows-x64': ('Windows x64', 'Most Windows PCs'),
    'windows-arm64': ('Windows ARM64', 'ARM-based Windows PCs'),
    'linux-x64': ('Linux', 'x86_64'),
}
buttons = []
for platform, entry in pack['files'].items():
    source = assets / f'tracking-{platform}.zip'
    assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256']
    relative = f'assets/extensions/tracking/{pack["version"]}/{source.name}'
    target = site / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    entry['url'] = f'https://n1m21n.github.io/Infinite/{relative}'
    name, detail = labels[platform]
    size = source.stat().st_size / 1_000_000
    buttons.append(f'<a class="btn btn-dl" href="{relative}" download><span><b>{name}</b><small>{detail} · {size:.1f} MB</small></span></a>')

template = (site / 'library.html').read_text()
header = template[template.index('  <header'):template.index('  <main>')]
header = header.replace(' aria-current="page"', '')
header = header.replace('<a href="library.html">Library</a>', '<a href="library.html">Library</a>\n        <a href="extensions.html" aria-current="page">Extensions</a>')
head = template[:template.index('<body>')]
head = '\n'.join(line for line in head.splitlines() if 'theme-color' not in line and 'og:image' not in line)
head = head.replace('Infinite — Library', 'Infinite · Extensions').replace('library.html', 'extensions.html')
head = head.replace('Free downloadable Field devices for Infinite: dither, liquid glass, lenses and generators. Download a file, import it into a Field Pixel node.', 'Optional packs for Infinite. Download hand, face and pose tracking for your platform.')
head = head.replace('Free downloadable Field devices for Infinite.', 'Optional packs for Infinite, installed from Settings → Extensions.')
head = head.replace('</head>', '  <link rel="stylesheet" href="extensions.css?v=1">\n</head>')
page = head + '\n<body class="extensions-page">\n' + header + '''
  <main>
    <section class="section lib-hero">
      <div class="container">
        <div class="section-head">
          <p class="lib-hand hand">more to play with</p>
          <h1 class="slab">extensions</h1>
          <p class="section-sub">Optional packs for Infinite. Install only what you need.</p>
        </div>
        <article class="download-card" aria-labelledby="tracking-title">
          <h2 id="tracking-title">Tracking</h2>
          <p>Turn a camera or image into controls with Hand Track, Face Track and Pose Track.</p>
          <p><b>Install in the app:</b> Settings → Extensions → Tracking → Install.</p>
          <p>For an offline install, download the pack for your platform below.</p>
          <div class="dl-row extensions-downloads">''' + '\n'.join(buttons) + '''</div>
          <dl class="specs mono"><div><dt>pack</dt><dd>v0.1.0</dd></div><div><dt>requires</dt><dd>Infinite v0.5.0+</dd></div><div><dt>price</dt><dd>Free</dd></div></dl>
        </article>
      </div>
    </section>
    <section class="section" aria-label="Offline installation">
      <div class="container lib-how">
        <div><p class="recipe-num mono">01</p><h3>Download</h3><p>Choose the Tracking pack for your platform. Keep the ZIP intact.</p></div>
        <div><p class="recipe-num mono">02</p><h3>Install</h3><p>Open Settings → Extensions → Install from file, then choose the ZIP.</p></div>
        <div><p class="recipe-num mono">03</p><h3>Patch</h3><p>Add Hand Track, Face Track or Pose Track and connect a Video In or image node.</p></div>
      </div>
    </section>
  </main>
  <script src="site.js?v=4" defer></script>
</body>
</html>
'''
(site / 'extensions.html').write_text(page)
(site / 'extensions.css').write_text('''/* Existing website components, with a responsive platform download grid. */
.extensions-page .download-card { max-width: 900px; margin: 0 auto; }
.extensions-page .download-card p { margin: 16px 0; color: var(--ink2); }
.extensions-downloads { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 16px; }
.extensions-downloads .btn { min-width: 0; }
@media (max-width: 560px) { .extensions-downloads { grid-template-columns: 1fr; } }
''')
(site / 'assets/extensions/extensions.json').write_text(json.dumps(catalog, indent=2) + '\n')
for filename in ['index.html', 'library.html']:
    path = site / filename
    text = path.read_text()
    if 'href="extensions.html"' not in text:
        marker = '<a href="library.html"'
        if filename == 'index.html':
            start = text.index(marker)
            end = text.index('</a>', start) + 4
        else:
            end = text.index('</a>', text.index(marker)) + 4
        text = text[:end] + '\n        <a href="extensions.html">Extensions</a>' + text[end:]
        path.write_text(text)
sitemap = site / 'sitemap.xml'
text = sitemap.read_text()
if 'extensions.html' not in text:
    text = text.replace('</urlset>', '  <url><loc>https://n1m21n.github.io/Infinite/extensions.html</loc><priority>0.6</priority></url>\n</urlset>')
    sitemap.write_text(text)

notes = '''## Download Infinite v0.5.0
Choose one app download for your platform.

| Platform | Download |
|---|---|
| macOS, Intel and Apple Silicon | [Infinite.dmg](https://github.com/n1m21n/Infinite/releases/download/v0.5.0/Infinite.dmg) |
| Windows x64, most PCs | [Infinite-windows-x64.zip](https://github.com/n1m21n/Infinite/releases/download/v0.5.0/Infinite-windows-x64.zip) |
| Windows ARM64 | [Infinite-windows-ARM64.zip](https://github.com/n1m21n/Infinite/releases/download/v0.5.0/Infinite-windows-ARM64.zip) |
| Linux x86_64 | [Infinite-x86_64.AppImage](https://github.com/n1m21n/Infinite/releases/download/v0.5.0/Infinite-x86_64.AppImage) |

Optional Tracking packs install from **Settings → Extensions** or the [Extensions page](https://n1m21n.github.io/Infinite/extensions.html). The small `extensions.json` asset is used automatically by the app.

## What's new
- More consistent controls, previews, menus and editor windows, with improved light-theme contrast and reduced motion.
- Undo History, canvas find, 15 starter templates, node copy/paste between patches, clearer errors and a Cook times overlay.
- Sketch: JavaScript drawing, 17 presets and SVG import. Sketch 3D: JavaScript meshes and 6 presets.
- Field Notes for scripted note sequences, and MIDI Out for hardware and virtual ports.
- Spatial Mixer: binaural mixing, measured HRTF, room controls, limiter, loudness meter and 24-bit export. Head tracking is macOS only.
- Hand Track, Face Track and Pose Track, with optional Tracking packs for all supported platforms.
- Delaunay Mesh, Voronoi Cells, Curve Ops and quadric Decimate.
- Dither, gradient Perlin and Simplex noise, Oklab/OKLCh colour interpolation, and palette gamut mapping.
- Time and Spring modes for Smooth; Game of Life and Smooth Life presets for Field Pixel.
- Fix Sketch crashes, Smooth spring instability, Spatial Mixer cable restore, extension download shutdown, stale tracking output, MIDI note cleanup and settings persistence.

**Existing patches:** Palette colours may look different. Re-check Ocean and Audio Ribbon orientation. Rename Field variables called `beat` or `noteNum`.

**Manuals:** [Node reference](https://n1m21n.github.io/Infinite/assets/Infinite_Node_Reference_Manual.pdf) · [Field language](https://n1m21n.github.io/Infinite/assets/Field_Language_Manual.pdf) · [Headless guide](https://n1m21n.github.io/Infinite/assets/Infinite_Headless_User_Guide.pdf) · [Node field guide](https://n1m21n.github.io/Infinite/assets/The_Node_Field_Guide.pdf)
'''
(repo / 'docs/release-notes-v0.5.0.md').write_text(notes)
(assets / 'notes-clean.md').write_text(notes)
print('Generated Extensions page, four hosted packs, website catalog and compact release notes.')
