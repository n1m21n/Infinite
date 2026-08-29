#include "NodeViewport.h"

#include "gl3.h"
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

   // Same chain-walk, for the first non-null InstanceTransformOverride() (a
   // selectionOnly Delete/Transform downstream of the instancer) - see
   // GeometryOpNode::InstanceTransformOverride. Falls back to the instancer's
   // own placements when nothing overrides.
   const std::vector<Mat4>& ResolveInstanceTransforms(IGeometrySource* source, InstanceOnPointsNode* instancer)
   {
      for (IGeometrySource* s = source; s != nullptr; s = s->PassthroughSource())
      {
         if (const std::vector<Mat4>* override_ = s->InstanceTransformOverride())
            return *override_;
      }
      return instancer->InstanceTransforms();
   }

   // Same chain-walk for the instance selection mask, used by the selection
   // overlay's instance-domain fallback (whole instances tinted, not faces).
   const std::vector<unsigned char>* ResolveInstanceSelection(IGeometrySource* source)
   {
      for (IGeometrySource* s = source; s != nullptr; s = s->PassthroughSource())
      {
         if (const std::vector<unsigned char>* mask = s->InstanceSelection())
            return mask;
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

   unsigned int LinkPointProgram()
   {
      const unsigned int vert = CompileShader(GL_VERTEX_SHADER, kPointVertSrc, "point");
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
   if (mVbo != 0) glDeleteBuffers(1, &mVbo);
   if (mIbo != 0) glDeleteBuffers(1, &mIbo);
   if (mVao != 0) glDeleteVertexArrays(1, &mVao);
   if (mSelIbo != 0) glDeleteBuffers(1, &mSelIbo);
   if (mSelVao != 0) glDeleteVertexArrays(1, &mSelVao);
   if (mInstanceVbo != 0) glDeleteBuffers(1, &mInstanceVbo);
   if (mVertexColorVbo != 0) glDeleteBuffers(1, &mVertexColorVbo);
   if (mPointVbo != 0) glDeleteBuffers(1, &mPointVbo);
   if (mPointVao != 0) glDeleteVertexArrays(1, &mPointVao);
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

unsigned int NodeViewport::Render(IGeometrySource* geo, const SharedViewportCamera& cam, int w, int h,
                                  bool forceRedraw)
{
   if (geo == nullptr)
      return 0;
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
   const std::vector<Particle>* cloud = hasMesh ? nullptr : geo->GetPointCloud();
   bool hasCloud = false;
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

   const unsigned long long revision = usePointCloud ? geo->PointCloudRevision() : geo->MeshRevision();
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
   static const unsigned int pointProgram = LinkPointProgram();
   if ((usePointCloud && pointProgram == 0) || (!usePointCloud && program == 0))
      return 0;
   static const unsigned int selectionProgram = LinkSelectionProgram();

   if (!EnsureFbo(w, h))
      return 0;

   if (usePointCloud)
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

   if (usePointCloud)
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

   glBindVertexArray(mVao);
   if (instanced)
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
   mLastMaterial = material;
   mLastSurfaceTexture = surfaceTexture;
   mLastSurfaceTextureRevision = surfaceTextureRevision;
   mHasRendered = true;
   return mColorTex;
}
