#!/usr/bin/env python3
"""
clusters.py
L4: areas of the code, found in the L3 network.

  co-change   two files touched by the same commit (commits over MAX_COMMIT_FILES files are
              sweeping refactors/merges and say little about what belongs together); a commit
              of n files adds 1/(n-1) to each of its pairs
  includes    a file including another adds INCLUDE_W
  areas       modularity communities of that file graph (Louvain local moves, RESOLUTION),
              deterministic: fixed node order, ties to the smaller community id. Plain label
              propagation collapses into one area around hub files like main.cpp; modularity
              discounts an edge by how connected both ends already are.

Ranking uses co-change only: pulling in files that usually change with the best candidates
lifted the replay gate 0.5415 -> 0.568, while pooling evidence per area lowered it (0.5363), so
area_w defaults to 0. The areas themselves name the part of the code a result sits in (L5).

Built from the network's own commit -> file edges and the AST includes, so it needs no data of
its own and is always as current as the network (the replay gets it as of each past commit).
"""

from collections import defaultdict

MAX_COMMIT_FILES = 20
INCLUDE_W = 0.5
RESOLUTION = 1.0
PASSES = 20


class Areas:
    def __init__(self, network):
        self.adj = defaultdict(lambda: defaultdict(float))
        for doc_id, edges in network.doc_edges.items():
            if not doc_id.startswith("commit::"):
                continue
            files = [d for kind, d, _ in edges if kind == "touches"]
            if not 2 <= len(files) <= MAX_COMMIT_FILES:
                continue
            w = 1.0 / (len(files) - 1)
            for i, a in enumerate(files):
                for b in files[i + 1:]:
                    self.adj[a][b] += w
                    self.adj[b][a] += w
        for f, incs in network.code.includes.items():
            for g in incs:
                if g != f:
                    self.adj[f][g] += INCLUDE_W
                    self.adj[g][f] += INCLUDE_W
        self.area = self._propagate()
        self.members = defaultdict(list)
        for f, a in self.area.items():
            self.members[a].append(f)

    def _propagate(self):
        nodes = sorted(self.adj)
        deg = {n: sum(self.adj[n].values()) for n in nodes}
        m2 = sum(deg.values())  # 2m
        comm = {n: i for i, n in enumerate(nodes)}
        tot = {i: deg[n] for i, n in enumerate(nodes)}
        if not m2:
            return comm
        for _ in range(PASSES):
            moved = False
            for n in nodes:
                c0, k = comm[n], deg[n]
                links = defaultdict(float)
                for m, w in self.adj[n].items():
                    if m != n:
                        links[comm[m]] += w
                tot[c0] -= k
                # modularity gain of joining c: links_to_c - resolution * k * tot_c / 2m
                best, gain = c0, links.get(c0, 0.0) - RESOLUTION * k * tot[c0] / m2
                for c, l in sorted(links.items()):
                    g = l - RESOLUTION * k * tot[c] / m2
                    if g > gain + 1e-12:
                        best, gain = c, g
                tot[best] += k
                if best != c0:
                    comm[n] = best
                    moved = True
            if not moved:
                break
        return comm

    def rerank(self, scores, top_n=10, area_w=0.0, cochange_w=0.3, expand_from=5):
        """scores: {file: score}. Returns new scores: each file's own score (top = 1), plus its
        area's pooled score over the top_n files, plus co-change with the expand_from best files
        (which can add files that had no score)."""
        if not scores:
            return {}
        top = max(scores.values())
        s = {f: v / top for f, v in scores.items()}
        best = sorted(s, key=s.get, reverse=True)
        pooled = defaultdict(float)
        for f in best[:top_n]:
            if f in self.area:
                pooled[self.area[f]] += s[f]
        pmax = max(pooled.values(), default=0.0)
        co = defaultdict(float)
        for f in best[:expand_from]:
            nbrs = self.adj.get(f)
            if not nbrs:
                continue
            norm = max(nbrs.values())
            for g, w in nbrs.items():
                co[g] += s[f] * w / norm
        cmax = max(co.values(), default=0.0)
        out = {}
        for f in set(s) | set(co):
            v = s.get(f, 0.0)
            if pmax and f in self.area:
                v += area_w * pooled.get(self.area[f], 0.0) / pmax
            if cmax:
                v += cochange_w * co.get(f, 0.0) / cmax
            out[f] = v
        return out
