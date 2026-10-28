#!/usr/bin/env python3
"""Row v2 -> arrays, for every sound-model tool (SOUND-MODEL.md 0.5, 0.6).

    sound_data.py preset_diva_v2.jsonl          build or refresh the cache and print a summary

load(path) returns a dict of numpy arrays, one entry per preset (rows of one plug-in and one params_hash):
  preset_id, src, label      (N,)        strings
  knobs                      (N, P)      0-1 per host parameter, NaN where the host gave no value
  desc                       (N, T, 43)  per note: 32 log10 bands + 11 scalars (marginal_gate.target)
  mel                        (N, T, 64, 48) uint8, 0.5 dB steps (render_pass.logmel); 0 where a note had no mel
  ok                         (N, T)      the note window was measured
  group                      (N,)        int: presets that must be held out together (below)
  notes                      (T,)        note labels in stimulus order
The cache (<path>.npz) is rebuilt when the jsonl changes size or mtime, so loading is seconds at any size.

Groups, for any library: the preset's bank (banks(): first folder, or the next one down under a generic root) and, across folders,
presets whose knob vectors are identical at 2 decimals (copies saved under another name) are merged with union-find. The split rule
lives here too: a group is held out when crc32(name of its first member's folder) % 5 == 0, unless it holds more than 15% of the
presets (holding out a whole category tests another task). The rule depends only on names, so it is stable as more presets arrive."""
import base64, collections, glob, json, os, re, sys, zlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marginal_gate as mg  # noqa: E402

MEL_SHAPE = (64, 48)
CACHE = 2  # bump when build() changes, so old caches rebuild


def banks(srcs):
    """Bank per preset: the first folder of its path inside the library ('/' or a '.db#' container). A folder holding over half the
    library is a generic root (Omnisphere's Factory, Vital's Presets), so its presets take their next folder down instead."""
    ps = [re.split(r"[/#]", s.split(":", 2)[-1])[:-1] or ["(root)"] for s in srcs]; d = [1] * len(ps)
    while True:
        keys = ["/".join(p[:k]) for p, k in zip(ps, d)]
        top, n = collections.Counter(keys).most_common(1)[0] if keys else ("", 0)
        deeper = [i for i, k in enumerate(keys) if k == top and len(ps[i]) > d[i]]
        if n <= len(ps) / 2 or not deeper:
            return keys
        for i in deeper:
            d[i] += 1


def _groups(folders, knobs):
    parent = list(range(len(folders)))
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]; i = parent[i]
        return i
    first = {}
    for i, f in enumerate(folders):
        if f in first: parent[find(i)] = find(first[f])
        else: first[f] = i
    seen = {}
    for i, k in enumerate(np.round(np.nan_to_num(knobs, nan=-1), 2)):
        h = k.tobytes()
        if h in seen: parent[find(i)] = find(seen[h])
        else: seen[h] = i
    roots = [find(i) for i in range(len(folders))]
    ids = {r: n for n, r in enumerate(dict.fromkeys(roots))}
    return np.array([ids[r] for r in roots])


PARAMS_DIR = "~/Infinite/training-data/params"  # render_pass.param_table writes one table per params_hash here


def same_knobs(h1, h2):
    """Two parameter tables mean the same knob vector when address, name, unit and range agree (a plug-in update that only moves a
    default, Diva 1.4.5 -> 1.4.8, changes the hash but not what knobs[] says)."""
    def tab(h):
        f = glob.glob(os.path.join(os.path.expanduser(PARAMS_DIR), "*_%s.json" % h))
        return [{k: q.get(k) for k in ("address", "name", "unit", "min", "max")} for q in json.load(open(f[0]))["params"]] if f else None
    t1 = tab(h1)
    return t1 is not None and t1 == tab(h2)


def build(path):
    by, order, notes, ph = {}, [], [], None
    for line in open(path):
        r = json.loads(line)
        if r.get("schema") != 2:
            continue
        p = by.get(r["preset_id"])
        if p is None:
            p = by[r["preset_id"]] = {"src": r["src"], "label": r.get("label", ""), "notes": {}}
            order.append(r["preset_id"])
        if "knobs" in r:
            if ph not in (None, r["params_hash"]) and not same_knobs(ph, r["params_hash"]):
                raise SystemExit("%s mixes parameter tables %s and %s: split the file per plug-in version" % (path, ph, r["params_hash"]))
            ph = r["params_hash"]; p["knobs"] = r["knobs"]
        if r["note"] not in notes:
            notes.append(r["note"])
        p["notes"][r["note"]] = r
    keep = [i for i in order if "knobs" in by[i]]
    N, T, P = len(keep), len(notes), max((len(by[i]["knobs"]) for i in keep), default=0)
    knobs = np.full((N, P), np.nan, np.float32); desc = np.zeros((N, T, 43), np.float32)
    mel = np.zeros((N, T) + MEL_SHAPE, np.uint8); ok = np.zeros((N, T), bool)
    for n, i in enumerate(keep):
        k = by[i]["knobs"]; knobs[n, :len(k)] = [np.nan if v is None else v for v in k]
        for t, lab in enumerate(notes):
            r = by[i]["notes"].get(lab)
            if r and "bands" in r:
                desc[n, t] = mg.target(r); ok[n, t] = True
                if r.get("mel"):
                    mel[n, t] = np.frombuffer(base64.b64decode(r["mel"]), np.uint8).reshape(MEL_SHAPE)
    folders = banks([by[i]["src"] for i in keep])
    return {"preset_id": np.array(keep), "src": np.array([by[i]["src"] for i in keep]), "label": np.array([by[i]["label"] for i in keep]),
            "knobs": knobs, "desc": desc, "mel": mel, "ok": ok, "group": _groups(folders, knobs), "folder": np.array(folders),
            "notes": np.array(notes), "params_hash": np.array(ph or "")}


def load(path):
    path = os.path.expanduser(path); st = os.stat(path); cache = path + ".npz"; stamp = "%d:%d:%d" % (st.st_size, int(st.st_mtime), CACHE)
    if os.path.exists(cache):
        z = np.load(cache, allow_pickle=False)
        if str(z["stamp"]) == stamp:
            return {k: z[k] for k in z.files if k != "stamp"}
    d = build(path)
    np.savez(cache, stamp=np.array(stamp), **d)
    return d


def split(d):
    """Boolean masks (random, group) of held-out presets; both stable as the library grows."""
    rnd = np.array([zlib.crc32(i.encode()) % 5 == 0 for i in d["preset_id"]])
    g = d["group"]; size = np.bincount(g); name = {}
    for n, gi in enumerate(g):
        name.setdefault(gi, d["folder"][n])
    held = {gi for gi, f in name.items() if zlib.crc32(str(f).encode()) % 5 == 0 and size[gi] <= 0.15 * len(g)}
    return {"random": rnd, "group": np.array([gi in held for gi in g])}


if __name__ == "__main__":
    d = load(sys.argv[1]); s = split(d)
    print("%d presets, %d knobs (%.0f%% with values), notes %s, %d groups | held out: random %d, group %d"
          % (len(d["preset_id"]), d["knobs"].shape[1], 100 * np.isfinite(d["knobs"]).mean() if d["knobs"].size else 0, list(d["notes"]),
             d["group"].max() + 1 if len(d["group"]) else 0, s["random"].sum(), s["group"].sum()))
