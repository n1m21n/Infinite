#include "NodeViewport.h"

#include "gl3.h"
#include "BenchReport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "GeometryOpNodes.h"

namespace
{
   // A longer "lens" than Render3DNode's main-viewport default (45 deg) -
   // this solo thumbnail auto-frames every mesh snugly against the frustum
   // edge, and at a wide FOV that snug framing visibly keystones tall
   // symmetric shapes (a Cylinder's near rim projects wider than its far
   // rim). Narrower FOV + a proportionally longer auto-frame distance keeps
   // the object the same apparent size while reading closer to orthographic.
   constexpr float kFovDegrees = 28.0f;

   // Position + normal + UV, no instancing. Cheapest lit look that still
   // reads as a solid: ambient + one fixed directional light + a small
   // specular kick so curvature is visible on a flat-lit preview. UV is
   // carried through so the albedo texture patched into a node (if any) shows
   // up here the same way it does in Render3DNode's final output.
   const char* kVertSrc =
      "#version 150\n"
      "in vec3 aPos;\n"
      "in vec3 aNormal;\n"
      "in vec2 aUv;\n"
      "in vec3 aVertexColor;\n"
      "in mat4 aInstance;\n"
      "uniform mat4 uModel;\n"
      "uniform mat4 uViewProj;\n"
      "uniform mat3 uNormalMatrix;\n"
      "uniform int uInstanced;\n"
      "uniform int uHasVertexColor;\n"
      // GPU instances (Instance on Points over GPU points): each copy's
      // matrix from its point texel, as Render3DNode's vertex shader does.
      "uniform int uGpuPoints;\n"
      "uniform sampler2D uPointPos;\n"
      "uniform int uPointTexW;\n"
      "uniform mat4 uPointModel;\n"
      "uniform float uPointSizeScale;\n"
      "uniform mat4 uInstanceGroup;\n"
      "uniform mat4 uShapeModel;\n"
      "out vec3 vNormal;\n"
      "out vec3 vWorldPos;\n"
      "out vec2 vUv;\n"
      "out vec3 vVertexColor;\n"
      "void main() {\n"
      "   mat4 model = (uInstanced == 1) ? aInstance : uModel;\n"
      "   if (uGpuPoints == 1) {\n"
      "      vec4 point = texelFetch(uPointPos, ivec2(gl_InstanceID % uPointTexW, gl_InstanceID / uPointTexW), 0);\n"
      "      float s = point.w * uPointSizeScale;\n"
      "      vec3 p = (uPointModel * vec4(point.xyz, 1.0)).xyz;\n"
      "      model = uInstanceGroup * mat4(vec4(s, 0.0, 0.0, 0.0), vec4(0.0, s, 0.0, 0.0),\n"
      "                                    vec4(0.0, 0.0, s, 0.0), vec4(p, 1.0)) * uShapeModel;\n"
      "   }\n"
      "   vec4 world = model * vec4(aPos, 1.0);\n"
      "   vWorldPos = world.xyz;\n"
      // Per-instance matrices skip the CPU-computed inverse-transpose
      // uNormalMatrix uses for non-uniform scale - fine for a thumbnail, and
      // the same shortcut Render3DNode's shadow pass already takes.
      "   mat3 nrm = (uInstanced == 1) ? mat3(model) : uNormalMatrix;\n"
      "   vNormal = normalize(nrm * aNormal);\n"
      "   vUv = aUv;\n"
      "   vVertexColor = (uHasVertexColor == 1) ? aVertexColor : vec3(1.0);\n"
      "   gl_Position = uViewProj * world;\n"
      "}\n";

   const char* kFragSrc =
      "#version 150\n"
      "in vec3 vNormal;\n"
      "in vec3 vWorldPos;\n"
      "in vec2 vUv;\n"
      "in vec3 vVertexColor;\n"
      "out vec4 fragColor;\n"
      "uniform vec3 uBaseColor;\n"
      "uniform vec3 uLightDir;\n"
      "uniform vec3 uCamPos;\n"
      "uniform sampler2D uTexture;\n"
      "uniform int uHasTexture;\n"
      "uniform sampler2D uPaint;\n"
      "uniform int uHasPaint;\n"
      "void main() {\n"
      "   vec3 n = normalize(vNormal);\n"
      "   if (!gl_FrontFacing) n = -n;\n"
      "   vec3 base = uBaseColor * vVertexColor;\n"
      "   if (uHasTexture == 1) base *= texture(uTexture, vUv).rgb;\n"
      "   if (uHasPaint == 1) { vec4 pt = texture(uPaint, vUv); base = mix(base, pt.rgb, pt.a); }\n"
      "   vec3 l = normalize(uLightDir);\n"
      "   float ndl = max(dot(n, l), 0.0);\n"
      "   vec3 viewDir = normalize(uCamPos - vWorldPos);\n"
      "   vec3 halfVec = normalize(l + viewDir);\n"
      "   float spec = pow(max(dot(n, halfVec), 0.0), 24.0) * 0.15;\n"
      "   vec3 color = base * (0.35 + ndl * 0.8) + vec3(spec);\n"
      "   fragColor = vec4(color, 1.0);\n"
      "}\n";

   // Selection overlay - flat, unlit tint so it reads as an annotation rather
   // than a material. Render3DNode has no selection overlay of its own (the
   // highlight only ever appears here and in the viewport-panel cards), so
   // this isn't mirroring anything - it just needs the same instanced/
   // un-instanced model choice kVertSrc uses, or an instanced source draws the
   // highlight once at unit scale instead of on every instance.
   const char* kSelectVertSrc =
      "#version 150\n"
      "in vec3 aPos;\n"
      "in mat4 aInstance;\n"
      "uniform mat4 uModel;\n"
      "uniform mat4 uViewProj;\n"
      "uniform int uInstanced;\n"
      "void main() {\n"
      "   mat4 model = (uInstanced == 1) ? aInstance : uModel;\n"
      "   gl_Position = uViewProj * model * vec4(aPos, 1.0);\n"
      "}\n";

   const char* kSelectFragSrc =
      "#version 150\n"
      "out vec4 fragColor;\n"
      "uniform vec3 uColor;\n"
      "uniform float uOpacity;\n"
      "void main() { fragColor = vec4(uColor, uOpacity); }\n";

   // Unlit round point sprites for a pure point-cloud source (Particle
   // System) - no normals to light, so this skips kVertSrc/kFragSrc entirely
   // rather than faking a normal. Colour comes straight from each Particle
   // (already carries the start/end colour gradient the sim computed).
   const char* kPointVertSrc =
      "#version 150\n"
      "in vec3 aPos;\n"
      "in vec3 aColor;\n"
      "uniform mat4 uViewProj;\n"
      "uniform float uPointSize;\n"
      "out vec3 vColor;\n"
      "void main() {\n"
      "   gl_Position = uViewProj * vec4(aPos, 1.0);\n"
      "   gl_PointSize = uPointSize;\n"
      "   vColor = aColor;\n"
      "}\n";

   const char* kPointFragSrc =
      "#version 150\n"
      "in vec3 vColor;\n"
      "out vec4 fragColor;\n"
      "void main() {\n"
      "   vec2 c = gl_PointCoord - vec2(0.5);\n"
      "   if (dot(c, c) > 0.25) discard;\n"
      "   fragColor = vec4(vColor, 1.0);\n"
      "}\n";

