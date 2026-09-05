#include "FieldPixelNode.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "Transport.h"
#include "gl3.h"
#include <cmath>

namespace
{
   double EvalPrologueNode(const Field::IRNodePtr& node, double t, double dt, int frame,
                           const Field::ParamTable& params,
                           const std::unordered_map<std::string, double>& env)
   {
      if (!node) return 0.0;
      switch (node->kind)
      {
         case Field::IRKind::Literal:
            return node->numberValue;
         case Field::IRKind::Variable:
         {
            if (node->varName == "t") return t;
            if (node->varName == "dt") return dt;
            if (node->varName == "frame") return (double)frame;
            for (const auto& p : params.Params())
            {
               if (p.isDeclared && p.name == node->varName)
                  return (double)p.value;
            }
            auto it = env.find(node->varName);
            if (it != env.end()) return it->second;
            return 0.0;
         }
         case Field::IRKind::Unary:
         {
            double a = EvalPrologueNode(node->children[0], t, dt, frame, params, env);
            if (node->op == "-") return -a;
            if (node->op == "!") return a == 0.0 ? 1.0 : 0.0;
            return a;
         }
         case Field::IRKind::Binary:
         {
            double a = EvalPrologueNode(node->children[0], t, dt, frame, params, env);
            double b = EvalPrologueNode(node->children[1], t, dt, frame, params, env);
            if (node->op == "+") return a + b;
            if (node->op == "-") return a - b;
            if (node->op == "*") return a * b;
            if (node->op == "/") return b != 0.0 ? a / b : 0.0;
            if (node->op == "%") return b != 0.0 ? std::fmod(a, b) : 0.0;
            if (node->op == "^") return std::pow(a, b);
            if (node->op == "<") return a < b ? 1.0 : 0.0;
            if (node->op == "<=") return a <= b ? 1.0 : 0.0;
            if (node->op == ">") return a > b ? 1.0 : 0.0;
            if (node->op == ">=") return a >= b ? 1.0 : 0.0;
            if (node->op == "==") return a == b ? 1.0 : 0.0;
            if (node->op == "!=") return a != b ? 1.0 : 0.0;
            if (node->op == "&&") return (a != 0.0 && b != 0.0) ? 1.0 : 0.0;
            if (node->op == "||") return (a != 0.0 || b != 0.0) ? 1.0 : 0.0;
            return 0.0;
         }
         case Field::IRKind::Call:
         {
            std::vector<double> args;
            for (const auto& c : node->children)
               args.push_back(EvalPrologueNode(c, t, dt, frame, params, env));
            const std::string& fn = node->callee;
            if (fn == "sin" && !args.empty()) return std::sin(args[0]);
            if (fn == "cos" && !args.empty()) return std::cos(args[0]);
            if (fn == "tan" && !args.empty()) return std::tan(args[0]);
            if (fn == "abs" && !args.empty()) return std::abs(args[0]);
            if (fn == "floor" && !args.empty()) return std::floor(args[0]);
            if (fn == "ceil" && !args.empty()) return std::ceil(args[0]);
            if (fn == "round" && !args.empty()) return std::floor(args[0] + 0.5);
            if (fn == "sign" && !args.empty()) return args[0] > 0.0 ? 1.0 : (args[0] < 0.0 ? -1.0 : 0.0);
            if (fn == "sqrt" && !args.empty()) return args[0] >= 0.0 ? std::sqrt(args[0]) : 0.0;
            if (fn == "exp" && !args.empty()) return std::exp(args[0]);
            if (fn == "log" && !args.empty()) return args[0] > 0.0 ? std::log(args[0]) : 0.0;
            if (fn == "min" && args.size() >= 2) return std::min(args[0], args[1]);
            if (fn == "max" && args.size() >= 2) return std::max(args[0], args[1]);
            if (fn == "mod" && args.size() >= 2) return args[1] != 0.0 ? std::fmod(args[0], args[1]) : 0.0;
            if (fn == "pow" && args.size() >= 2) return std::pow(args[0], args[1]);
            if (fn == "clamp" && args.size() >= 3) return std::min(std::max(args[0], args[1]), args[2]);
            if (fn == "lerp" && args.size() >= 3) return args[0] + (args[1] - args[0]) * args[2];
            if (fn == "mix" && args.size() >= 3) return args[0] + (args[1] - args[0]) * args[2];
            if (fn == "if" && args.size() >= 3) return args[0] != 0.0 ? args[1] : args[2];
            return 0.0;
         }
         default:
            return 0.0;
      }
   }
}

