"""
Infinite roadmap game: quest board + user-base scoreboard as one self-contained HTML page.

    python3 tools/roadmap/roadmap.py build [--open]          # pull GitHub, snapshot metrics, write roadmap.html
    python3 tools/roadmap/roadmap.py add --track nodes --kind feature --horizon next --title "..." --why "..." [--after R3,R4] [--user]
    python3 tools/roadmap/roadmap.py start R3                 # prints the skills to load first
    python3 tools/roadmap/roadmap.py done R3 --why "what shipped" [--release v0.4.6]
    python3 tools/roadmap/roadmap.py drop R3 --why "..."
    python3 tools/roadmap/roadmap.py move R3 --horizon now
    python3 tools/roadmap/roadmap.py ga --sessions 1234 [--users 900] [--days 30]   # GA4 has no live pull here; log it
    python3 tools/roadmap/roadmap.py show

Quests are events in quests.jsonl (state is folded from them, like groww-finance's quest board). Stars and downloads are
read live from GitHub on every build and appended once per day to metrics.jsonl, so the trend is kept. GA sessions come
from `ga` rows in the same file. XP is only earned by closing quests; downloads are the Gold.

Closed = committed and merged to main. Shipped is different: a closed quest ships when a tagged release contains it, recorded
with `done --release vX` (or later `ship R3 --release vX`). Closed-but-unreleased quests show as "merged".
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timedelta, timezone
UTC = timezone.utc
IST = timezone(timedelta(hours=5, minutes=30))
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
QUESTS = HERE / "quests.jsonl"
METRICS = HERE / "metrics.jsonl"
TEMPLATE = HERE / "template.html"
OUT = HERE / "roadmap.html"
REPO = "n1m21n/Infinite"

ALWAYS = ["codebase-lenses", "codebase-navigation"]
TRACKS = {
    "engine": ["run-infinite-hygiene", "audio-pipeline-sweep", "render-pipeline-sweep"],
    "nodes": ["node-ui-pillars", "cable-logic-sweep", "node-param-audit"],
    "field": ["field-language", "field-compiler", "field-testing"],
    "timeline": ["timeline-arrangement-architecture", "av-sync-sweep"],
    "learn": ["semi-brain", "new-modulator-node", "modulation-sweep"],
    "platform": ["windows-parity", "linux-parity", "plugin-host-hardening"],
    "ux": ["node-ui-pillars", "panels-sweep", "shortcuts-sweep"],
    "ship": ["ship-infinite", "release-notes-audit"],
    "growth": ["motion-film", "node-tutorial-film"],
}
KINDS = {"feature": 10, "fix": 5, "infra": 5, "growth": 10}
HORIZONS = ["now", "next", "later"]
LIVES_MAX = 5

# User-base ladder: every threshold in a level must be met. sessions = GA sessions in the last logged window.
LEVELS = [
    ("Prototype", {}),
    ("First users", {"downloads": 100}),
    ("Fans", {"stars": 250, "downloads": 1000}),
    ("Community", {"stars": 1000, "downloads": 5000, "sessions": 5000}),
    ("Instrument", {"stars": 5000, "downloads": 25000, "sessions": 25000}),
    ("Standard", {"stars": 20000, "downloads": 100000, "sessions": 100000}),
]


def _now() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds")


def _read(p: Path) -> list[dict]:
    return [json.loads(x) for x in p.read_text().splitlines() if x.strip()] if p.exists() else []


def _append(p: Path, row: dict) -> None:
    with p.open("a") as f:
        f.write(json.dumps({"ts": _now(), **row}) + "\n")


def _afters(x: dict) -> list[str]:
    a = x.get("after") or []
    return [s for s in (a.split(",") if isinstance(a, str) else a) if s]


# --- Priority ranking -------------------------------------------------------------------------------------------
# One transparent score per open quest decides "next quest". Every part is listed on the dashboard so the ranking can
# be argued with. Only ready quests (open, unblocked, not waiting on the owner) are ever offered as next.
HORIZON_W = {"now": 30, "next": 15, "later": 0}
# where the quest came from: the owner's own ask and real users' problems beat our own backlog
ORIGIN_W = {"ask": 25, "bug": 25, "pr": 20, "issue": 15, "brief": 12, "explore": 10, "plan": 5, "idea": 3, "merge": 0}
KIND_W = {"fix": 12, "feature": 8, "growth": 8, "infra": 4}
EFFORT_W = {"S": 0, "M": -4, "L": -10}
ORIGINS = list(ORIGIN_W)
# Impact rubric, each axis rated 0-3 by whoever adds the quest (roadmap.py add/edit --rate user=3,func=2,perf=0,ux=1).
#   user = reach: 3 default path for most users, 2 common workflow, 1 niche or non-default mode, 0 internal only
#   func = something broken (3 crash/data loss/wrong output, 2 feature missing or wrong) or newly possible
#   perf = frame/audio cost, weighted again by the owner's tier order: audio > projector > canvas > previews
#   ux   = clarity, discoverability, look and feel
RATE_W = {"user": 5, "func": 3, "perf": 3, "ux": 2}
PERF_TIER_W = {"audio": 4, "projector": 3, "canvas": 2, "preview": 1}
# Until someone rates a quest, guess from its kind and track and say so in the breakdown.
GUESS = {"fix": {"func": 2, "user": 1}, "feature": {"func": 1, "user": 1}, "growth": {"user": 2}, "infra": {"func": 1}}
TRACK_GUESS = {"ux": {"ux": 2}, "engine": {"perf": 1}}
STOP = {"feature", "bugfix", "fix", "claude", "the", "and", "for", "node", "part", "with", "from", "into"}


def origin_of(x: dict) -> str:
    if x.get("origin"):
        return x["origin"]
    src = x.get("src") or ""
    for pre, o in (("issue:", "issue"), ("pr:", "pr"), ("plan:", "plan"), ("brief:", "brief"), ("merge:", "merge")):
        if src.startswith(pre):
            return o
    # No source and no stated origin: logged by a session, not asked for by the owner.
    return "explore"


def effort_of(x: dict) -> str:
    return x.get("effort") or ("L" if origin_of(x) == "plan" else "S" if origin_of(x) == "pr" else "M")


def _tokens(t: str) -> set[str]:
    return {w for w in re.findall(r"[a-z0-9]+", t.lower()) if len(w) > 3 and w not in STOP}


def score(x: dict, ctx: dict) -> tuple[int, list[str]]:
    """Score one open quest. ctx: {issues:{n:{age,bug}}, prs:{n:{age}}, branches:[name], levelup:bool, now:datetime}."""
    o = origin_of(x)
    src = x.get("src") or ""
    if o == "issue" and ctx.get("issues", {}).get(int(src.split(":")[1]) if src[6:].isdigit() else -1, {}).get("bug"):
        o = "bug"
    parts = [(f"horizon {x['horizon']}", HORIZON_W[x["horizon"]]), (f"from {o}", ORIGIN_W[o]),
             (f"{x['kind']}", KIND_W[x["kind"]])]
    if x["unlocks"]:
        parts.append((f"unlocks {x['unlocks']}", min(18, 6 * x["unlocks"])))
    days = max(0, (ctx["now"] - datetime.fromisoformat(x["ts"])).days) if ctx.get("now") else 0
    if days:
        parts.append((f"waiting {days}d", min(10, round(days * 0.4))))
    if o in ("bug", "issue", "pr") and src[src.find(":") + 1:].isdigit():
        n = int(src.split(":")[1])
        age = (ctx.get("issues", {}).get(n) or ctx.get("prs", {}).get(n) or {}).get("age", 0)
        if age:
            parts.append((f"open {age}d, costs health", min(20, round(age * 0.5))))
    if x["track"] in ("growth", "ship") and ctx.get("levelup"):
        parts.append(("moves the next level", 8))
    toks = _tokens(x["title"])
    hit = next((b for b in ctx.get("branches", []) if len(toks & _tokens(b)) >= 2), None)
    if hit:
        x["wip"] = hit
        parts.append((f"branch {hit} already started", 15))
    rate = x.get("rate")
    guess = not rate
    if guess:
        rate = {**GUESS.get(x["kind"], {}), **TRACK_GUESS.get(x["track"], {})}
    imp = {k: max(0, min(3, int(rate.get(k, 0)))) for k in RATE_W}
    for k, w in RATE_W.items():
        if imp[k]:
            # a guessed rating is shown but never counted: only a rated quest can win on impact
            parts.append((f"{k} {imp[k]}/3{' (unrated guess, not counted)' if guess else ''}", 0 if guess else imp[k] * w))
    if imp["perf"] and x.get("ptier") in PERF_TIER_W:
        parts.append((f"perf tier {x['ptier']}", PERF_TIER_W[x["ptier"]]))
    e = effort_of(x)
    if EFFORT_W[e]:
        parts.append((f"effort {e}", EFFORT_W[e]))
    return round(sum(v for _, v in parts)), [f"{k} {v:+d}" for k, v in parts]


AUDIT = Path(__file__).with_name("audit.json")  # written by backfill.py: how much of git and the release notes the ledger covers
EVIDENCE_KEYS = ("opened", "closed", "release", "commits", "epic")


def board(ctx: dict | None = None) -> list[dict]:
    """Fold the append-only ledger into one row per quest. Dates and releases are derived from git evidence by
    backfill.py and written as edit events carrying `opened`/`closed`/`release`/`commits`; those win over the event
    timestamps (which only say when the row was typed). `logged` keeps the typed date for audit."""
    q: dict[str, dict] = {}
    ev: dict[str, dict] = {}
    for e in _read(QUESTS):
        if e["event"] == "add":
            q[e["id"]] = {**e, "status": "open", "after": _afters(e), "logged": e["ts"]}
        elif e["id"] not in q:
            continue
        elif e["event"] in ("edit", "move"):
            q[e["id"]].update({k: e[k] for k in ("title", "why", "horizon", "kind", "track", "user", "src", "origin", "effort", "rate", "ptier") if k in e})
            ev.setdefault(e["id"], {}).update({k: e[k] for k in EVIDENCE_KEYS if k in e})
            if "after" in e:
                q[e["id"]]["after"] = _afters(e)
        elif e["event"] == "shipped":
            q[e["id"]]["release"] = e["release"]
        else:
            q[e["id"]].update(status="done" if e["event"] == "done" else "dropped", closed=e["ts"],
                              result=e.get("why", ""), release=e.get("release", ""))
    for i, o in ev.items():
        x = q[i]
        if "opened" in o:
            x["ts"] = o["opened"]
        if "closed" in o and x["status"] != "open":
            x["closed"] = o["closed"]
        if "release" in o and x["status"] == "done":
            x["release"] = o["release"]
        if "commits" in o:
            x["commits"] = o["commits"]
        if o.get("epic"):
            x["epic"] = o["epic"]
    # An epic groups quests that each earn their own XP; it earns none itself and spans its children's dates.
    for x in q.values():
        kids = [q[c] for c in x.get("epic") or [] if c in q]
        if kids:
            own = [x] if x.get("commits") else []  # an epic may also own direct commits of its own
            x["ts"] = min(k["ts"] for k in kids + own)
            if x["status"] != "open" and all(k.get("closed") for k in kids):
                x["closed"] = max(k["closed"] for k in kids + [o for o in own if o.get("closed")])
            if x["status"] == "done":
                last = max(kids, key=lambda k: k.get("closed") or "")
                x["release"] = last.get("release", "") if all(k.get("release") for k in kids) else ""
    out = list(q.values())
    for x in out:
        x["skills"] = ALWAYS + TRACKS[x["track"]]
        x["waiting_on"] = [a for a in x["after"] if q.get(a, {}).get("status") != "done"]
        x["blocked"] = bool(x["status"] == "open" and x["waiting_on"])
        x["unlocks"] = sum(x["id"] in y["after"] for y in out if y["status"] == "open")
        x["xp"] = KINDS[x["kind"]] if x["status"] == "done" and not x.get("epic") else 0
        x["shipped"] = bool(x["status"] == "done" and x.get("release"))
    c = {"now": datetime.now(UTC), **(ctx or {})}
    for x in out:
        x["origin"] = origin_of(x)
        x["effort"] = effort_of(x)
        x["score"], x["score_why"] = score(x, c) if x["status"] == "open" else (0, [])
    return out


def ready(b: list[dict]) -> list[dict]:
    """Open and unblocked; best priority score first (ties: oldest quest). A quest flagged `user` has a proposed decision
    awaiting the owner's approval: it still ranks, it is just not built until approved."""
    r = [x for x in b if x["status"] == "open" and not x["blocked"]]
    return sorted(r, key=lambda x: (-x["score"], int(x["id"][1:])))


