#version 450 core
// Turbo 0.71 (S1): Render 3D HQ - the HDRI behind the scene, at its full
// resolution (textureLod 0: no seam where the equirect wraps).
in vec2 vNdc;

uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform sampler2D uEnvMap;
uniform float uEnvRotation;
uniform float uEnvIntensity;
uniform float uExposure;

layout(location = 0) out vec4 outColor;

//@include hq_common.glsl

void main()
{
   vec4 worldH = uInvViewProj * vec4(vNdc, 1.0, 1.0);
   vec3 dir = normalize(worldH.xyz / worldH.w - uCamPos);
   vec3 col = textureLod(uEnvMap, dirToEquirect(dir, uEnvRotation), 0.0).rgb * uEnvIntensity;
   outColor = vec4(col * uExposure, 1.0);
}
