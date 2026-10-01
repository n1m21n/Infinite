#!/usr/bin/env python3
"""Keep the quest ledger complete and its dates honest, from git and the release notes. Idempotent; run any time.

Sources, in order (each new quest is keyed by `src`, so a rerun adds nothing twice):
  1. merges    every merge into main that brings its own commits is one quest (src merge:<branch> or merge:<sha7>).
               feature/* = feature, bugfix/* fix/* perf/* = fix, docs/* chore/* test/* = infra, claude/* judged by its
               tip commit. Skipped: syncs of main into a branch, release/* (the release itself is the milestone).
  2. evidence  every quest with commits gets opened = first commit, closed = last commit (merge date for merges),
               release = first v* tag that contains it. Written as edit events only when a value changes.
  3. notes     every release-note bullet not already covered by a quest in that release becomes one quest
               (src note:<tag>:<n>), linked to the direct-to-main commits in that release window that match it.
  4. audit     coverage of each source, written to audit.json and shown on the dashboard.
Rule: never type a date or release by hand. Attach evidence (`roadmap.py link Rn --commits ...`) and rerun this.
Run: python3 tools/roadmap/backfill.py [--dry] [--audit]"""
import json, re, subprocess, sys
from functools import lru_cache
import roadmap as R

AUDIT = R.HERE / "audit.json"


def git(*a):
    return subprocess.run(["git", *a], cwd=R.ROOT, capture_output=True, text=True, check=True).stdout


TAGS = [t for t in git("tag", "--sort=creatordate").split() if re.match(r"^v\d", t)]
INFO = {}  # sha -> (iso date, subject)
for ln in git("log", "--all", "--format=%H|%aI|%s").splitlines():
    h, ts, s = ln.split("|", 2)
    INFO[h] = (ts, s)
TAG_DATE = {t: git("log", "-1", "--format=%aI", t).strip() for t in TAGS}


def full(sha):
    return next((h for h in INFO if h.startswith(sha)), sha) if len(sha) < 40 else sha


TRACK_WORDS = [
    ("learn", ("semi-brain", "predict", "modulat", "lfo", "macro", "envelope", "random", "pattern", "midi learn")),
    ("field", ("field", "infdev")),
    ("timeline", ("arrange", "timeline", "clip", "lane", "stretch", "av-sync", "recorder", "export", "movie")),
    ("platform", ("windows", "win-", "linux", "appimage", "vst3", "au-", "plugin", "ci", "syphon", "spout", "packag", "mac", "wasapi", "headless")),
    ("growth", ("website", "video", "launch", "readme", "badge", "soundtrack")),
    ("ship", ("release", "version", "notes")),
    ("ux", ("ui", "menu", "panel", "shortcut", "style", "theme", "help", "dialog", "button", "dock", "hover", "popup", "cursor", "font", "scroll", "canvas", "cull")),
    ("nodes", ("node", "filter", "blur", "color", "geometry", "mesh", "shader", "material", "light", "slideshow", "meter", "eq", "synth", "delay", "reverb", "sampler", "compressor", "looper")),
]


def track_of(text):
    t = text.lower()
    for tr, ws in TRACK_WORDS:
        if any(re.search(r"(?<![a-z])" + re.escape(w), t) for w in ws):
            return tr
    return "engine"


@lru_cache(maxsize=None)
def release_of(sha):
    have = set(git("tag", "--contains", sha).split())
    return next((t for t in TAGS if t in have), "")


def branch_commits(merge):
    return [h for h in git("rev-list", "--no-merges", f"{merge}^1..{merge}^2").split()]


HOUSEKEEPING = re.compile(r"^(Release\b|Bump version|chore\(semi-brain\)|Merge\b)", re.I)
SYNC = re.compile(r"^Merge (branch '(origin/)?main'|(origin/)?main\b|remote-tracking branch 'origin/main')", re.I)
SUB = re.compile(r"^Merge (?:pull request #\d+ from \S+?/|branch '|remote-tracking branch '|worktree-|)(?P<b>[\w.\-]+(?:/[\w.\-/]+?)?)'?(?: into (?P<t>\S+))?(?::.*)?$")
KIND_OF_HEAD = {"feature": "feature", "bugfix": "fix", "fix": "fix", "perf": "fix", "docs": "infra", "chore": "infra", "test": "infra"}


