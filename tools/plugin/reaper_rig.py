#!/usr/bin/env python3
"""Reaper rig for the plugin metrics in a real DAW (docs/plans/vst-au/README.md step 1.7).

    tools/plugin/reaper_rig.py [--reaper PATH] [--build build-plugin] [--out DIR] [--timeout S]

For each R patch (R1 on "Infinite Instrument", R2 on "Infinite FX") it:
  1. installs the two VST3 bundles into ~/Library/Audio/Plug-Ins/VST3 for the run (APFS clones),
  2. starts REAPER on a generated ReaScript that builds the project (track, plugin, a MIDI phrase
     or an input WAV), saves it as .rpp and quits,
  3. renders that project with `REAPER -renderproject` four ways: offline, offline again,
     online (RENDER_1X 1, the realtime path) and offline after a save/reopen round trip,
  4. nulls the WAVs: M-4 (offline twice bit-identical), M-5 (offline vs online <= -60 dBFS RMS),
     M-11 (reopened project renders identically), and for R2 an impulse project, M-6 (Reaper's
     delay compensation puts the impulse back at its input time: residual offset 0 samples).
Installed bundles are removed on exit, also on failure.

The engine and patch reach the plugin through $INFINITE_PLUGIN_ENGINE / $INFINITE_PLUGIN_PATCH,
which REAPER passes to in-process VST3s because the binary is started directly.

Needs a licensed REAPER: the evaluation nag window blocks unattended starts, and the rig then
times out with a message saying so. Block sizes 32/64/512/2048 are covered at shell level by
`plugin_shell_check --metrics`; here REAPER renders at its own block size.
"""
import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RATE = 48000
SECONDS = 8
IMPULSE_AT = 4800
BUNDLES = ("Infinite Instrument.vst3", "Infinite FX.vst3")


# ---- WAV ------------------------------------------------------------------------------------

