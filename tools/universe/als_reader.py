#!/usr/bin/env python3
"""Ableton Live sets -> MIDI clips for the ingest inbox, knob automation arcs, and which devices each track used.
    als_reader.py [ROOT ...] [--inbox ~/Infinite/training-data/inbox] [--arcs ~/Infinite/training-data/arcs/als.jsonl]
A .als is gzip XML (read only). Per set:
  MidiClip    every clip's <KeyTrack><MidiKey/> and <MidiNoteEvent Time Duration Velocity/> (beats), written as one
              type-1 .mid per set (a track per Live track, clips at their arrangement time; session clips laid end to
              end after the arrangement) into inbox/als_<owner|packs>/ with a SOURCE.txt.
  Automation  every <AutomationEnvelope> resolved through <PointeeId> to the parameter that owns <AutomationTarget Id>,
              kept as (beat, value) points: one arcs row per envelope with track, device, parameter.
  Devices     device element names per track (OriginalSimpler, AuPluginDevice name, ...).
Sets under 'my music' are the owner's own (licence "owner"); the rest are pack demo sets ("sample pack, local only")."""
import argparse, collections, gzip, json, os, re, struct, sys, time

SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
PPQ = 480
NOTE = re.compile(r'<MidiNoteEvent Time="([-\d.e]+)" Duration="([-\d.e]+)" Velocity="([\d.]+)"[^>]*IsEnabled="(true|false)"')
TRACK = re.compile(r"<(MidiTrack|AudioTrack|GroupTrack|ReturnTrack|MasterTrack|MainTrack)\b[^>]*>(.*?)</\1>", re.S)
DEVICE = re.compile(r"<(OriginalSimpler|MultiSampler|Operator|InstrumentVector|UltraAnalog|Drift|Meld|Tension|Collision|"
                    r"DrumGroupDevice|InstrumentGroupDevice|AudioEffectGroupDevice|PluginDevice|AuPluginDevice|MxDeviceAudioEffect|"
                    r"MxDeviceInstrument|MxDeviceMidiEffect|AutoFilter|Reverb|Delay|Echo|Chorus2|Saturator|Compressor2|Eq8|"
                    r"Redux2|PhaserNew|AutoPan|GlueCompressor|DrumBuss|Roar|Limiter|Gate|Utility|StereoGain|Tuner|"
                    r"MidiArpeggiator|MidiChord|MidiScale|MidiPitcher|MidiVelocity|MidiRandom|FilterDelay|GrainDelay|"
                    r"Hybrid|Corpus|Erosion|Vocoder|Overdrive|Pedal|Amp|Cabinet|Looper|BeatRepeat|Resonator|FrequencyShifter)\b[^>]*>", re.S)


def vlq(n):
    b = [n & 0x7F]
    while n > 127:
        n >>= 7
        b.insert(0, (n & 0x7F) | 0x80)
    return bytes(b)


def write_mid(path, tracks, bpm, num, den):
    """tracks: [(name, [(beat, dur, pitch, vel)])] -> type-1 SMF"""
    def chunk(events):
        ev, t, out = sorted(events), 0, b""
        for tick, order, data in ev:
            out += vlq(tick - t) + data
            t = tick
        return b"MTrk" + struct.pack(">I", len(out) + 4) + out + b"\x00\xff\x2f\x00"
    tempo = int(60e6 / bpm)
    head = [(0, 0, b"\xff\x51\x03" + tempo.to_bytes(3, "big")),
            (0, 0, b"\xff\x58\x04" + bytes([num, max(0, den.bit_length() - 1), 24, 8]))]
    body = [chunk(head)]
    for i, (name, notes) in enumerate(tracks[:15]):
        ch = 9 if re.search(r"drum|kit|perc|beat|kick|snare|hat", name, re.I) else (i if i < 9 else i + 1)
        nm = name.encode("utf-8", "replace")[:60]
        ev = [(0, 0, b"\xff\x03" + vlq(len(nm)) + nm)]
        for beat, dur, p, v in notes:
            on = max(0, round(beat * PPQ))
            ev.append((on, 2, bytes([0x90 | ch, p, max(1, min(127, v))])))
            ev.append((on + max(1, round(dur * PPQ)), 1, bytes([0x80 | ch, p, 0])))
        body.append(chunk(ev))
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(body), PPQ) + b"".join(body))


