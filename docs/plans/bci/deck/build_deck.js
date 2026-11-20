// BCI review deck — Infinite Brand Book v2.1
// Every number comes from scripts run on 2026-11-19:
//   tools/bci/batch_validate.py (M1, M2, site ratios)
//   tools/bci/calib_ab.py (per-person calibration, cross-day)
//   tools/bci/direct_pins.py (M3 proxy)
//   tools/bci/blink_m4.py (M4 blink toggle)
//   selftest [9] (M6 compute cost)
//
// Usage:
//   NODE_PATH=<dir with pptxgenjs> node build_deck.js BCI_results.pptx <path to apply_theme.js>

const path = require("path");
const pptxgen = require("pptxgenjs");
const { applyTheme } = require(process.argv[3]);

const OUT = process.argv[2];
const HERE = __dirname;
const PROOF = path.join(HERE, "..", "proof");
const LOGO_MARK = path.join(HERE, "..", "..", "..", "..", "art", "brand", "logo", "png", "mark-256.png");

// ---------------------------------------------------------------- Brand Tokens (docs/brand/brand.json v2.1)
const MODES = {
  midnight: {
    ground: "151930",
    deep: "0D1020",
    s1: "20263F",
    s2: "2A3052",
    s3: "343B5E",
    line: "343B5E",
    ink: "EEF1FA",
    ink2: "A3AACB",
    ink3: "8890B1",
    signal: "A7B1FF",
    accent: "FF6B35",
    accentInk: "FF6B35",
    onAccent: "151930",
    pass: "6ED78C",
    fail: "FF6B35",
  },
  mist: {
    ground: "E4E2EF",
    deep: "D8D5E5",
    s1: "F7F5FB",
    s2: "FFFFFF",
    s3: "EEEDF6",
    line: "C4C1D9",
    ink: "12152A",
    ink2: "404660",
    ink3: "5A6180",
    signal: "4A54D6",
    accent: "D9430E",
    accentInk: "B53700",
    onAccent: "FFFFFF",
    pass: "1E9646",
    fail: "B53700",
  },
  paper: {
    ground: "FBFAF6",
    deep: "EFF0F4",
    s1: "FFFFFF",
    s2: "FFFFFF",
    s3: "F5F5F8",
    line: "DAD8E6",
    ink: "12152A",
    ink2: "404660",
    ink3: "5A6180",
    signal: "4A54D6",
    accent: "D9430E",
    accentInk: "B53700",
    onAccent: "FFFFFF",
    pass: "1E9646",
    fail: "B53700",
  },
};

const THEME = {
  name: "Infinite Brand v2.1",
  headFontFace: "Geist",
  bodyFontFace: "Geist",
  colors: {
    dk1: "12152A",
    lt1: "FBFAF6",
    dk2: "404660",
    lt2: "E4E2EF",
    accent1: "4A54D6",
    accent2: "B53700",
    accent3: "A7B1FF",
    accent4: "5A6180",
    accent5: "1E9646",
    accent6: "D9430E",
    hlink: "4A54D6",
    folHlink: "4A54D6",
  },
};

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE"; // 13.3333 x 7.5 in
pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
pres.title = "Playing sound and picture with the brain";
pres.author = "Naman Soni";

// ---------------------------------------------------------------- Symmetrical 12-col Grid Geometry
const W = 13.3333;
const H = 7.5;
const M = 0.6; // symmetrical left & right margins
const CW = W - 2 * M; // 12.1333 in
const KICKER_Y = 0.40;
const TITLE_Y = 0.68;
const BODY_Y = 1.54;
const BODY_BOTTOM = 6.72;
const BODY_H = BODY_BOTTOM - BODY_Y; // 5.18 in
const FOOT_Y = 7.02;

function cols(n, gap = 0.24) {
  const w = (CW - (n - 1) * gap) / n;
  return { w, x: (i) => +(M + i * (w + gap)).toFixed(4) };
}

function rows(n, y0 = BODY_Y, hTot = BODY_H, gap = 0.22) {
  const h = (hTot - (n - 1) * gap) / n;
  return { h, y: (i) => +(y0 + i * (h + gap)).toFixed(4) };
}

// ---------------------------------------------------------------- Masters (background only; slides carry their own mode)
pres.defineSlideMaster({
  title: "BASE",
  background: { color: MODES.paper.ground },
});

// ---------------------------------------------------------------- Primitives
let pageCounter = 0;

function makeSlide(sectionTitle, modeName, kicker, titleText) {
  pageCounter += 1;
  const m = MODES[modeName];
  const s = pres.addSlide({ masterName: "BASE", sectionTitle });
  s.background = { color: m.ground };

  if (kicker) {
    s.addText(kicker.toUpperCase(), {
      isTextBox: true,
      margin: 0,
      x: M,
      y: KICKER_Y,
      w: CW,
      h: 0.24,
      fontFace: "Geist Mono",
      fontSize: 10,
      bold: true,
      charSpacing: 1.5,
      color: m.signal,
      valign: "middle",
    });
  }
  if (titleText) {
    s.addText(titleText, {
      isTextBox: true,
      margin: 0,
      x: M,
      y: TITLE_Y,
      w: CW,
      h: 0.68,
      fontFace: "Geist",
      fontSize: 27,
      bold: true,
      color: m.ink,
      valign: "middle",
    });
  }
  // Consistent mono folio footer (symmetrical M left & right)
  const folio = String(pageCounter).padStart(2, "0");
  s.addText("INFINITE × BCI  ·  NAMAN SONI", {
    isTextBox: true,
    margin: 0,
    x: M,
    y: FOOT_Y,
    w: 6.0,
    h: 0.24,
    fontFace: "Geist Mono",
    fontSize: 9.5,
    charSpacing: 1.0,
    color: m.ink3,
    valign: "middle",
  });
  s.addText(folio, {
    isTextBox: true,
    margin: 0,
    x: W - M - 1.2,
    y: FOOT_Y,
    w: 1.2,
    h: 0.24,
    fontFace: "Geist Mono",
    fontSize: 9.5,
    charSpacing: 1.0,
    color: m.ink3,
    align: "right",
    valign: "middle",
  });

  return { s, m };
}

function T(s, text, opts) {
  s.addText(text, {
    isTextBox: true,
    margin: 0,
    fontFace: "Geist",
    fontSize: 14,
    valign: "top",
    ...opts,
  });
}

function caption(s, m, text, opts) {
  T(s, text, {
    fontFace: "Geist",
    fontSize: 11,
    color: m.ink3,
    ...opts,
  });
}

// Brand Card: 24px-proportioned rounded rect, zero outlines
function card(s, x, y, w, h, fillHex, name = "card") {
  s.addShape(pres.ShapeType.roundRect, {
    x,
    y,
    w,
    h,
    rectRadius: 0.08,
    fill: { color: fillHex },
    line: { type: "none" },
    objectName: name,
  });
}

// Centered aspect-preserving image inside a target box (bx, by, bw, bh)
function fitImage(s, imgPath, imgW, imgH, bx, by, bw, bh, altText, wellFill = null) {
  if (wellFill) {
    card(s, bx, by, bw, bh, wellFill, "media well");
  }
  const pad = wellFill ? 0.08 : 0;
  const aw = bw - 2 * pad;
  const ah = bh - 2 * pad;
  const rImg = imgW / imgH;
  const rBox = aw / ah;
  let w, h;
  if (rImg > rBox) {
    w = aw;
    h = aw / rImg;
  } else {
    h = ah;
    w = ah * rImg;
  }
  const x = bx + (bw - w) / 2;
  const y = by + (bh - h) / 2;
  s.addImage({ path: imgPath, x, y, w, h, altText });
}

// Clean chart styling bound to mode
function chartOpts(m, title) {
  return {
    showTitle: true,
    title,
    titleFontSize: 13,
    titleColor: m.ink,
    titleFontFace: "+mn-lt",
    catAxisLabelColor: m.ink2,
    valAxisLabelColor: m.ink3,
    catAxisLabelFontSize: 11,
    valAxisLabelFontSize: 10.5,
    catAxisLabelFontFace: "+mn-lt",
    valAxisLabelFontFace: "+mn-lt",
    valGridLine: { color: m.line, size: 0.75 },
    catGridLine: { style: "none" },
    showValue: true,
    dataLabelFontSize: 10.5,
    dataLabelColor: m.ink,
    dataLabelFontFace: "+mn-lt",
    legendFontSize: 11,
    legendFontFace: "+mn-lt",
    legendColor: m.ink2,
    plotArea: { fill: { color: m.s1 } },
    chartArea: { fill: { color: m.s1 } },
  };
}

// Paper table helpers (PDF_GUIDELINES.md: hairline horizontal rows, no vertical borders)
function tblHeader(m, text) {
  return {
    text: text.toUpperCase(),
    options: {
      fontFace: "Geist Mono",
      fontSize: 9.5,
      bold: true,
      color: m.ink3,
      fill: { color: m.deep },
      border: [
        { type: "none" },
        { type: "none" },
        { type: "solid", pt: 1, color: m.line },
        { type: "none" },
      ],
    },
  };
}

