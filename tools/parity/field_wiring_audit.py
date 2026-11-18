#!/usr/bin/env python3
"""Static wiring audit for the 2D View's model: every field of Shape, Fx, Effect, Layer, Document (Scene2DNode.h) and
BrushEngine::Brush, checked in three places.

  ui      a `.field` / `->field` use in View2DPanel.cpp (a control, or code that sets it)
  save    written in a save function and read in a load function (Serialize/Write*/…Text vs Parse/Read*)
  engine  used by render or engine code outside save/load (Scene2D*.cpp, BrushEngine.cpp)

A field missing one is a candidate bug: a dead knob (ui, no engine), a setting lost on reopen (no save), or model state
nobody can reach (engine, no ui). Known-intended rows live in SKIP below with the reason.

Usage: tools/parity/field_wiring_audit.py [--all] [--tsv FILE]
Exit 1 if any unexplained gap remains (a gate), 0 otherwise.
"""
import argparse, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
HDR = ROOT / 'src/nodes/Scene2DNode.h'
BRUSH_H = ROOT / 'src/core/BrushEngine.h'
UI = [ROOT / 'src/app/panels/View2DPanel.cpp']
NODE = sorted((ROOT / 'src/nodes').glob('Scene2D*.cpp'))
BRUSH_CPP = [ROOT / 'src/core/BrushEngine.cpp']
STRUCTS = {'Shape': HDR, 'Fx': HDR, 'Effect': HDR, 'Layer': HDR, 'Document': HDR, 'Brush': BRUSH_H}
SAVE_FN = re.compile(r'^(Serialize|Write\w*|\w*ToText|\w*Save\w*|PresetText|Dump\w*)$')
LOAD_FN = re.compile(r'^(Parse\w*|Read\w*|\w*FromText|\w*Load\w*|Apply\w*Preset)$')

# field -> reason it is fine to miss a column (kept short; a row here is a decision, not a todo)
SKIP = {
    'Layer.id': 'identity', 'Layer.parent': 'tree', 'Layer.kind': 'identity', 'Layer.tiles': 'pixel store',
    'Layer.mtiles': 'pixel store', 'Layer.open': 'row fold', 'Layer.made': 'runtime',
    'Effect.shown': 'read by CookFrame (canvas visibility of the effect node)',
    'Effect.entry': 'read by CookFrame (the chain entry node)',
    'Layer.label': 'row colour: the panel is its only reader',
    'Brush.color': 'the swatch colour, set at stroke start', 'Brush.key': 'sampled where a stroke starts',
    'Brush.sourceOffset': 'clone source, set per stroke', 'Brush.sourceAnchor': 'clone source, set per stroke',
    'Brush.tipTex': 'GL texture', 'Brush.grainTex': 'GL texture', 'Brush.colorTex': 'GL texture',
}


def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def struct_body(text, name):
    m = re.search(r'\bstruct\s+' + name + r'\b[^;{]*\{', text)
    if not m:
        return ''
    i, depth = m.end(), 1
    while i < len(text) and depth:
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def fields(body):
    """Data members at the struct's top level (nested braces and member functions skipped)."""
    out, depth, cur = [], 0, ''
    for ch in body:
        if ch == '{':
            depth += 1
            if depth == 1:
                cur += '{'
            continue
        if ch == '}':
            depth -= 1
            if depth == 0:
                cur += '}'
            continue
        if depth:
            continue
        cur += ch
        if ch == ';':
            out.append(cur.strip())
            cur = ''
    names = []
    for decl in out:
        d = re.sub(r'\s+', ' ', decl.rstrip(';'))
        if not d or d.startswith(('using ', 'enum', 'static ', 'friend ', 'typedef ', 'struct ')) or re.search(r'\boperator\b', d):
            continue
        head = re.split(r'=', d, 1)[0]
        if '(' in head:   # a function
            continue
        d = re.sub(r'\{\}', '', d)
        d = re.sub(r'<[^<>]*(<[^<>]*>[^<>]*)*>', '<>', d)   # template args out (they hold commas)
        parts = [p.strip() for p in d.split(',')]
        first = re.match(r'^(?:mutable |const |unsigned |signed |long |short )*[\w:<>]+[\s\*&]+(\w+)', parts[0])
        if not first:
            continue
        names.append(first.group(1))
        for p in parts[1:]:
            m = re.match(r'^[\*&]*\s*(\w+)', p)
            if m and not re.match(r'^\d', m.group(1)):
                names.append(m.group(1))
    return names


