#!/usr/bin/env python3
"""Drive INFINITE_PARAMFUZZ to the end on a sanitizer build.

The sanitizer build aborts at the first report, so this restarts the app
after each crash with INFINITE_PARAMFUZZ_AFTER set to the row that crashed,
until the summary line prints.

    param_fuzz_inproc.py BIN OUT.tsv [--types A,B] [--alarm SEC]

OUT.tsv rows: verdict, row, detail. A verdict is NAN, HOT or SLOW (the
selftest's own flags), CRASH (the first sanitizer line or the exit code) or
HANG (the alarm fired). Exit 1 if any row is written.
"""
import argparse, os, re, signal, subprocess, sys

TRY = re.compile(r'^PARAMFUZZ try (.+)$')
FLAG = re.compile(r'^PARAMFUZZ (NAN|HOT|SLOW) (\S+(?: \S+)*?=\S+)  (.*)$')
DONE = re.compile(r'^PARAMFUZZ \d+ types, \d+ rows, \d+ flagged')
SAN = re.compile(r'(runtime error: .*|ERROR: AddressSanitizer: \S+.*)')


def run_once(binary, types, after, alarm):
    env = dict(os.environ, INFINITE_PARAMFUZZ=types or '1')
    if after:
        env['INFINITE_PARAMFUZZ_AFTER'] = after
    else:
        env.pop('INFINITE_PARAMFUZZ_AFTER', None)
    p = subprocess.run(['perl', '-e', f'alarm {alarm}; exec @ARGV', binary], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    return p.returncode, p.stdout.splitlines()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('out')
    ap.add_argument('--types', default='')
    ap.add_argument('--alarm', type=int, default=1800)
    a = ap.parse_args()
    rows, after = [], ''
    while True:
        code, lines = run_once(a.binary, a.types, after, a.alarm)
        last, san = None, None
        for ln in lines:
            m = TRY.match(ln)
            if m:
                last, san = m.group(1), None
                continue
            m = FLAG.match(ln)
            if m:
                rows.append((m.group(1), m.group(2), m.group(3)))
                continue
            m = SAN.search(ln)
            if m and san is None:
                san = m.group(1)
        if any(DONE.match(ln) for ln in lines):
            break
        if last is None or last == after:
            print(f'param_fuzz_inproc: no progress (exit {code}); last lines:', *lines[-5:], sep='\n  ')
            sys.exit(2)
        hang = code == 128 + signal.SIGALRM or code == -signal.SIGALRM
        rows.append(('HANG' if hang else 'CRASH', last, san or f'exit {code}'))
        print(f'{rows[-1][0]} {last}: {rows[-1][2]}', flush=True)
        after = last
    with open(a.out, 'w') as f:
        for r in rows:
            f.write('\t'.join(r) + '\n')
    print(f'param_fuzz_inproc: {len(rows)} rows flagged -> {a.out}')
    sys.exit(1 if rows else 0)


if __name__ == '__main__':
    main()
