#!/usr/bin/env python3
"""
classify_dev_trajectory.py

The earlier session-linguistics pass (analyze_session_linguistics.py) classified turns
using a generic, imported taxonomy (Tulving's episodic/semantic/procedural memory model
+ unsupervised k-means topic clusters). That is a fine model of memory, but a poor model
of *this* codebase: it can't tell you "we spent September mostly fixing Field nodes" or
"UI/UX work is trending up" because it has no idea what a Field node, a scope, or a commit
type is.

This pass instead reuses categories this project ALREADY has, extracted elsewhere in the
semi-brain pipeline, instead of inventing new ones:

  - work_type / scope: the exact Conventional Commits vocabulary this repo's own commits
    use (see mine_git_history.py's CONVENTIONAL_PATTERN and scope heuristics: feat/fix/
    refactor/perf/chore/docs/test/build/style, arrange/audio/field/render3d/ui/core/...).
    Session turns are classified against the SAME vocabulary so a turn and a commit can be
    compared on equal footing.
  - node_category: derived straight from src/nodes/*.{h,cpp} via ast_symbol_graph.json's
    "nodes" subsystem. Every concrete node class (PitchBendNode, FieldSynthNode, ...) is
    grouped by source file into a category (Field nodes, Audio/DSP nodes, Geometry nodes,
    ...), then turn text is scanned for literal class-name mentions. This is exact-match
    against real identifiers, not a keyword guess.
  - problem_type / solution_type: grounded in this project's own recorded recurring bug
    patterns (see tools/semi-brain memory: parallel-array misalignment, non-source-over
    alpha blending, lane-unsafe constant folding) plus the standard categories that show up
    in this codebase's own fix commits (crash/segfault, build/tooling, platform-specific,
    visual/rendering, performance, logic).

It also buckets every turn by ISO week (turns already carry real timestamps) and computes
a simple least-squares trend slope per scope/work_type/node_category over the last 10
weeks, so "what have we been trending toward" is a measured fact, not a guess.
"""

import json
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

EXTRACTORS_OUT = Path(__file__).resolve().parent / "output"
AST_FILE = EXTRACTORS_OUT / "ast_symbol_graph.json"
SESSIONS_FILE = EXTRACTORS_OUT / "session_history_corpus.json"
ANTIGRAVITY_SESSIONS_FILE = EXTRACTORS_OUT / "antigravity_history_corpus.json"
OUT_FILE = EXTRACTORS_OUT / "dev_trajectory_corpus.json"

# --- Conventional Commits vocabulary, reused verbatim from mine_git_history.py -------------

WORK_TYPES = ["fix", "feat", "refactor", "perf", "chore", "docs", "test", "build", "style"]

WORK_TYPE_KEYWORDS = {
    "fix": ["fix", "fixed", "fixes", "bug", "crash", "segfault", "regression", "broken", "wrong", "incorrect"],
    "feat": ["add", "added", "new node", "implement", "feature", "support for"],
    "refactor": ["refactor", "redesign", "restructure", "rewrite", "clean up", "cleanup", "simplify"],
    "perf": ["perf", "performance", "speed", "optimiz", "slow", "latency", "faster"],
    "chore": ["chore", "housekeeping", "bump", "update dependency", "sync brain", "semi-brain"],
    "docs": ["docs", "documentation", "readme", "comment", "explain"],
    "test": ["test", "unit test", "verify", "verification", "regression test"],
    "build": ["cmake", "build system", "linker", "compile error", "build failure", "makefile"],
    "style": ["formatting", "lint", "style guide", "whitespace"],
}

# UI/UX and architecture aren't Conventional Commit "type"s in this repo, but the user
# explicitly wants them as first-class work_type buckets alongside the commit vocabulary
# above, so they're folded in as additional candidates.
WORK_TYPE_KEYWORDS["ui_ux"] = ["imgui", "knob", "panel", "canvas", "dark mode", "contrast", "layout",
                                "tooltip", "right-click", "context menu", "symmetry", "widget", "button"]
WORK_TYPE_KEYWORDS["architecture"] = ["invariant", "architecture", "interface", "abstraction", "subsystem",
                                       "design doc", "taxonomy", "data model", "ownership", "lifetime"]
