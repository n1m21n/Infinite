#!/usr/bin/env python3
"""E1 eval harness: one runner for every arm, the same held-out split and the same prompt (plan section 6).

Arms
  b0        no model: majority (param name, direction) in train, restricted to the node's real params
  b0n       no model: nearest seen change signature -> its param, mapped to this node's params by name
  b0c       tiny classifier (kNN on the raw measured facts; same-type neighbours when the type is seen)
  b1        stock LM, bare: patch + change, free greedy answer, parsed (no grammar, no node facts)
  b2        stock LM + grammar (answers ranked over the node's real params only) + node facts in the prompt
  lora      b2 with a LoRA adapter (--adapter)

Scores, per split (test_seen, test_params, test_types), each with a 95 % interval:
  top1, top3      exact node.param + direction
  equiv1          the named move really produces the measured change (same bucketed change on the same source
                  picture, looked up in the rendered moves: every move of every param is rendered)

Writes OUT/eval/<arm>.json (scores + per-item predictions) and prints a table.
`--export-train` writes OUT/lora-data/{train,valid}.jsonl in the b2 prompt format for mlx_lm.lora.

Usage:
  tools/infinite-model/e1_eval.py --arm b0 b0n b0c
  tools/infinite-model/e1_eval.py --arm b2 --model mlx-community/<model>
  tools/infinite-model/e1_eval.py --export-train
  tools/infinite-model/e1_eval.py --arm lora --model <model> --adapter build/e1/adapters/0.8b
  tools/infinite-model/e1_eval.py --compare b2 lora      (paired 95 % interval of the difference)
"""
import argparse, collections, difflib, hashlib, json, math, os, random, sys, time
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e1_data  # noqa: E402

SPLITS = ("test_seen", "test_params", "test_types")
SCALARS = [k for k in e1_data.FACTS if k != "tones"]


# ---- data ------------------------------------------------------------------------------------------
class Data:
    def __init__(self, out, cap):
        self.out = out
        self.rows = {}
        for l in open(os.path.join(out, "rows.jsonl")):
            r = json.loads(l)
            self.rows[r["id"]] = r
        sp = json.load(open(os.path.join(out, "split.json")))
        got = hashlib.sha256(json.dumps(sp["ids"], sort_keys=True).encode()).hexdigest()
        if got != sp["sha256"]:
            sys.exit(f"split.json changed since it was hashed ({got[:12]} != {sp['sha256'][:12]})")
        self.split = sp
        self.ids = {k: [i for i in v if i in self.rows] for k, v in sp["ids"].items()}
        # a fixed eval subsample per split (same for every arm): first `cap` ids by hash
        h = lambda i: hashlib.sha1(i.encode()).hexdigest()
        self.eval_ids = {k: sorted(self.ids[k], key=h)[:cap] if cap else self.ids[k] for k in SPLITS}
        types = {t["type"]: e1_data.fill_filter_menus(t)
                 for t in json.load(open(os.path.join(out, "describe.json")))["types"]
                 if t["category"] == e1_data.FAMILY}
        self.params = {name: e1_data.swept_params(t, None) for name, t in types.items()}
        # every rendered move by (type, source, param, direction) -> set of change signatures
        self.moves = collections.defaultdict(set)
        for r in self.rows.values():
            self.moves[(r["type"], r["source"], r["param"], r["direction"])].add(sig(r["change"]))
        self.train = [self.rows[i] for i in self.ids["train"]]

    def candidates(self, ftype):
        return [(p["key"], d) for p in self.params[ftype] for d in ("up", "down")]


def sig(change):
    return tuple(sorted(change.items()))


def norm(key):
    k = key[1:] if key.startswith("u") and key[1:2].isupper() else key
    return "".join(c for c in k.lower() if c.isalnum())


def name_sim(a, b):
    return difflib.SequenceMatcher(None, norm(a), norm(b)).ratio()


# ---- prompts (one format for every LM arm) --------------------------------------------------------
def node_facts(data, ftype):
    lines = []
    for p in data.params[ftype]:
        rng = (f"menu {'/'.join(p['options'])}" if p.get("options")
               else f"{p['kind']} {p['min']:g}..{p['max']:g}")
        lines.append(f"- {p['key']} ({p.get('label') or p['key']}): {rng}, default {float(p.get('default') or 0):g}")
    return "\n".join(lines)


