// Layered files in and out of the 2D View: Photoshop .psd and OpenRaster .ora.
//
// A layer keeps its name, opacity, blend, visibility, clipping, its group and
// (PSD) its pixel mask. Paint layers that sit untransformed go out as their own
// pixels with the mask kept apart; every other layer (node, fill, vector, text,
// a turned or scaled paint layer) goes out as it composites, drawn alone, with
// its masks baked in. Adjustment layers have no pixels and are left out.
// Coming in, every pixel layer becomes a paint layer.

#include <fstream>
#include "GLUtil.h"
#include <iterator>
#include "Scene2DNode.h"
#include "gl3.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sstream>
#include "BlendModes.h"
#include "TileStore.h"
#include "miniz.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "platform/AppPaths.h"

namespace
{
   using Kind = Scene2DNode::Kind;
   using Layer = Scene2DNode::Layer;
   constexpr int kT = TileStore::kTile;

   // ---- blend names ---------------------------------------------------------

   // PSD blend keys in BlendModes order (Normal .. Luminosity); the modes after have no PSD key.
   const char* const kPsdKeys[] = { "norm", "dark", "mul ", "idiv", "lbrn", "dkCl", "lite", "scrn", "div ",
                                    "lddg", "lgCl", "over", "sLit", "hLit", "vLit", "lLit", "pLit", "hMix",
                                    "diff", "smud", "fsub", "fdiv", "hue ", "sat ", "colr", "lum " };
   constexpr int kPsdKeyCount = (int)(sizeof(kPsdKeys) / sizeof(kPsdKeys[0]));

   int BlendFromPsd(const char key[4])
   {
      if (std::memcmp(key, "pass", 4) == 0)
         return Scene2DNode::kPassThrough;
      for (int i = 0; i < kPsdKeyCount; i++)
         if (std::memcmp(key, kPsdKeys[i], 4) == 0)
            return i;
      return 0;   // dissolve and anything unknown
   }

   const char* PsdKey(int blend)
   {
      if (blend == Scene2DNode::kPassThrough)
         return "pass";
      return blend >= 0 && blend < kPsdKeyCount ? kPsdKeys[blend] : "norm";
   }

   // OpenRaster composite-op names, same order; "" where the format has none.
   const char* const kOraOps[] = { "svg:src-over",       "svg:darken",       "svg:multiply",     "svg:color-burn",
                                   "krita:linear_burn",  "krita:darker color", "svg:lighten",    "svg:screen",
                                   "svg:color-dodge",    "svg:plus",         "krita:lighter color", "svg:overlay",
                                   "svg:soft-light",     "svg:hard-light",   "krita:vivid_light", "krita:linear light",
                                   "krita:pin_light",    "krita:hard mix",   "svg:difference",   "svg:exclusion",
                                   "krita:subtract",     "krita:divide",     "svg:hue",          "svg:saturation",
                                   "svg:color",          "svg:luminosity" };
   constexpr int kOraOpCount = (int)(sizeof(kOraOps) / sizeof(kOraOps[0]));

   int BlendFromOra(const std::string& op)
   {
      for (int i = 0; i < kOraOpCount; i++)
         if (op == kOraOps[i])
            return i;
      if (op == "krita:linear_light") return 15;
      if (op == "krita:hard_mix") return 17;
      if (op == "krita:darker_color") return 5;
      if (op == "krita:lighter_color") return 10;
      return 0;
   }

   const char* OraOp(int blend) { return blend >= 0 && blend < kOraOpCount ? kOraOps[blend] : "svg:src-over"; }

   // ---- bytes -----------------------------------------------------------------

   struct Reader
   {
      const uint8_t* p = nullptr;
      size_t n = 0, at = 0;
      bool bad = false;
      bool Has(size_t k) const { return !bad && at + k <= n; }
      uint8_t U8()
      {
         if (!Has(1)) { bad = true; return 0; }
         return p[at++];
      }
      uint16_t U16() { const uint16_t a = U8(); return (uint16_t)((a << 8) | U8()); }
      uint32_t U32() { const uint32_t a = U16(); return (a << 16) | U16(); }
      int16_t I16() { return (int16_t)U16(); }
      int32_t I32() { return (int32_t)U32(); }
      void Skip(size_t k)
      {
         if (!Has(k)) { bad = true; at = n; return; }
         at += k;
      }
      bool Tag(char out[4])
      {
         for (int i = 0; i < 4; i++)
            out[i] = (char)U8();
         return !bad;
      }
   };

   struct Writer
   {
      std::vector<uint8_t>& o;
      void U8(uint8_t v) { o.push_back(v); }
      void U16(uint16_t v) { U8((uint8_t)(v >> 8)); U8((uint8_t)v); }
      void U32(uint32_t v) { U16((uint16_t)(v >> 16)); U16((uint16_t)v); }
      void Tag(const char* t) { o.insert(o.end(), t, t + 4); }
      void Bytes(const uint8_t* b, size_t k) { o.insert(o.end(), b, b + k); }
      size_t Mark() { const size_t at = o.size(); U32(0); return at; }   // a length filled in later
      void Fill(size_t mark, size_t pad = 1)
      {
         while ((o.size() - mark - 4) % pad != 0)
            U8(0);
         const uint32_t len = (uint32_t)(o.size() - mark - 4);
         o[mark] = (uint8_t)(len >> 24);
         o[mark + 1] = (uint8_t)(len >> 16);
         o[mark + 2] = (uint8_t)(len >> 8);
         o[mark + 3] = (uint8_t)len;
      }
   };

