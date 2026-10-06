// Turbo 0.51: GLSL for the "glitch" FilterDef, included only by FilterDefs.cpp.
// Split into several raw strings (joined by GlitchBody()) because MSVC rejects a
// single string literal over ~16 KB. Each piece stays well under that.
namespace glitchsrc
{
   // Piece 1: uniforms, the untouched pre-0.51 shader (legacy path), hashing and timing.
   const char* kPart1 = R"GLSL(
uniform int uKind;
uniform float uAmount;
uniform float uDetail;
uniform float uSpeed;
uniform float uSeed;
uniform float uSizeVar;
uniform int uSplits;
uniform float uContrast;
uniform float uDensity;
uniform float uStagger;
uniform float uBurst;
uniform float uDecay;
uniform int uSync;
uniform float uRgbVar;
uniform float uColorFx;
uniform int uAxis;
uniform int uClockMode;

float rand(vec2 co) { return fract(sin(dot(co, vec2(12.9898, 78.233)) + uSeed) * 43758.5453); }

// The original algorithms, byte for byte, so old patches look the same.
void legacy() {
   float t = uTime * uSpeed;
   vec2 uv = vUv;
   if (uKind == 0) {
      float blockY = floor(uv.y * max(1.0, uDetail * 40.0));
      uv.x += (rand(vec2(blockY, floor(t * 10.0))) - 0.5) * uAmount * 0.2;
      vec4 c = texture(uSrc, uv);
      vec4 cr = texture(uSrc, uv + vec2(uAmount * 0.01, 0.0));
      vec4 cb = texture(uSrc, uv - vec2(uAmount * 0.01, 0.0));
      fragColor = vec4(cr.r, c.g, cb.b, c.a);
      return;
   }
   if (uKind == 1) {
      vec2 dir = vec2(cos(uDetail * 6.2832), sin(uDetail * 6.2832)) * uAmount * 0.02;
      fragColor = vec4(texture(uSrc, uv + dir).r, texture(uSrc, uv).g,
                       texture(uSrc, uv - dir).b, texture(uSrc, uv).a);
      return;
   }
   if (uKind == 2) {
      vec4 c = texture(uSrc, uv);
      float line = sin((uv.y + t * 0.1) * max(10.0, uDetail * 800.0) * 3.14159);
      fragColor = vec4(c.rgb * (1.0 - uAmount * step(0.0, -line)), c.a);
      return;
   }
   if (uKind == 3) {
      vec2 grid = max(vec2(2.0), vec2(uDetail * 60.0));
      vec2 cell = floor(uv * grid);
      if (rand(cell + floor(t * 8.0)) > 1.0 - uAmount)
         uv += (vec2(rand(cell + 1.0), rand(cell + 2.0)) - 0.5) * 0.15;
      fragColor = texture(uSrc, clamp(uv, 0.0, 1.0));
      return;
   }
   if (uKind == 4) {
      uv.x += sin(uv.y * max(1.0, uDetail * 200.0) + t * 2.0) * uAmount * 0.05;
      fragColor = texture(uSrc, clamp(uv, 0.0, 1.0));
      return;
   }
   float slice = floor(uv.y * max(2.0, uDetail * 120.0));
   float r = rand(vec2(slice, floor(t * 4.0)));
   uv.x = fract(uv.x + (r - 0.5) * uAmount * 0.5);
   vec4 c = texture(uSrc, uv);
   if (r > 0.85) c.rgb = c.gbr;
   fragColor = c;
}

// ---- new engine: integer PCG hash (does not degrade over time like sin-hash) ----
uint pcg(uint v) {
   uint s = v * 747796405u + 2891336453u;
   uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
   return (w >> 22u) ^ w;
}
uint hash2(uint a, uint b) { return pcg(a + pcg(b)); }
uint hash3(uint a, uint b, uint c) { return pcg(a + pcg(b + pcg(c))); }
float h01(uint h) { return float(h >> 8u) * (1.0 / 16777216.0); }
float sgn(uint h) { return ((h & 1u) == 0u) ? 1.0 : -1.0; }
uint toU(float x) { return uint(max(x, 0.0)); }

vec2 gRes;
uint gSeed;
float gStepF;   // step counter as a float: free = seconds * speed * 8, synced = beats / period
float gTc;      // smooth time in units of 8 steps (seconds * speed when free)
uint gBlk;      // 4-step block index (layout re-rolls and bursts are decided per block)
uint gLayout;
float gBurstOn; // 1 while inside an active burst block
float gEnv;     // decay envelope across the block

float syncBeats() {
   if (uSync == 1) return 4.0;   // 1 bar, assuming 4/4
   if (uSync == 2) return 2.0;
   if (uSync == 3) return 1.0;
   if (uSync == 4) return 0.5;
   if (uSync == 5) return 0.25;
   return 0.125;
}

void setupTiming() {
   float clockT = (uClockMode == 1) ? uClock : uTime;
   gRes = 1.0 / uTexelSize;
   gSeed = toU(uSeed * 131.0) + 12345u;
   gStepF = (uSync == 0) ? clockT * uSpeed * 8.0 : uBeats / syncBeats();
   gTc = gStepF * 0.125;
   float blk = floor(gStepF * 0.25);
   gBlk = toU(blk);
   gLayout = hash2(gBlk, gSeed);
   gBurstOn = (uBurst <= 0.0 || h01(hash2(gBlk, gSeed + 77u)) < 1.0 - 0.85 * uBurst) ? 1.0 : 0.0;
   gEnv = 1.0 - uDecay * fract(gStepF * 0.25);
}
)GLSL";

   // Piece 2: weighted random splits (1D slices, 2D blocks) and per-slice state.
   const char* kPart2 = R"GLSL(
