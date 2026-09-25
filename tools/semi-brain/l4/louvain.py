#!/usr/bin/env python3
"""
louvain.py
L4: deterministic Louvain local-move community detection, shared by clusters.py (file-level
areas) and regions.py (symbol-level regions of main.cpp). Fixed node order, ties go to the
smaller community id, so the same adjacency always yields the same communities.
"""


def louvain(adj, resolution=1.0, passes=20):
    """adj: {node: {neighbor: weight}}, symmetric. Returns {node: community_id}."""
    nodes = sorted(adj)
    deg = {n: sum(adj[n].values()) for n in nodes}
    m2 = sum(deg.values())  # 2m
    comm = {n: i for i, n in enumerate(nodes)}
    if not m2:
        return comm
    tot = {i: deg[n] for i, n in enumerate(nodes)}
    for _ in range(passes):
        moved = False
        for n in nodes:
            c0, k = comm[n], deg[n]
            links = {}
            for m, w in adj[n].items():
                if m != n:
                    links[comm[m]] = links.get(comm[m], 0.0) + w
            tot[c0] -= k
            best, gain = c0, links.get(c0, 0.0) - resolution * k * tot[c0] / m2
            for c, l in sorted(links.items()):
                g = l - resolution * k * tot[c] / m2
                if g > gain + 1e-12:
                    best, gain = c, g
            tot[best] += k
            if best != c0:
                comm[n] = best
                moved = True
        if not moved:
            break
    return comm