   // The same points from GPU textures (core/GpuPoints.h): point i at texel
   // (i % uPointTexW, i / uPointTexW); w == 0 hides it. Positions go through
   // pointMatrix only, as the CPU path above uses each particle's position
   // as is (a Transform's CPU cloud has its matrix baked in).
   const char* kGpuPointVertSrc =
      "#version 150\n"
      "uniform sampler2D uPointPos;\n"
      "uniform sampler2D uPointColor;\n"
      "uniform int uPointTexW;\n"
      "uniform mat4 uPointMatrix;\n"
      "uniform mat4 uViewProj;\n"
      "uniform float uPointSize;\n"
      "out vec3 vColor;\n"
      "void main() {\n"
      "   ivec2 texel = ivec2(gl_VertexID % uPointTexW, gl_VertexID / uPointTexW);\n"
      "   vec4 point = texelFetch(uPointPos, texel, 0);\n"
      "   vColor = texelFetch(uPointColor, texel, 0).rgb;\n"
      "   if (point.w <= 0.0) {\n"
      "      gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n" // outside the clip volume
      "      gl_PointSize = 1.0;\n"
      "      return;\n"
      "   }\n"
      "   gl_Position = uViewProj * (uPointMatrix * vec4(point.xyz, 1.0));\n"
      "   gl_PointSize = uPointSize;\n"
      "}\n";

   // Bound in place of a real surface texture when a geometry has none, same
   // reason and technique as Render3DNode's WhiteTexture() (Geometry3DNodes.cpp)
   // - an unbound sampler reads as a driver warning on macOS.
   unsigned int WhiteTexture()
   {
      static unsigned int sTex = 0;
      if (sTex == 0)
      {
         const unsigned char white[4] = { 255, 255, 255, 255 };
         glGenTextures(1, &sTex);
         glBindTexture(GL_TEXTURE_2D, sTex);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      }
      return sTex;
   }

   unsigned int CompileShader(GLenum type, const char* src, const char* what)
   {
      unsigned int shader = glCreateShader(type);
      glShaderSource(shader, 1, &src, nullptr);
      glCompileShader(shader);
      GLint ok = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
      if (!ok)
      {
         char log[1024];
         glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
         fprintf(stderr, "NodeViewport %s shader error: %s\n", what, log);
         glDeleteShader(shader);
         return 0u;
      }
      return shader;
   }

   unsigned int LinkLitProgram()
   {
      const unsigned int vert = CompileShader(GL_VERTEX_SHADER, kVertSrc, "lit");
      const unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, kFragSrc, "lit");
      if (vert == 0 || frag == 0)
         return 0u;

      unsigned int program = glCreateProgram();
      glBindAttribLocation(program, 0, "aPos");
      glBindAttribLocation(program, 1, "aNormal");
      // A mat4 attribute occupies four consecutive locations (2-5), same
      // layout Render3DNode uses for its instance transform attribute.
      glBindAttribLocation(program, 2, "aInstance");
      glBindAttribLocation(program, 6, "aUv");
      glBindAttribLocation(program, 7, "aVertexColor");
      glAttachShader(program, vert);
      glAttachShader(program, frag);
      glLinkProgram(program);
      glDeleteShader(vert);
      glDeleteShader(frag);

      GLint linked = 0;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      if (!linked)
      {
         char log[1024];
         glGetProgramInfoLog(program, sizeof(log), nullptr, log);
         fprintf(stderr, "NodeViewport lit link error: %s\n", log);
         glDeleteProgram(program);
         return 0u;
      }
      return program;
   }

   unsigned int LinkSelectionProgram()
   {
      const unsigned int vert = CompileShader(GL_VERTEX_SHADER, kSelectVertSrc, "selection");
      const unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, kSelectFragSrc, "selection");
      if (vert == 0 || frag == 0)
         return 0u;

      unsigned int program = glCreateProgram();
      glBindAttribLocation(program, 0, "aPos");
      // Attribs 2-5 are free in mSelVao (it otherwise uses only attrib 0) -
      // same mat4-occupies-four-locations layout as LinkLitProgram's aInstance.
      glBindAttribLocation(program, 2, "aInstance");
      glAttachShader(program, vert);
      glAttachShader(program, frag);
      glLinkProgram(program);
      glDeleteShader(vert);
      glDeleteShader(frag);

      GLint linked = 0;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      if (!linked)
      {
         char log[1024];
         glGetProgramInfoLog(program, sizeof(log), nullptr, log);
         fprintf(stderr, "NodeViewport selection link error: %s\n", log);
         glDeleteProgram(program);
         return 0u;
      }
      return program;
   }

   unsigned int LinkPointProgram(const char* vertSrc = kPointVertSrc)
   {
      const unsigned int vert = CompileShader(GL_VERTEX_SHADER, vertSrc, "point");
      const unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, kPointFragSrc, "point");
      if (vert == 0 || frag == 0)
         return 0u;

      unsigned int program = glCreateProgram();
      glBindAttribLocation(program, 0, "aPos");
      glBindAttribLocation(program, 1, "aColor");
      glAttachShader(program, vert);
      glAttachShader(program, frag);
      glLinkProgram(program);
      glDeleteShader(vert);
      glDeleteShader(frag);

      GLint linked = 0;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      if (!linked)
      {
         char log[1024];
         glGetProgramInfoLog(program, sizeof(log), nullptr, log);
         fprintf(stderr, "NodeViewport point link error: %s\n", log);
         glDeleteProgram(program);
         return 0u;
      }
      return program;
   }
}

NodeViewport::~NodeViewport()
{
   ReleaseFbo();
   if (mVbo != 0) { Bench::GpuMem::ReleaseBuffer(mVbo); glDeleteBuffers(1, &mVbo); }
   if (mIbo != 0) { Bench::GpuMem::ReleaseBuffer(mIbo); glDeleteBuffers(1, &mIbo); }
   if (mVao != 0) glDeleteVertexArrays(1, &mVao);
   if (mSelIbo != 0) { Bench::GpuMem::ReleaseBuffer(mSelIbo); glDeleteBuffers(1, &mSelIbo); }
   if (mSelVao != 0) glDeleteVertexArrays(1, &mSelVao);
   if (mInstanceVbo != 0) { Bench::GpuMem::ReleaseBuffer(mInstanceVbo); glDeleteBuffers(1, &mInstanceVbo); }
   if (mVertexColorVbo != 0) { Bench::GpuMem::ReleaseBuffer(mVertexColorVbo); glDeleteBuffers(1, &mVertexColorVbo); }
   if (mPointVbo != 0) { Bench::GpuMem::ReleaseBuffer(mPointVbo); glDeleteBuffers(1, &mPointVbo); }
   if (mPointVao != 0) glDeleteVertexArrays(1, &mPointVao);
   if (mGpuPointVao != 0) glDeleteVertexArrays(1, &mGpuPointVao);
   if (mSelInstanceOverrideVbo != 0) { Bench::GpuMem::ReleaseBuffer(mSelInstanceOverrideVbo); glDeleteBuffers(1, &mSelInstanceOverrideVbo); }
}

