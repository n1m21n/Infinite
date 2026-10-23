// Turbo 0.71 (S1): Render 3D HQ - helpers shared by its shaders (//@include).
const float PI = 3.14159265359;

// Colours come from the UI in sRGB; lighting is linear.
vec3 toLinear(vec3 c)
{
   return pow(max(c, vec3(0.0)), vec3(2.2));
}

// Equirectangular lookup, Render 3D's convention: the HDRI's top row is the
// zenith and rot turns it about +Y. Sample it with textureLod: the u seam
// (phi wrapping) makes the implicit derivatives huge there and texture()
// would drop to the smallest mip along a vertical line.
vec2 dirToEquirect(vec3 d, float rot)
{
   float phi = atan(d.z, d.x) + rot;
   float theta = acos(clamp(d.y, -1.0, 1.0));
   return vec2(phi / (2.0 * PI) + 0.5, 1.0 - theta / PI);
}

// The inverse, without rotation: the direction an equirect texel looks at
// (the IBL maps are built in this frame; rotation is applied at lookup).
vec3 uvToDir(vec2 uv)
{
   float phi = (uv.x - 0.5) * 2.0 * PI;
   float theta = (1.0 - uv.y) * PI;
   return vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}

// No HDRI: Render 3D's three-stop surround (sRGB stops, linear result).
vec3 gradientEnv(vec3 dir, vec3 sky, vec3 horizon, vec3 ground)
{
   float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
   vec3 lower = mix(toLinear(ground), toLinear(horizon), smoothstep(0.0, 0.5, t));
   return mix(lower, toLinear(sky), smoothstep(0.5, 1.0, t));
}
