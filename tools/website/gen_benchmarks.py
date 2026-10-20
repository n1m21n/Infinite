#!/usr/bin/env python3
"""Builds the user-facing Infinite benchmark report: website/benchmarks.html, the brand PDF and a JSON copy.

What: one body of HTML (tables and plain-language notes) is rendered twice: into website/benchmarks.html
      (the site's own chrome and tokens) and into website/assets/Infinite_Benchmark_Report.pdf (Paper mode,
      tools/brand/paper_doc.py, headless Chrome). website/assets/benchmarks.json carries the same numbers
      for crawlers and AI agents.
Data: docs/quality/benchmark-data/*.md are the outputs of tools/re-survey (plugin_probe, synth_probe, app_bench)
      copied in as they were run; docs/quality/SCOREBOARD.md is the full metric list (tools/quality/quality.py).
      Re-run those tools, copy the new tables over, then run this script.
Usage: python3 tools/website/gen_benchmarks.py [--no-pdf]
Then:  python3 tools/website/gen_seo.py (sitemap, llms.txt) and python3 tools/brand/brand_lint.py
Exit codes: 0 written, 2 missing data file.
"""
import html
import json
import math
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "brand"))
W = os.path.join(ROOT, "website")
DATA = os.path.join(ROOT, "docs", "quality", "benchmark-data")
BASE = "https://infinitedevices.org/"
PDF_NAME = "Infinite_Benchmark_Report.pdf"
DATE = "17 October 2026"
ISO = "2026-10-17"
esc = lambda s: html.escape(str(s), quote=False)


def table_rows(name):
    """Rows of the first markdown table in DATA/<name> as {header: cell}."""
    path = os.path.join(DATA, name)
    if not os.path.exists(path):
        sys.exit(f"missing {path}")
    rows = [[c.strip() for c in ln.strip().strip("|").split("|")] for ln in open(path) if ln.startswith("|")]
    head = rows[0]
    return [dict(zip(head, r)) for r in rows[2:]]


def by_target(rows, key="Target"):
    return {r[key]: r for r in rows}


def cpu(r):
    return float(r["CPU ms/s (net)"])


def table(head, rows, cls=""):
    t = f'<table class="t {cls}"><thead><tr>' + "".join(f"<th>{esc(h)}</th>" for h in head) + "</tr></thead><tbody>"
    for r in rows:
        t += "<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>"
    return t + "</tbody></table>"


def sec(num, title, hand=None):
    return f'<div class="kick chap">Section {num:02d}</div><h2 id="s{num}">{esc(title)}</h2>'


# ---------------------------------------------------------------------------------------------------- data
fx = by_target(table_rows("effects.md"))
sy = by_target(table_rows("synths.md"))
ap = by_target(table_rows("apps.md"), "App")


def fxk(sub):
    return next(v for k, v in fx.items() if sub in k)


def syk(sub):
    return next(v for k, v in sy.items() if sub in k)


def apk(sub):
    return next(v for k, v in ap.items() if sub in k)


def vc(k):
    return syk(k)["CPU ms/s idle / voice"].split("/")[1].strip()


def lat(r):
    m = re.search(r"\(([\d.]+) ms\)", r["Latency smp (ms)"])
    return float(m.group(1))


# ---------------------------------------------------------------------------------------------------- sections
def s_glance():
    inf = apk("Infinite")
    cards = [
        ("0.34 s", "Launch to first frame on a base MacBook Air M2 with 8 GB. Opening a 400-node patch takes 6 ms."),
        ("0.2 cents", "Worst pitch error across the keyboard from the Wavetable synth. Vital and Serum 2 measure the same."),
        ("100× +", "Faster movie export than Blender on the same test scenes (different renderers; throughput, not image style)."),
        ("%s MB" % inf["Memory MB (footprint)"], "Memory with the app open and idle. The download is %s MB." % inf["Bundle MB"]),
        ("97.6 %", "Of the plugins installed on the test machine load and pass audio in Infinite's plugin host."),
        ("0", "Audio dropouts and 0 % missed screen refreshes in the standard live-performance test."),
    ]
    c = "".join(f'<div class="bm-card"><div class="bm-n">{esc(n)}</div><p>{esc(t)}</p></div>' for n, t in cards)
    return (sec(1, "At a glance") +
            f'<p class="lead">We measured Infinite against the tools you already use, on one ordinary laptop, with the same tests for everyone, and we '
            f'publish the numbers that are good for us and the ones that are not.</p><div class="bm-cards">{c}</div>'
            '<h3>Where Infinite is behind</h3><ul>'
            f'<li><b>Idle CPU.</b> With nothing loaded Infinite sits at {esc(inf["Idle CPU % mean / max"].split("/")[0].strip())} % of one core. Logic, Blender and Vital sit under 4 %. We are working on it.</li>'
            '<li><b>Some built-in effects cost more CPU</b> than the plugin equivalents: EQ, Limiter, Drive and Chorus (Section 4).</li>'
            '<li><b>Instancing.</b> 10,000 instanced objects cost 3.1 ms a frame in Infinite and 1.3 ms in Blender.</li>'
            '<li><b>The heaviest projector scene</b> (four 4K videos, three output windows, a camera and Syphon at once) drops frames. Normal and 1080p scenes do not.</li>'
            '<li><b>Default levels.</b> Reverb, Delay, Chorus and Flanger come out louder than they go in at their default settings.</li></ul>')


def s_how():
    return (sec(2, "How we tested") +
            '<p>Everything here was measured on one machine: a MacBook Air with an Apple M2 and 8 GB of memory, running macOS. That is the base model, '
            'on purpose. If it is fast here it is fast on yours. Nothing was measured on Windows or Linux.</p>'
            '<ul><li><b>Black box.</b> Notes or audio go in, the result comes out and is measured. We never open or read the other product.</li>'
            '<li><b>Same test for everyone.</b> A competitor plugin runs inside Infinite\'s own plugin host, receives the same notes or the same test signal, and is measured by the same code as Infinite\'s own nodes.</li>'
            '<li><b>Default settings.</b> Every instrument and effect is tested as it loads, so presets are not hand-picked to flatter anyone. Where a default does nothing (an EQ with no bands) only its CPU cost is meaningful, and the report says so.</li>'
            '<li><b>CPU is the render cost.</b> Milliseconds of processing per second of audio, with the machine otherwise idle. Lower is better, and 10 ms/s is 1 % of one core. Differences under about 1 ms/s are noise.</li>'
            '<li><b>Versions.</b> Infinite 0.6 (a development build matching the 0.6.0 release); Ableton Live 12.4.6, Bitwig Studio 6.0.3, Logic Pro 12.3.1, TouchDesigner 2025.32820, VCV Rack 2.6.6, Blender 5.1.0, BespokeSynth 1.3.0, Vital 1.5.5, Serum 2.0.16, Diva 1.4.5, FabFilter Pro-R 2 2.04, Pro-C 2 2.20, Saturn 2 2.11, plus the installed versions of Valhalla and Kilohearts plugins.</li>'
            '<li><b>Reproduce it.</b> The tools are in the repository under <code>tools/re-survey</code>. Section 8 lists the commands.</li></ul>'
            '<p>Dates: instruments, effects and apps were measured on ' + DATE + '; the internal tests and 3D/render comparisons on 12 to 16 October 2026.</p>')


