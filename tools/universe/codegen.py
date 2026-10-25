"""Code-node universe: programs for the nodes that take a language (Field Synth / Effect / Notes / FieldPixel).

Each family is its own pool entry, e.g. "Field Synth:fm", so the atlas learns per family and the modulation
targets (p.<param>) are exact. `register(facts)` adds the entries; `source(family, seed)` writes the program.
Seed picks constants and structural variants; every declared param is used by the program.
"""
import copy, json, os, random, re

TAU = "6.283185"


def _rng(fam, seed):
    return random.Random("%s:%d" % (fam, seed))


def _p(r, name, lo, hi, log=False):
    import math
    v = math.exp(r.uniform(math.log(lo), math.log(hi))) if log else r.uniform(lo, hi)
    return "param float %s = %.4g [%.4g, %.4g]" % (name, v, lo, hi)


# ------------------------------------------------------------------ Field Synth
SYNTH_PARAMS = {  # name: (lo, hi, log)
    "cutoff": (200, 10000, True), "res": (0.0, 0.9, False), "drive": (1.0, 4.0, False),
    "attack": (0.005, 0.5, True), "decay": (0.05, 3.0, True),
    "detune": (0.0, 0.03, False), "index": (0.0, 8.0, False), "ratio": (0.5, 8.0, False),
    "width": (0.05, 0.95, False), "tilt": (0.05, 1.5, False), "vib": (0.0, 1.0, False), "sub": (0.0, 1.0, False),
}
SYNTH_FAMS = {
    "saw": ["cutoff", "res", "drive", "attack", "decay", "detune", "sub"],
    "pulse": ["cutoff", "res", "drive", "attack", "decay", "width", "vib"],
    "tri": ["cutoff", "res", "attack", "decay", "sub", "vib", "drive"],
    "fm": ["cutoff", "attack", "decay", "index", "ratio", "drive", "vib"],
    "additive": ["cutoff", "attack", "decay", "tilt", "detune", "vib"],
    "sync": ["cutoff", "res", "attack", "decay", "ratio", "drive", "sub"],
    "super": ["cutoff", "res", "attack", "decay", "detune", "drive"],
}


