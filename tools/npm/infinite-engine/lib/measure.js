// Picture facts, measured the way tools/infinite-model/effect_table.py measured the effect table. Change both together.
// A picture is read as seen: RGBA composited over black. No dependencies: PNG decoding uses node's zlib.
const fs = require("fs");
const path = require("path");
const os = require("os");
const zlib = require("zlib");
const { runCommand } = require("./engine");

const SIZE = "128x128"; // the table was measured at this size; every picture is rendered to it before measuring
const FACTS = {
  luma: [1.0, [0.01, 0.04, 0.12]],
  contrast: [1.0, [0.02, 0.08, 0.2]],
  spread: [1.0, [0.01, 0.03, 0.08]],
  red: [1.0, [0.01, 0.04, 0.12]],
  green: [1.0, [0.01, 0.04, 0.12]],
  blue: [1.0, [0.01, 0.04, 0.12]],
  warmth: [1.0, [0.01, 0.04, 0.12]],
  black: [100.0, [0.01, 0.05, 0.15]],
  white: [100.0, [0.01, 0.05, 0.15]],
  alpha: [100.0, [0.01, 0.05, 0.15]],
  detail: [1.0, [0.1, 0.3, 0.8]],
  tones: [1.0, [0.03, 0.1, 0.3]]
};
const LEVELS = ["none", "small", "medium", "large"];
const WORDS = {
  luma: "brightness", contrast: "contrast (2-98 % luma range)", spread: "tonal spread", red: "red", green: "green",
  blue: "blue", warmth: "warmth (red minus blue)", black: "share of black pixels", white: "share of white pixels",
  alpha: "opacity", detail: "fine detail / sharpness (RMS edge strength)", tones: "tone distribution (histogram shape)"
};

function decodePng(file) {
  const buf = fs.readFileSync(file);
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error(`not a PNG: ${file}`);
  let off = 8, w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off), type = buf.toString("ascii", off + 4, off + 8);
    const data = buf.subarray(off + 8, off + 8 + len);
    if (type === "IHDR") {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4); depth = data[8]; ctype = data[9]; interlace = data[12];
    } else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    off += 12 + len;
  }
  const ch = { 0: 1, 2: 3, 4: 2, 6: 4 }[ctype];
  if (depth !== 8 || !ch || interlace) throw new Error(`unsupported PNG (depth ${depth}, colour type ${ctype}, interlace ${interlace}): render it through a patch instead`);
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = w * ch, out = Buffer.alloc(h * stride);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], src = y * (stride + 1) + 1, dst = y * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= ch ? out[dst + x - ch] : 0, b = y ? out[dst - stride + x] : 0;
      const c = x >= ch && y ? out[dst - stride + x - ch] : 0;
      let v = raw[src + x];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      out[dst + x] = v & 255;
    }
  }
  const rgba = new Float64Array(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const s = i * ch;
    const [r, g, b, al] = ch === 4 ? [out[s], out[s + 1], out[s + 2], out[s + 3]] : ch === 3 ? [out[s], out[s + 1], out[s + 2], 255]
      : ch === 2 ? [out[s], out[s], out[s], out[s + 1]] : [out[s], out[s], out[s], 255];
    rgba.set([r / 255, g / 255, b / 255, al / 255], i * 4);
  }
  return { w, h, rgba };
}