   std::string Utf16ToUtf8(const std::vector<uint16_t>& u)
   {
      std::string s;
      for (size_t i = 0; i < u.size(); i++)
      {
         uint32_t c = u[i];
         if (c >= 0xD800 && c < 0xDC00 && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (u[++i] - 0xDC00);
         if (c == 0)
            break;
         if (c < 0x80) s += (char)c;
         else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 63)); }
         else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
         else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 63)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
      }
      return s;
   }

   std::vector<uint16_t> Utf8ToUtf16(const std::string& s)
   {
      std::vector<uint16_t> u;
      for (size_t i = 0; i < s.size();)
      {
         const uint8_t b = (uint8_t)s[i];
         uint32_t c = b;
         int more = 0;
         if (b >= 0xF0) { c = b & 7; more = 3; }
         else if (b >= 0xE0) { c = b & 15; more = 2; }
         else if (b >= 0xC0) { c = b & 31; more = 1; }
         i++;
         for (int k = 0; k < more && i < s.size(); k++, i++)
            c = (c << 6) | ((uint8_t)s[i] & 63);
         if (c >= 0x10000)
         {
            c -= 0x10000;
            u.push_back((uint16_t)(0xD800 + (c >> 10)));
            u.push_back((uint16_t)(0xDC00 + (c & 1023)));
         }
         else
            u.push_back((uint16_t)c);
      }
      return u;
   }

   // PackBits, one row.
   void PackRow(const uint8_t* row, int w, std::vector<uint8_t>& out)
   {
      int i = 0;
      while (i < w)
      {
         int run = 1;
         while (i + run < w && run < 128 && row[i + run] == row[i])
            run++;
         if (run >= 2)
         {
            out.push_back((uint8_t)(257 - run));
            out.push_back(row[i]);
            i += run;
            continue;
         }
         int lit = 1;
         while (i + lit < w && lit < 128 && !(i + lit + 1 < w && row[i + lit] == row[i + lit + 1]))
            lit++;
         out.push_back((uint8_t)(lit - 1));
         out.insert(out.end(), row + i, row + i + lit);
         i += lit;
      }
   }

   bool UnpackRow(Reader& r, size_t len, uint8_t* out, size_t want)
   {
      const size_t end = r.at + len;
      size_t got = 0;
      while (r.at < end && got < want && !r.bad)
      {
         const int8_t h = (int8_t)r.U8();
         if (h >= 0)
         {
            for (int k = 0; k <= h; k++)
            {
               const uint8_t v = r.U8();
               if (got < want) out[got++] = v;
            }
         }
         else if (h != -128)
         {
            const uint8_t v = r.U8();
            for (int k = 0; k < 1 - h; k++)
               if (got < want) out[got++] = v;
         }
      }
      if (end <= r.n)
         r.at = end;
      return !r.bad && got == want;
   }

   // One channel of w x h samples at `depth` bits, decoded to 8 bits, rows top-down.
   // `len` is the channel's stored length after the 2-byte compression word (0: unknown, run to the end of the data).
   bool ReadChannel(Reader& r, int comp, int w, int h, int depth, size_t len, std::vector<uint8_t>& out,
                    const std::vector<uint32_t>* rleCounts = nullptr)
   {
      const int bps = depth / 8;
      const size_t rowBytes = (size_t)w * bps, total = rowBytes * h;
      std::vector<uint8_t> raw(total, 0);
      if (comp == 0)
      {
         if (!r.Has(total)) return false;
         std::memcpy(raw.data(), r.p + r.at, total);
         r.at += total;
      }
      else if (comp == 1)
      {
         std::vector<uint32_t> counts;
         if (rleCounts != nullptr)
            counts = *rleCounts;
         else
            for (int y = 0; y < h; y++)
               counts.push_back(r.U16());
         for (int y = 0; y < h && !r.bad; y++)
            UnpackRow(r, counts[y], &raw[(size_t)y * rowBytes], rowBytes);
         if (r.bad) return false;
      }
      else if (comp == 2 || comp == 3)
      {
         if (len == 0 || !r.Has(len)) return false;
         mz_ulong dst = (mz_ulong)total;
         if (mz_uncompress(raw.data(), &dst, r.p + r.at, (mz_ulong)len) != MZ_OK)
            return false;
         r.at += len;
         if (comp == 3)
            for (int y = 0; y < h; y++)
            {
               uint8_t* row = &raw[(size_t)y * rowBytes];
               if (bps == 1)
                  for (int x = 1; x < w; x++) row[x] = (uint8_t)(row[x] + row[x - 1]);
               else if (bps == 2)
                  for (int x = 1; x < w; x++)
                  {
                     const uint16_t a = (uint16_t)((row[2 * x - 2] << 8) | row[2 * x - 1]);
                     const uint16_t b = (uint16_t)(((row[2 * x] << 8) | row[2 * x + 1]) + a);
                     row[2 * x] = (uint8_t)(b >> 8);
                     row[2 * x + 1] = (uint8_t)b;
                  }
            }
      }
      else
         return false;
      out.resize((size_t)w * h);
      for (size_t i = 0; i < out.size(); i++)
      {
         if (bps == 1) out[i] = raw[i];
         else if (bps == 2) out[i] = raw[i * 2];
         else
         {
            float f;
            const uint32_t u = ((uint32_t)raw[i * 4] << 24) | ((uint32_t)raw[i * 4 + 1] << 16) | ((uint32_t)raw[i * 4 + 2] << 8) | raw[i * 4 + 3];
            std::memcpy(&f, &u, 4);
            out[i] = (uint8_t)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f);
         }
      }
      return true;
   }

   void WriteChannelRle(Writer& wr, const std::vector<uint8_t>& plane, int w, int h)
   {
      wr.U16(1);
      std::vector<std::vector<uint8_t>> rows((size_t)h);
      for (int y = 0; y < h; y++)
         PackRow(&plane[(size_t)y * w], w, rows[y]);
      for (int y = 0; y < h; y++)
         wr.U16((uint16_t)rows[y].size());
      for (int y = 0; y < h; y++)
         wr.Bytes(rows[y].data(), rows[y].size());
   }

   size_t RleSize(const std::vector<uint8_t>& plane, int w, int h)
   {
      size_t n = 2 + (size_t)h * 2;
      std::vector<uint8_t> row;
      for (int y = 0; y < h; y++)
      {
         row.clear();
         PackRow(&plane[(size_t)y * w], w, row);
         n += row.size();
      }
      return n;
   }

   // Top-down straight RGBA cut into a paint layer's tiles (bottom row first).
   std::vector<std::string> CutTiles(const std::vector<uint8_t>& topDown, int w, int h, bool mask)
   {
      const int cols = (w + kT - 1) / kT, rows = (h + kT - 1) / kT;
      std::vector<std::string> keys((size_t)cols * rows, "-");
      std::vector<uint8_t> tile;
      for (size_t i = 0; i < keys.size(); i++)
      {
         const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
         const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
         tile.resize((size_t)tw * th * 4);
         bool blank = true;
         for (int y = 0; y < th; y++)
         {
            const int srcRow = h - 1 - (y0 + y);
            std::memcpy(&tile[(size_t)y * tw * 4], &topDown[((size_t)srcRow * w + x0) * 4], (size_t)tw * 4);
            for (int x = 0; x < tw && blank; x++)
            {
               const uint8_t* q = &tile[((size_t)y * tw + x) * 4];
               blank = mask ? (q[0] == 255) : (q[3] == 0);
            }
         }
         if (!blank)
            keys[i] = TileStore::Put(tile.data(), tw, th);
      }
      return keys;
   }

   // A paint layer's (or mask's) tiles put back together, top-down; masks read white where a tile is "-".
   std::vector<uint8_t> JoinTiles(const std::vector<std::string>& keys, int w, int h, bool mask)
   {
      std::vector<uint8_t> out((size_t)w * h * 4, 0);
      if (mask)
         std::fill(out.begin(), out.end(), 255);
      const int cols = (w + kT - 1) / kT;
      std::vector<uint8_t> tile;
      for (size_t i = 0; i < keys.size(); i++)
      {
         const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
         const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
         int gw = 0, gh = 0;
         if (tw <= 0 || th <= 0 || keys[i] == "-" || !TileStore::Get(keys[i], tile, gw, gh) || gw != tw || gh != th)
            continue;
         for (int y = 0; y < th; y++)
            std::memcpy(&out[((size_t)(h - 1 - (y0 + y)) * w + x0) * 4], &tile[(size_t)y * tw * 4], (size_t)tw * 4);
      }
      return out;
   }

   void FlipRows(std::vector<uint8_t>& rgba, int w, int h)
   {
      const size_t row = (size_t)w * 4;
      std::vector<uint8_t> tmp(row);
      for (int y = 0; y < h / 2; y++)
      {
         uint8_t* a = &rgba[(size_t)y * row];
         uint8_t* b = &rgba[(size_t)(h - 1 - y) * row];
         std::memcpy(tmp.data(), a, row);
         std::memcpy(a, b, row);
         std::memcpy(b, tmp.data(), row);
      }
   }

   std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& rgba, int w, int h)
   {
      std::vector<uint8_t> png;
      if (w <= 0 || h <= 0)
         return png;
      stbi_write_png_to_func(
         [](void* ctx, void* data, int size)
         {
            auto* out = static_cast<std::vector<uint8_t>*>(ctx);
            out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
         },
         &png, w, h, 4, rgba.data(), w * 4);
      return png;
   }

   std::string XmlEscape(const std::string& s)
   {
      std::string o;
      for (char c : s)
      {
         if (c == '&') o += "&amp;";
         else if (c == '<') o += "&lt;";
         else if (c == '>') o += "&gt;";
         else if (c == '"') o += "&quot;";
         else o += c;
      }
      return o;
   }

   std::string XmlUnescape(const std::string& s)
   {
      std::string o;
      for (size_t i = 0; i < s.size(); i++)
      {
         if (s[i] != '&') { o += s[i]; continue; }
         const size_t semi = s.find(';', i);
         if (semi == std::string::npos) { o += s[i]; continue; }
         const std::string e = s.substr(i + 1, semi - i - 1);
         if (e == "amp") o += '&';
         else if (e == "lt") o += '<';
         else if (e == "gt") o += '>';
         else if (e == "quot") o += '"';
         else if (e == "apos") o += '\'';
         else if (!e.empty() && e[0] == '#')
         {
            const long c = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtol(e.c_str() + 2, nullptr, 16) : std::strtol(e.c_str() + 1, nullptr, 10);
            o += Utf16ToUtf8({ (uint16_t)c });
         }
         i = semi;
      }
      return o;
   }

   // A layer's pixels for a file: top-down straight RGBA at (x, y) in the document, plus its pixel mask (grey, same box) when kept apart.
   struct FilePixels
   {
      int x = 0, y = 0, w = 0, h = 0;
      std::vector<uint8_t> rgba;
      bool hasMask = false, maskOn = true;
      std::vector<uint8_t> mask;   // RGBA, R read
   };
}