bool NodeViewport::EnsureFbo(int w, int h)
{
   if (mFbo != 0 && mWidth == w && mHeight == h)
      return true;

   ReleaseFbo();
   GLUtil::NoteFboAllocation();

   glGenTextures(1, &mColorTex);
   glBindTexture(GL_TEXTURE_2D, mColorTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   Bench::GpuMem::RecordTexture(mColorTex, Bench::GpuMemCategory::RenderTargets, w, h, GL_RGBA8, false, "NodeViewport");

   glGenRenderbuffers(1, &mDepthBuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
   Bench::GpuMem::RecordRenderbuffer(mDepthBuffer, Bench::GpuMemCategory::RenderTargets, w, h, GL_DEPTH_COMPONENT24, 1, "NodeViewport");

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
      fprintf(stderr, "NodeViewport framebuffer incomplete: 0x%x\n", status);
      ReleaseFbo();
      return false;
   }

   mWidth = w;
   mHeight = h;
   return true;
}

void NodeViewport::ReleaseFbo()
{
   if (mColorTex != 0) { Bench::GpuMem::ReleaseTexture(mColorTex); glDeleteTextures(1, &mColorTex); mColorTex = 0; }
   if (mDepthBuffer != 0) { Bench::GpuMem::ReleaseRenderbuffer(mDepthBuffer); glDeleteRenderbuffers(1, &mDepthBuffer); mDepthBuffer = 0; }
   if (mFbo != 0) { glDeleteFramebuffers(1, &mFbo); mFbo = 0; }
   mWidth = mHeight = 0;
}

void NodeViewport::UploadMesh(const Mesh& mesh, unsigned long long revision)
{
   if (mVao == 0)
   {
      glGenVertexArrays(1, &mVao);
      glGenBuffers(1, &mVbo);
      glGenBuffers(1, &mIbo);
   }

   if (mMeshRevision == revision)
      return;

   glBindVertexArray(mVao);
   glBindBuffer(GL_ARRAY_BUFFER, mVbo);
   glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex),
                mesh.vertices.data(), GL_STATIC_DRAW);
   Bench::GpuMem::RecordBuffer(mVbo, Bench::GpuMemCategory::MeshBuffers, mesh.vertices.size() * sizeof(Vertex), "NodeViewport_Vbo");
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                mesh.indices.data(), GL_STATIC_DRAW);
   Bench::GpuMem::RecordBuffer(mIbo, Bench::GpuMemCategory::MeshBuffers, mesh.indices.size() * sizeof(unsigned int), "NodeViewport_Ibo");

   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
   glEnableVertexAttribArray(6);
   glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));

   mHasVertexColor = mesh.HasVertexColor();
   if (mHasVertexColor)
   {
      if (mVertexColorVbo == 0)
         glGenBuffers(1, &mVertexColorVbo);
      glBindBuffer(GL_ARRAY_BUFFER, mVertexColorVbo);
      glBufferData(GL_ARRAY_BUFFER, mesh.vertexColor.size() * sizeof(float),
                   mesh.vertexColor.data(), GL_STATIC_DRAW);
      Bench::GpuMem::RecordBuffer(mVertexColorVbo, Bench::GpuMemCategory::MeshBuffers, mesh.vertexColor.size() * sizeof(float), "NodeViewport_VertexColor");
      glEnableVertexAttribArray(7);
      glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
   }
   else
   {
      glDisableVertexAttribArray(7);
   }
   glBindVertexArray(0);

   mLo[0] = mLo[1] = mLo[2] = 1e30f;
   mHi[0] = mHi[1] = mHi[2] = -1e30f;
   for (const Vertex& v : mesh.vertices)
   {
      const float p[3] = { v.px, v.py, v.pz };
      for (int k = 0; k < 3; k++)
      {
         if (!std::isfinite(p[k]))
            continue;
         mLo[k] = std::min(mLo[k], p[k]);
         mHi[k] = std::max(mHi[k], p[k]);
      }
   }
   mHasBounds = mLo[0] <= mHi[0];

   mMeshRevision = revision;
   mIndexCount = (int)mesh.indices.size();
   mVertexCount = (int)mesh.vertices.size();
}

void NodeViewport::UploadPoints(const std::vector<Particle>& points, unsigned long long revision)
{
   if (mPointVao == 0)
   {
      glGenVertexArrays(1, &mPointVao);
      glGenBuffers(1, &mPointVbo);
   }

   if (mLastPointRevision == revision)
      return;

   struct PointVertex { float px, py, pz, r, g, b; };
   std::vector<PointVertex> verts;
   verts.reserve(points.size());

   mLo[0] = mLo[1] = mLo[2] = 1e30f;
   mHi[0] = mHi[1] = mHi[2] = -1e30f;
   for (const Particle& p : points)
   {
      if (!p.alive)
         continue;
      verts.push_back({ p.px, p.py, p.pz, p.r, p.g, p.b });
      const float pos[3] = { p.px, p.py, p.pz };
      for (int k = 0; k < 3; k++)
      {
         if (!std::isfinite(pos[k]))
            continue;
         mLo[k] = std::min(mLo[k], pos[k]);
         mHi[k] = std::max(mHi[k], pos[k]);
      }
   }
   mHasBounds = mLo[0] <= mHi[0];

   glBindVertexArray(mPointVao);
   glBindBuffer(GL_ARRAY_BUFFER, mPointVbo);
   glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(PointVertex), verts.data(), GL_DYNAMIC_DRAW);
   Bench::GpuMem::RecordBuffer(mPointVbo, Bench::GpuMemCategory::MeshBuffers, verts.size() * sizeof(PointVertex), "NodeViewport_PointVbo");
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void*)0);
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void*)(3 * sizeof(float)));
   glBindVertexArray(0);

   mLastPointRevision = revision;
   mPointCount = (int)verts.size();
}

void NodeViewport::UpdateSelectionBuffer(const Mesh& mesh, unsigned long long revision)
{
   if (mSelRevision == revision)
      return;
   mSelRevision = revision;
   mSelIndexCount = 0;

   // Empty mask means "no Select upstream" - nothing to annotate, same rule
   // Render3DNode::UpdateSelectionBuffer uses (Geometry3DNodes.cpp).
   if (mesh.faceMask.empty())
      return;

   const size_t faces = mesh.FaceCount();
   std::vector<unsigned int> selected;
   selected.reserve(faces * 3);
   for (size_t f = 0; f < faces; f++)
   {
      if (f >= mesh.faceMask.size() || mesh.faceMask[f] == 0)
         continue;
      selected.push_back(mesh.indices[f * 3]);
      selected.push_back(mesh.indices[f * 3 + 1]);
      selected.push_back(mesh.indices[f * 3 + 2]);
   }
   if (selected.empty())
      return;

   if (mSelVao == 0)
      glGenVertexArrays(1, &mSelVao);
   if (mSelIbo == 0)
      glGenBuffers(1, &mSelIbo);

   glBindVertexArray(mSelVao);
   glBindBuffer(GL_ARRAY_BUFFER, mVbo);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mSelIbo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, selected.size() * sizeof(unsigned int),
                selected.data(), GL_STATIC_DRAW);
   Bench::GpuMem::RecordBuffer(mSelIbo, Bench::GpuMemCategory::MeshBuffers, selected.size() * sizeof(unsigned int), "NodeViewport_SelIbo");
   glBindVertexArray(0);

   mSelIndexCount = (int)selected.size();
}

void NodeViewport::Orbit(SharedViewportCamera& cam, float dAzimuth, float dElevation)
{
   cam.azimuth -= dAzimuth;
   // Clamped short of the poles, same reason as Render3DNode's embedded
   // preview in main.cpp: at the poles the view matrix's up vector goes
   // parallel to the view direction and the image rolls. 85.9437 deg is the
   // old 1.5 rad clamp converted to this struct's degree convention.
   cam.elevation = std::max(-85.9437f, std::min(cam.elevation + dElevation, 85.9437f));
   mUserAdjusted = true;
}

void NodeViewport::Zoom(float wheelDelta)
{
   mDistance = std::max(0.05f, std::min(mDistance * (1.0f - wheelDelta * 0.15f), 500.0f));
   mUserAdjusted = true;
}

void NodeViewport::RefreshGroupPreview(IGeometrySource* group)
{
   if (BakeGroupMesh(group, mGroupPreview.signature, mGroupPreview.mesh) || mGroupPreview.revision == 0)
      mGroupPreview.revision = NextMeshRevision();
}

const Mesh& NodeViewport::GroupPreviewMesh(IGeometrySource* group)
{
   RefreshGroupPreview(group);
   return mGroupPreview.mesh;
}


// ---- picking ---------------------------------------------------------------