def synth_source(fam, seed):
    r = _rng("Field Synth:" + fam, seed)
    keys = SYNTH_FAMS[fam]
    L = [_p(r, k, *SYNTH_PARAMS[k]) for k in keys]
    st = ["ph1", "ph2", "ph3", "env", "lp", "bp", "lfo"]
    L += ["state float %s = 0.0" % s for s in st]
    has = set(keys)
    L.append("")
    atk = "attack" if "attack" in has else "0.01"
    dec = "decay" if "decay" in has else "0.8"
    L.append("rateUp = clamp(1.0 / (sr * max(%s, 0.003)), 0.0001, 0.1)" % atk)
    L.append("rateDn = clamp(1.0 / (sr * max(%s, 0.05)), 0.000002, 0.01)" % dec)
    L.append("target = if(gate > 0.5, noteVel, 0.0)")
    L.append("env = env + (target - env) * if(target > env, rateUp, rateDn)")
    L.append("lfo = (lfo + (%.3f) / sr) %% 1.0" % r.uniform(3.0, 7.0))
    f = "freq"
    if "vib" in has:
        f = "(freq * (1.0 + vib * 0.012 * sin(%s * lfo)))" % TAU
    L.append("f0 = %s" % f)
    det = "detune" if "detune" in has else "0.0"
    L.append("ph1 = (ph1 + f0 / sr) %% 1.0")
    if fam == "saw":
        L.append("ph2 = (ph2 + f0 * (1.0 + %s) / sr) %% 1.0" % det)
        L.append("raw = ((ph1 * 2.0 - 1.0) + (ph2 * 2.0 - 1.0)) * 0.5")
    elif fam == "pulse":
        L.append("raw = if(ph1 < width, 1.0, -1.0) * 0.7")
    elif fam == "tri":
        L.append("raw = (abs(ph1 * 4.0 - 2.0) - 1.0) * 0.9")
    elif fam == "fm":
        L.append("ph2 = (ph2 + f0 * ratio / sr) %% 1.0")
        L.append("raw = sin(%s * ph1 + index * env * sin(%s * ph2)) * 0.8" % (TAU, TAU))
    elif fam == "additive":
        n = r.choice([2, 3, 3])
        terms = " + ".join("sin(%s * ph1 * %d.0) / (1.0 + tilt * %d.0)" % (TAU, k, k * k) for k in range(1, n + 1))
        L.append("raw = (%s) * (1.0 + detune * 10.0) * 0.4" % terms)
    elif fam == "sync":
        L.append("ph2 = if(ph1 < f0 / sr, 0.0, (ph2 + f0 * ratio / sr) %% 1.0)")
        L.append("raw = (ph2 * 2.0 - 1.0) * 0.8")
    elif fam == "super":
        L.append("ph2 = (ph2 + f0 * (1.0 + detune) / sr) %% 1.0")
        L.append("ph3 = (ph3 + f0 * (1.0 - detune * 1.3) / sr) %% 1.0")
        L.append("raw = (ph1 + ph2 + ph3) * 0.4 - 0.6")
    if "sub" in has:
        L.append("ph3 = (ph3 + f0 * 0.5 / sr) %% 1.0" if fam != "super" else "sub2 = 0.0")
        if fam != "super":
            L.append("raw = raw + sub * sin(%s * ph3) * 0.6" % TAU)
        else:
            L.append("raw = raw + sub * sin(%s * ph1 * 0.5) * 0.5" % TAU)
    L.append("x = raw * env")
    if "cutoff" in has:
        L.append("g = clamp(2.0 * sin(3.14159 * min(cutoff, sr * 0.45) / sr), 0.001, 1.2)")
        if "res" in has and r.random() < 0.7:
            L.append("q = 1.0 - res * 0.95")
            L.append("lp = lp + g * bp")
            L.append("bp = bp + g * ((x - lp) - q * bp)")
            L.append("y = lp")
        else:
            L.append("lp = lp + g * (x - lp)")
            L.append("y = lp")
            if "res" in has:
                L.append("bp = lp - bp * 0.0")
                L.append("y = y + res * (lp - bp) * 0.0 + res * 0.3 * (x - lp)")
    else:
        L.append("y = x")
    if "drive" in has:
        L.append("y = y * drive")
        L.append("y = y / (1.0 + abs(y))" if r.random() < 0.5 else "y = clamp(y, -0.9, 0.9)")
    L.append("out = y * 0.8")
    return "\n".join(L) + "\n"


# ------------------------------------------------------------------ Field Effect
FX_PARAMS = {
    "mix": (0.0, 1.0, False), "drive": (1.0, 8.0, False), "tone": (400, 12000, True), "rate": (0.05, 12.0, True),
    "depth": (0.0, 1.0, False), "fb": (0.0, 0.92, False), "time": (0.01, 0.5, True), "bits": (2.0, 14.0, False),
    "hold": (1.0, 40.0, True), "pitch": (50, 2000, True), "shape": (0.0, 1.0, False), "width": (0.0, 1.0, False),
}
FX_FAMS = {
    "crush": ["bits", "hold", "tone", "drive", "mix"],
    "fold": ["drive", "shape", "tone", "mix"],
    "echo": ["time", "fb", "tone", "mix", "depth"],
    "comb": ["pitch", "fb", "tone", "mix"],
    "ring": ["pitch", "depth", "shape", "mix"],
    "trem": ["rate", "depth", "shape", "mix"],
    "chorus": ["rate", "depth", "time", "fb", "mix"],
    "stutter": ["time", "hold", "fb", "mix", "shape"],
}


