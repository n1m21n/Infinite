// Measured effect table (lib/data/effects-table.json, written by tools/infinite-model/effect_table.py):
// what adding each node does and what moving each param does, measured on 10 source pictures.
// The knob finder ranks moves by how well their measured effect matches a wanted change (the E1b "b0p" lookup,
// which beat every trained model on the same question).
const fs = require("fs");
const path = require("path");
const { bucket, tokensOf, describe, measure } = require("./measure");

const LEVEL = { small: 1, medium: 2, large: 3 };
let TABLE = null;
function table() {
  if (!TABLE) TABLE = JSON.parse(fs.readFileSync(path.join(__dirname, "data", "effects-table.json"), "utf-8"));
  return TABLE;
}

function findType(name) {
  const t = table().types, n = String(name).toLowerCase().replace(/^(effects|compositing)\s+/, "").trim();
  if (t[n]) return n;
  const squash = s => s.toLowerCase().replace(/[\s_-]/g, "");
  return Object.keys(t).find(k => squash(k) === squash(n)) || null;
}

// "detail down large", "detail down", "tones medium", "brightness up" -> {token: level|null}
const ALIAS = { brightness: "luma", sharpness: "detail", opacity: "alpha", colour: "saturation", color: "saturation" };
function parseWant(want) {
  const out = {}, bad = [];
  for (const raw of [].concat(want || [])) {
    const w = String(raw).toLowerCase().trim().split(/\s+/);
    let fact = w[0] in ALIAS ? ALIAS[w[0]] : w[0];
    const lvl = w.find(x => x in LEVEL) || null;
    if (fact === "tones") { out.tones = lvl; continue; }
    const dir = w.find(x => x === "up" || x === "down");
    if (!fact || !dir || !(fact in table().levels)) { bad.push(raw); continue; }
    out[`${fact} ${dir}`] = lvl;
  }
  return { want: out, bad };
}

// full = the wanted change is a whole measured change (before/after): anything else the move does is a mismatch.
// Otherwise only the named changes matter: a move that goes the opposite way is penalised, other side effects barely.
const flip = t => t.endsWith(" up") ? t.slice(0, -3) + " down" : t.endsWith(" down") ? t.slice(0, -5) + " up" : null;
function score(effect, want, full) {
  let s = 0, extra = 0;
  for (const [tok, [share, lvl]] of Object.entries(effect)) {
    if (tok in want) s += share * (want[tok] === null || LEVEL[lvl] === LEVEL[want[tok]] ? 1.0 : 0.6);
    else extra += full ? 0.3 * share : (flip(tok) in want ? share : 0.05 * share);
  }
  return s - extra;
}

// Best score over picture kinds (colour / grey), or only the kind asked for. -> [score, kind, effect]
function scoreKinds(byKind, all, want, full, kind) {
  const opts = kind ? Object.entries(byKind || {}).filter(([k]) => k === kind) : [];
  if (!opts.length) return [score(all, want, full), "all", all];
  return opts.map(([k, [eff]]) => [score(eff, want, full), k, eff]).sort((a, b) => b[0] - a[0])[0];
}

function fmt(effect) {
  const e = Object.entries(effect).filter(([, [share]]) => share >= 0.25);
  return e.length ? e.map(([t, [share, l]]) => `${t} ${l} (${Math.round(share * 100)} % of pictures)`).join(", ") : "no consistent change";
}

// Ranked candidates: param moves on each type, plus adding the node itself. One best value per (type, param, direction).
function findKnob(want, { nodeTypes = null, k = 5, full = false, kind = null } = {}) {
  const types = nodeTypes ? nodeTypes.map(findType).filter(Boolean) : Object.keys(table().types);
  const best = new Map();
  for (const ty of types) {
    const T = table().types[ty];
    const [add, ak, aeff] = scoreKinds(T.add_by_kind, T.add, want, full, kind);
    best.set(`${ty}|add`, { node_type: ty, node_line: T.node_line, move: "add the node at defaults", score: add, effect: aeff, kind: ak });
    for (const [key, p] of Object.entries(T.params)) for (const m of p.moves) {
      const [s, mk, meff] = scoreKinds(m.by_kind, m.effect, want, full, kind), id = `${ty}|${key}|${m.direction}`;
      if (!best.has(id) || s > best.get(id).score)
        best.set(id, { node_type: ty, node_line: T.node_line, param: key, label: p.label, direction: m.direction,
          value: m.option || m.value, default: p.default, range: p.options ? p.options : [p.min, p.max],
          patch_line: m.patch_line, score: s, effect: meff, kind: mk });
    }
  }
  return [...best.values()].filter(c => c.score > 0).sort((a, b) => b.score - a.score).slice(0, k)
    .map(c => ({ ...c, score: Math.round(c.score * 100) / 100, measured_on: `${c.kind} pictures`,
      measured_effect: fmt(c.effect), effect: undefined, kind: undefined }));
}