function tblCell(m, text, opts = {}) {
  return {
    text,
    options: {
      fontFace: opts.mono ? "Geist Mono" : "Geist",
      fontSize: opts.fontSize || 12,
      bold: !!opts.bold,
      color: opts.color || m.ink,
      fill: { color: opts.fill || m.s1 },
      align: opts.align || "left",
      border: [
        { type: "none" },
        { type: "none" },
        { type: "solid", pt: 0.6, color: m.line },
        { type: "none" },
      ],
    },
  };
}

// ================================================================ 01. TITLE (Midnight)
pres.addSection({ title: "Infinite" });
{
  const { s, m } = makeSlide("Infinite", "midnight", null, null);
  // Lemniscate mark top-left (256x110 -> ratio 2.327)
  s.addImage({ path: LOGO_MARK, x: M, y: 0.56, w: 1.05, h: 1.05 / 2.3273, altText: "Infinite mark" });

  T(s, "INFINITE × BCI  ·  RESEARCH REVIEW", {
    x: M,
    y: 2.35,
    w: CW,
    h: 0.3,
    fontFace: "Geist Mono",
    fontSize: 11,
    bold: true,
    charSpacing: 1.8,
    color: m.ink2,
  });

  s.addText(
    [
      { text: "Playing sound & picture\nwith the ", options: { fontFace: "Geist", fontSize: 40, bold: true, color: m.ink } },
      { text: "brain", options: { fontFace: "Caveat", fontSize: 54, bold: true, color: m.accent } },
    ],
    {
      isTextBox: true,
      margin: 0,
      x: M,
      y: 2.65,
      w: CW,
      h: 2.25,
      valign: "top",
    }
  );

  // One subtle Ember rule (per brand template slides.html)
  s.addShape(pres.ShapeType.roundRect, {
    x: M,
    y: 5.05,
    w: 1.0,
    h: 0.08,
    rectRadius: 0.04,
    fill: { color: m.accent },
    line: { type: "none" },
    objectName: "title rule",
  });

  T(s, "Connecting a consumer EEG headband to an open-source audiovisual instrument.\nWhat already works, what failed honestly, and where I need neuroscience advice.", {
    x: M,
    y: 5.35,
    w: 9.8,
    h: 0.85,
    fontSize: 17,
    color: m.ink2,
  });

  s.addNotes(
    "30-second pitch: Infinite is an open-source audiovisual workstation I built — like a modular DAW for sound, shaders, and 3D. " +
      "A brain headset is just another signal source. This review covers how we connect the two directly, what we tested on public EEG datasets, " +
      "and the neuroscience questions I'd love your guidance on."
  );
}

// ================================================================ 02. WHAT INFINITE IS (Midnight)
{
  const { s, m } = makeSlide("Infinite", "midnight", "01 / INFINITE", "One patch plays music, visuals and 3D live");
  const leftW = 7.65;
  const rightX = M + leftW + 0.25;
  const rightW = CW - leftW - 0.25; // 4.2333
  const topH = 4.78; // 7.65 / 1.600 = 4.781
  const gapY = 0.20;
  // Right column images stacked so total height matches leftW/1.60 = 4.781!
  // hero_visual ratio 2.0645 -> h1 = 4.2333 / 2.0645 = 2.05
  // hero_3d ratio 1.6092 -> h2 = 4.2333 / 1.6092 = 2.63 -> 2.05 + 0.10 + 2.63 = 4.78!
  const h1 = +(rightW / 2.0645).toFixed(4);
  const h2 = +(topH - h1 - 0.10).toFixed(4);

  fitImage(s, path.join(HERE, "hero_patch.jpg"), 1600, 1000, M, BODY_Y, leftW, topH, "Infinite node graph", m.deep);
  fitImage(s, path.join(HERE, "hero_visual.jpg"), 1280, 620, rightX, BODY_Y, rightW, h1, "Generated shader output", m.deep);
  fitImage(s, path.join(HERE, "hero_3d.jpg"), 1400, 870, rightX, BODY_Y + h1 + 0.10, rightW, h2, "3D view driven by the same patch", m.deep);

  T(s, "353 node types  ·  Free & open source (macOS, Windows, Linux)  ·  Cables carry audio, video and 0–1 control signals", {
    x: M,
    y: BODY_Y + topH + 0.18,
    w: CW,
    h: 0.34,
    fontFace: "Geist Mono",
    fontSize: 11.5,
    color: m.ink2,
  });

  s.addNotes(
    "This is my home turf. Infinite is a free, open-source audiovisual workstation I built. " +
      "You wire boxes called nodes — oscillators, filters, shaders, 3D lights — and one live patch produces music and visuals together."
  );
}

// ================================================================ 03. INSPIRATION (Paper)
{
  const { s, m } = makeSlide("Infinite", "paper", "01 / INSPIRATION", "Brain signals have already played music");
  const g = cols(3, 0.28);
  const cards = [
    {
      year: "1965  ·  HISTORICAL ROOT",
      title: "Alvin Lucier",
      sub: "Music for Solo Performer",
      img: "ref_lucier.jpg",
      iw: 1280,
      ih: 960,
      body: "Amplified alpha brainwaves vibrated loudspeakers placed against gongs and snare drums.",
      em: false,
    },
    {
      year: "2023  ·  DIRECT INSPIRATION",
      title: "Benn Jordan",
      sub: "EEG headband into synths",
      img: "ref_bj_patch.png",
      iw: 800,
      ih: 450,
      body: "Patched a consumer EEG headset into a modular synth environment, very close to an Infinite patch.",
      em: false,
    },
    {
      year: "2026  ·  THIS PROJECT",
      title: "Infinite + BCI",
      sub: "A learnable instrument",
      img: "ref_bj_drums.png",
      iw: 800,
      ih: 450,
      body: "Make the brain a first-class node with per-person calibration, visuals, and zero mood guessing.",
      em: true,
    },
  ];

  cards.forEach((c, i) => {
    const x = g.x(i);
    card(s, x, BODY_Y, g.w, 4.72, m.s1, `insp ${i + 1}`);
    T(s, c.year, {
      x: x + 0.22,
      y: BODY_Y + 0.20,
      w: g.w - 0.44,
      h: 0.24,
      fontFace: "Geist Mono",
      fontSize: 9.5,
      bold: true,
      charSpacing: 1.0,
      color: c.em ? m.accentInk : m.signal,
    });
    T(s, c.title, {
      x: x + 0.22,
      y: BODY_Y + 0.46,
      w: g.w - 0.44,
      h: 0.38,
      fontSize: 20,
      bold: true,
      color: m.ink,
    });
    T(s, c.sub, {
      x: x + 0.22,
      y: BODY_Y + 0.84,
      w: g.w - 0.44,
      h: 0.28,
      fontSize: 13,
      color: m.ink3,
    });
    fitImage(s, path.join(HERE, c.img), c.iw, c.ih, x + 0.22, BODY_Y + 1.22, g.w - 0.44, 2.22, c.title, m.deep);
    T(s, c.body, {
      x: x + 0.22,
      y: BODY_Y + 3.60,
      w: g.w - 0.44,
      h: 0.96,
      fontSize: 13.5,
      color: m.ink2,
    });
  });

  caption(s, m, "Left photo: Alvin Lucier (Den Haag, 2010, Wikimedia Commons)  ·  Centre & right: Benn Jordan, “How Playing Music Changes Your Brain” (YouTube, 2023).", {
    x: M,
    y: BODY_Y + 4.86,
    w: CW,
    h: 0.30,
  });

  s.addNotes(
    "Two touchstones: Alvin Lucier in 1965 made alpha waves physically vibrate percussion instruments, and Benn Jordan in 2023 patched a consumer EEG band into modular synths. " +
      "Infinite already is a modular audiovisual patcher, so adding a Brain Source node is a natural fit."
  );
}

// ================================================================ 04. CORE IDEA: LFO -> PIXEL (Mist)
pres.addSection({ title: "The Idea" });
{
  const { s, m } = makeSlide("The Idea", "mist", "02 / THE CORE IDEA", "Swap an LFO for a brain signal, and the patch stays the same");
  const imgW = 7.45;
  const imgH = +(imgW / (1600 / 900)).toFixed(4); // 4.1906
  const rightX = M + imgW + 0.28;
  const rightW = CW - imgW - 0.28; // 4.4033

  fitImage(s, path.join(HERE, "lfo_to_pixel.jpg"), 1600, 900, M, BODY_Y, imgW, imgH, "LFO node cabled into Field Pixel shader", m.s1);
  caption(s, m, "3D view of Infinite: an LFO modulator cabled into a Field Pixel shader node.", {
    x: M,
    y: BODY_Y + imgH + 0.14,
    w: imgW,
    h: 0.32,
  });

  const parts = [
    ["LFO", "A slow sine wave, cycling once every 4 beats."],
    ["Cable (0 to 1)", "Carries that slow wave onto any knob in the patch."],
    ["Field Pixel", "A GPU shader whose warp breathes along with the wave."],
  ];
  parts.forEach(([h, b], i) => {
    const y = BODY_Y + i * 1.14;
    card(s, rightX, y, rightW, 1.00, m.s1, `part ${i + 1}`);
    T(s, h, { x: rightX + 0.22, y: y + 0.14, w: rightW - 0.44, h: 0.30, fontSize: 15.5, bold: true, color: m.signal });
    T(s, b, { x: rightX + 0.22, y: y + 0.46, w: rightW - 0.44, h: 0.44, fontSize: 13, color: m.ink2 });
  });

  card(s, rightX, BODY_Y + 3 * 1.14, rightW, 1.38, m.accentInk, "swap key");
  T(s, "Infinite does not care where a 0–1 signal comes from.\nSwap the LFO for your brain, and the picture breathes with you.", {
    x: rightX + 0.22,
    y: BODY_Y + 3 * 1.14 + 0.14,
    w: rightW - 0.44,
    h: 1.10,
    fontSize: 14,
    bold: true,
    color: m.onAccent,
    valign: "middle",
  });

  s.addNotes(
    "This is the entire design in one image. In Infinite, an LFO, a MIDI slider, or a brainwave all arrive as a 0 to 1 signal on a cable. " +
      "You don't need to invent a separate visual language — any existing node reacts immediately."
  );
}