def fx_source(fam, seed):
    r = _rng("Field Effect:" + fam, seed)
    keys = FX_FAMS[fam]
    L = [_p(r, k, *FX_PARAMS[k]) for k in keys]
    N = 12000
    if fam in ("echo", "comb", "chorus", "stutter"):
        L.append("state float buf[%d] = 0.0" % N)
        L.append("state float wpos = 0.0")
    L += ["state float lp = 0.0", "state float ph = 0.0", "state float held = 0.0", "state float cnt = 0.0", ""]
    tone = "lp = lp + clamp(2.0 * sin(3.14159 * min(tone, sr * 0.45) / sr), 0.001, 1.0) * (%s - lp)"
    if fam == "crush":
        L += ["cnt = cnt + 1.0", "held = if(cnt >= hold, in * drive, held)", "cnt = if(cnt >= hold, 0.0, cnt)",
              "lv = pow(2.0, bits)", "q = floor(held * lv + 0.5) / lv", tone % "q", "wet = lp"]
    elif fam == "fold":
        L += ["x = in * drive", "folded = sin(x * (1.0 + shape * 3.0))" if r.random() < 0.5 else "folded = x / (1.0 + abs(x) * (0.5 + shape * 4.0))",
              tone % "folded", "wet = lp"]
    elif fam == "echo":
        L += ["d = clamp(time * sr, 4.0, %d.0)" % (N - 2), "ph = (ph + %.3f / sr) %% 1.0" % r.uniform(0.2, 1.5),
              "d = clamp(d + depth * 40.0 * sin(%s * ph), 4.0, %d.0)" % (TAU, N - 2),
              "idx = (wpos - d + %d.0) %% %d.0" % (N * 2, N), "tap = buf[idx]", tone % "tap",
              "buf[wpos] = in + lp * fb", "wpos = (wpos + 1.0) %% %d.0" % N, "wet = lp"]
    elif fam == "comb":
        L += ["d = clamp(sr / pitch, 4.0, %d.0)" % (N - 2), "idx = (wpos - d + %d.0) %% %d.0" % (N * 2, N),
              "tap = buf[idx]", tone % "tap", "buf[wpos] = in + lp * fb", "wpos = (wpos + 1.0) %% %d.0" % N, "wet = lp + in * 0.0"]
    elif fam == "ring":
        L += ["ph = (ph + pitch / sr) %% 1.0", "car = sin(%s * ph)" % TAU, "car = car + shape * (if(ph < 0.5, 1.0, -1.0) - car)",
              "wet = in * (1.0 - depth + depth * car)"]
    elif fam == "trem":
        L += ["ph = (ph + rate / sr) %% 1.0", "lfo = sin(%s * ph)" % TAU,
              "lfo = lfo + shape * (if(ph < 0.5, 1.0, -1.0) - lfo)", "wet = in * (1.0 - depth * 0.5 * (1.0 + lfo))"]
    elif fam == "chorus":
        L += ["ph = (ph + rate * 0.2 / sr) %% 1.0", "d = clamp(time * sr * (1.0 + depth * 0.5 * sin(%s * ph)), 4.0, %d.0)" % (TAU, N - 2),
              "idx = (wpos - d + %d.0) %% %d.0" % (N * 2, N), "tap = buf[idx]", "buf[wpos] = in + tap * fb",
              "wpos = (wpos + 1.0) %% %d.0" % N, "wet = (in + tap) * 0.5"]
    elif fam == "stutter":
        L += ["len = clamp(time * sr, 64.0, %d.0)" % (N - 2), "cnt = cnt + 1.0",
              "rec = if(cnt < len, 1.0, 0.0)", "buf[wpos] = if(rec > 0.5, in, buf[wpos] * fb)",
              "wpos = (wpos + 1.0) %% len", "cnt = if(cnt >= len * hold, 0.0, cnt)",
              "wet = if(rec > 0.5, in, buf[wpos])", "wet = wet + shape * 0.0"]
    L.append("out = in * (1.0 - mix) + wet * mix")
    return "\n".join(L) + "\n"


# ------------------------------------------------------------------ Field Notes
NOTE_PARAMS = {"wander": (0.5, 3.0, False), "chance": (0.2, 1.0, False), "octave": (-1.0, 2.0, False),
               "accent": (0.0, 1.0, False), "len": (0.2, 1.0, False), "spread": (1.0, 7.0, False)}