// Leaf [a,b] of a weighted random binary split of [0,1] that contains x.
vec2 bsp1(float x, uint seed, float minSize, out uint id) {
   float a = 0.0, b = 1.0;
   id = seed;
   for (int i = 0; i < 8; i++) {
      if (i >= uSplits || (b - a) < 2.0 * minSize) break;
      uint h = pcg(id + 0x9e3779b9u);
      float r1 = h01(h);
      float r2 = h01(pcg(h));
      if (uSizeVar > 0.0 && i > 0 && r2 < 0.3 * uSizeVar) break; // keep a big slab
      float w = mix(0.5, 0.12 + 0.76 * r1, uSizeVar);
      float m = a + (b - a) * w;
      if (x < m) { b = m; id = pcg(id * 2u + 1u); }
      else       { a = m; id = pcg(id * 2u + 2u); }
   }
   return vec2(a, b);
}

// 2D version: always splits the longer side in pixels, so leaves stay square-ish.
vec4 bsp2(vec2 p, uint seed, float minPx, out uint id) {
   vec4 r = vec4(0.0, 0.0, 1.0, 1.0);
   id = seed;
   int maxd = min(uSplits * 2, 16);
   for (int i = 0; i < 16; i++) {
      if (i >= maxd) break;
      vec2 szPx = (r.zw - r.xy) * gRes;
      bool sx = szPx.x >= szPx.y;
      float len = sx ? szPx.x : szPx.y;
      if (len < 2.0 * minPx) break;
      uint h = pcg(id + 0x9e3779b9u);
      float r1 = h01(h);
      float r2 = h01(pcg(h));
      if (uSizeVar > 0.0 && i > 1 && r2 < 0.2 * uSizeVar) break;
      float w = mix(0.5, 0.12 + 0.76 * r1, uSizeVar);
      if (sx) {
         float m = r.x + (r.z - r.x) * w;
         if (p.x < m) { r.z = m; id = pcg(id * 2u + 1u); } else { r.x = m; id = pcg(id * 2u + 2u); }
      } else {
         float m = r.y + (r.w - r.y) * w;
         if (p.y < m) { r.w = m; id = pcg(id * 2u + 1u); } else { r.y = m; id = pcg(id * 2u + 2u); }
      }
   }
   return r;
}

struct Slice { float off; float rgb; float fx; float on; float mag; };