// ================================================================ 05. TWO ROUTES: OLD VS DIRECT (Paper)
{
  const { s, m } = makeSlide("The Idea", "paper", "02 / WHY DIRECT SIGNALS", "Skip mood guesses. Send raw fundamentals into the patch.");

  const g = cols(4, 0.34);
  const drawRow = (label, labelCol, items, y, boxFill, titleCol, bodyCol, arrowCol) => {
    T(s, label, { x: M, y: y - 0.30, w: CW, h: 0.26, fontFace: "Geist Mono", fontSize: 10.5, bold: true, color: labelCol });
    items.forEach(([h, b], i) => {
      const x = g.x(i);
      card(s, x, y, g.w, 1.18, boxFill, `${label}-${i}`);
      T(s, h, { x: x + 0.18, y: y + 0.16, w: g.w - 0.36, h: 0.32, fontSize: 14.5, bold: true, color: titleCol });
      T(s, b, { x: x + 0.18, y: y + 0.50, w: g.w - 0.36, h: 0.56, fontSize: 12.5, color: bodyCol });
      if (i < 3) {
        s.addShape(pres.ShapeType.rightArrow, {
          x: x + g.w + 0.06,
          y: y + 0.47,
          w: 0.22,
          h: 0.24,
          fill: { color: arrowCol },
          line: { type: "none" },
          objectName: `arrow ${label} ${i}`,
        });
      }
    });
  };

  drawRow(
    "OLD ROUTE (FIRST VERSION, ABANDONED AS MAIN PATH)",
    m.ink3,
    [
      ["4 sensors", "Recorded separately"],
      ["Averaged into 1", "Threw away which region fired"],
      ["4 mood scores", "Unreliable arousal & valence"],
      ["Preset flower", "Same orange mandala every time"],
    ],
    BODY_Y + 0.34,
    m.deep,
    m.ink2,
    m.ink3,
    m.line
  );

  drawRow(
    "DIRECT ROUTE (CURRENT DESIGN)",
    m.signal,
    [
      ["4 separate streams", "Forehead + behind-ear kept apart"],
      ["Split three voices", "Brain rhythms, blinks, jaw muscle"],
      ["Direct outputs", "Picture, sound, 0–1 pins, triggers"],
      ["Your patch decides", "Artist learns to play it live"],
    ],
    BODY_Y + 2.08,
    m.s1,
    m.signal,
    m.ink2,
    m.signal
  );

  // Bottom strip: 4 equal cards strictly aligned to the same 4 columns!
  const pins = [
    ["Image pin", "Scrolling 4-sensor spectrum texture"],
    ["Audio pin", "Brain waves sped up 32× into pitch"],
    ["24 modulator pins", "Bands, calm score, blink, jaw triggers"],
    ["Brain Sculpt", "Shift-grab a knob to teach your mapping"],
  ];
  pins.forEach(([h, b], i) => {
    const x = g.x(i);
    card(s, x, BODY_Y + 3.68, g.w, 1.16, i === 3 ? m.accentInk : m.s3, `pin ${i + 1}`);
    T(s, h, {
      x: x + 0.18,
      y: BODY_Y + 3.84,
      w: g.w - 0.36,
      h: 0.30,
      fontSize: 14,
      bold: true,
      color: i === 3 ? m.onAccent : m.ink,
    });
    T(s, b, {
      x: x + 0.18,
      y: BODY_Y + 4.18,
      w: g.w - 0.36,
      h: 0.54,
      fontSize: 12.5,
      color: i === 3 ? m.onAccent : m.ink2,
    });
  });

  s.addNotes(
    "Our first prototype averaged all 4 sensors into mood numbers like 'arousal' and 'valence', then drove a preset flower. " +
      "Literature showed those mood scores are unreliable on 4 dry sensors, and averaging erased the strongest signal behind the ears. " +
      "Now we keep all 4 sensors separate and send raw building blocks straight to the patch."
  );
}

// ================================================================ 06. EIGHT BUILDING BLOCKS (Mist)
{
  const { s, m } = makeSlide("The Idea", "mist", "02 / EIGHT BUILDING BLOCKS", "What brain data becomes inside Infinite");
  const g = cols(4, 0.22);
  const r = rows(2, BODY_Y, 4.72, 0.24);

  const blocks = [
    ["01", "Pixel (2D space)", "Image pin: 4 sensors × 40 Hz scrolling spectrogram", "Built"],
    ["02", "Oscillation & pitch", "Audio pin: waves sped up 32× (10 Hz alpha → 320 Hz)", "Built"],
    ["03", "Voltage (0–1 knob)", "Band power and calibrated calm score ~20×/s", "Built"],
    ["04", "Impulse / event", "Blink or left/right jaw clench → clean note trigger", "Built"],
    ["05", "Logic (0 / 1)", "Eyes closed > 2 s → switch on without edge flicker", "Built"],
    ["06", "Relationship", "Left vs right balance, front vs ear sync", "Partly"],
    ["07", "Vertex (3D form)", "Region differences mapped to moving (x, y, z) points", "Planned"],
    ["08", "Memory & age", "How long a calm state has held; time since last blink", "Planned"],
  ];

  blocks.forEach(([num, title, desc, status], idx) => {
    const col = idx % 4;
    const row = Math.floor(idx / 4);
    const x = g.x(col);
    const y = r.y(row);
    card(s, x, y, g.w, r.h, m.s1, `block ${num}`);

    T(s, num, {
      x: x + 0.20,
      y: y + 0.18,
      w: 0.6,
      h: 0.26,
      fontFace: "Geist Mono",
      fontSize: 11,
      bold: true,
      color: m.ink3,
    });
    const stCol = status === "Built" ? m.pass : status === "Partly" ? m.signal : m.ink3;
    T(s, status.toUpperCase(), {
      x: x + g.w - 1.20,
      y: y + 0.18,
      w: 1.00,
      h: 0.26,
      fontFace: "Geist Mono",
      fontSize: 9.5,
      bold: true,
      align: "right",
      color: stCol,
    });
    T(s, title, {
      x: x + 0.20,
      y: y + 0.50,
      w: g.w - 0.40,
      h: 0.36,
      fontSize: 16,
      bold: true,
      color: m.ink,
    });
    T(s, desc, {
      x: x + 0.20,
      y: y + 0.94,
      w: g.w - 0.40,
      h: r.h - 1.08,
      fontSize: 12.5,
      color: m.ink2,
    });
  });

  caption(s, m, "Five primitives are built and tested today; left/right balance exists; 3D points and state age are next.", {
    x: M,
    y: BODY_Y + 4.86,
    w: CW,
    h: 0.30,
  });

  s.addNotes(
    "Infinite works with eight basic signal types — pixels, pitch, modulation, triggers, switches, relationships, 3D points, and time/memory. " +
      "Five are already built and tested. We don't pretend to decode thoughts; we translate voltage into these eight musical primitives."
  );
}