NOTE_FAMS = {
    "wander": ["wander", "chance", "accent", "octave"],
    "euclid": ["spread", "accent", "len", "octave"],
    "chance": ["chance", "spread", "len", "octave"],
    "walk": ["wander", "spread", "accent", "len"],
}


def notes_source(fam, seed):
    r = _rng("Field Notes:" + fam, seed)
    keys = NOTE_FAMS[fam]
    grid = r.choice(["1/16", "1/8", "1/16", "1/4"])
    L = [_p(r, k, *NOTE_PARAMS[k]) for k in keys]
    L += ["state float step = 0", "state float pos = 0", ""]
    oct_ = "floor(octave) * 7" if "octave" in keys else "0"
    L.append("if (tick(%s) > 0.5) {" % grid)
    if fam == "wander":
        L += ["   d = floor(abs(sin(step * wander)) * 6)", "   acc = if((step %% 4) < 0.5, 1, 0)",
              "   keep = if(rand() < chance, 1, 0)", "   note(deg(d) + %s, (0.5 + 0.45 * accent * acc) * keep, %s)" % (oct_, grid)]
    elif fam == "euclid":
        k = r.choice([3, 5, 7])
        L += ["   hit = if(((step * %d) %% 16) < %d, 1, 0)" % (k, k),
              "   d = floor(rand() * spread)", "   note(deg(d) + %s, (0.45 + 0.5 * accent * hit) * hit, %s * len * 2)" % (oct_, grid)]
    elif fam == "chance":
        L += ["   keep = if(rand() < chance, 1, 0)", "   d = floor(rand() * spread)",
              "   note(deg(d) + %s, 0.6 * keep, %s * len * 2)" % (oct_, grid)]
    elif fam == "walk":
        L += ["   pos = pos + floor(rand() * 3) - 1 + wander * 0.0", "   pos = clamp(pos, 0 - spread, spread)",
              "   acc = if((step %% 4) < 0.5, 1, 0)", "   note(deg(pos + spread) , 0.5 + 0.45 * accent * acc, %s * len * 2)" % grid]
    L += ["   step = (step + 1) % 16", "}"]
    return "\n".join(L) + "\n"


# ------------------------------------------------------------------ FieldPixel
PIX_PARAMS = {
    "speed": (0.0, 2.0, False), "freq": (2.0, 40.0, True), "warp": (0.0, 2.0, False), "hue": (0.0, 1.0, False),
    "rate": (0.01, 1.0, True), "arms": (1.0, 24.0, False), "scale": (1.0, 12.0, False), "sharp": (0.5, 8.0, False),
    "glow": (0.0, 2.0, False), "zoom": (0.3, 3.0, False),
}
PIX_FAMS = {
    "rings": ["speed", "freq", "warp", "hue", "glow"],
    "spiral": ["speed", "arms", "freq", "hue", "rate"],
    "plasma": ["speed", "scale", "warp", "hue", "sharp"],
    "grid": ["speed", "scale", "warp", "hue", "glow"],
    "feedback": ["speed", "freq", "warp", "rate", "hue"],
    "tunnel": ["speed", "arms", "zoom", "hue", "glow"],
}