// Per-slice state at the current step. Stagger gives each slice its own phase.
Slice sliceAt(uint id, uint salt, float amp) {
   float sf = gStepF + uStagger * h01(hash2(id, gSeed + 17u));
   uint st = toU(floor(sf));
   uint h = hash3(id, st, gSeed + salt);
   Slice s;
   s.on = (h01(pcg(h + 1u)) < uDensity ? 1.0 : 0.0) * gBurstOn;
   s.mag = pow(h01(pcg(h + 2u)), 1.0 + 3.0 * uContrast);
   s.off = s.on * sgn(pcg(h + 3u)) * s.mag * amp * gEnv;
   s.rgb = h01(pcg(h + 4u));
   s.fx = h01(pcg(h + 5u));
   return s;
}

Slice sliceField(float coord, uint salt, float minSize, float amp) {
   uint id;
   bsp1(coord, gLayout + salt, minSize, id);
   return sliceAt(id, salt, amp);
}

// Offset along each axis from 1D slices: Horizontal = rows move in x,
// Vertical = columns move in y, Both = both.
void fieldAxes(float minSize, float amp, out vec2 off, out float rgb, out float fx, out float on) {
   off = vec2(0.0); rgb = 0.5; fx = 1.0; on = 0.0;
   if (uAxis != 1) {
      Slice s = sliceField(vUv.y, 0u, minSize, amp);
      off.x = s.off; rgb = s.rgb; fx = s.fx; on = s.on;
   }
   if (uAxis != 0) {
      Slice s = sliceField(vUv.x, 1000u, minSize, amp);
      off.y = s.off;
      if (uAxis == 1) { rgb = s.rgb; fx = s.fx; on = s.on; } else on = max(on, s.on);
   }
}

// Per-slice colour trick, picked by hash: channel swap, invert or posterize.
vec3 colorFx(vec3 c, float fx, float on, float prob) {
   if (on < 0.5 || fx >= prob) return c;
   int k = int(floor(fx / max(prob, 1e-4) * 3.0));
   if (k <= 0) return c.gbr;
   if (k == 1) return 1.0 - c;
   return floor(c * 3.0 + 0.5) / 3.0;
}

vec4 splitRgb(vec2 uv, float rgb) {
   vec2 dir = (uAxis == 1) ? vec2(0.0, 1.0) : vec2(1.0, 0.0);
   float sp = uAmount * 0.01 * mix(1.0, 4.0 * rgb, uRgbVar);
   vec4 c = texture(uSrc, uv);
   vec4 cr = texture(uSrc, fract(uv + dir * sp));
   vec4 cb = texture(uSrc, fract(uv - dir * sp));
   return vec4(cr.r, c.g, cb.b, c.a);
}
)GLSL";

   // Piece 3: the three bsp-based kinds (0 Slice Shift, 3 Blocks, 5 Datamosh).
   const char* kPart3 = R"GLSL(
void kindSlice() {
   float minSize = 1.0 / max(1.0, uDetail * 40.0);
   vec2 off; float rgb, fx, on;
   fieldAxes(minSize, uAmount * 0.1, off, rgb, fx, on);
   vec4 c = splitRgb(fract(vUv + off), rgb);
   c.rgb = colorFx(c.rgb, fx, on, uColorFx);
   fragColor = c;
}

void kindBlocks() {
   float minPx = gRes.x / max(2.0, uDetail * 60.0);
   uint id;
   bsp2(vUv, gLayout + 2000u, minPx, id);
   float sf = gStepF + uStagger * h01(hash2(id, gSeed + 17u));
   uint h = hash3(id, toU(floor(sf)), gSeed + 2000u);
   float on = (h01(pcg(h + 1u)) < uDensity ? 1.0 : 0.0) * gBurstOn;
   float ex = 1.0 + 3.0 * uContrast;
   vec2 d = vec2(sgn(pcg(h + 2u)) * pow(h01(pcg(h + 3u)), ex),
                 sgn(pcg(h + 4u)) * pow(h01(pcg(h + 5u)), ex));
   if (uAxis == 0) d.y = 0.0;
   if (uAxis == 1) d.x = 0.0;
   vec2 off = d * on * 0.1 * uAmount * gEnv;
   vec4 c = splitRgb(fract(vUv + off), h01(pcg(h + 6u)));
   c.rgb = colorFx(c.rgb, h01(pcg(h + 7u)), on, uColorFx);
   fragColor = c;
}

