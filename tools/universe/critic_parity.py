#!/usr/bin/env python3
"""Does the C++ role critic (src/app/graph/RoleCritic.cpp) measure and score what the Python one does?
    critic_parity.py [--gen website/gen] [--first 70000] [--seeds 1] [--styles 0,7,14,21,28]
Renders parts with the live generator (wasm_render_parts.js, shaped), measures each with frame_features.py and with role_critic_cli
(built here with clang++ from RoleCritic.cpp), and compares: feature vectors in the critic's own units (difference / training spread),
P(declared role), and the accept / reject call against RoleCriticWeights.inc's cut-offs."""
import argparse, json, os, re, subprocess, sys, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import frame_features as ff
ROOT = os.path.abspath(os.path.join(HERE, "../.."))

def inc():
    s = open(os.path.join(ROOT, "src/app/graph/RoleCriticWeights.inc")).read()
    get = lambda n: np.array([float(v.rstrip("f")) for v in re.search(r"%s\[\d+\] = \{(.*?)\};" % n, s, re.S).group(1).replace("\n", " ").split(",") if v.strip()])
    names = re.findall(r'"(\w+)"', re.search(r"kRoleNames\[kRoles\] = \{(.*?)\};", s).group(1))
    return names, get("kScale"), get("kCut")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--gen", default=os.path.join(ROOT, "website/gen")); ap.add_argument("--first", type=int, default=70000)
    ap.add_argument("--seeds", type=int, default=1); ap.add_argument("--styles", default="0,7,14,21,28"); ap.add_argument("--out"); a = ap.parse_args()
    tmp = tempfile.mkdtemp(); cli = os.path.join(tmp, "role_critic_cli")
    subprocess.check_call(["clang++", "-std=c++17", "-O2", "-I", os.path.join(ROOT, "src"), os.path.join(HERE, "role_critic_cli.cpp"),
                           os.path.join(ROOT, "src/app/graph/RoleCritic.cpp"), "-o", cli])
    names, scale, cut = inc(); heads, py, stream = [], [], bytearray()
    for st in a.styles.split(","):
        env = dict(os.environ, STYLE_FROM=st, STYLE_STEP="1000")
        p = subprocess.Popen(["node", os.path.join(HERE, "wasm_render_parts.js"), a.gen, str(a.first), str(a.seeds), "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15", "shaped"], stdout=subprocess.PIPE, env=env)
        while True:
            h = p.stdout.readline()
            if not h: break
            r = json.loads(h); raw = p.stdout.read(r["frames"] * 8); x = np.frombuffer(raw, np.float32).reshape(-1, 2).astype(float)
            heads.append(r); py.append(np.nan_to_num(ff.vector(ff.measure(x[:, 0], x[:, 1], 48000)))); stream += h + raw
        p.wait()
    out = subprocess.run([cli], input=bytes(stream), capture_output=True, check=True).stdout.decode().splitlines()
    cx = [json.loads(l) for l in out]; X = np.array([c["x"] for c in cx]); P = np.array([c["p"] for c in cx]); Y = np.array(py)
    d = np.abs(X - Y) / scale; role = [names.index(h["label"].split()[0]) for h in heads]
    # the Python model's probabilities from the same weights, on the Python features
    s = open(os.path.join(ROOT, "src/app/graph/RoleCriticWeights.inc")).read()
    g = lambda n: np.array([float(v.rstrip("f")) for v in re.search(r"%s\[\d+\] = \{(.*?)\};" % n, s, re.S).group(1).replace("\n", " ").split(",") if v.strip()])
    h1, h2 = int(re.search(r"kH1 = (\d+)", s).group(1)), int(re.search(r"kH2 = (\d+)", s).group(1))
    z = (Y - g("kMean")) / scale; a1 = np.maximum(z @ g("kW1").reshape(-1, h1) + g("kB1"), 0); a2 = np.maximum(a1 @ g("kW2").reshape(h1, h2) + g("kB2"), 0)
    o = a2 @ g("kW3").reshape(h2, -1) + g("kB3"); Pp = np.exp(o - o.max(1, keepdims=True)); Pp /= Pp.sum(1, keepdims=True)
    pc = P[np.arange(len(role)), role]; pp = Pp[np.arange(len(role)), role]; agree = ((pc >= cut[role]) == (pp >= cut[role])).mean()
    worst = np.argsort(-d.max(1))[:5]
    res = {"parts": len(heads), "feature_diff_in_spreads": {"median": float(np.median(d)), "p99": float(np.percentile(d, 99)), "max": float(d.max())},
           "worst_features": [[int(i), int(d[i].argmax()), float(d[i].max())] for i in worst], "p_declared_abs_diff_max": float(np.abs(pc - pp).max()),
           "accept_agree": float(agree), "accepted_cpp": float((pc >= cut[role]).mean())}
    print(json.dumps(res, indent=1))
    if a.out: json.dump(res, open(a.out, "w"), indent=1)

if __name__ == "__main__":
    main()