def write_wav_f32(path, frames):
    """frames: list of floats, written as stereo float32."""
    data = b"".join(struct.pack("<ff", x, x) for x in frames)
    fmt = struct.pack("<HHIIHH", 3, 2, RATE, RATE * 8, 8, 32)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def read_wav_left(path):
    """Left channel as floats; PCM 16/24/32 and float 32/64, WAVE_FORMAT_EXTENSIBLE included."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError(f"{path}: missing fmt or data chunk")
    tag, ch, _rate, _bps, align, bits = struct.unpack("<HHIIHH", fmt[:16])
    if tag == 0xFFFE:
        tag = struct.unpack("<H", fmt[24:26])[0]
    width = bits // 8
    out = []
    for i in range(0, len(data) - align + 1, align):
        s = data[i:i + width]
        if tag == 3:
            out.append(struct.unpack("<f" if width == 4 else "<d", s)[0])
        elif width == 2:
            out.append(struct.unpack("<h", s)[0] / 32768.0)
        elif width == 3:
            out.append(int.from_bytes(s, "little", signed=True) / 8388608.0)
        elif width == 4:
            out.append(struct.unpack("<i", s)[0] / 2147483648.0)
        else:
            raise ValueError(f"{path}: unsupported sample format tag {tag}, {bits} bits")
    return out


def db(x):
    return 20.0 * math.log10(x) if x > 0 else -999.0


def rms(a):
    return math.sqrt(sum(x * x for x in a) / len(a)) if a else 0.0


def null_rms(a, b):
    n = min(len(a), len(b))
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(n)) / n) if n else 1.0


# ---- inputs (same signal as plugin_shell_check --metrics) -----------------------------------

def signal(t):
    h = (t * 2654435761) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 2246822519) & 0xFFFFFFFF
    h ^= h >> 13
    noise = (h & 0xFFFF) / 32768.0 - 1.0
    return 0.35 * math.sin(2.0 * math.pi * 220.0 * t / RATE) + 0.1 * noise


# ---- REAPER ---------------------------------------------------------------------------------

LUA = r"""
reaper.Main_OnCommand(40023, 0) -- File: New project
reaper.GetSetProjectInfo(0, "PROJECT_SRATE", {rate}, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
reaper.InsertTrackAtIndex(0, true)
local tr = reaper.GetTrack(0, 0)
reaper.SetOnlyTrackSelected(tr)
local fx = reaper.TrackFX_AddByName(tr, "{plugin}", false, -1)
if fx < 0 then
  local f = io.open("{done}", "w") f:write("plugin not found: {plugin}\n") f:close()
  reaper.Main_OnCommand(40004, 0)
  return
end
if "{media}" ~= "" then
  reaper.SetEditCurPos(0, false, false)
  reaper.InsertMedia("{media}", 0)
else
  local item = reaper.CreateNewMIDIItemInProj(tr, 0, {seconds}, false)
  local take = reaper.GetActiveTake(item)
  {notes}
  reaper.MIDI_Sort(take)
end
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", "{outdir}", true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "render", true)
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", {seconds}, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", {rate}, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG", 0, true)
reaper.Main_SaveProjectEx(0, "{rpp}", 0)
local f = io.open("{done}", "w") f:write("ok\n") f:close()
reaper.Main_OnCommand(40004, 0) -- File: Quit REAPER
"""


def lua_notes():
    lines = []
    for k in range(24):
        on = (2400 + k * 14000) / RATE
        off = on + 9000 / RATE
        vel = int(127 * (0.5 + 0.4 * (k % 5) / 4.0))
        lines.append(
            f"reaper.MIDI_InsertNote(take, false, false, reaper.MIDI_GetPPQPosFromProjTime(take, {on:.6f}), "
            f"reaper.MIDI_GetPPQPosFromProjTime(take, {off:.6f}), 0, {48 + k % 24}, {vel}, true)")
    return "\n  ".join(lines)


class Rig:
    def __init__(self, args):
        self.reaper = args.reaper
        self.build = os.path.join(ROOT, args.build)
        self.out = args.out
        self.timeout = args.timeout
        self.cfg = os.path.join(self.out, "reaper.ini")
        self.installed = []
        self.fails = 0
        self.rows = []

    def check(self, ok, what, detail):
        self.rows.append((what, "PASS" if ok else "FAIL", detail))
        print(f"{'PASS' if ok else 'FAIL'}  {what}  {detail}", flush=True)
        if not ok:
            self.fails += 1

    def install(self):
        dst = os.path.expanduser("~/Library/Audio/Plug-Ins/VST3")
        os.makedirs(dst, exist_ok=True)
        for b in BUNDLES:
            src = os.path.join(self.build, "VST3", "Release", b)
            if not os.path.isdir(src):
                src = os.path.join(self.build, "VST3", b)
            target = os.path.join(dst, b)
            if os.path.exists(target):
                shutil.rmtree(target)
            subprocess.run(["cp", "-Rc", src, target], check=True)
            self.installed.append(target)

    def uninstall(self):
        for t in self.installed:
            shutil.rmtree(t, ignore_errors=True)
        self.installed = []

    def run_reaper(self, extra, env, wait_for=None):
        cmd = [self.reaper, "-newinst", "-nosplash", "-cfgfile", self.cfg] + extra
        p = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            if p.poll() is not None and (wait_for is None or os.path.exists(wait_for)):
                return True
            time.sleep(0.25)
        p.kill()
        p.wait()
        self.kill_strays()
        return False

    def kill_strays(self):
        # REAPER's plugin-scan children (-__vst_scan__) outlive a killed parent.
        subprocess.run(["pkill", "-9", "-f", f"-cfgfile {self.cfg}"], capture_output=True)

    def build_project(self, name, plugin, patch, media):
        d = os.path.join(self.out, name)
        os.makedirs(d, exist_ok=True)
        rpp, done = os.path.join(d, "project.rpp"), os.path.join(d, "built.txt")
        script = os.path.join(d, "build.lua")
        with open(script, "w") as f:
            f.write(LUA.replace("{rate}", str(RATE)).replace("{seconds}", str(SECONDS))
                    .replace("{plugin}", plugin).replace("{media}", media or "")
                    .replace("{notes}", lua_notes()).replace("{outdir}", d)
                    .replace("{rpp}", rpp).replace("{done}", done))
        env = dict(os.environ, INFINITE_PLUGIN_PATCH=patch)
        if not self.run_reaper([script], env, wait_for=done):
            raise RuntimeError("REAPER did not finish building the project; an unlicensed REAPER shows "
                               "an evaluation window that blocks unattended runs (install the licence key)")
        status = open(done).read().strip()
        if status != "ok":
            raise RuntimeError(status)
        return rpp, env

    def render(self, rpp, env, tag, online=False):
        """Renders a copy of `rpp` (RENDER_1X set for online) and returns the left channel."""
        d = os.path.dirname(rpp)
        text = open(rpp).read()
        lines = [l for l in text.splitlines() if not l.strip().startswith(("RENDER_1X", "RENDER_PATTERN"))]
        lines.insert(1, f"  RENDER_1X {1 if online else 0}")
        lines.insert(1, f"  RENDER_PATTERN {tag}")
        variant = os.path.join(d, f"{tag}.rpp")
        with open(variant, "w") as f:
            f.write("\n".join(lines) + "\n")
        wav = os.path.join(d, f"{tag}.wav")
        if os.path.exists(wav):
            os.remove(wav)
        if not self.run_reaper(["-renderproject", variant], env, wait_for=wav) or not os.path.exists(wav):
            raise RuntimeError(f"render {tag} produced no {wav}")
        return read_wav_left(wav)

    def measure(self, name, plugin, patch, media):
        rpp, env = self.build_project(name, plugin, patch, media)
        a = self.render(rpp, env, "offline_a")
        b = self.render(rpp, env, "offline_b")
        self.check(bool(a) and rms(a) > 1e-3 and a == b, f"M-4 {name}: offline renders bit-identical",
                   f"rms {db(rms(a)):.1f} dBFS, null {db(null_rms(a, b)):.1f} dBFS")
        rt = self.render(rpp, env, "online")
        n = db(null_rms(a, rt))
        self.check(len(rt) == len(a) and n <= -60.0, f"M-5 {name}: offline vs online null", f"{n:.1f} dBFS")
        # M-11: the saved project (plugin state chunk included) reopens and renders the same.
        r = self.render(rpp, env, "reopened")
        self.check(r == a, f"M-11 {name}: reopened project renders identically",
                   f"null {db(null_rms(a, r)):.1f} dBFS")
        return env

    def impulse(self, patch):
        wav = os.path.join(self.out, "impulse.wav")
        write_wav_f32(wav, [1.0 if t == IMPULSE_AT else 0.0 for t in range(RATE)])
        rpp, env = self.build_project("R2-impulse", "Infinite FX", patch, wav)
        for online in (False, True):
            out = self.render(rpp, env, "online" if online else "offline", online)
            first = next((i for i, x in enumerate(out) if abs(x) > 1e-6), -1)
            offset = first - IMPULSE_AT if first >= 0 else None
            self.check(offset == 0, "M-6 R2 impulse: delay compensation residual 0 samples",
                       f"{'online' if online else 'offline'}: impulse at {IMPULSE_AT}, first output at {first}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--reaper", default=os.path.expanduser("~/Applications/REAPER.app/Contents/MacOS/REAPER"))
    ap.add_argument("--build", default="build-plugin")
    ap.add_argument("--out", default=None)
    ap.add_argument("--timeout", type=float, default=120.0, help="seconds per REAPER run")
    args = ap.parse_args()
    if not os.path.exists(args.reaper):
        sys.exit(f"REAPER not found at {args.reaper} (--reaper)")
    args.out = os.path.abspath(args.out or tempfile.mkdtemp(prefix="infinite-reaper-"))
    os.makedirs(args.out, exist_ok=True)
    os.environ.setdefault("INFINITE_PLUGIN_ENGINE",
                          os.path.join(ROOT, "build-c1", "Infinite.app", "Contents", "MacOS", "Infinite"))
    patches = os.path.join(ROOT, "tests", "plugin", "patches")
    r1, r2 = os.path.join(patches, "R1-instrument.inf"), os.path.join(patches, "R2-fx.inf")

    rig = Rig(args)
    try:
        rig.install()
        sig = os.path.join(args.out, "signal.wav")
        write_wav_f32(sig, [signal(t) for t in range(RATE * SECONDS)])
        rig.measure("R1", "Infinite Instrument", r1, None)
        rig.measure("R2", "Infinite FX", r2, sig)
        rig.impulse(r2)
    except RuntimeError as e:
        rig.check(False, "rig", str(e))
    finally:
        rig.kill_strays()
        rig.uninstall()
    left = subprocess.run(["pgrep", "-f", "--", "--plugin-engine"], capture_output=True, text=True).stdout.split()
    rig.check(not left, "no engine left", f"{len(left)} running")
    print(f"\nREAPER RIG: {'PASS' if rig.fails == 0 else 'FAIL'} (renders in {args.out})")
    return 1 if rig.fails else 0


if __name__ == "__main__":
    sys.exit(main())
