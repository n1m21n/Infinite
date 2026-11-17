#!/usr/bin/env python3
"""Data ledger: every dataset in ~/re-output, who made it and who reads it.

What: one hand-kept table (LEDGER) of dataset -> producer tool, kind and consumers. `lint` checks it
      against the disk and the repo: every entry in ~/re-output is listed, every dataset has a consumer
      that really exists (a scoreboard metric, a parameter spec, a plan item, a doc or a tool), and no
      result measured on Infinite is older than the code it measured.
Why:  data nobody reads is wasted work, and a number measured before a fix reads as a current fact
      (builtins-kit showed Analog's pre-fix tuning for a day).
Kinds: competitor  extracted from another product; does not go stale when Infinite changes
       infinite    measured on Infinite; stale once its sources change after the newest result
       study       one-off measurement or kit input; kept for its doc
       superseded  an earlier pass kept as raw input; names the dataset that replaced it
       scratch     temp files and logs; ignored
Usage:
  python3 tools/re-survey/data_ledger.py lint                  # exit 1 on a missing consumer or stale result
  python3 tools/re-survey/data_ledger.py table [--out t.md]    # the ledger as a table
  python3 tools/re-survey/data_ledger.py changed [--base main] # kits whose sources the branch touched, with commands
  python3 tools/re-survey/data_ledger.py --self-test
Exit: 0 ok, 1 lint or self-test failed, 2 usage error.
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import REPO, RE_OUTPUT, SelfTest, md_table, write_out  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PLAN = os.path.join(REPO, "internal/plans/v0.6-quality/README.md")
REGISTRY = os.path.join(REPO, "docs/quality/registry.tsv")
PARAM_DIFF = os.path.join(HERE, "param_diff.py")
COMMUNITY = os.path.join(REPO, "tools/website/community.json")
SKIP_DIRS = {"kit", "cache", "scratch"}  # inputs and caches: not results
T = "tools/re-survey/"
AUDIO = ["src/audio", ":(glob)src/nodes/Audio*"]
PATCH = ["src/core/Patch.cpp", "src/app/graph/PatchIO.cpp", "src/app/graph/PatchLoad.cpp"]


def D(tool, kind, consumers=(), sources=(), rerun=(), quiet=False, by=None):
    return {"tool": tool, "kind": kind, "consumers": list(consumers), "sources": list(sources),
            "rerun": list(rerun), "quiet": quiet, "by": by}


# Consumers: ("quality", registry source) ("spec", param_diff source) ("plan", item) ("doc", path, word)
#            ("tool", path, word) ("smoke", command)
LEDGER = {
    # Competitor extractions
    "ableton": D("ableton_schema_extractor.py", "competitor", [("spec", "ableton"), ("plan", "2.9"), ("plan", "2.17")]),
    "bitwig": D("bitwig_extractor.py", "competitor", [("plan", "2.8"), ("plan", "2.18")]),
    "blender": D("blender_extractor.py", "competitor", [("plan", "3.7")]),
    "cables": D("cables_extractor.py", "competitor", [("plan", "3.8")]),
    "logic": D("logic_extractor.py", "competitor", [("spec", "logic"), ("plan", "2.9")]),
    "modulation": D("modulation_map.py", "competitor",
                    [("doc", "internal/plans/modulation/BITWIG-GAPS-AND-OUTPACE.md", "modulation_map")]),
    "pixelmator": D("pixelmator_extractor.py", "competitor", [("plan", "3.5")]),
    "plugins": D("commercial_plugin_extractor.py", "competitor", [("spec", "plugin"), ("plan", "2.11")]),
    "touchdesigner": D("touchdesigner_map.py", "competitor", [("plan", "3.1")]),
    "inventory": D("inventory.py", "competitor", [("doc", "internal/plans/re-survey/README.md", "inventory")]),
    "capability-map": D("capability_map.py", "competitor", [("plan", "2.31")]),
    "ui-map": D("ui_map.py", "competitor", [("plan", "2.3")]),
    "plugin-probe": D("plugin_probe.py", "competitor", [("plan", "2.11"), ("doc", "internal/plans/re-survey/dossiers/C.md", "plugin_probe")]),
    "cables-app-rea-analysis.json.gz": D("readers.py", "study", [("doc", "internal/plans/re-survey/README.md", "cables-app-rea")]),
    "cables-ops-catalog.json.gz": D("readers.py", "superseded", by="cables"),
    # Measured on Infinite
    "builtins-kit": D("builtins_kit.py", "infinite", [("quality", "builtins_kit"), ("plan", "1.9")],
                      [T + "builtins_kit.py"] + AUDIO,
                      ["python3 tools/re-survey/builtins_kit.py infinite",
                       "python3 tools/re-survey/builtins_kit.py analyse ~/re-output/builtins-kit/Infinite --out ~/re-output/builtins-kit/infinite.md"]),
    "stretch-kit": D("stretch_kit.py", "infinite", [("quality", "stretch_kit")],
                     [T + "stretch_kit.py", "src/audio/dsp/ClipTimeStretch.h", "src/audio/dsp/TransientDetect.h"],
                     ["python3 tools/re-survey/stretch_kit.py infinite",
                      "python3 tools/re-survey/stretch_kit.py analyse ~/re-output/stretch-kit/Infinite --out ~/re-output/stretch-kit/infinite.md"]),
    "synth-probe": D("synth_probe.py", "infinite", [("quality", "synth_probe")], [T + "synth_probe.py"],
                     ["python3 tools/re-survey/synth_probe.py run node:Wavetable node:Analog"]),
    "stress": D("stress_kit.py", "infinite", [("quality", "stress_kit"), ("doc", "internal/plans/re-survey/ROBUSTNESS.md", "stress_kit")],
                [T + "stress_kit.py"] + PATCH, ["python3 tools/re-survey/stress_kit.py all"]),
    "host-audit": D("host_audit.py", "infinite", [("quality", "host_audit"), ("plan", "1.12")],
                    [T + "host_audit.py", "src/platform/PluginVST3.mm", "src/nodes/AudioPluginNode.cpp"],
                    ["python3 tools/re-survey/host_audit.py run --out ~/re-output/host-audit/results.md"]),
    "project-bench": D("project_bench.py", "infinite", [("quality", "project_bench")], [T + "project_bench.py"] + PATCH,
                       ["python3 tools/re-survey/project_bench.py infinite --out ~/re-output/project-bench/results.md"], quiet=True),
    "load-bench": D("load_bench.py", "infinite", [("quality", "load_bench"), ("plan", "2.24")],
                    [T + "load_bench.py", "src/audio/AudioEngine.cpp", "src/core/HeadlessJob.cpp"],
                    ["python3 tools/re-survey/load_bench.py run --track node:Analog+node:EQ --out ~/re-output/load-bench/analog-eq.md",
                     "python3 tools/re-survey/load_bench.py run --track node:Wavetable+node:Reverb --out ~/re-output/load-bench/wt-reverb.md"],
                    quiet=True),
    "av-bench": D("av_bench.py", "infinite", [("quality", "av_bench")], [T + "av_bench.py", "src/app/frame/CookFrame.cpp"],
                  ["python3 tools/re-survey/av_bench.py infinite"], quiet=True),
    "blender-bench": D("blender_bench.py", "infinite", [("quality", "blender_bench")], [T + "blender_bench.py"],
                       ["python3 tools/re-survey/blender_bench.py infinite"], quiet=True),
    "render-bench": D("render_bench.py", "infinite", [("quality", "render_bench"), ("plan", "3.5"), ("doc", "internal/plans/re-survey/dossiers/D.md", "render-bench")],
                      [T + "render_bench.py"], ["python3 tools/re-survey/render_bench.py infinite"], quiet=True),
    # One-off studies and kit inputs
    "app-bench": D("app_bench.py", "study", [("doc", "internal/plans/re-survey/PROTOCOL-load.md", "app_bench")]),
    "au-oop": D("au_oop.py", "study", [("doc", "internal/plans/re-survey/dossiers/A.md", "au_oop")]),
    "bounce-kit": D("bounce_kit.py", "study", [("doc", "internal/plans/re-survey/coverage.md", "bounce_kit")]),
    "latency": D("latency_probe.py", "study", [("doc", "internal/plans/re-survey/PROTOCOL-latency.md", "latency")]),
    "workflow-cost": D("workflow_cost.py", "study", [("plan", "2.1"), ("plan", "2.2")]),
    # Not data
    "bin": D("", "scratch"), "tmp": D("", "scratch"),
    "av-bench-inf.log": D("", "scratch"), "blender-bench-bl.log": D("", "scratch"), "blender-bench-inf.log": D("", "scratch"),
}

# User patches (sent with permission) live in the repo, not in ~/re-output; listed so they have an owner too.
REPO_SETS = {
    "community patches": {"path": "tools/website/community.json", "tool": "tools/website/community_patches.py",
                          "consumers": [("smoke", "python3 tools/website/community_patches.py --smoke"),
                                        ("doc", ".claude/agents/verify-gate.md", "community_patches.py --smoke")]},
}


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def registry_rows(text=None):
    """{metric id: source} from the registry."""
    out = {}
    for line in (read(REGISTRY) if text is None else text).split("\n"):
        c = line.split("\t")
        if len(c) >= 8 and c[0] and c[0] != "id" and not c[0].startswith("#"):
            out[c[0]] = c[7]
    return out


def consumer_ok(c, reg, plan, pdiff):
    """'' when the consumer exists, else why not."""
    kind = c[0]
    if kind == "quality":
        return "" if c[1] in reg.values() else "no registry row with source %s" % c[1]
    if kind == "spec":
        return "" if '"%s:' % c[1] in pdiff else "param_diff.py has no %s: source" % c[1]
    if kind == "plan":
        if not plan:
            return "plan file missing: %s" % os.path.relpath(PLAN, REPO)
        return "" if re.search(r"^\| %s \|" % re.escape(c[1]), plan, re.M) else "plan has no item %s" % c[1]
    if kind in ("doc", "tool"):
        text = read(os.path.join(REPO, c[1]))
        if not text:
            return "%s missing" % c[1]
        return "" if c[2] in text else "%s does not mention %s" % (c[1], c[2])
    if kind == "smoke":
        script = c[1].split()[1]
        return "" if c[1].split()[-1] in read(os.path.join(REPO, script)) else "%s has no %s" % (script, c[1].split()[-1])
    return "unknown consumer kind %s" % kind


def consumer_text(c):
    return {"quality": "scoreboard (%s)", "spec": "specs (%s)", "plan": "plan %s"}.get(c[0], "%s") % c[1]


def result_files(path):
    """Result files of a dataset, newest first as (mtime, path); kit inputs and caches left out."""
    if os.path.isfile(path):
        return [(os.path.getmtime(path), path)]
    out = []
    for root, dirs, files in os.walk(path):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            p = os.path.join(root, f)
            out.append((os.path.getmtime(p), p))
    return sorted(out, reverse=True)


def source_time(sources):
    """When the sources' content last changed.

    The older of the last commit touching them and the newest file time: a kit run before its own
    commit stays current (file time is the edit), and a fresh checkout does too (commit time is older).
    A file edited and not committed counts from its file time."""
    if not sources:
        return 0.0
    r = subprocess.run(["git", "-C", REPO, "log", "-1", "--format=%ct", "--"] + sources, capture_output=True, text=True)
    commit_t = float(r.stdout.strip()) if r.stdout.strip() else 0.0
    r = subprocess.run(["git", "-C", REPO, "ls-files", "--"] + sources, capture_output=True, text=True)
    file_t = max([os.path.getmtime(os.path.join(REPO, f)) for f in r.stdout.split("\n")
                  if f and os.path.exists(os.path.join(REPO, f))] or [0.0])
    r = subprocess.run(["git", "-C", REPO, "status", "--porcelain", "--"] + sources, capture_output=True, text=True)
    if r.stdout.strip() or not commit_t:
        return file_t
    return min(commit_t, file_t)


def quality_ids(files, limit=40):
    """QUALITY ids found in the newest result files (text files only)."""
    ids = set()
    for _, p in files[:limit]:
        if p.endswith((".md", ".txt", ".log")):
            ids.update(re.findall(r"^QUALITY id=(\S+)", read(p), re.M))
    return ids


def stale_reason(entry, files, src_t, reg):
    """'' when a dataset measured on Infinite is current, else why it is stale."""
    if entry["kind"] != "infinite":
        return ""
    if not files:
        return "no results"
    if src_t > files[0][0] + 1:
        return "sources changed after the newest result"
    wants = [c[1] for c in entry["consumers"] if c[0] == "quality"]
    if wants:
        ids = quality_ids(files)
        if not ids:
            return "results carry no QUALITY rows"
        unknown = sorted(i for i in ids if i not in reg)
        if unknown:
            return "QUALITY ids not in the registry: %s" % ", ".join(unknown[:4])
    return ""


def lint(ledger=None, root=None, verbose=True):
    """[(dataset, problem)] over the ledger and the disk."""
    ledger = LEDGER if ledger is None else ledger
    root = RE_OUTPUT if root is None else root
    reg, plan, pdiff = registry_rows(), read(PLAN), read(PARAM_DIFF)
    problems = []
    on_disk = sorted(os.listdir(root)) if os.path.isdir(root) else []
    for name in on_disk:
        if name not in ledger and not name.startswith("."):
            problems.append((name, "not in the ledger: add it to LEDGER with a consumer"))
    for name, e in sorted(ledger.items()):
        if e["kind"] == "scratch" or name not in on_disk:
            continue
        if e["kind"] == "superseded":
            if e["by"] not in on_disk:
                problems.append((name, "superseded by %s, which is missing" % e["by"]))
            continue
        if e["tool"] and not os.path.exists(os.path.join(HERE, e["tool"])):
            problems.append((name, "producer %s missing" % e["tool"]))
        bad = [consumer_ok(c, reg, plan, pdiff) for c in e["consumers"]]
        if not e["consumers"] or all(bad):
            problems.append((name, "no consumer" + (": " + "; ".join(b for b in bad if b) if bad else "")))
        else:
            problems += [(name, b) for b in bad if b]
        why = stale_reason(e, result_files(os.path.join(root, name)), source_time(e["sources"]), reg)
        if why:
            problems.append((name, "stale: %s%s. Re-run: %s" % (why, " (quiet machine)" if e["quiet"] else "", " && ".join(e["rerun"]))))
    for name, e in REPO_SETS.items():
        if not os.path.exists(os.path.join(REPO, e["path"])):
            problems.append((name, "%s missing" % e["path"]))
        problems += [(name, b) for b in (consumer_ok(c, reg, plan, pdiff) for c in e["consumers"]) if b]
    if verbose:
        for name, p in problems:
            print("FAIL  %s: %s" % (name, p))
        n = sum(1 for k, e in ledger.items() if e["kind"] not in ("scratch", "superseded") and k in on_disk)
        print("%s  %d datasets + %d repo sets: %d without a consumer, %d stale, %d other" % (
            "FAIL" if problems else "ok  ", n, len(REPO_SETS),
            sum(1 for _, p in problems if p.startswith("no consumer") or p.startswith("not in the ledger")),
            sum(1 for _, p in problems if p.startswith("stale")),
            sum(1 for _, p in problems if not p.startswith(("no consumer", "not in the ledger", "stale")))))
    return problems


def table():
    rows = []
    for name, e in sorted(LEDGER.items(), key=lambda kv: (kv[1]["kind"], kv[0])):
        if e["kind"] == "scratch":
            continue
        rows.append([name, e["kind"], e["tool"], ", ".join(consumer_text(c) for c in e["consumers"]) or "superseded by %s" % e["by"]])
    for name, e in REPO_SETS.items():
        rows.append([name, "repo", e["tool"], ", ".join(consumer_text(c) for c in e["consumers"])])
    return "# Data ledger\n\nMade by `tools/re-survey/data_ledger.py table`. Datasets live in `~/re-output`.\n\n" + \
        md_table(["Dataset", "Kind", "Producer", "Consumers"], rows) + "\n"


def changed(base):
    """[(dataset, entry, touched files)] for kits whose sources differ from base."""
    out = []
    for name, e in sorted(LEDGER.items()):
        if e["kind"] != "infinite" or not e["sources"]:
            continue
        r = subprocess.run(["git", "-C", REPO, "diff", "--name-only", base + "...HEAD", "--"] + e["sources"],
                           capture_output=True, text=True)
        files = [f for f in r.stdout.split("\n") if f]
        if files:
            out.append((name, e, files))
    return out


def self_test():
    t = SelfTest("data_ledger")
    reg = registry_rows("id\tp\ta\tu\tb\tt\tg\tsource\te\tw\nfoo.x\t\t\t\t\t\t\tfoo_kit\t\t\n")
    t.check(reg == {"foo.x": "foo_kit"}, "registry rows parse to id -> source")
    plan = "| 2.9 | Filter | ...\n| 12.9 | other |\n"
    t.check(consumer_ok(("quality", "foo_kit"), reg, plan, "") == "", "quality consumer found by registry source")
    t.check(consumer_ok(("quality", "bar_kit"), reg, plan, "") != "", "quality consumer with no registry row is refused")
    t.check(consumer_ok(("plan", "2.9"), reg, plan, "") == "" and consumer_ok(("plan", "9"), reg, plan, "") != "",
            "plan item matched as a whole row id")
    t.check(consumer_ok(("spec", "logic"), reg, plan, '"logic:Retro Synth"') == "", "spec consumer found in param_diff sources")
    t.check(consumer_ok(("doc", "docs/nope.md", "x"), reg, plan, "") != "", "missing doc is refused")
    e = D("foo.py", "infinite", [("quality", "foo_kit")], ["src"])
    t.check(stale_reason(e, [], 0, reg) == "no results", "an Infinite dataset with no results is stale")
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "foo", "kit"))
        p = os.path.join(d, "foo", "r.md")
        with open(p, "w") as f:
            f.write("x\n")
        with open(os.path.join(d, "foo", "kit", "in.wav"), "w") as f:
            f.write("x")
        os.utime(os.path.join(d, "foo", "kit", "in.wav"), (2e9, 2e9))
        files = result_files(os.path.join(d, "foo"))
        t.check([os.path.basename(q) for _, q in files] == ["r.md"], "kit inputs are not counted as results")
        now = files[0][0]
        t.check("QUALITY" in stale_reason(e, files, now - 10, reg), "results with no QUALITY rows are stale")
        with open(p, "w") as f:
            f.write('QUALITY id=foo.x subject="a" value=1\n')
        files = result_files(os.path.join(d, "foo"))
        t.check(stale_reason(e, files, files[0][0] - 10, reg) == "", "current results with known ids pass")
        t.check("sources changed" in stale_reason(e, files, files[0][0] + 60, reg), "sources newer than results are stale")
        with open(p, "w") as f:
            f.write('QUALITY id=foo.gone subject="a" value=1\n')
        t.check("not in the registry" in stale_reason(e, result_files(os.path.join(d, "foo")), 0, reg),
                "a QUALITY id the registry does not know is stale")
        t.check(stale_reason(D("x.py", "competitor"), [], 9e9, reg) == "", "competitor data never goes stale")
        os.makedirs(os.path.join(d, "stray"))
        probs = lint({"foo": D("", "scratch")}, d, verbose=False)
        t.check(any(n == "stray" and "not in the ledger" in why for n, why in probs), "a dataset missing from the ledger is reported")
    reg, plan, pdiff = registry_rows(), read(PLAN), read(PARAM_DIFF)
    bad = []
    for name, e in list(LEDGER.items()) + list(REPO_SETS.items()):
        tool = e["tool"]
        if tool and not os.path.exists(os.path.join(HERE, tool)) and not os.path.exists(os.path.join(REPO, tool)):
            bad.append("%s: producer %s" % (name, tool))
        bad += ["%s: %s" % (name, b) for b in (consumer_ok(c, reg, plan, pdiff) for c in e["consumers"]) if b]
        for s in e.get("sources", []):
            if not s.startswith(":(") and not os.path.exists(os.path.join(REPO, s)):
                bad.append("%s: source %s" % (name, s))
    t.check(not bad, "every producer, consumer and source in the ledger exists" + ("" if not bad else " (%s)" % "; ".join(bad[:4])))
    t.check(all(e["rerun"] for e in LEDGER.values() if e["kind"] == "infinite"), "every Infinite dataset names its re-run command")
    return t.done()


def main():
    if "--self-test" in sys.argv:
        return self_test()
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("lint")
    tp = sub.add_parser("table")
    tp.add_argument("--out")
    cp = sub.add_parser("changed")
    cp.add_argument("--base", default="main")
    args = ap.parse_args()
    if args.cmd == "lint":
        return 1 if lint() else 0
    if args.cmd == "table":
        text = table()
        if args.out:
            write_out(text, args.out)
        else:
            print(text)
        return 0
    if args.cmd == "changed":
        hits = changed(args.base)
        for name, e, files in hits:
            print("%s%s: %d source files changed (%s%s)" % (name, " [quiet machine]" if e["quiet"] else "", len(files),
                                                         ", ".join(files[:3]), ", ..." if len(files) > 3 else ""))
            for c in e["rerun"]:
                print("  " + c)
        print("%d kits to re-run against %s" % (len(hits), args.base))
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
