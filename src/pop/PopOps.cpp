#include "PopOps.h"

#include <algorithm>

namespace pop
{
   namespace
   {
      ParamSpec Make(const char* key, const char* label, ParamSpec::Type t, const char* showIf)
      {
         ParamSpec p;
         p.key = key;
         p.label = label;
         p.type = t;
         p.showIf = showIf ? showIf : "";
         return p;
      }
   }

   ParamSpec F(const char* key, const char* label, float def, float lo, float hi, const char* showIf)
   {
      ParamSpec p = Make(key, label, ParamSpec::kFloat, showIf);
      p.def[0] = def; p.lo = lo; p.hi = hi;
      return p;
   }
   ParamSpec I(const char* key, const char* label, int def, int lo, int hi, const char* showIf)
   {
      ParamSpec p = Make(key, label, ParamSpec::kInt, showIf);
      p.def[0] = (float)def; p.lo = (float)lo; p.hi = (float)hi;
      return p;
   }
   ParamSpec B(const char* key, const char* label, bool def, const char* showIf)
   {
      ParamSpec p = Make(key, label, ParamSpec::kBool, showIf);
      p.def[0] = def ? 1.0f : 0.0f; p.hi = 1.0f;
      return p;
   }
   ParamSpec M(const char* key, const char* label, std::vector<std::string> items, int def, const char* showIf)
   {
      ParamSpec p = Make(key, label, ParamSpec::kMenu, showIf);
      p.menu = std::move(items);
      p.def[0] = (float)def; p.hi = (float)std::max<size_t>(1, p.menu.size()) - 1.0f;
      return p;
   }
   ParamSpec C(const char* key, const char* label, float r, float g, float b, const char* showIf)
   {
      ParamSpec p = Make(key, label, ParamSpec::kColor, showIf);
      p.def[0] = r; p.def[1] = g; p.def[2] = b;
      return p;
   }
   ParamSpec T(const char* key, const char* label, const char* def, const char* showIf)
   {
      ParamSpec p = Make(key, label, ParamSpec::kText, showIf);
      p.defText = def ? def : "";
      return p;
   }
   ParamSpec V(const char* key, const char* label, float x, float y, float z, float lo, float hi, const char* showIf)
   {
      ParamSpec p = Make(key, label, ParamSpec::kVec3, showIf);
      p.def[0] = x; p.def[1] = y; p.def[2] = z; p.lo = lo; p.hi = hi;
      return p;
   }

   // --- PopDef ----------------------------------------------------------------
   int PopDef::ParamIndex(const std::string& key) const
   {
      for (size_t i = 0; i < params.size(); i++)
         if (params[i].key == key)
            return (int)i;
      return -1;
   }

   int PopDef::FloatOffset(int paramIndex) const
   {
      int off = 0;
      for (int i = 0; i < paramIndex && i < (int)params.size(); i++)
         off += params[(size_t)i].Floats();
      return off;
   }

   void PopDef::Defaults(ParamValues& v) const
   {
      v.f.clear();
      v.s.assign(params.size(), std::string());
      for (size_t i = 0; i < params.size(); i++)
      {
         const ParamSpec& p = params[i];
         for (int k = 0; k < p.Floats(); k++)
            v.f.push_back(p.def[k]);
         if (p.type == ParamSpec::kText)
            v.s[i] = p.defText;
      }
   }

   // --- CookCtx ---------------------------------------------------------------
   float CookCtx::F(const std::string& key) const
   {
      const int i = def->ParamIndex(key);
      if (i < 0)
         return 0.0f;
      const int off = def->FloatOffset(i);
      return (off < (int)values->f.size()) ? values->f[(size_t)off] : 0.0f;
   }

   void CookCtx::V3(const std::string& key, float out3[3]) const
   {
      out3[0] = out3[1] = out3[2] = 0.0f;
      const int i = def->ParamIndex(key);
      if (i < 0)
         return;
      const int off = def->FloatOffset(i);
      for (int k = 0; k < 3 && off + k < (int)values->f.size(); k++)
         out3[k] = values->f[(size_t)(off + k)];
   }

   const std::string& CookCtx::S(const std::string& key) const
   {
      static const std::string kEmpty;
      const int i = def->ParamIndex(key);
      return (i >= 0 && i < (int)values->s.size()) ? values->s[(size_t)i] : kEmpty;
   }

   std::vector<KernelUniform> CookCtx::ParamUniforms() const
   {
      std::vector<KernelUniform> out;
      int off = 0;
      for (const ParamSpec& p : def->params)
      {
         KernelUniform u;
         u.name = "p_" + p.key;
         const int n = p.Floats();
         switch (p.type)
         {
            case ParamSpec::kFloat:
               u.type = KernelUniform::kFloat;
               u.v[0] = values->f[(size_t)off];
               break;
            case ParamSpec::kInt:
            case ParamSpec::kBool:
            case ParamSpec::kMenu:
               u.type = KernelUniform::kInt;
               u.i = (int)values->f[(size_t)off];
               break;
            case ParamSpec::kVec3:
            case ParamSpec::kColor:
               u.type = KernelUniform::kVec3;
               for (int k = 0; k < 3; k++)
                  u.v[k] = values->f[(size_t)(off + k)];
               break;
            case ParamSpec::kText:
               break;
         }
         off += n;
         if (p.type != ParamSpec::kText)
            out.push_back(u);
      }
      return out;
   }

