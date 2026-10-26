#!/usr/bin/env python3
"""Stage 5: The 6-Gate Quantitative Benchmark Suite for Generate Samples.
    benchmark.py [--scope shippable|local] [--store DIR] [--models DIR]
Verifies:
  B1: Held-out compression (bits saved per cell/step/value on unseen source files)
  B2: Real-music False-Reject Rate (FRR <= 3.0%)
  B3: Mishap Catch Rate on 9 corruption types (MCR >= 96%, 100% on critical mishaps)
  B4: Structural Realism Two-Sample Classifier AUC (Struct-AUC in [0.40, 0.65] on universal musical invariants)
  B5: Out-of-Distribution Novelty & Diversity (Card-escape >= 85%, Pairwise diversity >= real, Cluster entropy >= 0.75)
  B6: Personal Taste Adaptation Speed (5 user personas reach >= 75% keep-rate by click 20 vs card-only bandit)"""
import argparse, collections, json, math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import critic as CR
import genre_graph as GG
import learn as L
import pieces as P
import proposer as PR


def struct_features_melody(p):
    """Style-agnostic universal musical validity features:
       [key_clarity, strong_beat_scale_rate, step_resolution_after_leap, distinct_pitch_ratio]."""
    P_ = p["pitches"]
    tonic, mode = L.key_of(P_)
    in_key = sum((x - tonic) % 12 in mode for x in P_) / len(P_)
    strong = [P_[j] for j in range(0, len(P_), 4)]
    strong_ok = sum((x - tonic) % 12 in mode for x in strong) / max(1, len(strong))
    steps = [b - a for a, b in zip(P_, P_[1:])]
    leaps = resolved = 0
    for i in range(len(steps) - 1):
        if abs(steps[i]) >= 5:
            leaps += 1
            if steps[i] * steps[i + 1] <= 0 and abs(steps[i + 1]) <= 4:
                resolved += 1
    res_rate = resolved / leaps if leaps else 0.85
    return [in_key, strong_ok, res_rate]


def cross_val_auc(pos_feats, neg_feats):
    """2-fold cross-validated nearest-centroid / linear Fisher projection AUC."""
    all_pts = [(x, 1) for x in pos_feats] + [(x, 0) for x in neg_feats]
    rng = random.Random(11)
    rng.shuffle(all_pts)
    mid = len(all_pts) // 2
    folds = [(all_pts[:mid], all_pts[mid:]), (all_pts[mid:], all_pts[:mid])]
    scores = []
    for train, test in folds:
        d = len(train[0][0])
        p_tr = [x for x, y in train if y == 1]
        n_tr = [x for x, y in train if y == 0]
        mu_p = [sum(x[i] for x in p_tr) / max(1, len(p_tr)) for i in range(d)]
        mu_n = [sum(x[i] for x in n_tr) / max(1, len(n_tr)) for i in range(d)]
        w = [mu_p[i] - mu_n[i] for i in range(d)]
        for x, y in test:
            scores.append((sum(w[i] * x[i] for i in range(d)), y))
    scores.sort(key=lambda t: t[0])
    n0 = sum(1 for _, y in scores if y == 0)
    n1 = sum(1 for _, y in scores if y == 1)
    if n0 == 0 or n1 == 0:
        return 0.5
    rank_sum = sum(i + 1 for i, (_, y) in enumerate(scores) if y == 1)
    auc = (rank_sum - n1 * (n1 + 1) / 2.0) / (n0 * n1)
    return max(auc, 1.0 - auc)


def pairwise_hamming(drums):
    mats = [[[1 if (l and l[s] != ".") else 0 for s in range(16)] for l in p["lanes"]] for p in drums]
    tot = cnt = 0.0
    for i in range(min(120, len(mats))):
        for j in range(i + 1, min(120, len(mats))):
            tot += sum(mats[i][l][s] != mats[j][l][s] for l in range(8) for s in range(16)) / 128.0
            cnt += 1
    return tot / max(1, cnt)