function factsOfPng(file) {
  const { w, h, rgba } = decodePng(file);
  const n = w * h, y = new Float64Array(n), hist = new Array(16).fill(0);
  let R = 0, G = 0, B = 0, A = 0, black = 0, white = 0;
  for (let i = 0; i < n; i++) {
    const al = rgba[i * 4 + 3], r = rgba[i * 4] * al, g = rgba[i * 4 + 1] * al, b = rgba[i * 4 + 2] * al;
    const l = r * 0.2126 + g * 0.7152 + b * 0.0722;
    y[i] = l; R += r; G += g; B += b; A += al;
    hist[Math.min(Math.floor(l * 16), 15)] += 1;
    if (l < 0.02) black++;
    if (l > 0.98) white++;
  }
  for (let k = 0; k < 16; k++) hist[k] /= n;
  let mu = 0, v = 0;
  for (let k = 0; k < 16; k++) mu += hist[k] * (k + 0.5) / 16;
  for (let k = 0; k < 16; k++) v += hist[k] * ((k + 0.5) / 16 - mu) ** 2;
  const q = Float64Array.from(y).sort();
  let g2 = 0;
  for (let r = 0; r < h - 1; r++) for (let c = 0; c < w - 1; c++) {
    const i = r * w + c, gx = y[i + 1] - y[i], gy = y[i + w] - y[i];
    g2 += gx * gx + gy * gy;
  }
  // 4x4 grid of mean brightness: global facts cannot see where things are
  const grid = new Array(16).fill(0);
  for (let r = 0; r < h; r++) for (let c = 0; c < w; c++) grid[Math.floor(r * 4 / h) * 4 + Math.floor(c * 4 / w)] += y[r * w + c];
  for (let k = 0; k < 16; k++) grid[k] /= n / 16;
  let lum = 0;
  for (let i = 0; i < n; i++) lum += y[i];
  return {
    luma: lum / n, contrast: q[Math.floor(0.98 * (n - 1))] - q[Math.floor(0.02 * (n - 1))], spread: Math.sqrt(v),
    red: R / n, green: G / n, blue: B / n, warmth: (R - B) / n, black: black / n * 100, white: white / n * 100,
    alpha: A / n * 100, detail: Math.sqrt(g2 / ((w - 1) * (h - 1))), hist, grid
  };
}

function bucket(before, after) {
  const out = {};
  for (const [k, [scale, edges]] of Object.entries(FACTS)) {
    let dv;
    if (k === "tones") dv = before.hist.reduce((s, x, i) => s + Math.abs(after.hist[i] - x), 0) / 2;
    else if (k === "detail") dv = Math.log((after.detail + 1e-3) / (before.detail + 1e-3));
    else dv = (after[k] - before[k]) / scale;
    const lvl = edges.filter(e => Math.abs(dv) >= e).length;
    if (lvl) out[k] = k === "tones" ? LEVELS[lvl] : (dv > 0 ? "up " : "down ") + LEVELS[lvl];
  }
  return out;
}

// A picture to measure: a .png/.jpg (or any image the app reads), a .inf patch path, or patch text.
function patchFor(input) {
  if (typeof input !== "string" || !input) throw new Error("picture input must be a file path or patch text");
  if (input.startsWith("infinite-patch")) return input;
  if (!fs.existsSync(input)) throw new Error(`no such file: ${input}`);
  if (input.toLowerCase().endsWith(".inf")) return fs.readFileSync(input, "utf-8");
  return `infinite-patch 1\nnode 1 Source Image Source\n  s path ${path.resolve(input)}\nend\nnode 2 Utility Output\nend\ncable 2 0 1\n`;
}

function measure(input, time = 0.5) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "infinite-measure-"));
  const pf = path.join(dir, "m.inf"), png = path.join(dir, "m.png");
  fs.writeFileSync(pf, patchFor(input), "utf-8");
  const res = runCommand(["--frame", pf, String(time), png, "--size", SIZE], 120000);
  const file = (res.frame_stats && res.frame_stats[0] && res.frame_stats[0].file) || (res.files && res.files[0]);
  if (!file || !fs.existsSync(file)) throw new Error(`render failed: ${JSON.stringify(res.errors || res.error || res.raw_output || res).slice(0, 400)}`);
  return { facts: factsOfPng(file), frame: file, warnings: res.warnings || [] };
}

function describe(change) {
  const parts = Object.entries(change).map(([k, v]) => k === "tones" ? `${WORDS[k]} changed (${v})` : `${WORDS[k]} ${v.replace(" ", ", ")}`);
  return parts.length ? parts.join("; ") : "no measurable change";
}

function tokensOf(change) {
  const out = {};
  for (const [k, v] of Object.entries(change)) {
    const w = v.split(" ");
    out[k === "tones" ? "tones" : `${k} ${w[0]}`] = w[w.length - 1];
  }
  return out;
}

module.exports = { FACTS, LEVELS, WORDS, SIZE, decodePng, factsOfPng, bucket, measure, describe, tokensOf, patchFor };
