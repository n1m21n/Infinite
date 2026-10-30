#!/usr/bin/env python3
"""Website generator parity: the wasm build must match the desktop render.

5 styles x 8 parts at seed 1: same length, integrated LUFS within 0.5 dB, correlation > 0.99.
  INFINITE_GENSAMPLEPARITY=1 INFINITE_GENSAMPLEBATCH=<native_dir> build/Infinite.app/Contents/MacOS/Infinite
  node wasm/parity_dump.js build-wasm/infinite_gen_wasm.js <wasm_dir>
  python3 tools/web_gen_parity.py <native_dir> <wasm_dir>
"""
import sys, struct, glob, os
import numpy as np

def read_wav(path):
    b = open(path, 'rb').read()
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(b):
        tag, size = b[pos:pos + 4], struct.unpack('<I', b[pos + 4:pos + 8])[0]
        body = b[pos + 8:pos + 8 + size]
        if tag == b'fmt ':
            fmt = struct.unpack('<HHIIHH', body[:16])
        elif tag == b'data':
            data = body
        pos += 8 + size + (size & 1)
    code, ch, rate, _, _, bits = fmt
    if code == 3 and bits == 32:
        x = np.frombuffer(data, '<f4').astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(data, '<i2').astype(np.float64) / 32768.0
    else:
        raise ValueError(f'{path}: unsupported format {code}/{bits}')
    return x.reshape(-1, ch), rate

def biquad(x, b, a):
    y = np.zeros_like(x); z1 = z2 = 0.0
    for i in range(len(x)):
        v = x[i]; o = b[0] * v + z1
        z1 = b[1] * v - a[1] * o + z2; z2 = b[2] * v - a[2] * o; y[i] = o
    return y

def lufs(x, rate):
    """BS.1770-4 integrated loudness, K-weighting for 48 kHz, two-stage gating."""
    assert rate == 48000
    s1 = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
    s2 = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])
    k = np.stack([biquad(biquad(x[:, c], *s1), *s2) for c in range(x.shape[1])], 1)
    n, hop = int(0.4 * rate), int(0.1 * rate)
    blocks = [np.mean(k[i:i + n] ** 2, axis=0).sum() for i in range(0, len(k) - n + 1, hop)]
    blocks = np.array(blocks)
    l = -0.691 + 10 * np.log10(blocks + 1e-20)
    keep = blocks[l > -70]
    if not len(keep): return -70.0
    rel = -0.691 + 10 * np.log10(keep.mean()) - 10
    keep = blocks[l > rel]
    return -0.691 + 10 * np.log10(keep.mean() + 1e-20)

def main():
    native, wasm = sys.argv[1], sys.argv[2]
    bad = n = 0
    for p in sorted(glob.glob(os.path.join(wasm, '*.wav'))):
        name = os.path.basename(p)
        q = os.path.join(native, name)
        if not os.path.exists(q):
            print('MISSING native', name); bad += 1; continue
        (a, ra), (b, rb) = read_wav(q), read_wav(p)
        n += 1
        if len(a) != len(b):
            print(f'FAIL {name}: length {len(a)} vs {len(b)}'); bad += 1; continue
        d = abs(lufs(a, ra) - lufs(b, rb))
        c = float(np.corrcoef(a.ravel(), b.ravel())[0, 1])
        ok = d <= 0.5 and c > 0.99
        print(f'{"ok  " if ok else "FAIL"} {name}: dLUFS {d:.3f}, corr {c:.5f}')
        bad += not ok
    print(f'{n} compared, {bad} failed')
    sys.exit(1 if bad or not n else 0)

main()
