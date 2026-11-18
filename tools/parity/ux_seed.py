#!/usr/bin/env python3
"""Seed the UX map (ux-map.tsv) for PhotoCraft commands by finding each command's label in the 2D View's menus.

A label found in View2DPanel.cpp as T("Label") is marked ok with its line as proof ("presence"; behaviour proof is the
VIEW2DTEST check named in the existing row, which this keeps). A label not found is marked missing. Rows already in the
map are never overwritten unless --force. Context-menu and gap rows are left for hand review.

Usage: tools/parity/ux_seed.py [--map TSV] [--force]
"""
import argparse, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ALIAS = {  # PhotoCraft label -> the 2D View's word for it
    'Free Transform': 'Transform', 'Deselect': 'Deselect', 'Select All': 'Select All', 'Inverse': 'Invert',
    'Group Layers': 'Group', 'Ungroup Layers': 'Ungroup', 'New Layer': 'New Layer', 'Duplicate Layer': 'Duplicate',
    'Delete Layer': 'Delete', 'Zoom In': 'Zoom In', 'Zoom Out': 'Zoom Out', 'Fit on Screen': 'Fit',
    'Actual Pixels': '100%', 'Step Backward': 'Undo', 'Step Forward': 'Redo', 'Paste in Place': 'Paste in Place',
}


# Commands the app shell owns for every panel (patch files, undo, prefs): not a 2D View menu.
APP = {'edit.undo': 'ok', 'edit.redo': 'ok', 'edit.toggleLastState': 'ok', 'edit.keyboardShortcuts': 'n/a',
       'edit.menus': 'n/a', 'edit.colorSettings': 'n/a', 'edit.toolbar': 'n/a', 'file.new': 'n/a', 'file.close': 'n/a',
       'file.closeAll': 'n/a', 'file.closeOthers': 'n/a', 'file.exit': 'n/a', 'file.save': 'n/a', 'file.saveAs': 'n/a',
       'file.saveACopy': 'n/a', 'file.revert': 'n/a', 'file.openAs': 'n/a', 'file.package': 'n/a', 'file.fileInfo': 'n/a',
       'edit.purge.clipboard': 'n/a', 'edit.purge.histories': 'n/a', 'edit.purge.undo': 'n/a',
       'edit.presets.exportImportPresets': 'n/a', 'edit.presets.migratePresets': 'n/a', 'edit.presets.presetManager': 'n/a',
       'file.export.exportPreferences': 'n/a'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', default=str(pathlib.Path.home() / 'Infinite/repo/internal/plans/2d-view/parity/ux-map.tsv'))
    ap.add_argument('--force', action='store_true')
    a = ap.parse_args()
    out = subprocess.run([sys.executable, str(HERE / 'ux_rules.py')], capture_output=True, text=True).stdout
    rules = re.findall(r'^\| (cmd:[^ ]+) \| (.*?) \|', out, re.M)
    src = (ROOT / 'src/app/panels/View2DPanel.cpp').read_text(errors='ignore').splitlines()
    labels = {}
    for i, line in enumerate(src, 1):
        for m in re.finditer(r'"([^"]{3,60})"', line):
            labels.setdefault(m.group(1).rstrip('.…').strip().lower(), i)
    p = pathlib.Path(a.map).expanduser()
    mp, head = {}, '# key\tstatus\tproof (VIEW2DTEST check, or code line for presence-only rows)\n'
    if p.exists():
        for line in p.read_text().splitlines():
            if line.startswith('#'):
                continue
            parts = line.split('\t')
            if len(parts) >= 2:
                mp[parts[0]] = parts[1:]
    added = found = 0
    for key, text in rules:
        if key in mp and not a.force:
            continue
        cid = key[4:]
        if cid in APP:
            mp[key] = [APP[cid], 'app-wide: Cmd+Z / Shift+Cmd+Z history' if APP[cid] == 'ok' else 'app shell owns it (patch file, prefs)']
            added += 1
            continue
        label = re.sub(r'\s*\[.*\]$', '', text).split(' › ')[-1].rstrip('.…').strip()
        label = ALIAS.get(label, label)
        line = labels.get(label.lower())
        mp[key] = ['ok', f'menu label present: View2DPanel.cpp:{line}'] if line else ['missing', 'no 2D View command']
        found += bool(line)
        added += 1
    p.write_text(head + ''.join(f'{k}\t' + '\t'.join(v) + '\n' for k, v in sorted(mp.items())))
    print(f'seeded {added} rows ({found} present, {added - found} missing) -> {p}')


if __name__ == '__main__':
    main()
