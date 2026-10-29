// Parity check for src/app/graph/RoleCritic.cpp against tools/universe/frame_features.py (critic_parity.py builds and runs it).
// stdin: the wasm_render_parts.js stream (one JSON header line with "frames", then frames*2 float32). stdout: one JSON line per part.
#include "app/graph/RoleCritic.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main()
{
   std::string head;
   while (std::getline(std::cin, head))
   {
      const size_t at = head.find("\"frames\":");
      const int frames = at == std::string::npos ? 0 : atoi(head.c_str() + at + 9);
      std::vector<float> pcm((size_t)frames * 2), l(frames), r(frames);
      std::cin.read((char*)pcm.data(), (std::streamsize)pcm.size() * 4);
      for (int i = 0; i < frames; i++)
         l[i] = pcm[(size_t)i * 2], r[i] = pcm[(size_t)i * 2 + 1];
      const std::vector<double> x = RoleCritic::Measure(l.data(), r.data(), frames, 48000.0);
      const std::vector<float> p = RoleCritic::Probabilities(x);
      std::string o = "{\"x\":[";
      char b[64];
      for (size_t i = 0; i < x.size(); i++)
         snprintf(b, sizeof b, "%s%.9g", i ? "," : "", x[i]), o += b;
      o += "],\"p\":[";
      for (size_t i = 0; i < p.size(); i++)
         snprintf(b, sizeof b, "%s%.7g", i ? "," : "", p[i]), o += b;
      std::cout << o << "]}\n";
   }
   return 0;
}
