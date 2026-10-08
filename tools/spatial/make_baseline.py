#!/usr/bin/env python3
"""Spatial audio baseline kit (docs/plans/spatial/README.md, section 6).

Writes, into OUT_DIR (default ~/infinite-spatial-baseline):
  stimulus_objects_adm.wav  ADM BWF, 24 mono objects (8 azimuths x 3 heights);
                            import into Logic, bounce binaural -> logic_binaural.wav
  mixer_pan.wav             the same trials through Infinite's Mixer pan law
                            (equal-power, L/R only): today's baseline
  key.json                  answer key + trial timing (do not open before testing)
  listening_test.html       blind test page; saves answers_*.json

Score with tools/spatial/score_baseline.py.
"""
import json, os, struct, sys, uuid
import numpy as np

SR = 48000
LEAD = 2.0          # seconds of silence before the first trial
SLOT = 3.0          # seconds per trial (stimulus ~0.95 s + gap)
REPEATS = 2
AZIMUTHS = [0, 45, 90, 135, 180, 225, 270, 315]   # clockwise from front, 90 = right
HEIGHTS = [0.0, 0.5, 1.0]                         # ADM cartesian Z (ear, mid, top)


def stimulus(rng):
    # 3 x 250 ms broadband noise bursts (200 Hz - 14 kHz), 10 ms raised-cosine
    # ramps, 100 ms gaps: the classic localisation probe.
    burst_n = int(0.25 * SR)
    gap_n = int(0.10 * SR)
    ramp_n = int(0.01 * SR)
    spec_n = 1 << 15
    out = []
    for _ in range(3):
        noise = rng.standard_normal(spec_n)
        spec = np.fft.rfft(noise)
        f = np.fft.rfftfreq(spec_n, 1 / SR)
        spec[(f < 200) | (f > 14000)] = 0
        b = np.fft.irfft(spec, spec_n)[:burst_n]
        env = np.ones(burst_n)
        r = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, ramp_n))
        env[:ramp_n] = r
        env[-ramp_n:] = r[::-1]
        b *= env
        out += [b, np.zeros(gap_n)]
    s = np.concatenate(out)
    return s / np.sqrt(np.mean(s[s != 0] ** 2)) * 10 ** (-20 / 20)  # -20 dBFS RMS


def positions():
    out = []
    for z in HEIGHTS:
        for az in AZIMUTHS:
            x, y = np.sin(np.radians(az)), np.cos(np.radians(az))
            m = max(abs(x), abs(y))
            x, y = x / m, y / m  # project onto the ADM room cube walls
            el = float(np.degrees(np.arctan2(z, np.hypot(x, y))))
            out.append({"az": az, "z": z, "x": round(float(x), 4), "y": round(float(y), 4),
                        "el": round(el, 2)})
    return out


def trial_order(rng, n):
    order = []
    for _ in range(REPEATS):
        order += list(rng.permutation(n))
    return [int(i) for i in order]


def tc(sec):
    h = int(sec // 3600); m = int(sec % 3600 // 60); s = sec % 60
    return f"{h:02d}:{m:02d}:{s:08.5f}"


def adm_xml(pos, dur):
    o = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<ebuCoreMain xmlns="urn:ebu:metadata-schema:ebuCore_2015" '
         'xmlns:dc="http://purl.org/dc/elements/1.1/" schema="EBU_CORE_20161222.xsd" xml:lang="en">',
         '<coreMetadata><format><audioFormatExtended version="ITU-R_BS.2076-2">',
         f'<audioProgramme audioProgrammeID="APR_1001" audioProgrammeName="Spatial baseline" '
         f'start="{tc(0)}" end="{tc(dur)}"><audioContentIDRef>ACO_1001</audioContentIDRef></audioProgramme>',
         '<audioContent audioContentID="ACO_1001" audioContentName="Probes">']
    for i in range(len(pos)):
        o.append(f'<audioObjectIDRef>AO_{0x1001 + i:04X}</audioObjectIDRef>')
    o.append('<dialogue nonDialogueContentKind="0">0</dialogue></audioContent>')
    for i, p in enumerate(pos):
        n = 0x1001 + i
        o.append(f'<audioObject audioObjectID="AO_{n:04X}" audioObjectName="P{i + 1:02d}" '
                 f'start="{tc(0)}" duration="{tc(dur)}">'
                 f'<audioPackFormatIDRef>AP_0003{n:04X}</audioPackFormatIDRef>'
                 f'<audioTrackUIDRef>ATU_{i + 1:08X}</audioTrackUIDRef></audioObject>')
        o.append(f'<audioPackFormat audioPackFormatID="AP_0003{n:04X}" audioPackFormatName="P{i + 1:02d}" '
                 f'typeLabel="0003" typeDefinition="Objects">'
                 f'<audioChannelFormatIDRef>AC_0003{n:04X}</audioChannelFormatIDRef></audioPackFormat>')
        o.append(f'<audioChannelFormat audioChannelFormatID="AC_0003{n:04X}" audioChannelFormatName="P{i + 1:02d}" '
                 f'typeLabel="0003" typeDefinition="Objects">'
                 f'<audioBlockFormat audioBlockFormatID="AB_0003{n:04X}_00000001" rtime="{tc(0)}" duration="{tc(dur)}">'
                 f'<cartesian>1</cartesian>'
                 f'<position coordinate="X">{p["x"]}</position>'
                 f'<position coordinate="Y">{p["y"]}</position>'
                 f'<position coordinate="Z">{p["z"]}</position>'
                 f'<width>0</width><depth>0</depth><height>0</height><gain>1</gain>'
                 f'</audioBlockFormat></audioChannelFormat>')
        o.append(f'<audioStreamFormat audioStreamFormatID="AS_0003{n:04X}" audioStreamFormatName="PCM_P{i + 1:02d}" '
                 f'formatLabel="0001" formatDefinition="PCM">'
                 f'<audioChannelFormatIDRef>AC_0003{n:04X}</audioChannelFormatIDRef>'
                 f'<audioTrackFormatIDRef>AT_0003{n:04X}_01</audioTrackFormatIDRef></audioStreamFormat>')
        o.append(f'<audioTrackFormat audioTrackFormatID="AT_0003{n:04X}_01" audioTrackFormatName="PCM_P{i + 1:02d}" '
                 f'formatLabel="0001" formatDefinition="PCM">'
                 f'<audioStreamFormatIDRef>AS_0003{n:04X}</audioStreamFormatIDRef></audioTrackFormat>')
        o.append(f'<audioTrackUID UID="ATU_{i + 1:08X}" sampleRate="{SR}" bitDepth="24">'
                 f'<audioTrackFormatIDRef>AT_0003{n:04X}_01</audioTrackFormatIDRef>'
                 f'<audioPackFormatIDRef>AP_0003{n:04X}</audioPackFormatIDRef></audioTrackUID>')
    o.append('</audioFormatExtended></format></coreMetadata></ebuCoreMain>')
    return "\n".join(o).encode("utf-8")


