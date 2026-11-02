#include "SceneNodes.h"

#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kProjectionNames = { "Perspective", "Orthographic" };
   const std::vector<std::string> kLightTypeNames = { "Directional", "Point", "Sun", "Ambient", "Spot", "Area" };
}

const std::vector<std::string>& CameraNode::ProjectionNames() { return kProjectionNames; }
const std::vector<std::string>& LightNode::TypeNames() { return kLightTypeNames; }

namespace
{
   const std::vector<std::string> kSensorFitNames = { "Auto", "Horizontal", "Vertical" };
}

const std::vector<std::string>& CameraNode::SensorFitNames() { return kSensorFitNames; }

float CameraNode::SensorSpan(float aspect) const
{
   aspect = std::max(1e-3f, aspect);
   if (sensorFit == 2)
      return std::max(0.01f, sensorHeight);
   if (sensorFit == 1)
      return std::max(0.01f, sensorWidth) / aspect;
   return std::max(0.01f, sensorWidth) / std::max(1.0f, aspect);
}

float CameraNode::FocalLength(float aspect) const
{
   return SensorSpan(aspect) / (2.0f * std::tan(fov * 0.5f * 3.14159265f / 180.0f));
}

void CameraNode::SetFocalLength(float mm, float aspect)
{
   fov = 2.0f * std::atan(SensorSpan(aspect) / (2.0f * std::max(0.01f, mm))) * 180.0f / 3.14159265f;
}

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
   // Shift slides the picture across the sensor; the wider side is the sensor's width.
   const float sx = 2.0f * shiftX * std::max(1.0f, 1.0f / aspect), sy = 2.0f * shiftY * std::max(1.0f, aspect);
   if (projection == 1)
   {
      Mat4 m = Mat4::Orthographic(orthoHeight, aspect, nearPlane, farPlane);
      m.m[12] -= sx;
      m.m[13] -= sy;
      return m;
   }
   Mat4 m = Mat4::Perspective(fov * 3.14159265f / 180.0f, aspect, nearPlane, farPlane);
   m.m[8] += sx;
   m.m[9] += sy;
   return m;
}

void LightNode::TemperatureColor(float kelvin, float out[3])
{
   // Planck's law summed against the CIE 1931 observer (the multi-lobe fit of Wyman, Sloan and Shirley 2013),
   // then XYZ to linear sRGB.
   const float t = std::clamp(kelvin, 800.0f, 40000.0f);
   auto g = [](float x, float mu, float s1, float s2) { const float d = (x - mu) / (x < mu ? s1 : s2); return std::exp(-0.5f * d * d); };
   double X = 0, Y = 0, Z = 0;
   for (float nm = 380.0f; nm <= 780.0f; nm += 5.0f)
   {
      const double lm = nm * 1e-9;
      const double planck = 1.0 / (std::pow(lm, 5.0) * (std::exp(1.4387769e-2 / (lm * t)) - 1.0));
      X += planck * (1.056 * g(nm, 599.8f, 37.9f, 31.0f) + 0.362 * g(nm, 442.0f, 16.0f, 26.7f) - 0.065 * g(nm, 501.1f, 20.4f, 26.2f));
      Y += planck * (0.821 * g(nm, 568.8f, 46.9f, 40.5f) + 0.286 * g(nm, 530.9f, 16.3f, 31.1f));
      Z += planck * (1.217 * g(nm, 437.0f, 11.8f, 36.0f) + 0.681 * g(nm, 459.0f, 26.0f, 13.8f));
   }
   float rgb[3] = { (float)(3.2406 * X - 1.5372 * Y - 0.4986 * Z), (float)(-0.9689 * X + 1.8758 * Y + 0.0415 * Z),
                    (float)(0.0557 * X - 0.2040 * Y + 1.0570 * Z) };
   const float peak = std::max({ rgb[0], rgb[1], rgb[2], 1e-30f });
   for (int k = 0; k < 3; k++)
      out[k] = std::max(0.0f, rgb[k] / peak);
}

void LightNode::ComputeVector(float out[3]) const
{
   const float spin = orbitPerBeat * (float)Transport::Instance().Beats();
   const float az = azimuth * 3.14159265f / 180.0f + spin;
   const float ce = std::cos(elevation * 3.14159265f / 180.0f);
   // Point, Spot and Area lights carry a world position; the others carry a direction.
   const float radius = (type == 1 || type == 4 || type == 5) ? distance : 1.0f;
   out[0] = radius * ce * std::cos(az);
   out[1] = radius * std::sin(elevation * 3.14159265f / 180.0f);
   out[2] = radius * ce * std::sin(az);
}