const std::vector<FieldPixelNode::Preset>& FieldPixelNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Default (UV Gradient)", "col = vec3(uv.x, uv.y, 0.5);" },
      { "Color Gradient",
        "param float angle = 45.0 [0.0, 360.0];\n"
        "param float speed = 0.5 [0.0, 5.0];\n"
        "rad = angle * 0.01745329;\n"
        "dir = vec2(cos(rad), sin(rad));\n"
        "d = dot(uv - 0.5, dir) + 0.5 + sin(t * speed) * 0.1;\n"
        "col = mix(vec3(0.1, 0.2, 0.8), vec3(1.0, 0.4, 0.2), clamp(d, 0.0, 1.0));" },
      { "Noise Texture",
        "param float scale = 12.0 [2.0, 40.0];\n"
        "param float speed = 1.0 [0.0, 4.0];\n"
        // Aspect-corrected so the noise cells stay isotropic instead of
        // stretching on a non-square width/height.
        "p = vec2(uv.x * aspect, uv.y) * scale;\n"
        "n1 = sin(p.x + t * speed) * cos(p.y - t * speed * 0.7);\n"
        "n2 = sin(p.x * 2.1 - t * speed * 1.2) * cos(p.y * 1.9 + t * speed);\n"
        "val = 0.5 + 0.25 * (n1 + n2);\n"
        "col = vec3(val * 0.9, val * 0.7, val * 1.1);" },
      { "Organic Blobs / Metaballs",
        "param float speed = 1.2 [0.1, 5.0];\n"
        "param float size = 0.16 [0.05, 0.3];\n"
        // Aspect-corrected so the blobs stay round instead of stretching
        // into ellipses on a non-square width/height.
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "b1 = vec2(0.28 * cos(t * speed), 0.28 * sin(t * speed * 0.8));\n"
        "b2 = vec2(0.24 * cos(t * speed * 1.3 + 2.0), 0.24 * sin(t * speed * 1.1 + 1.0));\n"
        "b3 = vec2(0.26 * sin(t * speed * 0.7 + 4.0), 0.26 * cos(t * speed * 0.9 + 3.0));\n"
        // Classic inverse-square metaball energy field: each blob
        // contributes r^2/(dist^2+eps), and the iso-surface sits at fld==1,
        // which is exactly where two blobs' influence circles overlap and
        // merge - a 1/dist falloff (the old formula) drops off too fast to
        // ever blend two blobs into one shape, so it always looked like
        // separate plain circles instead of blobby metaballs.
        "r2 = size * size;\n"
        "d1 = dot(p - b1, p - b1) + 0.0008;\n"
        "d2 = dot(p - b2, p - b2) + 0.0008;\n"
        "d3 = dot(p - b3, p - b3) + 0.0008;\n"
        "fld = r2 / d1 + r2 / d2 + r2 / d3;\n"
        "iso = smoothstep(0.85, 1.15, fld);\n"
        "col = vec3(iso * 0.2, iso * 0.8, iso * 0.9);" },
      { "Radial Rings / Waves",
        "param float freq = 24.0 [2.0, 60.0];\n"
        "param float speed = 3.0 [0.1, 10.0];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "d = length(p);\n"
        "wave = sin(d * freq - t * speed);\n"
        "col = vec3(0.5 + 0.5 * wave, 0.4 + 0.3 * cos(d * freq * 0.5), 0.8 + 0.2 * wave);" },
      { "Modulo Grid",
        "param float scale = 8.0 [1.0, 32.0];\n"
        // Aspect-corrected so grid cells stay square instead of stretching
        // on a non-square width/height.
        "p = vec2(uv.x * aspect, uv.y) * scale;\n"
        "grid = fmod(p, 1.0);\n"
        "col = vec3(grid.x, grid.y, 0.0);" },
      { "Chladni Nodal Pattern",
        "param float m = 3.0 [1.0, 10.0];\n"
        "param float n = 5.0 [1.0, 10.0];\n"
        // Aspect-corrected so the nodal pattern stays isotropic instead of
        // stretching on a non-square width/height.
        "p = vec2(uv.x * aspect, uv.y) * 3.14159265;\n"
        "val = cos(n * p.x) * cos(m * p.y) - cos(m * p.x) * cos(n * p.y);\n"
        "line = 1.0 - smoothstep(0.0, 0.08, abs(val));\n"
        "col = vec3(line, line * 0.85, line * 0.6);" },
      { "Sound-Colored Field",
        "param float speed = 1.5 [0.1, 6.0];\n"
        "param float saturation = 0.8 [0.0, 1.0];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "d = length(p);\n"
        "angle = atan2(p.y, p.x);\n"
        "hue = fract(angle / 6.283185 + t * 0.1 * speed);\n"
        "r = clamp(abs(fract(hue + 1.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
        "g = clamp(abs(fract(hue + 0.6666) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
        "b = clamp(abs(fract(hue + 0.3333) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
        "rgb = mix(vec3(1.0), vec3(r, g, b), saturation);\n"
        "col = rgb * (0.6 + 0.4 * sin(d * 20.0 - t * speed));" },
      { "Kinetic Moire Ripples",
        "param float freq = 18.0 [2.0, 60.0];\n"
        "param float speed = 2.5 [0.1, 10.0];\n"
        "param float rings = 8.0 [1.0, 20.0];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "c1 = vec2(-0.15, 0.0);\n"
        "c2 = vec2(0.15, 0.0);\n"
        "p1 = length(p - c1);\n"
        "p2 = length(p - c2);\n"
        "w1 = sin(p1 * freq * rings - t * speed);\n"
        "w2 = sin(p2 * freq * rings + t * speed);\n"
        "val = 0.5 + 0.25 * (w1 + w2);\n"
        "col = vec3(val, val * 0.7, 1.0 - val);" },
      // --- Build step 22 (OPEN-C): kernels that read their neighbours. ---
      // These are the first presets that could not have been written before
      // an offset read existed, because their whole behaviour is a pixel
      // asking what the pixels beside it are doing.
      { "Reaction Diffusion (Gray-Scott)",
        "param float feed = 0.055 [0.01, 0.09];\n"
        "param float kill = 0.062 [0.03, 0.075];\n"
        "param float dA = 1.0 [0.0, 1.0];\n"
        "param float dB = 0.5 [0.0, 1.0];\n"
        "param float seed = 0.06 [0.0, 0.5];\n"
        // [wrap] makes the pattern tile seamlessly across the edges; swap it
        // for [clamp] and it piles up against the border instead. Two
        // different pictures, which is why the mode is per-cell.
        "state float A = 1 [wrap];\n"
        "state float B = 0 [wrap];\n"
        "d = 1.0 / res;\n"
        "lapA = A(uv + vec2(d.x, 0.0)) + A(uv - vec2(d.x, 0.0)) + A(uv + vec2(0.0, d.y)) + A(uv - vec2(0.0, d.y)) - 4.0 * A;\n"
        "lapB = B(uv + vec2(d.x, 0.0)) + B(uv - vec2(d.x, 0.0)) + B(uv + vec2(0.0, d.y)) + B(uv - vec2(0.0, d.y)) - 4.0 * B;\n"
        "r = A * B * B;\n"
        // B has to come from somewhere: with B = 0 everywhere the system sits
        // on a fixed point and never starts. `age` is 0 only on the first cook
        // after a clear or a transport reset, so this is a one-shot seed - a
        // scatter of B that the reaction then grows into coral. Turn the
        // transport back to the start and it re-seeds and grows again.
        "h = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);\n"
        "first = 1.0 - step(0.5, age);\n"
        "inject = first * step(1.0 - seed, h);\n"
        // An explicit diffusion step is only stable while dA * step * 8 <= 2,
        // so the textbook listing's step of 1.0 at dA = 1.0 sits exactly on
        // the boundary and rings into a checkerboard instead of settling into
        // a pattern. 0.2 is comfortably inside the bound and only changes how
        // fast the pattern grows, not which pattern it is.
        "step = 0.2;\n"
        "A = clamp(A + step * (dA * lapA - r + feed * (1.0 - A)), 0.0, 1.0);\n"
        "B = clamp(B + step * (dB * lapB + r - (kill + feed) * B) + inject, 0.0, 1.0);\n"
        "col = vec3(B * 1.7, B * 0.9 + 0.04, 1.0 - B * 1.3);" },
      { "Advected Smoke / Vortex",
        "param float speed = 0.5 [0.0, 3.0];\n"
        "param float swirl = 2.5 [0.0, 10.0];\n"
        "param float decay = 0.99 [0.9, 1.0];\n"
        "param float amount = 0.6 [0.0, 1.0];\n"
        "state float D = 0 [clamp];\n"
        "q = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        // A curl-shaped flow field: the velocity is perpendicular to the
        // radius, twisted by distance, so the density winds into vortices.
        "ang = atan2(q.y, q.x) + swirl * length(q) + 1.5707963;\n"
        "flow = vec2(cos(ang), sin(ang)) * speed * 0.004;\n"
        // Reading the cell UPSTREAM of the flow is the advection step - the
        // one read a current-pixel-only state cell cannot express.
        "e = vec2(0.22 * cos(t * 0.7), 0.22 * sin(t * 0.9));\n"
        "emit = 1.0 - smoothstep(0.0, 0.05, length(q - e));\n"
        "D = clamp(D(uv - flow) * decay + emit * amount, 0.0, 1.0);\n"
        "col = vec3(D * 1.3, D * 0.55 + 0.02, 0.9 - D * 0.7);" },
      { "Liquid Marble (Domain Warp)",
        "param float speed = 0.8 [0.1, 4.0];\n"
        "param float scale = 4.5 [1.0, 15.0];\n"
        "param float warp = 1.6 [0.2, 4.0];\n"
        "p = vec2(uv.x * aspect, uv.y) * scale;\n"
        "q = vec2(sin(p.x + t * speed), cos(p.y + t * speed * 0.8));\n"
        "r = vec2(sin(p.y + q.x * warp + t * speed * 0.6), cos(p.x + q.y * warp - t * speed * 0.7));\n"
        "f = sin(p.x + r.x * warp) * cos(p.y + r.y * warp);\n"
        "v = 0.5 + 0.5 * f;\n"
        "col = vec3(0.5 + 0.5 * cos(6.283185 * (v + 0.0)), 0.5 + 0.5 * cos(6.283185 * (v + 0.33)), 0.5 + 0.5 * cos(6.283185 * (v + 0.67)));" },
      { "Quasicrystal Kaleidoscope",
        "param float scale = 20.0 [4.0, 60.0];\n"
        "param float speed = 1.0 [0.1, 5.0];\n"
        "param float contrast = 2.5 [1.0, 6.0];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5) * scale;\n"
        "w1 = cos(p.x + t * speed);\n"
        "w2 = cos(p.x * 0.8090 + p.y * 0.5878 - t * speed * 0.9);\n"
        "w3 = cos(p.x * 0.3090 + p.y * 0.9511 + t * speed * 0.8);\n"
        "w4 = cos(-p.x * 0.3090 + p.y * 0.9511 - t * speed * 0.7);\n"
        "w5 = cos(-p.x * 0.8090 + p.y * 0.5878 + t * speed * 0.6);\n"
        "v = (w1 + w2 + w3 + w4 + w5) / 5.0;\n"
        "bands = 0.5 + 0.5 * sin(v * 3.14159 * contrast);\n"
        "col = vec3(bands * 0.9, bands * 0.5 + 0.3 * sin(t + v * 4.0), 1.0 - bands * 0.6);" },
      { "Voronoi Cells",
        "param float scale = 6.0 [1.0, 24.0];\n"
        "param float jitter = 0.9 [0.0, 1.0];\n"
        "param float speed = 0.6 [0.0, 3.0];\n"
        "p = vec2(uv.x * aspect, uv.y) * scale;\n"
        "g = floor(p - 0.5);\n"
        "q1 = g;\n"
        "h1 = fract(sin(dot(q1, vec2(127.1, 311.7))) * 43758.5453);\n"
        "f1 = q1 + vec2(0.5 + jitter * 0.45 * sin(t * speed + 6.2832 * h1), 0.5 + jitter * 0.45 * cos(t * speed * 1.31 + 6.2832 * fract(h1 * 17.13)));\n"
        "d1 = length(f1 - p);\n"
        "q2 = g + vec2(1.0, 0.0);\n"
        "h2 = fract(sin(dot(q2, vec2(127.1, 311.7))) * 43758.5453);\n"
        "f2 = q2 + vec2(0.5 + jitter * 0.45 * sin(t * speed + 6.2832 * h2), 0.5 + jitter * 0.45 * cos(t * speed * 1.31 + 6.2832 * fract(h2 * 17.13)));\n"
        "d2 = length(f2 - p);\n"
        "q3 = g + vec2(0.0, 1.0);\n"
        "h3 = fract(sin(dot(q3, vec2(127.1, 311.7))) * 43758.5453);\n"
        "f3 = q3 + vec2(0.5 + jitter * 0.45 * sin(t * speed + 6.2832 * h3), 0.5 + jitter * 0.45 * cos(t * speed * 1.31 + 6.2832 * fract(h3 * 17.13)));\n"
        "d3 = length(f3 - p);\n"
        "q4 = g + vec2(1.0, 1.0);\n"
        "h4 = fract(sin(dot(q4, vec2(127.1, 311.7))) * 43758.5453);\n"
        "f4 = q4 + vec2(0.5 + jitter * 0.45 * sin(t * speed + 6.2832 * h4), 0.5 + jitter * 0.45 * cos(t * speed * 1.31 + 6.2832 * fract(h4 * 17.13)));\n"
        "d4 = length(f4 - p);\n"
        "best = if(d2 < d1, h2, h1);\n"
        "bd = min(d1, d2);\n"
        "best = if(d3 < bd, h3, best);\n"
        "bd = min(bd, d3);\n"
        "best = if(d4 < bd, h4, best);\n"
        "bd = min(bd, d4);\n"
        "core = 1.0 - smoothstep(0.0, 0.55, bd);\n"
        "cell = vec3(0.5 + 0.5 * sin(6.2832 * best), 0.5 + 0.5 * sin(6.2832 * best + 2.094), 0.5 + 0.5 * sin(6.2832 * best + 4.188));\n"
        "col = cell * (0.28 + 0.72 * core);" },
      { "Halftone Dither",
        "param float cellSize = 24.0 [6.0, 60.0];\n"
        "param float contrast = 1.4 [0.5, 3.0];\n"
        "p = vec2(uv.x * aspect, uv.y) * cellSize;\n"
        "cell = floor(p);\n"
        "f = fract(p) - 0.5;\n"
        "lum = (col.r * 0.299 + col.g * 0.587 + col.b * 0.114) * contrast;\n"
        "jitter = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453) * 0.15;\n"
        "radius = sqrt(clamp(lum + jitter, 0.0, 1.0)) * 0.5;\n"
        "edge = length(f) - radius;\n"
        "dotMask = 1.0 - smoothstep(0.0, 0.03, edge);\n"
        "col = vec3(dotMask);" },
      { "Hashed Flow-Field Streaklines",
        "param float cellScale = 6.0 [2.0, 16.0];\n"
        "param float density = 40.0 [10.0, 100.0];\n"
        "param float speed = 1.2 [0.0, 4.0];\n"
        "param float drift = 0.05 [0.0, 0.3];\n"
        "p = vec2(uv.x * aspect, uv.y);\n"
        "cell = floor(p * cellScale);\n"
        "hash = fract(sin(dot(cell, vec2(91.7, 57.3))) * 43758.5453);\n"
        "ang = hash * 6.283185 + t * drift;\n"
        "flowDir = vec2(cos(ang), sin(ang));\n"
        "along = dot(p, flowDir);\n"
        "streak = 0.5 + 0.5 * sin(along * density - t * speed);\n"
        "col = vec3(streak * 0.85, streak * 0.55 + 0.1, 1.0 - streak * 0.6);" },
      { "Eye",
        "param float pupilSize = 0.10 [0.05, 0.18];\n"
        "param float irisSize = 0.24 [0.15, 0.32];\n"
        "param float lidClose = 0.0 [0.0, 1.0];\n"
        "param float irisFiberFreq = 24.0 [8.0, 48.0];\n"
        "param float highlightSize = 0.035 [0.01, 0.07];\n"
        "param float lashLength = 0.12 [0.05, 0.2];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "angle = atan2(p.y, p.x);\n"
        "rad = length(p);\n"
        "col = vec3(0.85, 0.68, 0.58);\n"
        "eyeW = 0.34;\n"
        "eyeH = 0.20;\n"
        "norm = clamp(p.x / eyeW, -1.0, 1.0);\n"
        "archTop = eyeH * (1.0 - norm * norm);\n"
        "archBot = eyeH * 0.8 * (1.0 - norm * norm);\n"
        "topLid = archTop * (1.0 - 0.92 * lidClose);\n"
        "botLid = archBot * (1.0 - 0.15 * lidClose);\n"
        "eyeOpen = smoothstep(-botLid - 0.01, -botLid + 0.01, p.y) * (1.0 - smoothstep(topLid - 0.01, topLid + 0.01, p.y));\n"
        "col = mix(col, vec3(0.95, 0.94, 0.9), eyeOpen);\n"
        "irisMask = (1.0 - smoothstep(irisSize, irisSize + 0.008, rad)) * eyeOpen;\n"
        "fiber = 0.5 + 0.5 * sin(angle * irisFiberFreq + rad * 10.0);\n"
        "irisBase = mix(vec3(0.25, 0.5, 0.35), vec3(0.15, 0.35, 0.25), fiber);\n"
        "irisShade = smoothstep(0.0, irisSize, rad);\n"
        "irisColor = mix(vec3(0.05, 0.15, 0.1), irisBase, irisShade);\n"
        "col = mix(col, irisColor, irisMask);\n"
        "pupilMask = (1.0 - smoothstep(pupilSize, pupilSize + 0.006, rad)) * eyeOpen;\n"
        "col = mix(col, vec3(0.02, 0.02, 0.03), pupilMask);\n"
        "specD = length(p - vec2(-pupilSize * 0.5, pupilSize * 0.5));\n"
        "specMask = (1.0 - smoothstep(0.0, highlightSize, specD)) * eyeOpen;\n"
        "col = mix(col, vec3(1.0), specMask);\n"
        "spec2D = length(p - vec2(irisSize * 0.3, -irisSize * 0.25));\n"
        "spec2Mask = (1.0 - smoothstep(0.0, highlightSize * 0.4, spec2D)) * irisMask;\n"
        "col = mix(col, vec3(1.0), spec2Mask * 0.6);\n"
        "topCrease = 1.0 - smoothstep(0.0, 0.01, abs(p.y - topLid));\n"
        "botCrease = 1.0 - smoothstep(0.0, 0.01, abs(p.y + botLid));\n"
        "col = mix(col, vec3(0.3, 0.18, 0.15), topCrease * 0.6);\n"
        "col = mix(col, vec3(0.3, 0.18, 0.15), botCrease * 0.4);\n"
        "lashRoot1 = vec2(0.30, 0.03);\n"
        "lashDir1 = vec2(cos(0.35), sin(0.35));\n"
        "lp1 = p - lashRoot1;\n"
        "along1 = lp1.x * lashDir1.x + lp1.y * lashDir1.y;\n"
        "perp1 = -lp1.x * lashDir1.y + lp1.y * lashDir1.x;\n"
        "lash1 = (1.0 - smoothstep(0.0, 0.004, abs(perp1))) * smoothstep(0.0, 0.01, along1) * (1.0 - smoothstep(lashLength - 0.01, lashLength, along1));\n"
        "col = mix(col, vec3(0.08, 0.05, 0.05), lash1);\n"
        "lashRoot2 = vec2(0.20, 0.08);\n"
        "lashDir2 = vec2(cos(0.55), sin(0.55));\n"
        "lp2 = p - lashRoot2;\n"
        "along2 = lp2.x * lashDir2.x + lp2.y * lashDir2.y;\n"
        "perp2 = -lp2.x * lashDir2.y + lp2.y * lashDir2.x;\n"
        "lash2 = (1.0 - smoothstep(0.0, 0.004, abs(perp2))) * smoothstep(0.0, 0.01, along2) * (1.0 - smoothstep(lashLength - 0.01, lashLength, along2));\n"
        "col = mix(col, vec3(0.08, 0.05, 0.05), lash2);\n"
        "lashRoot3 = vec2(0.08, 0.10);\n"
        "lashDir3 = vec2(cos(0.75), sin(0.75));\n"
        "lp3 = p - lashRoot3;\n"
        "along3 = lp3.x * lashDir3.x + lp3.y * lashDir3.y;\n"
        "perp3 = -lp3.x * lashDir3.y + lp3.y * lashDir3.x;\n"
        "lash3 = (1.0 - smoothstep(0.0, 0.004, abs(perp3))) * smoothstep(0.0, 0.01, along3) * (1.0 - smoothstep(lashLength - 0.01, lashLength, along3));\n"
        "col = mix(col, vec3(0.08, 0.05, 0.05), lash3);" },
      { "Sunset / Landscape",
        "param float horizonY = 0.42 [0.25, 0.55];\n"
        "param float sunY = 0.14 [-0.05, 0.35];\n"
        "param float sunSize = 0.07 [0.03, 0.15];\n"
        "param float ridgeFreq = 3.0 [1.0, 8.0];\n"
        "param float hazeAmount = 0.5 [0.0, 1.0];\n"
        "param float reflectAmount = 0.35 [0.0, 1.0];\n"
        "param float fogDensity = 0.5 [0.0, 1.0];\n"
        "skyGrad = smoothstep(horizonY, 1.0, uv.y);\n"
        "skyColor = mix(vec3(1.0, 0.7, 0.45), vec3(0.05, 0.08, 0.25), skyGrad);\n"
        "col = skyColor;\n"
        "sunPos = vec2(0.5, horizonY + sunY);\n"
        "sunD = vec2((uv.x - sunPos.x) * aspect, uv.y - sunPos.y);\n"
        "sunDist = length(sunD);\n"
        "sunGlowMask = 1.0 - smoothstep(sunSize, sunSize * 3.0, sunDist);\n"
        "sunDiskMask = 1.0 - smoothstep(sunSize, sunSize + 0.01, sunDist);\n"
        "col = mix(col, vec3(1.0, 0.85, 0.6), sunGlowMask * 0.5);\n"
        "col = mix(col, vec3(1.0, 0.95, 0.8), sunDiskMask);\n"
        "p = vec2(uv.x * aspect, uv.y);\n"
        "layer3Height = horizonY * 0.65 + 0.09 * abs(sin(p.x * ridgeFreq * 0.6 + 0.7)) + 0.035 * abs(sin(p.x * ridgeFreq * 1.7 + 2.1));\n"
        "mask3 = 1.0 - smoothstep(layer3Height - 0.004, layer3Height + 0.004, uv.y);\n"
        "layer3Color = mix(vec3(0.55, 0.62, 0.72), skyColor, hazeAmount * 0.7);\n"
        "col = mix(col, layer3Color, mask3);\n"
        "layer2Height = horizonY * 0.50 + 0.13 * abs(sin(p.x * ridgeFreq * 1.1 + 2.4)) + 0.05 * abs(sin(p.x * ridgeFreq * 2.6 + 0.9));\n"
        "mask2 = 1.0 - smoothstep(layer2Height - 0.004, layer2Height + 0.004, uv.y);\n"
        "layer2Color = mix(vec3(0.32, 0.4, 0.5), skyColor, hazeAmount * 0.35);\n"
        "col = mix(col, layer2Color, mask2);\n"
        "layer1Height = horizonY * 0.34 + 0.16 * abs(sin(p.x * ridgeFreq * 1.6 + 4.1)) + 0.06 * abs(sin(p.x * ridgeFreq * 3.3 + 1.6));\n"
        "mask1 = 1.0 - smoothstep(layer1Height - 0.004, layer1Height + 0.004, uv.y);\n"
        "layer1Color = vec3(0.10, 0.13, 0.18);\n"
        "col = mix(col, layer1Color, mask1);\n"
        "groundMask = 1.0 - smoothstep(horizonY - 0.01, horizonY + 0.01, uv.y);\n"
        "waterBase = vec3(0.08, 0.12, 0.2);\n"
        "col = mix(col, waterBase, groundMask);\n"
        "reflPos = vec2(0.5, horizonY - sunY);\n"
        "reflD = vec2((uv.x - reflPos.x) * aspect, (uv.y - reflPos.y) * 0.35);\n"
        "reflDist = length(reflD);\n"
        "reflGlow = (1.0 - smoothstep(sunSize * 0.5, sunSize * 2.5, reflDist)) * groundMask;\n"
        "col = mix(col, vec3(1.0, 0.85, 0.6), reflGlow * reflectAmount);\n"
        "fog = smoothstep(horizonY - 0.15, horizonY, uv.y) * groundMask;\n"
        "col = mix(col, vec3(0.6, 0.65, 0.7), fog * fogDensity * 0.5);" },
      { "Cat",
        "param float size = 0.30 [0.15, 0.45];\n"
        "param float earSize = 0.15 [0.05, 0.3];\n"
        "param float squint = 0.8 [0.0, 1.0];\n"
        "param float stripes = 0.4 [0.0, 1.0];\n"
        "param float warmth = 0.0 [-0.3, 0.3];\n"
        "param float shine = 0.35 [0.0, 1.0];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "rad = length(p);\n"
        "lp = vec2(abs(p.x) - size * 0.62, p.y - size * 0.60);\n"
        "earH = earSize * 1.7;\n"
        "earW = earSize * 0.95;\n"
        "taper = clamp(lp.y / max(earH, 0.0001), 0.0, 1.0);\n"
        "edgeX = earW * (1.0 - taper);\n"
        "earSide = 1.0 - smoothstep(edgeX - 0.004, edgeX + 0.004, abs(lp.x));\n"
        "earTop = 1.0 - smoothstep(earH - 0.01, earH + 0.01, lp.y);\n"
        "earBot = smoothstep(-0.02, 0.01, lp.y);\n"
        "earMask = earSide * earTop * earBot;\n"
        "ilp = lp - vec2(0.0, earH * 0.12);\n"
        "iearH = earH * 0.6;\n"
        "iearW = earW * 0.5;\n"
        "itaper = clamp(ilp.y / max(iearH, 0.0001), 0.0, 1.0);\n"
        "iedgeX = iearW * (1.0 - itaper);\n"
        "innerSide = 1.0 - smoothstep(iedgeX - 0.003, iedgeX + 0.003, abs(ilp.x));\n"
        "innerTop = 1.0 - smoothstep(iearH - 0.01, iearH + 0.01, ilp.y);\n"
        "innerBot = smoothstep(-0.01, 0.01, ilp.y);\n"
        "innerEarMask = innerSide * innerTop * innerBot * earMask;\n"
        "headMask = 1.0 - smoothstep(size - 0.01, size + 0.01, rad);\n"
        "stripePattern = 0.5 + 0.5 * sin(p.y * 26.0 + p.x * 6.0);\n"
        "stripeMask = stripes * clamp(stripePattern - 0.55, 0.0, 1.0) * 2.2 * headMask * smoothstep(-0.05, size * 0.5, p.y);\n"
        "ep = vec2(abs(p.x) - size * 0.36, p.y - size * 0.08);\n"
        "eyeR = size * 0.11;\n"
        "roundEye = 1.0 - smoothstep(eyeR - 0.006, eyeR + 0.006, length(ep));\n"
        "pupil = 1.0 - smoothstep(eyeR * 0.45 - 0.004, eyeR * 0.45 + 0.004, length(ep));\n"
        "ex = ep.x / max(eyeR * 1.4, 0.0001);\n"
        "arcY = eyeR * 0.9 * (1.0 - ex * ex);\n"
        "arcDist = abs(ep.y - arcY);\n"
        "arcEye = (1.0 - smoothstep(0.010, 0.014, arcDist)) * (1.0 - smoothstep(eyeR * 1.4 - 0.01, eyeR * 1.4 + 0.01, abs(ep.x)));\n"
        "eyeMask = mix(roundEye, arcEye, squint);\n"
        "pupilMask = pupil * (1.0 - squint);\n"
        "noseCenter = p - vec2(0.0, -size * 0.08);\n"
        "noseMask = 1.0 - smoothstep(size * 0.05 - 0.006, size * 0.05 + 0.006, length(noseCenter));\n"
        "mp = vec2(abs(p.x) - size * 0.10, p.y + size * 0.16);\n"
        "mx = mp.x / max(size * 0.22, 0.0001);\n"
        "mouthY = -size * 0.07 * (1.0 - mx * mx);\n"
        "mouthDist = abs(mp.y - mouthY);\n"
        "mouthMask = (1.0 - smoothstep(0.006, 0.010, mouthDist)) * (1.0 - smoothstep(size * 0.22 - 0.01, size * 0.22 + 0.01, abs(mp.x)));\n"
        "wDir1 = vec2(cos(0.12), sin(0.12));\n"
        "wp1 = vec2(abs(p.x) - size * 0.34, p.y + size * 0.02);\n"
        "wAlong1 = wp1.x * wDir1.x + wp1.y * wDir1.y;\n"
        "wPerp1 = -wp1.x * wDir1.y + wp1.y * wDir1.x;\n"
        "whisker1 = (1.0 - smoothstep(0.0025, 0.004, abs(wPerp1))) * smoothstep(0.0, 0.02, wAlong1) * (1.0 - smoothstep(size * 0.55, size * 0.6, wAlong1));\n"
        "wDir2 = vec2(cos(-0.10), sin(-0.10));\n"
        "wp2 = vec2(abs(p.x) - size * 0.34, p.y + size * 0.09);\n"
        "wAlong2 = wp2.x * wDir2.x + wp2.y * wDir2.y;\n"
        "wPerp2 = -wp2.x * wDir2.y + wp2.y * wDir2.x;\n"
        "whisker2 = (1.0 - smoothstep(0.0025, 0.004, abs(wPerp2))) * smoothstep(0.0, 0.02, wAlong2) * (1.0 - smoothstep(size * 0.55, size * 0.6, wAlong2));\n"
        "whiskerMask = clamp(whisker1 + whisker2, 0.0, 1.0);\n"
        "furBase = mix(vec3(0.62 + warmth, 0.40, 0.20), vec3(0.90, 0.58, 0.30), clamp(1.0 - rad / max(size, 0.0001), 0.0, 1.0));\n"
        "furShaded = mix(furBase, furBase * 0.55, stripeMask);\n"
        "lightVec = p - vec2(-size * 0.32, size * 0.30);\n"
        "highlight = shine * (1.0 - smoothstep(0.0, size * 0.6, length(lightVec)));\n"
        "furFinal = mix(furShaded, vec3(1.0, 0.95, 0.85), highlight * 0.4);\n"
        "col = mix(col, furFinal, headMask);\n"
        "col = mix(col, furFinal, earMask);\n"
        "col = mix(col, vec3(0.85, 0.45, 0.50), innerEarMask);\n"
        "col = mix(col, vec3(0.05, 0.05, 0.05), eyeMask);\n"
        "col = mix(col, vec3(0.02, 0.02, 0.02), pupilMask);\n"
        "col = mix(col, vec3(0.85, 0.35, 0.40), noseMask);\n"
        "col = mix(col, vec3(0.10, 0.08, 0.08), mouthMask);\n"
        "col = mix(col, vec3(0.95, 0.95, 0.95), whiskerMask);" },
      { "Robot",
        "param float headW = 0.34 [0.22, 0.46];\n"
        "param float headH = 0.40 [0.26, 0.52];\n"
        "param float eyeSize = 0.09 [0.05, 0.14];\n"
        "param float eyeGlow = 1.2 [0.4, 2.5];\n"
        "param float scanSpeed = 2.0 [0.0, 8.0];\n"
        "param float panelTint = 0.5 [0.0, 1.0];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "col = vec3(0.07, 0.08, 0.10);\n"
        "headX = 1.0 - smoothstep(headW - 0.015, headW + 0.015, abs(p.x));\n"
        "headY = 1.0 - smoothstep(headH - 0.015, headH + 0.015, abs(p.y));\n"
        "head = headX * headY;\n"
        "col = mix(col, vec3(0.72, 0.76, 0.80), head);\n"
        "panelH = (1.0 - smoothstep(0.0, 0.004, abs(p.y - headH * 0.18))) * head;\n"
        "col = mix(col, vec3(0.5, 0.54, 0.58), panelH * panelTint);\n"
        "panelV = (1.0 - smoothstep(0.0, 0.004, abs(p.x))) * head;\n"
        "col = mix(col, vec3(0.5, 0.54, 0.58), panelV * panelTint * 0.6);\n"
        "eyeL = p - vec2(-0.14, 0.05);\n"
        "eyeR = p - vec2(0.14, 0.05);\n"
        "dL = length(eyeL);\n"
        "dR = length(eyeR);\n"
        "housingL = 1.0 - smoothstep(eyeSize, eyeSize + 0.02, dL);\n"
        "housingR = 1.0 - smoothstep(eyeSize, eyeSize + 0.02, dR);\n"
        "col = mix(col, vec3(0.06, 0.06, 0.08), housingL);\n"
        "col = mix(col, vec3(0.06, 0.06, 0.08), housingR);\n"
        "lensL = 1.0 - smoothstep(eyeSize * 0.62, eyeSize * 0.62 + 0.012, dL);\n"
        "lensR = 1.0 - smoothstep(eyeSize * 0.62, eyeSize * 0.62 + 0.012, dR);\n"
        "lensColor = vec3(0.2, 0.85, 1.0) * eyeGlow;\n"
        "col = mix(col, lensColor, lensL);\n"
        "col = mix(col, lensColor, lensR);\n"
        "scanL = 0.5 + 0.5 * sin(eyeL.y * 40.0 - t * scanSpeed);\n"
        "scanR = 0.5 + 0.5 * sin(eyeR.y * 40.0 - t * scanSpeed);\n"
        "scanMaskL = lensL * smoothstep(0.82, 1.0, scanL);\n"
        "scanMaskR = lensR * smoothstep(0.82, 1.0, scanR);\n"
        "col = mix(col, vec3(1.0), scanMaskL * 0.5);\n"
        "col = mix(col, vec3(1.0), scanMaskR * 0.5);\n"
        "specL = length(eyeL - vec2(-eyeSize * 0.35, eyeSize * 0.35));\n"
        "specR = length(eyeR - vec2(-eyeSize * 0.35, eyeSize * 0.35));\n"
        "specMaskL = (1.0 - smoothstep(0.0, eyeSize * 0.18, specL)) * lensL;\n"
        "specMaskR = (1.0 - smoothstep(0.0, eyeSize * 0.18, specR)) * lensR;\n"
        "col = mix(col, vec3(1.0), specMaskL);\n"
        "col = mix(col, vec3(1.0), specMaskR);\n"
        "mouthW = 0.15;\n"
        "mouthH = 0.085;\n"
        "mouthY = -0.20;\n"
        "mouthX = 1.0 - smoothstep(mouthW - 0.006, mouthW + 0.006, abs(p.x));\n"
        "mouthYm = 1.0 - smoothstep(mouthH - 0.006, mouthH + 0.006, abs(p.y - mouthY));\n"
        "mouthHousing = mouthX * mouthYm;\n"
        "col = mix(col, vec3(0.04, 0.04, 0.05), mouthHousing);\n"
        "bar1 = mouthHousing * (1.0 - smoothstep(0.0, 0.006, abs(p.y - mouthY - 0.05)));\n"
        "bar2 = mouthHousing * (1.0 - smoothstep(0.0, 0.006, abs(p.y - mouthY - 0.017)));\n"
        "bar3 = mouthHousing * (1.0 - smoothstep(0.0, 0.006, abs(p.y - mouthY + 0.017)));\n"
        "bar4 = mouthHousing * (1.0 - smoothstep(0.0, 0.006, abs(p.y - mouthY + 0.05)));\n"
        "col = mix(col, vec3(0.45, 0.5, 0.55), bar1);\n"
        "col = mix(col, vec3(0.45, 0.5, 0.55), bar2);\n"
        "col = mix(col, vec3(0.45, 0.5, 0.55), bar3);\n"
        "col = mix(col, vec3(0.45, 0.5, 0.55), bar4);\n"
        "antennaTop = headH + 0.12;\n"
        "antennaStem = (1.0 - smoothstep(0.0, 0.006, abs(p.x))) * smoothstep(headH - 0.01, headH + 0.01, p.y) * (1.0 - smoothstep(antennaTop - 0.01, antennaTop + 0.01, p.y));\n"
        "col = mix(col, vec3(0.55, 0.6, 0.65), antennaStem);\n"
        "tipD = length(p - vec2(0.0, antennaTop));\n"
        "tipMask = 1.0 - smoothstep(0.02, 0.032, tipD);\n"
        "tipTint = vec3(1.0, 0.3, 0.3);\n"
        "tipGlow = tipTint * (0.6 + 0.4 * sin(t * scanSpeed * 1.5));\n"
        "col = mix(col, tipGlow, tipMask);" },
      { "Nebula / Galaxy",
        "param float cloudScale = 3.0 [1.0, 8.0];\n"
        "param float swirl = 2.0 [0.0, 6.0];\n"
        "param float speed = 0.3 [0.0, 2.0];\n"
        "param float coreGlow = 1.0 [0.2, 2.5];\n"
        "param float colorShift = 0.0 [0.0, 1.0];\n"
        "param float starDensity = 40.0 [10.0, 120.0];\n"
        "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);\n"
        "rad = length(p);\n"
        "angle = atan2(p.y, p.x);\n"
        "swirlAngle = angle + swirl * rad * 2.0 - t * speed * 0.5;\n"
        "sp = vec2(cos(swirlAngle), sin(swirlAngle)) * rad;\n"
        "freqA = cloudScale;\n"
        "cA = sin(sp.x * freqA + t * speed) * cos(sp.y * freqA * 1.3 - t * speed * 0.7);\n"
        "rB = vec2(sp.x * cos(0.65) - sp.y * sin(0.65), sp.x * sin(0.65) + sp.y * cos(0.65));\n"
        "cB = sin(rB.x * freqA * 2.1 + t * speed * 1.4) * cos(rB.y * freqA * 1.7 - t * speed * 0.9);\n"
        "rC = vec2(sp.x * cos(1.15) - sp.y * sin(1.15), sp.x * sin(1.15) + sp.y * cos(1.15));\n"
        "cC = sin(rC.x * freqA * 3.7 + t * speed * 0.6) * cos(rC.y * freqA * 2.9 + t * speed * 1.1);\n"
        "turb = (cA + 0.6 * cB + 0.35 * cC) / 1.95;\n"
        "cloudVal = 0.5 + 0.5 * turb;\n"
        "huey = cloudVal + colorShift;\n"
        "cloudColor = vec3(0.5 + 0.5 * cos(6.283185 * (huey + 0.0)), 0.5 + 0.5 * cos(6.283185 * (huey + 0.33)), 0.5 + 0.5 * cos(6.283185 * (huey + 0.67)));\n"
        "cloudColor = cloudColor * vec3(0.7, 0.5, 1.0);\n"
        "col = cloudColor * (0.35 + 0.5 * cloudVal);\n"
        "core = coreGlow * (0.15 / (rad * rad + 0.01));\n"
        "glowTint = vec3(1.0, 0.9, 0.75);\n"
        "col = col + glowTint * core * 0.15;\n"
        "starP = vec2(uv.x * aspect, uv.y) * starDensity;\n"
        "starCell = floor(starP);\n"
        "starF = fract(starP) - 0.5;\n"
        "starHash = fract(sin(dot(starCell, vec2(12.9898, 78.233))) * 43758.5453);\n"
        "starHash2 = fract(sin(dot(starCell, vec2(39.346, 11.135))) * 53758.5453);\n"
        "starOffset = vec2(starHash - 0.5, starHash2 - 0.5) * 0.7;\n"
        "starDist = length(starF - starOffset);\n"
        "starPresence = smoothstep(0.94, 1.0, starHash2);\n"
        "twinkle = 0.5 + 0.5 * sin(t * 3.0 + starHash * 30.0);\n"
        "starDot = (1.0 - smoothstep(0.0, 0.12, starDist)) * starPresence * (0.5 + 0.5 * twinkle);\n"
        "col = col + vec3(1.0) * starDot;" }
   };
   return kPresets;
}

