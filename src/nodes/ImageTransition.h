#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "platform/OpenGLHeaders.h"

// Turbo 0.50: the two-image transition shader shared by Slideshow and VMPC
// (factored out of SlideshowNode.cpp so both draw identical fades and wipes).
// Styles are numbered as Slideshow's `transition` param (0 = Fade ... 5 =
// Zoom Fade); VMPC prepends "Cut" and passes style - 1.
namespace ImageTransition
{
   enum Style
   {
      kFade = 0,
      kSlideLeft,
      kSlideRight,
      kWipeLeft,
      kWipeRight,
      kZoomFade,
      kStyleCount
   };

   inline const std::vector<std::string>& Names()
   {
      static const std::vector<std::string> names = {
         "Fade", "Slide Left", "Slide Right", "Wipe Left", "Wipe Right", "Zoom Fade"
      };
      return names;
   }

   inline constexpr const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uA;\n"
      "uniform sampler2D uB;\n"
      "uniform vec2 uScaleA;\n"
      "uniform vec2 uScaleB;\n"
      "uniform float uProgress;\n"
      "uniform float uWipeFeather;\n"
      "uniform int uTransition;\n"
      "vec4 sampleFrame(sampler2D tex, vec2 screenUv, vec2 scale) {\n"
      "   if (screenUv.x < 0.0 || screenUv.x > 1.0 || screenUv.y < 0.0 || screenUv.y > 1.0)\n"
      "      return vec4(0.0);\n"
      "   vec2 uv = (screenUv - 0.5) * scale + 0.5;\n"
      "   if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)\n"
      "      return vec4(0.0);\n"
      "   return texture(tex, uv);\n"
      "}\n"
      "vec4 over(vec4 back, vec4 front) {\n"
      "   float alpha = front.a + back.a * (1.0 - front.a);\n"
      "   if (alpha <= 0.00001) return vec4(0.0);\n"
      "   vec3 rgb = (front.rgb * front.a + back.rgb * back.a * (1.0 - front.a)) / alpha;\n"
      "   return vec4(rgb, alpha);\n"
      "}\n"
      "void main() {\n"
      "   float t = smoothstep(0.0, 1.0, uProgress);\n"
      "   vec4 a;\n"
      "   vec4 b;\n"
      "   if (uTransition == 1) {\n"
      "      a = sampleFrame(uA, vUv + vec2(t, 0.0), uScaleA);\n"
      "      b = sampleFrame(uB, vUv - vec2(1.0 - t, 0.0), uScaleB);\n"
      "      fragColor = over(a, b);\n"
      "   } else if (uTransition == 2) {\n"
      "      a = sampleFrame(uA, vUv - vec2(t, 0.0), uScaleA);\n"
      "      b = sampleFrame(uB, vUv + vec2(1.0 - t, 0.0), uScaleB);\n"
      "      fragColor = over(a, b);\n"
      "   } else if (uTransition == 3) {\n"
      "      a = sampleFrame(uA, vUv, uScaleA);\n"
      "      b = sampleFrame(uB, vUv, uScaleB);\n"
      "      float mask = 1.0 - smoothstep(t - uWipeFeather, t + uWipeFeather, vUv.x);\n"
      "      fragColor = mix(a, b, mask);\n"
      "   } else if (uTransition == 4) {\n"
      "      a = sampleFrame(uA, vUv, uScaleA);\n"
      "      b = sampleFrame(uB, vUv, uScaleB);\n"
      "      float edge = 1.0 - t;\n"
      "      float mask = smoothstep(edge - uWipeFeather, edge + uWipeFeather, vUv.x);\n"
      "      fragColor = mix(a, b, mask);\n"
      "   } else if (uTransition == 5) {\n"
      "      vec2 uvA = (vUv - 0.5) * (1.0 - 0.12 * t) + 0.5;\n"
      "      vec2 uvB = (vUv - 0.5) * (1.12 - 0.12 * t) + 0.5;\n"
      "      a = sampleFrame(uA, uvA, uScaleA);\n"
      "      b = sampleFrame(uB, uvB, uScaleB);\n"
      "      fragColor = mix(a, b, t);\n"
      "   } else {\n"
      "      a = sampleFrame(uA, vUv, uScaleA);\n"
      "      b = sampleFrame(uB, vUv, uScaleB);\n"
      "      fragColor = mix(a, b, t);\n"
      "   }\n"
      "}\n";

   // Letterbox (preserve aspect) scale of a srcW x srcH frame inside a
   // dstW x dstH target, as the shader's uScaleA / uScaleB expect.
   inline void ProportionalScale(int srcW, int srcH, int dstW, int dstH, float& scaleX, float& scaleY)
   {
      scaleX = 1.0f;
      scaleY = 1.0f;
      const float srcAspect = (float)std::max(1, srcW) / (float)std::max(1, srcH);
      const float dstAspect = (float)std::max(1, dstW) / (float)std::max(1, dstH);
      if (dstAspect > srcAspect)
         scaleX = dstAspect / srcAspect;
      else
         scaleY = srcAspect / dstAspect;
   }

   // Binds A/B to texture units 0/1 and sets every uniform. Call from inside
   // GLUtil::RunShaderPass's setup callback, with `program` bound.
   inline void SetUniforms(unsigned int program, unsigned int texA, unsigned int texB,
                           float scaleAX, float scaleAY, float scaleBX, float scaleBY,
                           float progress, int style, int dstW)
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texA);
      glUniform1i(glGetUniformLocation(program, "uA"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, texB);
      glUniform1i(glGetUniformLocation(program, "uB"), 1);
      glUniform2f(glGetUniformLocation(program, "uScaleA"), scaleAX, scaleAY);
      glUniform2f(glGetUniformLocation(program, "uScaleB"), scaleBX, scaleBY);
      glUniform1f(glGetUniformLocation(program, "uProgress"), progress);
      glUniform1f(glGetUniformLocation(program, "uWipeFeather"), 2.0f / (float)std::max(1, dstW));
      glUniform1i(glGetUniformLocation(program, "uTransition"), std::clamp(style, 0, (int)kStyleCount - 1));
      glActiveTexture(GL_TEXTURE0);
   }
}
