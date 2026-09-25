#!/usr/bin/env python3
"""
store.py
The L0 store: what a session asserts for later sessions. `brain.assert(claim, evidence,
confidence)` from the CLI (l0/cli.py) or the MCP server (l0/mcp_server.py).

  kinds     fact | decision | rejected (an alternative tried and dropped, and why) |
            question (open) | hint (where to look for what)
  evidence  "commit:<hash>", "file:<path>[:line]", "symbol:<Name>", "test:<name>", "url:<url>".
            commit/file/symbol are checked against the repo when asserted
  tiers     owner     approved or stated by the owner (l0/cli.py approve, sleep.py --approve)
            verified  some commit/file/symbol evidence resolves - the pointer is real, which
                      is not the same as the claim being proven
            claude    nothing resolves
  weight    reliability(region, kind) x calibrated confidence x tier factor x decay, capped
            at CAP so a note can never outrank verified code.
              reliability  Beta mean per (top dir of its first file, kind), prior PRIOR;
                           counts come only from real outcomes - a turn that got the note in
                           its brief and then edited (or not) the note's files (l1/sleep.py)
              calibration  confidence bins -> observed hit rate, once a bin has CAL_MIN notes
              decay        half-life HALF_LIFE_DAYS; owner notes do not decay
  no echo   asserting the same claim again changes nothing, and being retrieved never counts
            as a confirmation: only edits after a brief do

Stored as an append-only event log l1/state/l0.jsonl (local only: claims come from chats), so
the CLI and the MCP server can write at the same time. Reliability and calibration live in
l1/state/l0_reliability.json, written by the nightly sleep job.
"""

import json
import re
import subprocess
import time
import uuid
from pathlib import Path

L0_DIR = Path(__file__).resolve().parent
SEMI_BRAIN_DIR = L0_DIR.parent
REPO_PATH = SEMI_BRAIN_DIR.parents[1]
STATE_DIR = SEMI_BRAIN_DIR / "l1" / "state"
STORE = STATE_DIR / "l0.jsonl"
RELIABILITY = STATE_DIR / "l0_reliability.json"
AST_GRAPH = SEMI_BRAIN_DIR / "1_extractors" / "output" / "ast_symbol_graph.json"

KINDS = ("fact", "decision", "rejected", "question", "hint")
BOOST_KINDS = ("fact", "decision", "hint")   # kinds whose files join the file ranking
TIERS = ("owner", "verified", "claude")
TIER_FACTOR = {"owner": 1.0, "verified": 0.85, "claude": 0.5}
CAP = 0.6
HALF_LIFE_DAYS = 60.0
PRIOR = (2.0, 1.0)
CAL_MIN = 10
MIN_OVERLAP = 2
MAX_CLAIM = 600

_IDENT_RE = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|\d+")
_STOP = {"the", "and", "for", "with", "get", "set", "node", "nodes", "fix", "from", "into",
         "when", "not", "its", "this", "that", "only", "are", "was", "but", "because", "should",
         "must", "use", "used", "does", "can", "has", "have", "all", "any", "one", "why"}


def words(text):
    out = set()
    for tok in re.findall(r"[A-Za-z0-9_]+", text or ""):
        for w in _IDENT_RE.findall(tok):
            w = w.lower()
            if len(w) > 4 and w.endswith("s") and not w.endswith("ss"):
                w = w[:-1]
            if len(w) > 2 and w not in _STOP:
                out.add(w)
    return out


def _norm(claim):
    return " ".join(re.findall(r"[a-z0-9]+", claim.lower()))


def region(files):
    for f in files:
        parts = f.split("/")
        return "/".join(parts[:2]) if len(parts) > 2 else parts[0]
    return "-"


class Evidence:
    """Checks commit/file/symbol pointers against the repo."""

    def __init__(self):
        self._symbols = None

    def symbols(self):
        if self._symbols is None:
            try:
                self._symbols = set(json.loads(AST_GRAPH.read_text()).get("symbols", {}))
            except (OSError, ValueError):
                self._symbols = set()
        return self._symbols

    def check(self, ev):
        """(resolves, file it points at or '')."""
        kind, _, ref = ev.partition(":")
        if kind == "commit" and re.fullmatch(r"[0-9a-f]{7,40}", ref):
            r = subprocess.run(["git", "cat-file", "-e", ref + "^{commit}"], cwd=REPO_PATH,
                               capture_output=True)
            return r.returncode == 0, ""
        if kind == "file":
            path = ref.split(":", 1)[0]
            return (REPO_PATH / path).is_file(), path
        if kind == "symbol":
            syms = self.symbols()
            return ref in syms or any(s.endswith("::" + ref) for s in syms), ""
        return False, ""