// ---- drawing one layer alone ---------------------------------------------------------

namespace
{
   // The finished picture of PairAt(0) into rgba, top-down straight RGBA8.
   void ReadFinished(unsigned int acc, unsigned int prog, int w, int h, bool linear, float headroom, std::vector<uint8_t>& rgba)
   {
      GLUtil::Fbo f;
      if (!GLUtil::EnsureFbo(f, w, h, GLUtil::ImageWorkingFormat(), "Scene2D file layer"))
         return;
      GLUtil::RunShaderPass(f, prog, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, acc);
         glUniform1i(GLUtil::UniformLoc(prog, "uAcc"), 0);
         glUniform1i(GLUtil::UniformLoc(prog, "uLinear"), linear ? 1 : 0);
         GLUtil::SetImageHeadroom(prog, headroom);
      });
      rgba.assign((size_t)w * h * 4, 0);
      GLint prev = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, f.fbo);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
      FlipRows(rgba, w, h);
      GLUtil::DestroyFbo(f);
   }
}

bool Scene2DNode::RenderLayerAlone(int id, int frameId, std::vector<uint8_t>& rgba)
{
   Layers();
   if (FindConst(id) == nullptr || !EnsureShaders() || mFinishProgram == 0)
      return false;
   SyncSurfaces();   // outside a cook the paint surfaces and text/shape rasters may not exist yet
   SyncRasters();
   // Shown, at full strength and Normal, its groups too; put back after.
   const std::vector<Layer> keep = mLayers;
   for (int at = id; at != 0;)
   {
      Layer* t = FindLayer(at);
      if (t == nullptr)
         break;
      t->visible = true;
      t->opacity = 1.0f;
      if (at == id)
      {
         t->blend = 0;
         t->clip = false;
      }
      at = t->parent;
   }
   const int keepSolo = soloLayer;
   const bool keepOpaque = mAccOpaque;
   soloLayer = id;
   mIsolating = true;
   mAccOpaque = false;
   const float none[4] = { 0, 0, 0, 0 };
   Clear(PairAt(0), none);
   DrawList(0, 0, frameId, 1.0f);
   FlushQueue();   // the last batched pass lands before the read back
   soloLayer = keepSolo;
   mIsolating = false;
   mAccOpaque = keepOpaque;
   rgba.clear();
   ReadFinished(PairAt(0).Tex(), mFinishProgram, mDoc.w, mDoc.h, mDoc.linear, mDoc.headroom, rgba);
   mLayers = keep;
   return !rgba.empty();
}

bool Scene2DNode::RenderBelow(int id, int frameId, std::vector<uint8_t>& rgba)
{
   Layers();
   if (FindConst(id) == nullptr || !EnsureShaders() || mFinishProgram == 0)
      return false;
   SyncSurfaces();   // outside a cook the paint surfaces and text/shape rasters may not exist yet
   SyncRasters();
   // Hide the layer and everything drawn after it: later siblings at its level and at every group level above it.
   const std::vector<Layer> keep = mLayers;
   for (int at = id, level = 0; at != 0; level++)
   {
      const Layer* t = FindConst(at);
      if (t == nullptr)
         break;
      const int parent = t->parent;
      bool after = false;
      for (const Layer* k : ListOf(parent))
      {
         if (k->id == at)
         {
            after = true;
            if (level > 0)
               continue;   // a group holding the layer: its lower layers stay
         }
         if (after)
            if (Layer* m = FindLayer(k->id))
               m->visible = false;
      }
      at = parent;
   }
   const bool keepOpaque = mAccOpaque;
   const int keepSolo = soloLayer;
   soloLayer = 0;
   mIsolating = true;
   const float none[4] = { 0, 0, 0, 0 };
   Clear(PairAt(0), none);
   DrawList(0, 0, frameId, 1.0f);
   FlushQueue();   // the last batched pass lands before the read back
   mIsolating = false;
   mAccOpaque = keepOpaque;
   soloLayer = keepSolo;
   rgba.clear();
   ReadFinished(PairAt(0).Tex(), mFinishProgram, mDoc.w, mDoc.h, mDoc.linear, mDoc.headroom, rgba);
   mLayers = keep;
   return !rgba.empty();
}