def _gh(*a: str):
    r = subprocess.run(["gh", *a], cwd=ROOT, capture_output=True, text=True, check=False)
    if r.returncode:
        raise RuntimeError(r.stderr.strip() or "gh failed")
    return json.loads(r.stdout)


def platform_of(name: str) -> str:
    n = name.lower()
    return ("mac" if n.endswith(".dmg") else "linux" if "appimage" in n else
            "windows arm" if "windows" in n and "arm" in n else "windows" if "windows" in n else "other")


def github() -> dict:
    repo = _gh("api", f"repos/{REPO}")
    rels = _gh("api", f"repos/{REPO}/releases?per_page=100")
    issues = _gh("api", f"repos/{REPO}/issues?state=all&per_page=100")
    releases = []
    for r in rels:
        body = r.get("body") or ""
        bullets = [re.sub(r"\*\*", "", b).strip() for b in re.findall(r"^\s*[-*] (.+)$", body, re.M)]
        plat: dict[str, int] = {}
        for a in r["assets"]:
            plat[platform_of(a["name"])] = plat.get(platform_of(a["name"]), 0) + a["download_count"]
        releases.append({"tag": r["tag_name"], "date": r["published_at"][:10], "downloads": sum(plat.values()),
                         "platforms": plat, "fixes": sum(b.lower().startswith("fix") for b in bullets),
                         "features": sum(not b.lower().startswith("fix") for b in bullets), "notes": bullets, "body": body,
                         "url": r["html_url"]})
    real_issues = [i for i in issues if "pull_request" not in i]
    prs = [i for i in issues if "pull_request" in i]
    now = datetime.now(UTC)
    def days(a, b=None):
        t = lambda x: datetime.fromisoformat(x.replace("Z", "+00:00"))
        return ((t(b) if b else now) - t(a)).days
    closed = [i for i in real_issues if i["state"] == "closed"]
    real_open = [i for i in real_issues if i["state"] == "open"]
    ttc = sorted(days(i["created_at"], i["closed_at"]) for i in closed)
    return {"stars": repo["stargazers_count"], "forks": repo["forks_count"], "watchers": repo["subscribers_count"],
            "releases": releases, "downloads": sum(r["downloads"] for r in releases),
            "open_issues": [{"n": i["number"], "title": i["title"], "age": days(i["created_at"]),
                             "bug": any("bug" in lb["name"].lower() for lb in i["labels"])} for i in real_open],
            "issues_closed": len(closed), "issues_total": len(real_issues),
            "median_days_to_close": ttc[len(ttc) // 2] if ttc else 0,
            "prs": [{"n": i["number"], "title": i["title"], "state": "open" if i["state"] == "open" else
                     ("merged" if i["pull_request"].get("merged_at") else "closed"), "age": days(i["created_at"]),
                     "by": i["user"]["login"]} for i in prs]}


def snapshot(gh: dict) -> None:
    today = _now()[:10]
    if any(r.get("kind") == "github" and r["ts"][:10] == today for r in _read(METRICS)):
        return
    _append(METRICS, {"kind": "github", "stars": gh["stars"], "downloads": gh["downloads"], "forks": gh["forks"]})


HEALTH_DAYS = 30


def health_of(gh: dict, b: list[dict] | None = None) -> tuple[int, list[str]]:
    """0-100 software health = base - debt + momentum, every line listed so it can be argued with.

    DEBT (what is broken or waiting): every open GitHub issue costs more the longer it sits (bugs 1.5x); every open PR
    nobody answered for a week costs a little; a slow median time-to-close costs up to 10; and every open fix quest
    on the board (bugs we found while working, fix briefs) costs by its intensity (func rating 0-3, default 2) and
    age. Issue-sourced quests are skipped there, GitHub already counts them.
    MOMENTUM (what we did lately): fixes closed in the last 30 days earn by intensity (a crash fix beats a nit), features
    and growth closed earn a flat amount, and the ready upcoming pipeline (open feature/growth quests due now or next,
    scored by their user and ux ratings) earns a little for potential. Each credit is capped so volume cannot hide debt."""
    b = b or []
    now = datetime.now(UTC)
    base, why = 75.0, ["base 75"]
    debt = 0.0
    for i in gh["open_issues"]:
        c = min(30.0, 8 + i["age"] * 0.6) * (1.5 if i["bug"] else 1)
        debt += c
        why.append(f"open issue #{i['n']} ({i['age']}d{', bug' if i['bug'] else ''}): -{round(c)}")
    for p in gh["prs"]:
        if p["state"] == "open" and p["age"] > 7:
            c = min(8.0, 2 + (p["age"] - 7) * 0.2)
            debt += c
            why.append(f"PR #{p['n']} waiting {p['age']}d: -{round(c)}")
    slow = min(10.0, max(0.0, gh["median_days_to_close"] - 2) * 1.0)
    if slow:
        debt += slow
        why.append(f"median {gh['median_days_to_close']}d to close an issue: -{round(slow)}")
    intensity = lambda x: 1 + (x.get("rate") or {}).get("func", 2)          # 1..4
    for x in b:
        if x["status"] == "open" and x["kind"] == "fix" and not (x.get("src") or "").startswith(("issue:", "pr:")):
            age = (now - datetime.fromisoformat(x["ts"])).days
            c = min(20.0, (2 + 2 * intensity(x)) * (1 + min(age, 60) / 60))
            debt += c
            why.append(f"open fix {x['id']} (intensity {intensity(x)}/4, {age}d): -{round(c)}")
    recent = lambda x: x["status"] == "done" and (now - datetime.fromisoformat(x["closed"])).days < HEALTH_DAYS
    fixes = [x for x in b if recent(x) and x["kind"] == "fix"]
    feats = [x for x in b if recent(x) and x["kind"] in ("feature", "growth")]
    pipe = [x for x in b if x["status"] == "open" and x["kind"] in ("feature", "growth") and x["horizon"] != "later"
            and not x["blocked"]]
    credit = 0.0
    if fixes:
        c = min(12.0, sum(1 + intensity(x) for x in fixes) * 0.1)
        credit += c
        why.append(f"{len(fixes)} fixes closed in {HEALTH_DAYS}d, weighted by intensity: +{round(c)}")
    if feats:
        c = min(8.0, len(feats) * 0.1)
        credit += c
        why.append(f"{len(feats)} features/growth closed in {HEALTH_DAYS}d: +{round(c)}")
    if pipe:
        c = min(5.0, sum(((x.get("rate") or {}).get("user", 1) + (x.get("rate") or {}).get("ux", 0)) for x in pipe) * 0.5)
        credit += c
        why.append(f"{len(pipe)} upcoming features due now/next, by user/ux value: +{round(c)}")
    h = max(0, min(100, round(base - debt + credit)))
    return h, why + [f"= {h}"]


def level(v: dict) -> int:
    lv = 0
    for i, (_, need) in enumerate(LEVELS):
        if all(v.get(k, 0) >= n for k, n in need.items()):
            lv = i
        else:
            break
    return lv


def git_state() -> dict:
    def g(*a):
        return subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True, check=False).stdout.strip()
    raw = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True, check=False).stdout
    dirty = [{"st": ln[:2].strip() or "?", "path": ln[3:]} for ln in raw.splitlines()]
    branches = []
    for b in g("for-each-ref", "--format=%(refname:short)", "refs/heads").splitlines():
        ahead = int(g("rev-list", "--count", f"main..{b}") or 0)
        if b != "main" and ahead and not b.startswith("backup/"):
            branches.append({"name": b, "ahead": ahead, "last": g("log", "-1", "--format=%ad|%s", "--date=short", b),
                             "commits": g("log", "--format=%h %s", "-8", f"main..{b}").splitlines()})
    return {"commit": g("rev-parse", "--short", "HEAD"), "branch": g("branch", "--show-current"),
            "commits": int(g("rev-list", "--count", "main") or 0), "dirty": dirty, "branches": branches}