// Datamosh-lite: variable slices, and the shifted slice is smeared back along its
// own displacement (a streak, as if the codec kept dragging old pixels).
void kindMosh() {
   float minSize = 1.0 / max(2.0, uDetail * 120.0);
   vec2 off; float rgb, fx, on;
   fieldAxes(minSize, uAmount * 0.25, off, rgb, fx, on);
   vec4 acc = vec4(0.0);
   float wsum = 0.0;
   for (int k = 0; k < 8; k++) {
      float f = float(k) / 7.0;
      float w = 0.4 + f * 1.6;
      acc += texture(uSrc, fract(vUv + off * f)) * w;
      wsum += w;
   }
   vec4 c = acc / wsum;
   c.rgb = colorFx(c.rgb, fx, on, max(0.15, uColorFx));
   fragColor = c;
}
)GLSL";

   // Piece 4: Scan Jitter, VHS.
   const char* kPart4 = R"GLSL(
// Exponential per-line offset plus occasional tear bands.
float scanOff(float coord, uint salt) {
   float lines = (salt == 0u) ? gRes.y : gRes.x;
   float lc = coord * lines;
   uint line = toU(floor(lc));
   float sf = gStepF + uStagger * h01(hash2(line, gSeed + 17u));
   uint h = hash3(line, toU(floor(sf)), gSeed + 6000u + salt);
   float on = (h01(pcg(h + 1u)) < uDensity * (0.15 + 0.85 * uDetail) ? 1.0 : 0.0) * gBurstOn;
   float e = -log(max(1.0 - h01(pcg(h + 2u)), 1e-4));
   e = pow(e, 1.0 + 2.0 * uContrast);
   float off = on * sgn(pcg(h + 3u)) * e * uAmount * 0.01;
   float bh = 3.0 + floor(10.0 * h01(hash2(gBlk, gSeed + 5u)));
   uint tid = toU(floor(lc / bh));
   uint ht = hash3(tid, toU(floor(gStepF)), gSeed + 7000u + salt);
   if (h01(ht) < (0.02 + 0.1 * uDetail) * uDensity * gBurstOn)
      off += sgn(pcg(ht + 1u)) * (0.05 + 0.25 * h01(pcg(ht + 2u))) * uAmount * 0.5;
   return off * gEnv;
}

void kindScanJitter() {
   vec2 off = vec2(0.0);
   if (uAxis != 1) off.x = scanOff(vUv.y, 0u);
   if (uAxis != 0) off.y = scanOff(vUv.x, 1u);
   fragColor = splitRgb(fract(vUv + off), 0.5);
}

vec3 toYiq(vec3 c) {
   return vec3(dot(c, vec3(0.299, 0.587, 0.114)), dot(c, vec3(0.596, -0.274, -0.322)),
               dot(c, vec3(0.211, -0.523, 0.312)));
}
vec3 fromYiq(vec3 q) {
   return vec3(q.x + 0.956 * q.y + 0.621 * q.z, q.x - 0.272 * q.y - 0.647 * q.z,
               q.x - 1.106 * q.y + 1.703 * q.z);
}