bool Scene2DNode::RenderSet(const std::vector<int>& set, int frameId, std::vector<uint8_t>& rgba)
{
   Layers();
   if (!EnsureShaders() || mFinishProgram == 0)
      return false;
   SyncSurfaces();
   SyncRasters();
   const std::vector<Layer> keep = mLayers;
   auto inSet = [&](int id)
   {
      for (int s : set)
         if (IsInside(id, s))
            return true;
      return false;
   };
   for (const Layer& k : keep)
   {
      Layer* m = FindLayer(k.id);
      if (m == nullptr || inSet(k.id))
         continue;
      bool holds = false;   // a group the set sits in: shown, at full strength, passing its children through
      for (int s : set)
         holds = holds || IsInside(s, k.id);
      if (holds)
      {
         m->visible = true;
         m->opacity = 1.0f;
         m->blend = kPassThrough;
      }
      else
         m->visible = false;
   }
   const bool keepOpaque = mAccOpaque;
   const int keepSolo = soloLayer;
   soloLayer = 0;
   mIsolating = true;
   mAccOpaque = false;
   const float none[4] = { 0, 0, 0, 0 };
   Clear(PairAt(0), none);
   DrawList(0, 0, frameId, 1.0f);
   FlushQueue();
   mIsolating = false;
   mAccOpaque = keepOpaque;
   soloLayer = keepSolo;
   rgba.clear();
   ReadFinished(PairAt(0).Tex(), mFinishProgram, mDoc.w, mDoc.h, mDoc.linear, mDoc.headroom, rgba);
   mLayers = keep;
   return rgba.size() == (size_t)mDoc.w * mDoc.h * 4;
}

bool Scene2DNode::PictureUnder(int id, int effect, int frameId, std::vector<uint8_t>& rgba)
{
   const Layer* l = FindConst(id);
   if (l == nullptr)
      return false;
   if (effect < 0)
      return RenderBelow(id, frameId, rgba);
   // An effect: the layer alone with this effect and the ones after it off.
   const std::vector<Layer> keep = mLayers;
   if (Layer* m = FindLayer(id))
      for (size_t i = (size_t)effect; i < m->effects.size(); i++)
         m->effects[i].on = false;
   const bool ok = RenderLayerAlone(id, frameId, rgba);
   mLayers = keep;
   return ok;
}

namespace
{
   // Placement a file can carry as-is: no turn, scale, flip, crop or anything that changes pixels on the way out.
   bool PlainPaint(const Layer& l)
   {
      return l.kind == Kind::Paint && l.pw > 0 && l.ph > 0 && l.sx == 1.0f && l.sy == 1.0f && l.angle == 0.0f && l.shear == 0.0f && !l.flipX &&
             !l.flipY && !l.cropOn && !l.relief && l.fill >= 1.0f && !l.HasEffects() && !l.hasVMask && l.maskUid == 0;
   }
}

// ---- reading and writing ------------------------------------------------------------

namespace
{
   using Grab = std::function<bool(const Layer&, FilePixels&)>;