def pixel_source(fam, seed):
    r = _rng("FieldPixel:" + fam, seed)
    keys = PIX_FAMS[fam]
    L = [_p(r, k, *PIX_PARAMS[k]) + ";" for k in keys]
    if fam == "feedback":
        L.append("state float A = 0 [wrap];")
    L += ["", "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);", "rad = length(p) + 0.0008;", "ang = atan2(p.y, p.x);"]
    if fam == "rings":
        L += ["w = sin(rad * freq - t * speed * 6.283185 + sin(ang * 3.0) * warp);", "v = 0.5 + 0.5 * w;", "v = v + glow * exp(0.0 - rad * 3.0);"]
    elif fam == "spiral":
        L += ["w = sin(ang * arms + rad * freq - t * speed * 6.283185);", "v = smoothstep(0.0 - 0.1, 0.1, w);", "v = mix(v, 1.0 - rad, rate * 0.5);"]
    elif fam == "plasma":
        L += ["q = p * scale;", "w = sin(q.x + t * speed) + sin(q.y * 1.3 - t * speed * 0.7) + sin(length(q) * 2.0 - t * speed + warp * sin(q.x * q.y));",
              "v = 0.5 + 0.5 * sin(w * sharp);"]
    elif fam == "grid":
        L += ["q = p * scale + vec2(sin(t * speed), cos(t * speed)) * warp;", "g = abs(fract(q) - 0.5);", "v = 1.0 - smoothstep(0.0, 0.08, min(g.x, g.y)) ;",
              "v = v + glow * exp(0.0 - rad * 2.0) * 0.5;"]
    elif fam == "feedback":
        L += ["fb = A(uv + p * warp * 0.05);", "w = sin(rad * freq - t * speed * 6.283185 + fb * 6.283185);", "bw = step(0.0, sin(ang * 6.0 + w * 3.0));",
              "A = mix(A, bw, rate);", "v = A;"]
    elif fam == "tunnel":
        L += ["z = zoom / rad;", "a = ang * arms / 6.283185;", "w = sin(z * 2.0 - t * speed * 4.0 + a * 6.283185);", "v = smoothstep(0.0 - 0.2, 0.2, w) * clamp(rad * 3.0, 0.0, 1.0);",
              "v = v + glow * exp(0.0 - rad * 4.0);"]
    L += ["h = %s;" % ("hue" if "hue" in keys else "0.0"),
          "col = vec3(v) * (0.5 + 0.5 * cos(6.283185 * (vec3(0.0, 0.33, 0.67) + h + v * 0.3)));"]
    return "\n".join(L) + "\n"



# ------------------------------------------------------------------ Sketch (JS drawing) and Sketch 3D
SK_PARAMS = {
    "count": (3.0, 48.0, False), "spin": (0.0, 2.0, False), "size": (4.0, 90.0, False), "hue": (0.0, 1.0, False),
    "reach": (0.1, 0.9, False), "wob": (0.0, 3.0, False), "arms": (2.0, 12.0, False), "depth": (2.0, 7.0, False),
    "angle": (0.2, 1.2, False), "twist": (0.0, 2.0, False), "radius": (0.5, 3.0, False), "bulge": (0.0, 1.5, False),
}
SK_FAMS = {
    "orbit": ["count", "spin", "size", "hue", "reach"],
    "lissa": ["count", "spin", "size", "hue", "wob"],
    "rose": ["arms", "spin", "size", "hue", "reach"],
    "tree": ["depth", "angle", "spin", "hue", "wob"],
    "grid": ["count", "spin", "size", "hue", "wob"],
}
SK3_FAMS = {
    "helix": ["count", "twist", "size", "radius", "spin"],
    "ring": ["count", "radius", "size", "bulge", "spin"],
    "stack": ["count", "twist", "size", "bulge", "spin"],
}


def _sk_params(r, keys, tight=()):
    L = []
    for k in keys:
        lo, hi, _ = SK_PARAMS[k]
        d = lo + (hi - lo) * r.uniform(0.2, 0.8)
        if k in ("count", "arms", "depth"):
            d = float(int(d))
        L.append('param("%s", %.3f, %.3f, %.3f);' % (k, d, lo, hi))
    return L


