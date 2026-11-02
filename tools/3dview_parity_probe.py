#!/usr/bin/env python3
"""Every Blender setting, operator and key in the 3D View's scope, and which ones Infinite's 3D View already has.

Reads Blender's own data (RNA properties with their UI names and options, operators, the default keymap) by running
inside the installed Blender, headless. Reference only, nothing is copied.
Usage: tools/3dview_parity_probe.py [--md docs/plans/3d-view/blender-inventory.md] [--unresolved] [--tracker]
--tracker rewrites the coverage table and Missing list in docs/plans/3d-view/parity.md from the map.
MAP (docs/plans/3d-view/parity-map.tsv: Struct.identifier or operator id, status, where) overrides the keyword guess:
status done = Infinite has it, missing = in scope and not built, deferred = needs renderer work or approval, out = outside the README.
--unresolved prints how many in-scope settings and operators have no map row (0 = every row decided).
Present = the setting's UI name or identifier appears in the 3D View sources (keyword probe; confirm by testing).
"""
import json, pathlib, re, subprocess, sys, tempfile

BLENDER = '/Applications/Blender.app/Contents/MacOS/Blender'
ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ['src/app/panels/View3DPanel.cpp', 'src/nodes/Scene3DNode.cpp', 'src/nodes/Scene3DNode.h', 'src/nodes/SceneNodes.cpp',
           'src/nodes/SceneNodes.h', 'src/nodes/Render3DHdr.cpp', 'src/nodes/Render3DPano.cpp', 'src/nodes/Render3DPost.cpp',
           'src/nodes/Render3DTonemap.h', 'src/nodes/EnvironmentNode.cpp', 'src/nodes/OutputNode.cpp', 'src/nodes/Group3DNode.h']

# (area, RNA struct, identifier filter or None). Every setting of each struct is listed; SCOPE_OUT marks the ones the
# README leaves out (modelling, sculpt, paint, rigging, modifiers, materials, particles, physics, animation data).
STRUCTS = [
 ('Viewport: View tab and header', 'SpaceView3D', None),
 ('Viewport: View tab and header', 'RegionView3D', None),
 ('Viewport: 3D cursor', 'View3DCursor', None),
 ('Viewport: shading', 'View3DShading', None),
 ('Viewport: overlays', 'View3DOverlay', None),
 ('Transform, snap, pivot', 'ToolSettings', None),
 ('Transform, snap, pivot', 'TransformOrientationSlot', None),
 ('Object', 'Object', None),
 ('Camera', 'Camera', None),
 ('Camera', 'CameraDOFSettings', None),
 ('Camera', 'CameraBackgroundImage', None),
 ('Camera', 'CameraStereoData', None),
 ('Light', 'Light', None), ('Light', 'PointLight', None), ('Light', 'SunLight', None),
 ('Light', 'SpotLight', None), ('Light', 'AreaLight', None),
 ('Light probe', 'LightProbe', None), ('Light probe', 'LightProbeSphere', None), ('Light probe', 'LightProbePlane', None), ('Light probe', 'LightProbeVolume', None),
 ('Constraints (aim only)', 'Constraint', r'^(influence|mute|enabled|owner_space|target_space)$'), ('Constraints (aim only)', 'TrackToConstraint', None), ('Constraints (aim only)', 'DampedTrackConstraint', None),
 ('World', 'World', None), ('World', 'WorldMistSettings', None), ('World', 'WorldLighting', None),
 ('Render', 'RenderSettings', None),
 ('Render', 'SceneEEVEE', None),
 ('Render', 'SceneDisplay', None),
 ('Render', 'ColorManagedViewSettings', None),
 ('Render', 'ColorManagedDisplaySettings', None),
 ('Output', 'ImageFormatSettings', None),
 ('Output', 'FFmpegSettings', None),
 ('Scene', 'Scene', None),
 ('Scene', 'UnitSettings', None),
 ('View layer', 'ViewLayer', None),
 ('View layer', 'ViewLayerEEVEE', None),
 ('Outliner', 'SpaceOutliner', None),
 ('Preferences: navigation and input', 'PreferencesInput', None),
 ('Preferences: viewport', 'PreferencesView', r'^(show_gizmo|gizmo_|mini_axis|smooth_view|rotation_angle|show_object_info|show_view_name|show_playback_fps|object_origin|use_fresnel|lookdev|show_navigate|use_mouse_over_open|show_splash|show_tooltips)'),
 ('Preferences: viewport', 'PreferencesEdit', r'^(undo_|use_global_undo|object_align|use_enter_edit|use_duplicate|use_visual_keying|keyframe|use_auto_keying)'),
]
# ID and RNA bookkeeping, never a user setting.
OUT = r'(^rna_type$|^bl_|^is_|session_uid|original|library|override|use_fake_user|use_extra_user|^users$|^tag$|asset_data|id_type|name_full|id_properties|preview$|^animation_data$|filepath_raw)'
# Domain words the README puts out of scope, matched as whole words between underscores ("face" not "interface").
# A row that matches is listed as "out" with the word, never dropped.
SCOPE_OUT = r'(particle|modifier|shape_key|vertex|weight|sculpt|paint|uv|rigid|soft_body|collision|field|pose|bone|armature|grease|gpencil|curve|mesh|edit|lattice|metaball|bake|cloth|fluid|dynamic|face|edge|normal|statvis|retopology|sequencer|sequence|strip|keying|keyframe|audio|sound|speaker|motion_path|paths|material|texture|hair|volume_add|lineart|freestyle|shaderfx|effector|force|explode|fur|liquid|smoke|subdivision|voxel|skin|hook|geometry_node|nla|action|fcurve|driver|marker|collection|make_local|single_user|dupli|update_shapes|visual_geometry|light_linking|lightgroup|stereo|multiview|views_format|compositor|compositing|workspace|annotation|cycles|font|text|mask|movieclip|tracking|node|transfer|mode_set|unwrap|select_mirror|data_instance|custom_update|assign_property|fix_to_camera|auto_normalize|lock_relative|multipaint|lock_object_modes|preview|fake_user|override|library|asset)'
OPS = r'^(VIEW3D|OBJECT|TRANSFORM|OUTLINER|RENDER|ED|SCREEN|CAMERA|WORLD|SCENE)_OT_'
# In scope even though a word above matches: Infinite exports with sound, and these belong to the camera, look or output.
KEEP = r'(motion_blur_shutter_curve|curve_mapping|mouse_emulate|stamp_|audio_codec|audio_bitrate|audio_channels|audio_mixrate|audio_volume)'
OPS_SKIP = r'^SCREEN_OT_(?!(repeat_last|repeat_history|redo_last|frame_jump|frame_offset|animation_play|region_toggle|screen_full_area|keyframe_jump)$)'
KEYMAPS = ['3D View', '3D View Generic', 'Object Mode', 'Object Non-modal', 'Outliner', 'Transform Modal Map', 'View3D Fly Modal',
           'View3D Walk Modal', 'View3D Rotate Modal', 'View3D Move Modal', 'View3D Zoom Modal', 'View3D Gesture Circle',
           'Gesture Box', 'Gesture Lasso', 'Gesture Zoom Border', 'Screen', 'Frames']