   // A layer's pixels: its own tiles when plain, else drawn alone and cut to what shows.
   bool GrabLayer(Scene2DNode& doc, const Layer& l, int frameId, bool keepMask, FilePixels& out,
                  const std::function<bool(int, int, std::vector<uint8_t>&)>& alone)
   {
      const int W = doc.Doc().w, H = doc.Doc().h;
      if (PlainPaint(l))
      {
         out.w = l.pw;
         out.h = l.ph;
         out.x = (int)std::lround(l.x - l.ax * l.pw);
         out.y = (int)std::lround(l.y - l.ay * l.ph);
         out.rgba = JoinTiles(l.tiles, l.pw, l.ph, false);
         if (l.HasPixelMask() && l.mw == l.pw && l.mh == l.ph)
         {
            out.mask = JoinTiles(l.mtiles, l.mw, l.mh, true);
            if (keepMask)
            {
               out.hasMask = true;
               out.maskOn = l.pmaskOn;
            }
            else if (l.pmaskOn)
               for (size_t i = 0; i < out.rgba.size(); i += 4)
                  out.rgba[i + 3] = (uint8_t)((out.rgba[i + 3] * out.mask[i] + 127) / 255);
         }
         return true;
      }
      std::vector<uint8_t> full;
      if (!alone(l.id, frameId, full))
         return false;
      int x0 = W, y0 = H, x1 = -1, y1 = -1;
      for (int y = 0; y < H; y++)
         for (int x = 0; x < W; x++)
            if (full[((size_t)y * W + x) * 4 + 3] != 0)
            {
               x0 = std::min(x0, x); x1 = std::max(x1, x);
               y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
      if (x1 < x0)
      {
         out.w = out.h = 0;
         return true;
      }
      out.x = x0;
      out.y = y0;
      out.w = x1 - x0 + 1;
      out.h = y1 - y0 + 1;
      out.rgba.resize((size_t)out.w * out.h * 4);
      for (int y = 0; y < out.h; y++)
         std::memcpy(&out.rgba[(size_t)y * out.w * 4], &full[((size_t)(y0 + y) * W + x0) * 4], (size_t)out.w * 4);
      return true;
   }
}

void Scene2DNode::ResetForFile(int w, int h)
{
   Layers();
   mLayers.clear();
   mDoc.w = std::clamp(w, 1, GLUtil::kMaxTargetSide);
   mDoc.h = std::clamp(h, 1, GLUtil::kMaxTargetSide);
   mDoc.background = 3;
   mDoc.slots.clear();
   Commit();
}

int Scene2DNode::AddFileLayer(int parent, const std::string& name, int x, int y, int w, int h, const std::vector<uint8_t>& rgba)
{
   Layer& l = AddLayer(Kind::Paint, 0, 0);
   l.parent = parent;
   l.name = name;
   if (w > 0 && h > 0 && rgba.size() >= (size_t)w * h * 4)
   {
      l.pw = w;
      l.ph = h;
      l.tiles = CutTiles(rgba, w, h, false);
      l.x = (float)x + w * 0.5f;
      l.y = (float)y + h * 0.5f;
   }
   return l.id;
}

bool Scene2DNode::LoadPsd(const std::vector<uint8_t>& bytes, std::string* err)
{
   auto fail = [&](const char* why) {
      if (err != nullptr) *err = why;
      return false;
   };
   Reader r{ bytes.data(), bytes.size() };
   char sig[4];
   r.Tag(sig);
   if (std::memcmp(sig, "8BPS", 4) != 0) return fail("not a PSD");
   const int version = r.U16();
   if (version != 1) return fail("PSB (large document) files are not read");
   r.Skip(6);
   const int channels = r.U16();
   const int H = (int)r.U32(), W = (int)r.U32();
   const int depth = r.U16(), mode = r.U16();
   if (r.bad || W <= 0 || H <= 0) return fail("damaged header");
   if (depth != 8 && depth != 16 && depth != 32) return fail("unsupported bit depth");
   if (mode != 3 && mode != 1) return fail("only RGB and greyscale PSDs are read");
   const bool grey = mode == 1;
   r.Skip(r.U32());   // colour mode data
   r.Skip(r.U32());   // image resources
   const uint32_t lmLen = r.U32();
   const size_t lmEnd = r.at + lmLen;
   if (r.bad || lmEnd > r.n) return fail("damaged layer section");

   struct Chan { int id; uint32_t len; };
   struct Rec
   {
      int top = 0, left = 0, bottom = 0, right = 0;
      std::vector<Chan> chans;
      char key[4] = { 'n', 'o', 'r', 'm' };
      int opacity = 255, clip = 0, flags = 0;
      int mTop = 0, mLeft = 0, mBottom = 0, mRight = 0, mDefault = 255, mFlags = 0;
      bool hasMask = false;
      std::string name;
      int section = 0;   // lsct: 1 open folder, 2 closed folder, 3 end of a folder
      char sectionKey[4] = { 0, 0, 0, 0 };
      int fill = 255;
      std::vector<uint8_t> rgba, mask;
   };
   std::vector<Rec> recs;

   // Layer records and their channel data; `layerEnd` bounds the block.
   auto readLayers = [&](size_t layerEnd) -> bool
   {
      const int count = std::abs((int)r.I16());
      for (int i = 0; i < count && !r.bad; i++)
      {
         Rec rec;
         rec.top = r.I32(); rec.left = r.I32(); rec.bottom = r.I32(); rec.right = r.I32();
         const int nc = r.U16();
         for (int c = 0; c < nc; c++)
         {
            Chan ch;
            ch.id = r.I16();
            ch.len = r.U32();
            rec.chans.push_back(ch);
         }
         char s8[4];
         r.Tag(s8);
         r.Tag(rec.key);
         rec.opacity = r.U8();
         rec.clip = r.U8();
         rec.flags = r.U8();
         r.U8();
         const uint32_t extra = r.U32();
         const size_t extraEnd = r.at + extra;
         const uint32_t maskLen = r.U32();
         const size_t maskEnd = r.at + maskLen;
         if (maskLen >= 20)
         {
            rec.mTop = r.I32(); rec.mLeft = r.I32(); rec.mBottom = r.I32(); rec.mRight = r.I32();
            rec.mDefault = r.U8();
            rec.mFlags = r.U8();
            rec.hasMask = true;
         }
         r.at = std::min(maskEnd, r.n);
         r.Skip(r.U32());   // blending ranges
         const int nameLen = r.U8();
         std::string pas;
         for (int k = 0; k < nameLen && !r.bad; k++)
            pas += (char)r.U8();
         r.Skip((4 - (1 + nameLen) % 4) % 4);
         rec.name = pas;
         while (r.at + 12 <= extraEnd && !r.bad)
         {
            char bsig[4], key[4];
            r.Tag(bsig);
            r.Tag(key);
            const uint32_t len = r.U32();
            const size_t end = r.at + len;
            if (std::memcmp(key, "luni", 4) == 0)
            {
               const uint32_t n = r.U32();
               std::vector<uint16_t> u;
               for (uint32_t k = 0; k < n && r.at + 2 <= end; k++)
                  u.push_back(r.U16());
               rec.name = Utf16ToUtf8(u);
            }
            else if (std::memcmp(key, "lsct", 4) == 0)
            {
               rec.section = (int)r.U32();
               if (len >= 12)
               {
                  char s[4];
                  r.Tag(s);
                  r.Tag(rec.sectionKey);
               }
            }
            else if (std::memcmp(key, "iOpa", 4) == 0)
               rec.fill = r.U8();
            r.at = std::min(end, r.n);
         }
         r.at = std::min(extraEnd, r.n);
         recs.push_back(std::move(rec));
      }
      // Channel image data, layer by layer.
      for (Rec& rec : recs)
      {
         const int w = rec.right - rec.left, h = rec.bottom - rec.top;
         const int mw = rec.mRight - rec.mLeft, mh = rec.mBottom - rec.mTop;
         if (w > 0 && h > 0)
         {
            rec.rgba.assign((size_t)w * h * 4, 0);
            for (size_t i = 3; i < rec.rgba.size(); i += 4)
               rec.rgba[i] = 255;   // opaque unless an alpha channel says otherwise
         }
         std::vector<uint8_t> plane;
         for (const Chan& ch : rec.chans)
         {
            const size_t start = r.at;
            if (ch.len < 2 || !r.Has(ch.len)) { r.at = std::min(start + ch.len, r.n); continue; }
            const int comp = r.U16();
            const bool isMask = ch.id == -2;
            const int cw = isMask ? mw : w, chh = isMask ? mh : h;
            if (cw > 0 && chh > 0 && (ch.id >= -2) && ReadChannel(r, comp, cw, chh, depth, ch.len - 2, plane))
            {
               if (isMask)
               {
                  rec.mask = plane;
               }
               else
               {
                  const int slot = ch.id == -1 ? 3 : ch.id;
                  if (slot >= 0 && slot <= 3)
                     for (size_t k = 0; k < plane.size(); k++)
                     {
                        if (grey && slot == 0)
                           rec.rgba[k * 4] = rec.rgba[k * 4 + 1] = rec.rgba[k * 4 + 2] = plane[k];
                        else if (!grey || slot == 3)
                           rec.rgba[k * 4 + slot] = plane[k];
                     }
               }
            }
            r.at = std::min(start + ch.len, r.n);
         }
      }
      r.at = std::min(layerEnd, r.n);
      return !r.bad;
   };

   if (lmLen > 0)
   {
      const uint32_t liLen = r.U32();
      const size_t liEnd = r.at + liLen;
      if (liLen > 0)
         readLayers(liEnd);
      r.at = std::min(liEnd, r.n);
      // Global mask info, then extra blocks: a 16 / 32-bit file keeps its layers in Lr16 / Lr32.
      if (r.Has(4))
         r.Skip(r.U32());
      while (recs.empty() && r.at + 12 <= lmEnd && !r.bad)
      {
         char bsig[4], key[4];
         r.Tag(bsig);
         r.Tag(key);
         const uint32_t len = r.U32();
         const size_t end = r.at + len;
         if (std::memcmp(key, "Lr16", 4) == 0 || std::memcmp(key, "Lr32", 4) == 0 || std::memcmp(key, "Layr", 4) == 0)
            readLayers(end);
         r.at = std::min(end + (len & 1), r.n);
      }
   }
   r.at = std::min(lmEnd, r.n);

   ResetForFile(W, H);
   if (recs.empty())
   {
      // A flat file: the merged picture, planar.
      const int comp = r.U16();
      std::vector<uint32_t> counts;
      if (comp == 1)
         for (int k = 0; k < channels * H; k++)
            counts.push_back(r.U16());
      std::vector<uint8_t> rgba((size_t)W * H * 4, 255), plane;
      const int use = grey ? std::min(channels, 2) : std::min(channels, 4);
      for (int c = 0; c < use && !r.bad; c++)
      {
         std::vector<uint32_t> rows;
         if (comp == 1)
            rows.assign(counts.begin() + (size_t)c * H, counts.begin() + (size_t)(c + 1) * H);
         if (!ReadChannel(r, comp, W, H, depth, 0, plane, comp == 1 ? &rows : nullptr))
            return fail("damaged picture data");
         const int slot = grey ? (c == 0 ? 0 : 3) : c;
         for (size_t k = 0; k < plane.size(); k++)
         {
            if (grey && slot == 0)
               rgba[k * 4] = rgba[k * 4 + 1] = rgba[k * 4 + 2] = plane[k];
            else
               rgba[k * 4 + slot] = plane[k];
         }
      }
      AddFileLayer(0, "Background", 0, 0, W, H, rgba);
      Commit();
      return true;
   }

   // Records run bottom to top; a folder's end marker comes first, its header last.
   std::vector<int> open;   // group ids
   for (Rec& rec : recs)
   {
      const int parent = open.empty() ? 0 : open.back();
      if (rec.section == 3)
      {
         Layer& g = AddLayer(Kind::Group, 0, 0);
         g.parent = parent;
         open.push_back(g.id);
         continue;
      }
      Layer* l = nullptr;
      if (rec.section == 1 || rec.section == 2)
      {
         if (open.empty())
            continue;
         l = FindLayer(open.back());
         open.pop_back();
         l->open = rec.section == 1;
         l->blend = rec.sectionKey[0] != 0 ? BlendFromPsd(rec.sectionKey) : BlendFromPsd(rec.key);
      }
      else
      {
         const int w = rec.right - rec.left, h = rec.bottom - rec.top;
         const int id = AddFileLayer(parent, rec.name, rec.left, rec.top, w, h, rec.rgba);
         l = FindLayer(id);
         l->blend = BlendFromPsd(rec.key);
         l->clip = rec.clip != 0;
         l->fill = rec.fill / 255.0f;
         const int mw = rec.mRight - rec.mLeft, mh = rec.mBottom - rec.mTop;
         if (rec.hasMask && w > 0 && h > 0 && mw > 0 && mh > 0 && rec.mask.size() == (size_t)mw * mh)
         {
            // The mask sized to the layer's box; outside its own rect it reads its default colour.
            std::vector<uint8_t> m((size_t)w * h * 4, 255);
            for (int y = 0; y < h; y++)
               for (int x = 0; x < w; x++)
               {
                  const int mx = rec.left + x - rec.mLeft, my = rec.top + y - rec.mTop;
                  const uint8_t v = (mx >= 0 && my >= 0 && mx < mw && my < mh) ? rec.mask[(size_t)my * mw + mx] : (uint8_t)rec.mDefault;
                  uint8_t* q = &m[((size_t)y * w + x) * 4];
                  q[0] = q[1] = q[2] = v;
               }
            l->mw = w;
            l->mh = h;
            l->mtiles = CutTiles(m, w, h, true);
            l->pmaskOn = (rec.mFlags & 2) == 0;
         }
      }
      l->name = rec.name;
      l->opacity = rec.opacity / 255.0f;
      l->visible = (rec.flags & 2) == 0;
   }
   Commit();
   return true;
}

bool Scene2DNode::SavePsd(std::vector<uint8_t>& out, int frameId)
{
   Layers();
   struct ExportScope { bool was = sExport; ExportScope() { sExport = true; } ~ExportScope() { sExport = was; } } exporting;
   CookIfNeeded(frameId);
   const int W = mDoc.w, H = mDoc.h;
   out.clear();
   Writer wr{ out };
   wr.Tag("8BPS");
   wr.U16(1);
   for (int i = 0; i < 6; i++) wr.U8(0);
   wr.U16(4);
   wr.U32((uint32_t)H);
   wr.U32((uint32_t)W);
   wr.U16(8);
   wr.U16(3);
   wr.U32(0);   // colour mode data
   wr.U32(0);   // image resources

   struct Out
   {
      std::string name;
      int blend = 0, opacity = 255, section = 0;
      bool visible = true, clip = false, open = true;
      FilePixels px;
   };
   std::vector<Out> outs;
   auto alone = [this](int id, int f, std::vector<uint8_t>& px) { return RenderLayerAlone(id, f, px); };
   std::function<void(int)> walk = [&](int parent)
   {
      for (size_t i = 0; i < mLayers.size(); i++)
      {
         const Layer l = mLayers[i];
         if (l.parent != parent || l.kind == Kind::Adjust || l.noExport)
            continue;
         if (l.kind == Kind::Group)
         {
            Out end;
            end.name = "</Layer group>";
            end.section = 3;
            outs.push_back(end);
            walk(l.id);
            Out head;
            head.name = l.name.empty() ? "Group" : l.name;
            head.section = l.open ? 1 : 2;
            head.blend = l.blend;
            head.opacity = (int)std::lround(std::clamp(l.opacity, 0.0f, 1.0f) * 255.0f);
            head.visible = l.visible;
            outs.push_back(head);
            continue;
         }
         Out o;
         o.name = l.name.empty() ? "Layer " + std::to_string(l.id) : l.name;
         o.blend = l.blend;
         o.opacity = (int)std::lround(std::clamp(l.opacity, 0.0f, 1.0f) * 255.0f);
         o.visible = l.visible;
         o.clip = l.clip;
         GrabLayer(*this, l, frameId, true, o.px, alone);
         outs.push_back(std::move(o));
      }
   };
   walk(0);

   // Planes per layer: alpha, R, G, B, then the mask.
   auto plane = [](const std::vector<uint8_t>& rgba, int c) {
      std::vector<uint8_t> p(rgba.size() / 4);
      for (size_t k = 0; k < p.size(); k++)
         p[k] = rgba[k * 4 + c];
      return p;
   };

   const size_t lm = wr.Mark();
   const size_t li = wr.Mark();
   wr.U16((uint16_t)(int16_t)(-(int)outs.size()));
   std::vector<std::vector<std::vector<uint8_t>>> planes(outs.size());
   for (size_t i = 0; i < outs.size(); i++)
   {
      Out& o = outs[i];
      const FilePixels& p = o.px;
      const bool has = p.w > 0 && p.h > 0;
      wr.U32((uint32_t)(has ? p.y : 0));
      wr.U32((uint32_t)(has ? p.x : 0));
      wr.U32((uint32_t)(has ? p.y + p.h : 0));
      wr.U32((uint32_t)(has ? p.x + p.w : 0));
      const int ids[5] = { -1, 0, 1, 2, -2 };
      const int nch = (has && p.hasMask) ? 5 : 4;
      wr.U16((uint16_t)nch);
      for (int c = 0; c < nch; c++)
      {
         std::vector<uint8_t> pl;
         if (has)
            pl = plane(c == 4 ? p.mask : p.rgba, c == 0 ? 3 : (c == 4 ? 0 : c - 1));
         wr.U16((uint16_t)(int16_t)ids[c]);
         wr.U32((uint32_t)(has ? RleSize(pl, p.w, p.h) : 2));
         planes[i].push_back(std::move(pl));
      }
      wr.Tag("8BIM");
      wr.Tag(PsdKey(o.section != 0 ? o.blend : (o.blend == kPassThrough ? 0 : o.blend)));
      wr.U8((uint8_t)o.opacity);
      wr.U8(o.clip ? 1 : 0);
      wr.U8((uint8_t)((o.visible ? 0 : 2) | 8 | (o.section == 3 ? 16 : 0)));
      wr.U8(0);
      const size_t extra = wr.Mark();
      if (has && p.hasMask)
      {
         wr.U32(20);
         wr.U32((uint32_t)p.y); wr.U32((uint32_t)p.x); wr.U32((uint32_t)(p.y + p.h)); wr.U32((uint32_t)(p.x + p.w));
         wr.U8(255);
         wr.U8(p.maskOn ? 0 : 2);
         wr.U16(0);
      }
      else
         wr.U32(0);
      wr.U32(0);   // blending ranges
      std::string pas = o.name.substr(0, 255);
      wr.U8((uint8_t)pas.size());
      wr.Bytes((const uint8_t*)pas.data(), pas.size());
      while ((pas.size() + 1) % 4 != 0)
      {
         wr.U8(0);
         pas += '\0';
      }
      wr.Tag("8BIM");
      wr.Tag("luni");
      const size_t lu = wr.Mark();
      const std::vector<uint16_t> u = Utf8ToUtf16(o.name);
      wr.U32((uint32_t)u.size());
      for (uint16_t c : u) wr.U16(c);
      wr.Fill(lu, 4);
      if (o.section != 0)
      {
         wr.Tag("8BIM");
         wr.Tag("lsct");
         const size_t ls = wr.Mark();
         wr.U32((uint32_t)o.section);
         if (o.section != 3)
         {
            wr.Tag("8BIM");
            wr.Tag(PsdKey(o.blend));
         }
         wr.Fill(ls);
      }
      wr.Fill(extra);
   }
   for (size_t i = 0; i < outs.size(); i++)
   {
      const FilePixels& p = outs[i].px;
      for (const std::vector<uint8_t>& pl : planes[i])
      {
         if (p.w > 0 && p.h > 0)
            WriteChannelRle(wr, pl, p.w, p.h);
         else
            wr.U16(0);
      }
   }
   wr.Fill(li, 4);
   wr.U32(0);   // global layer mask info
   wr.Fill(lm);

   // The merged picture, planar R, G, B, A.
   std::vector<uint8_t> merged;
   int mw = 0, mh = 0;
   if (!ReadOutput(merged, mw, mh) || mw != W || mh != H)
      merged.assign((size_t)W * H * 4, 0);
   else
      FlipRows(merged, W, H);
   std::vector<std::vector<uint8_t>> mp;
   for (int c = 0; c < 4; c++)
      mp.push_back(plane(merged, c));
   wr.U16(1);
   std::vector<std::vector<uint8_t>> rows;
   for (int c = 0; c < 4; c++)
      for (int y = 0; y < H; y++)
      {
         rows.emplace_back();
         PackRow(&mp[c][(size_t)y * W], W, rows.back());
      }
   for (const auto& row : rows) wr.U16((uint16_t)row.size());
   for (const auto& row : rows) wr.Bytes(row.data(), row.size());
   return true;
}

// ---- OpenRaster ---------------------------------------------------------------------

bool Scene2DNode::LoadOra(const std::vector<uint8_t>& bytes, std::string* err)
{
   auto fail = [&](const char* why) {
      if (err != nullptr) *err = why;
      return false;
   };
   mz_zip_archive zip;
   std::memset(&zip, 0, sizeof zip);
   if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
      return fail("not an ORA (zip) file");
   auto file = [&](const std::string& name, std::vector<uint8_t>& data) {
      size_t n = 0;
      void* p = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &n, 0);
      if (p == nullptr)
         return false;
      data.assign((uint8_t*)p, (uint8_t*)p + n);
      mz_free(p);
      return true;
   };
   std::vector<uint8_t> xmlBytes;
   if (!file("stack.xml", xmlBytes))
   {
      mz_zip_reader_end(&zip);
      return fail("no stack.xml");
   }
   const std::string xml(xmlBytes.begin(), xmlBytes.end());

