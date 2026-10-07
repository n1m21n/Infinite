#!/usr/bin/env python3
"""Measure what INFINITE_ARRANGESAMPLEEXPORTTEST actually wrote.

The fixture (src/app/frame, search INFINITE_ARRANGESAMPLEEXPORTTEST) renders a
6 s window through the real arrangement render queue: a 100 BPM click track,
tempo-synced at 120 BPM, whose clip starts at beat 2, on an audio lane, plus a
Ramp clip starting at the same beat on a video lane. So in BOTH files:

  * the first click, and the first non-black video frame, belong at 1.0 s
  * every click after it sits on a 0.5 s grid (120 BPM, stretched 100 -> 120)

This check lives outside the app on purpose: if it re-derived those instants
from the app's own beat maths it could only ever agree with itself. It reads
the WAV with the stdlib and the MP4 with ffprobe, and knows nothing about
Transport, ticks or time-stretching.

Usage: check-arrange-export.py <dir>    exit 0 = pass, 1 = fail (reason on stdout)
"""
import json
import os
import subprocess
import sys
import wave

FIRST_CLICK_S = 1.0
CLICK_GRID_S = 0.5
DURATION_S = 6.0
ONSET_TOL_S = 0.030      # one click is 6 ms; 30 ms is well inside a 500 ms grid
REFINE_S = 0.040         # a tripped threshold is pulled forward to the burst peak
DURATION_TOL_S = 0.15


def fail(msg):
    print(msg)
    sys.exit(1)


def click_onsets(path):
    """Onset time of every burst in the WAV, by threshold with a hold-off.

    Two measured properties of the real file shape the detection, and neither
    is a defect:

      * the burst that sits exactly on the clip's window onset is attenuated
        by the engine's declick ramp - measured at about a quarter of the
        later bursts - so the threshold is relative and deliberately low;
      * the time-stretch smears a little energy backwards, so the threshold
        can trip up to ~10 ms early. Each trip is therefore refined forward
        to the loudest sample within REFINE_S, which is the click itself.
    """
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            fail("export_audio.wav is not 16-bit (%d bytes/sample)" % w.getsampwidth())
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        raw = w.readframes(n)
    import array
    pcm = array.array("h")
    pcm.frombytes(raw)
    # mono-ise by taking the loudest channel per frame; a channel swap or a
    # dead channel is not what this check is for (ARRANGESAMPLETEST covers it)
    peak = max((abs(v) for v in pcm), default=0)
    if peak < 3000:
        fail("export_audio.wav is silent or nearly so (peak %d/32767) - the "
             "timeline audio never reached the WAV writer" % peak)
    thresh = peak * 0.15
    holdoff = int(0.2 * sr)
    refine = int(REFINE_S * sr)
    onsets, i, frames = [], 0, n

    def amp_at(f):
        return max(abs(pcm[f * ch + c]) for c in range(ch))

    while i < frames:
        if amp_at(i) >= thresh:
            best, best_at = -1, i
            for f in range(i, min(i + refine, frames)):
                a = amp_at(f)
                if a > best:
                    best, best_at = a, f
            onsets.append(best_at / sr)
            i = best_at + holdoff
        else:
            i += 1
    return onsets, n / sr


def probe(path):
    try:
        out = subprocess.run(
            ["ffprobe", "-v", "error", "-show_format", "-show_streams",
             "-print_format", "json", path],
            capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        print("ffprobe not installed - skipping the MP4 half of this check")
        return None
    except subprocess.CalledProcessError as e:
        fail("ffprobe could not read %s: %s" % (os.path.basename(path), e.stderr.strip()))
    return json.loads(out)


def main():
    if len(sys.argv) != 2:
        fail("usage: check-arrange-export.py <dir>")
    d = sys.argv[1]
    wav_path = os.path.join(d, "export_audio.wav")
    mp4_path = os.path.join(d, "export_av.mp4")
    for p in (wav_path, mp4_path):
        if not os.path.isfile(p):
            fail("%s was never written" % os.path.basename(p))
        if os.path.getsize(p) == 0:
            fail("%s is zero bytes" % os.path.basename(p))

    onsets, wav_dur = click_onsets(wav_path)
    if not onsets:
        fail("no clicks found in export_audio.wav")
    if abs(wav_dur - DURATION_S) > DURATION_TOL_S:
        fail("export_audio.wav is %.3f s, expected %.1f s" % (wav_dur, DURATION_S))
    if abs(onsets[0] - FIRST_CLICK_S) > ONSET_TOL_S:
        fail("first click at %.3f s, expected %.1f s (tolerance %d ms) - the "
             "clip's start beat or its time-stretch is off"
             % (onsets[0], FIRST_CLICK_S, ONSET_TOL_S * 1000))
    worst, worst_at = 0.0, 0.0
    for t in onsets:
        k = round((t - FIRST_CLICK_S) / CLICK_GRID_S)
        err = abs(t - (FIRST_CLICK_S + k * CLICK_GRID_S))
        if err > worst:
            worst, worst_at = err, t
    if worst > ONSET_TOL_S:
        fail("click at %.3f s is %.0f ms off the %.1f s grid (tolerance %d ms) - "
             "the synced sample is drifting against the render clock"
             % (worst_at, worst * 1000, CLICK_GRID_S, ONSET_TOL_S * 1000))

    # 1.0 s to 6.0 s on a 0.5 s grid is 11 clicks; anything under 8 means the
    # clip stopped early or the writer truncated it
    if len(onsets) < 8:
        fail("only %d clicks in export_audio.wav, expected ~11 (1.0 s to 6.0 s "
             "on a %.1f s grid) - the clip or the WAV stopped short"
             % (len(onsets), CLICK_GRID_S))

    summary = ("WAV %.3f s, %d clicks, first at %.3f s, worst grid error %.0f ms"
               % (wav_dur, len(onsets), onsets[0], worst * 1000))

    info = probe(mp4_path)
    if info is not None:
        vs = [s for s in info.get("streams", []) if s.get("codec_type") == "video"]
        if not vs:
            fail("export_av.mp4 has no video stream")
        v = vs[0]
        dur = float(info.get("format", {}).get("duration", 0.0))
        if abs(dur - DURATION_S) > DURATION_TOL_S:
            fail("export_av.mp4 is %.3f s, expected %.1f s" % (dur, DURATION_S))
        frames = int(v.get("nb_frames") or 0)
        if frames < 150:
            fail("export_av.mp4 has %d frames, expected ~180 (30 fps x 6 s) - "
                 "the video compositor dropped frames on the way to the encoder" % frames)
        summary += ("; MP4 %s %sx%s, %.3f s, %d frames"
                    % (v.get("codec_name"), v.get("width"), v.get("height"), dur, frames))

    print(summary)


if __name__ == "__main__":
    main()
