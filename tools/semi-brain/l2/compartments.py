#!/usr/bin/env python3
"""
compartments.py
The L2 compartment map and the merge of per-compartment result lists.

  code           ast_symbol                                   what exists now
  history        git_commit                                   what was changed, and why
  skills         skill                                        how this repo says to work
  plans          design_plan                                  designs recovered from git
  conversations  session_history, session_insight,            what was asked and decided
                 dev_trajectory                               (private)
  research       research_doc, session_research               what was learned from outside
  external       byox_blueprint                               first-principles references

The retriever ranks each compartment separately (BM25 + dense, fused by RRF inside the
compartment); merge() interleaves them by QUOTA so every compartment that matched gets a voice.
"""

COMPARTMENTS = {
    "code": ("ast_symbol",),
    "history": ("git_commit",),
    "skills": ("skill",),
    "plans": ("design_plan",),
    "conversations": ("session_history", "session_insight", "dev_trajectory"),
    "research": ("research_doc", "session_research"),
    "external": ("byox_blueprint",),
}
CATEGORY_TO_COMPARTMENT = {c: name for name, cats in COMPARTMENTS.items() for c in cats}

# How many of each compartment's best hits reach the merged context, in merge order.
QUOTA = {"code": 12, "history": 6, "skills": 2, "research": 2, "conversations": 3, "plans": 1,
         "external": 0}


def compartment_of(category):
    return CATEGORY_TO_COMPARTMENT.get(category, "external")


def merge(by_compartment, quota=None):
    """Round-robin over compartments (in QUOTA order), each contributing up to its quota."""
    quota = quota or QUOTA
    lists = {c: list(by_compartment.get(c, []))[:q] for c, q in quota.items()}
    out, seen = [], set()
    while any(lists.values()):
        for c in quota:
            if lists[c]:
                hit = lists[c].pop(0)
                if hit["doc_id"] not in seen:
                    seen.add(hit["doc_id"])
                    out.append(hit)
    return out
