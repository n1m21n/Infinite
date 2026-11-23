"""Quick check for kick-the-tires.sh: re-runs a slice of every phase from this folder's scripts and compares it
with the recorded data/. Writes nothing into data/.

  1. B3 (phase 0): the oracle's smallest unstable k for Moog and diode, from data-rerun/phase0-b3.json
     (kick-the-tires.sh runs phase0_b3.py first), must lie in (2.88, 2.89] and (8.90, 8.91].
  2. H2 (phase 1): Moog at k <= 2.85 and diode at k <= 8.81 must certify; Moog at 2.90 and diode at 8.92 must not.
  3. H1 / H3 (phases 3, 4): all 16 L kernels plus 12 G and 12 B kernels are scored again with the locked
     scorer; witness and status must match data/phase3-L-G.json and data/phase4-B.json exactly.
  4. Float32 (phase 3): if an Infinite binary is found, the L certificates are re-checked on the interpreter
     and must give the same verdict.

    python kick_check.py <rerun dir>          exit 0 when every check matches
"""
import json, pathlib, sys, time
from multiprocessing import Pool

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import phase3_score as ps  # noqa: E402
from certify import Box, certify  # noqa: E402
from stabmod import discretise, moog_Ac, diode_Ac  # noqa: E402
from emit_field import kernels  # noqa: E402

G_PICK = ["g121-000", "g121-002", "g121-013", "g121-029", "g121-050", "g121-084", "g121-107", "g121-115",
          "g121-140", "g121-145", "g121-174", "g121-190"]
B_PICK = ["g123-000", "g123-001", "g123-014", "g123-039", "g123-047", "g123-061", "g123-098", "g123-123",
          "g123-156", "g123-160", "g123-172", "g123-199"]

fails = []

def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what, flush=True)
    if not ok:
        fails.append(what)

if __name__ == "__main__":
    rerun = pathlib.Path(sys.argv[1])
    t0 = time.time()
    print("B3: oracle against the paper's counterexamples")
    b3 = json.loads((rerun / "phase0-b3.json").read_text())
    for name, lo, hi in (("moog", 2.88, 2.89), ("diode", 8.90, 8.91)):
        v = b3["oracle"][name]["smallest_unstable_kmax"]
        check(lo < v <= hi, f"{name}: smallest unstable k {v} in ({lo}, {hi}]")

    print("H2: certifier at the published bounds")
    for name, Ac, good, bad in (("moog", moog_Ac, 2.85, 2.90), ("diode", diode_Ac, 8.81, 8.92)):
        for kmax, want in ((good, "certified"), (bad, "no proof")):
            r = certify(lambda g, k: discretise(Ac(k), g), Box([1e-5, 0], [1e3, kmax], log=[0]))
            check((r["status"] == "certified") == (want == "certified"), f"{name} k <= {kmax}: {r['status']} (want {want})")

    print("H1 / H3: locked scorer on 16 L + 12 G + 12 B kernels")
    ps.init_refs()
    specs = {s["id"]: s for c in ("G", "B") for s in json.loads((ROOT / "corpus" / f"{c}.json").read_text())}
    want = {r["name"]: r for f in ("phase3-L-G.json", "phase4-B.json")
            for r in json.loads((ROOT / "data" / f).read_text())["rows"]}
    pick = set(G_PICK + B_PICK)
    jobs = [(s, nm, params, specs.get(nm), i) for i, (s, nm, _, params, _, _) in enumerate(kernels())
            if s == "L" or nm in pick]
    with Pool(4, initializer=ps.init_refs) as pool:
        rows = list(pool.imap(ps.score_one, jobs))
    for r in rows:
        w = want[r["name"]]
        check(r["witness"] == w["witness"] and r["status"] == w["status"],
              f"{r['set']} {r['name']}: witness {r['witness']}, {r['status']}")
    check(not any(r["witness"] and r["status"] == "certified" for r in rows), "H1: no certified kernel has a witness")

    print("Float32: L certificates on the interpreter")
    try:
        ps.find_bin()
        have_bin = True
    except SystemExit:
        have_bin = False
        print("  skip  no Infinite binary (set INFINITE_BIN, or build with scripts/ibuild.py)")
    if have_bin:
        L = [r for r in rows if r["set"] == "L"]
        ps.float32_check(L, ROOT / "corpus" / "field", specs, {(s, nm): pr for s, nm, _, pr, _, _ in kernels()})
        for r in L:
            if "f32" in r:
                check(r["f32"]["holds"] == want[r["name"]]["f32"]["holds"],
                      f"L {r['name']}: Float32 holds {r['f32']['holds']}, max norm {r['f32'].get('max_norm', 0):.9f}")

    print(f"{len(fails)} failed, {time.time() - t0:.0f} s")
    sys.exit(1 if fails else 0)
