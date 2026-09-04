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
        "col = vec3(D * 1.3, D * 0.55 + 0.02, 0.9 - D * 0.7);" }
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