WORK_TYPES = WORK_TYPES + ["ui_ux", "architecture"]

SCOPE_KEYWORDS = {
    "arrange": ["arrange", "timeline", "clip"],
    "audio": ["audio", "dsp", "vst", "synth", "oscillator", "filter", "envelope"],
    "field": ["field", "expr"],
    "render3d": ["render", "3d", "mesh", "geom"],
    "ui": ["ui", "knob", "panel", "imgui"],
    "prediction": ["prediction", "predictor", "modulator", "confidence"],
    "linux": ["linux", "xvfb", "wayland", "x11"],
    "windows": ["windows", "msvc", "win32"],
    "macos": ["macos", "cocoa", "objective-c", "coreaudio"],
    "semi-brain": ["semi-brain", "semi_brain", "knowledge_index", "training data"],
    "build": ["cmake", "build system", "makefile"],
    "core": [],  # fallback
}
SCOPE_PRIORITY = ["arrange", "field", "prediction", "render3d", "audio", "ui", "linux", "windows",
                   "macos", "semi-brain", "build", "core"]

PROBLEM_TYPE_KEYWORDS = {
    "crash_segfault": ["segfault", "crash", "null pointer", "nullptr", "sigsegv", "use-after-free",
                        "dangling pointer"],
    "visual_rendering": ["visual glitch", "rendering bug", "z-fighting", "alpha blend", "wrong color",
                          "not drawing", "invisible", "flicker"],
    "build_tooling": ["build failure", "compile error", "linker error", "cmake error", "won't build"],
    "platform_specific": ["only on linux", "only on windows", "only on mac", "platform-specific",
                            "windows only", "linux only"],
    "performance": ["too slow", "performance regression", "high cpu", "memory blowup", "leak"],
    "data_misalignment": ["parallel array", "index mismatch", "off by one", "misaligned"],
    "logic_bug": ["wrong result", "incorrect behavior", "edge case", "logic error", "doesn't work as expected"],
}

SOLUTION_TYPE_KEYWORDS = {
    "guard_invariant": ["added a guard", "invariant", "assert", "validation check", "bounds check"],
    "refactor_fix": ["refactored", "restructured", "rewrote", "simplified"],
    "algorithm_change": ["changed the algorithm", "different approach", "new formula", "changed the math"],
    "config_build_fix": ["cmake fix", "build flag", "compiler flag", "linker flag"],
    "revert": ["reverted", "rolled back", "undid"],
    "root_cause_fix": ["root cause", "turns out", "the issue was", "the reason was", "traced it to"],
}


def load_json(path, default):
    if path.exists():
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return default