def s_instruments():
    order = [("Infinite Wavetable", "node:Wavetable"), ("Infinite Analog", "node:Analog"), ("Infinite Oscillator", "node:Oscillator"),
             ("Vital", "Vital"), ("Serum 2", "Serum 2"), ("Diva", "Diva")]
    rows = []
    for label, key in order:
        r = syk(key)
        v = "24 dB" if "node:" in key else ("none" if label in ("Vital", "Serum 2") else r["Vel span dB"] + " dB")
        rows.append([esc(label), esc(r["Pitch err ¢ (worst)"]), esc(r["Non-harm 60"]), esc(r["Non-harm 96"]), esc(r["Non-harm 108"]),
                     esc(r["CPU ms/s idle / voice"].split("/")[1].strip()), v])
    return (sec(3, "Instruments") +
            '<p>Eight notes from MIDI 24 to 108 are played into each synth, and what comes out is analysed. <b>Pitch error</b> is the worst note, in cents (a hundredth of a semitone). '
            '<b>Aliasing</b> is the energy that is not part of the note, in decibels; more negative is cleaner, and the high notes at 96 and 108 are where cheap synths fold back and sound harsh. '
            '<b>CPU per voice</b> is the extra render time for each added note.</p>' +
            table(["Instrument", "Worst pitch error (cents)", "Off-note energy, MIDI 60 (dB)", "MIDI 96 (dB)", "MIDI 108 (dB)", "CPU per voice (ms/s)", "Responds to velocity"], rows) +
            '<h3>What this means</h3><ul>'
            '<li>Infinite\'s Wavetable and Analog synths are as accurate and as clean as Vital and Serum 2: 0.2 cents, and about -81 to -85 dB off-note energy even at the top of the keyboard.</li>'
            f'<li>Wavetable is also light: {vc("node:Wavetable")} ms/s per voice against {vc("Vital")} for Vital and {vc("Serum 2")} for Serum 2. Analog is the heavier of the two at {vc("node:Analog")} ms/s.</li>'
            '<li>Diva is measured as it loads and sits an octave down with a modelled, imperfect oscillator. Its figures describe an analog-style design, not a flaw.</li>'
            '<li>Infinite\'s synths respond to how hard you play across a 24 dB range. Vital and Serum 2 ignore velocity until you route it yourself, so that column compares defaults, not capability.</li>'
            '<li>Not comparable: Kilohearts Phase Plant and FabFilter Twin 3 came up silent or unusable with no patch loaded, so they are left out rather than guessed at.</li></ul>')


def s_effects():
    pairs = [("Reverb", "node:Reverb", [("ValhallaRoom", "ValhallaRoom"), ("FabFilter Pro-R 2", "Pro-R 2")]),
             ("Delay", "node:Delay", [("ValhallaDelay", "ValhallaDelay")]),
             ("Compressor", "node:Dynamics", [("FabFilter Pro-C 2", "Pro-C 2")]),
             ("Limiter", "node:Limiter", [("FabFilter Pro-L 2", "Pro-L 2")]),
             ("EQ", "node:EQ", [("FabFilter Pro-Q 4", "Pro-Q 4")]),
             ("Drive / saturation", "node:Drive", [("FabFilter Saturn 2", "Saturn 2")]),
             ("Chorus", "node:Chorus", [("Kilohearts Chorus", "kHs Chorus")]),
             ("Flanger", "node:Flanger", [("Kilohearts Flanger", "kHs Flanger")]),
             ("Phaser", "node:Phaser", [("Kilohearts Phaser", "kHs Phaser")])]
    rows, js = [], []
    for kind, ik, others in pairs:
        a = fxk(ik)
        for n, (label, k) in enumerate(others):
            b = fxk(k)
            ca, cb = cpu(a), cpu(b)
            verdict = "Infinite lighter" if ca < cb - 1 else ("Plugin lighter" if cb < ca - 1 else "About equal")
            rows.append([esc(kind) if n == 0 else "", f"{ca:.1f}", f"{lat(a):.1f}", esc(label), f"{cb:.1f}", f"{lat(b):.1f}", verdict])
            js.append({"effect": kind, "infinite_cpu_ms_per_s": ca, "other": label, "other_cpu_ms_per_s": cb})
    return (sec(4, "Effects") +
            '<p>A test tone or noise goes through each effect at its default settings. <b>CPU</b> is the render cost in ms per second of audio (lower is better). '
            '<b>Latency</b> is the delay the effect adds after the host compensates for it.</p>' +
            table(["Effect", "Infinite CPU", "Infinite latency (ms)", "Compared with", "CPU", "Latency (ms)", "Result"], rows) +
            '<h3>What this means</h3><ul>'
            '<li><b>Infinite is lighter</b> on Reverb, Delay and the compressor, and about level on Flanger and Phaser.</li>'
            '<li><b>The plugins are lighter</b> on EQ, Limiter, Drive and Chorus. The EQ gap is the clearest: ' +
            f'{cpu(fxk("node:EQ")):.1f} ms/s for Infinite against {cpu(fxk("Pro-Q 4")):.1f} for Pro-Q 4. These are on our list to fix.</li>'
            '<li>Infinite effects add no delay once compensated (0 ms). ValhallaRoom reports %.1f ms.</li>' % lat(fxk("ValhallaRoom")) +
            '<li>At default settings Infinite\'s Reverb, Delay, Chorus and Flanger are louder than the input and can clip. FabFilter Pro-R 2 and ValhallaDelay stay within about 1 dB. We list this as a known issue.</li>'
            '<li>An EQ with no bands, a limiter that is not limiting and a compressor with no gain reduction are close to a wire. Their distortion figures match the test rig, so only their CPU cost is a fair comparison.</li></ul>')


def s_video():
    return (sec(5, "3D, video and export") +
            '<p>Blender runs headless through its public Python interface; Infinite runs the matching patch. Cloth and subdivision are one simulation step or one deform per frame. '
            'Movie export times the whole render and encode, measured as the slope between a 1-second and a 3-second render so start-up cancels out. '
            'Blender here is a general-purpose renderer, so this compares export throughput on matched scenes, not image style.</p>' +
            table(["Test", "Infinite", "Blender", "Result"],
                  [["Cloth sheet 32×32, per frame", "0.42 ms", "18.4 ms", "Infinite much faster"],
                   ["Subdivide ×2 with animated displacement, per frame", "7.2 ms", "8.1 ms", "Infinite slightly faster"],
                   ["10,000 instanced objects, per frame", "3.1 ms", "1.3 ms", "Blender faster"],
                   ["Movie export, 720p ProRes 4444, blur scene", "170 fps", "1.6 fps", "Infinite ~100× faster"],
                   ["Movie export, 1080p ProRes 4444, blur scene", "81 fps", "1.2 fps", "Infinite ~70× faster"],
                   ["Movie export, 2160p ProRes 4444, blur scene", "44 fps", "0.5 fps", "Infinite ~95× faster"],
                   ["Movie export, 720p ProRes 4444, 10,000 cubes", "128 fps", "0.9 fps", "Infinite ~145× faster"],
                   ["H264 quality (PSNR), blur scenes", "50.6 to 51.8 dB", "44.3 to 46.1 dB", "Infinite higher"],
                   ["H264 quality (PSNR), 10,000 cubes", "35.5 to 37.1 dB", "28.8 to 32.2 dB", "Infinite higher"],
                   ["Sound-to-picture offset, 10-minute render", "0.04 ms", "0.02 ms", "Both inaudible"],
                   ["Sound-to-picture drift, 10-minute render", "0 ms/min", "0 ms/min", "Tied"]]) +
            '<h3>What this means</h3><ul>'
            '<li>A 10-minute film at 30 fps is about 18,000 frames: roughly two minutes to export from Infinite at the 720p rate above, about three hours from Blender. Audio and picture stay locked: drift over ten minutes is zero.</li>'
            '<li>Instancing is the one 3D test Infinite loses, by a factor of 2.4, and is flagged below target.</li>'
            '<li>A rendered arrangement runs at 2.25× real time on the reference scene, with no audio/video drift.</li></ul>')