// before/after with a patch: apply each candidate to the real before patch, render, keep the ones that land closest.
function setParam(patchText, nodeType, line) {
  const lines = patchText.split("\n"), key = line.split(" ")[1];
  const i = lines.findIndex(l => /^node \S+ /.test(l) && findType(l.split(" ").slice(2).join(" ")) === nodeType);
  if (i < 0) return null;
  let j = i + 1;
  while (j < lines.length && lines[j].trim() !== "end") {
    if (lines[j].trim().split(/\s+/)[1] === key) { lines[j] = "  " + line; return lines.join("\n"); }
    j++;
  }
  lines.splice(i + 1, 0, "  " + line);
  return lines.join("\n");
}

const DIST = { small: 1, medium: 2, large: 3 };
function distance(change) {
  return Object.values(change).reduce((s, v) => s + DIST[v.split(" ").pop()], 0);
}

function verify(beforeText, afterFacts, cands) {
  const out = [];
  for (const c of cands) {
    if (!c.patch_line) continue;
    const pt = setParam(beforeText, c.node_type, c.patch_line);
    if (!pt) continue;
    try {
      const r = measure(pt);
      const left = bucket(r.facts, afterFacts);
      out.push({ ...c, rendered_distance: distance(left), still_differs: describe(left) });
    } catch (e) { /* a candidate that fails to render is dropped */ }
  }
  return out.sort((a, b) => a.rendered_distance - b.rendered_distance || b.score - a.score);
}

function nodeEffects(name) {
  const tb = table();
  if (!name) return {
    families: tb.families, measured_at: `${tb.size}, t=${tb.time}s, on ${tb.sources.length} source pictures`,
    types: Object.fromEntries(Object.entries(tb.types).map(([k, T]) => [k, `${T.node_line}: ${fmt(T.add)}`]))
  };
  const ty = findType(name);
  if (!ty) throw new Error(`no measured effects for "${name}". Measured: ${Object.keys(tb.types).join(", ")}`);
  const T = tb.types[ty];
  const params = {};
  for (const [key, p] of Object.entries(T.params)) {
    const dirs = {};
    for (const m of p.moves) (dirs[m.direction] = dirs[m.direction] || []).push(`${m.option || m.value}: ${fmt(m.effect)}`);
    params[key] = { label: p.label, default: p.default, range: p.options || [p.min, p.max], up: dirs.up || [], down: dirs.down || [] };
  }
  return { node_type: ty, node_line: T.node_line, add_at_defaults: fmt(T.add), add_no_change_share: T.add_no_change, params };
}

function compare(a, b) {
  const A = measure(a), B = measure(b);
  const change = bucket(A.facts, B.facts);
  let layout = 0;
  for (let i = 0; i < 16; i++) layout = Math.max(layout, Math.abs(A.facts.grid[i] - B.facts.grid[i]));
  const r3 = x => Math.round(x * 1000) / 1000;
  const facts = {};
  for (const k of ["luma", "contrast", "spread", "red", "green", "blue", "saturation", "black", "white", "alpha", "detail"])
    facts[k] = { a: r3(A.facts[k]), b: r3(B.facts[k]) };
  const differences = describe(change);
  return {
    match: Object.keys(change).length === 0 && layout < 0.04,
    differences: layout >= 0.04 ? `${differences}; layout differs (a 4x4 region's brightness moved by ${r3(layout)})` : differences,
    change, change_tokens: tokensOf(change), facts, region_brightness_max_diff: r3(layout),
    facts_b: B.facts, frames: { a: A.frame, b: B.frame }, warnings: [...A.warnings, ...B.warnings]
  };
}

module.exports = { table, findType, parseWant, findKnob, nodeEffects, compare, verify, setParam, distance };
