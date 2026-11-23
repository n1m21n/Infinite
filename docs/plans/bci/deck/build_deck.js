// Infinite × BCI deck — built slide by slide (Infinite Brand Book v2.1)
// Usage:
//   NODE_PATH=<dir with pptxgenjs> node build_deck.js BCI_results.pptx <path to apply_theme.js>

const path = require("path");
const pptxgen = require("pptxgenjs");
const { applyTheme } = require(process.argv[3]);

const OUT = process.argv[2];
const HERE = __dirname;
const LOGO_MARK = path.join(HERE, "..", "..", "..", "..", "art", "brand", "logo", "png", "mark-ink-512.png");

const MODES = {
  midnight: {
    ground: "151930",
    deep: "0D1020",
    s1: "20263F",
    s2: "2A3052",
    line: "343B5E",
    ink: "EEF1FA",
    ink2: "A3AACB",
    ink3: "8890B1",
    signal: "A7B1FF",
    accent: "FF6B35",
    onAccent: "151930",
  },
  mist: {
    ground: "E4E2EF",
    deep: "D8D5E5",
    s1: "F7F5FB",
    s2: "FFFFFF",
    line: "C4C1D9",
    ink: "12152A",
    ink2: "404660",
    ink3: "5A6180",
    signal: "4A54D6",
    accent: "D9430E",
    accentInk: "B53700",
    onAccent: "FFFFFF",
  },
  paper: {
    ground: "FBFAF6",
    deep: "EFF0F4",
    s1: "FFFFFF",
    s2: "FFFFFF",
    line: "DAD8E6",
    ink: "12152A",
    ink2: "404660",
    ink3: "5A6180",
    signal: "4A54D6",
    accent: "D9430E",
    accentInk: "B53700",
    onAccent: "FFFFFF",
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

const W = 13.3333;
const M = 0.65; // symmetrical margins
const CW = W - 2 * M; // 12.0333 in

pres.defineSlideMaster({ title: "BASE", background: { color: MODES.paper.ground } });

// Minimal slide: plain ground, no kicker, no folio, no footer. Symmetrical margins M.
function makeSlide(modeName) {
  const m = MODES[modeName];
  const s = pres.addSlide({ masterName: "BASE" });
  s.background = { color: m.ground };
  return { s, m };
}

// ================================================================ SLIDE 01: ABOUT ME
// Source of the timeline: n1m21n room, site/public/data/links.json
{
  const { s, m } = makeSlide("paper");

  s.addText("Naman Soni", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 8, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273; // Infinite mark, top right on the margin
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  // Three interest groups, centred columns, converging on Infinite
  const GROUPS = [
    ["Mind", ["Neuroscience", "Psychology", "Consciousness", "Perception"]],
    ["Making", ["3D modelling and graphics", "Interactive art", "Design", "Creative technology"]],
    ["Sound", ["Electronic music", "Sound synthesis", "Algorithmic and data music", "Interactive music"]],
  ];
  const gw = CW / 3, HUBX = W / 2, TOP = 4.35, HUBY = 5.2;
  GROUPS.forEach(([head, items], i) => {
    const x = M + gw * i, cx = x + gw / 2;
    s.addText(head, {
      isTextBox: true, margin: 0, x, y: 1.85, w: gw, h: 0.6,
      fontFace: "Geist", fontSize: 32, bold: true, color: m.signal, align: "center", valign: "middle",
    });
    s.addText(items.map((t, k) => ({ text: t, options: { breakLine: k < items.length - 1 } })), {
      isTextBox: true, margin: 0, x, y: 2.6, w: gw, h: 1.85,
      fontFace: "Geist", fontSize: 18, color: m.ink, align: "center", valign: "top", paraSpaceAfter: 7,
    });
  });

  // Arrows: two curved sweeps and one straight drop, all ending with a head beside the word Infinite
  const AL = { color: m.signal, width: 2, endArrowType: "triangle" };
  const SX0 = M + gw / 2, SX1 = M + gw * 2.5, EY = 5.5, EX = 5.75; // left start, right start, end y, left end x
  const bx = SX0, bw = EX - SX0;
  s.addShape(pres.shapes.CUSTOM_GEOMETRY, {
    x: bx, y: TOP, w: bw, h: EY - TOP, fill: { color: m.ground, transparency: 100 }, line: AL,
    points: [{ x: 0, y: 0, moveTo: true }, { x: bw, y: EY - TOP, curve: { type: "cubic", x1: 0, y1: (EY - TOP) * 1.0, x2: bw * 0.45, y2: EY - TOP } }],
  });
  const rx = W - EX; // mirrored end x
  s.addShape(pres.shapes.CUSTOM_GEOMETRY, {
    x: rx, y: TOP, w: SX1 - rx, h: EY - TOP, fill: { color: m.ground, transparency: 100 }, line: AL,
    points: [{ x: SX1 - rx, y: 0, moveTo: true }, { x: 0, y: EY - TOP, curve: { type: "cubic", x1: SX1 - rx, y1: (EY - TOP) * 1.0, x2: (SX1 - rx) - bw * 0.45, y2: EY - TOP } }],
  });
  s.addShape(pres.shapes.LINE, { x: HUBX, y: TOP, w: 0, h: HUBY - TOP, line: AL });

  s.addText("Infinite", {
    isTextBox: true, margin: 0, x: HUBX - 3, y: HUBY + 0.05, w: 6, h: 0.5,
    fontFace: "Geist", fontSize: 28, bold: true, color: m.ink, align: "center", valign: "middle",
  });
  s.addText("One canvas where all three can be played", {
    isTextBox: true, margin: 0, x: HUBX - 3.5, y: HUBY + 0.58, w: 7, h: 0.32,
    fontFace: "Geist", fontSize: 14, color: m.ink, align: "center", valign: "middle",
  });

  // Links: four aligned columns
  const LINKS = [
    ["Infinite", "infinitedevices.org", "https://infinitedevices.org"],
    ["Code", "github.com/n1m21n", "https://github.com/n1m21n"],
    ["Music", "n1m21n.bandcamp.com", "https://n1m21n.bandcamp.com/"],
    ["Instagram", "@n1m21n", "https://www.instagram.com/n1m21n/"],
  ];
  const lcw = CW / LINKS.length;
  LINKS.forEach(([k, v, url], i) => {
    const x = M + lcw * i;
    s.addText(k.toUpperCase(), {
      isTextBox: true, margin: 0, x, y: 6.3, w: lcw, h: 0.24,
      fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: m.signal, align: "center", valign: "middle",
    });
    s.addText(v, {
      isTextBox: true, margin: 0, x, y: 6.55, w: lcw, h: 0.32,
      fontFace: "Geist", fontSize: 16, color: m.signal, align: "center", valign: "middle", hyperlink: { url, tooltip: url },
    });
  });

  s.addNotes("I'm Naman. Three things pull me: how the mind works, making things, and sound. Infinite is where they meet, and this project adds the brain as a live signal.");
}

// ================================================================ SLIDE 02: ABOUT INFINITE
{
  const { s, m } = makeSlide("paper");

  s.addText("Infinite", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 8, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  // Left: a real patch (visuals and sound wired together), with plain labels laid on it
  const IMG_W = 1988, IMG_H = 1098;
  const IX = M, IY = 2.2, IW = 7.2, IH = IW * IMG_H / IMG_W, sc = IW / IMG_W;
  s.addImage({ path: path.join(HERE, "infinite_patch.png"), x: IX, y: IY, w: IW, h: IH, altText: "A real Infinite patch: pictures and sound wired together" });
  // [label, x px, y px] in image pixels; Midnight tags so they read on the busy canvas
  [["PICTURES", 24, 322], ["CABLES", 440, 138], ["SOUND", 1150, 198], ["OUTPUT", 1640, 22]].forEach(([t, px, py]) => {
    const tw = 0.1 + t.length * 0.105;
    s.addText(t, {
      isTextBox: true, margin: 0, x: IX + px * sc, y: IY + py * sc, w: tw, h: 0.24,
      fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: "A7B1FF", align: "center", valign: "middle",
      fill: { color: MODES.midnight.ground },
    });
  });

  // Right: four answers (two text colours only: ink and Signal), spread over the image height
  const RX = IX + IW + 0.45, RW = W - M - RX, ROWS = [
    ["WHAT IT IS", "Nodes joined by cables, creating sound, images and 3D geometry.", 2],
    ["WHO IT IS FOR", "Musicians, VJs, creative coders, visual artists and experimenters.", 2],
    ["WHY IT CONNECTS", "Sound, image and geometry are not separate. Like nature, they are one connected whole, so they share one canvas, in real time.", 3],
    ["WHAT IS NEXT", "Body signals as instruments: tiny muscle movements, heart rate from Apple Watch sensors, and brain signals from EEG.", 3],
  ];
  const LINE = 0.25, LAB = 0.3;
  const used = ROWS.reduce((t, r) => t + LAB + r[2] * LINE, 0), gap = (IH - used) / (ROWS.length - 1);
  let ry = IY;
  ROWS.forEach(([k, v, n]) => {
    s.addText(k, {
      isTextBox: true, margin: 0, x: RX, y: ry, w: RW, h: 0.26,
      fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: m.signal, valign: "middle",
    });
    s.addText(v, {
      isTextBox: true, margin: 0, x: RX, y: ry + LAB, w: RW, h: n * LINE + 0.05,
      fontFace: "Geist", fontSize: 15, color: m.ink, valign: "top",
    });
    ry += LAB + n * LINE + gap;
  });

  s.addNotes("Infinite connects nodes with cables to make sound, images and 3D. Sound, image and geometry are one connected thing, so they live on one canvas in real time.");
}

// ================================================================ SLIDE 03: INSPIRATION
{
  const { s, m } = makeSlide("paper");

  s.addText("Inspiration", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 8, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  // Two equal halves with a gap; images share one height and edges
  const GAP = 0.5, HW = (CW - GAP) / 2, IY = 1.75, IH = HW / 1.6;
  const RXc = M + HW + GAP, SW = (HW - 0.2) / 2;
  s.addImage({ path: path.join(HERE, "s3_lucier.jpg"), x: M, y: IY, w: HW, h: IH, altText: "Alvin Lucier seated with electrodes, a gong and a snare drum behind him" });
  s.addImage({ path: path.join(HERE, "s3_drums.jpg"), x: RXc, y: IY, w: SW, h: IH, altText: "Benn Jordan drumming while wearing an EEG headband" });
  s.addImage({ path: path.join(HERE, "s3_patch.jpg"), x: RXc + SW + 0.2, y: IY, w: SW, h: IH, altText: "Benn Jordan's synth patch driven by his EEG headband" });

  const COLS = [
    { x: M, name: "Alvin Lucier, 1965", rows: [
      ["WHAT", "Brainwaves played a gong, drums and cymbals."],
      ["HOW", "Calm, eyes closed: the signal shook the speakers."],
      ["GEAR", "Scalp electrodes, amplifier, speakers, percussion."],
    ] },
    { x: RXc, name: "Benn Jordan, 2023", rows: [
      ["WHAT", "A musician wearing a consumer EEG headband."],
      ["HOW", "Focus and stress readings control synth sounds."],
      ["GEAR", "EEG headband, laptop, software synths, drum kit."],
    ] },
  ];
  COLS.forEach((c) => {
    let y = IY + IH + 0.2;
    s.addText(c.name, {
      isTextBox: true, margin: 0, x: c.x, y, w: HW, h: 0.36,
      fontFace: "Geist", fontSize: 18, bold: true, color: m.ink, valign: "middle",
    });
    y += 0.42;
    c.rows.forEach(([k, v]) => {
      s.addText(k, {
        isTextBox: true, margin: 0, x: c.x, y, w: 0.7, h: 0.28,
        fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: m.signal, valign: "middle",
      });
      s.addText(v, {
        isTextBox: true, margin: 0, x: c.x + 0.75, y, w: HW - 0.75, h: 0.28,
        fontFace: "Geist", fontSize: 14, color: m.ink, valign: "middle",
      });
      y += 0.32;
    });
  });

  s.addNotes("Two inspirations. In 1965 Alvin Lucier used his own brainwaves to shake speakers touching a gong and drums. In 2023 Benn Jordan used a consumer EEG headband to control synths. Infinite is built for this kind of patching.");
}

// ================================================================ SLIDE 04: SOFTWARE FUNDAMENTALS
{
  const { s, m } = makeSlide("paper");

  s.addText("Everything is numbers", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 9, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  const GAP = 0.5, CWc = (CW - 2 * GAP) / 3, IY = 1.85, IH = CWc * 860 / 1100;
  [
    ["s4_sound.png", "MUSIC", "Waves", "A wave that repeats. How many times a second it repeats (Hz) is the pitch you hear.", "A sound wave that repeats 440 times a second"],
    ["s4_image.png", "IMAGES", "Pixels", "A grid of tiny dots. Each dot is a mix of red, green and blue.", "A grid of pixels, one pixel split into red, green and blue"],
    ["s4_geometry.png", "3D", "Points", "Shapes made of points in space. Each point is three numbers: x, y and z.", "A cube made of eight points"],
  ].forEach(([img, tag, head, body, alt], k) => {
    const x = M + k * (CWc + GAP);
    s.addImage({ path: path.join(HERE, img), x, y: IY, w: CWc, h: IH, altText: alt });
    let y = IY + IH + 0.25;
    s.addText(tag, {
      isTextBox: true, margin: 0, x, y, w: CWc, h: 0.26,
      fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: m.signal, valign: "middle",
    });
    y += 0.3;
    s.addText(head, {
      isTextBox: true, margin: 0, x, y, w: CWc, h: 0.45,
      fontFace: "Geist", fontSize: 26, bold: true, color: m.ink, valign: "middle",
    });
    y += 0.52;
    s.addText(body, {
      isTextBox: true, margin: 0, x, y, w: CWc, h: 0.8,
      fontFace: "Geist", fontSize: 15, color: m.ink, valign: "top",
    });
  });

  s.addText("A brain headset gives numbers too. Infinite turns them into 0 to 1 signals that drive waves, pixels and points.", {
    isTextBox: true, margin: 0, x: M, y: 6.4, w: CW, h: 0.4,
    fontFace: "Geist", fontSize: 18, bold: true, color: m.signal, align: "center", valign: "middle",
  });

  s.addNotes("Under the hood, music is a wave measured in Hz, an image is a grid of red, green and blue pixels, and 3D shapes are points with x, y and z. Infinite works with all three as numbers, which is why they can control each other.");
}

// ================================================================ SLIDE 05: SWAP THE LFO FOR A BRAIN
{
  const { s, m } = makeSlide("paper");

  s.addText("Swap the LFO for a brain", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 10, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  // Left: two real nodes joined by one cable, labelled in plain words
  const CROP_W = 1300, CROP_H = 703, OX = 200, OY = 100;
  const IX = M, IY = 2.2, IW = 7.2, IH = IW * CROP_H / CROP_W, sc = IW / CROP_W;
  s.addImage({ path: path.join(HERE, "s5_illustration.png"), x: IX, y: IY, w: IW, h: IH, altText: "Flat illustration: an LFO and then a brain signal, each joined by a cable to a picture" });

  const RX = IX + IW + 0.45, RW = W - M - RX, ROWS = [
    ["THE SIGNAL", "A number from 0 to 1 that keeps changing. The cable carries it to a knob.", 2],
    ["TODAY", "An LFO, a simple repeating wave, moves the picture.", 2],
    ["WITH A BRAIN", "Swap the LFO for brain signals. The same cable now lets your brain move a picture, a sound or a 3D shape.", 3],
    ["THE BRAIN NODE", "Brain Source turns a headset's raw brain voltage into 24 of these signals.", 2],
  ];
  const LINE = 0.25, LAB = 0.3;
  const used = ROWS.reduce((t, r) => t + LAB + r[2] * LINE, 0), gap = (IH - used) / (ROWS.length - 1);
  let ry = IY;
  ROWS.forEach(([k, v, n]) => {
    s.addText(k, {
      isTextBox: true, margin: 0, x: RX, y: ry, w: RW, h: 0.26,
      fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: m.signal, valign: "middle",
    });
    s.addText(v, {
      isTextBox: true, margin: 0, x: RX, y: ry + LAB, w: RW, h: n * LINE + 0.05,
      fontFace: "Geist", fontSize: 15, color: m.ink, valign: "top",
    });
    ry += LAB + n * LINE + gap;
  });

  s.addNotes("An LFO is a simple repeating wave that moves a knob through a cable. Brain Source does the same job but its numbers come from a headset, so your brain moves the picture, the sound or the 3D shape.");
}

// ================================================================ SLIDE 06: INSIDE THE BRAIN NODE
{
  const { s, m } = makeSlide("paper");

  s.addText("Inside the Brain node", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 10, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  const IY = 1.7, IH = CW * 360 / 1500;
  s.addImage({ path: path.join(HERE, "s6_inside.png"), x: M, y: IY, w: CW, h: IH, altText: "Four steps inside the Brain node: sensors, clean, split into rhythms, out as 0 to 1 signals" });

  const GAP = 0.5, CWc = (CW - 2 * GAP) / 3, TY = IY + IH + 0.4;
  [
    ["WHY THESE CHANNELS", "A Muse 2 has 4 dry sensors: two on the forehead, two behind the ears. We keep only what they show reliably: calm (alpha), blinks, jaw and brow."],
    ["WHERE THE MOCK DATA COMES FROM", "A built-in simulator makes fake brain waves: calm, focus and dream states, with blinks and jaw clenches. Four public datasets add real recordings."],
    ["HOW IT IS TESTED", "Automatic checks: calm against focus, blink against jaw, then real recordings, where closing the eyes must raise alpha."],
  ].forEach(([k, v], n) => {
    const x = M + n * (CWc + GAP);
    s.addText(k, {
      isTextBox: true, margin: 0, x, y: TY, w: CWc, h: 0.26,
      fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: m.signal, valign: "middle",
    });
    s.addText(v, {
      isTextBox: true, margin: 0, x, y: TY + 0.32, w: CWc, h: 1.2,
      fontFace: "Geist", fontSize: 14, color: m.ink, valign: "top",
    });
  });

  s.addNotes("Inside the Brain node: four sensors give 256 numbers a second each, the node removes mains hum and slow drift, splits what is left into five brain rhythms plus blink and jaw detectors, and outputs 24 signals from 0 to 1 plus a picture and a sound. Without a headset a built-in simulator and four public datasets stand in, and automatic checks test every step.");
}

// ================================================================ SLIDE 06b: TEACH THE BRAIN YOUR MAPPING (Brain Sculpt)
{
  const { s, m } = makeSlide("paper");
  s.addText("Teach the brain your mapping", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 10, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });
  // Real shot of Brain Source -> Brain Sculpt -> bloom radius (headless patch, left to right), cropped to the nodes.
  const IY = 1.56, IH = CW * 944 / 2594;
  s.addImage({ path: path.join(HERE, "s6b_sculpt_shot.png"), x: M, y: IY, w: CW, h: IH, altText: "Real Infinite patch: Brain Source wired into Brain Sculpt, which drives the radius of a bloom on a glowing ring" });
  const GAP = 0.5, CWc = (CW - 2 * GAP) / 3, TY = IY + IH + 0.22;
  [
    ["SHIFT-GRAB A KNOB", "Hold Shift and grab any wired knob while you are in a mental state: calm, focused, surprised."],
    ["EACH RELEASE IS AN ANCHOR", "It stores the knob position and where your mind was in a 6D brain space: arousal, valence, entropy, coherence, brow, jaw."],
    ["THEN THE MIND MOVES IT", "A smooth Gaussian blend between anchors moves every bound knob as your mind travels between those states."],
  ].forEach(([k, v], n) => {
    const x = M + n * (CWc + GAP);
    s.addText(k, { isTextBox: true, margin: 0, x, y: TY, w: CWc, h: 0.26, fontFace: "Geist Mono", fontSize: 10, bold: true, charSpacing: 1.2, color: m.signal, valign: "middle" });
    s.addText(v, { isTextBox: true, margin: 0, x, y: TY + 0.3, w: CWc, h: 0.9, fontFace: "Geist", fontSize: 13, color: m.ink, valign: "top" });
  });
  s.addNotes("Brain Sculpt teaches the brain your own mapping. Brain Source feeds it the brain signals. Hold Shift and grab any wired knob while in a mental state; each release stores an anchor in a six-dimensional brain space (arousal, valence, entropy, coherence, brow, jaw). A Gaussian RBF blend between anchors then moves every bound knob as the mind moves between states.");
}

// ================================================================ SLIDE 07: THE HEADBAND AND ITS OUTPUTS
{
  const { s, m } = makeSlide("paper");

  s.addText("The headband and its outputs", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 10, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  const IY = 1.7, IH = 3.5, LWI = IH * 780 / 720, GAP = 0.5, RWI = CW - LWI - GAP;
  s.addImage({ path: path.join(HERE, "s7_headband.png"), x: M, y: IY, w: LWI, h: IH, altText: "Top view of the headband with four sensors: AF7 and AF8 on the forehead, TP9 and TP10 behind the ears" });
  s.addImage({ path: path.join(HERE, "s7_outputs.png"), x: M + LWI + GAP, y: IY, w: RWI, h: IH, altText: "The 24 outputs of the Brain node in four groups, plus image and audio" });
  s.addImage({ path: path.join(HERE, "s7_params.png"), x: M, y: IY + IH + 0.3, w: CW, h: CW * 160 / 2400, altText: "The settings of the Brain node" });

  s.addNotes("Four dry sensors: AF7 and AF8 on the forehead, TP9 and TP10 behind the ears. The Brain node turns them into 24 output pins (plus an image and a sound). The twelve old mood-style pins stay for old patches but are not trusted on a Muse. Settings: device, synthetic preset, blink filter, smoothing, hold time, artifact blanking, startle guard, play a baked take, and calibrate.");
}

// ================================================================ SLIDE 08: WHERE THE DATA COMES FROM
{
  const { s, m } = makeSlide("paper");

  s.addText("Where the data comes from", {
    isTextBox: true, margin: 0, x: M, y: 0.6, w: 10, h: 0.9,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.6 + (0.9 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  const IY = 1.68, IH = CW * 620 / 1500;
  s.addImage({ path: path.join(HERE, "s8_data.png"), x: M, y: IY, w: CW, h: IH, altText: "Built-in simulator with four presets on the left, four public EEG datasets on the right" });

  s.addNotes("Fake brains for testing, real brains for proof. On the left, our built-in SyntheticCortex simulator generates calm, focus, dream, and micro-gesture signals with no headset. On the right, four public EEG datasets give real recordings: PhysioNet (109 people, gel cap), GIPSA (20 people, gel cap), HEROIC (14 to 15 people on a real dry Muse 2), and EEG-EyeBlinks (46 people, OpenBCI).");
}

// ================================================================ SLIDE 09: EXPERIMENT 1: GEL CAP VS MUSE
{
  const { s, m } = makeSlide("paper");

  s.addText("Experiment 1: gel cap vs Muse", {
    isTextBox: true, margin: 0, x: M, y: 0.55, w: 10, h: 0.85,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.55 + (0.85 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  // Left: gel cap and Muse 2 photos
  const IY = 1.58, LW = 4.05, CH = 2.25, GAP = 0.2;
  [
    ["ref_gel_cap.jpg", "Gel lab cap", "16 to 64 wet sensors"],
    ["ref_muse2.jpg", "Muse 2 headband", "4 dry metal pads"],
  ].forEach(([imgName, label, sub], i) => {
    const cy = IY + i * (CH + GAP);
    s.addShape(pres.ShapeType.roundRect, { x: M, y: cy, w: LW, h: CH, rectRadius: 0.14, fill: { color: MODES.mist.ground }, line: { transparency: 100 } });
    s.addImage({ path: path.join(HERE, imgName), x: M + 0.12, y: cy + 0.12, w: LW - 0.24, h: 1.52, sizing: { type: "cover", w: LW - 0.24, h: 1.52 }, altText: label });
    s.addText(label, { isTextBox: true, margin: 0, x: M + 0.14, y: cy + 1.72, w: LW - 0.28, h: 0.24, fontFace: "Geist", fontSize: 14, bold: true, color: m.ink });
    s.addText(sub, { isTextBox: true, margin: 0, x: M + 0.14, y: cy + 1.96, w: LW - 0.28, h: 0.22, fontFace: "Geist", fontSize: 11, color: m.ink3 });
  });

  // Right: to-scale bar chart
  const RX = M + LW + 0.35, RW = CW - LW - 0.35, RH = 4.7;
  s.addImage({ path: path.join(HERE, "s9_chart.png"), x: RX, y: IY, w: RW, h: RH, altText: "Bar chart comparing eyes-closed alpha rise: gel cap 2.25x forehead and 3.59x behind ears vs Muse 2 ~1.0x forehead and 1.44x behind ears" });

  s.addText("On a dry Muse 2, the forehead barely moves when you close your eyes, and behind the ears is 1.44x (under half of a gel cap).", {
    isTextBox: true, margin: 0, x: M, y: IY + RH + 0.22, w: CW, h: 0.45,
    fontFace: "Geist", fontSize: 16, bold: true, color: m.signal, valign: "middle",
  });

  s.addNotes("Gel lab cap (wet sensors) vs Muse 2 (4 dry metal pads). When you close your eyes, alpha on a gel cap rises 2.25x on the forehead and 3.59x behind the ears (GIPSA, 20 people). On a real dry Muse 2 (HEROIC), the forehead barely moves (~1.0x) and behind the ears rises 1.44x.");
}

// ================================================================ SLIDE 10: EXPERIMENT 2: THE 72-SECOND CALIBRATION
{
  const { s, m } = makeSlide("paper");

  s.addText("Experiment 2: the 72-second calibration", {
    isTextBox: true, margin: 0, x: M, y: 0.55, w: 10.5, h: 0.85,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.55 + (0.85 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  const IY = 1.62, IH = CW * 560 / 1500;
  s.addImage({ path: path.join(HERE, "s10_calib.png"), x: M, y: IY, w: CW, h: IH, altText: "Left: 72-second sound check (30 s eyes open, 30 s eyes closed, 12 s blink 5 times). Right: accuracy bars showing fixed rule 68% same day and 50% later day vs calibrated 88.5% same day and 85.5% next day." });

  s.addText("Personal calibration makes a cheap dry headband actually work: 88.5% same day, 85.5% next day.", {
    isTextBox: true, margin: 0, x: M, y: IY + IH + 0.25, w: CW, h: 0.45,
    fontFace: "Geist", fontSize: 16, bold: true, color: m.signal, valign: "middle",
  });

  s.addNotes("Calibration is a 72-second sound check: 30 s eyes open, 30 s eyes closed, 12 s blinking 5 times. It fits you, not an average person. Tested on the public HEROIC Muse 2 dataset (64 sessions, 6 s smoothing), a fixed rule scores 68% same day and drops to a 50% coin flip on a later day. Personal calibration lifts same-day accuracy to 88.5% and holds 85.5% on a later day.");
}

// ================================================================ SLIDE 11: VISUAL RESULTS
{
  const { s, m } = makeSlide("paper");

  s.addText("Visual results: sound & picture", {
    isTextBox: true, margin: 0, x: M, y: 0.55, w: 10.6, h: 0.85,
    fontFace: "Geist", fontSize: 44, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.55 + (0.85 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  const IY = 1.58, IH = CW * 560 / 1500;
  s.addImage({ path: path.join(HERE, "s11_visual.png"), x: M, y: IY, w: CW, h: IH, altText: "Flowchart from calibrated alpha (0 to 1) and blink through Infinite cables into FieldPixel Bell and Gain/Reverb, with Calm (0.41 luma, -13.6 LUFS) vs Focus (0.13 luma, -24.2 LUFS) renders" });

  s.addText("The same calibrated 0 to 1 signal drives Infinite's shader and synth live: calm glows and swells, focus dims.", {
    isTextBox: true, margin: 0, x: M, y: IY + IH + 0.25, w: CW, h: 0.45,
    fontFace: "Geist", fontSize: 16, bold: true, color: m.signal, valign: "middle",
  });

  s.addNotes("How the final calibrated data becomes actual image and sound: the 72-second profile outputs a steady 0 to 1 alpha meter plus blink. Cables route it to FieldPixel (glowing bell shader) and Gain + Reverb (synth swell + 32x sped-up brain tone). At t = 5.00 s, Calm (high alpha) glows bright at 0.41 luma and -13.6 LUFS (369 Hz), while Focus (low alpha) dims to 0.13 luma and -24.2 LUFS.");
}

// ================================================================ SLIDE 12: HELP NEEDED & NEXT STEPS
{
  const { s, m } = makeSlide("paper");

  s.addText("Help needed: research, headsets & next steps", {
    isTextBox: true, margin: 0, x: M, y: 0.55, w: 10.6, h: 0.85,
    fontFace: "Geist", fontSize: 40, bold: true, color: m.ink, valign: "middle",
  });
  const lw = 1.35, lh = lw / 2.3273;
  s.addImage({ path: LOGO_MARK, x: W - M - lw, y: 0.55 + (0.85 - lh) / 2, w: lw, h: lh, altText: "Infinite mark" });

  const IY = 1.58, IH = CW * 560 / 1500;
  s.addImage({ path: path.join(HERE, "s12_questions.png"), x: M, y: IY, w: CW, h: IH, altText: "Four cards of questions for the meeting: Research & approaches, Universities/labs/funding, Headsets & cheap hardware, Signal/pilot/ethics" });

  s.addText("Next step: a 15-minute live pilot with 3 to 5 people once we align on headset, methods, and ethics.", {
    isTextBox: true, margin: 0, x: M, y: IY + IH + 0.25, w: CW, h: 0.45,
    fontFace: "Geist", fontSize: 16, bold: true, color: m.signal, valign: "middle",
  });

  s.addNotes("Open questions for the meeting: which papers and alternative dry-EEG methods to read; which university labs, researchers, or grants are tackling creative BCI; how to source headsets cheaply (is a ~$250 Muse 2 enough, OpenBCI $600-1200, or a lab loan); and how to handle speed vs smoothness (6 s vs 2 s), cross-day drift, and ethics/consent for a 3-to-5 person pilot.");
}

(async () => {
  await pres.writeFile({ fileName: OUT });
  await applyTheme(OUT, THEME);
  console.log("wrote", OUT);
})();