const std::vector<std::string>& FieldPixelNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
   {
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   }
   return kNames;
}

void FieldPixelNode::LoadPreset(int index)
{
   const auto& p = Presets();
   if (index >= 0 && index < (int)p.size())
   {
      code = p[index].code;
      Apply();
   }
}

Field::DeviceFile FieldPixelNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "pixel";
   device.code = code;
   for (const auto& p : mParamTable.Params())
   {
      if (p.isDeclared)
         device.params[p.name] = p.value;
   }
   device.nodeSettings["width"] = (double)width;
   device.nodeSettings["height"] = (double)height;
   device.nodeSettings["animate"] = animate ? 1.0 : 0.0;
   return device;
}

void FieldPixelNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   auto itW = device.nodeSettings.find("width");
   if (itW != device.nodeSettings.end())
      width = (float)itW->second;
   auto itH = device.nodeSettings.find("height");
   if (itH != device.nodeSettings.end())
      height = (float)itH->second;
   auto itA = device.nodeSettings.find("animate");
   if (itA != device.nodeSettings.end())
      animate = itA->second != 0.0;
   Apply();
   for (const auto& kv : device.params)
   {
      Field::ParamEntry* p = mParamTable.Find(kv.first);
      if (p != nullptr)
         p->value = kv.second;
   }
}

