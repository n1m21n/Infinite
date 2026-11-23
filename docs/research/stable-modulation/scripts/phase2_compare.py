"""Phase 2 check: matrices dumped by INFINITE_FIELDSTABDUMP (Float32 interpreter) vs the Python reference.

    python phase2_compare.py <amat dir>     writes data/phase2-compare.json and prints a table

Pass rule (README section 8, phase 2): every kernel dumped (status ok), and max |A_field - A_python| <= 1e-5
over all its grid points. Both the absolute and the relative error (over max |A|) are recorded, since
Float32 rounding grows with the size of the entries.
"""
import json, pathlib, sys
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from emit_field import kernels  # noqa: E402

TOL = 1e-5

def read_amat(p):
    lines = p.read_text().splitlines()
    head = lines[0].split()
    status, n, np_ = head[1], int(head[3]), int(head[5])
    rows = [[float(x) for x in ln.split()] for ln in lines[1:] if ln.strip()]
    return status, n, np_, head, rows

if __name__ == "__main__":
    amat = pathlib.Path(sys.argv[1])
    res = []
    for set_, name, _, params, grid, ref in kernels():
        p = amat / set_ / f"{name}.amat"
        if not p.exists():
            res.append({"set": set_, "name": name, "status": "missing"})
            continue
        status, n, npar, head, rows = read_amat(p)
        r = {"set": set_, "name": name, "status": status, "n": n, "gate": float(head[7]),
             "reason": " ".join(head[8:])}
        if status == "ok":
            ea, er, worst = 0.0, 0.0, None
            for row in rows:
                th = row[:npar]
                Af = np.array(row[npar:]).reshape(n, n)
                Ap = np.asarray(ref(th), float).reshape(n, n)
                e = float(np.max(np.abs(Af - Ap)))
                if e > ea:
                    ea, worst = e, th
                er = max(er, e / max(1.0, float(np.max(np.abs(Ap)))))
            r.update({"points": len(rows), "expected_points": len(grid), "max_abs": ea, "max_rel": er,
                      "worst_theta": worst, "pass": bool(ea <= TOL and len(rows) == len(grid))})
        else:
            r["pass"] = False
        res.append(r)
    out = HERE.parent / "data" / "phase2-compare.json"
    out.write_text(json.dumps(res, indent=1))
    for s in ("L", "G"):
        rs = [r for r in res if r["set"] == s]
        ok = [r for r in rs if r.get("pass")]
        print(f"{s}: {len(ok)}/{len(rs)} pass; max abs {max((r.get('max_abs', 0) for r in rs), default=0):.3g}")
        for r in rs:
            if s == "L" or not r.get("pass"):
                print(f"  {r['name']:<22} {r['status']:<12} n {r.get('n', '-')}  pts {r.get('points', '-')}"
                      f"  abs {r.get('max_abs', float('nan')):.3g}  rel {r.get('max_rel', float('nan')):.3g}"
                      f"  gate {r.get('gate', float('nan')):.3g} {r.get('reason', '')}")
    print("wrote", out)