INSIDE = r'''
import bpy, json, re, sys
cfg = json.loads(open(sys.argv[sys.argv.index('--') + 1]).read())
out = {'version': bpy.app.version_string, 'props': [], 'ops': [], 'keys': []}
for area, sname, filt in cfg['structs']:
    st = getattr(bpy.types, sname, None)
    if st is None: continue
    for p in st.bl_rna.properties:
        ident = p.identifier
        if filt and not re.search(filt, ident): continue
        base = st.bl_rna.base
        if base is not None and base.identifier != sname and ident in base.properties.keys() and base.identifier in cfg['names'] + ['ID', 'Space']: continue
        if re.search(cfg['out'], ident) or p.type == 'POINTER' and p.fixed_type.identifier in ['ID', 'AnimData'] + cfg['names']: continue
        opts = [i.name for i in p.enum_items] if p.type == 'ENUM' else []
        out['props'].append([area, sname, ident, p.name or ident, p.type, opts, p.description[:110]])
for mod in dir(bpy.ops):
    for o in dir(getattr(bpy.ops, mod)):
        try: rna = getattr(getattr(bpy.ops, mod), o).get_rna_type()
        except Exception: continue
        if re.search(cfg['ops'], rna.identifier) and not re.search(cfg['ops_skip'], rna.identifier):
            out['ops'].append([rna.identifier, rna.name, (rna.description or '')[:110]])
import os
sys.path.append(os.path.join(bpy.utils.system_resource('SCRIPTS'), 'presets', 'keyconfig'))
from keymap_data import blender_default as bd
for name, _, data in bd.generate_keymaps(bd.Params(legacy=False)):
    if name not in cfg['keymaps']: continue
    for it in data.get('items', []):
        idname, ev = it[0], it[1] or {}
        if not ev: continue
        mods = [m for m in ('ctrl', 'shift', 'alt', 'oskey') if ev.get(m) in (True, 1)]
        key = '+'.join([m.capitalize() for m in mods] + [str(ev.get('type'))]) + ('' if ev.get('value') in ('PRESS', None) else ' ' + str(ev.get('value')).lower())
        props = (it[2] or {}).get('properties') or []
        out['keys'].append([name, key, idname, ', '.join(f'{k}={v}' for k, v in props)[:60]])
open(cfg['dst'], 'w').write(json.dumps(out))
'''