def prompt_parts(data, r, facts=True):
    """(prefix, suffix): the prefix (patch + node facts) is the same for every item on one source + node and is
    cached; the suffix carries the measured change and the question."""
    pre = f"Infinite patch:\n{r['patch']}\n"
    if facts:
        pre += f"Params of node 2 ({e1_data.FAMILY} {r['type']}):\n{node_facts(data, r['type'])}\n"
    suf = (f"Measured change in the output picture after one param of node 2 moved: {r['change_text']}\n"
           f"Which param moved, and which way? Answer as {r['type']}.<param> up|down\nAnswer:")
    return pre, suf


def answer_text(ftype, key, d):
    return f" {ftype}.{key} {d}"


# ---- arms without a language model ----------------------------------------------------------------
def arm_b0(data, r):
    freq = collections.Counter((norm(t["param"]), t["direction"]) for t in data.train)
    return sorted(data.candidates(r["type"]), key=lambda c: -freq.get((norm(c[0]), c[1]), 0))


def tokens(change):
    return {f"{k}:{v}" for k, v in change.items()} | {f"{k}:{v.split()[0]}" for k, v in change.items()}


def map_votes(data, r, votes):
    """votes: Counter of (param key, direction) from other rows -> ranked candidates of this node."""
    score = collections.Counter()
    for (key, d), w in votes.items():
        for c in data.candidates(r["type"]):
            if c[1] == d:
                s = 1.0 if c[0] == key else name_sim(c[0], key) ** 4
                score[c] += w * s
    return sorted(data.candidates(r["type"]), key=lambda c: -score.get(c, 0))


def arm_b0n(data, r):
    tq = tokens(r["change"])
    sims = sorted(((len(tq & tokens(t["change"])) / max(1, len(tq | tokens(t["change"]))), t) for t in data.train),
                  key=lambda x: -x[0])[:15]
    return map_votes(data, r, collections.Counter((t["param"], t["direction"]) for _, t in sims))


def vec(r):
    b, a = r["before"], r["after"]
    v = [(a[k] - b[k]) / e1_data.FACTS[k][0] for k in SCALARS]
    v += list(np.asarray(a["hist"]) - np.asarray(b["hist"]))
    return np.asarray(v, np.float32)


class KNN:
    def __init__(self, data):
        self.rows = data.train
        X = np.stack([vec(t) for t in self.rows])
        self.mu, self.sd = X.mean(0), X.std(0) + 1e-6
        self.X = (X - self.mu) / self.sd
        self.types = np.asarray([t["type"] for t in self.rows])

    def __call__(self, data, r, k=9):
        q = (vec(r) - self.mu) / self.sd
        mask = self.types == r["type"]
        idx = np.where(mask)[0] if mask.any() else np.arange(len(self.rows))
        d = ((self.X[idx] - q) ** 2).sum(1)
        near = idx[np.argsort(d)[:k]]
        votes = collections.Counter()
        for j, i in enumerate(near):
            votes[(self.rows[i]["param"], self.rows[i]["direction"])] += 1.0 / (1 + j)
        return map_votes(data, r, votes)