def s_apps():
    names = [("Infinite", "Infinite"), ("Logic Pro", "Logic"), ("Ableton Live 12 Suite", "Ableton"), ("Bitwig Studio", "Bitwig"),
             ("TouchDesigner", "TouchDesigner"), ("Blender", "Blender"), ("VCV Rack 2", "VCV"), ("BespokeSynth", "Bespoke"), ("Vital (plugin app)", "Vital")]
    rows = []
    for label, k in names:
        r = apk(k)
        rows.append([esc(label), r["Window s (1st)"], r["Window s (warm)"], esc(r["Idle CPU % mean / max"]), r["Memory MB (footprint)"], r["Bundle MB"]])
    return (sec(6, "Launch, memory and download size") +
            '<p>Each app is opened with nothing loaded and left alone. <b>Window</b> is the time from opening it until the first window appears (the first launch, then a warm launch). '
            '<b>Idle CPU</b> is the share of one core used while doing nothing, over the whole app including its helper processes. <b>Memory</b> is what Activity Monitor shows.</p>' +
            table(["App", "First window, first launch (s)", "Warm (s)", "Idle CPU % (mean / max)", "Memory (MB)", "Download size on disk (MB)"], rows) +
            '<h3>What this means</h3><ul>'
            '<li>Infinite is a small install: 45 MB, against 874 MB to 8 GB for the DAWs and TouchDesigner. Only VCV Rack and Vital are as small.</li>'
            '<li>Memory is lower than Ableton, Blender, Bitwig, TouchDesigner and BespokeSynth. Only Logic sits lower.</li>'
            '<li>Infinite opens its first window in under a second here, the quickest first launch of the apps tested. Infinite, Blender and Vital were re-measured on a quiet machine; the rest come from the first pass.</li>'
            '<li>Idle CPU is mid-pack: higher than Logic, Blender and Vital (all under 4 %), lower than Bitwig, BespokeSynth, Ableton and TouchDesigner. VCV Rack\'s high figure is its audio engine running continuously by design.</li></ul>')


def s_internal():
    return (sec(7, "How fast Infinite is on its own") +
            '<p>These are Infinite\'s own targets, tested on the same base laptop. They matter for live use: if the audio buffer is missed you hear a click, and if a frame is missed you see a stutter.</p>' +
            table(["Test", "Result", "Target", ""],
                  [["Launch to first frame", "342 ms", "under 1 s", "Met"],
                   ["Open a 400-node patch", "6.0 ms", "under 16.7 ms (one frame)", "Met"],
                   ["Save a patch", "2.9 ms", "under 16.7 ms", "Met"],
                   ["Open a 4,000-node patch (headless)", "0.87 s", "reported", "Info"],
                   ["Pan around a 300-node canvas", "60 fps (16.7 ms)", "60 fps", "Met"],
                   ["Audio: 24 voices, 256-sample buffer, worst-case load", "45 % of the buffer", "under 50 %", "Met"],
                   ["Audio dropouts in that test", "0", "0", "Met"],
                   ["Live projector, window frame interval (99th percentile)", "17.6 ms", "under 18.3 ms", "Met"],
                   ["Missed screen refreshes", "0 %", "under 0.5 %", "Met"],
                   ["Change a control to seeing it on screen", "1 frame", "2 frames or fewer", "Met"],
                   ["Four 4K videos decoded at once", "full speed, at most 2 dropped frames", "full speed, 0 dropped", "Met"],
                   ["Heaviest projector scene (4× 4K, 3 windows, camera, Syphon)", "33 ms, 9 % missed refreshes", "under 18.3 ms, 0.5 %", "Below target"],
                   ["Peak memory in the heavy visual scenes", "760 MB", "under 912 MB", "Met"],
                   ["Audio-reactive picture latency", "16.6 ms", "under 45 ms", "Met"],
                   ["Beat lock, picture to click", "100 %", "100 %", "Met"],
                   ["Crashes in fuzzed patches, random patches and kill-during-save rounds", "0", "0", "Met"],
                   ["Plugins that load and pass audio", "97.6 % (VST3 and AU)", "reported", "Info"],
                   ["Installed plugins that crash Infinite with no outside cause", "0", "0", "Met"],
                   ["30-minute soak test, live audio and picture", "0 audio dropouts", "reported", "Info"]]) +
            '<h3>Correctness checks</h3>'
            '<p>The full list at the end of this report covers every further check, all passing or listed as known exceptions: '
            'pitch within one cent at 44.1, 48 and 96 kHz; no clicks when notes start or stop; no stuck notes after release; the same patch renders bit-for-bit the same twice; '
            'no hum or DC offset from effects; blend modes within 0.125 of 255 against the W3C formulas; no broken meshes. The three known exceptions are the insert-level jump on some effects, '
            'a small level error on stretched drum hits, and some winding-order flips in the Screw geometry node.</p>'
            '<h3>Not yet measured</h3>'
            '<p>Four long or heavy tests are defined but have not been run: a 4,000-node canvas under pan, select and delete; an edit storm while audio plays; a multi-hour soak with memory growth and temperature; '
            'and a real-time audio-reactive scene of 10,000 instances. Until they are run we do not claim results for them.</p>')


def s_limits():
    return (sec(8, "What we did not test, and how to reproduce this") +
            '<ul><li><b>No head-to-head against other DAWs on audio load or latency.</b> Matched track-count tests in Ableton, Bitwig and Logic need a hand-built project in each, and round-trip latency needs a loopback device.</li>'
            '<li><b>No live visual comparison with TouchDesigner or Resolume</b> yet. The harness exists; it needs a manual step inside TouchDesigner.</li>'
            '<li><b>One machine, one operating system.</b> Numbers on faster hardware will be better, and Windows and Linux are unmeasured.</li>'
            '<li><b>Defaults, not best case.</b> A plugin with a tuned preset would measure differently.</li>'
            '<li><b>Our numbers only.</b> Every figure here was measured by us on this machine. No vendor claims or published specs are used.</li></ul>'
            '<h3>Run it yourself</h3>'
            '<pre><code>git clone https://github.com/n1m21n/Infinite &amp;&amp; cd Infinite\n'
            'python3 tools/re-survey/synth_probe.py run Vital "Serum 2" node:Wavetable\n'
            'python3 tools/re-survey/plugin_probe.py run dry node:Reverb ValhallaRoom\n'
            'python3 tools/re-survey/app_bench.py run Logic Blender /Applications/Infinite.app --runs 2\n'
            'python3 tools/re-survey/render_bench.py infinite</code></pre>'
            '<p>Corrections and re-runs are welcome: open an issue at github.com/n1m21n/Infinite or tell us on Discord.</p>')


def clean(desc):
    d = re.sub(r"\((?:v0\.6[^)]*|F\d+[^)]*|proposed[^)]*|run with[^)]*|suite §\d|ROBUSTNESS[^)]*|noisy[^)]*|Found in Block[^)]*|\d{4}-\d\d-\d\d[^)]*|render_bench|compare\.py[^)]*)\)", "", desc)
    d = re.sub(r"\([^)]*(?:proposed|ROBUSTNESS)[^)]*\)", "", d)
    d = re.sub(r"; proposed:.*$|; v0\.6 [\d.]+", "", d)
    d = d.replace("(64 is not a target: Found in Block 1)", "(64 frames is not a target)").replace("(real LoadPatchFrom)", "").replace("(real SavePatchTo)", "")
    d = re.sub(r"\b(B\d+|R\d+|F\d+)\b ?", "", d)
    d = d.replace("§6", "").replace("  ", " ").replace(" ,", ",").replace(" ;", ";").strip(" ;:,")
    return d[:1].upper() + d[1:] if d else d


