#include "NodeViewport.h"

#include <OpenGL/gl3.h>
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

   // Same chain-walk as Render3DNode's FindInstancer (Geometry3DNodes.cpp): an
   // InstanceOnPoints wrapped in Transform/Subdivide/etc. is still an instancer
   // as far as the preview is concerned, so the thumbnail shows every instance
   // rather than just the one stamp mesh those wrappers actually operate on.
   InstanceOnPointsNode* FindInstancer(IGeometrySource* source)
   {
      for (IGeometrySource* s = source; s != nullptr; s = s->PassthroughSource())
      {
         if (auto* instancer = dynamic_cast<InstanceOnPointsNode*>(s))
            return instancer;
      }
      return nullptr;
   }
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
      "out vec3 vNormal;\n"
      "out vec3 vWorldPos;\n"
      "out vec2 vUv;\n"
      "out vec3 vVertexColor;\n"
      "void main() {\n"
      "   mat4 model = (uInstanced == 1) ? aInstance : uModel;\n"
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
      "void main() {\n"
      "   vec3 n = normalize(vNormal);\n"
      "   if (!gl_FrontFacing) n = -n;\n"
      "   vec3 base = uBaseColor * vVertexColor;\n"
      "   if (uHasTexture == 1) base *= texture(uTexture, vUv).rgb;\n"
      "   vec3 l = normalize(uLightDir);\n"
      "   float ndl = max(dot(n, l), 0.0);\n"
      "   vec3 viewDir = normalize(uCamPos - vWorldPos);\n"
      "   vec3 halfVec = normalize(l + viewDir);\n"
      "   float spec = pow(max(dot(n, halfVec), 0.0), 24.0) * 0.15;\n"
      "   vec3 color = base * (0.35 + ndl * 0.8) + vec3(spec);\n"
      "   fragColor = vec4(color, 1.0);\n"
      "}\n";

   // Selection overlay, same idea as Render3DNode's kSelectVertSrc/kSelectFragSrc
   // in Geometry3DNodes.cpp - flat, unlit tint so it reads as an annotation
   // rather than a material.
   const char* kSelectVertSrc =
      "#version 150\n"
      "in vec3 aPos;\n"
      "uniform mat4 uModel;\n"
      "uniform mat4 uViewProj;\n"
      "void main() { gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }\n";

   const char* kSelectFragSrc =
      "#version 150\n"
      "out vec4 fragColor;\n"
      "uniform vec3 uColor;\n"
      "uniform float uOpacity;\n"
      "void main() { fragColor = vec4(uColor, uOpacity); }\n";

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
}

NodeViewport::~NodeViewport()
{
   ReleaseFbo();
   if (mVbo != 0) glDeleteBuffers(1, &mVbo);
   if (mIbo != 0) glDeleteBuffers(1, &mIbo);
   if (mVao != 0) glDeleteVertexArrays(1, &mVao);
   if (mSelIbo != 0) glDeleteBuffers(1, &mSelIbo);
   if (mSelVao != 0) glDeleteVertexArrays(1, &mSelVao);
   if (mInstanceVbo != 0) glDeleteBuffers(1, &mInstanceVbo);
   if (mVertexColorVbo != 0) glDeleteBuffers(1, &mVertexColorVbo);
}

bool NodeViewport::EnsureFbo(int w, int h)
{
   if (mFbo != 0 && mWidth == w && mHeight == h)
      return true;

   ReleaseFbo();

   glGenTextures(1, &mColorTex);
   glBindTexture(GL_TEXTURE_2D, mColorTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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
   if (mColorTex != 0) { glDeleteTextures(1, &mColorTex); mColorTex = 0; }
   if (mDepthBuffer != 0) { glDeleteRenderbuffers(1, &mDepthBuffer); mDepthBuffer = 0; }
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
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                mesh.indices.data(), GL_STATIC_DRAW);

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
}

void NodeViewport::Zoom(float wheelDelta)
{
   mDistance = std::max(0.05f, std::min(mDistance * (1.0f - wheelDelta * 0.15f), 500.0f));
}

unsigned int NodeViewport::Render(IGeometrySource* geo, const SharedViewportCamera& cam, int w, int h,
                                  bool forceRedraw)
{
   if (geo == nullptr)
      return 0;
   const Mesh& mesh = geo->GetMesh();
   // HasGeometry(), not Empty(): a vertices-only mesh (Points to Vertices'
   // output, indices empty) has nothing to preview as triangles but is still
   // drawn below as GL_POINTS.
   if (!mesh.HasGeometry())
      return 0;

   w = std::max(16, w);
   h = std::max(16, h);

   const unsigned long long revision = geo->MeshRevision();
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
   const bool modelChanged = !(model == mLastModel);
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
      material.subsurfaceRadius != mLastMaterial.subsurfaceRadius;
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
   const bool needsRedraw = forceRedraw || !mHasRendered || revision != mLastRenderedRevision ||
                            cameraChanged || modelChanged || materialChanged || instancingChanged ||
                            textureChanged || w != mWidth || h != mHeight;
   if (!needsRedraw)
      return mColorTex;

   // Compiled once for the process and shared read-only by every viewport -
   // not per instance, so N nodes with viewports open cost one compile total.
   static const unsigned int program = LinkLitProgram();
   if (program == 0)
      return 0;
   static const unsigned int selectionProgram = LinkSelectionProgram();

   if (!EnsureFbo(w, h))
      return 0;

   UploadMesh(mesh, revision);

   if (!mFramed && mHasBounds)
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
         for (const Mat4& xform : instancer->InstanceTransforms())
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

   glBindVertexArray(mVao);
   if (instanced)
   {
      if (mInstanceVbo == 0)
         glGenBuffers(1, &mInstanceVbo);
      if (instanceRevision != mInstanceRevision || !(instanceGroupMatrix == mInstanceGroupMatrix) ||
          !mInstanceAttribsOn)
      {
         const std::vector<Mat4>& xforms = instancer->InstanceTransforms();
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
         glDrawArrays(GL_POINTS, 0, mVertexCount);
      else
         glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, nullptr);
   }
   glBindVertexArray(0);
   glUseProgram(0);

   if (selectionProgram != 0 && !mesh.faceMask.empty())
   {
      UpdateSelectionBuffer(mesh, revision);
      if (mSelIndexCount > 0)
      {
         glUseProgram(selectionProgram);
         glUniformMatrix4fv(glGetUniformLocation(selectionProgram, "uModel"), 1, GL_FALSE, model.m);
         glUniformMatrix4fv(glGetUniformLocation(selectionProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
         const float selColor[3] = { 1.0f, 0.42f, 0.12f };
         glUniform3fv(glGetUniformLocation(selectionProgram, "uColor"), 1, selColor);
         glUniform1f(glGetUniformLocation(selectionProgram, "uOpacity"), 0.55f);

         glDepthFunc(GL_LEQUAL);
         glEnable(GL_POLYGON_OFFSET_FILL);
         glPolygonOffset(-1.0f, -1.0f);
         glDepthMask(GL_FALSE);
         glEnable(GL_BLEND);
         glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

         glBindVertexArray(mSelVao);
         glDrawElements(GL_TRIANGLES, mSelIndexCount, GL_UNSIGNED_INT, nullptr);
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
   mLastMaterial = material;
   mLastSurfaceTexture = surfaceTexture;
   mLastSurfaceTextureRevision = surfaceTextureRevision;
   mHasRendered = true;
   return mColorTex;
}