def build_node_category_map():
    """Map every concrete node class name (from ast_symbol_graph.json) to a human category,
    grouped by the src/nodes/*.{h,cpp} file it lives in. Grounded in real identifiers, not
    guessed keywords."""
    ast_data = load_json(AST_FILE, {})
    symbols = ast_data.get("symbols", {})

    FILE_TO_CATEGORY = {
        "NoteNodes": "Note/MIDI nodes", "MidiNodes": "Note/MIDI nodes",
        "FieldElementNode": "Field nodes", "FieldPrimitiveNode": "Field nodes",
        "FieldPixelNode": "Field nodes", "FieldGraphNode": "Field nodes",
        "FieldSampleNode": "Field nodes", "FieldSynthNode": "Field nodes",
        "GeometryOpNodes": "Geometry/3D nodes", "Geometry3DNodes": "Geometry/3D nodes",
        "PointDistributionNodes": "Geometry/3D nodes", "MolderNode": "Geometry/3D nodes",
        "GrainMolderNode": "Geometry/3D nodes", "Switcher3DNode": "Geometry/3D nodes",
        "DrawNode": "Geometry/3D nodes",
        "AudioNodes": "Audio/DSP nodes", "AudioEffectNode": "Audio/DSP nodes",
        "AudioPluginNode": "Audio/DSP nodes", "SamplerNode": "Audio/DSP nodes",
        "GranularNode": "Audio/DSP nodes", "PaulStretchNode": "Audio/DSP nodes",
        "SlicerNode": "Audio/DSP nodes", "WavetableSynthCore": "Audio/DSP nodes",
        "WaveTerrainNode": "Audio/DSP nodes", "DrumSequencerNode": "Audio/DSP nodes",
        "ImageSpectralSynthNode": "Audio/DSP nodes", "FeedbackNodes": "Audio/DSP nodes",
        "AudioDisplacementNode": "Audio/DSP nodes",
        "ModulatorNodes": "Modulator/Prediction nodes",
        "AnalyzeNodes": "Analysis nodes",
        "UtilityNodes": "Utility/Flow nodes", "MacroNodes": "Utility/Flow nodes",
        "OutputNode": "Utility/Flow nodes", "EquationNode": "Utility/Flow nodes",
        "SimulationNodes": "Simulation nodes",
        "VideoSourceNode": "Visual/Color nodes", "PaletteNode": "Visual/Color nodes",
        "AudioColorRampNode": "Visual/Color nodes", "MetallicNode": "Visual/Color nodes",
        "GenerativeNodes": "Generative nodes",
    }

    class_to_category = {}
    for name, meta in symbols.items():
        if meta.get("subsystem") != "nodes":
            continue
        if "::" in name:
            continue
        if not name.endswith("Node") or len(name) < 6:
            continue
        file_stem = Path(meta.get("file", "")).stem
        category = FILE_TO_CATEGORY.get(file_stem)
        if category:
            class_to_category[name] = category

    # Sort longest-first so e.g. "AudioMidiNotesNode" is tried before "MidiNotesNode" style overlaps.
    ordered_names = sorted(class_to_category.keys(), key=len, reverse=True)
    return ordered_names, class_to_category


def classify_scope(text_lower):
    for scope in SCOPE_PRIORITY:
        kws = SCOPE_KEYWORDS[scope]
        if any(kw in text_lower for kw in kws):
            return scope
    return "core"


def classify_multi(text_lower, keyword_map):
    scores = {}
    for label, kws in keyword_map.items():
        hits = sum(1 for kw in kws if kw in text_lower)
        if hits:
            scores[label] = hits
    if not scores:
        return None
    return max(scores.items(), key=lambda kv: kv[1])[0]


def node_categories_mentioned(text, ordered_class_names, class_to_category):
    found = Counter()
    for cname in ordered_class_names:
        if cname in text:
            found[class_to_category[cname]] += 1
    return found


def iso_week(ts_str):
    try:
        dt = datetime.fromisoformat(ts_str.replace("Z", "+00:00"))
    except ValueError:
        return None
    dt = dt.astimezone(timezone.utc)
    year, week, _ = dt.isocalendar()
    return f"{year}-W{week:02d}"


def trend_slope(weekly_counts_ordered):
    """Simple least-squares slope over the ordered weekly counts. Positive = rising."""
    n = len(weekly_counts_ordered)
    if n < 2:
        return 0.0
    xs = list(range(n))
    mean_x = sum(xs) / n
    mean_y = sum(weekly_counts_ordered) / n
    num = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, weekly_counts_ordered))
    den = sum((x - mean_x) ** 2 for x in xs)
    return num / den if den else 0.0