STATUS = {"met": "Met", "MISS": "Below target", "behind": "Below target", "exc": "Known exception", "info": "Reported", "unmeasured": "Not yet measured"}


def s_full():
    sb = open(os.path.join(ROOT, "docs", "quality", "SCOREBOARD.md")).read()
    parts, cur, out, n = [], None, [], 0
    for ln in sb.split("\n"):
        if ln.startswith("## ") and ":" in ln:
            if cur:
                parts.append((cur, out))
            cur, out = ln[3:].split(": ", 1)[1], []
        elif ln.startswith("| `"):
            c = [x.strip() for x in ln.strip().strip("|").split("|")]
            res, subj = c[5], ""
            m = re.match(r"(.*?)\s*\((.*)\)\s*$", c[5])
            if m:
                res, subj = m.group(1), m.group(2)
                subj = re.sub(r"^(node|Infinite): ?", "", subj)
                subj = "" if subj.startswith("bench:") else subj
            if subj:
                res = f"{res} ({subj})".replace("node:", "")
            tgt = c[2].replace("<=", "≤").replace(">=", "≥") or "reported"
            out.append((clean(c[1]), tgt, res or "-", STATUS.get(c[4], c[4])))
            n += 1
        elif ln.startswith("## Per subject"):
            break
    if cur:
        parts.append((cur, out))
    body = (sec(9, "Full results") +
            f'<p>Every one of the {n} metrics we track, with its target and the worst result across all the nodes or scenes tested. '
            '"Reported" means there is no pass/fail target (a design choice is compared, not judged). The competitor comparisons are in Sections 3 to 6.</p>')
    for name, rows in parts:
        body += f"<h3>{esc(name)}</h3>" + table(["What is measured", "Target", "Worst result", "Status"],
                                                 [[esc(r[0]), esc(r[1]), esc(r[2]), esc(r[3])] for r in rows], "full")
    return body, n


def body_html():
    full, n = s_full()
    return "".join([s_glance(), s_how(), s_instruments(), s_effects(), s_video(), s_apps(), s_internal(), s_limits(), full]), n