// ================================================================ 07. ALPHA BELL IN ACTION (Midnight)
{
  const { s, m } = makeSlide("The Idea", "midnight", "02 / STARTER INSTRUMENT", "Alpha Bell: close your eyes and it blooms and sings");
  const g = cols(2, 0.36);

  const bells = [
    {
      img: "bell_calm.png",
      tag: "EYES CLOSED  ·  CALM ALPHA",
      stat: "Luma 0.41  ·  369 Hz tone  ·  -13.6 LUFS",
      desc: "Alpha rises, ring expands and glows warm, reverb swells smoothly.",
      col: m.accent,
    },
    {
      img: "bell_focus.png",
      tag: "EYES OPEN  ·  FOCUS",
      stat: "Luma 0.13  ·  568 Hz  ·  -24.2 LUFS",
      desc: "Alpha drops, ring dims and tightens, audio drops into a quiet hum.",
      col: m.signal,
    },
  ];

  bells.forEach((b, i) => {
    const x = g.x(i);
    card(s, x, BODY_Y, g.w, 4.18, m.s1, `bell ${i + 1}`);
    fitImage(s, path.join(HERE, b.img), 480, 480, x + 0.22, BODY_Y + 0.22, 3.00, 3.00, b.tag, m.deep);
    const tx = x + 3.44;
    const tw = g.w - 3.66;
    T(s, b.tag, { x: tx, y: BODY_Y + 0.30, w: tw, h: 0.28, fontFace: "Geist Mono", fontSize: 10.5, bold: true, color: b.col });
    T(s, b.stat, { x: tx, y: BODY_Y + 0.72, w: tw, h: 0.65, fontFace: "Geist Mono", fontSize: 13.5, bold: true, color: m.ink });
    T(s, b.desc, { x: tx, y: BODY_Y + 1.50, w: tw, h: 1.50, fontSize: 14, color: m.ink2 });
    caption(s, m, "Headless render at t = 5.00 s (bci_alpha_bell.inf)", { x: x + 0.22, y: BODY_Y + 3.42, w: g.w - 0.44, h: 0.30 });
  });

  card(s, M, BODY_Y + 4.38, CW, 0.72, m.s2, "patch flow");
  T(s, "Signal path:  Brain Source (alpha) → Field Pixel bell + Reverb/Gain   ·   Blink → Logic Gate toggles spectrogram overlay   ·   Brain Sculpt → Shift-grab radius", {
    x: M + 0.24,
    y: BODY_Y + 4.38,
    w: CW - 0.48,
    h: 0.72,
    fontSize: 13.5,
    color: m.ink,
    valign: "middle",
  });

  s.addNotes(
    "This is our starter patch, Alpha Bell. When you close your eyes and alpha rises, the ring expands and the 32× sped-up tone swells. " +
      "When you focus with eyes open, it dims and quiets down. Blinks toggle the raw spectrogram overlay, and Brain Sculpt lets you Shift-grab any knob to teach your own range."
  );
}

// ================================================================ 08. THREE VOICES IN ONE MIC (Paper)
pres.addSection({ title: "Bridge" });
{
  const { s, m } = makeSlide("Bridge", "paper", "03 / WHAT THE HEADSET HEARS", "What EEG, eye blinks, and jaw muscles actually look like");
  const leftW = 7.40;
  const rightX = M + leftW + 0.28;
  const rightW = CW - leftW - 0.28;
  const wfH = +(leftW / (1280 / 760)).toFixed(4); // 4.3938 in

  fitImage(s, path.join(HERE, "ref_waveforms.png"), 1280, 760, M, BODY_Y, leftW, wfH, "Real recorded EEG, EOG blink, and EMG jaw waveforms", m.s1);
  caption(s, m, "Real 2-second recordings from PhysioNet, EEG-EyeBlinks, and HEROIC datasets. Note the voltage scale difference.", {
    x: M,
    y: BODY_Y + wfH + 0.12,
    w: leftW,
    h: 0.36,
  });

  // Right feature column
  card(s, rightX, BODY_Y, rightW, 2.22, m.deep, "microvolt card");
  T(
    s,
    [
      { text: "~10", options: { fontFace: "Geist", fontSize: 52, bold: true, color: m.signal } },
      { text: "  µV", options: { fontFace: "Geist Mono", fontSize: 15, bold: true, color: m.ink3 } },
    ],
    { x: rightX + 0.24, y: BODY_Y + 0.16, w: rightW - 0.48, h: 0.85 }
  );
  T(s, "Typical brainwave size (millionths of a volt). A blink is 10–30× bigger; a guitar pickup is ~10,000× louder.", {
    x: rightX + 0.24,
    y: BODY_Y + 1.08,
    w: rightW - 0.48,
    h: 0.96,
    fontSize: 13,
    color: m.ink2,
  });

  card(s, rightX, BODY_Y + 2.38, rightW, 1.56, m.s1, "stadium card");
  T(s, "The stadium analogy", { x: rightX + 0.24, y: BODY_Y + 2.52, w: rightW - 0.48, h: 0.32, fontSize: 15, bold: true, color: m.ink });
  T(s, "Like hearing a stadium from the car park. EEG hears thousands of neurons chanting in sync, never a single thought.", {
    x: rightX + 0.24,
    y: BODY_Y + 2.88,
    w: rightW - 0.48,
    h: 0.96,
    fontSize: 13,
    color: m.ink2,
  });

  card(s, rightX, BODY_Y + 4.10, rightW, 0.82, m.accentInk, "trigger shift");
  T(s, "Scientists delete blinks & jaw static.\nAn instrument keeps them as triggers.", {
    x: rightX + 0.24,
    y: BODY_Y + 4.14,
    w: rightW - 0.48,
    h: 0.74,
    fontSize: 13,
    bold: true,
    color: m.onAccent,
    valign: "middle",
  });

  s.addNotes(
    "Say (simple): Each sensor hears three things at once — tiny ripples from the brain, a giant bump when you blink, and fuzzy static when you clench your jaw. " +
      "Scientists throw blinks and jaw noise away; we separate them and turn them into musical triggers.\n" +
      "Go deeper (if asked): Brain EEG is ~10 µV (1–40 Hz), EOG eye blinks are 50–300 µV because the eyeball is a dipole, and jaw EMG is 25–150 Hz broadband."
  );
}

// ================================================================ 09. FOUR SENSORS (Mist)
{
  const { s, m } = makeSlide("Bridge", "mist", "03 / SENSOR PLACEMENT (10–20 MAP)", "Four dry sensors: what they reach, and what they miss");
  const leftW = 5.45;
  const rightX = M + leftW + 0.28;
  const rightW = CW - leftW - 0.28; // 6.4033

  fitImage(s, path.join(HERE, "ref_10_20.png"), 1280, 1146, M, BODY_Y, leftW, 4.68, "International 10-20 EEG placement map with Muse sensors highlighted", m.s1);
  caption(s, m, "Blue = Muse 2 sensors (AF7, AF8, TP9, TP10)  ·  Orange = out of reach (C3, C4 motor; O1, O2 vision).", {
    x: M,
    y: BODY_Y + 4.78,
    w: leftW,
    h: 0.34,
  });

  const spots = [
    ["Forehead  ·  AF7, AF8", "Front of the brain (attention, planning), plus every blink, glance, and frown.", m.signal],
    ["Behind the ears  ·  TP9, TP10", "Side of the brain (hearing, speech). Catches alpha spilling forward from the back, plus jaw muscle.", m.signal],
    ["Missing: Top & back  ·  C3/C4, O1/O2", "Movement strip (motor imagery) and vision sit under hair at the top and back. A dry band cannot reach them.", m.ink3],
  ];
  spots.forEach(([h, b, col], i) => {
    const y = BODY_Y + i * 1.18;
    card(s, rightX, y, rightW, 1.06, m.s1, `spot ${i + 1}`);
    T(s, h, { x: rightX + 0.24, y: y + 0.14, w: rightW - 0.48, h: 0.32, fontSize: 15, bold: true, color: col });
    T(s, b, { x: rightX + 0.24, y: y + 0.48, w: rightW - 0.48, h: 0.50, fontSize: 12.5, color: m.ink2 });
  });

  card(s, rightX, BODY_Y + 3.60, rightW, 1.08, m.accentInk, "honest limit");
  T(s, "Honest claim: four skin sensors read rhythm, left/right balance, and muscle.\nThey never read words, pictures, or hand-movement thoughts.", {
    x: rightX + 0.24,
    y: BODY_Y + 3.68,
    w: rightW - 0.48,
    h: 0.92,
    fontSize: 13.5,
    bold: true,
    color: m.onAccent,
    valign: "middle",
  });

  s.addNotes(
    "This is the standard 10-20 scalp map. Blue circles are the 4 sensors on a Muse headband — two on the forehead, two behind the ears. " +
      "Orange circles are motor imagery (C3/C4) and vision (O1/O2), which Muse cannot reach. Being upfront about this avoids overclaiming."
  );
}