   struct Node
   {
      bool stack = false;
      std::map<std::string, std::string> at;
      std::vector<Node> kids;   // top first, as written
   };
   auto attrs = [](const std::string& tag) {
      std::map<std::string, std::string> m;
      size_t i = 0;
      while ((i = tag.find('=', i)) != std::string::npos)
      {
         size_t k = i;
         while (k > 0 && std::isspace((unsigned char)tag[k - 1])) k--;
         size_t s = k;
         while (s > 0 && !std::isspace((unsigned char)tag[s - 1])) s--;
         const std::string key = tag.substr(s, k - s);
         size_t q = tag.find_first_of("\"'", i);
         if (q == std::string::npos) break;
         const size_t e = tag.find(tag[q], q + 1);
         if (e == std::string::npos) break;
         m[key] = XmlUnescape(tag.substr(q + 1, e - q - 1));
         i = e + 1;
      }
      return m;
   };
   Node root;
   root.stack = true;
   std::vector<Node*> path = { &root };
   int W = 0, H = 0;
   bool sawRootStack = false;
   for (size_t i = 0; (i = xml.find('<', i)) != std::string::npos;)
   {
      const size_t e = xml.find('>', i);
      if (e == std::string::npos) break;
      const std::string tag = xml.substr(i + 1, e - i - 1);
      i = e + 1;
      if (tag.empty() || tag[0] == '?' || tag[0] == '!')
         continue;
      if (tag.rfind("image", 0) == 0)
      {
         auto a = attrs(tag);
         W = std::atoi(a["w"].c_str());
         H = std::atoi(a["h"].c_str());
      }
      else if (tag.rfind("stack", 0) == 0)
      {
         if (!sawRootStack)
         {
            sawRootStack = true;   // the image's own stack
            continue;
         }
         Node n;
         n.stack = true;
         n.at = attrs(tag);
         path.back()->kids.push_back(n);
         Node* added = &path.back()->kids.back();
         if (tag.back() != '/')
            path.push_back(added);
      }
      else if (tag == "/stack")
      {
         if (path.size() > 1)
            path.pop_back();
      }
      else if (tag.rfind("layer", 0) == 0)
      {
         Node n;
         n.at = attrs(tag);
         path.back()->kids.push_back(n);
      }
   }
   if (W <= 0 || H <= 0)
   {
      mz_zip_reader_end(&zip);
      return fail("stack.xml has no size");
   }
   ResetForFile(W, H);
   auto num = [](const std::map<std::string, std::string>& a, const char* k, float def) {
      auto it = a.find(k);
      return it == a.end() || it->second.empty() ? def : (float)std::atof(it->second.c_str());
   };
   auto str = [](const std::map<std::string, std::string>& a, const char* k) {
      auto it = a.find(k);
      return it == a.end() ? std::string() : it->second;
   };
   std::function<void(const Node&, int)> emit = [&](const Node& n, int parent)
   {
      // Written top first; the document is bottom first.
      for (auto it = n.kids.rbegin(); it != n.kids.rend(); ++it)
      {
         const Node& k = *it;
         Layer* l = nullptr;
         if (k.stack)
         {
            Layer& g = AddLayer(Kind::Group, 0, 0);
            g.parent = parent;
            const int gid = g.id;
            emit(k, gid);
            l = FindLayer(gid);
            const std::string op = str(k.at, "composite-op");
            const bool isolate = str(k.at, "isolation") == "isolate";
            l->blend = (isolate || (!op.empty() && op != "svg:src-over")) ? BlendFromOra(op) : kPassThrough;
         }
         else
         {
            std::vector<uint8_t> png, rgba;
            int w = 0, h = 0;
            if (file(str(k.at, "src"), png))
            {
               int c = 0;
               if (unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &c, 4))
               {
                  rgba.assign(px, px + (size_t)w * h * 4);
                  stbi_image_free(px);
               }
            }
            const int id = AddFileLayer(parent, "", (int)num(k.at, "x", 0), (int)num(k.at, "y", 0), w, h, rgba);
            l = FindLayer(id);
            l->blend = BlendFromOra(str(k.at, "composite-op"));
         }
         l->name = str(k.at, "name");
         l->opacity = std::clamp(num(k.at, "opacity", 1.0f), 0.0f, 1.0f);
         l->visible = str(k.at, "visibility") != "hidden";
      }
   };
   emit(root, 0);
   mz_zip_reader_end(&zip);
   Commit();
   return true;
}

