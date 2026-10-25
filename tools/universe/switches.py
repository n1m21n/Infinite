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


def apply_inf(text, spec, facts):
    """Post-compile overrides of a scene genome: `14=Blur` swaps node 14's type (defaults for the new one), `9.mode=2` sets an enum/number."""
    swaps, sets = {}, {}
    for tk in spec.split():
        lhs, _, val = tk.partition("=")
        if "." in lhs:
            k, _, key = lhs.partition("."); sets.setdefault(int(k), {})[key] = val
        else:
            swaps[int(lhs)] = val.replace("_", " ")
    out, cur, typ, dropping = [], None, None, False
    for line in text.splitlines():
        p = line.split()
        if line.startswith("node "):
            cur, typ, dropping = int(p[1]), " ".join(p[3:]), False
            if swaps.get(cur) in facts:
                typ = swaps[cur]; dropping = True
                out.append("node %d %s %s" % (cur, facts[typ]["category"], typ.split(":")[0]))
                for key, (tag, val) in sorted(u.seeded_params(facts[typ], 7919 * cur + 17).items()):  # seeded, not default: most filters do nothing at their defaults
                    out.append("  %s %s %s" % (tag, key, ("%.6g" % val) if tag == "f" else val if tag == "s" else int(val)))
                continue
        elif line == "end" and cur in sets:
            tags = {r["key"]: r["tag"] for r in facts[typ]["rows"]}
            for key, val in sets[cur].items():
                j = len(out)
                while j > 0 and not out[j - 1].startswith("node "):  # drop the node's earlier line for this key: the first one wins in the engine
                    j -= 1
                    if out[j].split()[1:2] == [key]:
                        del out[j]
                out.append("  %s %s %s" % (tags.get(key, "i"), key, val))
        elif dropping and line.startswith("  "):
            continue
        out.append(line)
    return "\n".join(out) + "\n"


def name_of(text, k):
    for l in text.splitlines():
        p = l.split()
        if l.startswith("node ") and int(p[1]) == k:
            return " ".join(p[3:])


def propose_inf(code, facts, P, r, text=None):
    """Pick one node of the compiled scene and turn it into a switch group: `... %% 14={Blur|Bloom}` or `... %% 9.mode={0|2}`."""
    import scene
    text = text or scene.compile_scene(code, facts, P)
    nodes = [(int(l.split()[1]), " ".join(l.split()[3:])) for l in text.splitlines() if l.startswith("node ")]
    modded = {}
    for l in text.splitlines():
        if l.startswith("mod "):
            q = l.split(); modded.setdefault(int(q[1]), set()).add(q[2])
    cands = []
    for k, n in nodes:
        if n not in facts or n in ("Audio Out", "Output", "Render 3D") or facts[n]["category"] in ("Modulators", "Macros", "Prediction") and not facts[n]["inputs"]:
            continue
        t = facts[n]; need = modded.get(k, set())
        sib = [m for m in facts if m != n and ":" not in m and signature(facts[m]) == signature(t) and not facts[m]["hardware_driven"]
               and need <= {x.get("key") or str(x.get("index")) for x in facts[m]["modulatable"]}]
        if sib:
            cands.append(("type", k, sib))
        if t["category"] in ("Modulators", "Macros"):
            continue  # a modulator's mode barely shows in one frame; the type swaps above already cover them
        for row in t["rows"]:
            if row.get("options") and len(row["options"]) >= 2 and row.get("ui", True) and row["key"] not in need:
                cands.append(("mode", k, row))
    if not cands:
        return None
    kind, k, x = r.choice(cands)
    if kind == "type":
        alts = [name_of(text, k)] + r.sample(x, min(len(x), r.randint(1, 3)))
        spec = "%d={%s}" % (k, "|".join(a.replace(" ", "_") for a in alts))
    else:
        vals = r.sample(range(len(x["options"])), min(len(x["options"]), r.randint(2, 4)))
        spec = "%d.%s={%s}" % (k, x["key"], "|".join(map(str, vals)))
    return code.partition(" %% ")[0] + " %% " + spec


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