def merges():
    """Every merge that landed its own work on main's first-parent line: [{sha, src, title, kind, head}]."""
    out, seen = [], set()
    for ln in reversed(git("log", "main", "--first-parent", "--merges", "--format=%H|%s").splitlines()):
        sha, subj = ln.split("|", 1)
        if SYNC.match(subj) or "reconcile" in subj.lower():
            continue
        m = SUB.match(subj)
        b = m["b"] if m and ("/" in m["b"] or subj.startswith(("Merge branch '", "Merge worktree-"))) else ""
        if m and m["t"] and m["t"] not in ("main", "origin/main"):
            continue
        head = b.split("/")[0] if "/" in b else ""
        if head == "release" or (b and b in seen):
            continue
        work = branch_commits(sha)
        if not work:
            continue
        tip = INFO.get(git("rev-parse", sha + "^2").strip(), ("", ""))[1]
        if head == "claude":
            kind = "fix" if re.match(r"(fix|bug)", tip, re.I) else "feature"
        elif head:
            kind = KIND_OF_HEAD.get(head, "feature")
        else:
            kind = "fix" if re.search(r"\b(fix|un-break|defect|failure|crash)", subj, re.I) else "feature"
        if b:
            seen.add(b)
            name = (b.split("/", 1)[1] if "/" in b else b).replace("-", " ").replace("_", " ")
            title = tip if head == "claude" and tip else name[:1].upper() + name[1:]
            src = f"merge:{b}"
        else:
            title = re.sub(r"^merge(\([^)]*\))?:?\s*", "", subj, flags=re.I)
            title = title[:1].upper() + title[1:]
            src = f"merge:{sha[:7]}"
        out.append(dict(sha=sha, src=src, title=title[:90], kind=kind, work=work, text=f"{b} {subj} {tip}"))
    return out


STOPW = R.STOP | {"add", "adds", "added", "fix", "fixe", "new", "now", "when", "that", "this", "than", "into", "not", "are",
                  "was", "its", "your", "you", "can", "all", "one", "more", "also", "use", "make", "get", "via", "per",
                  "has", "have", "been", "out", "any", "each", "own", "only", "just", "them", "they", "their", "which",
                  "infinite", "merge", "branch", "main", "step", "docs", "test", "tests"}


SHORT = {"of", "to", "in", "on", "is", "it", "an", "at", "by", "or", "as", "be", "do", "no", "so", "up", "we", "if", "me",
         "my", "us", "vs", "go", "re"}


def _stem(w):
    for suf in ("ing", "es", "ed", "s"):
        if w.endswith(suf) and len(w) - len(suf) >= 4:
            return w[: -len(suf)]
    return w


@lru_cache(maxsize=None)
def _tok(t):
    return frozenset(w for w in (_stem(x) for x in re.findall(r"[a-z0-9]+", t.lower())) if (len(w) >= 3 or (len(w) == 2 and w not in SHORT)) and w not in STOPW)


def _head(t):
    """The subject of a release-note bullet: its first clause, where the node/feature name lives."""
    h = re.split(r"[:—(;.]| - ", t, 1)[0]
    ws = [w for w in (_stem(x) for x in re.findall(r"[a-z0-9]+", h.lower())) if w in _tok(w)]
    return frozenset(ws[:8])


def _sim(a, b):
    i = len(a & b)
    return i / min(len(a), len(b)) if i >= 2 and a and b else 0.0


def evidence_of(x, mmap):
    """(opened, closed, release, commits) derived from a quest's evidence, or None."""
    src = x.get("src") or ""
    if src in mmap:
        m = mmap[src]
        dates = [INFO[h][0] for h in m["work"] if h in INFO]
        return min(dates), INFO[m["sha"]][0], release_of(m["sha"]), [m["sha"][:7]]
    cs = [full(c) for c in x.get("commits") or []]
    if not cs and src.startswith("merge:"):
        m = re.search(r"\(([0-9a-f]{7,40})\)", x.get("why", ""))
        cs = [full(m[1])] if m else []
        if cs and cs[0] in INFO and INFO[cs[0]][1].startswith("Merge"):
            work = branch_commits(cs[0]) or cs
            dates = [INFO[h][0] for h in work if h in INFO]
            return min(dates), INFO[cs[0]][0], release_of(cs[0]), [cs[0][:7]]
    cs = [c for c in cs if c in INFO]
    if not cs:
        return None
    dates = sorted(INFO[c][0] for c in cs)
    last = max(cs, key=lambda c: INFO[c][0])
    rels = [release_of(c) for c in cs]
    rel = max(rels, key=lambda t: TAGS.index(t) if t else -1) if all(rels) else ""
    return dates[0], dates[-1], rel, [c[:7] for c in cs]


