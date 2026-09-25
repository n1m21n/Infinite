#!/usr/bin/env python3
"""
gitdata.py
Git plumbing shared by the replay benchmark: commit listing with timestamps, trees at a
commit, blob contents in one `git cat-file --batch` process, and a content-keyed parse cache
so walking 100+ parent trees only parses each file version once.
"""

import json
import sqlite3
import subprocess
import sys
from pathlib import Path

SEMI_BRAIN_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SEMI_BRAIN_DIR))
sys.path.insert(0, str(SEMI_BRAIN_DIR / "1_extractors"))
from l1 import REPO_PATH, STATE_DIR  # noqa: E402

CODE_EXTS = (".cpp", ".h", ".mm")


def git(*args, input_bytes=None):
    return subprocess.run(["git", *args], cwd=REPO_PATH, input=input_bytes,
                          capture_output=True, check=True).stdout


def list_commits(rev="HEAD", no_merges=False):
    """All commits reachable from rev, newest first, with committer time and parents."""
    fmt = "%H%x1f%P%x1f%ct%x1f%an%x1f%ae%x1f%ad%x1f%s%x1f%b%x1e"
    args = ["log", rev, f"--format={fmt}"]
    if no_merges:
        args.insert(1, "--no-merges")
    out = git(*args).decode("utf-8", "replace")
    commits = []
    for rec in out.split("\x1e"):
        rec = rec.strip("\n")
        if not rec:
            continue
        h, parents, ct, an, ae, ad, subject, body = rec.split("\x1f", 7)
        commits.append({"hash": h, "parents": parents.split(), "time": int(ct), "author_name": an,
                        "author_email": ae, "author_date": ad, "subject": subject, "body": body.strip()})
    return commits


class BlobReader:
    """One long-lived `git cat-file --batch` for reading many blobs cheaply."""

    def __init__(self):
        self.proc = subprocess.Popen(["git", "cat-file", "--batch"], cwd=REPO_PATH,
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def read(self, obj):
        self.proc.stdin.write(obj.encode() + b"\n")
        self.proc.stdin.flush()
        header = self.proc.stdout.readline().split()
        if len(header) < 3 or header[1] == b"missing":
            return None
        size = int(header[2])
        data = self.proc.stdout.read(size)
        self.proc.stdout.read(1)  # trailing newline
        return data

    def close(self):
        self.proc.stdin.close()
        self.proc.wait()


def ls_tree(commit, *paths):
    """[(path, blob_sha)] for files under paths at commit."""
    out = git("ls-tree", "-r", commit, "--", *paths).decode("utf-8", "replace")
    items = []
    for line in out.splitlines():
        meta, path = line.split("\t", 1)
        _, kind, sha = meta.split()
        if kind == "blob":
            items.append((path, sha))
    return items


class ParseCache:
    """(path, blob sha) -> build_ast_graph.extract_from_bytes result, persisted in l1/state."""

    def __init__(self, db_path=STATE_DIR / "ast_blob_cache.db"):
        db_path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(db_path, timeout=60)
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("CREATE TABLE IF NOT EXISTS parsed (path TEXT, blob TEXT, result TEXT, PRIMARY KEY (path, blob))")
        self.mem = {}
        self._extract = None

    def get(self, path, blob, reader):
        k = (path, blob)
        if k in self.mem:
            return self.mem[k]
        row = self.conn.execute("SELECT result FROM parsed WHERE path=? AND blob=?", k).fetchone()
        if row:
            res = json.loads(row[0])
        else:
            if self._extract is None:
                from build_ast_graph import extract_from_bytes
                self._extract = extract_from_bytes
            data = reader.read(blob)
            res = self._extract(path, data or b"")
            self.conn.execute("INSERT OR REPLACE INTO parsed VALUES (?,?,?)", (path, blob, json.dumps(res)))
            self.conn.commit()
        self.mem[k] = res
        return res


def code_files_at(commit):
    return [(p, b) for p, b in ls_tree(commit, "src") if p.endswith(CODE_EXTS)]


def graph_order(items):
    """Same file order build_ast_graph uses: all .cpp, then .h, then .mm."""
    return sorted(items, key=lambda pb: (CODE_EXTS.index(Path(pb[0]).suffix), pb[0]))
