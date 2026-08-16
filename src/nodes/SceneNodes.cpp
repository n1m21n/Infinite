#include "SceneNodes.h"

#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kProjectionNames = { "Perspective", "Orthographic" };
   const std::vector<std::string> kLightTypeNames = { "Directional", "Point", "Sun", "Ambient", "Spot" };
}

const std::vector<std::string>& CameraNode::ProjectionNames() { return kProjectionNames; }
const std::vector<std::string>& LightNode::TypeNames() { return kLightTypeNames; }

void CameraNode::ComputeEye(float outEye[3]) const
{
   const float spin = orbitPerBeat * (float)Transport::Instance().Beats();
   const float az = azimuth * 3.14159265f / 180.0f + spin;
   const float ce = std::cos(elevation * 3.14159265f / 180.0f);
   outEye[0] = targetX + distance * ce * std::cos(az);
   outEye[1] = targetY + distance * std::sin(elevation * 3.14159265f / 180.0f);
   outEye[2] = targetZ + distance * ce * std::sin(az);
}

Mat4 CameraNode::ViewMatrix() const
{
   float eye[3];
   ComputeEye(eye);
   const float target[3] = { targetX, targetY, targetZ };
   // Rolling the up vector is cheaper and more stable than rotating the whole
   // view matrix afterwards.
   const float rollRad = roll * 3.14159265f / 180.0f;
   const float up[3] = { std::sin(rollRad), std::cos(rollRad), 0.0f };
   return Mat4::LookAt(eye, target, up);
}

Mat4 CameraNode::ProjectionMatrix(float aspect) const
{
   if (projection == 1)
      return Mat4::Orthographic(orthoHeight, aspect, nearPlane, farPlane);
   return Mat4::Perspective(fov * 3.14159265f / 180.0f, aspect, nearPlane, farPlane);
}

void LightNode::ComputeVector(float out[3]) const
{
   const float spin = orbitPerBeat * (float)Transport::Instance().Beats();
   const float az = azimuth * 3.14159265f / 180.0f + spin;
   const float ce = std::cos(elevation * 3.14159265f / 180.0f);
   // Point and Spot lights carry a world position; the others carry a direction.
   const float radius = (type == 1 || type == 4) ? distance : 1.0f;
   out[0] = radius * ce * std::cos(az);
   out[1] = radius * std::sin(elevation * 3.14159265f / 180.0f);
   out[2] = radius * ce * std::sin(az);
}