FieldPixelNode::FieldPixelNode()
{
   code = Presets()[0].code;
}

FieldPixelNode::~FieldPixelNode()
{
   if (mProgram != 0)
   {
      glDeleteProgram(mProgram);
      mProgram = 0;
   }
}

// The ping-pong pair holds STATE, not picture. A state kernel renders its
// colour into mOut on a second pass, so `col` means the same thing whether or
// not the kernel declares state cells.
unsigned int FieldPixelNode::GetOutputTexture() { return GLUtil::FboTexture(mOut); }
unsigned int FieldPixelNode::GetOutputTexture(int index)
{
   if (index == 1 && exposeAuxTexture && !mIR.declaredStates.empty())
      return mState.CurrentOutputTexture();
   return index == 0 ? GetOutputTexture() : 0;
}
int FieldPixelNode::GetOutputWidth() const { return mOut.w; }
int FieldPixelNode::GetOutputHeight() const { return mOut.h; }

bool FieldPixelNode::Apply()
{
   pinRefusal.clear();
   std::vector<Field::Token> tokens;
   Field::FieldError lexErr;
   if (!Field::Lex(code, tokens, lexErr))
   {
      mLastError = "line " + std::to_string(lexErr.span.line) + ", col " + std::to_string(lexErr.span.col) + ": " + lexErr.message;
      return false;
   }

   Field::AstNodePtr ast;
   Field::FieldError parseErr;
   if (!Field::ParseProgram(tokens, ast, parseErr))
   {
      mLastError = "line " + std::to_string(parseErr.span.line) + ", col " + std::to_string(parseErr.span.col) + ": " + parseErr.message;
      return false;
   }

   Field::PixelIRProgram ir;
   Field::FieldError irErr;
   if (!Field::LowerPixelProgramToIR(ast, ir, irErr))
   {
      mLastError = "line " + std::to_string(irErr.span.line) + ", col " + std::to_string(irErr.span.col) + ": " + irErr.message;
      return false;
   }

   // Emit and compile against LOCALS. Keeping the last working program means
   // keeping the IR and uniform table that describe it too: if mIR were swapped
   // here and the compile then failed, the old program would keep running while
   // every uniform location read -1 (so params and hoisted values froze) and
   // GetOutputTexture() could flip between mOut and the state bank.
   Field::GlslEmitResult emit = Field::EmitGlsl(ir);

   std::string compileErr;
   unsigned int program = GLUtil::CompileProgram(emit.source.c_str(), &compileErr);
   if (program == 0)
   {
      mLastError = compileErr;
      return false;
   }

   // Dynamic pins, Phase 2b (build step 13, §5.1): reconcile the declared
   // output/input pin tables against this compile's LOCAL `ir` (not yet
   // swapped into mIR) - a refusal here must leave mIR/mProgram untouched,
   // same "keep last working program" discipline as a GLSL compile error
   // above. Must run before the mProgram/mIR commit below.
   {
      std::vector<Field::DeclaredPin> declOut, declIn;
      for (const auto& d : ir.declaredOutputs)
         declOut.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), true });
      for (const auto& d : ir.declaredInputs)
         declIn.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), false });

      std::string pinNotice, pinRefusalMsg;
      bool outOk = Field::ReconcileFieldPins(mOutputPins, declOut, mNodeIndex, NativeOutputCount(), pinNotice, pinRefusalMsg);
      bool inOk = outOk && Field::ReconcileFieldPins(mInputPins, declIn, mNodeIndex, /*nativeCount=*/1, pinNotice, pinRefusalMsg);
      if (!outOk || !inOk)
      {
         glDeleteProgram(program);
         mLastError = pinRefusalMsg;
         pinRefusal = pinRefusalMsg;
         return false;
      }
      if (!pinNotice.empty())
         mNotice = pinNotice;
   }

   if (mProgram != 0)
      glDeleteProgram(mProgram);
   mProgram = program;
   mIR = std::move(ir);
   mEmitResult = std::move(emit);
   mParamTable.Reconcile(mIR.declaredParams, mNodeIndex, mNotice);
   mLastError.clear();

   // Cache uniform locations once
   mLocRes = glGetUniformLocation(mProgram, "fld_res");
   mLocT = glGetUniformLocation(mProgram, "fld_t");
   mLocDt = glGetUniformLocation(mProgram, "fld_dt");
   mLocFrame = glGetUniformLocation(mProgram, "fld_frame");
   mLocAge = glGetUniformLocation(mProgram, "fld_age");
   mLocSrcTex = glGetUniformLocation(mProgram, "fld_srcTex");
   mLocSrcAlpha = glGetUniformLocation(mProgram, "fld_srcAlpha");
   mLocOutMode = glGetUniformLocation(mProgram, "fld_outMode");
   mLocStateBank0 = glGetUniformLocation(mProgram, "fld_s_bank0");

   mUniformLocs.clear();
   for (auto& slot : mEmitResult.uniforms)
   {
      slot.location = glGetUniformLocation(mProgram, slot.name.c_str());
      mUniformLocs.push_back(slot.location);
   }

   return true;
}