// Tracking band drifting up the frame, chroma bleed, head-switch noise at the
// bottom, per-line jitter and tape noise. Amount = damage, Detail = noise.
void kindVhs() {
   vec2 uv = vUv;
   float A = uAmount;
   uint line = toU(floor(uv.y * gRes.y * 0.5));
   uint st = toU(floor(gStepF * 3.0));
   uint hl = hash3(line, st, gSeed);
   float jit = (h01(hl) - 0.5) * 0.004 * A * (0.3 + uDetail);

   float bandC = fract(gTc * 0.12 + h01(gSeed));
   float bandH = 0.03 + 0.04 * A;
   float d = (fract(uv.y - bandC + 0.5) - 0.5) / bandH;
   float inBand = smoothstep(1.0, 0.0, abs(d)) * gBurstOn;
   float bandOff = inBand * 0.03 * A * (sin(uv.y * 400.0 + gTc * 30.0) * 0.5 + (h01(pcg(hl + 1u)) - 0.5));

   float hs = smoothstep(0.05, 0.0, uv.y);
   float hsOff = hs * 0.05 * A * (h01(pcg(hl + 2u)) - 0.3);

   uv.x = fract(uv.x + jit + bandOff + hsOff);

   float bleed = 0.0012 * (0.5 + A);
   vec3 base = toYiq(texture(uSrc, uv).rgb);
   vec2 iq = vec2(0.0);
   for (int k = 0; k < 8; k++)
      iq += toYiq(texture(uSrc, vec2(fract(uv.x - float(k) * bleed), uv.y)).rgb).yz;
   iq /= 8.0;
   vec3 col = fromYiq(vec3(base.x, mix(base.yz, iq, 0.85)));

   uint px = toU(floor(uv.x * gRes.x));
   uint py = toU(floor(uv.y * gRes.y));
   float grain = h01(hash3(px, py, st + gSeed)) - 0.5;
   col += grain * (0.06 + 0.2 * uDetail) * (0.4 + A * 0.6);
   col += grain * 0.5 * (inBand + hs);
   if (h01(hash2(line, st + 31u)) < 0.004 * uDetail * (0.5 + A)) {
      float x0 = h01(hash2(line, st + 32u));
      float len = 0.05 + 0.3 * h01(hash2(line, st + 33u));
      if (fract(uv.x - x0) < len) col = mix(col, vec3(1.0), 0.7);
   }
   fragColor = vec4(col, texture(uSrc, uv).a);
}
)GLSL";

   // Piece 5: Compression, Pixel Sort lite, main().
   const char* kPart5 = R"GLSL(
// Macroblock codec damage: chroma subsampling, per-block posterize, stale holds.
// Detail below 0.5 uses 8 px blocks, above it 16 px.
void kindCompression() {
   float bs = (uDetail < 0.5) ? 8.0 : 16.0;
   vec2 px = vUv * gRes;
   vec2 bi = floor(px / bs);
   uint bid = hash2(toU(bi.x), toU(bi.y));
   float sf = gStepF + uStagger * h01(hash2(bid, gSeed + 17u));
   uint hb = hash3(bid, toU(floor(sf * 0.25)), gSeed + 9000u);
   float hold = (h01(hb) < uDensity * (0.04 + 0.3 * uAmount) ? 1.0 : 0.0) * gBurstOn;
   vec3 col;
   if (hold > 0.5) {
      // stale block: DC colour only, taken from a displaced block
      vec2 shift = vec2(float(int(pcg(hb + 1u) % 5u) - 2), float(int(pcg(hb + 2u) % 5u) - 2));
      col = texture(uSrc, fract((bi + 0.5 + shift) * bs / gRes)).rgb;
   } else {
      float cb = mix(1.0, bs * 0.5, clamp(uAmount, 0.0, 1.0));
      vec2 cpx = (floor(px / cb) + 0.5) * cb;
      vec3 y = toYiq(texture(uSrc, vUv).rgb);
      vec3 c = toYiq(texture(uSrc, cpx / gRes).rgb);
      col = fromYiq(vec3(y.x, c.yz));
      float r = h01(pcg(hb + 3u));
      if (r < 0.3 + 0.7 * uColorFx) {
         float lv = mix(64.0, 3.0, clamp(uAmount * h01(pcg(hb + 4u)), 0.0, 1.0));
         col = floor(col * lv + 0.5) / lv;
      }
      if (uContrast > 0.0) {
         vec3 avg = texture(uSrc, (floor(px / bs) + 0.5) * bs / gRes).rgb;
         col = mix(col, avg, uContrast * h01(pcg(hb + 5u)));
      }
   }
   fragColor = vec4(col, 1.0);
}

float lumaAt(vec2 uv) { return dot(texture(uSrc, uv).rgb, vec3(0.299, 0.587, 0.114)); }

