#!/usr/bin/env python3
"""Switch slots: one node position, several alternatives, one sibling patch per alternative.
    {E7.3|E12.1|E4.9}     type switch: reverb <-> delay <-> bitcrush at the same spot (same pins, same mods)
    S3.2~fmMode=1         mode switch: an enum parameter of the node set explicitly (wavetable pm <-> fm, Union <-> Difference ...)
    S3.2{~fmMode=0|~fmMode=1}   both forms can mix:  {S3.2~fmMode=0|S3.2~fmMode=1|S9.2}
    switches.py "<audio genome>" [--n 3] [--seed 1]     print a switch genome and its expansion
    expand(code) -> [genome, ...]       propose(code, facts, P, r) -> switch genome"""
import itertools, random, re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u

TOK = re.compile(r"(?<![\w.~])([A-Z])(\d+)(?:\.(\d+))?((?:~\w+=-?[\d.]+)*)")
GROUP = re.compile(r"\{([^{}]*)\}")


def expand(code, cap=24):
    """Every combination of the `{a|b|c}` groups of a genome (first alternative first); a plain genome expands to itself."""
    groups = GROUP.findall(code)
    if not groups:
        return [code]
    out = []
    for combo in itertools.islice(itertools.product(*[g.split("|") for g in groups]), cap):
        it = iter(combo)
        out.append(GROUP.sub(lambda m: next(it), code))
    return out


def slots(code, P):
    """[(start, end, letter, index, pool)] for every real chain node of the main chain, in compile order (what `@k` indexes)."""
    head = code.partition(";")[0]
    found = []
    for m in TOK.finditer(head):
        pre = head[:m.start()].rstrip()
        first = m.group(1) == "N" and (not pre or pre[-1] in "[+=&")
        letter = m.group(1)
        if letter not in P or letter in "OLIZ" and False:
            continue
        found.append((m.start(), m.end(), letter, int(m.group(2)), P["G" if first else letter]))
    return found


def signature(t):
    return (t["category"], tuple(i["kind"] for i in t["inputs"]), tuple(o["kind"] for o in t["outputs"]))


def targeted(code, k):
    """Keys that mods drive on compiled node k (an alternative must keep them)."""
    return {key for _, _, kk, key, _ in u.parse(code)[3] if kk == k}


def propose(code, facts, P, r, kinds=("type", "mode")):
    """Pick one switchable slot and return the genome with that slot turned into a switch group, or None."""
    cands = []
    for k, (a, b, letter, idx, pool) in enumerate(slots(code, P)):
        if letter in "OJX":
            continue
        name = pool[idx % len(pool)]
        if name not in facts:
            continue
        t = facts[name]
        need = targeted(code, k)
        if "type" in kinds:
            sib = [j for j, n in enumerate(pool) if n != name and n in facts and signature(facts[n]) == signature(t)
                   and need <= {m.get("key") for m in facts[n]["modulatable"]} and not facts[n]["hardware_driven"]]
            if len(sib) >= 1:
                cands.append(("type", a, b, letter, idx, sib))
        if "mode" in kinds:
            for row in t["rows"]:
                if row.get("options") and len(row["options"]) >= 2 and row.get("ui", True) and row["key"] not in need:
                    cands.append(("mode", a, b, letter, idx, row))
    if not cands:
        return None
    kind, a, b, letter, idx, x = r.choice(cands)
    m = TOK.match(code, a)
    base = code[a:b]
    if kind == "type":
        alts = [base] + ["%s%d.%s" % (letter, j, m.group(3) or 0) for j in r.sample(x, min(len(x), r.randint(1, 3)))]
    else:
        vals = r.sample(range(len(x["options"])), min(len(x["options"]), r.randint(2, 4)))
        alts = ["%s%d.%s~%s=%d" % (letter, idx, m.group(3) or 0, x["key"], v) for v in vals]
    return code[:a] + "{" + "|".join(alts) + "}" + code[b:]


if __name__ == "__main__":
    import scene
    facts = u.load_facts(); P = scene.pools_v2(facts)
    code = sys.argv[1]
    n = int(sys.argv[sys.argv.index("--n") + 1]) if "--n" in sys.argv else 3
    r = random.Random(int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 1)
    for _ in range(n):
        sw = propose(code, facts, P, r)
        print(sw)
        for e in expand(sw) if sw else []:
            print("   ", e)