def name_of(body):
    m = re.search(r'<EffectiveName Value="([^"]*)"', body) or re.search(r'<UserName Value="([^"]+)"', body)
    return m[1] if m else ""


def clips(body):
    """(start beat, notes) per MidiClip, notes looped/clipped to the clip's own start..end"""
    for c in re.finditer(r"<MidiClip\b[^>]*Time=\"([-\d.e]+)\"[^>]*>(.*?)</MidiClip>", body, re.S):
        t0, cb = float(c[1]), c[2]
        g = lambda tag: (lambda m: float(m[1]) if m else None)(re.search(r"<%s Value=\"([-\d.e]+)\"" % tag, cb))
        start, end = g("CurrentStart"), g("CurrentEnd")
        ls, le = g("LoopStart"), g("LoopEnd")
        loop = re.search(r'<LoopOn Value="true"', cb) is not None
        notes = []
        for kt in re.finditer(r"<KeyTrack\b[^>]*>(.*?)</KeyTrack>", cb, re.S):
            k = re.search(r'<MidiKey Value="(\d+)"', kt[1])
            if not k:
                continue
            for n in NOTE.finditer(kt[1]):
                if n[4] == "true":
                    notes.append((float(n[1]), float(n[2]), int(k[1]), round(float(n[3]))))
        if not notes:
            continue
        span = (end - start) if start is not None and end is not None else max(n[0] + n[1] for n in notes)
        if loop and ls is not None and le and le > ls and span > le - ls + 1e-6:
            L, out, k = le - ls, [], 0
            base = [n for n in notes if ls <= n[0] < le]
            while k * L < span:
                out += [(n[0] - ls + k * L, n[1], n[2], n[3]) for n in base if n[0] - ls + k * L < span]
                k += 1
            notes = out
        else:
            off = ls if ls is not None else 0.0  # clip content starts at its loop start; Current* are arrangement beats
            notes = [(n[0] - off, n[1], n[2], n[3]) for n in notes if 0 <= n[0] - off < span]
        yield t0, span, notes


def read_set(path):
    x = gzip.open(path).read().decode("utf-8", "replace")
    m = re.search(r"<Tempo>.*?<Manual Value=\"([\d.]+)\"", x, re.S)
    bpm = float(m[1]) if m else 120.0
    ts = re.search(r"<TimeSignature>.*?<Numerator Value=\"(\d+)\".*?<Denominator Value=\"(\d+)\"", x, re.S)
    num, den = (int(ts[1]), int(ts[2])) if ts else (4, 4)
    # every AutomationTarget id -> (track, device, parameter) by walking element nesting once
    owner, stack = {}, []
    for t in re.finditer(r"<(/?)([A-Za-z0-9_.]+)((?:\s[^>]*?)?)(/?)>", x):
        if t[2] in ("AutomationTarget", "ModulationTarget"):
            i = re.search(r'Id="(\d+)"', t[3])
            if i and not t[1]:
                owner[i[1]] = (stack[-1] if stack else "?")
            if not t[4] and not t[1]:
                stack.append(t[2])
            elif t[1]:
                stack and stack.pop()
            continue
        if t[4]:
            continue
        if t[1]:
            stack and stack.pop()
        else:
            stack.append(t[2])
    tracks, autos, devices = [], [], {}
    for tr in TRACK.finditer(x):
        kind, body = tr[1], tr[2]
        nm = name_of(body) or kind
        devs = [d[1] for d in DEVICE.finditer(body)]
        plug = re.findall(r'<PlugName Value="([^"]+)"|<Name Value="([^"]+)"\s*/>\s*<Manufacturer', body)
        devices[nm] = devs + ["plugin:" + (a or b) for a, b in plug][:12]
        if kind == "MidiTrack":
            arr = []
            for t0, span, notes in clips(body):
                arr.append((t0, span, notes))
            tracks.append((nm, arr))
        for e in re.finditer(r"<AutomationEnvelope\b[^>]*>\s*<EnvelopeTarget>\s*<PointeeId Value=\"(\d+)\"\s*/>.*?<Events>(.*?)</Events>",
                             body, re.S):
            pts = [(round(float(a), 4), round(float(b), 5)) for a, b in
                   re.findall(r'<FloatEvent [^>]*Time="([-\d.e]+)" Value="([-\d.e]+)"', e[2]) if float(a) > -1e6]
            if len(pts) >= 3 and len({p[1] for p in pts}) > 1:
                autos.append({"track": nm, "param": owner.get(e[1], "?"), "points": pts})
    return bpm, num, den, tracks, autos, devices


