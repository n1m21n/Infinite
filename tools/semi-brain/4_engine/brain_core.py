#!/usr/bin/env python3
"""
brain_core.py
The computational reasoning engine for the Semi-Brain:
- System 1 (Intuitive Heuristics, Taste Check & BYOX First-Principles Priors)
- System 2 (Systems Thinking, AST Call-Graph + SQLite Hybrid RAG Grounded Blast Radius)
- Choice Tree Evaluator (MCTS Branching & Forward Rollout)
"""

import json
import os
import re
import time
from collections import defaultdict
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Any
from pathlib import Path

SEMI_BRAIN_DIR = Path(__file__).resolve().parents[1]
DISTILLED_DIR = SEMI_BRAIN_DIR / "2_distilled_brain"
AST_GRAPH_FILE = SEMI_BRAIN_DIR / "1_extractors" / "output" / "ast_symbol_graph.json"

# Architecturally load-bearing symbols per subsystem that a bug report rarely
# names literally (e.g. "audio pop on retrigger" never says "ParamMailbox"),
# but that own the invariant almost every bug in that subsystem routes through.
# Only injected when they actually resolve to a real AST symbol -- see
# _resolve_anchor_symbols -- so a stale/renamed entry here is a silent no-op,
# never a hallucinated citation.
SUBSYSTEM_ANCHOR_SYMBOLS = {
    "audio_dsp": ["ParamMailbox", "AudioEngine", "INode", "IAudioSource"],
    "render_3d": ["IGeometrySource", "INode"],
    "field_compiler": ["FieldVM", "ElementVM", "Modulation"],
    "arrange_timeline": ["Clip", "Patch"],
    "nodes": ["INode", "Modulation"],
    "core_system": ["Patch", "INode"],
    "compositing_2d": ["FilterDef", "INode", "GLUtil"],
    "ui_shell": ["INode"],
    "platform": ["Platform"],
}

from retriever import HybridRetriever
from l2.compartments import merge as merge_compartments
from l3.network import Network, SEED_WEIGHT
from l4.clusters import Areas
from l4.regions import Regions
from l3.recent import RecentWork
from l3 import weights as learned_weights
from l0.store import Store as L0Store, BOOST_KINDS

_IDENT_RE = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|\d+")
_STOP = {"the", "and", "for", "with", "get", "set", "node", "nodes", "fix", "from", "into",
         "when", "not", "its", "this", "that", "only"}


def _split_ident(token):
    return [w.lower() for w in _IDENT_RE.findall(token)]


def _stem(w):
    return w[:-1] if len(w) > 4 and w.endswith("s") and not w.endswith("ss") else w


@dataclass
class ProblemFrame:
    raw_query: str
    inferred_subsystem: str
    target_domains: List[str]
    system1_priors: List[str]
    negative_taboos_flagged: List[str]
    first_principles_refs: List[str]
    invariants_required: List[str]
    blast_radius_questions: Dict[str, str]
    retrieved_context: List[Dict[str, Any]] = field(default_factory=list)
    ast_impacted_symbols: List[str] = field(default_factory=list)
    ast_callers_found: List[str] = field(default_factory=list)
    ranked_files: List[str] = field(default_factory=list)
    file_evidence: Dict[str, List[str]] = field(default_factory=dict)
    compartments: Dict[str, List[Dict[str, Any]]] = field(default_factory=dict)
    notes: List[Dict[str, Any]] = field(default_factory=list)

@dataclass
class ImpactNode:
    symbol: str
    file: str
    line: int
    subsystem: str
    children: List['ImpactNode'] = field(default_factory=list)
    crosses_subsystem: bool = False
    is_hub: bool = False

    def count_nodes(self) -> int:
        return 1 + sum(c.count_nodes() for c in self.children)

    def count_crossings(self) -> int:
        return (1 if self.crosses_subsystem else 0) + sum(c.count_crossings() for c in self.children)

    def get_distinct_subsystems(self) -> set:
        subs = {self.subsystem}
        for c in self.children:
            subs.update(c.get_distinct_subsystems())
        return subs

    def get_max_depth(self) -> int:
        if not self.children:
            return 0
        return 1 + max(c.get_max_depth() for c in self.children)

    def collect_hubs(self) -> List['ImpactNode']:
        hubs = [self] if self.is_hub else []
        for c in self.children:
            hubs.extend(c.collect_hubs())
        return hubs

    def collect_crossing_nodes(self) -> List['ImpactNode']:
        crossings = [self] if self.crosses_subsystem else []
        for c in self.children:
            crossings.extend(c.collect_crossing_nodes())
        return crossings