MATCH = 0.2    # weighted overlap a match needs...
SURE = 0.4     # ...and above this it needs no margin
MARGIN = 0.08  # below SURE the best owner must beat the runner-up owner by this much


def confident(sc):
    """sc: [(score, owner)] best first, one row per owner. True when the best is a trustworthy match."""
    if not sc or sc[0][0] < MATCH:
        return False
    return sc[0][0] >= SURE or len(sc) == 1 or sc[0][0] - sc[1][0] >= MARGIN


def _idf(texts):
    import math
    df = {}
    for t in texts:
        for w in _tok(t):
            df[w] = df.get(w, 0) + 1
    n = len(texts) or 1
    return {w: math.log((n + 1) / (c + 0.5)) for w, c in df.items()}


def _vec(t, idf):
    import math
    v = {w: idf.get(w, 1.0) for w in _tok(t)}
    z = math.sqrt(sum(x * x for x in v.values())) or 1
    return {w: x / z for w, x in v.items()}


def _ovl(a, b, idf):
    """Weighted overlap: idf mass of shared words over the smaller side's mass. Needs 2+ shared words.
    Unlike cosine it does not punish a long prose bullet for matching a terse commit subject."""
    ta, tb = _tok(a), _tok(b)
    sh = ta & tb
    if len(sh) < 2 or not (sh & _head(a)):
        return 0.0
    m = lambda t: sum(idf.get(w, 1.0) for w in t)
    return m(sh) / (min(m(ta), m(tb)) or 1)


def _cos(a, b):
    return sum(x * b.get(w, 0) for w, x in a.items())


def quest_text(x, mmap):
    t = x["title"] + " " + x.get("why", "")
    m = mmap.get(x.get("src") or "")
    for h in (m["work"] if m else [full(c) for c in x.get("commits") or []]):
        t += " " + INFO.get(h, ("", ""))[1]
    return t


def quest_lines(x, mmap):
    """A quest's title plus every commit subject behind it (a merge counts as its own subject and its branch's)."""
    m = mmap.get(x.get("src") or "")
    hs = [m["sha"], *m["work"]] if m else []
    for c in ([] if m else x.get("commits") or []):
        h = full(c)
        hs.append(h)
        if INFO.get(h, ("", ""))[1].startswith("Merge"):
            hs += branch_commits(h)
    return [x["title"]] + [INFO.get(h, ("", ""))[1] for h in hs]


def owners(b, mmap):
    own = {}
    for x in b:
        m = mmap.get(x.get("src") or "")
        for h in (m["work"] if m else [full(c) for c in x.get("commits") or []]):
            own.setdefault(h, x["id"])
    for m in mmap.values():
        for h in m["work"]:
            own.setdefault(h, m["src"])
    return own


@lru_cache(maxsize=None)
def _window(k):
    rng = TAGS[k] if k == 0 else f"{TAGS[k - 1]}..{TAGS[k]}"
    return tuple(h for h in git("rev-list", "--first-parent", "--no-merges", rng).split() if not HOUSEKEEPING.match(INFO[h][1]))


def window_direct(k):
    return list(_window(k))


def run_evidence(mmap, log, dry):
    for x in R.board():
        e = evidence_of(x, mmap)
        if not e or x.get("epic"):
            continue
        opened, closed, rel, commits = e
        cur = (x["ts"], x.get("closed"), x.get("release", ""), x.get("commits"))
        want = (opened, closed if x["status"] != "open" else cur[1], rel if x["status"] == "done" else cur[2], commits)
        if cur != want:
            row = {"event": "edit", "id": x["id"], "opened": opened, "commits": commits}
            if x["status"] != "open":
                row["closed"] = closed
            if x["status"] == "done":
                row["release"] = rel
            log.append(f"~ {x['id']} {x['ts'][:10]}->{opened[:10]} rel {x.get('release', '') or '-'}->{row.get('release', '-') or '-'}  {x['title'][:60]}")
            if not dry:
                R._append(R.QUESTS, row)