// ================================================================ 10. BRAIN RHYTHMS (Paper)
{
  const { s, m } = makeSlide("Bridge", "paper", "03 / RHYTHMS & LFOs", "Brain rhythms are slow LFOs over a pink-noise floor");
  const leftW = 7.55;
  const rightX = M + leftW + 0.28;
  const rightW = CW - leftW - 0.28;

  s.addTable(
    [
      [tblHeader(m, "Rhythm"), tblHeader(m, "Rate"), tblHeader(m, "As tempo"), tblHeader(m, "Sped up 32×"), tblHeader(m, "Seen with")],
      [tblCell(m, "Delta"), tblCell(m, "1–4 Hz", { mono: true }), tblCell(m, "60–240 BPM"), tblCell(m, "32–128 Hz bass", { mono: true }), tblCell(m, "Deep sleep")],
      [tblCell(m, "Theta"), tblCell(m, "4–8 Hz", { mono: true }), tblCell(m, "Fast 16ths"), tblCell(m, "128–256 Hz", { mono: true }), tblCell(m, "Drowsy, inward")],
      [
        tblCell(m, "Alpha", { bold: true, color: m.signal, fill: m.deep }),
        tblCell(m, "8–12 Hz", { mono: true, bold: true, color: m.signal, fill: m.deep }),
        tblCell(m, "Tremolo", { bold: true, fill: m.deep }),
        tblCell(m, "256–384 Hz (≈ C4–G4)", { mono: true, bold: true, color: m.signal, fill: m.deep }),
        tblCell(m, "Eyes closed, calm", { bold: true, fill: m.deep }),
      ],
      [tblCell(m, "Beta"), tblCell(m, "12–30 Hz", { mono: true }), tblCell(m, "Flutter / buzz"), tblCell(m, "384–960 Hz", { mono: true }), tblCell(m, "Focus, tension")],
      [tblCell(m, "Gamma"), tblCell(m, "30+ Hz", { mono: true }), tblCell(m, "Low hum"), tblCell(m, "960 Hz +", { mono: true }), tblCell(m, "Mostly jaw/muscle")],
    ],
    {
      x: M,
      y: BODY_Y,
      w: leftW,
      colW: [1.05, 0.95, 1.45, 2.15, 1.95],
      rowH: 0.56,
      valign: "middle",
      margin: [0.04, 0.08, 0.04, 0.08],
    }
  );

  T(s, "1 Hz = 60 BPM. At 10 Hz, alpha is a modulation rate, not a musical note. Multiplying phase by 32 brings it cleanly into C4–G4.", {
    x: M,
    y: BODY_Y + 3.58,
    w: leftW,
    h: 0.65,
    fontSize: 13.5,
    color: m.ink2,
  });

  // Right column: Hans Berger 1929 trace + 1/f explanation
  card(s, rightX, BODY_Y, rightW, 2.28, m.s1, "berger card");
  T(s, "FIRST HUMAN EEG  ·  HANS BERGER (1929)", {
    x: rightX + 0.22,
    y: BODY_Y + 0.18,
    w: rightW - 0.44,
    h: 0.24,
    fontFace: "Geist Mono",
    fontSize: 9.5,
    bold: true,
    color: m.signal,
  });
  fitImage(s, path.join(HERE, "ref_berger_1929.png"), 916, 123, rightX + 0.22, BODY_Y + 0.52, rightW - 0.44, 0.88, "Hans Berger 1929 alpha wave recording", m.deep);
  T(s, "Closing the eyes brings out a 10 Hz alpha wave (top trace vs 10 Hz reference below). People can learn to raise it.", {
    x: rightX + 0.22,
    y: BODY_Y + 1.50,
    w: rightW - 0.44,
    h: 0.68,
    fontSize: 12.5,
    color: m.ink2,
  });

  card(s, rightX, BODY_Y + 2.48, rightW, 2.20, m.s3, "pink floor");
  T(s, "Why we subtract the 1/f floor", { x: rightX + 0.22, y: BODY_Y + 2.66, w: rightW - 0.44, h: 0.34, fontSize: 15.5, bold: true, color: m.ink });
  T(s, "Underneath brain rhythms sits a pink-noise slope (louder at low Hz). We fit that slope and measure alpha as the bump rising above it — just like reading an EQ curve.", {
    x: rightX + 0.22,
    y: BODY_Y + 3.08,
    w: rightW - 0.44,
    h: 1.40,
    fontSize: 13,
    color: m.ink2,
  });

  s.addNotes(
    "In audio terms, brainwaves are LFOs between 1 and 40 Hz. Alpha at 8–12 Hz is our hero signal: " +
      "Hans Berger saw it first in 1929 when a subject closed their eyes. Speed it up 32× and you hear it around middle C. " +
      "We also strip out the pink-noise 1/f background slope so we measure the actual alpha bump."
  );
}

// ================================================================ 11. SYSTEM & 72-SECOND CALIBRATION (Mist)
{
  const { s, m } = makeSlide("Bridge", "mist", "03 / INSIDE INFINITE  ·  BRAIN SOURCE NODE", "From headset microvolts to 24 control pins + sound + picture");
  const nodeW = 2.95;
  const rightX = M + nodeW + 0.28;
  const rightW = CW - nodeW - 0.28; // 8.9033

  // Left: Real Brain Source node screenshot from build-dev/Infinite.app!
  card(s, M, BODY_Y, nodeW, 4.92, m.deep, "node frame");
  fitImage(s, path.join(HERE, "Brain Source.png"), 242, 298, M + 0.14, BODY_Y + 0.14, nodeW - 0.28, 3.38, "Live Brain Source node in Infinite", null);
  T(s, "LIVE IN INFINITE (C++ BUILD)", {
    x: M + 0.18,
    y: BODY_Y + 3.64,
    w: nodeW - 0.36,
    h: 0.24,
    fontFace: "Geist Mono",
    fontSize: 9,
    bold: true,
    color: m.signal,
  });
  T(s, "Top: 4 sensor pills + live wave.\nButtons: 72 s Calibrate & Rec.\nBottom: 24 modulation pins + Image + Audio.", {
    x: M + 0.18,
    y: BODY_Y + 3.92,
    w: nodeW - 0.36,
    h: 0.88,
    fontSize: 11.5,
    color: m.ink2,
  });

  // Top right: 4 beginner-friendly steps
  const g4 = (rightW - 3 * 0.20) / 4;
  const steps = [
    ["01  STREAM", "Headset sends 4 numbers in µV, 256× a second over Bluetooth."],
    ["02  72\u00A0s CHECK", "30\u00A0s eyes open + 30\u00A0s eyes closed + 5 blinks = your personal baseline."],
    ["03  MEASURE", "C++ DSP strips hum/noise and splits 5 bands + blink/jaw triggers."],
    ["04  PATCH OUT", "24 control pins (0–1) + Image + Audio wire to any node live."],
  ];
  steps.forEach(([h, b], i) => {
    const x = +(rightX + i * (g4 + 0.20)).toFixed(4);
    const isCal = i === 1;
    card(s, x, BODY_Y, g4, 2.55, isCal ? m.accentInk : m.s1, `step ${i + 1}`);
    T(s, h, {
      x: x + 0.16,
      y: BODY_Y + 0.18,
      w: g4 - 0.32,
      h: 0.28,
      fontFace: "Geist Mono",
      fontSize: 9.5,
      bold: true,
      color: isCal ? m.onAccent : m.signal,
    });
    T(s, b, {
      x: x + 0.16,
      y: BODY_Y + 0.56,
      w: g4 - 0.32,
      h: 1.82,
      fontSize: 12.5,
      color: isCal ? m.onAccent : m.ink2,
    });
    if (i < 3) {
      s.addShape(pres.ShapeType.rightArrow, {
        x: x + g4 + 0.03,
        y: BODY_Y + 1.15,
        w: 0.14,
        h: 0.20,
        fill: { color: m.signal },
        line: { type: "none" },
        objectName: `pipe arrow ${i}`,
      });
    }
  });

  // Bottom right: 3 symmetric Hero Numeral stat tiles
  const g3w = (rightW - 2 * 0.22) / 3;
  const stats = [
    ["0.10", "ms", "Compute per update on M2 (budget: 2\u00A0ms)", m.signal],
    ["5", "headsets", "Muse 2/S, OpenBCI ×2, Crown + replay", m.ink],
    ["0", "hand weights", "Main calm score learned in 72\u00A0s setup", m.signal],
  ];
  stats.forEach(([num, unit, desc, col], i) => {
    const x = +(rightX + i * (g3w + 0.22)).toFixed(4);
    const y = BODY_Y + 2.82;
    card(s, x, y, g3w, 2.10, m.s1, `stat ${i + 1}`);
    T(
      s,
      [
        { text: num, options: { fontFace: "Geist", fontSize: 40, bold: true, color: col } },
        { text: " " + unit, options: { fontFace: "Geist Mono", fontSize: 11.5, bold: true, color: m.ink3 } },
      ],
      { x: x + 0.20, y: y + 0.22, w: g3w - 0.40, h: 0.72 }
    );
    T(s, desc, { x: x + 0.20, y: y + 1.06, w: g3w - 0.40, h: 0.86, fontSize: 12.5, color: m.ink2 });
  });

  s.addNotes(
    "Say (simple): On the left is the actual Brain Source node inside Infinite. You wear the band for a 72-second sound-check — " +
      "30 seconds eyes open, 30 seconds eyes closed, 5 blinks — and the node outputs 24 modulation pins plus live Image and Audio pins.\n" +
      "Go deeper (if asked): BrainFlow reads 256 Hz raw µV samples on a background worker thread, BciDspEngine computes 16 relative log-band features " +
      "(4 bands × 4 sensors) and fits shrinkage LDA during calibration in 0.10 ms per update."
  );
}

