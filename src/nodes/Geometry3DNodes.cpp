#include "Geometry3DNodes.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cstdio>

#include "Transport.h"
#include "SceneNodes.h"
#include "GeometryOpNodes.h"

namespace
{
   const std::vector<std::string> kShapeNames = {
      "Plane", "Cube", "Sphere", "Icosphere", "Torus", "Cylinder", "Cone", "Torus Knot"
   };
   const std::vector<std::string> kShadingNames = { "Lit", "Normals", "UV", "Flat" };
   const std::vector<std::string> kProjectionNames = { "Perspective", "Orthographic" };

   const char* kVertSrc =
      "#version 150\n"
      "in vec3 aPos;\n"
      "in vec3 aNormal;\n"
      "in vec2 aUv;\n"
      "in mat4 aInstance;\n"      // per-instance transform, divisor 1
      "uniform mat4 uModel;\n"
      "uniform int uInstanced;\n"
      "uniform mat4 uViewProj;\n"
      "uniform mat3 uNormalMatrix;\n"
      "out vec3 vWorldPos;\n"
      "out vec3 vNormal;\n"
      "out vec2 vUv;\n"
      "void main() {\n"
      "   mat4 model = (uInstanced == 1) ? aInstance : uModel;\n"
      "   vec4 world = model * vec4(aPos, 1.0);\n"
      "   vWorldPos = world.xyz;\n"
      "   vNormal = (uInstanced == 1)\n"
      "      ? normalize(mat3(model) * aNormal)\n"
      "      : normalize(uNormalMatrix * aNormal);\n"
      "   vUv = aUv;\n"
      "   gl_Position = uViewProj * world;\n"
      "}\n";

   const char* kFragSrc =
      "#version 150\n"
      "in vec3 vWorldPos;\n"
      "in vec3 vNormal;\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform vec3 uBaseColor;\n"
      "uniform float uMetallic;\n"
      "uniform float uRoughness;\n"
      "uniform float uOpacity;\n"
      "uniform int uShading;\n"
      "uniform vec3 uLightDir[3];\n"
      "uniform vec3 uLightColor[3];\n"
      "uniform float uLightIntensity[3];\n"
      "uniform int uLightType[3];\n"
      "uniform int uLightCount;\n"
      "uniform vec3 uAmbient;\n"
      "uniform float uRim;\n"
      "uniform vec3 uCamPos;\n"
      "uniform sampler2D uTexture;\n"
      "uniform int uHasTexture;\n"
      "void main() {\n"
      "   vec3 n = normalize(vNormal);\n"
      "   if (uShading == 1) { fragColor = vec4(n * 0.5 + 0.5, uOpacity); return; }\n"
      "   if (uShading == 2) { fragColor = vec4(vUv, 0.0, uOpacity); return; }\n"
      "\n"
      "   vec3 base = uBaseColor;\n"
      "   if (uHasTexture == 1) base *= texture(uTexture, vUv).rgb;\n"
      "   if (uShading == 3) { fragColor = vec4(base, uOpacity); return; }\n"
      "\n"
      "   vec3 viewDir = normalize(uCamPos - vWorldPos);\n"
      "   float shininess = mix(4.0, 256.0, 1.0 - clamp(uRoughness, 0.0, 1.0));\n"
      "   vec3 diffuseColor = mix(base, vec3(0.02), uMetallic);\n"
      "   vec3 specColor = mix(vec3(1.0), base, uMetallic);\n"
      "   vec3 col = uAmbient * base;\n"
      "\n"
      "   for (int i = 0; i < 3; i++) {\n"
      "      if (i >= uLightCount) break;\n"
      "      vec3 lightDir;\n"
      "      float attenuation = 1.0;\n"
      "      if (uLightType[i] == 1) {\n"
      "         vec3 toLight = uLightDir[i] - vWorldPos;\n"
      "         float dist = length(toLight);\n"
      "         lightDir = toLight / max(dist, 1e-4);\n"
      "         attenuation = 1.0 / (1.0 + dist * dist * 0.25);\n"
      "      } else {\n"
      "         lightDir = normalize(uLightDir[i]);\n"
      "      }\n"
      "      vec3 halfway = normalize(lightDir + viewDir);\n"
      "      float diffuse = max(dot(n, lightDir), 0.0);\n"
      "      float spec = pow(max(dot(n, halfway), 0.0), shininess) * (1.0 - uRoughness);\n"
      "      float energy = uLightIntensity[i] * attenuation;\n"
      "      col += diffuseColor * uLightColor[i] * energy * diffuse;\n"
      "      col += specColor * uLightColor[i] * energy * spec;\n"
      "   }\n"
      "   col += base * pow(1.0 - max(dot(n, viewDir), 0.0), 3.0) * uRim;\n"
      "   fragColor = vec4(col, uOpacity);\n"
      "}\n";
}