def run(dry=False):
    log = []
    ms = merges()
    mmap = {m["src"]: m for m in ms}
    b = R.board()
    have = {x.get("src") for x in b}
    nid = [sum(e["event"] == "add" for e in R._read(R.QUESTS))]

    def add(src, kind, track, title, why, opened, closed, release, commits):
        nid[0] += 1
        i = f"R{nid[0]}"
        log.append(f"+ {i} {closed[:10]} {release or '-':8} {kind:7} {title[:70]}")
        if dry:
            return
        R._append(R.QUESTS, {"ts": opened, "event": "add", "id": i, "after": [], "user": False, "src": src, "track": track,
                             "kind": kind, "horizon": "later", "title": title, "why": why, "origin": "merge"})
        R._append(R.QUESTS, {"ts": closed, "event": "done", "id": i, "why": why, **({"release": release} if release else {})})
        R._append(R.QUESTS, {"ts": closed, "event": "edit", "id": i, "opened": opened, "closed": closed, "release": release,
                             "commits": commits})

    # 1. merges
    linked = {full(c) for x in R.board() for c in x.get("commits") or []}
    for m in ms:
        if m["src"] in have or linked & {m["sha"], *m["work"]}:  # already logged by hand (done --commits / link)
            continue
        dates = [INFO[h][0] for h in m["work"]]
        add(m["src"], m["kind"], track_of(m["text"]), m["title"], f"merged into main ({m['sha'][:7]})",
            min(dates), INFO[m["sha"]][0], release_of(m["sha"]), [m["sha"][:7]])

    # 2. evidence -> dates and releases for every quest that has any
    run_evidence(mmap, log, dry)

    # 3. release-note bullets. Each bullet is matched (tf-idf cosine) against the quests shipped in that release and the
    # direct-to-main commits of that release window. Best match a quest: covered. Best match direct commits: the bullet
    # becomes a quest linked to them. No match: a quest dated to the release. Bullets repeated from an earlier release
    # (re-cuts like v0.4.25) are skipped.
    rels = {r["tag"]: r for r in R.github()["releases"]}
    b = R.board()
    owner = {h: o for h, o in owners(b, mmap).items() if not str(o).startswith("upkeep:")
             and not next((x for x in b if x["id"] == o and str(x.get("src", "")).startswith("upkeep:")), None)}
    seen_notes = []
    for k, tag in enumerate(TAGS):
        notes = (rels.get(tag) or {}).get("notes") or []
        near = {TAGS[j] for j in (k - 1, k, k + 1) if 0 <= j < len(TAGS)}  # notes are often written a tag early or late
        # one entry per quest title and per commit subject, so a long branch does not dilute its best line
        entries = []
        for x in b:
            if x["status"] == "done" and x.get("release") in near \
                    and not str(x.get("src", "")).startswith(("note:", "upkeep:")):  # nor by another bullet or a catch-all
                entries += [(x["id"], t) for t in quest_lines(x, mmap)]
        entries += [(h, INFO[h][1]) for h in window_direct(k) if h not in owner]
        idf = _idf([t for _, t in entries] + notes)
        vec = list(entries)
        for n, note in enumerate(notes):
            src = f"note:{tag}:{n}"
            nv = _vec(note, idf)
            repeat = any(_cos(nv, _vec(e, idf)) >= 0.6 for e in seen_notes)
            seen_notes.append(note)
            if src in have or repeat:
                continue
            best_of = {}
            for o, t in vec:
                c = _ovl(note, t, idf)
                if c > best_of.get(o, 0):
                    best_of[o] = c
            sc = sorted(((c, o) for o, c in best_of.items()), reverse=True)
            best, top = sc[0] if sc else (0, "")
            ok = confident(sc)
            if ok and top.startswith("R"):
                continue
            hits = [o for c, o in sc[:4] if ok and not o.startswith("R") and c >= max(MATCH, best * 0.7) and o not in owner]
            kind = "fix" if re.match(r"fix", note, re.I) else "feature"
            opened = min(INFO[h][0] for h in hits) if hits else TAG_DATE[tag]
            closed = max(INFO[h][0] for h in hits) if hits else TAG_DATE[tag]
            add(src, kind, track_of(note), note[:90], f"{tag} release notes" + (f" ({', '.join(h[:7] for h in hits)})" if hits else ", no single matching commit"),
                opened, closed, tag, [h[:7] for h in hits])
            for h in hits:
                owner[h] = src
            vec = [(o, t) for o, t in vec if o not in hits]

    # 4. every remaining direct commit in a released window joins the most similar quest of that release; what is left
    # (docs, tests, skills, tooling) becomes one infra quest per release. Unreleased commits stay loose for linking.
    b = R.board()
    owner = owners(b, mmap)
    for k, tag in enumerate(TAGS):
        loose = [h for h in window_direct(k) if h not in owner]
        if not loose:
            continue
        cands = [(x["id"], t) for x in b if x["status"] == "done" and x.get("release") == tag and not x.get("epic")
                 and not (x.get("src") or "").startswith(("merge:", "upkeep:")) for t in quest_lines(x, mmap)]
        idf = _idf([t for _, t in cands] + [INFO[h][1] for h in loose])
        grow, rest = {}, []
        for h in loose:
            per = {}
            for i, t in cands:
                per[i] = max(per.get(i, 0), _ovl(INFO[h][1], t, idf))
            sc = sorted(((c, i) for i, c in per.items()), reverse=True)
            if confident(sc):
                grow.setdefault(sc[0][1], []).append(h)
            else:
                rest.append(h)
        byid = {x["id"]: x for x in b}
        for i, hs in grow.items():
            cs = sorted({*(byid[i].get("commits") or []), *(h[:7] for h in hs)})
            log.append(f"~ {i} +{len(hs)} commits  {byid[i]['title'][:60]}")
            if not dry:
                R._append(R.QUESTS, {"event": "edit", "id": i, "commits": cs})
        src = f"upkeep:{tag}"
        if rest and src not in have:
            add(src, "infra", "engine", f"Docs, tests, skills and tooling for {tag}",
                f"{len(rest)} direct commits in the {tag} window with no feature of their own",
                min(INFO[h][0] for h in rest), max(INFO[h][0] for h in rest), tag, [h[:7] for h in rest])
        elif rest:
            x = next(x for x in b if x.get("src") == src)
            cs = sorted({*(x.get("commits") or []), *(h[:7] for h in rest)})
            if not dry:
                R._append(R.QUESTS, {"event": "edit", "id": x["id"], "commits": cs})
    if not dry:
        run_evidence(mmap, log, dry)
    print("\n".join(log) or "ledger already complete")
    return log


