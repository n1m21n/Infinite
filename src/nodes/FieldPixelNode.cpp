#include "FieldPixelNode.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "Transport.h"
#include <OpenGL/gl3.h>
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
      { "Radial Wave", "param float speed = 2.0 (0.1 .. 10.0);\nparam float freq = 20.0 (1.0 .. 50.0);\nd = length(uv - 0.5);\ncol = vec3(0.5 + 0.5 * sin(d * freq - t * speed));" },
      { "Feedback Diffusion", "param float decay = 0.98 (0.9 .. 1.0);\nstate float a = 0;\nd = length(uv - 0.5);\nsrc_val = if(d < 0.05, 1.0, a * decay);\na = src_val;\ncol = vec3(a);" },
      { "Modulo Grid", "param float scale = 8.0 (1.0 .. 32.0);\ngrid = fmod(uv * scale, 1.0);\ncol = vec3(grid.x, grid.y, 0.0);" }
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

unsigned int FieldPixelNode::GetOutputTexture()
{
   if (mIR.declaredStates.empty())
      return GLUtil::FboTexture(mOut);
   return mState.CurrentOutputTexture();
}

int FieldPixelNode::GetOutputWidth() const
{
   if (mIR.declaredStates.empty())
      return mOut.w;
   return mState.Width();
}

int FieldPixelNode::GetOutputHeight() const
{
   if (mIR.declaredStates.empty())
      return mOut.h;
   return mState.Height();
}

bool FieldPixelNode::Apply()
{
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

   mIR = ir;
   mParamTable.Reconcile(mIR.declaredParams, mNodeIndex, mNotice);

   mEmitResult = Field::EmitGlsl(mIR);

   std::string compileErr;
   unsigned int program = GLUtil::CompileProgram(mEmitResult.source.c_str(), &compileErr);
   if (program == 0)
   {
      mLastError = compileErr;
      return false;
   }

   if (mProgram != 0)
      glDeleteProgram(mProgram);
   mProgram = program;
   mLastError.clear();

   // Cache uniform locations once
   mLocRes = glGetUniformLocation(mProgram, "fld_res");
   mLocT = glGetUniformLocation(mProgram, "fld_t");
   mLocDt = glGetUniformLocation(mProgram, "fld_dt");
   mLocFrame = glGetUniformLocation(mProgram, "fld_frame");
   mLocSrcTex = glGetUniformLocation(mProgram, "fld_srcTex");
   mLocSrcAlpha = glGetUniformLocation(mProgram, "fld_srcAlpha");

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

   unsigned int srcTex = (input && input->GetOutputTexture() != 0) ? input->GetOutputTexture() : GetDefaultBlackTexture();
   float srcAlpha = (input && input->GetOutputTexture() != 0) ? 1.0f : 0.0f;

   auto setupUniforms = [this, w, h, clock, dt, frameId, srcTex, srcAlpha, &hoistedValues]()
   {
      if (mLocRes >= 0) glUniform2f(mLocRes, (float)w, (float)h);
      if (mLocT >= 0) glUniform1f(mLocT, clock);
      if (mLocDt >= 0) glUniform1f(mLocDt, dt);
      if (mLocFrame >= 0) glUniform1i(mLocFrame, frameId);

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
         else if (slot.paramIndex >= 0 && slot.paramIndex < (int)mParamTable.Params().size())
         {
            glUniform1f(loc, mParamTable.Params()[slot.paramIndex].value);
         }
      }

      glActiveTexture(GL_TEXTURE0);
   };

   if (mIR.declaredStates.empty())
   {
      if (!GLUtil::EnsureFbo(mOut, w, h, GL_RGBA16F))
         return;

      GLUtil::RunShaderPass(mOut, mProgram, setupUniforms);
   }
   else
   {
      mState.Resize(w, h);
      if (mState.NeedsClear())
      {
         float initVals[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
         for (size_t i = 0; i < mIR.declaredStates.size() && i < 4; i++)
         {
            if (!mIR.declaredStates[i].initialValues.empty())
               initVals[i] = mIR.declaredStates[i].initialValues[0];
         }
         mState.ClearBoth(initVals);
      }

      GLUtil::Fbo& writeFbo = mState.WriteFbo();
      GLUtil::RunShaderPass(writeFbo, mProgram, [this, &setupUniforms]()
      {
         mState.BindReadUnits(mProgram);
         setupUniforms();
      });

      mState.Swap(); // Swap immediately after pass
   }
}
