"""Phase 2 check: Lur'e matrices dumped by INFINITE_FIELDSTABDUMP_SAT=1 (Float32 interpreter, saturators
cut) vs the Python reference.

    python phase2_compare.py <lmat dir> [out.json]     writes data/phase2-compare.json (default)

Pass rule (README section 8, phase 2): every kernel dumped (status ok), the matcher finds the same
saturator shapes as the reference, D = 0, and max |field - python| <= 1e-5 over A, B, C at every grid
point. The interpreter numbers saturators in program order and Python in its own order, so the
saturators are matched by the permutation (same shapes only) with the smallest error.
"""
import itertools, json, pathlib, sys
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from emit_sat import kernels  # noqa: E402

TOL = 1e-5

def read_lmat(p):
    lines = p.read_text().splitlines()
    head = lines[0].split()
    kv = {head[i]: head[i + 1] for i in range(0, 12, 2)}
    rows = [[float(x) for x in ln.split()] for ln in lines[1:] if ln.strip()]
    return kv, head, rows

def compare(kv, rows, params, ref, shapes):
    n, m, np_ = int(kv["n"]), int(kv["m"]), int(kv["p"])
    got = kv["shapes"].split(",") if kv["shapes"] != "-" else []
    if sorted(got) != sorted(shapes):
        return {"pass": False, "why": f"shapes {got} vs {shapes}"}
    pts = []
    for r in rows:
        th, v = r[:np_], np.array(r[np_:])
        A = v[:n * n].reshape(n, n); v = v[n * n:]
        B = v[:n * m].reshape(n, m); v = v[n * m:]
        C = v[:m * n].reshape(m, n); v = v[m * n:]
        D = v[:m * m].reshape(m, m)
        pts.append((th, A, B, C, D))
    refs = [tuple(np.atleast_2d(x) for x in ref(th)) for (th, *_r) in pts]
    best = None
    for perm in itertools.permutations(range(m)):
        if any(got[perm[i]] != shapes[i] for i in range(m)):
            continue
        err = scale = 0.0
        for (th, A, B, C, D), (Ar, Br, Cr) in zip(pts, refs):
            Bp, Cp = B[:, perm], C[perm, :]
            err = max(err, np.max(np.abs(A - Ar)), np.max(np.abs(Bp - Br)) if m else 0.0,
                      np.max(np.abs(Cp - Cr)) if m else 0.0)
            scale = max(scale, np.max(np.abs(Ar)), np.max(np.abs(Br)) if m else 0.0, np.max(np.abs(Cr)) if m else 0.0)
        if best is None or err < best[0]:
            best = (err, scale, perm)
    dmax = max((float(np.max(np.abs(D))) if m else 0.0) for (*_r, D) in pts)
    err, scale, perm = best
    ok = err <= TOL and dmax <= TOL
    return {"pass": bool(ok), "max_abs": float(err), "max_rel": float(err / max(scale, 1e-30)), "D_max": dmax,
            "points": len(pts), "m": m, "n": n, "perm": list(perm)}

if __name__ == "__main__":
    lm = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else HERE.parent / "data" / "phase2-compare.json"
    res = {}
    for set_, name, _, params, grid, ref, shapes in kernels():
        if set_ == "B" and "B" not in out.name:
            continue
        p = lm / set_ / f"{name}.lmat"
        if not p.exists():
            res[f"{set_}/{name}"] = {"pass": False, "status": "missing"}
            continue
        kv, head, rows = read_lmat(p)
        status = head[1]
        if status != "ok":
            res[f"{set_}/{name}"] = {"pass": False, "status": status, "line": " ".join(head)}
            continue
        r = compare(kv, rows, params, ref, shapes)
        r["status"] = "ok"
        r["gate"] = float(kv["gate"])
        res[f"{set_}/{name}"] = r
    out.write_text(json.dumps(res, indent=1))
    by = {}
    for k, r in res.items():
        s = k.split("/")[0]
        b = by.setdefault(s, {"kernels": 0, "ok": 0, "pass": 0, "max_abs": 0.0, "fail": []})
        b["kernels"] += 1
        b["ok"] += r["status"] == "ok"
        b["pass"] += r["pass"]
        if r["status"] == "ok":
            b["max_abs"] = max(b["max_abs"], r.get("max_abs", 0.0))
        if not r["pass"]:
            b["fail"].append((k, r.get("status"), r.get("why", r.get("line", r.get("max_abs")))))
    for s, b in by.items():
        print(f"{s}: {b['kernels']} kernels, {b['ok']} dumped, {b['pass']} pass, max abs err {b['max_abs']:.2e}")
        for f in b["fail"][:12]:
            print("   fail", f)
    print("wrote", out)