class Store:
    def __init__(self, path=STORE, reliability=RELIABILITY):
        self.path = Path(path)
        self.rel_path = Path(reliability)
        self._sig = None
        self.notes = {}
        self.rel = {"regions": {}, "calibration": []}

    # ---- reading -------------------------------------------------------------------------
    def _stamp(self):
        out = []
        for p in (self.path, self.rel_path):
            try:
                st = p.stat()
                out.append((st.st_mtime_ns, st.st_size))
            except OSError:
                out.append(None)
        return tuple(out)

    def refresh(self):
        """Re-read when either file changed; a stat per call, so the warm server stays live."""
        sig = self._stamp()
        if sig == self._sig:
            return self
        self._sig = sig
        notes = {}
        if self.path.exists():
            for line in self.path.open():
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                op = e.get("op")
                if op == "assert":
                    notes[e["id"]] = dict(e, active=True)
                elif op in ("retract", "approve") and e.get("id") in notes:
                    n = notes[e["id"]]
                    if op == "retract":
                        n["active"] = False
                    else:
                        n["tier"] = "owner"
        self.notes = notes
        try:
            self.rel = json.loads(self.rel_path.read_text())
        except (OSError, ValueError):
            self.rel = {"regions": {}, "calibration": []}
        return self

    def active(self):
        return [n for n in self.refresh().notes.values() if n["active"]]

    # ---- weighting -----------------------------------------------------------------------
    def reliability(self, n):
        s, f = self.rel.get("regions", {}).get(f"{region(n['files'])}|{n['kind']}", (0, 0))
        return (PRIOR[0] + s) / (PRIOR[0] + PRIOR[1] + s + f)

    def calibrated(self, conf):
        for lo, hi, rate, count in self.rel.get("calibration", []):
            if lo <= conf < hi and count >= CAL_MIN:
                return rate
        return conf

    def weight(self, n, now):
        decay = 1.0 if n["tier"] == "owner" else 0.5 ** (max(0.0, now - n["t"]) / 86400.0 / HALF_LIFE_DAYS)
        w = self.reliability(n) * self.calibrated(n["confidence"]) * TIER_FACTOR[n["tier"]] * decay
        return min(CAP, w)

    def match(self, query, now=None, embargo=0.0, limit=5):
        """Notes the query is about: MIN_OVERLAP shared words, or a file of the note named in
        the query. Only notes asserted before now - embargo (the replay's cutoff)."""
        now = time.time() if now is None else now
        q = words(query)
        ql = (query or "").lower()
        out = []
        for n in self.active():
            if n["t"] >= now - embargo:
                continue
            overlap = len(q & set(n["terms"]))
            named = any(f.lower() in ql or f.rsplit("/", 1)[-1].lower() in ql for f in n["files"])
            if overlap < MIN_OVERLAP and not named:
                continue
            w = self.weight(n, now)
            score = w * min(1.0, (overlap + 2 * named) / 3.0)
            out.append({"id": n["id"], "kind": n["kind"], "tier": n["tier"], "claim": n["claim"],
                        "files": n["files"], "weight": round(w, 3), "score": round(score, 3)})
        out.sort(key=lambda m: -m["score"])
        return out[:limit]

    # ---- writing -------------------------------------------------------------------------
    def _append(self, event):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("a") as fh:
            fh.write(json.dumps(event) + "\n")

    def add(self, claim, kind="fact", evidence=(), confidence=0.6, files=(), terms=(),
            session="", owner=False, source="cli"):
        """Returns (note, created). The same claim asserted again returns the existing note
        unchanged: repeating a claim is not evidence for it."""
        claim = (claim or "").strip()[:MAX_CLAIM]
        if not claim:
            raise ValueError("empty claim")
        if kind not in KINDS:
            raise ValueError(f"kind must be one of {KINDS}")
        confidence = min(0.95, max(0.05, float(confidence)))
        norm = _norm(claim)
        for n in self.active():
            if _norm(n["claim"]) == norm:
                return n, False
        checker = Evidence()
        checked, files = [], [f for f in files if f]
        for ev in evidence:
            ok, path = checker.check(ev)
            checked.append({"ref": ev, "resolves": ok})
            if ok and path and path not in files:
                files.append(path)
        tier = "owner" if owner else ("verified" if any(c["resolves"] for c in checked) else "claude")
        note = {"op": "assert", "id": uuid.uuid4().hex[:10], "t": time.time(), "kind": kind,
                "claim": claim, "evidence": checked, "confidence": confidence, "tier": tier,
                "files": files, "terms": sorted(words(claim) | set(terms) |
                                                {w for f in files for w in words(f.rsplit("/", 1)[-1])}),
                "session": session, "source": source}
        self._append(note)
        self._sig = None
        return dict(note, active=True), True

    def retract(self, note_id, reason=""):
        if note_id not in self.refresh().notes:
            raise KeyError(note_id)
        self._append({"op": "retract", "id": note_id, "t": time.time(), "reason": reason})
        self._sig = None

    def approve(self, note_id):
        """Owner approval: the note becomes owner tier. Only ever on the owner's word."""
        if note_id not in self.refresh().notes:
            raise KeyError(note_id)
        self._append({"op": "approve", "id": note_id, "t": time.time()})
        self._sig = None