def dump():
    tmp = pathlib.Path(tempfile.mkdtemp())
    cfg = {'structs': STRUCTS, 'names': [x[1] for x in STRUCTS], 'out': OUT, 'ops': OPS, 'ops_skip': OPS_SKIP, 'keymaps': KEYMAPS, 'dst': str(tmp / 'dump.json')}
    (tmp / 'cfg.json').write_text(json.dumps(cfg)); (tmp / 'inside.py').write_text(INSIDE)
    r = subprocess.run([BLENDER, '-b', '--factory-startup', '--python', str(tmp / 'inside.py'), '--', str(tmp / 'cfg.json')],
                   capture_output=True)
    if not (tmp / 'dump.json').exists():
        sys.exit('Blender side failed:\n' + r.stdout.decode()[-2000:])
    return json.loads((tmp / 'dump.json').read_text())

MAP = ROOT / 'docs/plans/3d-view/parity-map.tsv'

def load_map():
    m = {}
    if MAP.exists():
        for ln in MAP.read_text().splitlines()[1:]:
            c = ln.split('\t')
            if len(c) >= 2 and c[0]:
                m[c[0]] = (c[1], c[2] if len(c) > 2 else '')
    return m

TRACKER = ROOT / 'docs/plans/3d-view/parity.md'

def tracker(props, ops, mp):
    """Rewrite the coverage table and the "Missing, ranked" list of parity.md from the map, so they never drift."""
    def st(key): return mp.get(key, ('', ''))[0]
    areas = list(dict.fromkeys(p[0] for p in props))
    rows, tot = [], [0, 0, 0, 0, 0]
    for a in areas:
        ids = [f'{p[1]}.{p[2]}' for p in props if p[0] == a]
        n = [sum(st(i) == k for i in ids) for k in ('done', 'missing', 'deferred', 'out')] + [0]
        n[4] = sum(n[:4]); tot = [x + y for x, y in zip(tot, n)]
        rows.append(f'| {a} | ' + ' | '.join(map(str, n)) + ' |')
    on = [sum(st(o[0]) == k for o in ops) for k in ('done', 'missing', 'deferred', 'out')] + [0]
    on[4] = sum(on[:4])
    table = ['| Area | Done | Missing | Deferred | Out | Mapped |', '|---|---|---|---|---|---|'] + rows
    table.append('| **All settings** | ' + ' | '.join(f'**{x}**' for x in tot) + ' |')
    table.append('| **Operators** | ' + ' | '.join(f'**{x}**' for x in on) + ' |')
    table.append('| **Keymap entries** | | | | | not mapped: compare with README Controls by hand |')
    out = []
    for a in areas:
        items = [f'- {p[3]} (`{p[1]}.{p[2]}`): {mp[p[1] + "." + p[2]][1]}' for p in props if p[0] == a and st(f'{p[1]}.{p[2]}') == 'missing']
        if items: out += [f'### {a} ({len(items)})', ''] + items + ['']
    items = [f'- {o[1]} (`{o[0]}`): {mp[o[0]][1]}' for o in ops if st(o[0]) == 'missing']
    if items: out += [f'### Operators ({len(items)})', ''] + items + ['']
    t = TRACKER.read_text()
    a, b = t.index('| Area | Done |'), t.index('\nRe-run the probe')
    t = t[:a] + '\n'.join(table) + '\n' + t[b:]
    c = t.index('### ', t.index('## Missing, ranked'))
    t = t[:c] + '\n'.join(out).rstrip() + '\n'
    TRACKER.write_text(t)