def sketch_source(fam, seed):
    r = _rng("Sketch:" + fam, seed)
    keys = SK_FAMS[fam]
    L = _sk_params(r, keys) + ["", "function draw(t) {", "  background(%.2f);" % r.uniform(0.02, 0.1)]
    if fam == "orbit":
        L += ["  translate(width / 2, height / 2);", "  noStroke();", "  const R = min(width, height) * reach;",
              "  for (let i = 0; i < count; i++) {", "    rotate(TAU / count + spin * t * 0.1);", "    fill(hsl((hue + i / count) %% 1, 0.7, 0.6));",
              "    circle(R + R * 0.3 * sin(t + i), 0, size);", "  }"]
    elif fam == "lissa":
        L += ["  translate(width / 2, height / 2);", "  noStroke();", "  const R = min(width, height) * 0.4;",
              "  for (let i = 0; i < count * 4; i++) {", "    const a = i / (count * 4) * TAU;",
              "    fill(hsl((hue + i / (count * 4)) %% 1, 0.8, 0.6));",
              "    circle(R * sin(%d * a + t * spin), R * sin(%d * a + wob), size * 0.4);" % (r.choice([2, 3, 4]), r.choice([3, 4, 5])), "  }"]
    elif fam == "rose":
        L += ["  translate(width / 2, height / 2);", "  noStroke();", "  const R = min(width, height) * reach;",
              "  for (let i = 0; i < 160; i++) {", "    const a = i / 160 * TAU;", "    const k = R * cos(arms * 0.5 * a + t * spin * 0.3);",
              "    fill(hsl((hue + i / 160) %% 1, 0.75, 0.6));", "    circle(k * cos(a), k * sin(a), size * 0.3);", "  }"]
    elif fam == "tree":
        L += ["  translate(width / 2, height * 0.95);", "  stroke(1);", "  strokeWeight(2);", "  branch(height * 0.22, depth, t);", "}", "",
              "function branch(len, d, t) {", "  if (d < 1) return;", "  stroke(hsl((hue + d * 0.08) %% 1, 0.7, 0.6));", "  line(0, 0, 0, -len);", "  translate(0, -len);",
              "  push();", "  rotate(angle + wob * 0.05 * sin(t * spin + d));", "  branch(len * 0.72, d - 1, t);", "  pop();",
              "  push();", "  rotate(0 - angle - wob * 0.05 * sin(t * spin + d));", "  branch(len * 0.72, d - 1, t);", "  pop();"]
    elif fam == "grid":
        L += ["  noStroke();", "  const n = floor(count);", "  for (let i = 0; i < n; i++) {", "    for (let j = 0; j < n; j++) {",
              "      const x = (i + 0.5) / n * width;", "      const y = (j + 0.5) / n * height;",
              "      fill(hsl((hue + (i + j) / (2 * n) + 0.1 * sin(t * spin)) %% 1, 0.7, 0.55));",
              "      circle(x + wob * 8 * sin(t * spin + j), y + wob * 8 * cos(t * spin + i), size * 0.5);", "    }", "  }"]
    L += ["}"]
    return "\n".join(L) + "\n"


def sketch3d_source(fam, seed):
    r = _rng("Sketch 3D:" + fam, seed)
    keys = SK3_FAMS[fam]
    L = _sk_params(r, keys) + ["", "function draw(t) {"]
    if fam == "helix":
        L += ["  for (let i = 0; i < count; i++) {", "    push();", "    rotateY(i * twist * 0.3 + t * spin);", "    translate(radius, i * 0.25 - count * 0.125, 0);",
              "    fill(hsl((i / count) %% 1, 0.7, 0.6));", "    sphere(size * 0.01);", "    pop();", "  }"]
    elif fam == "ring":
        L += ["  for (let i = 0; i < count; i++) {", "    push();", "    rotateY(i / count * TAU + t * spin);", "    translate(radius, 0, 0);",
              "    fill(hsl((i / count) %% 1, 0.7, 0.6));", "    box(size * 0.01 * (1 + bulge * sin(i + t)));", "    pop();", "  }"]
    else:
        L += ["  for (let i = 0; i < count; i++) {", "    push();", "    translate(0, i * 0.2 - count * 0.1, 0);", "    rotateY(i * twist * 0.4 + t * spin);",
              "    fill(hsl((i / count) %% 1, 0.7, 0.6));", "    box(size * 0.02 * (1 + bulge * 0.3 * sin(i)), 0.08, size * 0.02);", "    pop();", "  }"]
    L += ["}"]
    return "\n".join(L) + "\n"