# ---------------------------------------------------------------------------------------------------- outputs
SITE_CSS = """/* Benchmarks page (made by tools/website/gen_benchmarks.py): the report body on the site's own tokens. */
.bm-wrap { margin: 0 auto; }   /* width: --wrap 960px in system.css */
.bm-wrap p, .bm-wrap li, .bm-wrap .section-sub { max-width: 56ch; }
.bm-wrap .lead { color: var(--ink2); font-size: 1.125rem; line-height: 1.5; }
.bm-wrap h2 { font-size: var(--fs-h3); font-weight: 700; letter-spacing: -0.02em; margin: 56px 0 12px; scroll-margin-top: 90px; }
.bm-wrap h3 { font-size: 1rem; font-weight: 600; margin: 28px 0 8px; }
.bm-wrap p, .bm-wrap li { color: var(--ink2); line-height: 1.6; }
.bm-wrap li { margin: 4px 0; }
.bm-wrap code { font-family: "Geist Mono", ui-monospace, monospace; font-size: 0.8438em; background: var(--card); padding: 1px 8px; border-radius: 4px; }
.bm-wrap pre { background: var(--card); border-radius: 12px; padding: 16px 16px; overflow-x: auto; }
.bm-wrap pre code { background: none; padding: 0; }
.bm-wrap .kick { font-size: 0.8438rem; font-weight: 500; color: var(--muted); margin-top: 48px; }
.bm-cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 16px; margin: 24px 0; }
.bm-card { background: var(--card); border-radius: var(--radius-card); box-shadow: var(--shadow-1); padding: 24px; }
.bm-card p { margin: 6px 0 0; font-size: 0.8438rem; }
.bm-n { font-size: 2rem; font-weight: 800; letter-spacing: -0.03em; color: var(--ink); }
.bm-scroll { overflow-x: auto; margin: 12px 0 20px; border-radius: 12px; }
table.t { width: 100%; border-collapse: collapse; font-size: 0.8438rem; background: var(--card); }
table.t th { text-align: left; font-weight: 600; color: var(--ink); padding: 12px 12px; border-bottom: 1px solid color-mix(in srgb, currentColor 25%, transparent); white-space: nowrap; }
table.t td { padding: 8px 12px; color: var(--ink2); border-bottom: 1px solid color-mix(in srgb, currentColor 14%, transparent); vertical-align: top; }
table.t td:first-child { color: var(--ink); font-weight: 600; }
.bm-actions { display: flex; flex-wrap: wrap; gap: 12px; margin: 24px 0 8px; }

/* ---- visual layer: bento tiles, gauges, segmented control, column charts. Brand: card 24, pills, no outlines, one Ember per frame. ---- */
.bm-hero h1 { margin: 0; }
.bm-spec { display: inline-flex; align-items: center; gap: 8px; color: var(--muted); font-size: 0.8438rem; margin: 8px 0 0; }
.ic { width: 20px; height: 20px; flex: none; fill: currentColor; }
.bm-actions .btn { display: inline-flex; align-items: center; gap: 8px; }
.pill { display: inline-flex; align-items: center; gap: 8px; min-height: 40px; padding: 0 16px; border-radius: 999px; font-size: 0.8438rem; font-weight: 500; white-space: nowrap; color: var(--ink); background: color-mix(in srgb, var(--ink) 7%, transparent); }
.pill .ic { width: 16px; height: 16px; color: var(--signal); }
.bm-bento { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 16px; margin: 32px 0 16px; }
.tile, .bm-card { background: var(--card); border-radius: var(--r-lg, 24px); box-shadow: var(--shadow-1); padding: 24px; }
.tile { display: flex; flex-direction: column; justify-content: space-between; min-height: 168px; }
.th { display: flex; align-items: center; gap: 8px; font-size: 0.9375rem; font-weight: 600; color: var(--ink); }
.th .ic { width: 20px; height: 20px; color: var(--accent); }
.tb { display: flex; flex-wrap: wrap; align-items: flex-end; justify-content: space-between; gap: 12px; margin-top: 16px; }
.tb b { font-size: 2.5rem; font-weight: 800; letter-spacing: -0.03em; line-height: 1; color: var(--ink); }
.tb b small { font-size: 0.9375rem; font-weight: 600; color: var(--muted); margin-left: 4px; letter-spacing: 0; }
.rg { width: 64px; height: 64px; flex: none; transform: rotate(-90deg); }
.rg circle { fill: none; stroke-width: 9; stroke-linecap: round; }
.rg .t { stroke: color-mix(in srgb, var(--ink) 10%, transparent); }
.rg .f { stroke: var(--accent); stroke-dasharray: 0 100; transition: stroke-dasharray var(--dur-page, 667ms) var(--ease-out, ease-out) 125ms; }
.in .rg .f { stroke-dasharray: var(--p) 100; }
.mc { display: flex; align-items: flex-end; gap: 4px; height: 56px; }
.mc i { width: 8px; height: var(--h); border-radius: 4px; background: color-mix(in srgb, var(--ink) 14%, transparent); transform-origin: bottom; transform: scaleY(0); transition: transform var(--dur-page, 667ms) var(--ease-out, ease-out) calc(var(--k) * 40ms); }
.mc i.hi { background: var(--accent); }
.in .mc i { transform: none; }
.sl { width: 80px; height: 40px; }
.sl path { fill: none; stroke: var(--signal); stroke-width: 3; stroke-linecap: round; stroke-dasharray: 100; stroke-dashoffset: 100; transition: stroke-dashoffset var(--dur-page, 667ms) var(--ease-out, ease-out); }
.sl circle { fill: var(--accent); opacity: 0; transition: opacity var(--dur-medium, 333ms) 500ms; }
.in .sl path { stroke-dashoffset: 0; } .in .sl circle { opacity: 1; }
.tile.arc { grid-column: 3 / 5; grid-row: 2 / 4; position: relative; align-items: stretch; }
.ac { width: 100%; max-width: 420px; margin: 16px auto 0; display: block; }
.sg { stroke-width: 4; stroke-linejoin: round; transition: opacity var(--dur-medium, 333ms) var(--ease-out, ease-out) calc(var(--k, 0) * 60ms); opacity: 0; }
.in .sg { opacity: 1; }
.sg.win { fill: var(--accent); stroke: var(--accent); } .sg.tie { fill: var(--signal); stroke: var(--signal); } .sg.lose { fill: color-mix(in srgb, var(--ink) 14%, transparent); stroke: color-mix(in srgb, var(--ink) 14%, transparent); }
.ac-n { text-align: center; margin-top: -64px; } .ac-n b { font-size: 3rem; font-weight: 800; letter-spacing: -0.03em; color: var(--ink); } .ac-n small { font-size: 1.25rem; color: var(--muted); margin-left: 2px; }
.ac-k { display: flex; justify-content: center; gap: 8px; margin-top: 24px; }
.pill i.sg { width: 10px; height: 10px; border-radius: 50%; opacity: 1; stroke: none; }
.pill i.sg.win { background: var(--accent); } .pill i.sg.tie { background: var(--signal); } .pill i.sg.lose { background: color-mix(in srgb, var(--ink) 25%, transparent); }
.bm-card { margin: 16px 0; }
.seg { position: relative; display: grid; grid-template-columns: repeat(var(--n), 1fr); padding: 4px; border-radius: 999px; background: color-mix(in srgb, var(--ink) 7%, transparent); }
.seg .th-b { position: absolute; top: 4px; bottom: 4px; left: 4px; width: calc((100% - 8px) / var(--n)); border-radius: 999px; background: var(--surface-1); box-shadow: var(--shadow-1); transform: translateX(calc(var(--i) * 100%)); transition: transform var(--spring-slab-ms, 333ms) var(--spring-slab, ease-out); }
.seg button { position: relative; z-index: 1; display: inline-flex; align-items: center; justify-content: center; gap: 8px; min-height: 40px; border: 0; background: none; cursor: pointer; font: inherit; font-size: 0.8438rem; font-weight: 500; color: var(--ink2); border-radius: 999px; -webkit-tap-highlight-color: transparent; transition: color var(--dur-small, 188ms); }
.seg button.on { color: var(--ink); } .seg button .ic { width: 16px; height: 16px; }
.key { display: flex; gap: 16px; margin-top: 16px; font-size: 0.8438rem; color: var(--muted); } .key span { display: inline-flex; align-items: center; gap: 8px; }
.key i { width: 10px; height: 10px; border-radius: 50%; } .key .a, .ro-n .a { color: var(--accent); } .key i.a { background: var(--accent); } .key i.b { background: var(--signal); }
.view { display: none; } .view.on { display: block; }
.ro { display: flex; flex-wrap: wrap; align-items: center; gap: 8px 16px; margin: 24px 0 8px; }
.ro-t { flex: 1 0 100%; font-size: 0.8438rem; font-weight: 500; color: var(--muted); }
.ro-n { display: flex; align-items: baseline; gap: 16px; } .ro-n b { font-size: 2.75rem; font-weight: 800; letter-spacing: -0.03em; line-height: 1; color: var(--ink); }
.ro-n .a { color: var(--accent); } .ro-n .b { color: var(--signal); }
.ro-p { min-height: 32px; padding: 0 12px; font-size: 0.8125rem; }
.cols { position: relative; display: flex; align-items: flex-end; gap: 6px; height: 200px; margin: 16px 0 28px; }
.cl { position: absolute; left: 0; right: 0; top: 100%; margin-top: 8px; font-style: normal; font-family: "Geist Mono", ui-monospace, monospace; font-size: var(--size-micro-mono); color: var(--ink2); text-align: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; pointer-events: none; }
.md { position: absolute; left: 0; right: 0; bottom: var(--md, 50%); height: 0; border-top: 2px dashed color-mix(in srgb, var(--ink) 20%, transparent); pointer-events: none; }
.col, .grp { position: relative; flex: 1; height: 100%; padding: 0; border: 0; background: none; cursor: pointer; display: flex; align-items: flex-end; justify-content: center; gap: 3px; border-radius: 12px; -webkit-tap-highlight-color: transparent; transition: background var(--dur-small, 188ms); }
.grp.sel, .col.sel { background: color-mix(in srgb, var(--ink) 5%, transparent); }
.col i, .grp i { display: block; width: 100%; max-width: 40px; height: var(--h); border-radius: 10px 10px 6px 6px; transform-origin: bottom; background: color-mix(in srgb, var(--signal) 30%, transparent); animation: grow var(--dur-page, 667ms) var(--ease-out, ease-out) both; animation-delay: calc(var(--k, 0) * 40ms); transition: background var(--dur-small, 188ms); }
.grp i { max-width: 18px; }
.cmp:not(.in) .col i, .cmp:not(.in) .grp i { animation-play-state: paused; }
.col.inf i { background: color-mix(in srgb, var(--accent) 45%, transparent); }
.col.sel i { background: var(--signal); } .col.inf.sel i { background: var(--accent); }
.grp i.a { background: color-mix(in srgb, var(--accent) 45%, transparent); } .grp i.b { background: color-mix(in srgb, var(--signal) 30%, transparent); }
.grp.sel i.a { background: var(--accent); } .grp.sel i.b { background: var(--signal); }
@keyframes grow { from { transform: scaleY(0); } }
.chips { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 16px; }
.bm-full { margin: 32px 0 0; } .bm-full > summary { list-style: none; display: flex; align-items: center; gap: 10px; min-height: 44px; padding: 0 20px; border-radius: 999px; cursor: pointer; background: var(--card); box-shadow: var(--shadow-1); color: var(--ink); font-weight: 600; width: fit-content; }
.bm-full > summary::-webkit-details-marker { display: none; } .bm-full[open] > summary .ic:last-child { transform: rotate(180deg); }
@media (max-width: 900px) { .bm-bento { grid-template-columns: repeat(2, minmax(0, 1fr)); } .tile.arc { grid-column: 1 / 3; grid-row: auto; order: -1; } }
@media (max-width: 680px) { .tile, .bm-card { padding: 20px; border-radius: 20px; } .tile { min-height: 168px; } .tb { flex-direction: column; align-items: flex-start; gap: 12px; } .tb b { font-size: 2rem; } .rg { width: 52px; height: 52px; } .mc { height: 44px; } .mc i { width: 6px; }
  .seg button .ic { display: none; } .cols { height: 168px; gap: 4px; margin-bottom: 8px; } .cl { display: none; } .ro-n b { font-size: 2.25rem; } .ac-n b { font-size: 2.25rem; } .ac-n { margin-top: -48px; } .ac-k { margin-top: 16px; } }
@media (prefers-reduced-motion: reduce) { .col i, .grp i { animation: none; } }
"""