# ---- language-model arms ------------------------------------------------------------------------
class LM:
    def __init__(self, model, adapter=None):
        import mlx.core as mx
        from mlx_lm import load
        from mlx_lm.models.cache import make_prompt_cache
        self.mx, self.make_cache = mx, make_prompt_cache
        self.model, self.tok = load(model, adapter_path=adapter)
        self.prefix_cache = {}

    def _clone(self, cache, n):
        """The prompt cache repeated n times along the batch axis (works for KV and linear-attention caches)."""
        mx = self.mx
        out = []
        for c in cache:
            new = c.__class__.__new__(c.__class__)
            for k, v in c.__dict__.items():
                if isinstance(v, mx.array):
                    v = mx.repeat(v, n, axis=0)
                elif isinstance(v, list):
                    v = [mx.repeat(x, n, axis=0) if isinstance(x, mx.array) else x for x in v]
                new.__dict__[k] = v
            out.append(new)
        return out

    def _prefilled(self, text):
        """Cache after `text`, and the logits of its last token. Prefixes are cached (the node-facts KV prefix)."""
        mx = self.mx
        ids = self.tok.encode(text)
        cache = self.make_cache(self.model)
        logits = self.model(mx.array([ids]), cache=cache)
        mx.eval(logits)
        return cache, logits[:, -1, :]

    def rank(self, pre, suf, cands, ftype):
        """Candidates sorted by the sum log-probability of their answer tokens (exact constrained decoding over a
        finite answer set). Returns (ranked, ms for this query with the prefix already cached)."""
        mx = self.mx
        if pre not in self.prefix_cache:
            self.prefix_cache = {pre: self._prefilled(pre)[0]}  # one prefix at a time: items are grouped by it
        t0 = time.time()
        cache = self._clone(self.prefix_cache[pre], 1)
        sid = self.tok.encode(suf, add_special_tokens=False) if hasattr(self.tok, "encode") else self.tok.encode(suf)
        logits = self.model(mx.array([sid]), cache=cache)
        last = logits[:, -1, :]
        texts = [answer_text(ftype, *c) for c in cands]
        toks = [self.tok.encode(t, add_special_tokens=False) for t in texts]
        L = max(len(t) for t in toks)
        pad = [t + [0] * (L - len(t)) for t in toks]
        bc = self._clone(cache, len(toks))
        out = self.model(mx.array(pad), cache=bc)
        lp = out - mx.logsumexp(out, axis=-1, keepdims=True)
        first = last - mx.logsumexp(last, axis=-1, keepdims=True)
        mx.eval(lp, first)
        lp, first = np.array(lp.astype(mx.float32)), np.array(first.astype(mx.float32))[0]
        scores = []
        for i, t in enumerate(toks):
            s = first[t[0]] + sum(lp[i, j - 1, t[j]] for j in range(1, len(t)))
            scores.append(s / len(t) ** 0.0)
        ms = (time.time() - t0) * 1000
        order = np.argsort(-np.asarray(scores))
        return [cands[i] for i in order], ms

    def generate(self, prompt, max_tokens=16):
        from mlx_lm import generate
        return generate(self.model, self.tok, prompt=prompt, max_tokens=max_tokens, verbose=False)


def parse_free(text, data, r):
    """b1: read `<type>.<param> up|down` from free text; anything else is a miss."""
    w = text.strip().split()
    if len(w) < 2 or "." not in w[0]:
        return []
    key = w[0].split(".", 1)[1]
    d = w[1].strip(".,").lower()
    return [(key, d)] if d in ("up", "down") else []


# ---- scoring ------------------------------------------------------------------------------------
def wilson(k, n):
    if n == 0:
        return (0.0, 0.0)
    z, p = 1.96, k / n
    den = 1 + z * z / n
    c = (p + z * z / (2 * n)) / den
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return (round(c - h, 4), round(c + h, 4))


def score(data, r, ranked):
    truth = (r["param"], r["direction"])
    top1 = ranked[:1] == [truth]
    eq = bool(ranked) and (top1 or sig(r["change"]) in data.moves.get((r["type"], r["source"], *ranked[0]), ()))
    return {"top1": top1, "top3": truth in ranked[:3], "equiv1": eq}


def run_arm(data, arm, a):
    lm = knn = None
    if arm in ("b1", "b2", "lora"):
        lm = LM(a.model, a.adapter if arm == "lora" else None)
    if arm == "b0c":
        knn = KNN(data)
    res = {"arm": arm, "model": a.model if lm else None, "adapter": a.adapter if arm == "lora" else None,
           "split_sha256": data.split["sha256"], "splits": {}, "items": []}
    for split in SPLITS:
        ids = sorted(data.eval_ids[split], key=lambda i: (data.rows[i]["type"], data.rows[i]["source"]))
        hits = collections.Counter()
        ms = []
        for n, i in enumerate(ids):
            r = data.rows[i]
            if arm == "b0":
                ranked = arm_b0(data, r)
            elif arm == "b0n":
                ranked = arm_b0n(data, r)
            elif arm == "b0c":
                ranked = knn(data, r)
            elif arm == "b1":
                pre, suf = prompt_parts(data, r, facts=False)
                t0 = time.time()
                ranked = parse_free(lm.generate(pre + suf), data, r)
                ms.append((time.time() - t0) * 1000)
            else:
                pre, suf = prompt_parts(data, r)
                ranked, t = lm.rank(pre, suf, data.candidates(r["type"]), r["type"])
                ms.append(t)
            s = score(data, r, ranked)
            hits.update(k for k, v in s.items() if v)
            res["items"].append({"id": i, "split": split, "pred": [f"{p} {d}" for p, d in ranked[:3]], **s})
            if lm and n % 50 == 49:
                print(f"  {arm} {split} {n + 1}/{len(ids)}  top1 {hits['top1'] / (n + 1):.3f}", flush=True)
        N = len(ids)
        res["splits"][split] = {"n": N, **{k: {"rate": round(hits[k] / max(N, 1), 4), "ci95": wilson(hits[k], N)}
                                         for k in ("top1", "top3", "equiv1")}}
        if ms:
            res["splits"][split]["ms_per_answer"] = {"median": round(float(np.median(ms)), 1),
                                                     "p90": round(float(np.percentile(ms, 90)), 1)}
    os.makedirs(os.path.join(data.out, "eval"), exist_ok=True)
    name = arm if not a.tag else f"{arm}-{a.tag}"
    with open(os.path.join(data.out, "eval", name + ".json"), "w") as f:
        json.dump(res, f, indent=1)
    return name, res


