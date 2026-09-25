#!/usr/bin/env python3
"""
brain_watchd.py
Long-running watcher (a launchd agent, see 4_engine/install_hooks.sh) that keeps the semi-brain
in sync as things change, instead of a batch sync after every commit.

  watches   .git refs/HEAD (commits, checkouts, merges), src/, .claude/skills, docs/,
            this project's Claude Code transcripts, Antigravity transcripts (FSEvents)
  debounce  a sync starts once the watched paths have been quiet for DEBOUNCE_S
  runs      the same thing the hook ran: `taskpolicy -b nice -n 19 sync_brain.py --sync`, under
            the hook's mkdir lock ($GITDIR/sync_brain.lock), so ab.sh's `pgrep -f sync_brain.py`
            wait, background QoS and lowest CPU priority all still apply. One sync at a time;
            changes that land during a sync queue exactly one more.
  throttle  a transcript-only change (a live chat) syncs at most every TRANSCRIPT_GAP_S; a git,
            code, skill or doc change syncs as soon as the debounce allows.
  deferred  the sync defers its 30-minute session refits (last_sync.json "deferred_until");
            the daemon runs one more sync when the earliest of them comes due.

The post-commit/post-merge hooks see this daemon's pidfile and step aside while it is alive.
Named brain_watchd, not sync_*, so the pgrep in scripts/bench/ab.sh only matches a running sync.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import REPO_PATH, STATE_DIR, SEMI_BRAIN_DIR  # noqa: E402
from l1.fsevents import watch  # noqa: E402

DEBOUNCE_S = 2.0
TRANSCRIPT_GAP_S = 60.0
LOCK_RETRY_S = 10.0
LOG_MAX = 1 << 20

sys.path.insert(0, str(SEMI_BRAIN_DIR / "1_extractors"))
from mine_session_history import SESSIONS_DIR as CLAUDE_DIR  # noqa: E402
from mine_antigravity_history import BRAIN_DIR as ANTIGRAVITY_DIR  # noqa: E402


def git_common_dir():
    out = subprocess.run(["git", "rev-parse", "--git-common-dir"], cwd=REPO_PATH,
                         capture_output=True, text=True).stdout.strip()
    return (REPO_PATH / out).resolve()


GITDIR = git_common_dir()
LOCK = GITDIR / "sync_brain.lock"
PIDFILE = GITDIR / "brain_watchd.pid"
LOG = STATE_DIR / "watchd.log"


def log(msg):
    try:
        if LOG.exists() and LOG.stat().st_size > LOG_MAX:
            LOG.replace(LOG.with_suffix(".log.1"))
        with open(LOG, "a") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + msg + "\n")
    except OSError:
        pass


def classify(path):
    """'git' / 'repo' / 'transcript', or None for noise (git objects/index, lock files, our own output)."""
    real = os.path.realpath(path)
    g = str(GITDIR) + "/"
    if real.startswith(g):
        rel = real[len(g):]
        if rel in ("HEAD", "packed-refs", "logs/HEAD") or rel.startswith("refs/"):
            return None if rel.endswith(".lock") else "git"
        return None
    repo = str(REPO_PATH.resolve()) + "/"
    if real.startswith(repo):
        rel = real[len(repo):]
        if rel.startswith(("src/", ".claude/skills/", "docs/")):
            return "repo"
        return None
    if real.startswith((str(CLAUDE_DIR.resolve()), str(ANTIGRAVITY_DIR.resolve()))):
        return "transcript" if real.endswith(".jsonl") else None
    return None


class Daemon:
    def __init__(self):
        self.last_event = 0.0
        self.pending = set()
        self.proc = None
        self.proc_started = 0.0
        self.last_transcript_sync = 0.0
        self.due = None
        self.retry_at = 0.0

    def _read_due(self):
        try:
            d = json.loads((STATE_DIR / "last_sync.json").read_text()).get("deferred_until") or {}
            self.due = min(d.values()) if d else None
        except (OSError, ValueError):
            self.due = None

    def on_events(self, paths):
        kinds = {k for k in map(classify, paths) if k}
        if kinds:
            self.pending |= kinds
            self.last_event = time.time()

    def _take_lock(self):
        try:
            LOCK.mkdir()
            return True
        except FileExistsError:
            pass
        # Held by a hook-started runner, or left behind by one that was killed: a lock with
        # no sync_brain.py process alive is stale.
        if subprocess.run(["pgrep", "-f", "sync_brain.py"], capture_output=True).returncode != 0:
            log("removing stale sync lock")
            try:
                LOCK.rmdir()
                LOCK.mkdir()
                return True
            except OSError:
                return False
        return False

    def _start(self, reason):
        if not self._take_lock():
            # a hook-started runner (or a stale lock) holds it; retry on a later tick
            return False
        cmd = ["taskpolicy", "-b", "nice", "-n", "19", sys.executable,
               str(SEMI_BRAIN_DIR / "4_engine" / "sync_brain.py"), "--sync"]
        out = open(STATE_DIR / "last_sync.log", "w")
        self.proc = subprocess.Popen(cmd, cwd=REPO_PATH, stdout=out, stderr=subprocess.STDOUT,
                                     stdin=subprocess.DEVNULL)
        self.proc_started = time.time()
        log(f"sync start ({reason})")
        return True

    def tick(self):
        now = time.time()
        if self.proc is not None:
            rc = self.proc.poll()
            if rc is None:
                return
            log(f"sync done rc={rc} in {now - self.proc_started:.1f}s")
            self.proc = None
            try:
                LOCK.rmdir()
            except OSError:
                pass
            self._read_due()
        if self.due is not None and now >= self.due:
            self.pending.add("deferred")
            self.due = None
        if not self.pending or now - self.last_event < DEBOUNCE_S:
            return
        if self.pending == {"transcript"} and now - self.last_transcript_sync < TRANSCRIPT_GAP_S:
            return
        if now < self.retry_at:
            return
        reason = ",".join(sorted(self.pending))
        if self._start(reason):
            self.pending = set()
            self.last_transcript_sync = now
        else:
            # Another worktree's hook runner (they share this git dir, so the lock) holds it.
            if not self.retry_at:
                log("sync lock held by another runner; waiting")
            self.retry_at = now + LOCK_RETRY_S
            return
        self.retry_at = 0.0


def main():
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    PIDFILE.write_text(str(os.getpid()))
    paths = [str(p.resolve()) for p in (GITDIR, REPO_PATH / "src", REPO_PATH / ".claude" / "skills",
                                        REPO_PATH / "docs", CLAUDE_DIR, ANTIGRAVITY_DIR)
             if p.exists()]
    d = Daemon()
    d.pending = {"startup"}  # catch up on whatever changed while the daemon was down
    log(f"watching {len(paths)} paths, pid {os.getpid()}")
    try:
        watch(paths, d.on_events, latency=0.5, tick=d.tick, tick_every=0.5)
    finally:
        try:
            if PIDFILE.read_text() == str(os.getpid()):
                PIDFILE.unlink()
        except OSError:
            pass


if __name__ == "__main__":
    main()
