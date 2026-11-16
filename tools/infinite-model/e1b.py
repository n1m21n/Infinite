#!/usr/bin/env python3
"""E1b: name the knob from what the knobs do, not from their names (plan section 12, step 8).

Same split, same scores as E1. What changes is the prompt:
  - every param of node 2 carries its measured effect: for each direction, the facts its rendered moves change
    most often, counted on the OTHER sources only (leave-one-source-out, so an item never sees its own render);
  - param names are replaced by p1..pN in a random order per item (seeded by the item id), so a name cannot
    carry the answer. The answer is mapped back to the real key before scoring.

Arms
  b0p     no model: the candidate whose effect profile best matches the measured change
  b2e     stock LM, ranked over the node's p-labels (same harness as B2)
  lorae   b2e with a LoRA trained on the same prompts (train rows written twice, two label orders)

  python tools/infinite-model/e1b.py export                      # build/e1/lora-data-e1b/{train,valid}.jsonl
  python tools/infinite-model/e1b.py b0p
  python tools/infinite-model/e1b.py lm --model Qwen/Qwen3.5-0.8B [--adapter build/e1/adapters/e1b-0.8b]   (CUDA)
"""
import argparse, collections, json, os, random, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import e1_eval  # noqa: E402
from e1_eval import SPLITS, Data, score, wilson, table, compare  # noqa: E402
import e1_data  # noqa: E402

LEVEL = {"small": 1, "medium": 2, "large": 3}