def _board_ctx(gh: dict | None, branches: list[str], levelup: bool) -> dict:
    """Same scoring context everywhere (page, button, CLI): open issues and PRs from GitHub when available."""
    ctx = {"branches": branches, "levelup": levelup}
    if gh:
        ctx["issues"] = {i["n"]: i for i in gh["open_issues"]}
        ctx["prs"] = {p["n"]: p for p in gh["prs"] if p["state"] == "open"}
    return ctx


def quest_prompt(x: dict) -> str:
    why = next((q.get("why", "") for q in _read(QUESTS) if q.get("id") == x["id"]), "")
    return (f"Pursue the next quest, {x['id']}: {x['title']}. Why: {why} Follow AGENTS.md: triage, load the relevant skills, "
            "branch per git-branch-workflow, build, run the gates, then ask me before merging. Mark it done on the roadmap after the merge.")


MILESTONE = Path(__file__).with_name("milestone.json")  # hand-edited each release: target version, goals, still-open quest ids


def milestone(b: list[dict]) -> dict | None:
    """Progress toward the next release. Done = closed quests with no release tag yet (growth quests and epics are not
    release content); scope = those plus the open quest ids listed per goal. The first goal whose rule matches claims a quest."""
    if not MILESTONE.exists():
        return None
    m = json.loads(MILESTONE.read_text())
    by_id = {x["id"]: x for x in b}
    goals = [{"key": g["key"], "label": g["label"], "done": [], "open": [i for i in g.get("open", []) if by_id.get(i, {}).get("status") == "open"]} for g in m["goals"]]
    for x in b:
        if x["status"] != "done" or x.get("release") or x.get("epic") or x["kind"] == "growth":
            continue
        for g, spec in zip(goals, m["goals"]):
            hit = (spec.get("match") and re.search(spec["match"], x["title"] + " " + x.get("src", ""), re.I)) or x["kind"] in spec.get("kinds", [])
            if hit:
                g["done"].append(x["id"])
                break
    for g in goals:
        g["total"] = len(g["done"]) + len(g["open"])
        g["pct"] = round(100 * len(g["done"]) / g["total"]) if g["total"] else 100
    done, total = sum(len(g["done"]) for g in goals), sum(g["total"] for g in goals)
    return {"version": m["version"], "since": m["since"], "theme": m.get("theme", ""), "goals": goals,
            "done": done, "total": total, "pct": round(100 * done / total) if total else 100}


