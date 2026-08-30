#!/usr/bin/env python3
"""Static half of the output/projection sweep: platform-seam parity.

Everything that leaves Infinite's window - Syphon/Spout publishing and
receiving, the projector window's borderless/topmost/cursor policy, the window
icon - is OS-specific, and the rule is that the OS-specific part lives behind
Platform:: and nowhere else. Two consequences are checkable without a build:

  1. TWO IMPLEMENTATIONS. Every function declared in src/platform/Platform.h
     outside a _WIN32 guard must exist in BOTH src/platform/Platform.mm and
     src/platform/win/*.cpp. A declaration implemented on one side only links
     on that OS and fails to link - or worse, silently no-ops - on the other.
     Declarations inside an `#if defined(_WIN32)` block are Windows-only by
     design and are skipped.

  2. NO _WIN32 IN THE NODE LAYER. src/nodes/ must contain no _WIN32 at all.
     A node that branches on the OS is a node that has to be re-tested per OS
     forever; the branch belongs in the Platform:: implementation instead.

Both are currently clean; this is a regression gate.
Exit 0 = clean, 1 = at least one violation.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))

DECL_RE = re.compile(
    r'^\s{0,6}(?:[\w:<>,&\*\s]+?)\b(\w+)\s*\([^;{]*\)\s*;\s*$', re.M)
NOT_A_FUNCTION = {'void', 'return', 'if', 'while', 'for', 'switch'}


def strip_win32_only(text):
    """Drop the regions of Platform.h that are Windows-only by design."""
    out, depth, skipping, skip_at = [], 0, False, 0
    for line in text.split('\n'):
        s = line.strip()
        if s.startswith('#if'):
            depth += 1
            if not skipping and '_WIN32' in s and 'defined' in s and '!' not in s:
                skipping, skip_at = True, depth
        if not skipping:
            out.append(line)
        if s.startswith('#endif'):
            if skipping and depth == skip_at:
                skipping = False
            depth -= 1
    return '\n'.join(out)


def main():
    header = open(os.path.join(ROOT, 'src/platform/Platform.h'),
                  encoding='utf-8', errors='replace').read()
    header = re.sub(r'//.*', '', header)
    decls = {n for n in DECL_RE.findall(strip_win32_only(header))
             if n not in NOT_A_FUNCTION}

    mac = open(os.path.join(ROOT, 'src/platform/Platform.mm'),
               encoding='utf-8', errors='replace').read()
    win = ''.join(open(f, encoding='utf-8', errors='replace').read()
                  for f in sorted(glob.glob(os.path.join(ROOT, 'src/platform/win/*.cpp'))))

    problems = []
    for name in sorted(decls):
        pat = re.compile(r'\b' + re.escape(name) + r'\s*\(')
        on_mac, on_win = bool(pat.search(mac)), bool(pat.search(win))
        if not (on_mac and on_win):
            problems.append('Platform::%s implemented on %s only - it will not '
                            'link, or will silently no-op, on the other OS'
                            % (name, 'macOS' if on_mac else 'Windows'))

    node_win32 = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'src/nodes/*'))):
        try:
            text = open(path, encoding='utf-8', errors='replace').read()
        except IsADirectoryError:
            continue
        for i, line in enumerate(text.split('\n')):
            if '_WIN32' in line:
                node_win32.append('%s:%d has _WIN32 - the OS branch belongs '
                                  'behind a Platform:: seam, not in a node'
                                  % (os.path.relpath(path, ROOT), i + 1))
    problems += node_win32

    for p in problems:
        print('  ' + p)
    print('OUTPUTSWEEP %d cross-platform Platform:: functions, %d node-layer '
          '_WIN32 uses, %d problem(s): %s'
          % (len(decls), len(node_win32), len(problems),
             'PROBLEMS' if problems else 'OK'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
