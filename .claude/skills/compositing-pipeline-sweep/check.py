#!/usr/bin/env python3
"""Static half of the compositing/2D sweep.

Two rules that every image node in src/nodes/ has to follow for the 2D chain to
stay correct, both checkable without a build:

  1. COOK MEMO.   CookIfNeeded(frameId) is called once per *edge* into a node,
     not once per node - a node feeding three downstream nodes is asked to cook
     three times in the same frame. Every implementation must therefore memo on
     the frame id and return early. Missing it is not just slow: a node that
     re-runs a ping-pong / accumulation pass (Feedback, Trails, Reaction
     Diffusion, Resynth) advances its state once per consumer, so the image
     changes depending on how many cables leave the node.

  2. PULL FRAME.  A Pull() inside a cook must be handed that cook's own frameId
     argument. Passing a literal, a member, or frameId+/-1 re-cooks the
     upstream node against a different frame than the graph is on, which
     defeats the memo above in exactly the way rule 1 is trying to prevent.

Both are currently clean; this is a regression gate on new nodes.
Exit 0 = clean, 1 = at least one violation.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))

DEF_RE = re.compile(r'(?:(\w+)::)?CookIfNeeded\(int (\w+)\)')
# A member latched with the frame id, under any of the names the codebase uses.
MEMO_RE = r'(==\s*{arg}\b|\b{arg}\s*==|Last\w*Frame|mCookedFrame)'


def bodies(path):
    """Yield (line_no, owner, arg, body) for each CookIfNeeded *definition*."""
    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    for i, line in enumerate(lines):
        m = DEF_RE.search(line)
        if not m:
            continue
        # a pure declaration ends in ';' with no body on the same line
        if ';' in line and '{' not in line:
            continue
        owner, arg = m.group(1), m.group(2)
        depth, body, j = 0, [], i
        while j < len(lines):
            body.append(lines[j])
            depth += lines[j].count('{') - lines[j].count('}')
            if depth <= 0 and '{' in '\n'.join(body):
                break
            j += 1
        yield i + 1, owner or os.path.basename(path), arg, '\n'.join(body)


def main():
    files = sorted(glob.glob(os.path.join(ROOT, 'src/nodes/*.cpp')) +
                   glob.glob(os.path.join(ROOT, 'src/nodes/*.h')) +
                   glob.glob(os.path.join(ROOT, 'src/core/*.h')))
    cooks = pulls = 0
    problems = []
    for path in files:
        rel = os.path.relpath(path, ROOT)
        for line_no, owner, arg, body in bodies(path):
            cooks += 1
            if not re.search(MEMO_RE.format(arg=arg), body):
                problems.append('%s:%d %s::CookIfNeeded does not memoize on %s '
                                '- it will re-run once per downstream cable'
                                % (rel, line_no, owner, arg))
            for pm in re.finditer(r'\.Pull\(([^)]*)\)', body):
                pulls += 1
                passed = pm.group(1).strip()
                if passed != arg:
                    problems.append('%s:%d %s::CookIfNeeded calls Pull(%s) but '
                                    'its frame id is %s'
                                    % (rel, line_no, owner, passed, arg))

    for p in problems:
        print('  ' + p)
    print('COMPOSITINGSWEEP %d cook bodies, %d Pull calls, %d problem(s): %s'
          % (cooks, pulls, len(problems), 'PROBLEMS' if problems else 'OK'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