FN_HEAD = re.compile(r'^(?: {0,3})(?:[\w:<>,\*&~]+\s+)*?(?:[\w]+::)?(~?\w+)\s*\([^;]*\)\s*(?:const)?\s*(?:override)?\s*$')


def split_functions(path):
    """{function name: body text} by brace matching from each top-level-looking head."""
    lines = strip_comments(path.read_text(errors='ignore')).split('\n')
    out, i = [], 0
    while i < len(lines) - 1:
        m = FN_HEAD.match(lines[i])
        if m and lines[i + 1].strip() == '{' and m.group(1) not in ('if', 'for', 'while', 'switch', 'catch'):
            depth, j, buf = 0, i + 1, []
            while j < len(lines):
                depth += lines[j].count('{') - lines[j].count('}')
                buf.append(lines[j])
                j += 1
                if depth <= 0:
                    break
            out.append((m.group(1), '\n'.join(buf)))
            i = j
            continue
        i += 1
    return out


def uses(text, f):
    return re.search(r'(?:\.|->)' + re.escape(f) + r'\b', text) is not None


def audit():
    """[(struct.field, ui, save, engine, gap, why)]; gap 'ok' or the missing columns."""
    ui = '\n'.join(strip_comments(p.read_text(errors='ignore')) for p in UI)
    # Brush settings persist through the panel's BrushFields (prefs and presets), by quoted name.
    brush_io = '\n'.join(b for p in UI for n, b in split_functions(p) if n == 'BrushFields')
    save, load, engine = [], [], []
    for p in NODE + BRUSH_CPP:
        for name, body in split_functions(p):
            (save if SAVE_FN.match(name) else load if LOAD_FN.match(name) else engine).append(body)
    save, load, engine = '\n'.join(save), '\n'.join(load), '\n'.join(engine)
    hdr_inline = strip_comments(HDR.read_text(errors='ignore')) + strip_comments(BRUSH_H.read_text(errors='ignore'))

    rows, gaps = [], 0
    for st, path in STRUCTS.items():
        body = struct_body(strip_comments(path.read_text(errors='ignore')), st)
        for f in fields(body):
            key = f'{st}.{f}'
            u, w, r = uses(ui, f), uses(save, f), uses(load, f)
            if st == 'Brush':
                w = r = ('"' + f + '"') in brush_io
            e = uses(engine, f) or uses(hdr_inline.replace(body, ''), f)
            miss = [c for c, ok in (('ui', u), ('save', w and r), ('engine', e)) if not ok]
            # Brush settings persist through presets; a Brush field with no save is a preset gap, not a doc one.
            why = SKIP.get(key, '')
            if not why and st == 'Document' and miss == ['engine'] and u:
                why = 'view state (the panel is its only reader)'
            if not why and 'ui' in miss and re.search(r'(Tex|TexW|TexH|Uid|Tiles)$', f):
                why = 'runtime handle, set by code'
            if miss and not why:
                gaps += 1
            rows.append((key, 'ui' if u else '-', ('rw' if w and r else 'w' if w else 'r' if r else '-'),
                         'eng' if e else '-', ','.join(miss) or 'ok', why))
    return rows, gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true', help='print every field, not only gaps')
    ap.add_argument('--tsv')
    a = ap.parse_args()
    rows, gaps = audit()
    out = ['field\tui\tsave\tengine\tgap\twhy']
    out += ['\t'.join(r) for r in rows if a.all or (r[4] != 'ok' and not r[5])]
    text = '\n'.join(out) + '\n'
    if a.tsv:
        pathlib.Path(a.tsv).write_text('\n'.join('\t'.join(r) for r in [out[0].split('\t')] + rows) + '\n')
    sys.stdout.write(text)
    print(f'# {len(rows)} fields, {gaps} with an unexplained gap', file=sys.stderr)
    return 1 if gaps else 0


if __name__ == '__main__':
    sys.exit(main())