// ================================================================ 12. RESULT 1: ALPHA BY SITE (Paper)
pres.addSection({ title: "Results" });
{
  const { s, m } = makeSlide("Results", "paper", "04 / RESULT 01  ·  GEL LAB CAP VS DRY MUSE", "Alpha is real on a Muse, mostly behind the ears");
  const chartW = 6.95;
  const rightX = M + chartW + 0.28;
  const rightW = CW - chartW - 0.28; // 4.9033

  s.addChart(
    pres.ChartType.bar,
    [
      { name: "PhysioNet (gel cap, 109)", labels: ["AF7", "AF8", "TP9", "TP10"], values: [1.19, 1.23, 1.77, 1.85] },
      { name: "GIPSA (gel cap, 20)", labels: ["AF7", "AF8", "TP9", "TP10"], values: [2.14, 2.36, 3.04, 3.95] },
      { name: "HEROIC (Muse 2, 14)", labels: ["AF7", "AF8", "TP9", "TP10"], values: [1.05, 1.07, 1.44, 1.43] },
    ],
    {
      x: M,
      y: BODY_Y,
      w: chartW,
      h: 4.78,
      barDir: "col",
      barGapWidthPct: 60,
      ...chartOpts(m, "Alpha power: eyes closed ÷ eyes open (median; 1.0 = no change)"),
      chartColors: [m.ink3, m.signal, m.accent],
      showLegend: true,
      legendPos: "b",
      dataLabelFormatCode: "0.00",
      valAxisMinVal: 0,
      valAxisMaxVal: 4.5,
      valAxisLabelFormatCode: "0.0",
      dataLabelFontSize: 9.5,
    }
  );

  // Right column top: side-by-side Gel lab cap vs Dry Muse 2 visual
  const miniW = (rightW - 0.16) / 2;
  card(s, rightX, BODY_Y, miniW, 1.98, m.s1, "gel cap card");
  fitImage(s, path.join(HERE, "ref_gel_cap.jpg"), 1280, 853, rightX + 0.12, BODY_Y + 0.12, miniW - 0.24, 1.32, "Lab gel EEG cap", m.deep);
  T(s, "Lab gel cap (PhysioNet, GIPSA)", { x: rightX + 0.12, y: BODY_Y + 1.50, w: miniW - 0.24, h: 0.40, fontSize: 11, bold: true, color: m.signal, align: "center" });

  const rx2 = rightX + miniW + 0.16;
  card(s, rx2, BODY_Y, miniW, 1.98, m.s1, "muse card");
  fitImage(s, path.join(HERE, "ref_muse2.jpg"), 1362, 1362, rx2 + 0.12, BODY_Y + 0.12, miniW - 0.24, 1.32, "Consumer Muse 2 dry headband", m.deep);
  T(s, "Dry Muse 2 (HEROIC)", { x: rx2 + 0.12, y: BODY_Y + 1.50, w: miniW - 0.24, h: 0.40, fontSize: 11, bold: true, color: m.accentInk, align: "center" });

  // Right middle: s001_region_sheet (dark spectrogram -> Midnight deep well #0D1020)
  fitImage(s, path.join(PROOF, "s001_region_sheet.png"), 1230, 810, rightX, BODY_Y + 2.12, rightW, 1.80, "Per-sensor spectrogram S001", MODES.midnight.deep);

  card(s, rightX, BODY_Y + 4.04, rightW, 0.74, m.deep, "muse vs gel");
  T(s, "Muse rise is ~⅓ of a gel lab cap. Forehead stays ~1.05×; behind the ears (TP9/TP10) reaches 1.44×.", {
    x: rightX + 0.18,
    y: BODY_Y + 4.08,
    w: rightW - 0.36,
    h: 0.66,
    fontSize: 12.5,
    bold: true,
    color: m.ink,
    valign: "middle",
  });

  s.addNotes(
    "Say (simple): We tested our code on three public EEG datasets: two recorded with wet medical gel caps in labs (PhysioNet and GIPSA), and one recorded on a real dry Muse headband (HEROIC). " +
      "On Muse, closing your eyes barely changes the forehead, while behind the ears rises 1.44×.\n" +
      "Go deeper (if asked): PhysioNet has 109 subjects (eyes open/closed runs R01/R02), GIPSA has 20 subjects, HEROIC has 64 Muse 2 sessions across 14–15 people. " +
      "Values are eyes-closed / eyes-open alpha prominence above the 1/f fit."
  );
}

// ================================================================ 13. RESULT 2: CALIBRATION (Mist)
{
  const { s, m } = makeSlide("Results", "mist", "04 / RESULT 02  ·  72-SECOND CALIBRATION", "Per-person calibration turns a coin flip into 88.5%");
  const chartW = 7.35;
  const rightX = M + chartW + 0.28;
  const rightW = CW - chartW - 0.28; // 4.5033

  s.addChart(
    pres.ChartType.bar,
    [
      { name: "Fixed averaged rule", labels: ["HEROIC (Muse 2)", "GIPSA (gel)", "PhysioNet (gel)"], values: [68.3, 91.7, 61.5] },
      { name: "Per-person calibration (LDA)", labels: ["HEROIC (Muse 2)", "GIPSA (gel)", "PhysioNet (gel)"], values: [88.5, 100.0, 100.0] },
    ],
    {
      x: M,
      y: BODY_Y,
      w: chartW,
      h: 4.65,
      barDir: "col",
      barGapWidthPct: 65,
      ...chartOpts(m, "Eyes open vs closed accuracy (%, median held-out)"),
      chartColors: [m.line, m.signal],
      showLegend: true,
      legendPos: "b",
      valAxisMinVal: 0,
      valAxisMaxVal: 100,
      dataLabelFormatCode: "0.0",
    }
  );

  const callouts = [
    ["88.5%", "Muse, same session (chance = 50%)", m.signal, m.s1],
    ["85.5%", "Muse, later day (day-1 weights + median re-centre)", m.onAccent, m.accentInk],
    ["73%", "of Muse sessions improved by calibrating", m.signal, m.s1],
  ];
  callouts.forEach(([num, desc, txtCol, bgCol], i) => {
    const y = BODY_Y + i * 1.60;
    card(s, rightX, y, rightW, 1.44, bgCol, `cal callout ${i + 1}`);
    T(s, num, { x: rightX + 0.24, y: y + 0.14, w: rightW - 0.48, h: 0.62, fontFace: "Geist", fontSize: 34, bold: true, color: txtCol });
    T(s, desc, { x: rightX + 0.24, y: y + 0.80, w: rightW - 0.48, h: 0.52, fontSize: 13, color: bgCol === m.accentInk ? m.onAccent : m.ink2 });
  });

  caption(s, m, "First half calibrates, second half tests (6 s smoothing). Gel 100% is optimistic; 85.5% cross-day on Muse is the honest number.", {
    x: M,
    y: BODY_Y + 4.82,
    w: CW,
    h: 0.30,
  });

  s.addNotes(
    "This is our strongest result. A fixed averaged-alpha rule gets 68.3% on Muse (and drops to 50% on a later day). " +
      "Fitting a personal weighting across the 16 meters lifts same-day accuracy to 88.5%, and 85.5% on a later day just by re-centring the midpoint."
  );
}

// ================================================================ 14. RESULT 3: READABILITY (Paper)
{
  const { s, m } = makeSlide("Results", "paper", "04 / RESULT 03  ·  BLIND CLIP TEST", "Your state reads at a glance, and by ear");
  const imgW = 5.45;
  const imgH = +(imgW / (890 / 770)).toFixed(4); // 4.715
  const rightX = M + imgW + 0.30;
  const rightW = CW - imgW - 0.30; // 6.3833

  fitImage(s, path.join(HERE, "m3_pairs.png"), 890, 770, M, BODY_Y, imgW, imgH, "Two blind test pairs: calibrated state strip over 4-channel spectrograms", m.s1);
  caption(s, m, "Two 10\u00A0s Muse pairs: calibrated score strip on top, 4 sensors (1–40\u00A0Hz) below.", {
    x: M,
    y: BODY_Y + imgH + 0.10,
    w: imgW,
    h: 0.48,
  });

  s.addChart(
    pres.ChartType.bar,
    [
      {
        name: "Pairs right (%)",
        labels: ["Raw spectrogram", "Cleaned spectrogram", "Sound (EEG ×32)", "Calibrated score strip"],
        values: [73.0, 73.8, 85.7, 92.0],
      },
    ],
    {
      x: rightX,
      y: BODY_Y,
      w: rightW,
      h: 3.45,
      barDir: "bar",
      barGapWidthPct: 45,
      ...chartOpts(m, "Automatic observer on 126 Muse pairs (% right)"),
      chartColors: [m.line, m.line, m.ink3, m.signal],
      showLegend: false,
      valAxisMinVal: 50,
      valAxisMaxVal: 100,
      dataLabelFormatCode: "0.0",
    }
  );

  card(s, rightX, BODY_Y + 3.62, rightW, 1.10, m.deep, "human caveat");
  T(s, [
    { text: "Human check: 10 / 10 right, ", options: { bold: true, color: m.accentInk } },
    { text: "but the viewer was me (not blind to the top strip). A stranger who has never seen Infinite needs to take this test next.", options: { color: m.ink2 } },
  ], { x: rightX + 0.22, y: BODY_Y + 3.74, w: rightW - 0.44, h: 0.86, fontSize: 13.5, valign: "middle" });

  s.addNotes(
    "We gave an automatic observer two 10-second clips from the same Muse session and asked which had eyes closed. " +
      "The raw spectrogram alone is weak (73%), audified sound reaches 86%, and the calibrated top strip hits 92%. " +
      "I scored 10/10 visually, but I built it — so we need a naive viewer."
  );
}