class Profiles:
    """(type, param, direction) -> per source, Counter of fact tokens ('detail down', 'tones')."""

    def __init__(self, data):
        self.by = collections.defaultdict(lambda: collections.defaultdict(list))
        for r in data.rows.values():
            self.by[(r["type"], r["param"], r["direction"])][r["source"]].append(r["change"])

    def counts(self, ftype, key, d, skip_source):
        c, lv, n = collections.Counter(), collections.defaultdict(list), 0
        for src, changes in self.by.get((ftype, key, d), {}).items():
            if src == skip_source:
                continue
            for ch in changes:
                n += 1
                for f, v in ch.items():
                    w = v.split()
                    tok = f if f == "tones" else f"{f} {w[0]}"
                    c[tok] += 1
                    lv[tok].append(LEVEL[w[-1]])
        return c, lv, n

    def text(self, ftype, key, d, skip_source):
        c, lv, n = self.counts(ftype, key, d, skip_source)
        if not n:
            return "no visible change"
        out = []
        for tok, k in c.most_common(3):
            if k / n < 0.25:
                break
            lvl = sorted(lv[tok])[len(lv[tok]) // 2]
            out.append(f"{tok} {['', 'small', 'medium', 'large'][lvl]}")
        return ", ".join(out) or "mixed"


def labels(data, r, seed_extra=""):
    keys = [p["key"] for p in data.params[r["type"]]]
    rnd = random.Random(r["id"] + seed_extra)
    order = keys[:]
    rnd.shuffle(order)
    return {k: f"p{i + 1}" for i, k in enumerate(order)}, order


def prompt(data, prof, r, lab, order):
    ps = {p["key"]: p for p in data.params[r["type"]]}
    lines = []
    for k in order:
        p = ps[k]
        rng = f"menu of {len(p['options'])}" if p.get("options") else f"{p['kind']} {p['min']:g}..{p['max']:g}"
        up = prof.text(r["type"], k, "up", r["source"])
        dn = prof.text(r["type"], k, "down", r["source"])
        lines.append(f"- {lab[k]}: {rng}, default {float(p.get('default') or 0):g}; up: {up}; down: {dn}")
    pre = (f"Infinite patch: {r['source']} source -> one {e1_data.FAMILY} node -> output.\n"
           f"Params of the effect node, with what moving each one does to the picture on other sources:\n"
           + "\n".join(lines) + "\n")
    suf = (f"Measured change in the output picture after one param moved: {r['change_text']}\n"
           f"Which param moved, and which way? Answer as p<n> up|down\nAnswer:")
    return pre, suf


def answer(lab_key, d):
    return f" {lab_key} {d}"


def export(data, prof):
    d = os.path.join(data.out, "lora-data-e1b")
    os.makedirs(d, exist_ok=True)
    for split, copies in (("train", ("", "b")), ("valid", ("",))):
        with open(os.path.join(d, split + ".jsonl"), "w") as f:
            n = 0
            for i in data.ids[split]:
                r = data.rows[i]
                for extra in copies:
                    lab, order = labels(data, r, extra)
                    pre, suf = prompt(data, prof, r, lab, order)
                    f.write(json.dumps({"prompt": pre + suf, "completion": answer(lab[r["param"]], r["direction"])}) + "\n")
                    n += 1
        print(f"[export] {split}: {n} rows -> {d}")
    i = data.ids["test_params"][0]
    r = data.rows[i]
    lab, order = labels(data, r)
    print("[example]\n" + "".join(prompt(data, prof, r, lab, order)) + answer(lab[r["param"]], r["direction"]))


def b0p_rank(data, prof, r):
    """Profile match: for each candidate, how often its moves (other sources) changed the facts that moved here."""
    want = {}
    for f, v in r["change"].items():
        w = v.split()
        want[f if f == "tones" else f"{f} {w[0]}"] = LEVEL[w[-1]]
    scores = {}
    for c in data.candidates(r["type"]):
        cnt, lv, n = prof.counts(r["type"], c[0], c[1], r["source"])
        if not n:
            scores[c] = -1.0
            continue
        s = 0.0
        for tok, l in want.items():
            if cnt[tok]:
                med = sorted(lv[tok])[len(lv[tok]) // 2]
                s += cnt[tok] / n * (1.0 if med == l else 0.6)
        extra = sum(k for t, k in cnt.items() if t not in want) / n
        scores[c] = s - 0.3 * extra
    return sorted(data.candidates(r["type"]), key=lambda c: -scores[c])


def run(data, prof, name, rank_fn, meta):
    res = {"arm": name, **meta, "split_sha256": data.split["sha256"], "splits": {}, "items": []}
    for split in SPLITS:
        ids = data.eval_ids[split]
        hits, ms = collections.Counter(), []
        for n, i in enumerate(ids):
            r = data.rows[i]
            t0 = time.time()
            ranked = rank_fn(r)
            ms.append((time.time() - t0) * 1000)
            s = score(data, r, ranked)
            hits.update(k for k, v in s.items() if v)
            res["items"].append({"id": i, "split": split, "pred": [f"{p} {d}" for p, d in ranked[:3]], **s})
            if n % 50 == 49:
                print(f"  {name} {split} {n + 1}/{len(ids)}  top1 {hits['top1'] / (n + 1):.3f}", flush=True)
        N = len(ids)
        res["splits"][split] = {"n": N, **{k: {"rate": round(hits[k] / max(N, 1), 4), "ci95": wilson(hits[k], N)}
                                         for k in ("top1", "top3", "equiv1")},
                                "ms_per_answer": {"median": round(sorted(ms)[len(ms) // 2], 1)}}
    os.makedirs(os.path.join(data.out, "eval"), exist_ok=True)
    with open(os.path.join(data.out, "eval", name + ".json"), "w") as f:
        json.dump(res, f, indent=1)
    return name, res


def lm_rank(lm, data, prof):
    e1_eval.answer_text = lambda ftype, k, d: answer(k, d)  # TorchLM.rank builds answers through this

    def rank(r):
        lab, order = labels(data, r)
        back = {v: k for k, v in lab.items()}
        pre, suf = prompt(data, prof, r, lab, order)
        cands = [(lab[k], d) for k, d in data.candidates(r["type"])]
        ranked, _ = lm.rank(pre, suf, cands, None)
        return [(back[l], d) for l, d in ranked]
    return rank


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["export", "b0p", "lm"])
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(HERE)), "build/e1"))
    ap.add_argument("--cap", type=int, default=150)
    ap.add_argument("--model", default="Qwen/Qwen3.5-0.8B")
    ap.add_argument("--adapter")
    a = ap.parse_args()
    data = Data(a.out, a.cap)
    prof = Profiles(data)
    if a.cmd == "export":
        export(data, prof)
        return
    results = []
    if a.cmd == "b0p":
        results.append(run(data, prof, "b0p", lambda r: b0p_rank(data, prof, r), {}))
    else:
        from e1_torch import TorchLM
        results.append(run(data, prof, "b2e", lm_rank(TorchLM(a.model), data, prof), {"model": a.model}))
        if a.adapter:
            results.append(run(data, prof, "lorae", lm_rank(TorchLM(a.model, a.adapter), data, prof),
                               {"model": a.model, "adapter": a.adapter}))
    table(results)
    if a.adapter:
        compare(a.out, "b2e", "lorae")
        if os.path.exists(os.path.join(a.out, "eval", "b2-torch.json")):
            compare(a.out, "b2-torch", "lorae")
        if os.path.exists(os.path.join(a.out, "eval", "b0p.json")):
            compare(a.out, "b0p", "lorae")


if __name__ == "__main__":
    main()
