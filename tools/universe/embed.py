#!/usr/bin/env python3
"""G9: audio embeddings (ml-venv python). CLAP (laion/clap-htsat-unfused, 512-d, audio+text space) and EnCodec 24 kHz (encoder latent
mean + std over time, 2 x 128-d) for every sample, map row and own render.
    embed.py samples [--limit N]                  features.jsonl files, first 10 s            -> embeddings/samples.{npy,json}
    embed.py own --node Analog [--limit N]        re-render each recipe, first window (C3)    -> embeddings/own_<engine>.{npy,json}
    embed.py text "warm pad" "dark bass" ...      CLAP text vectors                           -> printed shape
    embed.py explain --node Analog                C11: R2 of the embedding (PCA 32) from the 22-axis summaries, ridge, group split
Outputs in ~/Infinite/training-data/embeddings/; .json holds the ids in row order; local only."""
import argparse, json, os, sys, tempfile, time
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
OUT = os.path.expanduser("~/Infinite/training-data/embeddings"); CLAP = "laion/clap-htsat-unfused"; ENC = "facebook/encodec_24khz"


def models():
    import torch
    from transformers import ClapModel, ClapProcessor, EncodecModel
    cm, cp = ClapModel.from_pretrained(CLAP).eval(), ClapProcessor.from_pretrained(CLAP)
    em = EncodecModel.from_pretrained(ENC).eval()
    return torch, cm, cp, em


def resample(x, a, b):
    if a == b: return x
    n = int(len(x) * b / a); return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)


def embed(M, mono48):
    torch, cm, cp, em = M; x = np.asarray(mono48, np.float32)
    with torch.no_grad():
        ci = cp(audios=[x], sampling_rate=48000, return_tensors="pt")
        c = cm.get_audio_features(**ci)[0].numpy()
        x24 = resample(x, 48000, 24000).astype(np.float32)
        z = em.encoder(torch.from_numpy(x24)[None, None])[0].numpy()          # 128 x frames
    return np.concatenate([c / (np.linalg.norm(c) + 1e-9), z.mean(1), z.std(1)]).astype(np.float32)


def save(name, ids, E):
    os.makedirs(OUT, exist_ok=True); np.save(os.path.join(OUT, name + ".npy"), E); json.dump(ids, open(os.path.join(OUT, name + ".json"), "w"))
    print("%s: %d x %d -> %s" % (name, *E.shape, OUT))


def cmd_samples(a):
    import audio_features as af
    M = models(); ids, E = [], []; t0 = time.time()
    for ln in open(os.path.expanduser("~/Infinite/training-data/audio/features.jsonl")):
        s = json.loads(ln)["src"]
        try:
            st = af.decode(s, 10)
            if st is None: continue
            E.append(embed(M, st.mean(0))); ids.append(s)
        except Exception as e:
            continue
        if a.limit and len(ids) >= a.limit: break
        if len(ids) % 500 == 0: print(len(ids), "%.0f s" % (time.time() - t0), flush=True)
    save("samples", ids, np.array(E))


def cmd_own(a):
    import own_sweeps as o, claims as cl
    table, sw, fixed = o.param_table(a.node); tags = o.tag_of(table); rows = cl.own_rows(o.out_path(a.node))
    ids = sorted(rows)[: a.limit or None]; M = models(); out_ids, E = [], []
    for i in range(0, len(ids), 20):
        part = ids[i:i + 20]
        with tempfile.TemporaryDirectory(prefix="emb_") as tmp:
            wavs = o.render_batch(o.binary(), a.node, tags, [rows[p]["recipe"] for p in part], tmp)
            for p, w in zip(part, wavs):
                if not w: continue
                import audio_features as af
                r = af.read_pcm(w, 60); x = r[0].mean(0); rate = r[1]; s0 = int(o.LEAD * rate)
                E.append(embed(M, resample(x[s0:s0 + int(o.WIN * rate)], rate, 48000))); out_ids.append(p)
        print(len(out_ids), flush=True)
    save("own_" + a.node.lower(), out_ids, np.array(E))


def cmd_text(a):
    torch, cm, cp, _ = models()
    with torch.no_grad():
        t = cm.get_text_features(**cp(text=a.words, return_tensors="pt", padding=True)).numpy()
    print(t.shape)


def cmd_explain(a):
    import sound_space as ss, claims as cl, own_sweeps as o, sound_data as sd
    E = np.load(os.path.join(OUT, "own_%s.npy" % a.node.lower())); ids = json.load(open(os.path.join(OUT, "own_%s.json" % a.node.lower())))
    X, I, _ = ss.own_matrix(o.out_path(a.node)); pos = {p: k for k, p in enumerate(I)}
    keep = [k for k, p in enumerate(ids) if p in pos]; Xa = np.nan_to_num(X[[pos[ids[k]] for k in keep]]); Ea = E[keep]
    d = sd.load(o.out_path(a.node)); sp = sd.split(d); held = set(d["preset_id"][sp["group"]].tolist())
    te = np.array([ids[k] in held for k in keep]); tr = ~te
    mu, sdv = Xa[tr].mean(0), Xa[tr].std(0) + 1e-9; Z = (Xa - mu) / sdv
    em = Ea[tr].mean(0); U, S, Vt = np.linalg.svd(Ea[tr] - em, full_matrices=False); P = (Ea - em) @ Vt[:32].T
    A = np.hstack([Z, np.ones((len(Z), 1))]); w = np.linalg.solve(A[tr].T @ A[tr] + 1.0 * np.eye(A.shape[1]), A[tr].T @ P[tr])
    pr = A[te] @ w; r2 = 1 - ((P[te] - pr) ** 2).sum(0) / (((P[te] - P[te].mean(0)) ** 2).sum(0) + 1e-12)
    wt = (S[:32] ** 2) / (S[:32] ** 2).sum()
    print("C11 %s: %d rows (test %d) | R2 of embedding PCA-32 from the 11 axes: variance-weighted %.3f, median component %.3f" %
          (a.node, len(keep), te.sum(), float((r2 * wt).sum()), float(np.median(r2))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["samples", "own", "text", "explain"]); ap.add_argument("words", nargs="*"); ap.add_argument("--node", default="Analog")
    ap.add_argument("--limit", type=int, default=0); a = ap.parse_args()
    {"samples": cmd_samples, "own": cmd_own, "text": cmd_text, "explain": cmd_explain}[a.cmd](a)


if __name__ == "__main__":
    main()