def build(gh: dict) -> dict:
    snapshot(gh)
    rows = _read(METRICS)
    ga = [r for r in rows if r.get("kind") == "ga"]
    hist = [r for r in rows if r.get("kind") == "github"]
    last_ga = ga[-1] if ga else None
    vals = {"stars": gh["stars"], "downloads": gh["downloads"], "sessions": last_ga["sessions"] if last_ga else 0}
    lv = level(vals)
    nxt = LEVELS[lv + 1] if lv + 1 < len(LEVELS) else None
    g = git_state()
    b = board(_board_ctx(gh, [x["name"] for x in g["branches"]], nxt is not None))
    r = ready(b)
    health, why_h = health_of(gh, b)
    lives = round(health / 100 * LIVES_MAX)
    return {
        "generated": datetime.now(IST).strftime("%Y-%m-%d %H:%M IST"), "git": g, "gh": gh, "vals": vals,
        "level": lv, "levels": LEVELS, "next": nxt, "ga": last_ga, "ga_hist": ga, "hist": hist,
        "quests": b, "milestone": milestone(b), "quest": f"{r[0]['id']} {r[0]['title']}" if r else "Board is clear. Add the next quest.",
        "prompt": quest_prompt(r[0]) if r else "",
        "ranked": [{k: x[k] for k in ("id", "title", "track", "kind", "horizon", "origin", "effort", "score", "score_why")}
                   for x in r[:10]], "needs_you": [{"id": x["id"], "title": x["title"]} for x in b if x["status"] == "open" and x.get("user")],
        "audit": json.loads(AUDIT.read_text()) if AUDIT.exists() else None,
        "xp": sum(x["xp"] for x in b), "lives": lives, "lives_max": LIVES_MAX, "health": health, "health_why": why_h, "tracks": list(TRACKS), "horizons": HORIZONS,
    }