static unsigned int GetDefaultBlackTexture()
{
   static unsigned int sBlackTex = 0;
   if (sBlackTex == 0)
   {
      glGenTextures(1, &sBlackTex);
      glBindTexture(GL_TEXTURE_2D, sBlackTex);
      unsigned char black[4] = { 0, 0, 0, 0 };
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   return sBlackTex;
}

void FieldPixelNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mProgram == 0 && mLastError.empty())
      Apply();
   if (mProgram == 0)
      return;

   // Transport reset check
   unsigned long long currentEpoch = Transport::Instance().ResetEpoch();
   if (currentEpoch != mLastResetEpoch)
   {
      mState.Reset();
      mLastResetEpoch = currentEpoch;
   }

   const int w = std::max(4, (int)width);
   const int h = std::max(4, (int)height);

   const float clock = animate ? (float)Transport::Instance().Seconds() : 0.0f;
   const float dt = (mLastEvalT >= 0.0f) ? (float)(clock - mLastEvalT) : (1.0f / 60.0f);
   mLastEvalT = clock;

   // Evaluate hoisted prologue values on CPU
   std::unordered_map<std::string, double> env;
   std::vector<float> hoistedValues(mIR.prologue.size(), 0.0f);
   for (size_t i = 0; i < mIR.prologue.size(); i++)
   {
      const auto& stmt = mIR.prologue[i];
      if (stmt->rvalueExpr)
      {
         double val = EvalPrologueNode(stmt->rvalueExpr, (double)clock, (double)dt, frameId, mParamTable, env);
         env[stmt->assignTarget] = val;
         hoistedValues[i] = (float)val;
      }
   }

   // No native input pin (device-catalog simplification: Field Pixel is
   // generation-only for now) - `src` in a kernel always reads the black
   // texture/alpha-0, same as an unconnected pin used to.
   unsigned int srcTex = GetDefaultBlackTexture();
   float srcAlpha = 0.0f;

   auto setupUniforms = [this, w, h, clock, dt, frameId, srcTex, srcAlpha, &hoistedValues]()
   {
      if (mLocRes >= 0) glUniform2f(mLocRes, (float)w, (float)h);
      if (mLocT >= 0) glUniform1f(mLocT, clock);
      if (mLocDt >= 0) glUniform1f(mLocDt, dt);
      if (mLocFrame >= 0) glUniform1i(mLocFrame, frameId);
      if (mLocAge >= 0) glUniform1f(mLocAge, mStateAge);

      // Input image texture unit 1 (unit 0 is reserved for state ping-pong)
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      if (mLocSrcTex >= 0) glUniform1i(mLocSrcTex, 1);
      if (mLocSrcAlpha >= 0) glUniform1f(mLocSrcAlpha, srcAlpha);

      // Set hoisted and param uniforms
      for (size_t i = 0; i < mEmitResult.uniforms.size(); i++)
      {
         const auto& slot = mEmitResult.uniforms[i];
         int loc = slot.location;
         if (loc < 0) continue;

         if (slot.hoistedIndex >= 0 && slot.hoistedIndex < (int)hoistedValues.size())
         {
            glUniform1f(loc, hoistedValues[slot.hoistedIndex]);
         }
         else if (slot.paramIndex >= 0)
         {
            // slot.paramIndex is this compile's own declaration order (an
            // index into GlslBackend's local program.declaredParams), NOT a
            // position in mParamTable - mParamTable is a persistent, name-
            // keyed table that accumulates entries across every preset ever
            // loaded into this node instance (Reconcile only appends/renames,
            // never reorders or removes), so the two indices only happened to
            // line up for a node's very first compile. Look up by name - the
            // same stable key the UI sliders and Reconcile both use - instead
            // of trusting positional alignment between two unrelated arrays.
            if (const Field::ParamEntry* p = mParamTable.Find(slot.varName))
               glUniform1f(loc, p->value);
         }
      }

      glActiveTexture(GL_TEXTURE0);
   };

   if (!GLUtil::EnsureFbo(mOut, w, h, GL_RGBA16F))
      return;

   if (mIR.declaredStates.empty())
   {
      GLUtil::RunShaderPass(mOut, mProgram, setupUniforms);
      return;
   }

   // Build step 22 (OPEN-C): a kernel that reads its neighbours is a
   // simulation and integrates for minutes, so its cells go to RGBA32F. A
   // kernel that only reads its own pixel back (trails, feedback) stays on
   // the 16F bank at half the memory.
   mState.Resize(w, h, mEmitResult.usesOffsetReads);
   if (mState.NeedsClear())
   {
      float initVals[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      for (size_t i = 0; i < mIR.declaredStates.size() && i < 4; i++)
      {
         if (!mIR.declaredStates[i].initialValues.empty())
            initVals[i] = mIR.declaredStates[i].initialValues[0];
      }
      mState.ClearBoth(initVals);
      mStateAge = 0.0f;
   }

   // Two passes over the same kernel, both reading the PRE-SWAP state texture
   // so they cannot disagree, because RunShaderPass binds one colour
   // attachment and the kernel has two things to write.
   //   pass 0 -> the ping-pong write target, cells packed into RGBA
   //   pass 1 -> mOut, `col` as the node's visible output
   auto runPass = [&](GLUtil::Fbo& dst, int outMode)
   {
      GLUtil::RunShaderPass(dst, mProgram, [this, &setupUniforms, outMode]()
      {
         mState.BindReadUnits(mProgram, mLocStateBank0);
         setupUniforms();
         if (mLocOutMode >= 0) glUniform1i(mLocOutMode, outMode);
      });
   };

   runPass(mState.WriteFbo(), 0);
   runPass(mOut, 1);

   // Both passes of THIS cook see the same age, so a seed written by the
   // state pass is the same one the display pass shows.
   mStateAge += 1.0f;

   mState.Swap(); // exactly once, after both passes
}
