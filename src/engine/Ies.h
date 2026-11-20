#pragma once
// Row 4.2: IES photometric profiles (IESNA LM-63, type C), for point and spot lights. A profile becomes a fixed
// table of kIesV vertical x kIesH horizontal samples: vertical angle 0..180 degrees from the light's aim (0 = straight
// along it, as Cycles' IES Texture on a light pointed with its -Z), horizontal 0..360 around it. Values are scaled to
// Blender's convention so a light's own strength times the table is what Cycles draws: candela x multiplier x
// ballast x kIesScale (measured against Blender 5.2: a flat 4 pi candela file gives 0.888 of the bare light).
// Written from the public LM-63 format; no code taken from any other reader.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace engine
{
   constexpr int kIesV = 32, kIesH = 16, kIesRow = kIesV * kIesH; // one light's table: [h * kIesV + v]
   constexpr float kIesScale = 0.888f / (4.0f * 3.14159265f);

   // Linear interpolation of `vals` over ascending `angles` at x; outside the range: 0 (vertical) or clamped.
   inline float IesLerp(const std::vector<float>& angles, const float* vals, float x, bool zeroOutside)
   {
      const size_t n = angles.size();
      if (n == 0) return 0.0f;
      if (n == 1) return vals[0];
      if (x < angles[0] - 1e-4f || x > angles[n - 1] + 1e-4f)
         return zeroOutside ? 0.0f : vals[x < angles[0] ? 0 : n - 1];
      size_t k = 0;
      while (k + 2 < n && angles[k + 1] < x) k++;
      const float span = std::max(angles[k + 1] - angles[k], 1e-6f);
      const float t = std::clamp((x - angles[k]) / span, 0.0f, 1.0f);
      return vals[k] + (vals[k + 1] - vals[k]) * t;
   }

   // Parses LM-63 text into a kIesRow table (see above). False when the numbers do not add up.
   inline bool ParseIes(const std::string& text, std::vector<float>& out, std::string* err = nullptr)
   {
      auto fail = [&](const char* m) { if (err) *err = m; return false; };
      const size_t tilt = text.find("TILT=");
      if (tilt == std::string::npos) return fail("no TILT= line");
      const size_t eol = text.find('\n', tilt);
      std::string tiltVal = text.substr(tilt + 5, (eol == std::string::npos ? text.size() : eol) - tilt - 5);
      while (!tiltVal.empty() && std::isspace((unsigned char)tiltVal.back())) tiltVal.pop_back();
      std::string body = eol == std::string::npos ? std::string() : text.substr(eol + 1);
      for (char& c : body) if (c == ',') c = ' ';
      std::istringstream in(body);
      auto num = [&](double& v) { return (bool)(in >> v); };
      double d = 0.0;
      if (tiltVal == "INCLUDE")
      {
         // Lamp-to-luminaire geometry, the count of tilt angles, the angles and their factors: not used here.
         double geom = 0.0, nt = 0.0;
         if (!num(geom) || !num(nt)) return fail("bad TILT data");
         for (int k = 0; k < 2 * (int)nt; k++) if (!num(d)) return fail("bad TILT data");
      }
      double lamps, lumens, mult, nV, nH, ptype, units, w, l, h, ballast, future, watts;
      if (!num(lamps) || !num(lumens) || !num(mult) || !num(nV) || !num(nH) || !num(ptype) || !num(units) || !num(w) || !num(l) || !num(h))
         return fail("bad header numbers");
      if (!num(ballast) || !num(future) || !num(watts)) return fail("bad ballast line");
      const int iv = (int)nV, ih = (int)nH;
      if (iv < 1 || ih < 1 || iv > 4096 || ih > 4096) return fail("bad angle counts");
      std::vector<float> va((size_t)iv), ha((size_t)ih), cd((size_t)iv * (size_t)ih);
      for (float& x : va) { if (!num(d)) return fail("short vertical angles"); x = (float)d; }
      for (float& x : ha) { if (!num(d)) return fail("short horizontal angles"); x = (float)d; }
      for (float& x : cd) { if (!num(d)) return fail("short candela values"); x = (float)d; }
      if (!std::is_sorted(va.begin(), va.end()) || !std::is_sorted(ha.begin(), ha.end())) return fail("angles not ascending");
      const float scale = (float)(mult * (ballast > 0.0 ? ballast : 1.0)) * kIesScale;
      const float hLast = ha.back(), hFirst = ha.front();
      out.assign((size_t)kIesRow, 0.0f);
      std::vector<float> col((size_t)ih);
      for (int v = 0; v < kIesV; v++)
      {
         const float theta = 180.0f * (float)v / (float)(kIesV - 1);
         for (int k = 0; k < ih; k++) col[(size_t)k] = IesLerp(va, &cd[(size_t)k * (size_t)iv], theta, true);
         for (int hh = 0; hh < kIesH; hh++)
         {
            float phi = 360.0f * (float)hh / (float)kIesH;
            // The horizontal symmetry the file's last angle states: one plane, a quadrant (0..90), half (0..180).
            if (ih > 1 && hFirst <= 0.0f)
            {
               if (hLast <= 90.0f + 1e-3f) { phi = std::fmod(phi, 180.0f); if (phi > 90.0f) phi = 180.0f - phi; }
               else if (hLast <= 180.0f + 1e-3f) { if (phi > 180.0f) phi = 360.0f - phi; }
            }
            out[(size_t)(hh * kIesV + v)] = std::max(0.0f, IesLerp(ha, col.data(), phi, false)) * scale;
         }
      }
      return true;
   }

   // The table of an .ies file, kept per path until the file changes; null (and `err`) when it cannot be read.
   inline std::shared_ptr<const std::vector<float>> LoadIes(const std::filesystem::path& path, std::string* err = nullptr)
   {
      struct Entry { std::filesystem::file_time_type t; uintmax_t size = 0; std::shared_ptr<const std::vector<float>> table; std::string err; };
      static std::mutex mu;
      static std::map<std::filesystem::path, Entry> cache;
      std::error_code ec;
      const auto t = std::filesystem::last_write_time(path, ec);
      if (ec) { if (err) *err = "file not found"; return nullptr; }
      const uintmax_t size = std::filesystem::file_size(path, ec);
      std::lock_guard<std::mutex> lock(mu);
      auto it = cache.find(path);
      if (it != cache.end() && it->second.t == t && it->second.size == size)
      {
         if (err) *err = it->second.err;
         return it->second.table;
      }
      Entry e; e.t = t; e.size = size;
      std::ifstream f(path, std::ios::binary);
      std::stringstream ss; ss << f.rdbuf();
      auto table = std::make_shared<std::vector<float>>();
      if (ParseIes(ss.str(), *table, &e.err)) e.table = table;
      if (err) *err = e.err;
      cache[path] = e;
      return e.table;
   }
}