def audit():
    """Coverage of every source; the dashboard shows it. Unattributed direct commits are listed so they can be linked."""
    ms = merges()
    mmap = {m["src"]: m for m in ms}
    b = R.board()
    rels = {r["tag"]: r for r in R.github()["releases"]}
    srcs = {x.get("src") for x in b}
    attributed = {full(c) for x in b for c in x.get("commits") or []} | {h for m in ms if m["src"] in srcs for h in m["work"]}
    direct = [h for h in git("rev-list", "--first-parent", "--no-merges", "main").split() if not HOUSEKEEPING.match(INFO[h][1])]
    loose = [h for h in direct if h not in attributed]
    notes_total = sum(len((rels.get(t) or {}).get("notes") or []) for t in TAGS)
    per_rel = {}
    for t in TAGS:
        per_rel[t] = {"quests": sum(x["status"] == "done" and x.get("release") == t for x in b),
                      "notes": len((rels.get(t) or {}).get("notes") or [])}
    done = [x for x in b if x["status"] == "done"]
    a = {
        "merges": [sum(m["src"] in srcs for m in ms), len(ms)],
        "direct": [len(direct) - len(loose), len(direct)],
        "notes_total": notes_total,
        "evidence": [sum(bool(x.get("commits")) or (x.get("src") or "") in mmap for x in done), len(done)],
        "per_release": per_rel,
        "loose": [f"{INFO[h][0][:10]} {h[:7]} {INFO[h][1][:90]}" for h in loose],
        "no_evidence": [f"{x['id']} {x['title'][:80]}" for x in done if not (x.get("commits") or (x.get("src") or "") in mmap)],
        "first_commit": INFO[git("rev-list", "--max-parents=0", "main").split()[0]][0][:10],
    }
    AUDIT.write_text(json.dumps(a, indent=1))
    print(f"merges {a['merges'][0]}/{a['merges'][1]} · direct commits attributed {a['direct'][0]}/{a['direct'][1]} · "
          f"done quests with evidence {a['evidence'][0]}/{a['evidence'][1]} · release-note bullets {notes_total}")
    for t, v in per_rel.items():
        print(f"  {t:13} notes {v['notes']:3}  quests {v['quests']:3}")
    return a


if __name__ == "__main__":
    if "--dry" in sys.argv:
        import shutil, tempfile
        tmp = R.Path(tempfile.mkdtemp()) / "quests.jsonl"
        shutil.copy(R.QUESTS, tmp)
        R.QUESTS = tmp
        AUDIT = tmp.with_name("audit.json")
        run()
        audit()
        sys.exit()
    if "--audit" in sys.argv:
        audit()
    else:
        run()
        audit()