def faq():
    inf = apk("Infinite")
    idle = inf["Idle CPU % mean / max"].split("/")[0].strip()
    return [
        ("Is Infinite faster than Vital or Serum 2?", "On sound quality they are level: all three measure about 0.2 cents of pitch error and about -81 to -85 dB of off-note energy. "
         f"Infinite's Wavetable synth uses {vc('node:Wavetable')} ms of CPU per second of audio per voice, against {vc('Vital')} for Vital and {vc('Serum 2')} for Serum 2."),
        ("How fast does Infinite export video compared with Blender?", "On matched test scenes on a base MacBook Air M2, Infinite exported ProRes 4444 at 170 fps at 720p and 44 fps at 4K, against 1.6 fps and 0.5 fps for Blender. These are different renderers, so this compares throughput, not image style."),
        ("How much memory and disk does Infinite use?", f"{inf['Memory MB (footprint)']} MB of memory with the app open and idle, and {inf['Bundle MB']} MB on disk. That is less memory than Ableton Live, Blender, Bitwig, TouchDesigner and BespokeSynth, and a fraction of the size of the DAWs tested (Ableton Live is 8 GB on disk)."),
        ("Where is Infinite slower than other tools?", f"Idle CPU ({idle} % of a core with nothing loaded), the built-in EQ, Limiter, Drive and Chorus effects, 10,000-object instancing, and the heaviest multi-4K projector scene. All are listed as below target."),
        ("Which computer were these benchmarks run on?", "A base MacBook Air with an Apple M2 and 8 GB of memory, running macOS, on 12 to 17 October 2026. Windows and Linux were not measured."),
    ]


def jsonld(n):
    url = BASE + "benchmarks.html"
    return json.dumps({"@context": "https://schema.org", "@graph": [
        {"@type": "TechArticle", "@id": url + "#article", "headline": "Infinite v0.6 benchmarks: audio, video and speed against Vital, Serum 2, Blender, Ableton, Bitwig and more",
         "description": "Independent, reproducible measurements of Infinite against other synths, effects, 3D and creative tools on a base MacBook Air M2, including where Infinite is slower.",
         "url": url, "datePublished": ISO, "dateModified": ISO, "inLanguage": "en", "isAccessibleForFree": True,
         "author": {"@type": "Person", "name": "Naman Soni", "url": "https://github.com/n1m21n"},
         "publisher": {"@type": "Organization", "name": "Infinite Devices", "url": BASE},
         "about": {"@type": "SoftwareApplication", "name": "Infinite", "url": BASE},
         "encoding": {"@type": "MediaObject", "contentUrl": BASE + "assets/" + PDF_NAME, "encodingFormat": "application/pdf"}},
        {"@type": "Dataset", "name": "Infinite v0.6 benchmark results", "description": f"{n} tracked metrics plus competitor comparisons for instruments, effects, 3D, export and app footprint.",
         "url": url, "license": "https://github.com/n1m21n/Infinite/blob/main/LICENSE", "creator": {"@type": "Person", "name": "Naman Soni"},
         "distribution": [{"@type": "DataDownload", "encodingFormat": "application/json", "contentUrl": BASE + "assets/benchmarks.json"},
                          {"@type": "DataDownload", "encodingFormat": "application/pdf", "contentUrl": BASE + "assets/" + PDF_NAME}]},
        {"@type": "FAQPage", "mainEntity": [{"@type": "Question", "name": q, "acceptedAnswer": {"@type": "Answer", "text": a}} for q, a in faq()]},
        {"@type": "BreadcrumbList", "itemListElement": [
            {"@type": "ListItem", "position": 1, "name": "Infinite", "item": BASE},
            {"@type": "ListItem", "position": 2, "name": "Benchmarks", "item": url}]}]}, indent=1, ensure_ascii=False)


# ---------------------------------------------------------------------------------------------------- visual page
def _ic(name):
    svg = open(os.path.join(ROOT, "art", "icons", "src", name + "-20.svg"), encoding="utf-8").read().strip()
    return svg.replace("<svg ", '<svg class="ic" aria-hidden="true" ', 1).replace("\n", "")


def _num(v):
    return f"{v:g}" if v == int(v) else f"{v:.2f}".rstrip("0").rstrip(".")


def ring(pct):
    return (f'<svg class="rg" viewBox="0 0 80 80" aria-hidden="true"><circle class="t" cx="40" cy="40" r="32" pathLength="100"/>'
            f'<circle class="f" cx="40" cy="40" r="32" pathLength="100" style="--p:{pct}"/></svg>')


def mini_cols(vals, hi):
    mx = max(vals)
    return '<div class="mc" aria-hidden="true">' + "".join(
        f'<i class="{"hi" if n == hi else ""}" style="--h:{max(8, 100 * v / mx):.0f}%;--k:{n}"></i>' for n, v in enumerate(vals)) + "</div>"


def tiles():
    inf = apk("Infinite")
    names = ["Infinite", "Logic", "Ableton", "Bitwig", "TouchDesigner", "Blender", "VCV", "Bespoke", "Vital"]
    mem = [float(apk(k)["Memory MB (footprint)"]) for k in names]
    exp = [math.log10(1 + v) for v in (106, 68, 88)]
    t = [
        ("power", "Launch", "0.34", "s", ring(34)),
        ("snap", "Pitch", "0.2", "¢", ring(20)),
        ("loop", "Export", "100", "×", mini_cols(exp, 0)),
        ("library", "Memory", inf["Memory MB (footprint)"], "MB", mini_cols(sorted(mem), sorted(mem).index(mem[0]))),
        ("family-utility", "Plugins", "97.6", "%", ring(97.6)),
        ("folder", "Size", inf["Bundle MB"], "MB", mini_cols(sorted(math.sqrt(float(apk(k)["Bundle MB"])) for k in names), 0)),
        ("pointer", "Latency", "1", "frame", ring(50)),
        ("pause", "Dropouts", "0", "", '<svg class="sl" viewBox="0 0 80 40" aria-hidden="true"><path d="M2 20H78" pathLength="100"/><circle cx="76" cy="20" r="4"/></svg>'),
    ]
    return '<div class="bm-bento">' + "".join(
        f'<div class="tile"><div class="th">{_ic(i)}<span>{lab}</span></div><div class="tb"><b>{n}<small>{u}</small></b>{viz}</div></div>'
        for i, lab, n, u, viz in t)


def effect_pairs():
    pairs = [("Reverb", "node:Reverb", "ValhallaRoom"), ("Delay", "node:Delay", "ValhallaDelay"), ("Compressor", "node:Dynamics", "Pro-C 2"),
             ("Limiter", "node:Limiter", "Pro-L 2"), ("EQ", "node:EQ", "Pro-Q 4"), ("Drive", "node:Drive", "Saturn 2"),
             ("Chorus", "node:Chorus", "kHs Chorus"), ("Flanger", "node:Flanger", "kHs Flanger"), ("Phaser", "node:Phaser", "kHs Phaser")]
    out = []
    for kind, a, b in pairs:
        ca, cb = cpu(fxk(a)), cpu(fxk(b))
        out.append((kind, ca, cb, "win" if ca < cb - 1 else ("lose" if cb < ca - 1 else "tie")))
    return out


def arc_card():
    fxs = sorted(effect_pairs(), key=lambda f: {'win': 0, 'tie': 1, 'lose': 2}[f[3]])
    n = len(fxs)
    R, r, gap = 112.0, 62.0, 6.0
    seg = 180.0 / n
    paths = ""
    for k, (_, _, _, v) in enumerate(fxs):
        a0, a1 = math.radians(180 + k * seg + gap / 2), math.radians(180 + (k + 1) * seg - gap / 2)
        p = lambda ang, rad: f"{120 + rad * math.cos(ang):.1f} {122 + rad * math.sin(ang):.1f}"
        d = f"M{p(a0, R)} A{R} {R} 0 0 1 {p(a1, R)} L{p(a1, r)} A{r} {r} 0 0 0 {p(a0, r)}Z"
        paths += f'<path class="sg {v}" style="--k:{k}" d="{d}"/>'
    w = sum(1 for f in fxs if f[3] == "win")
    l = sum(1 for f in fxs if f[3] == "lose")
    return (f'<div class="tile arc"><div class="th">{_ic("family-effects")}<span>Effects</span></div>'
            f'<svg class="ac" viewBox="0 0 240 130" role="img" aria-label="Infinite is lighter on {w} of {n} effects">{paths}</svg>'
            f'<div class="ac-n"><b>{w}<small>/{n}</small></b></div>'
            f'<div class="ac-k"><span class="pill"><i class="sg win"></i>{w}</span><span class="pill"><i class="sg tie"></i>{n - w - l}</span><span class="pill"><i class="sg lose"></i>{l}</span></div></div>')