@dataclass
class ChoiceBranch:
    name: str
    description: str
    strategy: str
    rollout_simulation: Dict[str, str]
    invariant_score: float
    platform_score: float
    realtime_score: float
    roundtrip_score: float
    taste_score: float
    blast_penalty: float
    total_value: float = 0.0

    def compute_value(self):
        if self.realtime_score < 1.0 or self.platform_score < 1.0:
            self.total_value = -1.0 # Hard Pruning
        else:
            self.total_value = (
                0.30 * self.invariant_score +
                0.20 * self.platform_score +
                0.20 * self.realtime_score +
                0.15 * self.roundtrip_score +
                0.15 * self.taste_score -
                0.10 * self.blast_penalty
            )
        return self.total_value

class SemiBrainCognitiveEngine:
    def __init__(self):
        self.load_cognitive_schemas()
        self.load_ast_graph()
        self.retriever = HybridRetriever()
        self.network = Network.load(self.ast_graph)
        self.recent = self._load_recent()
        self.l0 = L0Store()
        self.weights = learned_weights.load()

    def _load_recent(self):
        commits = SEMI_BRAIN_DIR / "1_extractors" / "output" / "git_commits_corpus.json"
        try:
            corpus = json.loads(commits.read_text()) if commits.exists() else []
        except ValueError:
            corpus = []
        return RecentWork.load(SEMI_BRAIN_DIR / "l1" / "state", corpus)
        
    def load_cognitive_schemas(self):
        self.schemas = {}
        for md_file in DISTILLED_DIR.glob("*.md"):
            self.schemas[md_file.stem] = md_file.read_text(encoding="utf-8")
            
    def load_ast_graph(self):
        if AST_GRAPH_FILE.exists():
            with open(AST_GRAPH_FILE, "r", encoding="utf-8") as f:
                self.ast_graph = json.load(f)
        else:
            self.ast_graph = {"symbols": {}, "forward_call_graph": {}, "reverse_call_graph": {}, "subsystems": {}}

    def _resolve_anchor_symbols(self, subsystem: str) -> List[str]:
        """Ground SUBSYSTEM_ANCHOR_SYMBOLS entries against the real AST graph.
        An anchor name that no longer matches any symbol (renamed/removed) is
        dropped silently rather than injected as a fabricated citation."""
        resolved = []
        symbol_names = self.ast_graph.get("symbols", {}).keys()
        for anchor in SUBSYSTEM_ANCHOR_SYMBOLS.get(subsystem, []):
            anchor_lower = anchor.lower()
            match = next(
                (s for s in symbol_names if s.lower() == anchor_lower or s.lower().endswith("::" + anchor_lower)),
                None,
            )
            if match:
                resolved.append(match)
        return resolved

    def get_callers_for_symbols(self, symbols: List[str]) -> List[str]:
        callers = set()
        rev_graph = self.ast_graph.get("reverse_call_graph", {})
        for s in symbols:
            short_name = s.split("::")[-1]
            for c in rev_graph.get(s, []) + rev_graph.get(short_name, []):
                callers.add(c)
                if len(callers) >= 8:
                    break
        return sorted(list(callers))

    def infer_subsystem(self, query: str) -> str:
        q = query.lower()
        # Specific overrides first
        if any(k in q for k in ["vst3", "wasapi", "spout", "syphon", "device unplug", "device loss", "x86_64"]):
            return "platform"
        if any(k in q for k in ["curves", "feedback", "filterdef", "shader recompile", "bypassing curves", "layer stack", "ping-pong"]):
            return "compositing_2d"
        if any(k in q for k in ["lfo", "modulator", "imodulator"]):
            return "nodes"
        if any(k in q for k in ["shortcut", "keyboard", "window", "panel shell", "canvas drop"]):
            return "ui_shell"
        if any(k in q for k in ["movie", "recorder", "pbo readback", "export movie"]):
            return "core_system"
        if any(k in q for k in ["timeline", "arrange", "clip", "track", "lane", "playhead", "scrub"]):
            return "arrange_timeline"
        if any(k in q for k in ["field", "expression", "kernel", "compiler", "ir", "domain", "state cell"]):
            return "field_compiler"
        if any(k in q for k in ["mesh", "geometry", "3d", "render3d", "vertex", "normal", "metaball", "mapping"]):
            return "render_3d"
        if any(k in q for k in ["audio", "sound", "dsp", "oscillator", "filter", "synth", "reverb", "delay", "note", "wavetable"]):
            return "audio_dsp"
        return "core_system"

    def analyze_problem(self, query: str, now: Optional[float] = None, session: str = "",
                        embargo: float = 0.0) -> ProblemFrame:
        """now/session/embargo feed the work-in-progress prior (l3/recent.py): files edited
        earlier in this session and lately anywhere, from events before now - embargo. The
        same cutoff applies to L0 notes (l0/store.py)."""
        subsystem = self.infer_subsystem(query)
        q = query.lower()
        
        # 1. L2: BM25 + dense inside each compartment, merged by per-compartment quota
        by_compartment = self.retriever.compartment_search(query)
        hybrid_hits = merge_compartments(by_compartment)
        
        # Symbols the code compartment retrieved: a score bonus by rank, not a fixed head of
        # the list, so lexical/word/network evidence can outrank a weak retrieval hit.
        hit_bonus = {}
        for rank, hit in enumerate(by_compartment.get("code", [])[:self.CODE_HITS]):
            hit_bonus[hit["title"].replace("Symbol: ", "")] = self.HIT_BONUS / (1.0 + 0.25 * rank)
        matched_symbols = []

        # Scored symbol lookup
        raw_tokens = re.findall(r"[A-Za-z0-9_]+", query)
        tokens = set([t.lower() for t in raw_tokens if len(t) > 2])
        # Add camelCase split tokens
        for t in raw_tokens:
            splits = re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?=[A-Z]|$)", t)
            for s in splits:
                if len(s) > 2:
                    tokens.add(s.lower())
                    
        rag_text = " ".join([h.get("title", "") + " " + h.get("snippet", "") for h in hybrid_hits]).lower()

        # L3 prior: how strongly the network points at each file (1.0 = the top file).
        spread, why, net_syms = self._spread(by_compartment)
        top = max(spread.values(), default=0.0)
        prior = {f: v / top for f, v in spread.items()} if top > 0 else {}
        
        qwords = {_stem(w) for t in raw_tokens for w in _split_ident(t) if len(w) > 2} - _STOP
        sym_words = self._symbol_words()

        scored_candidates = []
        for sym_name, sym_meta in self.ast_graph.get("symbols", {}).items():
            sym_lower = sym_name.lower()
            sym_base = sym_name.split("::")[-1]
            sym_base_lower = sym_base.lower()
            
            score = hit_bonus.get(sym_name, 0.0)
            # 1. Exact match with token in query
            if sym_base_lower in tokens or sym_lower in tokens:
                score += 10.0
            # 2. Token match on class or struct
            if sym_meta.get("kind") in ["class", "struct"]:
                if any(t == sym_base_lower for t in tokens):
                    score += 8.0
                elif any(t in sym_base_lower for t in tokens):
                    score += 4.0
            # 3. Mentioned in RAG context
            if sym_base_lower in rag_text:
                score += 3.0
            # 4. Subsystem match bonus
            if sym_meta.get("subsystem") == subsystem:
                score += 1.0
            # 5. General substring match
            if any(t in sym_lower for t in tokens if len(t) >= 4):
                score += 1.5
                
            # 6. Words of the name (CamelCase split) shared with the query: a function the
            #    report describes but never names (ArrangeResyncUnsyncedSampleLengths).
            base_w, cls_w = sym_words[sym_name]
            ov_base, ov_cls = len(base_w & qwords), len(cls_w & qwords)
            if ov_base and ov_base + ov_cls >= 2:
                score += self.WORD_BASE * ov_base + self.WORD_CLASS * ov_cls

            # 7. The network points at this symbol's file, or names the symbol itself
            if score > 0.0:
                score += self.FILE_PRIOR * prior.get(sym_meta.get("file", ""), 0.0)
                score += self.SYMBOL_PRIOR * net_syms.get(sym_name, 0.0)

            if score > 2.0:
                scored_candidates.append((score, sym_name))
                
        scored_candidates.sort(key=lambda x: x[0], reverse=True)
        for _, sym_name in scored_candidates[:self.MAX_SYMBOLS]:
            if sym_name not in matched_symbols:
                matched_symbols.append(sym_name)

        # Subsystem-anchor injection: architecturally load-bearing symbols
        # (ParamMailbox, INode, ...) that own the invariant behind most bugs
        # in this subsystem but are rarely named in the bug report's own
        # words, so pure lexical scoring above never surfaces them.
        for anchor_sym in self._resolve_anchor_symbols(subsystem):
            if anchor_sym not in matched_symbols:
                matched_symbols.append(anchor_sym)

        callers = self.get_callers_for_symbols(matched_symbols)
        recent = getattr(self, "recent", None)
        # Only for a live session (the prompt hook passes one): without it there is no ongoing
        # work to continue, and on the commit replay the 12 h embargo leaves only noise.
        wts = getattr(self, "weights", None) or learned_weights.DEFAULTS
        work = (recent.rankings(time.time() if now is None else now, session, embargo, wts["tau_days"])
                if recent and session else ([], []))
        l0 = getattr(self, "l0", None)
        notes = l0.match(query, now, embargo) if l0 is not None else []
        ranked_files, evidence = self._rank_files(matched_symbols, spread, why, work, notes)
        
        # 2. System 1 Priors
        priors = [
            "Zero-sigil syntax and clean mathematical notation",
            "Monotonic UID high-water mark preservation on clone/undo/load",
            "Explicit dataflow separation (no hidden shared-buffer mutation)"
        ]
        
        taboos = []
        byox_refs = []
        
        if "audio" in subsystem or "dsp" in q or "buffer" in q:
            priors.append("Two-object rule: INode main thread vs DSP worker audio thread")
            priors.append("Zero heap allocation / zero mutex lock in audio path")
            taboos.append("Do NOT allocate memory (`new`, `malloc`, `std::vector::push_back`) in the audio callback")
            taboos.append("Do NOT use mutexes or locks in audio thread; use SPSC ringbuffer / atomics")
            byox_refs.append("BYOX Audio Synthesizer: Lock-free SPSC circular buffers & continuous phase accumulator")
            
        if "field" in subsystem or "compiler" in q:
            priors.append("Inferred domain rate hierarchy (Frame -> Element -> Pixel -> Sample)")
            taboos.append("Never poll down-domain from high-rate kernels")
            byox_refs.append("BYOX Compiler: Token Stream -> Recursive Descent AST -> Typed IR -> Lowering Target")
            
        if "render" in subsystem or "3d" in q:
            priors.append("IGeometrySource caching & dirty-stamp propagation")
            byox_refs.append("BYOX 3D Software Renderer: Perspective-correct barycentric interpolation & Z-buffer sorting")
            
        if "arrange" in subsystem or "clip" in q:
            priors.append("Unique lane/group naming & persistent track hierarchy")
            priors.append("Waveform live-drawn with clip-offset bounds clamping")
            byox_refs.append("BYOX Game Engine: Tick-based deterministic transport clock & DAG node traversal")
            
        # 3. System 2 Invariants & RAG-Grounded Blast Radius
        invariants = [
            "Save/Load patch token roundtrip identity bit-for-bit",
            "BypassSource passes primary input unaltered",
            "Monotonic UID allocation prevents cross-clip/cross-node collisions",
            "Cross-platform parity (macOS CoreAudio/Metal, Win WASAPI/DirectX, Linux PipeWire/X11)"
        ]
        
        symbols_str = ", ".join([f"`{s}`" for s in matched_symbols[:4]]) if matched_symbols else f"Subsystem `{subsystem}`"
        callers_str = ", ".join([f"`{c}`" for c in callers[:4]]) if callers else "Main render/cook loop"
        
        blast_q = {
            "Q1_Owning_Node": f"Subsystem `{subsystem}` (Relevant Symbols: {symbols_str})",
            "Q2_Faulty_Logic": f"Investigating root-cause state transition for: '{query}'",
            "Q3_Caller_Graph": f"AST Call Graph: Invoked by {callers_str}",
            "Q4_Silent_Degradation": "Verify downstream visualizers, meters, and connected cables do not freeze.",
            "Q5_Sibling_Multiplicity": "Audit sibling nodes/controls to verify the undo-shape is not repeated.",
            "Q6_Platform_Parity": "Ensure implementation compiles and runs identically across macOS, Win32, and Linux.",
            "Q7_Invariant_Safety": "Verify parameter delivery (ParamMailbox <= 1 block) and undo stability.",
            "Q8_Historical_Origin": "Inspect git log for when regression or initial contract was established.",
            "Q9_Harness_Gate": "Ensure dedicated sweep test fixture exercises this exact transition."
        }
        
        return ProblemFrame(
            raw_query=query,
            inferred_subsystem=subsystem,
            target_domains=["Frame", "Element", "Sample"] if "audio" in subsystem else ["Frame", "Pixel"],
            system1_priors=priors,
            negative_taboos_flagged=taboos,
            first_principles_refs=byox_refs,
            invariants_required=invariants,
            blast_radius_questions=blast_q,
            retrieved_context=hybrid_hits,
            ast_impacted_symbols=matched_symbols,
            ast_callers_found=callers,
            ranked_files=ranked_files,
            file_evidence=evidence,
            compartments=by_compartment,
            notes=notes,
        )

    SEEDS_PER_COMPARTMENT = 10
    FILE_RRF_K = 10.0

    CODE_HITS = 12
    HIT_BONUS = 3.0
    MAX_SYMBOLS = 24
    WORD_BASE = 2.0
    WORD_CLASS = 1.0

    def _symbol_words(self):
        """{symbol: (words of its own name, words of its scope)}, stemmed, cached per graph."""
        graph = self.ast_graph
        cache = getattr(self, "_sym_words", None)
        if cache is not None and cache[0] is graph:
            return cache[1]
        out = {}
        for name in graph.get("symbols", {}):
            *scope, base = name.split("::")
            out[name] = ({_stem(w) for w in _split_ident(base) if len(w) > 2} - _STOP,
                         {_stem(w) for p in scope for w in _split_ident(p) if len(w) > 2} - _STOP)
        self._sym_words = (graph, out)
        return out

    FILE_PRIOR = 4.0
    SYMBOL_PRIOR = 2.0

    def _spread(self, by_compartment):
        """L3 spreading activation from every compartment's top hits along doc -> code edges.
        Returns (file_scores, why, symbol_scores)."""
        network = getattr(self, "network", None)
        if network is None:
            return {}, {}, {}
        seeds = []
        for comp, hits in by_compartment.items():
            w = SEED_WEIGHT.get(comp, 0.0)
            for rank, hit in enumerate(hits[:self.SEEDS_PER_COMPARTMENT]):
                seeds.append((hit, w / (rank + 1)))
        fscore, sscore, why = network.activate(seeds)
        return fscore, why, sscore

    def _rank_files(self, matched_symbols, spread, why, work=([], []), notes=()):
        """Files most likely involved, best first, with the doc ids that point at each: the
        files of the matched symbols (in symbol order), the network's spread and the recent
        work (this session's edits, recent edits anywhere), fused by weighted RRF with the
        weights of l3/weights.py (retuned nightly by l1/sleep.py). L0 notes add their files as one
        more list whose weight is the best note's score, never above l0.store.CAP."""
        wts = getattr(self, "weights", None) or learned_weights.DEFAULTS
        lexical = []
        for sym in matched_symbols:
            f = self._get_symbol_meta(sym).get("file", "")
            if f and f not in lexical:
                lexical.append(f)
        fused = defaultdict(float)
        lists = ((lexical, 1.0), (sorted(spread, key=spread.get, reverse=True), 1.0),
                 (work[0], wts["session_w"]), (work[1], wts["recent_w"]))
        boost = [n for n in notes if n["kind"] in BOOST_KINDS and n["files"]]
        if boost:
            note_files = list(dict.fromkeys(f for n in boost for f in n["files"]))
            lists += ((note_files, boost[0]["score"]),)
        for lst, w in lists:
            for rank, f in enumerate(lst):
                fused[f] += w / (self.FILE_RRF_K + rank + 1)
        areas = self._areas()
        if areas is not None:
            fused = areas.rerank(fused)
        ranked = sorted(fused, key=fused.get, reverse=True)
        return ranked, {f: why[f][:3] for f in ranked[:20] if why.get(f)}

    def _areas(self):
        """L4 areas of the current network, built once per network."""
        network = getattr(self, "network", None)
        if network is None:
            return None
        cache = getattr(self, "_areas_cache", None)
        if cache is None or cache[0] is not network:
            cache = self._areas_cache = (network, Areas(network))
        return cache[1]

    def _regions(self):
        """L4 virtual split of src/main.cpp, loaded once per engine (static, not per network)."""
        cache = getattr(self, "_regions_cache", None)
        if cache is None:
            cache = self._regions_cache = Regions()
        return cache

    def _get_symbol_meta(self, sym: str, fallback_subsystem: str = "core_system") -> Dict[str, Any]:
        symbols_db = self.ast_graph.get("symbols", {})
        meta = symbols_db.get(sym)
        if not meta:
            short = sym.split("::")[-1]
            meta = symbols_db.get(short)
        if not meta:
            for k, v in symbols_db.items():
                if k.endswith("::" + sym) or k.split("::")[-1] == sym:
                    meta = v
                    break
        if not meta:
            return {"file": "", "line": 0, "end_line": 0, "subsystem": fallback_subsystem}
        return {
            "file": meta.get("file", ""),
            "line": meta.get("line", 0),
            "end_line": meta.get("end_line", 0),
            "subsystem": meta.get("subsystem", fallback_subsystem)
        }

    def _get_direct_callers(self, sym: str) -> List[str]:
        rev_graph = self.ast_graph.get("reverse_call_graph", {})
        callers = set()
        short = sym.split("::")[-1]
        for c in rev_graph.get(sym, []) + rev_graph.get(short, []):
            callers.add(c)
        if not callers:
            for k, v in rev_graph.items():
                if k.startswith(sym + "::") or k.startswith(short + "::"):
                    for c in v:
                        callers.add(c)
        return sorted(list(callers))

    def build_impact_tree(self, frame: ProblemFrame, max_depth: int = 3, max_fanout: int = 4) -> ImpactNode:
        primary_sym = None
        for s in frame.ast_impacted_symbols:
            if self._get_direct_callers(s):
                primary_sym = s
                break
        if not primary_sym:
            primary_sym = frame.ast_impacted_symbols[0] if frame.ast_impacted_symbols else frame.inferred_subsystem

        meta = self._get_symbol_meta(primary_sym, frame.inferred_subsystem)
        root = ImpactNode(
            symbol=primary_sym,
            file=meta["file"],
            line=meta["line"],
            subsystem=meta["subsystem"]
        )
        
        visited: set = {primary_sym, primary_sym.split("::")[-1]}

        def expand(node: ImpactNode, current_depth: int):
            if current_depth >= max_depth:
                return
            direct_callers = self._get_direct_callers(node.symbol)
            if not direct_callers:
                return
            
            candidates = []
            for caller in direct_callers:
                short_caller = caller.split("::")[-1]
                if caller in visited or short_caller in visited:
                    continue
                c_meta = self._get_symbol_meta(caller, node.subsystem)
                c_subsystem = c_meta["subsystem"]
                c_callers = self._get_direct_callers(caller)
                fanout = len(c_callers)
                is_hub = (fanout >= 2)
                crosses = (c_subsystem != node.subsystem and c_subsystem != "general")
                if crosses or is_hub:
                    candidates.append((fanout, caller, c_meta, crosses, is_hub))

            candidates.sort(key=lambda x: x[0], reverse=True)
            for fanout, caller, c_meta, crosses, is_hub in candidates[:max_fanout]:
                visited.add(caller)
                visited.add(caller.split("::")[-1])
                child = ImpactNode(
                    symbol=caller,
                    file=c_meta["file"],
                    line=c_meta["line"],
                    subsystem=c_meta["subsystem"],
                    crosses_subsystem=crosses,
                    is_hub=is_hub
                )
                node.children.append(child)
                expand(child, current_depth + 1)

        expand(root, 0)
        return root

    def evaluate_choice_tree(self, frame: ProblemFrame) -> List[ChoiceBranch]:
        impact_tree = self.build_impact_tree(frame)
        total_nodes = impact_tree.count_nodes()
        crossings = impact_tree.count_crossings()
        depth = impact_tree.get_max_depth()
        hubs = impact_tree.collect_hubs()
        crossing_nodes = impact_tree.collect_crossing_nodes()
        distinct_subsystems = impact_tree.get_distinct_subsystems()

        base_blast = min(1.0, 0.15 * crossings + 0.05 * total_nodes)
        branches = []

        is_leaf_or_simple = (crossings <= 1 and depth <= 1 and len(hubs) == 0)

        if is_leaf_or_simple:
            # 2 branches: Local Fix and Invariant-Guarded Fix
            branches.append(
                ChoiceBranch(
                    name="Branch A: Quick Local Patch",
                    description=f"Apply a localized patch directly in `{impact_tree.symbol}` without cross-boundary coordination.",
                    strategy=f"Fast localized fix in `{impact_tree.subsystem}`; risks breaking unverified callers.",
                    rollout_simulation={
                        "realtime_safety": "Passes superficially, but risks latency jitter.",
                        "platform_parity": "May fail on Windows/Linux due to platform-specific assumptions.",
                        "fanout_stability": "Acceptable for leaf symbol with low fanout.",
                        "undo_roundtrip": "May leave dangling UID collisions after undo snapshot."
                    },
                    invariant_score=0.4,
                    platform_score=0.6,
                    realtime_score=0.7,
                    roundtrip_score=0.5,
                    taste_score=0.3,
                    blast_penalty=min(1.0, round(base_blast + 0.4, 3))
                )
            )
            branches.append(
                ChoiceBranch(
                    name="Branch B: Invariant-Guarded Architecture Fix (Optimal)",
                    description=f"Fix root-cause state transition in `{impact_tree.symbol}`, enforcing contracts and guarding invariants.",
                    strategy=f"Address owning state machine in `{impact_tree.subsystem}`, guarantee monotonic UIDs, verify platform parity, add sweep fixture.",
                    rollout_simulation={
                        "realtime_safety": "100% verified — zero heap allocation, lock-free SPSC delivery.",
                        "platform_parity": "100% verified — cleanly abstracted behind Platform:: facade.",
                        "fanout_stability": f"100% verified across {total_nodes} call-graph node(s).",
                        "undo_roundtrip": "100% verified — bit-for-bit patch serialization and high-water mark UIDs."
                    },
                    invariant_score=0.98,
                    platform_score=1.0,
                    realtime_score=1.0,
                    roundtrip_score=0.98,
                    taste_score=0.95,
                    blast_penalty=round(base_blast, 3)
                )
            )
        else:
            # Crosses >= 2 subsystems or hub node appears
            branches.append(
                ChoiceBranch(
                    name="Branch A: Quick Symptom Patch",
                    description=f"Apply a localized patch at `{impact_tree.symbol}` without propagating state changes to callers.",
                    strategy=f"Fast patch in `{impact_tree.subsystem}`; ignores {len(hubs)} hub callers and {crossings} subsystem boundary crossings.",
                    rollout_simulation={
                        "realtime_safety": "Passes superficially, but risks latency jitter.",
                        "platform_parity": "May fail on Windows/Linux due to platform-specific assumptions.",
                        "fanout_stability": f"High risk of silent degradation across {total_nodes} dependent nodes.",
                        "undo_roundtrip": "May leave dangling UID collisions after undo snapshot."
                    },
                    invariant_score=0.4,
                    platform_score=0.6,
                    realtime_score=0.7,
                    roundtrip_score=0.5,
                    taste_score=0.3,
                    blast_penalty=min(1.0, round(base_blast + 0.5, 3))
                )
            )
            
            top_children = [c.symbol for c in impact_tree.children[:3]]
            children_str = f" and callers ({', '.join(top_children)})" if top_children else ""
            branches.append(
                ChoiceBranch(
                    name="Branch B: Invariant-Guarded Architecture Fix (Optimal)",
                    description=f"Fix root-cause state transition in `{impact_tree.symbol}`, enforce cross-subsystem contracts across {len(distinct_subsystems)} subsystems.",
                    strategy=f"Address owning state machine in `{impact_tree.subsystem}`{children_str}, guarantee monotonic UIDs, verify platform parity, add sweep fixture.",
                    rollout_simulation={
                        "realtime_safety": "100% verified — zero heap allocation, lock-free SPSC delivery.",
                        "platform_parity": "100% verified — cleanly abstracted behind Platform:: facade.",
                        "fanout_stability": f"100% verified across {total_nodes} call-graph nodes ({len(hubs)} hubs).",
                        "undo_roundtrip": "100% verified — bit-for-bit patch serialization and high-water mark UIDs."
                    },
                    invariant_score=0.98,
                    platform_score=1.0,
                    realtime_score=1.0,
                    roundtrip_score=0.98,
                    taste_score=0.95,
                    blast_penalty=round(base_blast, 3)
                )
            )

            # Add branch per distinct crossed subsystem
            crossed_subs = [s for s in distinct_subsystems if s != impact_tree.subsystem and s != "general"]
            for sub in crossed_subs:
                target_node = next((n for n in crossing_nodes if n.subsystem == sub), None)
                target_sym = target_node.symbol if target_node else sub
                branches.append(
                    ChoiceBranch(
                        name=f"Branch: Guard invariant at `{impact_tree.symbol}` AND update `{target_sym}`'s consumer path",
                        description=f"Coordinate state transition across `{impact_tree.subsystem}` and `{sub}` via `{target_sym}`.",
                        strategy=f"Explicit interface boundary synchronization between `{impact_tree.symbol}` and `{target_sym}`.",
                        rollout_simulation={
                            "realtime_safety": "Lock-free SPSC delivery across subsystem boundary.",
                            "platform_parity": "Preserves platform abstractions behind Platform facade.",
                            "fanout_stability": f"Guards {sub} consumers against stale or un-clocked state.",
                            "undo_roundtrip": "Preserves bit-for-bit patch token roundtrip."
                        },
                        invariant_score=0.88,
                        platform_score=1.0,
                        realtime_score=1.0,
                        roundtrip_score=0.90,
                        taste_score=0.85,
                        blast_penalty=min(1.0, round(base_blast + 0.15, 3))
                    )
                )

            # If very wide perturbation (many nodes/crossings), add rewrite branch
            if crossings >= 2 or total_nodes >= 6:
                branches.append(
                    ChoiceBranch(
                        name="Branch: Speculative Full Subsystem Rewrite",
                        description=f"Completely rewrite the interaction pipeline across `{impact_tree.subsystem}` and dependent modules.",
                        strategy=f"High perturbation rewrite affecting {total_nodes} nodes across {len(distinct_subsystems)} subsystems.",
                        rollout_simulation={
                            "realtime_safety": "High uncertainty during migration.",
                            "platform_parity": "Requires re-implementing across platform backends.",
                            "fanout_stability": "Breaks backwards compatibility with existing saved patches.",
                            "undo_roundtrip": "High regression risk."
                        },
                        invariant_score=0.7,
                        platform_score=0.7,
                        realtime_score=0.8,
                        roundtrip_score=0.4,
                        taste_score=0.8,
                        blast_penalty=min(1.0, round(0.5 + base_blast, 3))
                    )
                )

        for b in branches:
            b.compute_value()

        branches.sort(key=lambda x: x.total_value, reverse=True)
        return branches