   bool CookCtx::Kernel(const KernelSpec& spec, const PopData& src, const std::vector<KernelUniform>& extra)
   {
      std::vector<KernelUniform> u = ParamUniforms();
      u.insert(u.end(), extra.begin(), extra.end());
      std::vector<BufferPtr> result;
      if (!RunKernel(spec, src, u, env, result, error))
         return false;
      if (!out)
         out = ShallowCopy(src);
      for (size_t i = 0; i < spec.writes.size() && i < result.size(); i++)
         out->Set(spec.writes[i].name, result[i]);
      return true;
   }

   // --- registry ----------------------------------------------------------------
   const std::vector<PopDef>& Registry()
   {
      static std::vector<PopDef> defs = [] {
         std::vector<PopDef> d;
         RegisterTierA(d);
         RegisterTierB(d);
         RegisterPlatformOps(d);
         // tdName names the TouchDesigner operator each one follows.
         for (PopDef& def : d)
         {
            std::string td = def.tdName.empty() ? def.name : def.tdName;
            static const std::pair<const char*, const char*> kMap[] = {
               { "Geometry to PointZ", "SOP to POP" }, { "Image to PointZ", "TOP to POP" },
               { "Signal to PointZ", "CHOP to POP" }, { "Table to PointZ", "DAT to POP" } };
            for (const auto& m : kMap)
               if (td == m.first) td = m.second;
            const size_t z = td.rfind(" PointZ");
            if (z != std::string::npos && z + 7 == td.size())
               td = td.substr(0, z) + " POP";
            def.tdName = td;
         }
         return d;
      }();
      return defs;
   }

   const PopDef* FindDef(const std::string& name)
   {
      for (const PopDef& d : Registry())
         if (d.name == name)
            return &d;
      return nullptr;
   }

   // --- helpers ---------------------------------------------------------------
   bool PassInput(CookCtx& c)
   {
      const PopData* in = c.Input(0);
      if (in == nullptr)
      {
         c.out = std::make_shared<PopData>();
         c.out->topo = NewTopo();
         return false;
      }
      c.out = ShallowCopy(*in);
      return true;
   }

   std::shared_ptr<PopTopology> NewTopo() { return std::make_shared<PopTopology>(); }

   std::shared_ptr<PopData> FromPoints(std::vector<float>&& P, bool asPointPrims)
   {
      auto out = std::make_shared<PopData>();
      out->pointCount = (int)(P.size() / 3);
      out->Set(attr::P, MakeBuffer(std::move(P), 3));
      auto topo = NewTopo();
      topo->pointPrims = asPointPrims;
      out->topo = topo;
      return out;
   }

   PopDef KernelOp(const char* name, const char* td, const char* help, std::vector<ParamSpec> params,
                   std::vector<KernelIO> reads, std::vector<KernelIO> writes, const char* body,
                   bool masked, bool everyFrame)
   {
      PopDef d;
      d.name = name;
      d.tdName = td;
      d.help = help;
      d.inputs = 1;
      d.everyFrame = everyFrame;
      d.tier = 'A';
      d.params = std::move(params);
      if (masked)
         d.params.push_back(T("mask", "mask attribute", ""));
      const std::string bodyText = body;
      d.cook = [reads, writes, bodyText, masked](CookCtx& c) -> bool {
         if (!PassInput(c))
            return true;
         KernelSpec spec;
         spec.reads = reads;
         for (const KernelIO& w : writes)
         {
            bool have = false;
            for (const KernelIO& r : spec.reads)
               have = have || r.name == w.name;
            if (!have)
               spec.reads.push_back(w);
         }
         spec.writes = writes;
         std::string pre, post;
         const std::string mask = masked ? c.S("mask") : std::string();
         if (!mask.empty())
         {
            if (!ValidAttribName(mask))
            {
               c.warning = "mask '" + mask + "' is not an attribute name";
            }
            else
            {
               bool have = false;
               for (const KernelIO& r : spec.reads)
                  have = have || r.name == mask;
               if (!have)
                  spec.reads.push_back({ mask, 1 });
               for (const KernelIO& w : writes)
                  pre += std::string("   ") + GlslType(w.comps) + " old_" + w.name + " = " + w.name + ";\n";
               post += "   float mask_ = clamp(float(" + mask + "), 0.0, 1.0);\n";
               for (const KernelIO& w : writes)
                  post += "   " + w.name + " = mix(old_" + w.name + ", " + w.name + ", mask_);\n";
            }
         }
         spec.body = pre + "   {\n" + bodyText + "\n   }\n" + post;
         return c.Kernel(spec, *c.Input(0));
      };
      return d;
   }
}
