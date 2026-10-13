#!/usr/bin/env python3
"""Turbo 0.54: turns the Magenta Groove MIDI Dataset (MIDI-only zip) into a Drum Sequencer pattern pack.

    python3 scripts/import_groove_midi.py build/groove-v1.0.0-midionly.zip

Writes assets/drum-patterns/groove-midi/<Style>/*.mid with an index.json per style (title, style,
bpm) and CREDITS.txt. Keeps the "beat" performances (fills are a bar or two and repeat the beats);
long performances are split into A / B / C by the Drum Sequencer's MIDI browser. The app copies the
pack once into %LOCALAPPDATA%\\Infinite\\DrumPatterns.

Dataset: https://magenta.tensorflow.org/datasets/groove, CC BY 4.0, Gillick, Roberts, Engel,
Eck, Bamman (2019). Standard library only.
"""
import json
import re
import sys
import zipfile
from pathlib import Path

NAME = re.compile(r'(?P<n>\d+)_(?P<style>[a-z0-9\-]+)_(?P<bpm>\d+)_(?P<kind>beat|fill)_(?P<ts>\d+-\d+)\.midi?$', re.I)

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = Path(__file__).resolve().parents[1]
    out = root / 'assets' / 'drum-patterns' / 'groove-midi'
    out.mkdir(parents=True, exist_ok=True)
    index = {}
    kept = 0
    with zipfile.ZipFile(sys.argv[1]) as z:
        for info in z.infolist():
            parts = Path(info.filename).parts
            m = NAME.search(info.filename)
            if not m or m['kind'].lower() != 'beat':
                continue
            drummer = next((p for p in parts if p.startswith('drummer')), 'drummer')
            session = next((p for p in parts if p.startswith('session') or p.startswith('eval')), 'session')
            main_style, _, sub = m['style'].lower().partition('-')
            style = main_style.replace('neworleans', 'new orleans').replace('middleeastern', 'middle eastern') \
                              .replace('afrocuban', 'afro-cuban').replace('hiphop', 'hip hop').title()
            sub_txt = f' {sub}' if sub else ''
            ts = m['ts'].replace('-', '/')
            title = f"{style}{sub_txt} {m['bpm']} bpm ({ts}) - {drummer.replace('drummer', 'drummer ')} {session} #{m['n']}"
            fname = f"{drummer}-{session}-{int(m['n']):03d}-{m['style'].lower()}-{m['bpm']}.mid"
            folder = out / style
            folder.mkdir(exist_ok=True)
            (folder / fname).write_bytes(z.read(info))
            index.setdefault(style, []).append({'file': fname, 'title': title, 'style': style, 'bpm': int(m['bpm'])})
            kept += 1
    for style, items in index.items():
        items.sort(key=lambda e: (e['bpm'], e['title']))
        (out / style / 'index.json').write_text(json.dumps({'patterns': items}, indent=1), encoding='utf-8')
    (out / 'CREDITS.txt').write_text(
        'Groove MIDI Dataset (Magenta, Google), CC BY 4.0 - https://magenta.tensorflow.org/datasets/groove\n'
        'Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck, David Bamman. "Learning to Groove with Inverse\n'
        'Sequence Transformations." ICML 2019. Human drummers on a Roland TD-11; kept here: the beat performances,\n'
        'renamed and grouped by style for the Infinite-Turbo Drum Sequencer.\n', encoding='utf-8')
    print(f'{kept} patterns in {len(index)} styles -> {out}')

if __name__ == '__main__':
    main()