PLAN_IGNORE = {"ideas.md", "prior-art-scout-prompt.md"}
PLAN_TRACK = {"arrangement": "timeline", "audio": "nodes", "field": "field", "geometry": "nodes", "linux": "platform",
              "perf": "engine", "prediction": "learn", "website": "growth", "launch-video": "growth"}


def _title_of(p: Path) -> str:
    f = p / "README.md" if p.is_dir() else p
    if f.exists():
        for ln in f.read_text(errors="ignore").splitlines():
            if ln.startswith("#"):
                return ln.lstrip("# ").strip()[:90]
    return p.stem.replace("-", " ")


def sync(gh: dict | None = None) -> list[str]:
    """Keep the board in step with the world. Every docs/plans/* entry, docs/fix-briefs/* file, open GitHub issue and
    open pull request is a quest keyed by `src`; new sources add a quest, and a closed issue/merged PR closes its quest."""
    log: list[str] = []
    q = board()
    have = {x.get("src"): x for x in q if x.get("src")}

    def new(src, **kw):
        nid = f"R{sum(e['event'] == 'add' for e in _read(QUESTS)) + 1}"
        _append(QUESTS, {"event": "add", "id": nid, "after": [], "user": False, "src": src, **kw})
        log.append(f"+ {nid} {kw['title']}")

    plans = ROOT / "docs/plans"
    for e in sorted(plans.iterdir()) if plans.exists() else []:
        if e.name.startswith(".") or e.name in PLAN_IGNORE or f"plan:{e.name}" in have:
            continue
        new(f"plan:{e.name}", track=PLAN_TRACK.get(e.name, "engine"), kind="feature", horizon="later",
            title=f"Plan: {_title_of(e)}", why=f"docs/plans/{e.name}")
    fb = ROOT / "docs/fix-briefs"
    for e in sorted(fb.glob("*.md")) if fb.exists() else []:
        if f"brief:{e.name}" not in have:
            new(f"brief:{e.name}", track="engine", kind="fix", horizon="next", title=f"Fix brief: {_title_of(e)}",
                why=f"docs/fix-briefs/{e.name}")
    if gh:
        for i in gh["open_issues"]:
            if f"issue:{i['n']}" not in have:
                new(f"issue:{i['n']}", track="ux", kind="fix", horizon="now", title=f"Issue #{i['n']}: {i['title']}",
                    why="reported on GitHub; costs health until closed")
        for p in gh["prs"]:
            k = f"pr:{p['n']}"
            if p["state"] == "open" and k not in have:
                new(k, track="platform", kind="infra", horizon="now", title=f"Review PR #{p['n']}: {p['title']}",
                    why=f"opened by {p['by']}; unanswered PRs cost health")
        open_i = {f"issue:{i['n']}" for i in gh["open_issues"]}
        open_p = {f"pr:{p['n']}" for p in gh["prs"] if p["state"] == "open"}
        for src, x in have.items():
            if x["status"] != "open" or not src.startswith(("issue:", "pr:")) or src in open_i | open_p:
                continue
            n = int(src.split(":")[1])
            pr = next((p for p in gh["prs"] if p["n"] == n), None)
            ev = "drop" if pr and pr["state"] == "closed" else "done"
            _append(QUESTS, {"event": ev, "id": x["id"], "why": "closed on GitHub"})
            log.append(f"{'x' if ev == 'drop' else 'v'} {x['id']} closed on GitHub")
    return log