// ================================================================ Geometry

const std::vector<std::string>& GeometryNode::ShapeNames() { return kShapeNames; }
const std::vector<std::string>& GeometryNode::ShadingNames() { return kShadingNames; }

GeometryNode::~GeometryNode()
{
   GLUtil::DestroyFbo(mPreview);
}

void GeometryNode::RebuildIfNeeded()
{
   if (shape == mBuiltShape && detail == mBuiltDetail && sides == mBuiltSides &&
       knotP == mBuiltP && knotQ == mBuiltQ && tubeRadius == mBuiltTube && !mMesh.Empty())
      return;

   switch (shape)
   {
      case 0: mMesh = Primitives::Plane(detail); break;
      case 1: mMesh = Primitives::Cube(std::max(1, detail / 8)); break;
      case 2: mMesh = Primitives::Sphere(detail, sides * 2); break;
      case 3: mMesh = Primitives::Icosphere(std::max(0, std::min(4, detail / 8))); break;
      case 4: mMesh = Primitives::Torus(detail, sides, tubeRadius); break;
      case 5: mMesh = Primitives::Cylinder(sides, std::max(1, detail / 8), 1.0f); break;
      case 6: mMesh = Primitives::Cone(sides, std::max(1, detail / 8)); break;
      default: mMesh = Primitives::TorusKnot(detail * 8, sides, tubeRadius, knotP, knotQ); break;
   }

   mBuiltShape = shape;
   mBuiltDetail = detail;
   mBuiltSides = sides;
   mBuiltP = knotP;
   mBuiltQ = knotQ;
   mBuiltTube = tubeRadius;
}

const Mesh& GeometryNode::GetMesh()
{
   RebuildIfNeeded();
   return mMesh;
}

Mat4 GeometryNode::GetModelMatrix() const
{
   const float spin = spinY * (float)Transport::Instance().Beats();
   Mat4 m = Mat4::Scale(scaleX * uniformScale, scaleY * uniformScale, scaleZ * uniformScale);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY + spin), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX), m);
   m = Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
   return m;
}

void GeometryNode::GetMaterial(float outColor[3], float& outMetallic, float& outRoughness,
                               float& outOpacity, int& outShading) const
{
   outColor[0] = color[0];
   outColor[1] = color[1];
   outColor[2] = color[2];
   outMetallic = metallic;
   outRoughness = roughness;
   outOpacity = opacity;
   outShading = shading;
}

unsigned int GeometryNode::GetSurfaceTexture()
{
   return mTextureInput.IsConnected() && mTextureInput.GetSource()
             ? mTextureInput.GetSource()->GetOutputTexture()
             : 0;
}

unsigned int GeometryNode::GetOutputTexture()
{
   return GLUtil::FboTexture(mPreview);
}

void GeometryNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   RebuildIfNeeded();
   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);
   // The node's own preview is drawn by the editor using a shared solo renderer;
   // geometry itself produces no image.
}

// ================================================================= Render

const std::vector<std::string>& Render3DNode::ProjectionNames() { return kProjectionNames; }

