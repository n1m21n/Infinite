// PointZ operators that need the platform layer (installed fonts).

#include "PopOpsInternal.h"

#include "Mesh.h"
#include "platform/Platform.h"

namespace pop
{
   using namespace popa;

   void RegisterPlatformOps(std::vector<PopDef>& defs)
   {
      PopDef d = Simple("Text PointZ", "Text PointZ", "Text from an installed font as geometry: the letter outlines as closed lines, or "
                                                "filled and extruded into solid letters ('depth', 'bevel'). Centred on the origin.", 0);
      d.tier = 'B';
      d.params = { T("text", "text", "PointZ"), T("font", "font", "Arial"), F("size", "size", 1.0f, 0.01f, 20.0f),
                   F("spacing", "letter spacing", 0.0f, -1.0f, 2.0f), M("kind", "output", { "outlines (lines)", "filled", "extruded" }, 1),
                   F("depth", "depth", 0.2f, 0.0f, 5.0f, "kind=2"), F("bevel", "bevel", 0.0f, 0.0f, 1.0f, "kind=2") };
      d.cook = [](CookCtx& c) {
         std::vector<Platform::TextContour> contours;
         std::string error;
         if (!Platform::GetTextOutlines(c.S("text"), c.S("font"), c.F("spacing"), contours, error))
         {
            c.error = error.empty() ? std::string("could not build the letter outlines") : error;
            return false;
         }
         float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
         for (const Platform::TextContour& ct : contours)
            for (size_t i = 0; i + 1 < ct.points.size(); i += 2)
               for (int k = 0; k < 2; k++)
               {
                  lo[k] = std::min(lo[k], ct.points[i + (size_t)k]);
                  hi[k] = std::max(hi[k], ct.points[i + (size_t)k]);
               }
         const float cx = 0.5f * (lo[0] + hi[0]), cy = 0.5f * (lo[1] + hi[1]);
         const float h = std::max(1e-6f, hi[1] - lo[1]);
         const float sc = c.F("size") / h;
         const int kind = c.I("kind");
         if (kind == 0)
         {
            std::vector<float> P;
            auto topo = NewTopo();
            for (const Platform::TextContour& ct : contours)
            {
               std::vector<uint32_t> s;
               for (size_t i = 0; i + 1 < ct.points.size(); i += 2)
               {
                  s.push_back((uint32_t)(P.size() / 3));
                  P.insert(P.end(), { (ct.points[i] - cx) * sc, (ct.points[i + 1] - cy) * sc, 0.0f });
               }
               if (s.size() >= 2)
                  topo->AddStrip(s, true);
            }
            c.out = FromPoints(std::move(P), false);
            c.out->topo = topo;
            return true;
         }
         std::vector<MeshOps::Contour2D> conv;
         for (const Platform::TextContour& ct : contours)
         {
            MeshOps::Contour2D o;
            for (size_t i = 0; i + 1 < ct.points.size(); i += 2)
            {
               o.points.push_back((ct.points[i] - cx) * sc);
               o.points.push_back((ct.points[i + 1] - cy) * sc);
            }
            conv.push_back(std::move(o));
         }
         const Mesh m = MeshOps::ExtrudeContours(conv, kind == 2 ? c.F("depth") : 0.0f, kind == 2 ? c.F("bevel") : 0.0f);
         c.out = FromMesh(m);
         return true;
      };
      defs.push_back(d);
   }
}
