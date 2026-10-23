#!/usr/bin/env python3
"""Drive INFINITE_PARAMFUZZ to the end on a sanitizer build.

The sanitizer build aborts at the first report, so this restarts the app
after each crash with INFINITE_PARAMFUZZ_AFTER set to the row that crashed,
until the summary line prints.

    param_fuzz_inproc.py BIN OUT.tsv [--types A,B] [--alarm SEC]

OUT.tsv rows: verdict, row, detail. A verdict is NAN, HOT or SLOW (the
selftest's own flags), CRASH (the first sanitizer line or the exit code) or
HANG (the alarm fired). Exit 1 if any row is written.

Rows are written to OUT.tsv and printed as they are found, so a long run can
be checked while it goes (`tail -f OUT.tsv`).
"""
import argparse, os, re, signal, subprocess, sys

TRY = re.compile(r'^PARAMFUZZ try (.+)$')
FLAG = re.compile(r'^PARAMFUZZ (NAN|HOT|SLOW) (\S+(?: \S+)*?=\S+)  (.*)$')
DONE = re.compile(r'^PARAMFUZZ \d+ types, \d+ rows, \d+ flagged')
SAN = re.compile(r'(runtime error: .*|ERROR: AddressSanitizer: \S+.*)')


def run_once(binary, types, after, alarm, on_line):
    env = dict(os.environ, INFINITE_PARAMFUZZ=types or '1')
    if after:
        env['INFINITE_PARAMFUZZ_AFTER'] = after
    else:
        env.pop('INFINITE_PARAMFUZZ_AFTER', None)
    p = subprocess.Popen(['perl', '-e', f'alarm {alarm}; exec @ARGV', binary], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    lines = []
    for ln in p.stdout:
        ln = ln.rstrip('\n')
        lines.append(ln)
        on_line(ln)
    return p.wait(), lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('out')
    ap.add_argument('--types', default='')
    ap.add_argument('--alarm', type=int, default=1800)
    a = ap.parse_args()
    rows, after = [], ''
    out = open(a.out, 'w')

    def add(row):
        rows.append(row)
        out.write('\t'.join(row) + '\n')
        out.flush()
        print(f'{row[0]} {row[1]}: {row[2]}', flush=True)

    while True:
        st = {'last': None, 'san': None}

        def on_line(ln):
            m = TRY.match(ln)
            if m:
                st['last'], st['san'] = m.group(1), None
                return
            m = FLAG.match(ln)
            if m:
                add((m.group(1), m.group(2), m.group(3)))
                return
            m = SAN.search(ln)
            if m and st['san'] is None:
                st['san'] = m.group(1)

        code, lines = run_once(a.binary, a.types, after, a.alarm, on_line)
        last, san = st['last'], st['san']
        if any(DONE.match(ln) for ln in lines):
            break
        if last is None or last == after:
            print(f'param_fuzz_inproc: no progress (exit {code}); last lines:', *lines[-5:], sep='\n  ')
            sys.exit(2)
        hang = code == 128 + signal.SIGALRM or code == -signal.SIGALRM
        add(('HANG' if hang else 'CRASH', last, san or f'exit {code}'))
        after = last
    out.close()
    print(f'param_fuzz_inproc: {len(rows)} rows flagged -> {a.out}')
    sys.exit(1 if rows else 0)


if __name__ == '__main__':
    main()