# ------------------------------------------------------------------ registry
BASES = {
    "Field Synth": (SYNTH_FAMS, SYNTH_PARAMS, synth_source),
    "Field Effect": (FX_FAMS, FX_PARAMS, fx_source),
    "Field Notes": (NOTE_FAMS, NOTE_PARAMS, notes_source),
    "FieldPixel": (PIX_FAMS, PIX_PARAMS, pixel_source),
    "Sketch": (SK_FAMS, SK_PARAMS, sketch_source),
    "Sketch 3D": (SK3_FAMS, SK_PARAMS, sketch3d_source),
}


PRESETS = {}  # base type -> [{"name", "code", "params"}]; the index is the preset's pseudo-type family "pNN"
PARAM_RE = re.compile(r"param\s+float\s+(\w+)\s*=\s*(-?[\d.]+)\s*\[\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\]")


def load_presets():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "presets.json")
    if PRESETS or not os.path.exists(path):
        return PRESETS
    for base, items in json.load(open(path)).items():
        if base == "Field Graph":
            continue  # multi-node pipelines, not a standalone source
        keep = []
        for it in items:
            if re.search(r"^\s*input\s", it["code"], re.M):
                continue  # filters that read an image/audio input pin do not stand alone as sources
            it["params"] = [(m.group(1), float(m.group(2)), float(m.group(3)), float(m.group(4))) for m in PARAM_RE.finditer(it["code"])]
            keep.append(it)
        PRESETS[base] = keep
    return PRESETS


def preset_source(base, idx, seed):
    """The built-in preset's code with every float param's default re-drawn inside its own range (seed 0 keeps the shipped values)."""
    it = PRESETS[base][idx]
    if not seed:
        return it["code"]
    r = _rng("%s#%d" % (base, idx), seed)
    code = it["code"]
    for name, dv, lo, hi in it["params"]:
        v = lo + (hi - lo) * r.random()
        code = re.sub(r"(param\s+float\s+%s\s*=\s*)-?[\d.]+" % name, lambda m, v=v: "%s%.4f" % (m.group(1), v), code, count=1)
    return code


def register_presets(facts):
    """One pseudo type per built-in preset ("FieldPixel:p07"); params come from the code's own `param float` declarations."""
    for base, items in load_presets().items():
        if base not in facts:
            continue
        for i, it in enumerate(items):
            t = copy.deepcopy(facts[base]); t["type"] = "%s:p%02d" % (base, i); t["family"] = (base, "p%02d" % i); t["preset"] = it["name"]
            keep = [m for m in t["modulatable"] if not m.get("key", "").startswith("p.")]
            for j, (name, dv, lo, hi) in enumerate(it["params"]):
                keep.append({"index": 51 + j, "label": name, "key": "p." + name, "min": lo, "max": hi, "step": 0})
            t["modulatable"] = keep
            facts[t["type"]] = t
    return facts


def register(facts):
    register_presets(facts)
    """Add one pseudo type per family ("Field Synth:fm") and drop the default-code base types from the pools."""
    for base, (fams, prm, _) in BASES.items():
        if base not in facts:
            continue
        for fam, keys in fams.items():
            t = copy.deepcopy(facts[base])
            t["type"] = "%s:%s" % (base, fam)
            t["family"] = (base, fam)
            keep = [m for m in t["modulatable"] if not m.get("key", "").startswith("p.")]
            for i, k in enumerate(keys):
                lo, hi, _ = prm[k]
                keep.append({"index": 51 + i, "label": k, "key": "p." + k, "min": lo, "max": hi, "step": 0})
            t["modulatable"] = keep
            facts[t["type"]] = t
        facts[base]["hardware_driven"] = True  # keep the default-code node out of every pool
    for base in load_presets():
        if base in facts:
            facts[base]["hardware_driven"] = True
    return facts


def source(typ, seed):
    base, fam = typ.split(":")
    if re.fullmatch(r"p\d\d", fam):
        return preset_source(base, int(fam[1:]), int(seed or 0)).replace("%%", "%").replace("\n", "\\n")
    return BASES[base][2](fam, int(seed or 0)).replace("%%", "%").replace("\n", "\\n")


def base_name(typ):
    return typ.split(":")[0]
