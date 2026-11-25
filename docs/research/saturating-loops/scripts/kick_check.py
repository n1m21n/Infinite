"""Quick checks for kick-the-tires.sh: rerun the locked code on a slice and compare with data/.

  1. Phase 1 + phase 3 on the 24 fastest G kernels, the two windup kernels (s122-067, s122-124) and the
     12 fastest B kernels: B4, T1, G1, witness kind, G2 and two-start verdicts must equal data/.
  2. With an Infinite binary: L written as Field kernels, dumped with saturators cut, compared with Python
     (11/11 within 1e-5).
  3. The gate's negative test: three loops with a nonlinearity outside the library must come back out of
     scope, the clamp control must pass.

    python kick_check.py <rerun dir> [infinite binary]
Prints one line per failed check and "<n> checks, <k> failed"; exits 1 on any failure.
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1"); os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
import json, pathlib, subprocess, sys, tempfile, warnings
import multiprocessing as mp
warnings.filterwarnings("ignore")
import importlib.util
def _own(name):
    """This study's module by path: satloops puts Report 2's scripts, which reuse these file names, on sys.path."""
    spec = importlib.util.spec_from_file_location(name, pathlib.Path(__file__).resolve().parent / f"{name}.py")
    mod = importlib.util.module_from_spec(spec); sys.modules[name] = mod; spec.loader.exec_module(mod)
    return mod
phase3_score = _own("phase3_score")
phase2_compare = _own("phase2_compare")
import phase1_G

HERE = pathlib.Path(__file__).resolve().parent.parent
checks = fails = 0

def check(ok, what):
    global checks, fails
    checks += 1
    if not ok:
        fails += 1
        print("FAIL", what)

def kind(w):
    return None if not w else (w.get("kind") if isinstance(w, dict) else str(w).split("'kind': '")[1].split("'")[0])

def slice_check(set_, ids):
    specs = {s["id"]: s for s in json.loads((HERE / "corpus" / f"{set_}.json").read_text())}
    p1 = json.loads((HERE / "data" / f"phase1-{set_}.json").read_text())
    p3 = json.loads((HERE / "data" / f"phase3-{set_}.json").read_text())
    with mp.get_context("fork").Pool(4) as pool:
        base = pool.map(phase1_G.run, [specs[i] for i in ids])
        jobs = [(specs[r["id"]], r, p3[r["id"]]["phase2"], 1e-5) for r in base]
        got = pool.map(phase3_score.extras, jobs)
    for r in got:
        i, ref = r["id"], p3[r["id"]]
        for key in ("B4", "T1"):
            check(r[key]["status"] == ref[key]["status"], f"{set_} {i} {key}: {r[key]['status']} vs {ref[key]['status']}")
        check(r["G1"] == ref["G1"] == p1[i]["G1"], f"{set_} {i} G1: {r['G1']} vs {ref['G1']}")
        check(kind(r["witness"]) == kind(ref["witness"]), f"{set_} {i} witness: {kind(r['witness'])} vs {kind(ref['witness'])}")
        if ref["G1"] == "certified":
            check(r.get("G2") == ref.get("G2"), f"{set_} {i} G2: {r.get('G2')} vs {ref.get('G2')}")
            check(r["two_start"]["fail"] == ref["two_start"]["fail"], f"{set_} {i} two-start")
    return len(got)

def fastest(set_, k):
    p1 = json.loads((HERE / "data" / f"phase1-{set_}.json").read_text())
    return [i for _, i in sorted((r["seconds"], i) for i, r in p1.items())[:k]]

def field_checks(bin_, out):
    from emit_sat import kernels
    from emit_field import fmt
    src = pathlib.Path(tempfile.mkdtemp()) / "field"
    (src / "L").mkdir(parents=True)
    for set_, name, txt, _, grid, _, _ in kernels():
        if set_ == "L":
            (src / "L" / f"{name}.field").write_text(txt)
            (src / "L" / f"{name}.grid").write_text("".join(" ".join(fmt(v) for v in th) + "\n" for th in grid))
    neg = {"expfb": "g*(exp(s) - 1) + in", "rational": "g*s/(1 + s*s) + in",
           "badclip": "g*min(s, -0.5) + in", "goodclip": "g*clamp(s, -1, 1) + in"}
    (src / "N").mkdir()
    for k, e in neg.items():
        (src / "N" / f"{k}.field").write_text(f"param float g = 0.5 [0, 1]\nstate float s = 0\nnx = {e}\ns = nx\nout = s\n")
        (src / "N" / f"{k}.grid").write_text("0.5\n")
    lm = pathlib.Path(out) / "lmat"
    env = dict(os.environ, INFINITE_FIELDSTABDUMP=str(src), INFINITE_FIELDSTABDUMP_OUT=str(lm), INFINITE_FIELDSTABDUMP_SAT="1")
    subprocess.run([bin_], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    for set_, name, _, params, _, ref, shapes in kernels():
        if set_ != "L":
            continue
        p = lm / "L" / f"{name}.lmat"
        if not p.exists():
            check(False, f"L {name}: not dumped")
            continue
        kv, head, rows = phase2_compare.read_lmat(p)
        r = phase2_compare.compare(kv, rows, params, ref, shapes) if head[1] == "ok" else {"pass": False, "max_abs": head[1]}
        check(r["pass"], f"L {name}: field vs python {r.get('max_abs')}")
    for k in neg:
        p = lm / "N" / f"{k}.lmat"
        st = p.read_text().split()[1] if p.exists() else "missing"
        check(st == ("ok" if k == "goodclip" else "out-of-scope"), f"gate {k}: {st}")

if __name__ == "__main__":
    out = sys.argv[1]
    bin_ = sys.argv[2] if len(sys.argv) > 2 else ""
    n = slice_check("G", sorted(set(fastest("G", 24)) | {"s122-067", "s122-124"}))
    print(f"G slice: {n} kernels rerun", flush=True)
    if (HERE / "data" / "phase3-B.json").exists():
        n = slice_check("B", fastest("B", 12))
        print(f"B slice: {n} kernels rerun", flush=True)
    if bin_:
        field_checks(bin_, out)
        print("Field: L dump + gate negatives checked", flush=True)
    else:
        print("no Infinite binary: Field checks skipped")
    print(f"{checks} checks, {fails} failed")
    sys.exit(1 if fails else 0)