// ================================================================ 15. SCORECARD (Paper)
{
  const { s, m } = makeSlide("Results", "paper", "04 / HONEST SCORECARD", "3 pass, 3 fail against bars set before running any data");

  const passCell = (t) => tblCell(m, t, { bold: true, color: m.pass });
  const failCell = (t) => tblCell(m, t, { bold: true, color: m.fail });

  s.addTable(
    [
      [tblHeader(m, "Pre-set test"), tblHeader(m, "Target bar"), tblHeader(m, "Measured result"), tblHeader(m, "Verdict")],
      [tblCell(m, "Alpha rises on ≥ 1 sensor", { bold: true }), tblCell(m, "≥ 80% of people", { mono: true }), tblCell(m, "Gel 72.5% & 100%  ·  Muse 50%"), failCell("Fail")],
      [tblCell(m, "One best sensor beats average", { bold: true }), tblCell(m, "≥ 70% of people", { mono: true }), tblCell(m, "29–50% (calibration wins in 60–86%)"), failCell("Fail → reframed")],
      [tblCell(m, "State readable in 10 s clip", { bold: true }), tblCell(m, "≥ 9 / 10 pairs", { mono: true }), tblCell(m, "10 / 10 (developer, not blind)"), passCell("Pass*")],
      [tblCell(m, "Deliberate blink as on/off switch", { bold: true }), tblCell(m, "≥ 95% hit, ≤ 1 false/min", { mono: true }), tblCell(m, "Best 78% hit, 1.8 false/min"), failCell("Fail")],
      [tblCell(m, "Zero hand-tuned weights", { bold: true }), tblCell(m, "Code review"), tblCell(m, "All weights learned at calibration"), passCell("Pass")],
      [tblCell(m, "Real-time speed on Apple M2", { bold: true }), tblCell(m, "< 2 ms / update", { mono: true }), tblCell(m, "0.10 ms mean, 0.19 ms worst"), passCell("Pass")],
    ],
    {
      x: M,
      y: BODY_Y,
      w: CW,
      colW: [3.65, 2.35, 4.35, 1.78],
      rowH: 0.62,
      valign: "middle",
      margin: [0.05, 0.10, 0.05, 0.10],
    }
  );

  card(s, M, BODY_Y + 4.46, CW, 0.62, m.deep, "score summary");
  T(s, "Takeaway: the two alpha fails forced us to build per-person calibration. Blink toggling still fails because natural and deliberate blinks overlap.", {
    x: M + 0.22,
    y: BODY_Y + 4.46,
    w: CW - 0.44,
    h: 0.62,
    fontSize: 13.5,
    bold: true,
    color: m.signal,
    valign: "middle",
  });

  s.addNotes(
    "Every bar here was written down before downloading any EEG data. We don't hide the fails: " +
      "Muse alpha wasn't strong enough without calibration, and single-blink toggling gets fooled by natural blinks."
  );
}

// ================================================================ 16. OPEN QUESTIONS OVERVIEW (Mist)
pres.addSection({ title: "Open Questions" });
{
  const { s, m } = makeSlide("Open Questions", "mist", "05 / WHERE I NEED YOUR ADVICE", "Six open questions for this review");
  const g = cols(3, 0.26);
  const r = rows(2, BODY_Y, BODY_H - 0.08, 0.26);

  const qs = [
    ["01", "Which headset?", "4-sensor consumer dry band (Muse), or 8-channel research cap covering the back of the head?"],
    ["02", "How clean is the signal?", "How much of a Muse feature is true cortical rhythm vs subtle eye and muscle activity?"],
    ["03", "Which data to learn from?", "Public datasets have no music-making tasks. What recordings would make this believable?"],
    ["04", "How to process & adapt?", "72 s per-person calibration vs pretrained model; 6 s vs 2 s smoothing; day-to-day drift."],
    ["05", "Hardware budget", "Buy Muse (~$250), adapt OpenBCI (~$600–1,200), or build custom hardware?"],
    ["06", "What fails today", "Single-blink toggle, no live headset test yet, and single-session learning is unproven."],
  ];

  qs.forEach(([num, h, b], i) => {
    const col = i % 3;
    const row = Math.floor(i / 3);
    const x = g.x(col);
    const y = r.y(row);
    card(s, x, y, g.w, r.h, m.s1, `q ${num}`);
    T(s, num, {
      x: x + 0.24,
      y: y + 0.22,
      w: 0.70,
      h: 0.34,
      fontFace: "Geist Mono",
      fontSize: 14,
      bold: true,
      color: i === 0 ? m.accentInk : m.signal,
    });
    T(s, h, { x: x + 0.24, y: y + 0.64, w: g.w - 0.48, h: 0.40, fontSize: 18, bold: true, color: m.ink });
    T(s, b, { x: x + 0.24, y: y + 1.12, w: g.w - 0.48, h: r.h - 1.28, fontSize: 13.5, color: m.ink2 });
  });

  s.addNotes(
    "This is where I hand the floor to you. You know neuroscience and experimental design; I built the audiovisual engine. " +
      "The next four slides walk through these six questions."
  );
}

// ================================================================ 17. HEADSET & BUDGET (Paper)
{
  const { s, m } = makeSlide("Open Questions", "paper", "05 / QUESTIONS 01 & 05  ·  HARDWARE", "Which headset: 4-sensor consumer band or 8-sensor cap?");
  const g = cols(3, 0.28);

  const hw = [
    {
      tag: "BUY  ·  ~$250–$400",
      name: "Muse 2 / Muse S",
      img: "ref_muse2.jpg",
      iw: 1362,
      ih: 1362,
      specs: "4 dry sensors (AF7, AF8, TP9, TP10)\nReady now · Bluetooth · 30 s to wear\nTrade-off: misses back of head (O1/O2)",
      em: false,
    },
    {
      tag: "ADAPT  ·  ~$600–$1,200",
      name: "OpenBCI Ultracortex / Cyton",
      img: "ref_openbci.jpg",
      iw: 1080,
      ih: 1080,
      specs: "4–8+ dry or wet sensors anywhere\nOpen hardware · reaches occipital alpha\nTrade-off: bulkier, slower setup",
      em: false,
    },
    {
      tag: "LAB REFERENCE  ·  BENCHMARK",
      name: "Research gel EEG cap",
      img: "ref_gel_cap.jpg",
      iw: 1280,
      ih: 853,
      specs: "16–64 wet gel channels (PhysioNet / GIPSA)\nCleanest occipital alpha (~3× Muse)\nTrade-off: gel paste, clinical setup",
      em: false,
    },
  ];

  hw.forEach((h, i) => {
    const x = g.x(i);
    card(s, x, BODY_Y, g.w, 4.18, m.s1, `hw ${i + 1}`);
    T(s, h.tag, { x: x + 0.22, y: BODY_Y + 0.18, w: g.w - 0.44, h: 0.24, fontFace: "Geist Mono", fontSize: 9.5, bold: true, color: m.signal });
    T(s, h.name, { x: x + 0.22, y: BODY_Y + 0.44, w: g.w - 0.44, h: 0.36, fontSize: 16.5, bold: true, color: m.ink });
    fitImage(s, path.join(HERE, h.img), h.iw, h.ih, x + 0.22, BODY_Y + 0.90, g.w - 0.44, 1.98, h.name, m.deep);
    T(s, h.specs, { x: x + 0.22, y: BODY_Y + 3.02, w: g.w - 0.44, h: 1.05, fontSize: 12.5, color: m.ink2 });
  });

  card(s, M, BODY_Y + 4.36, CW, 0.76, m.accentInk, "ask hardware");
  T(s, "Ask the room: Is a 4-sensor Muse credible enough for a 5-person pilot ($250 shared), or do we need 8 sensors covering the back of the head?  (All supported via BrainFlow; software is free.)", {
    x: M + 0.24,
    y: BODY_Y + 4.36,
    w: CW - 0.48,
    h: 0.76,
    fontSize: 13.5,
    bold: true,
    color: m.onAccent,
    valign: "middle",
  });

  s.addNotes(
    "Infinite already supports Muse 2, Muse S, OpenBCI Ganglion/Cyton, and Neurosity Crown through BrainFlow. " +
      "A single shared Muse 2 runs a 5-person pilot for ~$250. My question: is 4 sensors at the edge of the head credible for a pilot, or should we borrow/use an 8-channel cap?"
  );
}

