#!/usr/bin/env python3
"""Sound clusters (data-universe step 4): cluster the atlas instruments per family in one timbre space.

Audio pass: instruments with audio features (32-band mean spectrum, centroid, flatness, attack, decay, rms) are
clustered per family with k-means (numpy only, fixed seeds), K by silhouette (2..12). Features: log bands, log
centroid, flatness, log attack, log decay, rms, then z-scored per family.
Tag pass: instruments with no audio are assigned to the nearest cluster by text/tag voting: cosine of their
tokens (name, category, Omnisphere moods/timbre/types) against each cluster's member tokens, plus a small
brightness prior from timbre words (dark/warm vs bright/glassy) against the cluster's centroid.
Instruments with no usable signal stay `cluster: null`, `assigned: "none"`.
Re-run after instrument_atlas.py whenever the audio pass has grown the atlas. Needs numpy (run without -I).

    python3 tools/universe/sound_clusters.py [--data DIR] [--kmax 12]
Writes atlas/clusters.json and atlas/instruments_clustered.jsonl (instruments.jsonl is never touched).
"""
import argparse, collections, json, math, os, re
import numpy as np

STOP = set("the and of for with a an in on to or by from mono stereo sample samples library core instruments instrument "
           "sampler legacy new old v1 v2 v3 vel velocity layers layer long short soft hard 01 02 03 04 05 06 07 08 09 10".split())
BRIGHT = {"bright", "glassy", "cutting", "metallic", "airy", "shimmering", "sparkling", "harmonically", "ringing", "buzzy", "edgy", "crisp", "sharp", "harsh"}
DARK = {"dark", "warm", "soft", "mellow", "muted", "rounded", "deep", "smooth", "dull", "murky", "subby"}


def toks(r):
    t = " ".join([r.get("name", ""), r.get("category", ""), " ".join(r.get("moods") or []), " ".join(r.get("timbre") or []), r.get("types") or ""])
    return [w for w in re.findall(r"[a-z]{3,}", t.lower()) if w not in STOP]


def feat(a):
    b = np.log10(np.asarray(a["bands"], dtype=np.float64) + 1e-6)
    sc = [math.log(max(a.get("centroid") or 1.0, 1.0)), a.get("flatness") or 0.0, math.log1p(a.get("attack_ms") or 0.0),
          math.log1p(a["decay_ms"]) if a.get("decay_ms") is not None else np.nan, a.get("rms") or 0.0]
    return np.concatenate([b, sc])


def kmeans(X, k, seed, iters=60):
    rng = np.random.default_rng(seed)
    C = [X[rng.integers(len(X))]]
    for _ in range(k - 1):  # k-means++
        d = np.min([((X - c) ** 2).sum(1) for c in C], axis=0)
        C.append(X[rng.choice(len(X), p=d / d.sum())] if d.sum() > 0 else X[rng.integers(len(X))])
    C = np.array(C)
    for _ in range(iters):
        lab = ((X[:, None, :] - C[None]) ** 2).sum(2).argmin(1)
        N = np.array([X[lab == j].mean(0) if (lab == j).any() else C[j] for j in range(k)])
        if np.allclose(N, C):
            break
        C = N
    inertia = ((X - C[lab]) ** 2).sum()
    return lab, C, inertia


def silhouette(X, lab):
    k = lab.max() + 1
    if k < 2:
        return -1.0
    D = np.sqrt(((X[:, None, :] - X[None]) ** 2).sum(2))
    s = []
    for i in range(len(X)):
        same = lab == lab[i]
        if same.sum() < 2:
            s.append(0.0)
            continue
        a = D[i, same].sum() / (same.sum() - 1)
        b = min(D[i, lab == j].mean() for j in range(k) if j != lab[i] and (lab == j).any())
        s.append((b - a) / max(a, b, 1e-9))
    return float(np.mean(s))