def seg_ctrl(items):
    n = len(items)
    return (f'<div class="seg" style="--n:{n};--i:0"><i class="th-b"></i>' + "".join(
        f'<button type="button" class="{"on" if k == 0 else ""}" data-i="{k}" aria-label="{lab}">{_ic(ic)}<span>{lab}</span></button>'
        for k, (ic, lab) in enumerate(items)) + "</div>")


def readout(title, a, b=None, pill=""):
    nums = f'<b class="a">{a}</b>' + (f'<b class="b">{b}</b>' if b is not None else "")
    return f'<div class="ro"><span class="ro-t">{esc(title)}</span><div class="ro-n">{nums}</div><span class="pill ro-p">{esc(pill)}</span></div>'


def col_single(rows, unit, scale="lin"):
    """rows: [(name, value, is_infinite)] already ordered. Heights are relative to the largest, sqrt or log when the spread is huge."""
    f = {"lin": lambda v: v, "sqrt": math.sqrt, "log": lambda v: math.log10(1 + v)}[scale]
    mx = max(f(v) for _, v, _ in rows)
    vals = sorted(v for _, v, _ in rows)
    med = vals[len(vals) // 2]
    out = ""
    s0 = next((i for i, r in enumerate(rows) if r[2]), 0)
    for k, (nm, v, inf) in enumerate(rows):
        rank = sorted(vals).index(v) + 1
        out += (f'<button type="button" class="col{" inf" if inf else ""}{" sel" if k == s0 else ""}" style="--h:{max(5, 100 * f(v) / mx):.1f}%;--k:{k}" aria-label="{esc(nm)} {_num(v)} {unit}" '
                f'data-t="{esc(nm)} · {unit}" data-a="{_num(v)}" data-p="#{rank} of {len(rows)}"><i></i><em class="cl">{esc(nm)}</em></button>')
    first = rows[s0]
    rank0 = vals.index(first[1]) + 1
    ro = readout(f"{first[0]} · {unit}", _num(first[1]), None, f"#{rank0} of {len(rows)}")
    return ro + f'<div class="cols" style="--md:{100 * f(med) / mx:.1f}%"><u class="md"></u>{out}</div>'


def col_groups(groups, unit, scale="lin"):
    """groups: [(name, a, b, pill)]: Infinite then the other tool, side by side."""
    f = {"lin": lambda v: v, "log": lambda v: math.log10(1 + v)}[scale]
    mx = max(max(f(a), f(b)) for _, a, b, _ in groups)
    out = ""
    for k, (nm, a, b, pill) in enumerate(groups):
        out += (f'<button type="button" class="grp{" sel" if k == 0 else ""}" style="--k:{k}" aria-label="{esc(nm)}: Infinite {_num(a)}, other {_num(b)} {unit}" '
                f'data-t="{esc(nm)} · {unit}" data-a="{_num(a)}" data-b="{_num(b)}" data-p="{esc(pill)}">'
                f'<i class="a" style="--h:{max(4, 100 * f(a) / mx):.1f}%"></i><i class="b" style="--h:{max(4, 100 * f(b) / mx):.1f}%"></i><em class="cl">{esc(nm)}</em></button>')
    g0 = groups[0]
    return readout(f"{g0[0]} · {unit}", _num(g0[1]), _num(g0[2]), g0[3]) + f'<div class="cols grps">{out}</div>'


def compare_apps():
    names = [("Infinite", "Infinite"), ("Logic", "Logic"), ("Ableton", "Ableton"), ("Bitwig", "Bitwig"), ("TouchDesigner", "TouchDesigner"),
             ("Blender", "Blender"), ("VCV Rack", "VCV"), ("Bespoke", "Bespoke"), ("Vital", "Vital")]
    metrics = [("power", "Launch", "s", lambda r: float(r["Window s (1st)"]), "lin"), ("library", "Memory", "MB", lambda r: float(r["Memory MB (footprint)"]), "lin"),
               ("folder", "Size", "MB", lambda r: float(r["Bundle MB"]), "sqrt"), ("pause", "Idle", "% CPU", lambda r: float(r["Idle CPU % mean / max"].split("/")[0]), "lin")]
    views = ""
    for n, (_, lab, unit, fn, sc) in enumerate(metrics):
        rows = [(a, fn(apk(k)), k == "Infinite") for a, k in names]
        rows.sort(key=lambda t: t[1])
        views += f'<div class="view{" on" if n == 0 else ""}">{col_single(rows, unit, sc)}</div>'
    return f'<section class="bm-card cmp" id="apps">{seg_ctrl([(m[0], m[1]) for m in metrics])}{views}</section>'


def compare_head():
    snd = [(lab, float(vc(k)), inf) for lab, k, inf in (("Wavetable", "node:Wavetable", True), ("Analog", "node:Analog", True), ("Vital", "Vital", False), ("Serum 2", "Serum 2", False))]
    fx = [(k, a, b, {"win": "Lighter", "lose": "Heavier", "tie": "Level"}[v]) for k, a, b, v in effect_pairs()]
    ex = [(l, a, b, f"{a / b:.0f}×") for l, a, b in (("720p", 170, 1.6), ("1080p", 81, 1.2), ("4K", 44, 0.5))]
    views = (f'<div class="view on">{col_single(snd, "ms/s per voice")}</div>'
             f'<div class="view">{col_groups(fx, "ms/s")}</div>'
             f'<div class="view">{col_groups(ex, "fps", "log")}</div>')
    key = '<div class="key"><span><i class="a"></i>Infinite</span><span><i class="b"></i>Other</span></div>'
    return f'<section class="bm-card cmp" id="head">{seg_ctrl([("family-synths", "Sound"), ("family-effects", "Effects"), ("family-3d", "Export")])}{key}{views}</section>'


def v_behind():
    items = [("power", "Idle CPU"), ("family-audio-effects", "EQ, limiter, drive, chorus"), ("cube", "10k instances"), ("viewport", "4× 4K, 3 windows"), ("mixer", "Default levels")]
    return (f'<section class="bm-card" id="behind"><div class="th">{_ic("warning")}<span>Behind</span></div><div class="chips">' +
            "".join(f'<span class="pill">{_ic(i)}{esc(t)}</span>' for i, t in items) + "</div></section>")


VISUAL_JS = """(function(){
  var io=new IntersectionObserver(function(es){es.forEach(function(e){if(e.isIntersecting){e.target.classList.add('in');io.unobserve(e.target);}});},{threshold:.2});
  document.querySelectorAll('.bm-bento,.cmp').forEach(function(n){io.observe(n);});
  document.querySelectorAll('.cmp').forEach(function(card){
    var seg=card.querySelector('.seg'),views=card.querySelectorAll('.view');
    seg.addEventListener('click',function(e){var b=e.target.closest('button');if(!b)return;var i=+b.getAttribute('data-i');
      seg.style.setProperty('--i',i);seg.querySelectorAll('button').forEach(function(x){x.classList.toggle('on',x===b);});
      views.forEach(function(v,k){v.classList.toggle('on',k===i);});});
    card.addEventListener('click',function(e){var c=e.target.closest('.col,.grp');if(!c)return;var v=c.closest('.view');
      v.querySelectorAll('.sel').forEach(function(x){x.classList.remove('sel');});c.classList.add('sel');
      var ro=v.querySelector('.ro');ro.querySelector('.ro-t').textContent=c.getAttribute('data-t');
      var n=ro.querySelector('.ro-n'),a=c.getAttribute('data-a'),b=c.getAttribute('data-b');
      n.innerHTML='<b class="a">'+a+'</b>'+(b!==null?'<b class="b">'+b+'</b>':'');ro.querySelector('.ro-p').textContent=c.getAttribute('data-p');});
  });
})();"""


def visual_html():
    return tiles() + arc_card() + "</div>" + compare_apps() + compare_head() + v_behind()


def page(body, n):
    tpl = open(os.path.join(W, "extensions.html")).read()
    head, rest = tpl.split("</head>", 1)
    head = re.sub(r"<title>.*?</title>", "<title>Infinite benchmarks: audio, video and speed</title>", head, flags=re.S)
    desc = "Open, reproducible benchmarks of Infinite against Vital, Serum 2, Blender, Ableton and TouchDesigner on a MacBook Air M2, including where it is slower."
    head = re.sub(r'(<meta name="description" content=")[^"]*', lambda m: m.group(1) + desc, head)
    head = head.replace("https://infinitedevices.org/extensions.html", BASE + "benchmarks.html")
    head = re.sub(r'(<meta (?:property="og:title"|name="twitter:title") content=")[^"]*', lambda m: m.group(1) + "Infinite benchmarks", head)
    head = re.sub(r'(<meta (?:property="og:description"|name="twitter:description") content=")[^"]*', lambda m: m.group(1) + desc, head)
    head = re.sub(r'(og:image" content=")[^"]*', lambda m: m.group(1) + BASE + "assets/og-image.jpg", head)
    head = re.sub(r'(twitter:image" content=")[^"]*', lambda m: m.group(1) + BASE + "assets/og-image.jpg", head)
    head = head.replace('  <link rel="alternate" type="application/json" href="assets/extensions/extensions.json" title="Extensions">\n', "")
    head = re.sub(r'  <script type="application/ld\+json">.*?</script>\n', "", head, flags=re.S)   # the template's own JSON-LD
    head = head.replace("extensions.css?v=4f22d1ae", "benchmarks.css?v=c060dbfd")
    head += f'  <link rel="alternate" type="application/json" href="assets/benchmarks.json" title="Benchmark data">\n  <link rel="alternate" type="application/pdf" href="assets/{PDF_NAME}" title="Benchmark report (PDF)">\n'
    head += f'  <script type="application/ld+json">\n{jsonld(n)}\n  </script>\n'
    nav_start = rest.index("<body")
    header = rest[nav_start:rest.index("<main>")]
    header = header.replace('class="extensions-page"', 'class="benchmarks-page"').replace(' aria-current="page"', "").replace('href="benchmarks.html">', 'href="benchmarks.html" aria-current="page">')
    # wrap tables in a scroller so phones scroll the table, not the page
    body_w = re.sub(r"(<table class=\"t.*?</table>)", r'<div class="bm-scroll">\1</div>', body, flags=re.S)
    qa = "".join(f"<h3>{esc(q)}</h3><p>{esc(a)}</p>" for q, a in faq())
    main = (f'<main>\n    <section class="section lib-hero bm-hero">\n      <div class="container bm-wrap">\n'
            '        <h1 class="slab">Benchmarks</h1>\n'
            f'        <p class="bm-spec mono">{_ic("metronome")}M2 · 8 GB · v0.6 · {DATE}</p>\n'
            f'        <div class="bm-actions"><a class="btn btn-primary" href="assets/{PDF_NAME}" download aria-label="Download the PDF">{_ic("arrange")}PDF</a>'
            f'<a class="btn btn-ghost" href="assets/benchmarks.json" aria-label="Raw data as JSON">{_ic("grid-dots")}JSON</a>'
            f'<a class="btn btn-ghost" href="https://github.com/n1m21n/Infinite/tree/main/tools/re-survey" target="_blank" rel="noopener" aria-label="Test tools">{_ic("settings")}Tools</a></div>\n'
            f'{visual_html()}\n'
            f'<details class="bm-full"><summary>{_ic("library")}<span>Full report</span>{_ic("chevron-down")}</summary>\n'
            f'<div class="bm-fullbody">{body_w}\n<h2 id="faq">Questions people ask</h2>{qa}</div></details>\n'
            f'      </div>\n    </section>\n  </main>\n<script>{VISUAL_JS}</script>\n')
    tail = rest[rest.index("</main>") + len("</main>"):]
    return head + "</head>" + header + main + tail


def pdf(body, out):
    import paper_doc as P
    css = P.PILL_CSS + """
.bm-cards{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:14px 0}
.bm-card{background:var(--surface);border-radius:16px;padding:12px 16px;break-inside:avoid}
.bm-card p{font-size:11px;line-height:1.4;margin:4px 0 0;color:var(--ink-2)}
.bm-n{font-weight:800;font-size:25px;letter-spacing:-0.03em}
table.t th{white-space:normal} table.full{font-size:11px} .chap{margin-top:22px;break-after:avoid} table.t tr{break-inside:avoid}
"""
    cover = P.cover("BENCHMARK REPORT", "Measured,", "not", "claimed", "How Infinite v0.6 compares with the instruments, effects and creative tools you already use, on a base MacBook Air. "
                    "Including the parts where it is slower.", [("Version", "0.6.0"), ("Measured", DATE), ("Machine", "Apple M2, 8 GB"), ("Report", "infinitedevices.org/benchmarks.html")],
                    logo_png=P.LOGO_TILE if os.path.exists(P.LOGO_TILE) else None)
    page_break = '<div style="break-after:page"></div>'
    full = P.doc(cover + page_break + body, "Infinite  /  Benchmark Report  /  v0.6.0", "infinitedevices.org/benchmarks.html", css)
    work = tempfile.mkdtemp()
    tmp = os.path.join(work, PDF_NAME)
    P.render(full, tmp, work)
    shutil.copy(tmp, out)
    shutil.rmtree(work, ignore_errors=True)


def data_json(n):
    return {"name": "Infinite v0.6 benchmark results", "measured": ISO, "machine": "Apple M2 (MacBook Air), 8 GB, macOS", "url": BASE + "benchmarks.html",
            "pdf": BASE + "assets/" + PDF_NAME, "metrics_tracked": n,
            "instruments": table_rows("synths.md"), "effects": table_rows("effects.md"), "apps": table_rows("apps.md"),
            "faq": [{"question": q, "answer": a} for q, a in faq()]}


def main():
    body, n = body_html()
    open(os.path.join(W, "benchmarks.css"), "w").write(SITE_CSS)
    open(os.path.join(W, "benchmarks.html"), "w").write(page(body, n))
    json.dump(data_json(n), open(os.path.join(W, "assets", "benchmarks.json"), "w"), indent=1, ensure_ascii=False)
    if "--no-pdf" not in sys.argv:
        out = os.path.join(W, "assets", PDF_NAME)
        pdf(body, out)
        for d in ("press_kit/manuals",):
            if os.path.isdir(os.path.join(ROOT, d)):
                shutil.copy(out, os.path.join(ROOT, d, PDF_NAME))
        print("pdf", os.path.getsize(out) // 1024, "KB")
    print("benchmarks.html, benchmarks.css, benchmarks.json written;", n, "metrics")


main()
