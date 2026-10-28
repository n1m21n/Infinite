#!/usr/bin/env python3
"""M0 of SOUND-MODEL.md: one frozen evaluation for every knobs -> sound model.

    sound_eval.py split --engine "u-he diva" --root /Library/Audio/Presets/u-he/Diva --renders preset_diva.jsonl --prefix state:diva: --name diva
    sound_eval.py score  (same flags) [--models ridge,knn,mlp]

Two held-out sets, both stable as more presets are rendered (membership depends on the preset's name, not on the order rows arrive):
  random  a preset is held out when crc32(id) % 5 == 0 (about 20%)
  bank    whole preset folders are held out, so near-duplicates inside a bank cannot leak. Folders are taken in crc32 order until 20% of
          the presets are held out; folders over 15% of the library stay in training (holding out all FX would test a different task).
The bank list is frozen in eval/splits/<name>.json by `split`; `score` refuses to run without it. Scores: mean held-out R2 over the 43
descriptor outputs, split into bands and scalars (log-mel L1 joins when rows carry `mel`)."""
import json, os, sys, zlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import marginal_gate as mg  # noqa: E402
import sound_model_probe as smp  # noqa: E402

SPLITS = os.path.expanduser("~/Infinite/training-data/eval/splits")


def bank(i):
    return i.split("/")[0] if "/" in i else "(root)"


def crc(s):
    return zlib.crc32(s.encode())


def make_split(ids):
    n = len(ids); count = {}
    for i in ids:
        count[bank(i)] = count.get(bank(i), 0) + 1
    held, tot = [], 0
    for b in sorted(count, key=crc):
        if count[b] > 0.15 * n or tot >= 0.2 * n:
            continue
        held.append(b); tot += count[b]
    return {"rule_random": "crc32(id) % 5 == 0", "bank_test": sorted(held), "presets_at_freeze": n, "bank_test_presets_at_freeze": tot}


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd not in ("split", "score"):
        sys.exit(__doc__)
    argv = sys.argv[2:]; name = "diva"; models = "ridge,knn,mlp"
    for flag in ("--name", "--models"):
        if flag in argv:
            k = argv.index(flag); v = argv[k + 1]; del argv[k:k + 2]
            name, models = (v, models) if flag == "--name" else (name, v)
    a = mg.args(argv)
    X, Y, ids, _, _, rng = mg.load(a)
    path = os.path.join(SPLITS, name + ".json")
    if cmd == "split":
        if os.path.exists(path):
            sys.exit("%s exists: the split is frozen. Delete it only on purpose (all earlier scores become incomparable)." % path)
        os.makedirs(SPLITS, exist_ok=True)
        s = make_split(ids); json.dump(s, open(path, "w"), indent=1)
        print("froze %s: %d banks, %d of %d presets held out by bank" % (path, len(s["bank_test"]), s["bank_test_presets_at_freeze"], len(ids)))
        return
    s = json.load(open(path))
    hb = set(s["bank_test"]); idx = np.arange(len(ids))
    sets = {"random": np.array([crc(i) % 5 == 0 for i in ids]), "bank": np.array([bank(i) in hb for i in ids])}
    print("%d presets, %d knob columns | %s" % (len(ids), X.shape[1], path))
    print("%-8s %-10s %6s %6s %8s %8s" % ("split", "model", "train", "test", "R2", "bands/scal"))
    for sn, m in sets.items():
        tr, te = idx[~m], idx[m]
        for mn in models.split(","):
            if mn == "ridge":
                P = max((smp.ridge(X[tr], Y[tr], X[te], l) for l in (30, 300, 3000)), key=lambda P: smp.r2(Y[te], P).mean())
            elif mn == "knn":
                P = smp.knn(X[tr], Y[tr], X[te], 5)
            elif mn == "mlp":
                P = smp.mlp(X[tr], Y[tr], X[te], np.random.default_rng(0))
            else:
                sys.exit("unknown model " + mn)
            r = smp.r2(Y[te], P)
            print("%-8s %-10s %6d %6d %8.3f %5.3f/%.3f" % (sn, mn, len(tr), len(te), r.mean(), r[:32].mean(), r[32:].mean()))


if __name__ == "__main__":
    main()