def best_k(X, kmax):
    n = len(X)
    if n < 6:
        return np.zeros(n, int), X.mean(0, keepdims=True), None
    best = None
    for k in range(2, min(kmax, n // 3) + 1):
        lab, C, ine = min((kmeans(X, k, 1000 + s) for s in range(5)), key=lambda t: t[2])
        sil = silhouette(X, lab)
        if best is None or sil > best[0] + 1e-9:
            best = (sil, lab, C)
    return best[1], best[2], round(best[0], 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=os.path.expanduser("~/Infinite/training-data"))
    ap.add_argument("--kmax", type=int, default=12)
    a = ap.parse_args()
    atlas = os.path.join(a.data, "atlas")
    rows = [json.loads(l) for l in open(os.path.join(atlas, "instruments.jsonl"))]
    fams = collections.defaultdict(list)
    for i, r in enumerate(rows):
        fams[r["family"]].append(i)
    # global descriptor thresholds from every audio row
    allaud = [r["audio"] for r in rows if r.get("audio") and r["audio"].get("bands")]
    cents = np.array([x.get("centroid") or 0 for x in allaud])
    decs = np.array([x["decay_ms"] for x in allaud if x.get("decay_ms") is not None])
    c_lo, c_hi = (np.percentile(cents, [33, 67]) if len(cents) else (0, 0))
    d_med = float(np.median(decs)) if len(decs) else 0
    out, stats = {}, []
    for fam, idx in sorted(fams.items()):
        aud = [i for i in idx if rows[i].get("audio") and rows[i]["audio"].get("bands")]
        entry = {"instruments": len(idx), "audio_clustered": len(aud), "tag_assigned": 0, "unassigned": 0, "silhouette": None, "clusters": []}
        out[fam] = entry
        if not aud:
            entry["unassigned"] = len(idx)
            continue
        F = np.array([feat(rows[i]["audio"]) for i in aud])
        col_mean = np.nanmean(F, 0)
        col_mean = np.where(np.isnan(col_mean), 0, col_mean)
        F = np.where(np.isnan(F), col_mean, F)
        mu, sd = F.mean(0), F.std(0) + 1e-9
        Z = (F - mu) / sd
        lab, C, sil = best_k(Z, a.kmax)
        entry["silhouette"] = sil
        k = len(C)
        # token profiles
        prof = [collections.Counter() for _ in range(k)]
        for j, i in enumerate(aud):
            prof[lab[j]].update(set(toks(rows[i])))
        df = collections.Counter(w for p in prof for w in p)
        cl = []
        for c in range(k):
            mem = [aud[j] for j in range(len(aud)) if lab[j] == c]
            raw = np.array([rows[i]["audio"] for i in mem], dtype=object)
            cen = float(np.mean([x.get("centroid") or 0 for x in raw]))
            dd = [x["decay_ms"] for x in raw if x.get("decay_ms") is not None]
            dec = float(np.mean(dd)) if dd else None
            att = float(np.mean([x.get("attack_ms") or 0 for x in raw]))
            tone = "dark" if cen < c_lo else "bright" if cen > c_hi else "mid"
            env = ("plucked" if dec is not None and dec < d_med else "sustained") if dec is not None else ("swelling" if att > 300 else "sustained")
            top = [w for w, _ in sorted(prof[c].items(), key=lambda t: (-t[1] * math.log(1 + k / df[t[0]]), t[0]))[:2]]
            cl.append({"id": f"{fam}-{c}", "name": " ".join([tone, env] + top), "size": len(mem), "centroid_hz": round(cen, 1),
                       "decay_ms": None if dec is None else round(dec, 1), "attack_ms": round(att, 1), "top_tags": top,
                       "centroid": [round(float(v), 4) for v in C[c]], "examples": [rows[i]["id"] for i in mem[:5]]})
            for i in mem:
                rows[i]["cluster"], rows[i]["assigned"] = cl[-1]["id"], "audio"
        # tag vote for the rest
        vocab = {}
        P = np.zeros((k, 0))
        words = sorted(df)
        W = {w: n for n, w in enumerate(words)}
        P = np.zeros((k, len(words)))
        for c in range(k):
            for w, n in prof[c].items():
                P[c, W[w]] = n / max(1, cl[c]["size"]) * math.log(1 + k / df[w])
        P /= np.linalg.norm(P, axis=1, keepdims=True) + 1e-9
        cen_rank = np.argsort(np.argsort([c["centroid_hz"] for c in cl])) / max(1, k - 1)  # 0 dark .. 1 bright
        sizes = np.array([c["size"] for c in cl], float)
        for i in idx:
            if rows[i].get("assigned") == "audio":
                continue
            ts = set(toks(rows[i]))
            v = np.zeros(len(words))
            for w in ts:
                if w in W:
                    v[W[w]] = 1
            sc = P @ v / (math.sqrt(max(1, len(ts))))
            tw = {x.lower() for x in (rows[i].get("timbre") or [])}
            b = (len(tw & BRIGHT) - len(tw & DARK))
            if b:
                sc = sc + 0.15 * np.sign(b) * (cen_rank - 0.5) * 2
            if v.sum() == 0 and not b:
                rows[i]["cluster"], rows[i]["assigned"] = None, "none"
                entry["unassigned"] += 1
                continue
            sc = sc + 1e-3 * sizes / sizes.sum()  # tie-break to the bigger cluster
            c = int(sc.argmax())
            rows[i]["cluster"], rows[i]["assigned"] = cl[c]["id"], "tags"
            entry["tag_assigned"] += 1
        for c in cl:
            c["members_tags"] = sum(1 for i in idx if rows[i].get("cluster") == c["id"] and rows[i]["assigned"] == "tags")
        entry["clusters"] = cl
    for r in rows:
        r.setdefault("cluster", None)
        r.setdefault("assigned", "none")
    for fam in fams:
        if out[fam]["audio_clustered"] == 0:
            pass
    json.dump({"total": len(rows), "kmax": a.kmax, "families": out}, open(os.path.join(atlas, "clusters.json"), "w"), indent=1)
    with open(os.path.join(atlas, "instruments_clustered.jsonl"), "w") as f:
        for r in rows:
            f.write(json.dumps(r, separators=(",", ":")) + "\n")
    print(f"{'family':9} {'inst':>5} {'audio':>5} {'tags':>5} {'none':>5} {'K':>2} {'sil':>5}  clusters (size)")
    ta = tt = tn = 0
    for fam, e in sorted(out.items(), key=lambda t: -t[1]["instruments"]):
        print(f"{fam:9} {e['instruments']:5} {e['audio_clustered']:5} {e['tag_assigned']:5} {e['unassigned']:5} {len(e['clusters']):2} {str(e['silhouette']):>5}  "
              + "; ".join(f"{c['name']} ({c['size']}+{c['members_tags']})" for c in e["clusters"]))
        ta += e["audio_clustered"]; tt += e["tag_assigned"]; tn += e["unassigned"]
    print(f"TOTAL {len(rows)}: audio-clustered {ta}, tag-assigned {tt}, unassigned {tn}")


if __name__ == "__main__":
    main()