def write_history(gh: dict) -> Path:
    out = ROOT / "docs/releases/HISTORY.md"
    parts = ["# Release history\n", "Generated by `python3 tools/roadmap/roadmap.py build` from the GitHub release notes. "
             "Do not edit by hand.\n"]
    for r in gh["releases"]:
        parts.append(f"\n## {r['tag']} ({r['date']})\n\n{r['body'].strip() or '(no notes)'}\n")
    out.parent.mkdir(exist_ok=True)
    out.write_text("\n".join(parts))
    return out


def ga_pull(days: int = 30, max_age_min: int = 15) -> str:
    """Fetch GA4 through ga_pull.py (uv) and log a `ga` row. Skips if a row is fresh. Returns a status line."""
    last = next((r for r in reversed(_read(METRICS)) if r.get("kind") == "ga"), None)
    if last and (datetime.now(UTC) - datetime.fromisoformat(last["ts"])).total_seconds() < max_age_min * 60:
        return "GA fresh"
    r = subprocess.run(["uv", "run", "--quiet", str(HERE / "ga_pull.py"), str(days)], capture_output=True, text=True,
                       check=False, timeout=120)
    if r.returncode:
        return "GA failed: " + (r.stderr.strip().splitlines() or ["?"])[-1]
    d = json.loads(r.stdout)
    _append(METRICS, {"kind": "ga", "sessions": d["sessions"], "users": d["users"], "days": d["days"], "daily": d["daily"], "dl": d["dl"], "dl_os": d["dl_os"]})
    return f"GA {d['sessions']} sessions / {d['users']} users / {d['days']}d"