def main():
    print("Loading node category map from ast_symbol_graph.json...", flush=True)
    ordered_class_names, class_to_category = build_node_category_map()
    print(f"  {len(ordered_class_names)} node classes mapped into {len(set(class_to_category.values()))} categories", flush=True)

    turns = load_json(SESSIONS_FILE, [])
    turns.extend(load_json(ANTIGRAVITY_SESSIONS_FILE, []))
    print(f"Classifying {len(turns)} session turns (Claude Code + Antigravity) against codebase-grounded taxonomy...", flush=True)

    weekly_scope = defaultdict(lambda: defaultdict(int))       # week -> scope -> count
    weekly_work_type = defaultdict(lambda: defaultdict(int))   # week -> work_type -> count
    weekly_node_cat = defaultdict(lambda: defaultdict(int))    # week -> node_category -> count
    scope_totals = Counter()
    work_type_totals = Counter()
    node_cat_totals = Counter()
    problem_type_totals = Counter()
    solution_type_totals = Counter()
    all_weeks_seen = set()

    classified_turns = []
    for turn in turns:
        user_text = turn.get("user_text", "") or ""
        assistant_text = turn.get("assistant_text", "") or ""
        if not user_text.strip():
            continue
        combined = f"{user_text}\n{assistant_text}"
        combined_lower = combined.lower()

        scope = classify_scope(combined_lower)
        work_type = classify_multi(combined_lower, WORK_TYPE_KEYWORDS) or "feat"
        node_hits = node_categories_mentioned(combined, ordered_class_names, class_to_category)
        top_node_cat = node_hits.most_common(1)[0][0] if node_hits else None

        problem_type = classify_multi(combined_lower, PROBLEM_TYPE_KEYWORDS)
        solution_type = classify_multi(combined_lower, SOLUTION_TYPE_KEYWORDS)

        week = iso_week(turn.get("timestamp", ""))

        scope_totals[scope] += 1
        work_type_totals[work_type] += 1
        if top_node_cat:
            node_cat_totals[top_node_cat] += 1
        if problem_type:
            problem_type_totals[problem_type] += 1
        if solution_type:
            solution_type_totals[solution_type] += 1

        if week:
            all_weeks_seen.add(week)
            weekly_scope[week][scope] += 1
            weekly_work_type[week][work_type] += 1
            if top_node_cat:
                weekly_node_cat[week][top_node_cat] += 1

        classified_turns.append({
            "session_id": turn.get("session_id", ""),
            "timestamp": turn.get("timestamp", ""),
            "source_tool": turn.get("source_tool", "claude_code"),
            "week": week,
            "scope": scope,
            "work_type": work_type,
            "node_category": top_node_cat,
            "problem_type": problem_type,
            "solution_type": solution_type,
        })

    weeks_ordered = sorted(all_weeks_seen)
    # Drop the current (in-progress) ISO week from trend math - it's always artificially low
    # and would bias every slope downward at the tail for no real reason.
    current_week = iso_week(datetime.now(timezone.utc).isoformat())
    complete_weeks = [w for w in weeks_ordered if w != current_week]
    recent_weeks = complete_weeks[-10:] if len(complete_weeks) > 10 else complete_weeks

    def series_for(weekly_dict, label):
        return [weekly_dict[w].get(label, 0) for w in recent_weeks]

    scope_trends = {}
    for scope in scope_totals:
        series = series_for(weekly_scope, scope)
        scope_trends[scope] = {"weekly_counts": series, "slope": round(trend_slope(series), 3)}

    work_type_trends = {}
    for wt in work_type_totals:
        series = series_for(weekly_work_type, wt)
        work_type_trends[wt] = {"weekly_counts": series, "slope": round(trend_slope(series), 3)}

    node_cat_trends = {}
    for nc in node_cat_totals:
        series = series_for(weekly_node_cat, nc)
        node_cat_trends[nc] = {"weekly_counts": series, "slope": round(trend_slope(series), 3)}

    output = {
        "recent_weeks": recent_weeks,
        "scope_totals": dict(scope_totals.most_common()),
        "work_type_totals": dict(work_type_totals.most_common()),
        "node_category_totals": dict(node_cat_totals.most_common()),
        "problem_type_totals": dict(problem_type_totals.most_common()),
        "solution_type_totals": dict(solution_type_totals.most_common()),
        "scope_trends": scope_trends,
        "work_type_trends": work_type_trends,
        "node_category_trends": node_cat_trends,
        "turns": classified_turns,
    }

    OUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_FILE, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=1)

    print(f"Scope totals: {output['scope_totals']}")
    print(f"Work type totals: {output['work_type_totals']}")
    print(f"Node category totals: {output['node_category_totals']}")
    print(f"Problem type totals: {output['problem_type_totals']}")
    print(f"Solution type totals: {output['solution_type_totals']}")
    rising = sorted(scope_trends.items(), key=lambda kv: -kv[1]["slope"])[:3]
    falling = sorted(scope_trends.items(), key=lambda kv: kv[1]["slope"])[:3]
    print(f"Rising scopes (last {len(recent_weeks)} weeks): {rising}")
    print(f"Falling scopes (last {len(recent_weeks)} weeks): {falling}")
    print(f"Wrote trajectory analysis to {OUT_FILE}")


if __name__ == "__main__":
    main()