// ================================================================ 18. SIGNAL PURITY, DATA & PROCESSING (Mist)
{
  const { s, m } = makeSlide("Open Questions", "mist", "05 / QUESTIONS 02–04  ·  SIGNAL & PROCESSING", "Signal purity, training data, and day-to-day drift");
  const leftW = 5.85;
  const rightX = M + leftW + 0.28;
  const rightW = CW - leftW - 0.28;

  card(s, M, BODY_Y, leftW, 4.95, m.s1, "signal mix card");
  T(s, "WHAT A MUSE ACTUALLY RECORDS (ILLUSTRATIVE SCALE)", {
    x: M + 0.24,
    y: BODY_Y + 0.22,
    w: leftW - 0.48,
    h: 0.26,
    fontFace: "Geist Mono",
    fontSize: 9.5,
    bold: true,
    color: m.signal,
  });

  const noise = [
    ["Brain rhythms (~10 µV)", 0.16, m.signal],
    ["Blinks & eye roll", 0.85, m.accentInk],
    ["Jaw & face muscle", 0.65, m.accentInk],
    ["Mains hum (50 Hz)", 0.38, m.ink3],
    ["Loose strap / motion", 1.00, m.line],
  ];
  noise.forEach(([label, frac, col], i) => {
    const y = BODY_Y + 0.68 + i * 0.56;
    T(s, label, { x: M + 0.24, y, w: 2.35, h: 0.36, fontSize: 12.5, color: m.ink, valign: "middle" });
    card(s, M + 2.68, y + 0.06, leftW - 2.94, 0.24, m.deep, `n bg ${i}`);
    s.addShape(pres.ShapeType.roundRect, {
      x: M + 2.68,
      y: y + 0.06,
      w: (leftW - 2.94) * frac,
      h: 0.24,
      rectRadius: 0.06,
      fill: { color: col },
      line: { type: "none" },
      objectName: `n fg ${i}`,
    });
  });

  card(s, M + 0.24, BODY_Y + 3.60, leftW - 0.48, 1.12, m.s3, "dataset gap");
  T(s, "Dataset gap: 189 people across 4 public datasets (PhysioNet, GIPSA, HEROIC, EEG-EyeBlinks), but 0 recordings while making music. Eyes open/closed is clear ground truth; 'emotion' has none.", {
    x: M + 0.42,
    y: BODY_Y + 3.70,
    w: leftW - 0.84,
    h: 0.92,
    fontSize: 12.5,
    color: m.ink2,
    valign: "middle",
  });

  const qs = [
    ["Q2 · Artifacts vs brain", "How much of a Muse feature is still subtle eye or muscle activity, and how should we filter it properly without ICA (since 4 channels is too few)?"],
    ["Q3 · Pretrained vs 72 s setup", "Calibration reaches 88.5% but costs 72 s every session. Could a cross-subject pretrained model replace or shorten it?"],
    ["Q4 · Speed vs stability & drift", "6 s smoothing = 88.5%, 2 s = 73.7% (music wants fast). Re-centring recovers day-2 drift to 85.5%. Is there a better adaptive rule?"],
  ];
  qs.forEach(([h, b], i) => {
    const y = BODY_Y + i * 1.70;
    card(s, rightX, y, rightW, 1.54, m.s1, `proc q ${i + 1}`);
    T(s, h, { x: rightX + 0.24, y: y + 0.18, w: rightW - 0.48, h: 0.34, fontSize: 15.5, bold: true, color: m.signal });
    T(s, b, { x: rightX + 0.24, y: y + 0.58, w: rightW - 0.48, h: 0.86, fontSize: 13, color: m.ink2 });
  });

  s.addNotes(
    "These are pure neuroscience and signal-processing questions — exactly where I want your advice. " +
      "None of the public datasets had someone playing an instrument, and with only 4 channels we can't run ICA. " +
      "Also, musicians want <2 s responsiveness, whereas our best classifier uses a 6 s window."
  );
}

// ================================================================ 19. WHAT FAILS TODAY (Paper)
{
  const { s, m } = makeSlide("Open Questions", "paper", "05 / QUESTION 06  ·  CURRENT FAILURES", "What still fails today (especially single-blink toggling)");
  const chartW = 6.20;
  const rightX = M + chartW + 0.28;
  const rightW = CW - chartW - 0.28;

  s.addChart(
    pres.ChartType.bar,
    [
      { name: "No calibration", labels: ["Cued blinks", "Watching video", "Reading"], values: [70.8, 40.5, 32.8] },
      { name: "After 5 cued blinks", labels: ["Cued blinks", "Watching video", "Reading"], values: [78.0, 60.5, 65.5] },
    ],
    {
      x: M,
      y: BODY_Y,
      w: chartW,
      h: 4.65,
      barDir: "col",
      barGapWidthPct: 65,
      ...chartOpts(m, "Intentional blink switch hit rate (%, target bar ≥ 95%)"),
      chartColors: [m.line, m.accent],
      showLegend: true,
      legendPos: "b",
      valAxisMinVal: 0,
      valAxisMaxVal: 100,
      dataLabelFormatCode: "0.0",
    }
  );
  caption(s, m, "Replayed through real engine on EEG-EyeBlinks (46 people, OpenBCI). False triggers: 0.6–1.8/min.", {
    x: M,
    y: BODY_Y + 4.76,
    w: chartW,
    h: 0.34,
  });

  const fails = [
    ["1 · Single-blink switch fails", "Natural and deliberate blinks overlap in height and width. Next: test an intentional double blink."],
    ["2 · Zero live headset runs yet", "Every number in this deck comes from replaying public recordings and synthetic signals."],
    ["3 · Learning is untested", "Does control actually improve with 10 minutes of practice? Requires real users across sessions."],
    ["4 · Dead lead & 125 ms audio lag", "One bad sensor pauses all calibration (fix: per-channel blanking). Audio pin lags 125 ms (fine for drones, slow for drums)."],
  ];
  fails.forEach(([h, b], i) => {
    const y = BODY_Y + i * 1.24;
    card(s, rightX, y, rightW, 1.10, m.s1, `fail ${i + 1}`);
    T(s, h, { x: rightX + 0.22, y: y + 0.14, w: rightW - 0.44, h: 0.32, fontSize: 14.5, bold: true, color: i === 0 ? m.accentInk : m.signal });
    T(s, b, { x: rightX + 0.22, y: y + 0.48, w: rightW - 0.44, h: 0.54, fontSize: 12.5, color: m.ink2 });
  });

  s.addNotes(
    "Four honest limitations before we touch a real head: single-blink toggling gets confused by natural blinks (we want to test double-blink next), " +
      "everything so far is offline replay, learning hasn't been measured, and the Hilbert transform adds 125 ms to the audio pin."
  );
}

// ================================================================ 20. NEXT: PILOT (Midnight)
pres.addSection({ title: "Next" });
{
  const { s, m } = makeSlide("Next", "midnight", "06 / PROPOSED NEXT STEP", null);
  s.addImage({ path: LOGO_MARK, x: W - M - 0.95, y: 0.44, w: 0.95, h: 0.95 / 2.3273, altText: "Infinite mark" });

  s.addText(
    [
      { text: "Next: a 3–5 person ", options: { fontFace: "Geist", fontSize: 38, bold: true, color: m.ink } },
      { text: "pilot", options: { fontFace: "Caveat", fontSize: 50, bold: true, color: m.accent } },
    ],
    { isTextBox: true, margin: 0, x: M, y: 0.72, w: 9.8, h: 0.92 }
  );

  const g = cols(3, 0.28);
  const proto = [
    ["STEP 01  ·  72 SECONDS", "Calibrate", "30 s eyes open, 30 s closed, 5 blinks. Test live setup friction on a real Muse."],
    ["STEP 02  ·  10 MINUTES", "Play Alpha Bell", "Can a stranger see, hear, and steer the instrument? Rate felt control (1–7) and naive clip accuracy."],
    ["STEP 03  ·  2 MINUTES", "Test double blinks", "10 cued double blinks vs normal reading. Measure hit rate and false triggers per minute."],
  ];
  proto.forEach(([k, h, b], i) => {
    const x = g.x(i);
    card(s, x, 2.05, g.w, 2.65, m.s1, `pilot ${i + 1}`);
    T(s, k, { x: x + 0.24, y: 2.28, w: g.w - 0.48, h: 0.26, fontFace: "Geist Mono", fontSize: 10.5, bold: true, color: m.signal });
    T(s, h, { x: x + 0.24, y: 2.64, w: g.w - 0.48, h: 0.44, fontSize: 21, bold: true, color: m.ink });
    T(s, b, { x: x + 0.24, y: 3.18, w: g.w - 0.48, h: 1.32, fontSize: 14, color: m.ink2 });
  });

  card(s, M, 5.05, CW, 1.38, m.s2, "asks");
  T(s, "WHAT I WOULD LOVE YOUR FEEDBACK ON", { x: M + 0.28, y: 5.22, w: CW - 0.56, h: 0.26, fontFace: "Geist Mono", fontSize: 10.5, bold: true, color: m.accent });
  T(s, "1. Pilot protocol & headset choice (Muse vs 8-channel cap)    ·    2. Ethics & consent for recording 3–5 participants    ·    3. Any lab EEG hardware we could borrow to benchmark against", {
    x: M + 0.28,
    y: 5.56,
    w: CW - 0.56,
    h: 0.72,
    fontSize: 14.5,
    color: m.ink,
  });

  s.addNotes(
    "This 15-minute pilot answers the things offline data cannot: does it work on a real head, can a stranger feel control, and does a double blink fix the toggle? " +
      "I'd love your advice on the study design, ethics/consent, and whether any lab headset is available to compare against Muse."
  );
}

(async () => {
  await pres.writeFile({ fileName: OUT });
  await applyTheme(OUT, THEME);
  console.log("wrote", OUT);
})();