def table(results):
    print(f"\n| arm | split | n | top-1 | top-3 | equivalent | ms |\n|---|---|---|---|---|---|---|")
    for name, res in results:
        for split, s in res["splits"].items():
            ms = s.get("ms_per_answer", {}).get("median", "")
            print(f"| {name} | {split} | {s['n']} | {s['top1']['rate']:.3f} {s['top1']['ci95']} | "
                  f"{s['top3']['rate']:.3f} | {s['equiv1']['rate']:.3f} | {ms} |")


def compare(out, a_name, b_name, key="top1", boots=2000):
    """Paired bootstrap 95 % interval of (b - a) per split. A gain needs the interval clear of 0."""
    A = json.load(open(os.path.join(out, "eval", a_name + ".json")))
    B = json.load(open(os.path.join(out, "eval", b_name + ".json")))
    ai = {i["id"]: i for i in A["items"]}
    rng = random.Random(1)
    print(f"\n{b_name} - {a_name}, {key}, paired bootstrap 95 %")
    for split in SPLITS:
        pairs = [(ai[i["id"]][key], i[key]) for i in B["items"] if i["split"] == split and i["id"] in ai]
        if not pairs:
            continue
        diffs = [int(b) - int(a) for a, b in pairs]
        bs = sorted(sum(rng.choice(diffs) for _ in diffs) / len(diffs) for _ in range(boots))
        lo, hi = bs[int(0.025 * boots)], bs[int(0.975 * boots)]
        print(f"  {split}: {sum(diffs) / len(diffs):+.3f}  [{lo:+.3f}, {hi:+.3f}]  "
              f"{'GAIN' if lo > 0 else 'no gain'}")


def export_train(data):
    d = os.path.join(data.out, "lora-data")
    os.makedirs(d, exist_ok=True)
    for split in ("train", "valid"):
        with open(os.path.join(d, split + ".jsonl"), "w") as f:
            for i in data.ids[split]:
                r = data.rows[i]
                pre, suf = prompt_parts(data, r)
                f.write(json.dumps({"prompt": pre + suf, "completion": answer_text(r["type"], r["param"],
                                                                                   r["direction"])}) + "\n")
    # mlx_lm.lora wants a test file too; the real held-out scoring is this harness
    with open(os.path.join(d, "test.jsonl"), "w") as f:
        f.write(open(os.path.join(d, "valid.jsonl")).read())
    print(f"[export] {len(data.ids['train'])} train, {len(data.ids['valid'])} valid -> {d}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "build/e1"))
    ap.add_argument("--arm", nargs="*", default=[])
    ap.add_argument("--model")
    ap.add_argument("--adapter")
    ap.add_argument("--tag", help="suffix for the result file (b2-2b)")
    ap.add_argument("--cap", type=int, default=400, help="items per test split (fixed by hash; 0 = all)")
    ap.add_argument("--export-train", action="store_true")
    ap.add_argument("--compare", nargs=2)
    a = ap.parse_args()
    data = Data(a.out, a.cap)
    if a.export_train:
        export_train(data)
    results = [run_arm(data, arm, a) for arm in a.arm]
    if results:
        table(results)
    if a.compare:
        compare(a.out, *a.compare)


if __name__ == "__main__":
    main()
