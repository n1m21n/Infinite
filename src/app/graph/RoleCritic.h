#pragma once
// The role critic behind Generate Samples: does a rendered part sound like the part it was asked for (bass, pads, hats, ...)?
// A small network (132 frame features -> 128 -> 64 -> 15 roles) trained on the live generator's own renders, with whole styles
// held out (tools/universe/role_critic_render.py; weights from tools/universe/export_role_critic.py). Measure() is a port of
// tools/universe/frame_features.py (measure + vector); tools/universe/critic_parity.py checks the two agree.
#include <vector>

namespace RoleCritic
{
   constexpr int kFeatures = 132;

   // frame_features.vector(measure(L, R, fs)), NaN replaced by 0.
   std::vector<double> Measure(const float* l, const float* r, int frames, double fs);

   // Index of a part name ("bass", "hats", ...) among the critic's roles; -1 for a part it does not judge ("all").
   int RoleIndex(const char* partName);

   // P(role) for every role (RoleCriticData::kRoles values).
   std::vector<float> Probabilities(const std::vector<double>& features);

   // True when the audio passes its declared part's cut-off, or the part is not judged. p (optional): P(declared role), or -1.
   bool Accept(const char* partName, const float* l, const float* r, int frames, double fs, float* p = nullptr);
}