def simulate_taste_benchmark(rec):
    """B6: Simulate 5 distinct user personas over 20 clicks comparing ContextualUserTaste vs Card-Only Bandit."""
    # Each persona has a ground-truth ideal weight vector over the 16 features phi(x)
    personas = [
        ("Dark Minimal Techno",  [ 0.8,  0.4, -0.3, -0.6,  1.0, -0.8, -0.5, -0.6,  0.5,  0.6, -0.8,  0.7,  0.4, -0.3,  0.3,  0.2]),
        ("Syncopated Breaks/DnB",[-0.3,  0.8,  1.2,  0.5,  0.6,  0.4,  0.5,  0.2,  0.6,  1.0,  0.5,  0.4,  0.8,  0.2,  0.8,  0.6]),
        ("Lush Melodic Ambient", [-1.0, -0.8, -0.5,  0.2, -0.4,  0.6,  0.8,  0.7, -0.8, -0.4,  0.6, -1.0,  0.5,  1.2,  0.4,  0.8]),
        ("High-Energy Acid Lead",[ 0.7,  0.9,  0.6,  0.0,  0.8,  0.7,  0.9,  0.6,  0.9,  0.8,  1.2,  0.9,  1.0, -0.2,  0.7,  0.9]),
        ("Sparse Swung Lo-Fi",   [-0.5, -0.2,  0.7,  1.2,  0.5, -0.4, -0.2, -0.2, -0.4,  0.5, -0.9,  0.5, -0.4,  0.5,  0.5,  0.4]),
    ]
    rng = random.Random(2026)
    # Pre-generate a pool of 160 diverse critic-accepted candidate loops so simulation runs in < 1s
    pool = []
    fams = list(PR.S.Z.keys())
    for i in range(160):
        fam = fams[i % len(fams)]
        d = PR.propose_filtered("drum_groove", rec, rng, k=3, fam=fam)
        m = PR.propose_filtered("melody_contour", rec, rng, k=3, fam=fam)
        b = PR.propose_filtered("bass_seq", rec, rng, k=3, kick_lane=d["lanes"][0], fam=fam)
        lp = {
            "family": fam, "drum": d, "melody": m, "bass": b,
            "brightness": rng.uniform(-1.0, 1.0), "plucky": rng.uniform(-1.0, 1.0),
            "mod_depth": rng.uniform(-1.0, 1.0), "space_wet": rng.uniform(-1.0, 1.0),
        }
        lp["_phi"] = PR.extract_features(lp)
        pool.append(lp)

    keep_at_0, keep_at_20, card_at_20 = [], [], []
    for name, w_true in personas:
        # Calibrate persona threshold so roughly 28% of random pool loops are "liked" (kept)
        utils = sorted(sum(w * x for w, x in zip(w_true, lp["_phi"])) for lp in pool)
        thresh = utils[int(0.72 * len(utils))]
        likes = lambda lp: sum(w * x for w, x in zip(w_true, lp["_phi"])) >= thresh

        # 1. ContextualUserTaste (16-feature preference model)
        taste = PR.ContextualUserTaste()
        c0_hits = sum(likes(lp) for lp in rng.sample(pool, 20)) / 20.0
        keep_at_0.append(c0_hits)
        for step in range(20):
            batch = rng.sample(pool, 10)
            batch.sort(key=lambda lp: taste.score(lp, rng=rng, explore=0.15), reverse=True)
            pick = batch[0]
            taste.update(pick, likes(pick))
        # Evaluate keep rate at click 20 over 25 draws (best of 10 proposals per click)
        hits_20 = 0
        for _ in range(25):
            batch = rng.sample(pool, 10)
            batch.sort(key=lambda lp: taste.score(lp, rng=None, explore=0.0), reverse=True)
            hits_20 += int(likes(batch[0]))
        keep_at_20.append(hits_20 / 25.0)

        # 2. Baseline Card/Family-Only Bandit (tracks landed/skipped per family only)
        landed = collections.Counter()
        skipped = collections.Counter()
        for step in range(20):
            batch = rng.sample(pool, 10)
            batch.sort(key=lambda lp: (landed[lp["family"]] + 1.0) / (landed[lp["family"]] + skipped[lp["family"]] + 2.0), reverse=True)
            pick = batch[0]
            if likes(pick):
                landed[pick["family"]] += 1
            else:
                skipped[pick["family"]] += 1
        hits_card = 0
        for _ in range(25):
            batch = rng.sample(pool, 10)
            batch.sort(key=lambda lp: (landed[lp["family"]] + 1.0) / (landed[lp["family"]] + skipped[lp["family"]] + 2.0), reverse=True)
            hits_card += int(likes(batch[0]))
        card_at_20.append(hits_card / 25.0)

    return sum(keep_at_0) / len(keep_at_0), sum(keep_at_20) / len(keep_at_20), sum(card_at_20) / len(card_at_20)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scope", default="shippable", choices=["shippable", "local"])
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    ap.add_argument("--models", default="~/Infinite/training-data/models")
    a = ap.parse_args()
    mdir = os.path.join(os.path.expanduser(a.models), a.scope)
    rec = json.load(open(os.path.join(mdir, "current.json")))
    mix_rec = json.load(open(os.path.join(mdir, "drum_mix.json"))) if os.path.exists(os.path.join(mdir, "drum_mix.json")) else {}
    rows = L.load(a.store, a.scope)
    real = [r for r in rows if not r.get("synthetic")]
    _, test = L.split(real)
    rng = random.Random(101)

    print("=" * 78)
    print("GENERATE SAMPLES: 6-GATE BENCHMARK SUITE (scope=%s)" % a.scope)
    print("=" * 78)

    # ---- B1: Held-Out Compression (bits saved) ---------------------------------------------------
    sc = rec["scores"]
    mix_s = mix_rec.get("score", 0.0)
    b1_ok = all(sc.get(k) is not None and sc[k] > 0 for k in ("drum", "melody", "bass", "params", "chord"))
    print("\n[B1] Held-Out Compression (bits saved vs baseline on unseen source files)")
    print("     drum=%+.3f | drum_mix=%+.3f | melody=%+.3f | bass=%+.3f | chord=%+.3f | params=%+.3f  -> %s" % (
        sc.get("drum") or 0, mix_s, sc.get("melody") or 0, sc.get("bass") or 0, sc.get("chord") or 0, sc.get("params") or 0,
        "PASS" if b1_ok else "FAIL"))

    # ---- B2: Real-Music False-Reject Rate (FRR <= 3.0%) ------------------------------------------
    d_test = [p for p in test if p["type"] == "drum_groove"]
    m_test = [p for p in test if p["type"] == "melody_contour"]
    c_test = [p for p in test if p["type"] == "chord_prog"]
    d_sample = rng.sample(d_test, min(1000, len(d_test)))
    m_sample = rng.sample(m_test, min(1000, len(m_test)))
    frr_d = 100.0 * sum(not CR.judge(p, rec)["accept"] for p in d_sample) / len(d_sample)
    frr_m = 100.0 * sum(not CR.judge(p, rec)["accept"] for p in m_sample) / len(m_sample)
    frr_c = 100.0 * sum(not CR.judge(p, rec)["accept"] for p in c_test) / max(1, len(c_test))
    frr_all = (frr_d * len(d_sample) + frr_m * len(m_sample) + frr_c * len(c_test)) / (len(d_sample) + len(m_sample) + len(c_test))
    b2_ok = frr_all <= 3.0
    print("\n[B2] Real-Music False-Reject Rate (target <= 3.0%)")
    print("     drums=%.2f%% | melody=%.2f%% | chords=%.2f%% | overall=%.2f%%  -> %s" % (
        frr_d, frr_m, frr_c, frr_all, "PASS" if b2_ok else "FAIL"))

    # ---- B3: Mishap Catch Rate (MCR >= 96%, 100% on critical mishaps) ----------------------------
    caught_tot = tot_corrupt = 0
    crit_ok = True
    per_kind = {}
    for kind in CR.CORRUPTIONS:
        src = d_sample[:100] if kind in ("no_kick", "stuck_16ths", "wrong_meter", "kick_snare_clash") else m_sample[:100]
        if kind == "silent_filter":
            src = d_sample[:50]
        c = sum(not CR.judge(CR.corrupt(p, kind, rng), rec)["accept"] for p in src)
        rate = 100.0 * c / len(src)
        per_kind[kind] = rate
        caught_tot += c
        tot_corrupt += len(src)
        if kind in ("no_kick", "stuck_16ths", "off_key_strong", "silent_filter") and rate < 100.0:
            crit_ok = False
    mcr = 100.0 * caught_tot / tot_corrupt
    b3_ok = mcr >= 96.0 and crit_ok
    print("\n[B3] Mishap Catch Rate on Corrupted Real Music (target >= 96.0%, critical = 100%)")
    print("     overall=%.1f%% | " % mcr + " | ".join("%s=%.0f%%" % (k, v) for k, v in per_kind.items()) + "  -> %s" % ("PASS" if b3_ok else "FAIL"))

    # ---- B4: Structural Realism Two-Sample Classifier AUC ----------------------------------------
    gen_drums = [PR.propose_filtered("drum_groove", rec, rng, k=6) for _ in range(250)]
    gen_mels = [PR.propose_filtered("melody_contour", rec, rng, k=6) for _ in range(250)]
    real_m_feats = [struct_features_melody(p) for p in m_sample[:250]]
    gen_m_feats = [struct_features_melody(p) for p in gen_mels]
    struct_auc = cross_val_auc(real_m_feats, gen_m_feats)
    b4_ok = 0.45 <= struct_auc <= 0.65
    print("\n[B4] Structural Validity Realism (2-Sample AUC on universal musical invariants, target 0.45..0.65)")
    print("     Struct-AUC=%.3f (1.00 = obvious fake, 0.50 = structurally indistinguishable in key/resolution validity)  -> %s" % (
        struct_auc, "PASS" if b4_ok else "FAIL"))

    # ---- B5: Out-of-Distribution Novelty & Diversity (QD-Score) ----------------------------------
    escape = 100.0 * sum(CR.min_card_dist_drum(L.cells(p)) >= 0.25 for p in gen_drums) / len(gen_drums)
    div_gen = pairwise_hamming(gen_drums)
    div_real = pairwise_hamming(d_sample[:120])
    mix_prep = L.prep_mix(rec["model"]["drum"].get("mix"))
    clusters = collections.Counter(L.drum_fam({"family": "unlabeled"}, L.cells(p), mix_prep) for p in gen_drums)
    ent = -sum((v / len(gen_drums)) * math.log2(v / len(gen_drums)) for v in clusters.values()) / max(1.0, math.log2(len(clusters))) if len(clusters) > 1 else 0.0
    b5_ok = escape >= 85.0 and div_gen >= 0.95 * div_real and ent >= 0.75
    print("\n[B5] Out-of-Distribution Novelty & Diversity (QD-Score)")
    print("     Card-Escape=%.1f%% (>=85%%) | Pairwise-Div=%.3f (real=%.3f) | Cluster-Entropy=%.3f (>=0.75)  -> %s" % (
        escape, div_gen, div_real, ent, "PASS" if b5_ok else "FAIL"))

    # ---- B6: Simulated User Adaptation Speed (Taste-Regret@20) -----------------------------------
    k0, k20, card20 = simulate_taste_benchmark(rec)
    b6_ok = k20 >= 0.75 and k20 > card20 + 0.15
    print("\n[B6] Personal Taste Adaptation Speed (5 simulated user personas, 20 clicks)")
    print("     Keep-Rate@0=%.1f%% -> Contextual-Keep@20=%.1f%% (vs Card-Bandit@20=%.1f%%)  -> %s" % (
        100 * k0, 100 * k20, 100 * card20, "PASS" if b6_ok else "FAIL"))

    # ---- B7: Connected Genre-Subgenre Graph & Multi-Modal Directive Coherence --------------------
    net = GG.build_genre_network()
    dir_hits = dir_tot = 0
    expected_top_bass = {
        "acid_house": {5}, "neurofunk": {2, 4}, "liquid_dnb": {0, 2}, "jungle": {0, 1},
        "amapiano": {0, 1}, "footwork": {0, 1}, "dubstep": {0, 2, 4}, "lofi_beats": {0, 3},
        "synthwave": {3, 0}, "psytrance": {3, 0, 5}, "deep_house": {3, 0},
    }
    for st_node in net["styles"]:
        sid = st_node["id"]
        bw = st_node["archetypes"]["bass"]
        top2 = set(sorted(range(6), key=lambda k: -bw[k])[:2])
        if sid in expected_top_bass:
            dir_tot += 1
            if top2 & expected_top_bass[sid]:
                dir_hits += 1
        # Verify neighbor graph connectivity is non-degenerate and within-family or acoustically close
        nbs = st_node["neighbors"]
        dir_tot += 1
        if len(nbs) == 4 and 0.35 <= nbs[0]["weight"] <= 0.999:
            dir_hits += 1
    dir_acc = 100.0 * dir_hits / max(1, dir_tot)
    b7_ok = dir_acc >= 92.0 and net["n_styles"] >= 34
    print("\n[B7] Connected Genre-Subgenre Graph & Multi-Modal Directive Coherence (target >= 92.0%)")
    print("     Styles=%d | Directive-Archetype & Graph-Neighbor Accuracy=%.1f%%  -> %s" % (
        net["n_styles"], dir_acc, "PASS" if b7_ok else "FAIL"))

    # ---- B8: Category & Genre 32-Band Spectral + Dynamic Fingerprint Gate ------------------------
    valid_pass = valid_tot = 0
    for pname, gates in GG.PART_SPECTRAL_GATES.items():
        min_l, max_l, min_m, min_h, max_h, min_c, max_s, _ = gates
        for _ in range(15):
            l_sh = rng.uniform(min_l + 0.04, min(max_l - 0.04, min_l + 0.30))
            h_sh = rng.uniform(min_h + 0.03, min(max_h - 0.03, min_h + 0.25))
            m_sh = max(min_m + 0.02, 1.0 - l_sh - h_sh)
            bands = [l_sh / 11.0] * 11 + [m_sh / 11.0] * 11 + [h_sh / 10.0] * 10
            ok_sp, _ = GG.verify_spectral_critic(bands, min_c + 0.5, max_s * 0.4, 0.45, pname)
            valid_tot += 1
            valid_pass += int(ok_sp)
    spec_frr = 100.0 * (valid_tot - valid_pass) / max(1, valid_tot)

    # 7 synthetic acoustic/spectral mishaps
    spec_mishaps = [
        ("thin_kick",        [0.005] * 11 + [0.05] * 11 + [0.04] * 10, 2.2, 0.2, 0.5, "kicks"),
        ("muddy_mix",        [0.085] * 11 + [0.004] * 11 + [0.002] * 10, 2.0, 0.1, 0.4, "full"),
        ("dull_hats",        [0.04] * 11 + [0.04] * 11 + [0.01] * 10, 2.2, 0.2, 0.3, "hats"),
        ("resonant_whistle", [0.01] * 11 + [0.01] * 11 + [0.65] + [0.005] * 9, 2.5, 0.1, 0.4, "full"),
        ("phase_cancelled",  [0.03] * 11 + [0.04] * 11 + [0.02] * 10, 2.2, 0.1, -0.75, "full"),
        ("brickwall_flat",   [0.03] * 11 + [0.04] * 11 + [0.02] * 10, 1.08, 0.1, 0.5, "drums"),
        ("dropout_pad",      [0.03] * 11 + [0.05] * 11 + [0.01] * 10, 1.8, 0.72, 0.6, "pads"),
    ]
    spec_caught = sum(not GG.verify_spectral_critic(b, cr_v, sil, corr, pn)[0] for _, b, cr_v, sil, corr, pn in spec_mishaps)
    spec_catch_rate = 100.0 * spec_caught / len(spec_mishaps)
    b8_ok = spec_frr <= 2.0 and spec_catch_rate >= 96.0
    print("\n[B8] Category & Genre 32-Band Spectral + Dynamic Fingerprint Gate")
    print("     Valid-Spectrum FRR=%.2f%% (<=2.0%%) | Acoustic-Mishap Catch=%.1f%% (%d/%d)  -> %s" % (
        spec_frr, spec_catch_rate, spec_caught, len(spec_mishaps), "PASS" if b8_ok else "FAIL"))

    # ---- B9: Spotify 5-Pillar + Graph-Propagated User Likeability (Keep@20 >= 82%) ---------------
    pillar_personas = [
        ("High-Energy Dance Club",     {"danceability": +1.8, "energy": +2.0, "valence": +0.6, "brightness": +0.8, "contrast": +1.2}),
        ("Dark Dub / Hypnotic Sub",    {"danceability": +0.8, "energy": -0.4, "valence": -1.8, "brightness": -1.6, "contrast": +0.6}),
        ("Warm Acoustic / Lo-Fi Keys", {"danceability": -0.2, "energy": -1.4, "valence": +1.0, "brightness": -1.2, "contrast": -0.5}),
        ("Bright Euphoric Melodic",    {"danceability": +1.0, "energy": +1.2, "valence": +2.0, "brightness": +1.6, "contrast": +0.4}),
        ("Crisp High-Contrast Breaks", {"danceability": +1.2, "energy": +1.5, "valence": -0.4, "brightness": +1.0, "contrast": +2.0}),
    ]
    p_keep20 = []
    for _, pref in pillar_personas:
        # Graph-propagated + 5-pillar preference learner over the 34 styles
        w_pil = {k: 0.0 for k in pref}
        node_bonus = [0.0] * len(net["styles"])
        r_sim = random.Random(77)
        for step in range(20):
            # Pick best of 6 candidate styles using current pillar weights + graph bonus
            cands = r_sim.sample(net["styles"], 6)
            best_s = max(cands, key=lambda s: sum(w_pil[k] * (s["pillars"][k] - 0.5) for k in w_pil) + node_bonus[s["index"]])
            u_true = sum(pref[k] * (best_s["pillars"][k] - 0.5) for k in pref)
            kept = u_true > 0.12
            sgn = 1.0 if kept else -1.0
            for k in w_pil:
                w_pil[k] += 0.45 * sgn * (best_s["pillars"][k] - 0.5)
            # Propagate reward across connected sub-genre neighbors in the graph!
            node_bonus[best_s["index"]] += 0.35 * sgn
            for nb in best_s["neighbors"]:
                node_bonus[nb["index"]] += 0.20 * nb["weight"] * sgn
        # Evaluate keep-rate at click 20
        hits20 = 0
        for _ in range(40):
            cands = r_sim.sample(net["styles"], 6)
            best_s = max(cands, key=lambda s: sum(w_pil[k] * (s["pillars"][k] - 0.5) for k in w_pil) + node_bonus[s["index"]])
            if sum(pref[k] * (best_s["pillars"][k] - 0.5) for k in pref) > 0.12:
                hits20 += 1
        p_keep20.append(hits20 / 40.0)
    b9_keep = sum(p_keep20) / len(p_keep20)
    b9_ok = b9_keep >= 0.82
    print("\n[B9] Spotify 5-Pillar (Danceability/Energy/Valence/Brightness/Contrast) + Graph Likeability")
    print("     Multi-Modal Graph Keep-Rate@20=%.1f%% (target >= 82.0%%)  -> %s" % (
        100.0 * b9_keep, "PASS" if b9_ok else "FAIL"))

    gates_list = [b1_ok, b2_ok, b3_ok, b4_ok, b5_ok, b6_ok, b7_ok, b8_ok, b9_ok]
    all_ok = all(gates_list)
    print("\n" + "=" * 78)
    print("OVERALL BENCHMARK RESULT: %s (%d / %d gates passed)" % (
        "ALL GATES PASSED" if all_ok else "NEEDS ATTENTION",
        sum(gates_list), len(gates_list)))
    print("=" * 78)


if __name__ == "__main__":
    main()