namespace
{
   // Moller-Trumbore. t out; u,v are the barycentric weights of b and c.
   bool RayTri(const float o[3], const float d[3], const float a[3], const float b[3], const float c[3], float& t, float& u, float& v)
   {
      const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
      const float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
      const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
      if (std::fabs(det) < 1e-12f)
         return false;
      const float inv = 1.0f / det;
      const float s[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
      u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
      // A small slack so a ray along a shared edge cannot slip between the two triangles on float error.
      const float kE = 1e-5f;
      if (u < -kE || u > 1.0f + kE)
         return false;
      const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
      v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
      if (v < -kE || u + v > 1.0f + kE)
         return false;
      t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
      return t > 1e-5f;
   }

   void ToWorld(const Mat4& m, const Vertex& v, float out[3])
   {
      for (int k = 0; k < 3; k++)
         out[k] = m.m[k] * v.px + m.m[4 + k] * v.py + m.m[8 + k] * v.pz + m.m[12 + k];
   }
}

bool NodeViewport::PickSurface(IGeometrySource* geo, const SharedViewportCamera& cam, int w, int h, float px, float pyTop,
                               int skinSide, SurfaceHit& out)
{
   if (geo == nullptr || geo->GroupChildCount() > 0 || !mFramed || w < 1 || h < 1)
      return false;
   const Mesh& mesh = geo->GetMesh();
   if (mesh.indices.size() < 3)
      return false;
   const Mat4 model = geo->GetModelMatrix();
   const float aspect = (float)w / (float)h;
   const float az = cam.azimuth * 3.14159265f / 180.0f, el = cam.elevation * 3.14159265f / 180.0f;
   const float ce = std::cos(el);
   const float eye[3] = { mTarget[0] + mDistance * ce * std::cos(az), mTarget[1] + mDistance * std::sin(el),
                          mTarget[2] + mDistance * ce * std::sin(az) };
   const float up[3] = { 0.0f, 1.0f, 0.0f };
   const Mat4 view = Mat4::LookAt(eye, mTarget, up);
   const Mat4 proj = Mat4::Perspective(kFovDegrees * 3.14159265f / 180.0f, aspect, 0.05f, std::max(50.0f, mDistance * 4.0f));
   const Mat4 viewProj = Mat4::Multiply(proj, view);
   // The ray: from the eye through the pixel, along the view's own axes.
   float f[3] = { mTarget[0] - eye[0], mTarget[1] - eye[1], mTarget[2] - eye[2] };
   const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
   for (float& x : f)
      x /= std::max(1e-6f, fl);
   float r[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
   const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
   for (float& x : r)
      x /= std::max(1e-6f, rl);
   const float u3[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
   const float th = std::tan(kFovDegrees * 0.5f * 3.14159265f / 180.0f);
   const float nx = (2.0f * px / (float)w - 1.0f) * th * aspect, ny = (1.0f - 2.0f * pyTop / (float)h) * th;
   float d[3] = { f[0] + r[0] * nx + u3[0] * ny, f[1] + r[1] * nx + u3[1] * ny, f[2] + r[2] * nx + u3[2] * ny };
   const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
   for (float& x : d)
      x /= std::max(1e-6f, dl);
   return RaySurface(geo, eye, d, viewProj, w, h, skinSide, out);
}

bool NodeViewport::RaySurface(IGeometrySource* geo, const float eye[3], const float d[3], const Mat4& viewProj, int w, int h,
                              int skinSide, SurfaceHit& out)
{
   if (geo == nullptr || geo->GroupChildCount() > 0 || w < 1 || h < 1)
      return false;
   const Mesh& mesh = geo->GetMesh();
   if (mesh.indices.size() < 3)
      return false;
   const Mat4 model = geo->GetModelMatrix();
   const size_t tris = mesh.indices.size() / 3;
   float best = 1e30f, bu = 0, bv = 0;
   size_t bi = tris;
   for (size_t t = 0; t < tris; t++)
   {
      float a[3], b[3], c[3], tt, uu, vv;
      ToWorld(model, mesh.vertices[mesh.indices[t * 3]], a);
      ToWorld(model, mesh.vertices[mesh.indices[t * 3 + 1]], b);
      ToWorld(model, mesh.vertices[mesh.indices[t * 3 + 2]], c);
      // Back faces are culled in the render, so the pick skips them too.
      const float g1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, g2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
      const float gn[3] = { g1[1] * g2[2] - g1[2] * g2[1], g1[2] * g2[0] - g1[0] * g2[2], g1[0] * g2[1] - g1[1] * g2[0] };
      if (gn[0] * d[0] + gn[1] * d[1] + gn[2] * d[2] > 0.0f)
         continue;
      if (RayTri(eye, d, a, b, c, tt, uu, vv) && tt < best)
      {
         best = tt;
         bu = uu;
         bv = vv;
         bi = t;
      }
   }
   if (bi == tris)
      return false;
   const Vertex& A = mesh.vertices[mesh.indices[bi * 3]];
   const Vertex& B = mesh.vertices[mesh.indices[bi * 3 + 1]];
   const Vertex& C = mesh.vertices[mesh.indices[bi * 3 + 2]];
   const float wa = 1.0f - bu - bv;
   out.tri = (int)bi;
   out.uv[0] = wa * A.u + bu * B.u + bv * C.u;
   out.uv[1] = wa * A.v + bu * B.v + bv * C.v;
   out.pos[0] = wa * A.px + bu * B.px + bv * C.px;
   out.pos[1] = wa * A.py + bu * B.py + bv * C.py;
   out.pos[2] = wa * A.pz + bu * B.pz + bv * C.pz;
   // Facing: the geometric normal against the way back to the eye.
   float a[3], b[3], c[3];
   ToWorld(model, A, a);
   ToWorld(model, B, b);
   ToWorld(model, C, c);
   const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
   float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
   const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
   out.facing = nl > 1e-12f ? std::fabs(n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / nl : 0.0f;
   // Skin texels per screen pixel: the triangle's screen footprint against its UV footprint.
   auto screen = [&](const float p[3], float s[2]) {
      const float* m = viewProj.m;
      const float cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
      const float cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
      const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
      const float iw = 1.0f / std::max(1e-6f, cw);
      s[0] = (cx * iw * 0.5f + 0.5f) * (float)w;
      s[1] = (0.5f - cy * iw * 0.5f) * (float)h;
   };
   float sa[2], sb[2], sc[2];
   screen(a, sa);
   screen(b, sb);
   screen(c, sc);
   const float scr = std::fabs((sb[0] - sa[0]) * (sc[1] - sa[1]) - (sb[1] - sa[1]) * (sc[0] - sa[0]));
   const float uvA = std::fabs((B.u - A.u) * (C.v - A.v) - (B.v - A.v) * (C.u - A.u));
   out.texelsPerPx = scr > 1e-6f ? std::sqrt(uvA / scr) * (float)skinSide : 0.0f;
   return true;
}

bool NodeViewport::ClosestSurface(IGeometrySource* geo, const float pos[3], SurfaceHit& out)
{
   if (geo == nullptr || geo->GroupChildCount() > 0)
      return false;
   const Mesh& mesh = geo->GetMesh();
   const size_t tris = mesh.indices.size() / 3;
   float best = 1e30f;
   for (size_t t = 0; t < tris; t++)
   {
      const Vertex& A = mesh.vertices[mesh.indices[t * 3]];
      const Vertex& B = mesh.vertices[mesh.indices[t * 3 + 1]];
      const Vertex& C = mesh.vertices[mesh.indices[t * 3 + 2]];
      // Closest point on a triangle (Ericson), in barycentric weights.
      const float ab[3] = { B.px - A.px, B.py - A.py, B.pz - A.pz }, ac[3] = { C.px - A.px, C.py - A.py, C.pz - A.pz };
      const float ap[3] = { pos[0] - A.px, pos[1] - A.py, pos[2] - A.pz };
      auto dot = [](const float x[3], const float y[3]) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; };
      float wb = 0, wc = 0;
      const float d1 = dot(ab, ap), d2 = dot(ac, ap);
      if (d1 <= 0 && d2 <= 0) { wb = 0; wc = 0; }
      else
      {
         const float bp[3] = { pos[0] - B.px, pos[1] - B.py, pos[2] - B.pz };
         const float d3 = dot(ab, bp), d4 = dot(ac, bp);
         const float cp[3] = { pos[0] - C.px, pos[1] - C.py, pos[2] - C.pz };
         const float d5 = dot(ab, cp), d6 = dot(ac, cp);
         const float vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
         if (d3 >= 0 && d4 <= d3) { wb = 1; wc = 0; }
         else if (vc <= 0 && d1 >= 0 && d3 <= 0) { wb = d1 / std::max(1e-12f, d1 - d3); wc = 0; }
         else if (d6 >= 0 && d5 <= d6) { wb = 0; wc = 1; }
         else if (vb <= 0 && d2 >= 0 && d6 <= 0) { wb = 0; wc = d2 / std::max(1e-12f, d2 - d6); }
         else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
         {
            wb = (d4 - d3) / std::max(1e-12f, (d4 - d3) + (d5 - d6));
            wc = 1.0f - wb;
         }
         else
         {
            const float den = 1.0f / std::max(1e-12f, va + vb + vc);
            wb = vb * den;
            wc = vc * den;
         }
      }
      const float wa = 1.0f - wb - wc;
      const float q[3] = { wa * A.px + wb * B.px + wc * C.px, wa * A.py + wb * B.py + wc * C.py, wa * A.pz + wb * B.pz + wc * C.pz };
      const float dd = (q[0] - pos[0]) * (q[0] - pos[0]) + (q[1] - pos[1]) * (q[1] - pos[1]) + (q[2] - pos[2]) * (q[2] - pos[2]);
      if (dd < best)
      {
         best = dd;
         out.tri = (int)t;
         out.uv[0] = wa * A.u + wb * B.u + wc * C.u;
         out.uv[1] = wa * A.v + wb * B.v + wc * C.v;
         std::copy(q, q + 3, out.pos);
      }
   }
   return out.tri >= 0;
}

unsigned int NodeViewport::Render(IGeometrySource* geo, const SharedViewportCamera& cam, int w, int h,
                                  bool forceRedraw)
{
   if (geo == nullptr)
      return 0;
   // A Group 3D has no mesh of its own - preview the children it carries.
   if (geo->GroupChildCount() > 0)
   {
      RefreshGroupPreview(geo);
      geo = &mGroupPreview;
   }
   const Mesh& mesh = geo->GetMesh();
   // HasGeometry(), not Empty(): a vertices-only mesh (Points to Vertices'
   // output, indices empty) has nothing to preview as triangles but is still
   // drawn below as GL_POINTS.
   const bool hasMesh = mesh.HasGeometry();

   // A pure point-cloud source (Particle System) has no mesh at all -
   // GetMesh() is a permanently-empty stub (SimulationNodes.h) - so it has to
   // be read through GetPointCloud() instead, same interface Render3DNode
   // uses for its own billboard-sprite draw. A dual source (Mesh to Points)
   // offers both; the mesh wins there, matching drawCloudSlot's "a cloud wins
   // over triangles" rule being moot since this thumbnail has no triangles to
   // lose in that case anyway - hasMesh already covers it.
   // GPU points first, and then the CPU cloud is never asked for: a GPU
   // producer builds it only on demand.
   const GpuPoints* gpuPoints = hasMesh ? nullptr : geo->GetGpuPoints();
   const bool useGpuPoints = gpuPoints != nullptr && gpuPoints->count > 0 && gpuPoints->posTex != 0 &&
                             gpuPoints->colorTex != 0 && gpuPoints->texW > 0;
   const std::vector<Particle>* cloud = (hasMesh || gpuPoints != nullptr) ? nullptr : geo->GetPointCloud();
   bool hasCloud = useGpuPoints;
   if (cloud != nullptr)
   {
      for (const Particle& p : *cloud)
      {
         if (p.alive) { hasCloud = true; break; }
      }
   }
   const bool usePointCloud = !hasMesh && hasCloud;

   if (!hasMesh && !hasCloud)
      return 0;

   w = std::max(16, w);
   h = std::max(16, h);

   const unsigned long long revision = useGpuPoints ? gpuPoints->revision
                                     : usePointCloud ? geo->PointCloudRevision() : geo->MeshRevision();
   const Mat4 pointMatrix = useGpuPoints ? gpuPoints->pointMatrix : Mat4::Identity();
   const Mat4 model = geo->GetModelMatrix();
   const Material material = geo->GetMaterial();
   const unsigned int surfaceTexture = geo->GetSurfaceTexture();
   const unsigned long long surfaceTextureRevision = geo->SurfaceTextureRevision();

   InstanceOnPointsNode* instancer = FindInstancer(geo);
   const bool instanced = instancer != nullptr && instancer->InstanceCount() > 0;
   const unsigned long long instanceRevision = instanced ? instancer->InstanceRevision() : 0;
   const Mat4 instanceGroupMatrix = instanced ? geo->GetInstanceGroupMatrix() : Mat4::Identity();

   // Position/rotation/scale sliders move GetModelMatrix() without touching
   // the mesh at all, so MeshRevision() alone would miss them - a static mesh
   // patch through a moving Transform node has to be caught here instead.
   const bool modelChanged = !(model == mLastModel) || !(pointMatrix == mLastPointMatrix);
   const bool materialChanged =
      material.color[0] != mLastMaterial.color[0] || material.color[1] != mLastMaterial.color[1] ||
      material.color[2] != mLastMaterial.color[2] || material.metallic != mLastMaterial.metallic ||
      material.roughness != mLastMaterial.roughness || material.opacity != mLastMaterial.opacity ||
      material.shading != mLastMaterial.shading ||
      material.emissionColor[0] != mLastMaterial.emissionColor[0] ||
      material.emissionColor[1] != mLastMaterial.emissionColor[1] ||
      material.emissionColor[2] != mLastMaterial.emissionColor[2] ||
      material.emission != mLastMaterial.emission ||
      material.ior != mLastMaterial.ior || material.transmission != mLastMaterial.transmission ||
      material.transmissionRoughness != mLastMaterial.transmissionRoughness ||
      material.specular != mLastMaterial.specular || material.clearcoat != mLastMaterial.clearcoat ||
      material.clearcoatRoughness != mLastMaterial.clearcoatRoughness ||
      material.subsurface != mLastMaterial.subsurface ||
      material.subsurfaceColor[0] != mLastMaterial.subsurfaceColor[0] ||
      material.subsurfaceColor[1] != mLastMaterial.subsurfaceColor[1] ||
      material.subsurfaceColor[2] != mLastMaterial.subsurfaceColor[2] ||
      material.subsurfaceRadius != mLastMaterial.subsurfaceRadius ||
      material.translucency != mLastMaterial.translucency;
   const bool cameraChanged = cam.azimuth != mLastAzimuth || cam.elevation != mLastElevation ||
                              mDistance != mLastDistance;
   const bool instancingChanged = instanced != mInstanceAttribsOn ||
      (instanced && (instanceRevision != mInstanceRevision || !(instanceGroupMatrix == mInstanceGroupMatrix)));
   // A texture patched into (or removed from) albedo does not bump the mesh
   // revision or touch the material struct, so it needs its own change gate -
   // same reasoning as BuildSceneSignature tracking texturedMaterial state in
   // Geometry3DNodes.cpp.
   const bool textureChanged = surfaceTexture != mLastSurfaceTexture ||
      surfaceTextureRevision != mLastSurfaceTextureRevision;
   const bool paintChanged = mPaintTex != mLastPaintTex || mPaintRev != mLastPaintRev;
   const bool needsRedraw = paintChanged || forceRedraw || !mHasRendered || revision != mLastRenderedRevision ||
                            cameraChanged || modelChanged || materialChanged || instancingChanged ||
                            textureChanged || w != mWidth || h != mHeight;
   if (!needsRedraw)
      return mColorTex;

   // Compiled once for the process and shared read-only by every viewport -
   // not per instance, so N nodes with viewports open cost one compile total.
   static const unsigned int program = LinkLitProgram();
   static const unsigned int pointProgram = LinkPointProgram();
   static const unsigned int gpuPointProgram = LinkPointProgram(kGpuPointVertSrc);
   if ((useGpuPoints && gpuPointProgram == 0) || (usePointCloud && !useGpuPoints && pointProgram == 0) ||
       (!usePointCloud && program == 0))
      return 0;
   static const unsigned int selectionProgram = LinkSelectionProgram();

   if (!EnsureFbo(w, h))
      return 0;

   if (useGpuPoints)
   {
      if (mGpuPointVao == 0)
         glGenVertexArrays(1, &mGpuPointVao);
      // Framing bounds from the producer's own box through pointMatrix, so
      // no point is ever read back.
      mHasBounds = gpuPoints->hasBounds;
      if (mHasBounds)
      {
         mLo[0] = mLo[1] = mLo[2] = 1e30f;
         mHi[0] = mHi[1] = mHi[2] = -1e30f;
         for (int corner = 0; corner < 8; corner++)
         {
            const float c[3] = {
               (corner & 1) ? gpuPoints->hi[0] : gpuPoints->lo[0],
               (corner & 2) ? gpuPoints->hi[1] : gpuPoints->lo[1],
               (corner & 4) ? gpuPoints->hi[2] : gpuPoints->lo[2]
            };
            for (int k = 0; k < 3; k++)
            {
               const float p = pointMatrix.m[k] * c[0] + pointMatrix.m[4 + k] * c[1] +
                               pointMatrix.m[8 + k] * c[2] + pointMatrix.m[12 + k];
               mLo[k] = std::min(mLo[k], p);
               mHi[k] = std::max(mHi[k], p);
            }
         }
      }
      // The CPU upload is stale now; rebuild it if the source falls back.
      mLastPointRevision = (unsigned long long)-1;
   }
   else if (usePointCloud)
      UploadPoints(*cloud, revision);
   else
      UploadMesh(mesh, revision);

   // A point cloud that has grown well past the bounds it was last framed
   // against gets re-framed - unless the user has already grabbed the camera
   // themselves (mUserAdjusted), in which case their framing always wins.
   // Static meshes never re-frame here: mFramed alone gates them, exactly as
   // before.
   float curBoundsRadius = 0.0f;
   if (mHasBounds)
   {
      const float dx = mHi[0] - mLo[0], dy = mHi[1] - mLo[1], dz = mHi[2] - mLo[2];
      curBoundsRadius = std::max(0.05f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
   }
   const bool shouldReframe = mHasBounds &&
      (!mFramed || (usePointCloud && !mUserAdjusted && curBoundsRadius > mFramedRadius * 1.5f));
   if (shouldReframe)
   {
      float lo[3] = { mLo[0], mLo[1], mLo[2] };
      float hi[3] = { mHi[0], mHi[1], mHi[2] };
      // Framing on the stamp mesh alone would zoom in on one un-instanced
      // copy - fit the whole scattered result instead by sweeping the stamp's
      // bounding corners through every instance transform.
      if (instanced)
      {
         lo[0] = lo[1] = lo[2] = 1e30f;
         hi[0] = hi[1] = hi[2] = -1e30f;
         for (const Mat4& xform : ResolveInstanceTransforms(geo, instancer))
         {
            const Mat4 m = instanceGroupMatrix == Mat4::Identity()
               ? xform : Mat4::Multiply(instanceGroupMatrix, xform);
            for (int corner = 0; corner < 8; corner++)
            {
               const float local[3] = {
                  (corner & 1) ? mHi[0] : mLo[0],
                  (corner & 2) ? mHi[1] : mLo[1],
                  (corner & 4) ? mHi[2] : mLo[2]
               };
               for (int row = 0; row < 3; row++)
               {
                  const float p = m.m[0 * 4 + row] * local[0] + m.m[1 * 4 + row] * local[1] +
                                  m.m[2 * 4 + row] * local[2] + m.m[3 * 4 + row];
                  lo[row] = std::min(lo[row], p);
                  hi[row] = std::max(hi[row], p);
               }
            }
         }
      }
      const float cx = (lo[0] + hi[0]) * 0.5f;
      const float cy = (lo[1] + hi[1]) * 0.5f;
      const float cz = (lo[2] + hi[2]) * 0.5f;
      const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
      const float radius = std::max(0.05f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
      mTarget[0] = cx; mTarget[1] = cy; mTarget[2] = cz;
      // Distance to just fit `radius` inside kFovDegrees, plus 10% headroom.
      // Framing this snugly at a wide FOV is what made symmetric shapes
      // (a Cylinder, a Prism) read as tapered/conical in the thumbnail: the
      // near rim sits noticeably closer to the eye than the far rim, so
      // perspective scales them differently even though the mesh itself has
      // no taper at all. See kFovDegrees below.
      mDistance = radius / std::tan(kFovDegrees * 0.5f * 3.14159265f / 180.0f) * 1.1f;
      mFramed = true;
      mFramedRadius = radius;
   }

   float normalMatrix[9];
   model.NormalMatrix(normalMatrix);

   const float aspect = (float)w / (float)h;
   // cam stores degrees (matched to Render3DNode's own camAzimuth/camElevation
   // convention) - convert to radians for the trig.
   const float azimuthRad = cam.azimuth * 3.14159265f / 180.0f;
   const float elevationRad = cam.elevation * 3.14159265f / 180.0f;
   const float ce = std::cos(elevationRad);
   const float eye[3] = {
      mTarget[0] + mDistance * ce * std::cos(azimuthRad),
      mTarget[1] + mDistance * std::sin(elevationRad),
      mTarget[2] + mDistance * ce * std::sin(azimuthRad)
   };
   const float up[3] = { 0.0f, 1.0f, 0.0f };
   const Mat4 view = Mat4::LookAt(eye, mTarget, up);
   const float farPlane = std::max(50.0f, mDistance * 4.0f);
   const Mat4 proj = Mat4::Perspective(kFovDegrees * 3.14159265f / 180.0f, aspect, 0.05f, farPlane);
   const Mat4 viewProj = Mat4::Multiply(proj, view);

   // --- save the GL state the rest of the app relies on, same pattern as
   // Render3DNode::CookIfNeeded (Geometry3DNodes.cpp) ---
   GLint prevFbo = 0, prevViewport[4];
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, prevViewport);
   const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
   const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
   const GLboolean prevBlend = glIsEnabled(GL_BLEND);
   const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
   GLint prevScissorBox[4];
   glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_STENCIL_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);

   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glViewport(0, 0, w, h);
   glClearColor(0.07f, 0.07f, 0.09f, 0.0f);
   glClearDepth(1.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glEnable(GL_CULL_FACE);
   glCullFace(GL_BACK);
   glDisable(GL_BLEND);

   if (useGpuPoints)
   {
      glUseProgram(gpuPointProgram);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, gpuPoints->posTex);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, gpuPoints->colorTex);
      glUniform1i(glGetUniformLocation(gpuPointProgram, "uPointPos"), 0);
      glUniform1i(glGetUniformLocation(gpuPointProgram, "uPointColor"), 1);
      glUniform1i(glGetUniformLocation(gpuPointProgram, "uPointTexW"), gpuPoints->texW);
      glUniformMatrix4fv(glGetUniformLocation(gpuPointProgram, "uPointMatrix"), 1, GL_FALSE, pointMatrix.m);
      glUniformMatrix4fv(glGetUniformLocation(gpuPointProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
      glUniform1f(glGetUniformLocation(gpuPointProgram, "uPointSize"), 6.0f); // as the CPU path below
      glEnable(GL_PROGRAM_POINT_SIZE);
      glBindVertexArray(mGpuPointVao);
      glDrawArrays(GL_POINTS, 0, gpuPoints->count);
      glBindVertexArray(0);
      glDisable(GL_PROGRAM_POINT_SIZE);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glUseProgram(0);
   }
   else if (usePointCloud)
   {
      glUseProgram(pointProgram);
      glUniformMatrix4fv(glGetUniformLocation(pointProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
      // Fixed screen-space size rather than deriving one from mDistance/fov:
      // this is a thumbnail, not a size-accurate preview, and a fixed size
      // keeps the cloud legible whether it's zoomed in or auto-framed wide.
      glUniform1f(glGetUniformLocation(pointProgram, "uPointSize"), 6.0f);
      glEnable(GL_PROGRAM_POINT_SIZE);
      glBindVertexArray(mPointVao);
      glDrawArrays(GL_POINTS, 0, mPointCount);
      glBindVertexArray(0);
      glDisable(GL_PROGRAM_POINT_SIZE);
      glUseProgram(0);
   }
   else
   {
   glUseProgram(program);
   glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, model.m);
   glUniformMatrix4fv(glGetUniformLocation(program, "uViewProj"), 1, GL_FALSE, viewProj.m);
   glUniformMatrix3fv(glGetUniformLocation(program, "uNormalMatrix"), 1, GL_FALSE, normalMatrix);
   glUniform3fv(glGetUniformLocation(program, "uBaseColor"), 1, material.color);
   glUniform3fv(glGetUniformLocation(program, "uCamPos"), 1, eye);
   const float lightDir[3] = { 0.4f, 0.85f, 0.35f };
   glUniform3fv(glGetUniformLocation(program, "uLightDir"), 1, lightDir);
   glUniform1i(glGetUniformLocation(program, "uInstanced"), instanced ? 1 : 0);
   glUniform1i(glGetUniformLocation(program, "uHasVertexColor"), mHasVertexColor ? 1 : 0);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, surfaceTexture != 0 ? surfaceTexture : WhiteTexture());
   glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
   glUniform1i(glGetUniformLocation(program, "uHasTexture"), surfaceTexture != 0 ? 1 : 0);
   glActiveTexture(GL_TEXTURE5);
   glBindTexture(GL_TEXTURE_2D, mPaintTex != 0 ? mPaintTex : WhiteTexture());
   glUniform1i(glGetUniformLocation(program, "uPaint"), 5);
   glUniform1i(glGetUniformLocation(program, "uHasPaint"), mPaintTex != 0 ? 1 : 0);
   glActiveTexture(GL_TEXTURE0);

   // A stamp with its own face selection keeps the CPU matrices: the overlay
   // below reads them through aInstance.
   const GpuPoints* gpuInstances =
      (instanced && mesh.faceMask.empty()) ? ResolveGpuInstances(geo, instancer) : nullptr;
   glUniform1i(glGetUniformLocation(program, "uGpuPoints"), gpuInstances != nullptr ? 1 : 0);

   glBindVertexArray(mVao);
   if (gpuInstances != nullptr)
   {
      if (mInstanceAttribsOn)
      {
         for (int col = 0; col < 4; col++)
         {
            glDisableVertexAttribArray(2 + col);
            glVertexAttribDivisor(2 + col, 0);
         }
         mInstanceAttribsOn = false;
      }
      // Unit 1: unit 0 holds the surface texture.
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, gpuInstances->posTex);
      glActiveTexture(GL_TEXTURE0);
      glUniform1i(glGetUniformLocation(program, "uPointPos"), 1);
      glUniform1i(glGetUniformLocation(program, "uPointTexW"), gpuInstances->texW);
      glUniformMatrix4fv(glGetUniformLocation(program, "uPointModel"), 1, GL_FALSE, gpuInstances->pointMatrix.m);
      glUniform1f(glGetUniformLocation(program, "uPointSizeScale"), instancer->GpuInstanceSizeScale());
      glUniformMatrix4fv(glGetUniformLocation(program, "uInstanceGroup"), 1, GL_FALSE, instanceGroupMatrix.m);
      const Mat4 shapeModel = instancer->GpuInstanceShapeModel();
      glUniformMatrix4fv(glGetUniformLocation(program, "uShapeModel"), 1, GL_FALSE, shapeModel.m);
      mInstanceCount = gpuInstances->count;
      glDrawElementsInstanced(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, nullptr,
                              (GLsizei)mInstanceCount);
   }
   else if (instanced)
   {
      if (mInstanceVbo == 0)
         glGenBuffers(1, &mInstanceVbo);
      if (instanceRevision != mInstanceRevision || !(instanceGroupMatrix == mInstanceGroupMatrix) ||
          !mInstanceAttribsOn || revision != mInstanceOverrideRevision)
      {
         const std::vector<Mat4>& xforms = ResolveInstanceTransforms(geo, instancer);
         std::vector<Mat4> composed;
         const std::vector<Mat4>* uploadXforms = &xforms;
         if (!(instanceGroupMatrix == Mat4::Identity()))
         {
            composed.reserve(xforms.size());
            for (const Mat4& x : xforms)
               composed.push_back(Mat4::Multiply(instanceGroupMatrix, x));
            uploadXforms = &composed;
         }
         glBindBuffer(GL_ARRAY_BUFFER, mInstanceVbo);
         glBufferData(GL_ARRAY_BUFFER, uploadXforms->size() * sizeof(Mat4), uploadXforms->data(),
                      GL_STATIC_DRAW);
         Bench::GpuMem::RecordBuffer(mInstanceVbo, Bench::GpuMemCategory::InstanceBuffers, uploadXforms->size() * sizeof(Mat4), "NodeViewport_InstanceVbo");
         for (int col = 0; col < 4; col++)
         {
            const unsigned int loc = 2 + col;
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                  (void*)(size_t)(col * 4 * sizeof(float)));
            glVertexAttribDivisor(loc, 1);
         }
         mInstanceRevision = instanceRevision;
         mInstanceGroupMatrix = instanceGroupMatrix;
         mInstanceOverrideRevision = revision;
         mInstanceCount = (int)uploadXforms->size();
         mInstanceAttribsOn = true;
      }
      glDrawElementsInstanced(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, nullptr,
                              (GLsizei)mInstanceCount);
   }
   else
   {
      if (mInstanceAttribsOn)
      {
         for (int col = 0; col < 4; col++)
         {
            glDisableVertexAttribArray(2 + col);
            glVertexAttribDivisor(2 + col, 0);
         }
         mInstanceAttribsOn = false;
      }
      if (mIndexCount == 0)
      {
         // This shader (unlike the usePointCloud path above, which sets
         // uPointSize and enables GL_PROGRAM_POINT_SIZE deliberately) never
         // writes gl_PointSize, so force the state off rather than leaving
         // gl_PointSize implicitly undefined if some earlier draw this frame
         // left it enabled.
         glDisable(GL_PROGRAM_POINT_SIZE);
         glDrawArrays(GL_POINTS, 0, mVertexCount);
      }
      else
         glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, nullptr);
   }
   glBindVertexArray(0);
   glUseProgram(0);
   } // !usePointCloud

   // Instance-domain selection (a Select downstream of an instancer) leaves
   // mesh.faceMask empty - there's nothing on the shared stamp to mask - and
   // instead masks entries in InstanceSelection(). See the fallback used
   // below: whole selected instances get tinted rather than a per-face
   // highlight, per local-prompts/instance-selection.md Part 1 step 5.
   const std::vector<unsigned char>* instanceSelection = instanced ? ResolveInstanceSelection(geo) : nullptr;
   const bool hasInstanceSelection = instanceSelection != nullptr && !instanceSelection->empty();

   if (!usePointCloud && selectionProgram != 0 && (!mesh.faceMask.empty() || hasInstanceSelection))
   {
      if (!mesh.faceMask.empty())
      {
         UpdateSelectionBuffer(mesh, revision);
      }
      else if (mSelRevision != revision)
      {
         // No faceMask to filter by - mSelIbo holds the whole stamp instead,
         // so every face of a selected instance draws, not a subset of faces.
         mSelRevision = revision;
         if (mSelVao == 0) glGenVertexArrays(1, &mSelVao);
         if (mSelIbo == 0) glGenBuffers(1, &mSelIbo);
         glBindVertexArray(mSelVao);
         glBindBuffer(GL_ARRAY_BUFFER, mVbo);
         glEnableVertexAttribArray(0);
         glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
         glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mSelIbo);
         glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                      mesh.indices.data(), GL_STATIC_DRAW);
         Bench::GpuMem::RecordBuffer(mSelIbo, Bench::GpuMemCategory::MeshBuffers, mesh.indices.size() * sizeof(unsigned int), "NodeViewport_SelIbo");
         glBindVertexArray(0);
         mSelIndexCount = (int)mesh.indices.size();
      }
      if (mSelIndexCount > 0)
      {
         glUseProgram(selectionProgram);
         glUniformMatrix4fv(glGetUniformLocation(selectionProgram, "uModel"), 1, GL_FALSE, model.m);
         glUniformMatrix4fv(glGetUniformLocation(selectionProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
         const float selColor[3] = { 1.0f, 0.42f, 0.12f };
         glUniform3fv(glGetUniformLocation(selectionProgram, "uColor"), 1, selColor);
         glUniform1f(glGetUniformLocation(selectionProgram, "uOpacity"), 0.55f);
         glUniform1i(glGetUniformLocation(selectionProgram, "uInstanced"), instanced ? 1 : 0);

         glDepthFunc(GL_LEQUAL);
         glEnable(GL_POLYGON_OFFSET_FILL);
         glPolygonOffset(-1.0f, -1.0f);
         glDepthMask(GL_FALSE);
         glEnable(GL_BLEND);
         glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

         glBindVertexArray(mSelVao);
         if (hasInstanceSelection)
         {
            // Filter to just the selected instances' composed transforms and
            // point mSelVao's instance attribs at that buffer instead of the
            // full mInstanceVbo - only entering this whole outer block is
            // already gated on an actual redraw, so rebuilding here every
            // time costs nothing extra.
            std::vector<Mat4> filtered;
            const std::vector<Mat4>& xforms = ResolveInstanceTransforms(geo, instancer);
            filtered.reserve(xforms.size());
            for (size_t i = 0; i < xforms.size(); i++)
            {
               if (i >= instanceSelection->size() || !(*instanceSelection)[i])
                  continue;
               filtered.push_back(instanceGroupMatrix == Mat4::Identity()
                  ? xforms[i] : Mat4::Multiply(instanceGroupMatrix, xforms[i]));
            }
            if (mSelInstanceOverrideVbo == 0)
               glGenBuffers(1, &mSelInstanceOverrideVbo);
            glBindBuffer(GL_ARRAY_BUFFER, mSelInstanceOverrideVbo);
            glBufferData(GL_ARRAY_BUFFER, filtered.size() * sizeof(Mat4), filtered.data(), GL_STATIC_DRAW);
            Bench::GpuMem::RecordBuffer(mSelInstanceOverrideVbo, Bench::GpuMemCategory::InstanceBuffers, filtered.size() * sizeof(Mat4), "NodeViewport_SelInstanceOverrideVbo");
            for (int col = 0; col < 4; col++)
            {
               const unsigned int loc = 2 + col;
               glEnableVertexAttribArray(loc);
               glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                     (void*)(size_t)(col * 4 * sizeof(float)));
               glVertexAttribDivisor(loc, 1);
            }
            mSelInstanceOverrideCount = (int)filtered.size();
            mSelInstanceAttribsOn = true;
            glDrawElementsInstanced(GL_TRIANGLES, mSelIndexCount, GL_UNSIGNED_INT, nullptr,
                                    (GLsizei)mSelInstanceOverrideCount);
         }
         else
         {
            // mInstanceVbo is generated/populated in the main draw block above,
            // which always runs before this - by the time we get here in an
            // instanced frame it's guaranteed non-zero. Configuring these attribs
            // here (not in UpdateSelectionBuffer, which can run before the main
            // block has ever created mInstanceVbo) avoids binding buffer 0.
            if (instanced && !mSelInstanceAttribsOn)
            {
               glBindBuffer(GL_ARRAY_BUFFER, mInstanceVbo);
               for (int col = 0; col < 4; col++)
               {
                  const unsigned int loc = 2 + col;
                  glEnableVertexAttribArray(loc);
                  glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                        (void*)(size_t)(col * 4 * sizeof(float)));
                  glVertexAttribDivisor(loc, 1);
               }
               mSelInstanceAttribsOn = true;
            }
            else if (instanced)
            {
               // Already pointed at mInstanceVbo from a previous frame, but the
               // instance-selection branch above may have last pointed these
               // same locations at mSelInstanceOverrideVbo instead - rebind
               // unconditionally rather than trusting mSelInstanceAttribsOn to
               // mean "pointed at the right buffer".
               glBindBuffer(GL_ARRAY_BUFFER, mInstanceVbo);
               for (int col = 0; col < 4; col++)
               {
                  const unsigned int loc = 2 + col;
                  glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                        (void*)(size_t)(col * 4 * sizeof(float)));
               }
            }
            else if (mSelInstanceAttribsOn)
            {
               for (int col = 0; col < 4; col++)
               {
                  glDisableVertexAttribArray(2 + col);
                  glVertexAttribDivisor(2 + col, 0);
               }
               mSelInstanceAttribsOn = false;
            }
            if (instanced)
            {
               glDrawElementsInstanced(GL_TRIANGLES, mSelIndexCount, GL_UNSIGNED_INT, nullptr,
                                       (GLsizei)mInstanceCount);
            }
            else
            {
               glDrawElements(GL_TRIANGLES, mSelIndexCount, GL_UNSIGNED_INT, nullptr);
            }
         }
         glBindVertexArray(0);
         glUseProgram(0);

         glDisable(GL_BLEND);
         glDepthMask(GL_TRUE);
         glDisable(GL_POLYGON_OFFSET_FILL);
         glPolygonOffset(0.0f, 0.0f);
         glDepthFunc(GL_LESS);
      }
   }

   // --- restore ---
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

   mLastRenderedRevision = revision;
   mLastAzimuth = cam.azimuth;
   mLastElevation = cam.elevation;
   mLastDistance = mDistance;
   mLastModel = model;
   mLastPointMatrix = pointMatrix;
   mLastMaterial = material;
   mLastSurfaceTexture = surfaceTexture;
   mLastPaintTex = mPaintTex;
   mLastPaintRev = mPaintRev;
   mLastSurfaceTextureRevision = surfaceTextureRevision;
   mHasRendered = true;
   return mColorTex;
}
