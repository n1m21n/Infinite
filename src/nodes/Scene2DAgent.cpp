// The agent's eyes on the 2D document (ai-tools.md 3.1): a small picture of the whole document, one layer or a region,
// and what changed since the last one. The panel's view2d RPC wraps these as `look`.
#include "Scene2DNode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
   // ReadOutput's rows run bottom first; a look's run top first.
   void FlipRows(std::vector<uint8_t>& px, int w, int h, size_t rgba)
   {
      const size_t row = (size_t)w * rgba;
      for (int y = 0; y < h / 2; y++)
         std::swap_ranges(px.begin() + (ptrdiff_t)(y * row), px.begin() + (ptrdiff_t)((y + 1) * row), px.begin() + (ptrdiff_t)((h - 1 - y) * row));
   }
}

bool Scene2DNode::AgentLook(int layer, int x0, int y0, int x1, int y1, int maxSide, int frameId, std::vector<uint8_t>& img, int& w,
                            int& h, float& scale)
{
   const int W = Doc().w, H = Doc().h;
   const size_t rgba = 4;
   std::vector<uint8_t> px;
   if (layer != 0)
   {
      if (FindLayer(layer) == nullptr || !RenderLayerAlone(layer, frameId, px))
         return false;
   }
   else
   {
      int rw = 0, rh = 0;
      CookIfNeeded(frameId);
      if (!ReadOutput(px, rw, rh) || rw != W || rh != H)
         return false;
      FlipRows(px, rw, rh, rgba);
   }
   if (px.size() != (size_t)W * H * rgba)
      return false;
   x0 = std::clamp(x0, 0, W), x1 = std::clamp(x1, 0, W), y0 = std::clamp(y0, 0, H), y1 = std::clamp(y1, 0, H);
   const int cw = x1 - x0, ch = y1 - y0;
   if (cw < 1 || ch < 1)
      return false;
   scale = std::min(1.0f, (float)std::max(1, maxSide) / (float)std::max(cw, ch));
   w = std::max(1, (int)std::lround(cw * scale)), h = std::max(1, (int)std::lround(ch * scale));
   img.assign((size_t)w * h * rgba, 0);
   // Box filter: each output pixel is the mean of the source pixels it covers.
   for (int oy = 0; oy < h; oy++)
   {
      const int sy0 = y0 + oy * ch / h, sy1 = std::max(sy0 + 1, y0 + (oy + 1) * ch / h);
      for (int ox = 0; ox < w; ox++)
      {
         const int sx0 = x0 + ox * cw / w, sx1 = std::max(sx0 + 1, x0 + (ox + 1) * cw / w);
         uint32_t acc[4] = {};
         for (int sy = sy0; sy < sy1; sy++)
            for (int sx = sx0; sx < sx1; sx++)
               for (size_t c = 0; c < rgba; c++)
                  acc[c] += px[((size_t)sy * W + sx) * rgba + c];
         const uint32_t n = (uint32_t)((sy1 - sy0) * (sx1 - sx0));
         for (size_t c = 0; c < rgba; c++)
            img[((size_t)oy * w + ox) * rgba + c] = (uint8_t)((acc[c] + n / 2) / n);
      }
   }
   return true;
}

bool Scene2DNode::AgentLookDiff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int w, int h, int box[4], double& mean)
{
   const int noise = 6;   // 8-bit steps a re-render can wobble by
   const size_t rgba = 4;
   box[0] = w, box[1] = h, box[2] = -1, box[3] = -1;
   mean = 0.0;
   if (a.size() != b.size() || a.size() != (size_t)w * h * rgba)
      return false;
   double sum = 0;
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
         int most = 0;
         for (size_t c = 0; c < rgba; c++)
         {
            const size_t i = ((size_t)y * w + x) * rgba + c;
            const int d = std::abs((int)a[i] - (int)b[i]);
            most = std::max(most, d);
            sum += d;
         }
         if (most > noise)
            box[0] = std::min(box[0], x), box[1] = std::min(box[1], y), box[2] = std::max(box[2], x + 1), box[3] = std::max(box[3], y + 1);
      }
   mean = sum / ((double)w * h * rgba * 255.0);
   return box[2] >= 0;
}