def chunk(cid, data):
    pad = b"\0" if len(data) % 2 else b""
    return cid + struct.pack("<I", len(data)) + data + pad


def pcm24(x):
    # x: (frames, channels) float -> interleaved 24-bit little endian
    i = np.clip(np.round(x * 8388607.0), -8388608, 8388607).astype("<i4")
    b = i.reshape(-1).view(np.uint8).reshape(-1, 4)[:, :3]
    return b.tobytes()


def write_wav(path, audio, extra_before_data=(), extra_after_data=()):
    ch = audio.shape[1]
    fmt = struct.pack("<HHIIHH", 1, ch, SR, SR * ch * 3, ch * 3, 24)
    body = b"WAVE" + chunk(b"fmt ", fmt)
    for c in extra_before_data:
        body += c
    body += chunk(b"data", pcm24(audio))
    for c in extra_after_data:
        body += c
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def bext():
    desc = b"Infinite spatial baseline".ljust(256, b"\0")
    orig = b"Infinite".ljust(32, b"\0")
    ref = uuid.uuid4().hex[:32].encode().ljust(32, b"\0")
    data = desc + orig + ref + b"2026-10-08" + b"00:00:00" + struct.pack("<QH", 0, 1)
    data += b"\0" * 64 + struct.pack("<hhhhh", 0, 0, 0, 0, 0) + b"\0" * 180
    return chunk(b"bext", data)


def chna(n):
    d = struct.pack("<HH", n, n)
    for i in range(n):
        m = 0x1001 + i
        d += struct.pack("<H", i + 1)
        d += f"ATU_{i + 1:08X}".encode() + f"AT_0003{m:04X}_01".encode() + f"AP_0003{m:04X}".encode() + b"\0"
    return chunk(b"chna", d)


def main():
    out = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else "~/infinite-spatial-baseline")
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(20261008)
    stim = stimulus(rng)
    pos = positions()
    n = len(pos)
    orders = {"logic_binaural": trial_order(rng, n), "mixer_pan": trial_order(rng, n)}
    frames = int((LEAD + SLOT * len(orders["mixer_pan"]) + 1.0) * SR)
    dur = frames / SR

    # ADM objects for Logic: one mono track per position, silent outside its slots.
    objs = np.zeros((frames, n))
    for t, idx in enumerate(orders["logic_binaural"]):
        s = int((LEAD + t * SLOT) * SR)
        objs[s:s + len(stim), idx] += stim
    write_wav(os.path.join(out, "stimulus_objects_adm.wav"), objs,
              extra_before_data=[bext(), chna(n)],
              extra_after_data=[chunk(b"axml", adm_xml(pos, dur))])

    # Today's Infinite: Mixer equal-power pan (DspMath::EqualPowerPan, unity at centre).
    st = np.zeros((frames, 2))
    for t, idx in enumerate(orders["mixer_pan"]):
        pan = np.sin(np.radians(pos[idx]["az"]))
        a = (pan + 1) * np.pi / 4
        g = np.array([np.cos(a), np.sin(a)]) * np.sqrt(2)
        s = int((LEAD + t * SLOT) * SR)
        st[s:s + len(stim)] += stim[:, None] * g
    write_wav(os.path.join(out, "mixer_pan.wav"), st)

    key = {"sr": SR, "lead": LEAD, "slot": SLOT, "stim_sec": len(stim) / SR,
           "positions": pos, "orders": orders}
    with open(os.path.join(out, "key.json"), "w") as f:
        json.dump(key, f, indent=1)

    here = os.path.dirname(os.path.abspath(__file__))
    page = open(os.path.join(here, "listening_test.html")).read()
    page = page.replace("__TIMING__", json.dumps({"lead": LEAD, "slot": SLOT, "stim": len(stim) / SR,
                                                  "trials": len(orders["mixer_pan"])}))
    with open(os.path.join(out, "listening_test.html"), "w") as f:
        f.write(page)
    print(out)


if __name__ == "__main__":
    main()