def serve(port: int) -> None:
    """Thin file server for roadmap.html, the same shape as groww-finance's dashboard.

    The page is always built by a fresh `roadmap.py build` process, never inside this one, so a long-running
    server can't serve stale code. Before each GET, if roadmap.html is older than the ledger, template or
    roadmap.py (or over 10 min old, for stars and downloads), it is rebuilt first. The page reloads itself
    every 20 s while visible.
    """
    import secrets
    import threading
    import time
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    token = secrets.token_urlsafe(16)  # per launch; the page carries it, other sites in the browser cannot know it
    lock = threading.Lock()
    me = [sys.executable, str(Path(__file__).resolve())]

    def run(cmd: list[str], timeout: int = 180) -> str:
        r = subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=timeout, cwd=HERE.parent.parent)
        lines = (r.stdout + r.stderr).strip().splitlines()
        return lines[-1] if lines else "done"

    def fresh(force: bool = False) -> None:
        with lock:
            src = max(p.stat().st_mtime for p in (TEMPLATE, QUESTS, Path(__file__).resolve()))
            out = OUT.stat().st_mtime if OUT.exists() else 0.0
            if force or out < src or time.time() - out > 600:
                run(me + ["build"])

    class H(BaseHTTPRequestHandler):
        def _reply(self, code: int, msg: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"ok": code == 200, "msg": msg}).encode())

        def do_POST(self):
            host = self.headers.get("Host", "")
            if self.headers.get("X-Token") != token or host not in (f"localhost:{port}", f"127.0.0.1:{port}"):
                return self._reply(403, "bad token")
            if self.path == "/refresh":
                out = [run(me + ["ga-pull"]), run([sys.executable, str(HERE / "backfill.py")], timeout=120)]
                fresh(force=True)
                return self._reply(200, " · ".join(out))
            self._reply(404, "no route")

        def do_GET(self):
            try:
                fresh()
            except Exception as e:  # keep serving the last good page
                print(f"build failed: {e}", flush=True)
            html = OUT.read_bytes() if OUT.exists() else b"build failed: no roadmap.html yet"
            html = html.replace(b"__TOKEN__", token.encode())
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(html)))
            self.end_headers()
            self.wfile.write(html)

        def log_message(self, *a):
            pass

    print(f"live at http://localhost:{port}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()


def show() -> None:
    try:
        gh = github()
    except Exception:
        gh = None
    b = board(_board_ctx(gh, [x["name"] for x in git_state()["branches"]], True))
    for h in HORIZONS:
        for x in b:
            if x["status"] == "open" and x["horizon"] == h:
                tag = "LOCKED" if x["blocked"] else ("PROPOSED" if x.get("user") else h.upper())
                print(f"{x['id']:<4} [{x['track']:<8}] {x['kind']:<7} {tag:<9} {x['score']:>3}  {x['title']}")
    r = ready(b)
    print(f"-- {sum(x['status'] == 'open' for x in b)} open, {sum(x['status'] == 'done' for x in b)} done, "
          f"{sum(x['xp'] for x in b)} XP" + (f"; next: {r[0]['id']} {r[0]['title']} (score {r[0]['score']}: {', '.join(r[0]['score_why'])})" if r else ""))


def _rate_kw(args) -> dict:
    out = {}
    if args.rate:
        r = dict(kv.split("=") for kv in args.rate.split(","))
        if bad := [k for k in r if k not in RATE_W]:
            sys.exit(f"unknown axis {bad}; use {list(RATE_W)}")
        out["rate"] = {k: int(v) for k, v in r.items()}
    if getattr(args, "ptier", None):
        out["ptier"] = args.ptier
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    bp = sub.add_parser("build")
    bp.add_argument("--open", action="store_true")
    a = sub.add_parser("add")
    a.add_argument("--track", required=True, choices=list(TRACKS))
    a.add_argument("--kind", default="feature", choices=list(KINDS))
    a.add_argument("--horizon", default="next", choices=HORIZONS)
    a.add_argument("--title", required=True)
    a.add_argument("--why", required=True)
    a.add_argument("--after", default="")
    a.add_argument("--user", action="store_true", help="proposed decision awaiting owner approval (still ranked, not built until approved)")
    a.add_argument("--origin", choices=ORIGINS, default="ask", help="ask=owner asked, explore=found while investigating, bug, idea, ...")
    a.add_argument("--effort", choices=list(EFFORT_W), default="M")
    a.add_argument("--rate", default="", help="impact 0-3 per axis, e.g. user=3,func=2,perf=0,ux=1")
    a.add_argument("--ptier", choices=list(PERF_TIER_W), help="perf work only: audio > projector > canvas > preview")
    sub.add_parser("start").add_argument("id")
    for n in ("done", "drop"):
        d = sub.add_parser(n)
        d.add_argument("id")
        d.add_argument("--why", required=True)
        if n == "done":
            d.add_argument("--release", default="")
            d.add_argument("--commits", default="", help="evidence: shas that did the work (dates/release derive from them)")
    m = sub.add_parser("move")
    m.add_argument("id")
    m.add_argument("--horizon", required=True, choices=HORIZONS)
    g = sub.add_parser("ga")
    g.add_argument("--sessions", type=int, required=True)
    g.add_argument("--users", type=int, default=0)
    g.add_argument("--days", type=int, default=30)
    sub.add_parser("show")
    ed = sub.add_parser("edit")
    ed.add_argument("id")
    ed.add_argument("--origin", choices=ORIGINS)
    ed.add_argument("--effort", choices=list(EFFORT_W))
    ed.add_argument("--rate", default="")
    ed.add_argument("--ptier", choices=list(PERF_TIER_W))
    ed.add_argument("--title")
    ed.add_argument("--after", help="comma-separated quest ids this quest waits on (replaces the list)")
    ed.add_argument("--src", help="source key, e.g. merge:<sha7>, so backfill treats this quest as that merge")
    ed.add_argument("--user", dest="needs_user", action="store_true", default=None, help="proposed decision awaiting owner approval; still ranked")
    ed.add_argument("--no-user", dest="needs_user", action="store_false", help="owner approved: clear the proposed flag")
    ed.add_argument("--epic", help="comma-separated child quest ids: this quest becomes an epic (0 XP, spans the children)")
    lk = sub.add_parser("link", help="attach git evidence to a quest; backfill.py derives opened/closed/release from it")
    lk.add_argument("id")
    lk.add_argument("--commits", required=True, help="comma-separated shas (a merge sha, or the direct commits)")
    sub.add_parser("sync")
    sp = sub.add_parser("ship")
    sp.add_argument("id")
    sp.add_argument("--release", required=True)
    sub.add_parser("ga-pull")
    sub.add_parser("serve").add_argument("--port", type=int, default=8765)
    args = ap.parse_args()

    if args.cmd == "build":
        print(ga_pull())
        g = github()
        for ln in sync(g):
            print(ln)
        print(write_history(g))
        tmp = OUT.with_suffix(".html.tmp")  # atomic: the server never reads a half-written page
        tmp.write_text(TEMPLATE.read_text().replace("__DATA__", json.dumps(build(g)).replace("</", "<\\/")))
        tmp.replace(OUT)
        print(OUT)
        if args.open:
            subprocess.run(["open", str(OUT)], check=False)
        return
    if args.cmd == "ga":
        _append(METRICS, {"kind": "ga", "sessions": args.sessions, "users": args.users, "days": args.days})
        print(f"logged GA: {args.sessions} sessions / {args.days} days")
        return
    if args.cmd == "ga-pull":
        print(ga_pull(max_age_min=0))
        return
    if args.cmd == "serve":
        serve(args.port)
        return
    if args.cmd == "sync":
        print("\n".join(sync(github())) or "board already in step")
        return
    if args.cmd == "show":
        show()
        return
    q = {x["id"]: x for x in board()}
    if args.cmd == "add":
        after = [x.strip() for x in args.after.split(",") if x.strip()]
        if unknown := [x for x in after if x not in q]:
            sys.exit(f"unknown quest(s) in --after: {unknown}")
        nid = f"R{sum(e['event'] == 'add' for e in _read(QUESTS)) + 1}"
        _append(QUESTS, {"event": "add", "id": nid, "track": args.track, "kind": args.kind, "horizon": args.horizon,
                         "title": args.title, "why": args.why, "after": after, "user": args.user, "origin": args.origin, "effort": args.effort,
                         **_rate_kw(args)})
    elif args.cmd == "start":
        x = q.get(args.id) or sys.exit(f"no quest {args.id}")
        if x["status"] != "open" or x["blocked"]:
            sys.exit(f"{args.id} is {x['status']}" + (f", waiting on {x['waiting_on']}" if x["blocked"] else ""))
        if x.get("user"):
            print(f"{args.id} is PROPOSED: put your recommended decision to the owner (approve / change / abandon) before building")
        print(f"{x['id']} [{x['track']}] {x['title']}\n  why: {x['why']}\n  load skills: {', '.join(x['skills'])}"
              f"\n  branch: {'bugfix' if x['kind'] == 'fix' else 'feature'}/<slug> off main (git-branch-workflow)")
        return
    elif args.cmd == "edit":
        if args.id not in q:
            sys.exit(f"no quest {args.id}")
        row = {k: v for k in ("origin", "effort", "ptier", "title", "src") if (v := getattr(args, k))}
        if args.needs_user is not None:
            row["user"] = args.needs_user
        if args.epic:
            if bad := [c for c in args.epic.split(",") if c not in q]:
                sys.exit(f"unknown quest(s) in --epic: {bad}")
            row["epic"] = args.epic.split(",")
        if args.after:
            row["after"] = [x.strip() for x in args.after.split(",") if x.strip()]
            if unknown := [x for x in row["after"] if x not in q]:
                sys.exit(f"unknown quest(s) in --after: {unknown}")
        _append(QUESTS, {"event": "edit", "id": args.id, **row, **_rate_kw(args)})
    elif args.cmd == "move":
        if args.id not in q:
            sys.exit(f"no quest {args.id}")
        _append(QUESTS, {"event": "move", "id": args.id, "horizon": args.horizon})
    elif args.cmd == "link":
        if args.id not in q:
            sys.exit(f"no quest {args.id}")
        _append(QUESTS, {"event": "edit", "id": args.id, "commits": [c.strip() for c in args.commits.split(",") if c.strip()]})
    elif args.cmd == "ship":
        if args.id not in q or q[args.id]["status"] != "done":
            sys.exit(f"{args.id} must be closed (merged) before it can ship")
        _append(QUESTS, {"event": "shipped", "id": args.id, "release": args.release})
    else:
        # drop also retires a closed quest that turned out to duplicate another (it stops earning XP)
        if args.id not in q or q[args.id]["status"] not in (("open", "done") if args.cmd == "drop" else ("open",)):
            sys.exit(f"{args.id} is not an open quest")
        row = {"event": args.cmd, "id": args.id, "why": args.why}
        if args.cmd == "done" and args.release:
            row["release"] = args.release
        _append(QUESTS, row)
        if args.cmd == "done" and args.commits:
            _append(QUESTS, {"event": "edit", "id": args.id, "commits": [c.strip() for c in args.commits.split(",") if c.strip()]})
    show()


if __name__ == "__main__":
    main()