bool Scene2DNode::SaveOra(std::vector<uint8_t>& out, int frameId)
{
   Layers();
   struct ExportScope { bool was = sExport; ExportScope() { sExport = true; } ~ExportScope() { sExport = was; } } exporting;
   CookIfNeeded(frameId);
   const int W = mDoc.w, H = mDoc.h;
   mz_zip_archive zip;
   std::memset(&zip, 0, sizeof zip);
   if (!mz_zip_writer_init_heap(&zip, 0, 0))
      return false;
   const char mime[] = "image/openraster";
   bool ok = mz_zip_writer_add_mem(&zip, "mimetype", mime, sizeof mime - 1, MZ_NO_COMPRESSION);
   auto add = [&](const std::string& name, const std::vector<uint8_t>& data) {
      ok = ok && mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_BEST_SPEED);
   };
   auto alone = [this](int id, int f, std::vector<uint8_t>& px) { return RenderLayerAlone(id, f, px); };
   std::ostringstream x;
   x << "<?xml version='1.0' encoding='UTF-8'?>\n<image version=\"0.0.5\" w=\"" << W << "\" h=\"" << H << "\">\n";
   int n = 0;
   std::function<void(int, int)> walk = [&](int parent, int indent)
   {
      const std::string pad((size_t)indent, ' ');
      for (size_t i = mLayers.size(); i-- > 0;)
      {
         const Layer l = mLayers[i];
         if (l.parent != parent || l.kind == Kind::Adjust || l.noExport)
            continue;
         const std::string common = " name=\"" + XmlEscape(l.name.empty() ? "Layer " + std::to_string(l.id) : l.name) +
                                    "\" opacity=\"" + std::to_string(std::clamp(l.opacity, 0.0f, 1.0f)) +
                                    "\" visibility=\"" + (l.visible ? "visible" : "hidden") + "\"";
         if (l.kind == Kind::Group)
         {
            const bool pass = l.blend == kPassThrough;
            x << pad << "<stack" << common << " composite-op=\"" << OraOp(pass ? 0 : l.blend) << "\" isolation=\""
              << (pass ? "auto" : "isolate") << "\">\n";
            walk(l.id, indent + 1);
            x << pad << "</stack>\n";
            continue;
         }
         FilePixels p;
         GrabLayer(*this, l, frameId, false, p, alone);
         const std::string src = "data/layer" + std::to_string(n++) + ".png";
         if (p.w <= 0 || p.h <= 0)
         {
            p.w = p.h = 1;
            p.rgba.assign(4, 0);
         }
         add(src, EncodePng(p.rgba, p.w, p.h));
         x << pad << "<layer" << common << " src=\"" << src << "\" x=\"" << p.x << "\" y=\"" << p.y << "\" composite-op=\""
           << OraOp(l.blend) << "\"/>\n";
      }
   };
   x << "<stack>\n";
   walk(0, 1);
   x << "</stack>\n</image>\n";
   const std::string xs = x.str();
   add("stack.xml", std::vector<uint8_t>(xs.begin(), xs.end()));
   std::vector<uint8_t> merged;
   int mw = 0, mh = 0;
   if (!ReadOutput(merged, mw, mh) || mw != W || mh != H)
      merged.assign((size_t)W * H * 4, 0);
   else
      FlipRows(merged, W, H);
   add("mergedimage.png", EncodePng(merged, W, H));
   // Thumbnail: at most 256 px on the long side, box-averaged.
   const float s = std::min(1.0f, 256.0f / (float)std::max(W, H));
   const int tw = std::max(1, (int)(W * s)), th = std::max(1, (int)(H * s));
   std::vector<uint8_t> thumb((size_t)tw * th * 4);
   for (int y = 0; y < th; y++)
      for (int xx = 0; xx < tw; xx++)
      {
         const int x0 = xx * W / tw, x1 = std::max(x0 + 1, (xx + 1) * W / tw);
         const int y0 = y * H / th, y1 = std::max(y0 + 1, (y + 1) * H / th);
         unsigned sum[4] = { 0, 0, 0, 0 }, cnt = 0;
         for (int sy = y0; sy < y1; sy++)
            for (int sx = x0; sx < x1; sx++, cnt++)
               for (int c = 0; c < 4; c++)
                  sum[c] += merged[((size_t)sy * W + sx) * 4 + c];
         for (int c = 0; c < 4; c++)
            thumb[((size_t)y * tw + xx) * 4 + c] = (uint8_t)(sum[c] / std::max(1u, cnt));
      }
   add("Thumbnails/thumbnail.png", EncodePng(thumb, tw, th));
   void* buf = nullptr;
   size_t size = 0;
   ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
   if (ok && buf != nullptr)
      out.assign((uint8_t*)buf, (uint8_t*)buf + size);
   mz_free(buf);
   mz_zip_writer_end(&zip);
   return ok;
}

// Bitmap fill layers: the file's picture as a texture, rows top first (the fill samples it by document uv, y down).
unsigned Scene2DNode::FillTexture(const std::string& path, int& w, int& h)
{
   if (path.empty())
      return 0;
   auto it = mFillPics.find(path);
   if (it == mFillPics.end())
   {
      FillPic pic;
      int c = 0;
      stbi_set_flip_vertically_on_load_thread(0);
      std::ifstream in(AppPaths::FsPath(path), std::ios::binary);
      const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (unsigned char* px = bytes.empty() ? nullptr
                                : stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &pic.w, &pic.h, &c, 4))
      {
         glGenTextures(1, &pic.tex);
         glBindTexture(GL_TEXTURE_2D, pic.tex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pic.w, pic.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glBindTexture(GL_TEXTURE_2D, 0);
         stbi_image_free(px);
      }
      if (mFillPics.size() > 16)
      {
         for (auto& kv : mFillPics)
            if (kv.second.tex != 0)
               glDeleteTextures(1, &kv.second.tex);
         mFillPics.clear();
      }
      it = mFillPics.emplace(path, pic).first;
   }
   w = it->second.w;
   h = it->second.h;
   return it->second.tex;
}