Render3DNode::~Render3DNode()
{
   if (mColorTex != 0) glDeleteTextures(1, &mColorTex);
   if (mDepthBuffer != 0) glDeleteRenderbuffers(1, &mDepthBuffer);
   if (mFbo != 0) glDeleteFramebuffers(1, &mFbo);
   if (mVbo != 0) glDeleteBuffers(1, &mVbo);
   if (mIbo != 0) glDeleteBuffers(1, &mIbo);
   if (mInstanceVbo != 0) glDeleteBuffers(1, &mInstanceVbo);
   if (mVao != 0) glDeleteVertexArrays(1, &mVao);
   if (mProgram != 0) glDeleteProgram(mProgram);
}

bool Render3DNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;

   auto compile = [](GLenum type, const char* src) -> unsigned int {
      unsigned int shader = glCreateShader(type);
      glShaderSource(shader, 1, &src, nullptr);
      glCompileShader(shader);
      GLint ok = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
      if (!ok)
      {
         char log[1024];
         glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
         fprintf(stderr, "Render3D shader error: %s\n", log);
         glDeleteShader(shader);
         return 0u;
      }
      return shader;
   };

   const unsigned int vert = compile(GL_VERTEX_SHADER, kVertSrc);
   const unsigned int frag = compile(GL_FRAGMENT_SHADER, kFragSrc);
   if (vert == 0 || frag == 0)
      return false;

   mProgram = glCreateProgram();
   glBindAttribLocation(mProgram, 0, "aPos");
   glBindAttribLocation(mProgram, 1, "aNormal");
   glBindAttribLocation(mProgram, 2, "aUv");
   glBindAttribLocation(mProgram, 3, "aInstance"); // occupies locations 3..6
   glAttachShader(mProgram, vert);
   glAttachShader(mProgram, frag);
   glLinkProgram(mProgram);
   glDeleteShader(vert);
   glDeleteShader(frag);

   GLint linked = 0;
   glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
   if (!linked)
   {
      char log[1024];
      glGetProgramInfoLog(mProgram, sizeof(log), nullptr, log);
      fprintf(stderr, "Render3D link error: %s\n", log);
      glDeleteProgram(mProgram);
      mProgram = 0;
      return false;
   }

   glGenVertexArrays(1, &mVao);
   glGenBuffers(1, &mVbo);
   glGenBuffers(1, &mIbo);
   glGenBuffers(1, &mInstanceVbo);
   return true;
}

bool Render3DNode::EnsureResources(int w, int h)
{
   if (mFbo != 0 && mWidth == w && mHeight == h)
      return true;

   if (mColorTex != 0) { glDeleteTextures(1, &mColorTex); mColorTex = 0; }
   if (mDepthBuffer != 0) { glDeleteRenderbuffers(1, &mDepthBuffer); mDepthBuffer = 0; }
   if (mFbo != 0) { glDeleteFramebuffers(1, &mFbo); mFbo = 0; }

   glGenTextures(1, &mColorTex);
   glBindTexture(GL_TEXTURE_2D, mColorTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   // A depth renderbuffer is the piece the 2D pipeline never needed: without it
   // triangles composite in draw order instead of by distance.
   glGenRenderbuffers(1, &mDepthBuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

   glGenFramebuffers(1, &mFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColorTex, 0);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthBuffer);

   const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);

   if (status != GL_FRAMEBUFFER_COMPLETE)
   {
      fprintf(stderr, "Render3D framebuffer incomplete: 0x%x\n", status);
      return false;
   }

   mWidth = w;
   mHeight = h;
   return true;
}