def sets(roots):
    for root in roots:
        for d, subs, fs in os.walk(root):
            subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith(".") and s != "Backup")
            for f in sorted(fs):
                if f.endswith(".als") and not f.startswith("._"):
                    yield os.path.join(d, f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--inbox", default="~/Infinite/training-data/inbox")
    ap.add_argument("--arcs", default="~/Infinite/training-data/arcs/als.jsonl")
    ap.add_argument("--budget", type=float, default=1200)
    a = ap.parse_args()
    inbox, arcs = os.path.expanduser(a.inbox), os.path.expanduser(a.arcs)
    os.makedirs(os.path.dirname(arcs), exist_ok=True)
    for who, lic in (("als_owner", "owner"), ("als_packs", "sample pack, local only")):
        os.makedirs(os.path.join(inbox, who), exist_ok=True)
        with open(os.path.join(inbox, who, "SOURCE.txt"), "w") as f:
            f.write("license: %s\n" % lic)
    t0, n, seen = time.time(), collections.Counter(), set()
    devs = collections.Counter()
    with open(arcs, "w") as fo:
        for p in sets(a.roots):
            if time.time() - t0 > a.budget:
                print("budget hit", file=sys.stderr)
                break
            owner = "/my music/" in p.lower() or "/my music" in os.path.dirname(p).lower()
            try:
                bpm, num, den, tracks, autos, devices = read_set(p)
            except (OSError, EOFError, ValueError) as e:
                n["unreadable"] += 1
                continue
            n["sets"] += 1
            lic = "owner" if owner else "sample pack, local only"
            out = []
            for nm, arr in tracks:
                arr.sort(key=lambda c: c[0])
                notes, tail = [], max([t + s for t, s, _ in arr if t >= 0] or [0])
                for t, span, ns in arr:  # session clips have Time 0..; arrangement clips carry real positions
                    notes += [(t + b, d, k, v) for b, d, k, v in ns]
                key = tuple(sorted(notes))
                if notes and key not in seen:
                    seen.add(key)
                    out.append((nm, notes))
                    n["clips"] += len(arr)
                    n["notes"] += len(notes)
            if out:
                rel = re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.relpath(p, a.roots[0]))[-150:]
                write_mid(os.path.join(inbox, "als_owner" if owner else "als_packs", rel[:-4] + ".mid"), out, bpm, num, den)
                n["mid_written"] += 1
            for r in autos:
                fo.write(json.dumps(dict(r, type="automation_arc", src=p, bpm=bpm, meter=[num, den], license=lic),
                                    separators=(",", ":")) + "\n")
                n["envelopes"] += 1
            fo.write(json.dumps({"type": "set_devices", "src": p, "bpm": bpm, "meter": [num, den], "tracks": devices,
                                 "license": lic}, separators=(",", ":")) + "\n")
            for v in devices.values():
                devs.update(v)
            n["owner" if owner else "packs"] += 1
    print(dict(n), "in %.0f s" % (time.time() - t0))
    print("devices:", dict(devs.most_common(25)))
    print("wrote", arcs)


if __name__ == "__main__":
    main()