def main():
    d = dump()
    mp = load_map()
    src = ''.join((ROOT / f).read_text(errors='ignore') for f in SOURCES if (ROOT / f).exists())
    low = src.lower()
    def has(name, ident=None):
        return (len(name) > 2 and name.lower() in low) or (ident is not None and re.search(r'\b' + re.escape(ident) + r'\b', src) is not None)
    def scope(ident):
        if re.search(KEEP, ident.lower()): return 'in'
        m = re.search(r'(?:^|_)' + SCOPE_OUT + r's?(?=_|$)', ident.lower())
        return f'out: {m.group(1)}' if m else 'in'
    seen, props = set(), []
    for area, st, ident, name, typ, opts, desc in d['props']:
        if (st, ident) in seen: continue
        seen.add((st, ident))
        ok, sc = has(name, ident), scope(ident)
        r = mp.get(f'{st}.{ident}')
        if r:
            ok = r[0] == 'done'
            sc = 'in' if r[0] in ('done', 'missing') else f'{r[0]}: {r[1]}'
        props.append((area, st, ident, name, typ, opts, [o for o in opts if has(o)], ok, sc, desc, r is not None))
    def op_row(i, n, ds):
        r = mp.get(i)
        if not r:
            return (i, n, ds, has(n), scope(i), False)
        return (i, n, ds, r[0] == 'done', 'in' if r[0] in ('done', 'missing') else f'{r[0]}: {r[1]}', True)
    ops = [op_row(i, n, ds) for i, n, ds in sorted({o[0]: o for o in d['ops']}.values())]
    if '--unresolved' in sys.argv:
        left = [p for p in props if p[8].startswith('in') and not p[10]] + [o for o in ops if o[4] == 'in' and not o[5]]
        print(len(left))
        if '-v' in sys.argv:
            for p in left:
                print(p[1] + '.' + p[2] if len(p) > 10 else p[0])
        return
    if '--tracker' in sys.argv:
        return tracker(props, ops, mp)
    keys = [(km, k, op, pr, scope(op)) for km, k, op, pr in d['keys']]
    areas = list(dict.fromkeys(p[0] for p in props))
    md = ['# Blender vs Infinite 3D View: full inventory', '',
          f'Generated by `tools/3dview_parity_probe.py` from Blender {d["version"]}, run headless: every setting (RNA property with its',
          'UI name and options), every operator and every default key Blender exposes for these areas. Names and options only, as a user',
          'sees them in the app; no Blender source is read (AGENTS.md rule 1).', '',
          'Infinite = the UI name or identifier is found in the 3D View sources. Keyword probe: a "-" can be the same thing under another',
          'name and a "yes" can be a different thing with the same word, so confirm by testing. Scope "out: word" = the README leaves it',
          'out (that word matched); listed so nothing is missed, not counted in the in-scope totals.', '',
          '## Totals', '', '| Area | In scope: present / all | Options present (in scope) | Out of scope |', '|---|---|---|---|']
    for a in areas:
        rows = [p for p in props if p[0] == a]; ins = [p for p in rows if p[8] == 'in']
        md.append(f'| {a} | {sum(p[7] for p in ins)}/{len(ins)} | {sum(len(p[6]) for p in ins)}/{sum(len(p[5]) for p in ins)} | {len(rows) - len(ins)} |')
    ins = [p for p in props if p[8] == 'in']
    md.append(f'| **All settings** | **{sum(p[7] for p in ins)}/{len(ins)}** | **{sum(len(p[6]) for p in ins)}/{sum(len(p[5]) for p in ins)}** | **{len(props) - len(ins)}** |')
    oi = [o for o in ops if o[4] == 'in']
    md.append(f'| **Operators (features)** | **{sum(o[3] for o in oi)}/{len(oi)}** | | **{len(ops) - len(oi)}** |')
    ki = [k for k in keys if k[4] == 'in']
    md.append(f'| **Keymap entries** | not probed ({len(ki)} in scope) | | **{len(keys) - len(ki)}** |')
    for a in areas:
        md += ['', f'## {a}', '', '| Setting | Struct.identifier | Type | Options (found in **bold**) | Scope | Infinite |', '|---|---|---|---|---|---|']
        for _, st, ident, name, typ, opts, got, ok, sc, desc, _m in sorted((p for p in props if p[0] == a), key=lambda p: p[8] != 'in'):
            o = ', '.join(f'**{x}**' if x in got else x for x in opts).replace('|', '/')
            md.append(f'| {name} | {st}.{ident} | {typ.lower()} | {o} | {sc} | {"yes" if ok else "-"} |')
    md += ['', '## Operators', '', 'Every operator Blender ships for the viewport, objects, transform, outliner, render, camera, world, undo and repeat.', '',
           '| Operator | Id | What it does | Scope | Infinite |', '|---|---|---|---|---|']
    for ident, name, desc, ok, sc, _m in sorted(ops, key=lambda o: (o[4] != 'in', o[0])):
        md.append(f'| {name} | {ident} | {desc.replace("|", "/")} | {sc} | {"yes" if ok else "-"} |')
    md += ['', '## Keymap', '', "Blender's default keymap for the 3D View, object mode, outliner, gestures, frames and the viewport's modal maps.",
           'Not matched against Infinite: compare with the Controls table in README.md by hand.', '',
           '| Keymap | Key | Operator | Properties | Scope |', '|---|---|---|---|---|']
    for km, key, op, pr, sc in keys:
        md.append(f'| {km} | {key} | {op} | {pr.replace("|", "/")} | {sc} |')
    text = '\n'.join(md) + '\n'
    if '--md' in sys.argv:
        pathlib.Path(sys.argv[sys.argv.index('--md') + 1]).write_text(text)
    else:
        print(text)

if __name__ == '__main__':
    main()