void Render3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShader())
      return;

   const int w = std::max(16, (int)width);
   const int h = std::max(16, (int)height);
   if (!EnsureResources(w, h))
      return;

   // Pull any textures the geometry wants before we start drawing.
   for (int i = 0; i < kSlots; i++)
   {
      if (auto* node = dynamic_cast<INode*>(geometry[i]))
         node->CookIfNeeded(frameId);
   }

   const float aspect = (float)w / (float)h;

   float eye[3];
   Mat4 view, proj;
   if (camera != nullptr)
   {
      camera->ComputeEye(eye);
      view = camera->ViewMatrix();
      proj = camera->ProjectionMatrix(aspect);
   }
   else
   {
      const float ce = std::cos(camElevation);
      eye[0] = targetX + camDistance * ce * std::cos(camAzimuth);
      eye[1] = targetY + camDistance * std::sin(camElevation);
      eye[2] = targetZ + camDistance * ce * std::sin(camAzimuth);
      const float target[3] = { targetX, targetY, targetZ };
      const float up[3] = { 0.0f, 1.0f, 0.0f };
      view = Mat4::LookAt(eye, target, up);
      proj = (projection == 1)
         ? Mat4::Orthographic(orthoHeight, aspect, nearPlane, farPlane)
         : Mat4::Perspective(fov * 3.14159265f / 180.0f, aspect, nearPlane, farPlane);
   }
   const Mat4 viewProj = Mat4::Multiply(proj, view);

   // Gather lights: patched Light nodes win, otherwise the built-in one.
   float lightDirs[kLightSlots * 3] = { 0 };
   float lightCols[kLightSlots * 3] = { 0 };
   float lightPower[kLightSlots] = { 0 };
   int lightTypes[kLightSlots] = { 0 };
   int lightCount = 0;
   for (int i = 0; i < kLightSlots; i++)
   {
      if (lights[i] == nullptr)
         continue;
      float vec[3];
      lights[i]->ComputeVector(vec);
      lightDirs[lightCount * 3 + 0] = vec[0];
      lightDirs[lightCount * 3 + 1] = vec[1];
      lightDirs[lightCount * 3 + 2] = vec[2];
      lightCols[lightCount * 3 + 0] = lights[i]->color[0];
      lightCols[lightCount * 3 + 1] = lights[i]->color[1];
      lightCols[lightCount * 3 + 2] = lights[i]->color[2];
      lightPower[lightCount] = lights[i]->intensity;
      lightTypes[lightCount] = lights[i]->type;
      lightCount++;
   }
   if (lightCount == 0)
   {
      const float le = std::cos(lightElevation);
      lightDirs[0] = le * std::cos(lightAzimuth);
      lightDirs[1] = std::sin(lightElevation);
      lightDirs[2] = le * std::sin(lightAzimuth);
      lightCols[0] = lightColor[0]; lightCols[1] = lightColor[1]; lightCols[2] = lightColor[2];
      lightPower[0] = lightIntensity;
      lightTypes[0] = 0;
      lightCount = 1;
   }

   // --- save the GL state the 2D pipeline relies on -------------------
   GLint prevFbo = 0, prevViewport[4];
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, prevViewport);
   const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
   const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
   const GLboolean prevBlend = glIsEnabled(GL_BLEND);

   // The 2D pipeline and ImGui both leave state behind. Scissor in particular
   // is left enabled with the UI's clip rect, which silently discards every
   // fragment we draw into our own framebuffer.
   const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
   GLint prevScissorBox[4];
   glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_RASTERIZER_DISCARD);
   glDisable(GL_STENCIL_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);

   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glViewport(0, 0, w, h);
   glClearColor(bgColor[0], bgColor[1], bgColor[2], bgOpacity);
   glClearDepth(1.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   if (depthTest)
   {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LESS);
   }
   else
   {
      glDisable(GL_DEPTH_TEST);
   }

   if (backfaceCull)
   {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glFrontFace(GL_CCW);
   }
   else
   {
      glDisable(GL_CULL_FACE);
   }

   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   glUseProgram(mProgram);
   glBindVertexArray(mVao);
   mLastTriangles = 0;
   mLastDrawCalls = 0;

   glUniformMatrix4fv(glGetUniformLocation(mProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
   glUniform3fv(glGetUniformLocation(mProgram, "uLightDir"), kLightSlots, lightDirs);
   glUniform3fv(glGetUniformLocation(mProgram, "uLightColor"), kLightSlots, lightCols);
   glUniform1fv(glGetUniformLocation(mProgram, "uLightIntensity"), kLightSlots, lightPower);
   glUniform1iv(glGetUniformLocation(mProgram, "uLightType"), kLightSlots, lightTypes);
   glUniform1i(glGetUniformLocation(mProgram, "uLightCount"), lightCount);
   glUniform3fv(glGetUniformLocation(mProgram, "uAmbient"), 1, ambientColor);
   glUniform1f(glGetUniformLocation(mProgram, "uRim"), rimIntensity);
   glUniform3fv(glGetUniformLocation(mProgram, "uCamPos"), 1, eye);

   for (int i = 0; i < kSlots; i++)
   {
      IGeometrySource* source = geometry[i];
      if (source == nullptr)
         continue;
      const Mesh& mesh = source->GetMesh();
      if (mesh.Empty())
         continue;

      glBindBuffer(GL_ARRAY_BUFFER, mVbo);
      glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex),
                   mesh.vertices.data(), GL_DYNAMIC_DRAW);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                   mesh.indices.data(), GL_DYNAMIC_DRAW);

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));

      const Mat4 model = source->GetModelMatrix();
      float normalMatrix[9];
      model.NormalMatrix(normalMatrix);

      float baseColor[3];
      float metallic, roughness, opacity;
      int shading;
      source->GetMaterial(baseColor, metallic, roughness, opacity, shading);

      const unsigned int surface = source->GetSurfaceTexture();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, surface);
      glUniform1i(glGetUniformLocation(mProgram, "uTexture"), 0);
      glUniform1i(glGetUniformLocation(mProgram, "uHasTexture"), surface != 0 ? 1 : 0);

      // Instanced sources upload a transform per copy and draw them all at
      // once; ten thousand instances stay a single draw call.
      auto* instancer = dynamic_cast<InstanceOnPointsNode*>(source);
      const bool instanced = instancer != nullptr && instancer->InstanceCount() > 0;
      glUniform1i(glGetUniformLocation(mProgram, "uInstanced"), instanced ? 1 : 0);
      if (instanced)
      {
         const std::vector<Mat4>& xforms = instancer->InstanceTransforms();
         glBindBuffer(GL_ARRAY_BUFFER, mInstanceVbo);
         glBufferData(GL_ARRAY_BUFFER, xforms.size() * sizeof(Mat4), xforms.data(), GL_DYNAMIC_DRAW);
         // a mat4 attribute is four consecutive vec4 slots
         for (int col = 0; col < 4; col++)
         {
            const unsigned int loc = 3 + col;
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                  (void*)(size_t)(col * 4 * sizeof(float)));
            glVertexAttribDivisor(loc, 1);
         }
      }
      else
      {
         for (int col = 0; col < 4; col++)
         {
            glDisableVertexAttribArray(3 + col);
            glVertexAttribDivisor(3 + col, 0);
         }
      }

      glUniformMatrix4fv(glGetUniformLocation(mProgram, "uModel"), 1, GL_FALSE, model.m);
      glUniformMatrix3fv(glGetUniformLocation(mProgram, "uNormalMatrix"), 1, GL_FALSE, normalMatrix);
      glUniform3fv(glGetUniformLocation(mProgram, "uBaseColor"), 1, baseColor);
      glUniform1f(glGetUniformLocation(mProgram, "uMetallic"), metallic);
      glUniform1f(glGetUniformLocation(mProgram, "uRoughness"), roughness);
      glUniform1f(glGetUniformLocation(mProgram, "uOpacity"), opacity);
      glUniform1i(glGetUniformLocation(mProgram, "uShading"), shading);

      if (instanced)
      {
         glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mesh.indices.size(), GL_UNSIGNED_INT,
                                 nullptr, (GLsizei)instancer->InstanceCount());
         mLastTriangles += (mesh.indices.size() / 3) * instancer->InstanceCount();
      }
      else
      {
         glDrawElements(GL_TRIANGLES, (GLsizei)mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
         mLastTriangles += mesh.indices.size() / 3;
      }
      mLastDrawCalls++;

   }

   glBindVertexArray(0);
   glUseProgram(0);

   // --- restore, or every 2D node after this draws wrongly ------------
   if (!prevDepth) glDisable(GL_DEPTH_TEST);
   if (!prevCull) glDisable(GL_CULL_FACE);
   if (!prevBlend) glDisable(GL_BLEND);
   if (prevScissor)
   {
      glEnable(GL_SCISSOR_TEST);
      glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
   }
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}