// Single-pass approximation of a pixel sort: find the bright span around this
// pixel, sample 16 points across it, and output the sample whose luma rank
// matches this pixel's position in the span.
void kindSort() {
   vec2 dir = vec2(1.0, 0.0);
   float perp = vUv.y * gRes.y;
   if (uAxis == 1) { dir = vec2(0.0, 1.0); perp = vUv.x * gRes.x; }
   if (uAxis == 2) {
      uint tile = hash2(toU(floor(vUv.x * gRes.x / 64.0)), toU(floor(vUv.y * gRes.y / 64.0)) + gSeed);
      if ((tile & 1u) == 1u) { dir = vec2(0.0, 1.0); perp = vUv.x * gRes.x; }
   }
   uint line = toU(floor(perp));
   float sf = gStepF + uStagger * h01(hash2(line, gSeed + 17u));
   uint h = hash3(line, toU(floor(sf)), gSeed + 8000u);
   vec4 here = texture(uSrc, vUv);
   float on = (h01(h) < uDensity ? 1.0 : 0.0) * gBurstOn;
   float thr = clamp(mix(0.9, 0.1, uDetail) + (h01(pcg(h + 1u)) - 0.5) * 0.3 * (0.2 + uContrast), 0.02, 0.98);
   float L0 = lumaAt(vUv);
   float maxLen = clamp(uAmount * 0.5, 0.01, 1.0);
   float stride = maxLen / 16.0;
   if (on < 0.5 || L0 < thr) { fragColor = here; return; }
   int s0 = 0, s1 = 0;
   for (int k = 1; k <= 16; k++) {
      if (lumaAt(fract(vUv - dir * stride * float(k))) < thr) break;
      s0 = k;
   }
   for (int k = 1; k <= 16; k++) {
      if (lumaAt(fract(vUv + dir * stride * float(k))) < thr) break;
      s1 = k;
   }
   int n = s0 + s1 + 1;
   if (n < 3) { fragColor = here; return; }
   int M = min(n, 16);
   vec3 cols[16];
   float ls[16];
   for (int j = 0; j < 16; j++) {
      if (j >= M) break;
      float pos = -float(s0) - 0.5 + (float(j) + 0.5) * float(n) / float(M);
      vec2 suv = fract(vUv + dir * stride * pos);
      cols[j] = texture(uSrc, suv).rgb;
      ls[j] = dot(cols[j], vec3(0.299, 0.587, 0.114));
   }
   int target = clamp(int(floor((float(s0) + 0.5) / float(n) * float(M))), 0, M - 1);
   vec3 pick = cols[0];
   for (int j = 0; j < 16; j++) {
      if (j >= M) break;
      int rank = 0;
      for (int i = 0; i < 16; i++) {
         if (i >= M) break;
         if (ls[i] < ls[j] || (ls[i] == ls[j] && i < j)) rank++;
      }
      if (rank == target) pick = cols[j];
   }
   fragColor = vec4(pick, here.a);
}

void main() {
   bool classic = (uKind == 1 || uKind == 2 || uKind == 4);
   // The old path stays when none of the new controls is touched (older patches).
   bool plain = uSizeVar == 0.0 && uSync == 0 && uStagger == 0.0 && uBurst == 0.0 &&
                uDecay == 0.0 && uContrast == 0.0 && uDensity == 1.0 && uRgbVar == 0.0 &&
                uColorFx == 0.0 && uAxis == 0 && uClockMode == 0;
   if (classic || (plain && (uKind == 0 || uKind == 3 || uKind == 5))) { legacy(); return; }
   setupTiming();
   if (uKind == 0) kindSlice();
   else if (uKind == 3) kindBlocks();
   else if (uKind == 5) kindMosh();
   else if (uKind == 6) kindScanJitter();
   else if (uKind == 7) kindVhs();
   else if (uKind == 8) kindCompression();
   else kindSort();
}
)GLSL";

   std::string Body()
   {
      return std::string(kPart1) + kPart2 + kPart3 + kPart4 + kPart5;
   }
}
