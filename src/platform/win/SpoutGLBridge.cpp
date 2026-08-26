#include "SpoutGLBridge.h"

#include "../../core/gl3.h"

namespace SpoutGLBridge
{
   namespace
   {
      const char* kVertSrc =
         "#version 150\n"
         "in vec2 aPos;\n"
         "in vec2 aUv;\n"
         "out vec2 vUv;\n"
         "void main() {\n"
         "   vUv = aUv;\n"
         "   gl_Position = vec4(aPos, 0.0, 1.0);\n"
         "}\n";

      // Samples the GL_TEXTURE_2D source with normalized uv and writes into
      // the bound GL_TEXTURE_RECTANGLE target - the only GL-level difference
      // between Spout's world and Syphon's.
      const char* kFragSrc =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 oColor;\n"
         "uniform sampler2D uTex;\n"
         "void main() { oColor = texture(uTex, vUv); }\n";

      unsigned int sQuadVao = 0;
      unsigned int sQuadVbo = 0;
      unsigned int sProgram = 0;

      void EnsureQuad()
      {
         if (sQuadVao != 0)
            return;

         float verts[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f, 1.0f, 0.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f
         };

         glGenVertexArrays(1, &sQuadVao);
         glGenBuffers(1, &sQuadVbo);
         glBindVertexArray(sQuadVao);
         glBindBuffer(GL_ARRAY_BUFFER, sQuadVbo);
         glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
         glEnableVertexAttribArray(0);
         glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
         glEnableVertexAttribArray(1);
         glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
         glBindVertexArray(0);
      }

      unsigned int CompileShader(GLenum type, const char* src)
      {
         unsigned int shader = glCreateShader(type);
         glShaderSource(shader, 1, &src, nullptr);
         glCompileShader(shader);
         GLint ok = 0;
         glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
         if (!ok)
         {
            glDeleteShader(shader);
            return 0u;
         }
         return shader;
      }

      bool EnsureProgram()
      {
         if (sProgram != 0)
            return true;

         unsigned int vert = CompileShader(GL_VERTEX_SHADER, kVertSrc);
         unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
         if (vert == 0 || frag == 0)
         {
            if (vert)
               glDeleteShader(vert);
            if (frag)
               glDeleteShader(frag);
            return false;
         }

         sProgram = glCreateProgram();
         glBindAttribLocation(sProgram, 0, "aPos");
         glBindAttribLocation(sProgram, 1, "aUv");
         glAttachShader(sProgram, vert);
         glAttachShader(sProgram, frag);
         glLinkProgram(sProgram);

         GLint linked = 0;
         glGetProgramiv(sProgram, GL_LINK_STATUS, &linked);
         glDeleteShader(vert);
         glDeleteShader(frag);

         if (!linked)
         {
            glDeleteProgram(sProgram);
            sProgram = 0;
            return false;
         }
         return true;
      }
   }

   void EnsureReceiveTexture(unsigned int& tex, int& curW, int& curH, int w, int h)
   {
      if (tex != 0 && curW == w && curH == h)
         return;

      if (tex == 0)
         glGenTextures(1, &tex);

      glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);

      curW = w;
      curH = h;
   }

   bool EnsureRectangleTarget(unsigned int& fbo, unsigned int& tex, int& curW, int& curH, int w, int h)
   {
      if (fbo != 0 && curW == w && curH == h)
         return true;

      if (fbo != 0)
      {
         DeleteFbo(fbo);
         DeleteTexture(tex);
      }

      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_RECTANGLE, tex);
      glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, tex, 0);

      GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindTexture(GL_TEXTURE_RECTANGLE, 0);

      if (status != GL_FRAMEBUFFER_COMPLETE)
      {
         DeleteFbo(fbo);
         DeleteTexture(tex);
         curW = curH = 0;
         return false;
      }

      curW = w;
      curH = h;
      return true;
   }

   bool BlitToRectangle(unsigned int dstFbo, unsigned int srcTex2D, int srcW, int srcH)
   {
      if (dstFbo == 0 || srcTex2D == 0 || !EnsureProgram())
         return false;

      EnsureQuad();

      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLint prevViewport[4];
      glGetIntegerv(GL_VIEWPORT, prevViewport);
      GLint prevProgram = 0;
      glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

      glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
      glViewport(0, 0, srcW, srcH);
      glUseProgram(sProgram);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex2D);
      glUniform1i(glGetUniformLocation(sProgram, "uTex"), 0);

      glBindVertexArray(sQuadVao);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glBindVertexArray(0);

      glBindTexture(GL_TEXTURE_2D, 0);
      glUseProgram(prevProgram);
      glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      return true;
   }

   void DeleteTexture(unsigned int& tex)
   {
      if (tex != 0)
      {
         glDeleteTextures(1, &tex);
         tex = 0;
      }
   }

   void DeleteFbo(unsigned int& fbo)
   {
      if (fbo != 0)
      {
         glDeleteFramebuffers(1, &fbo);
         fbo = 0;
      }
   }
}
