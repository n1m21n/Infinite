#include "ParticleGpu.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "platform/OpenGLHeaders.h"
#include "GpuAlgo.h"
#include "PopData.h"
#include "PopGpu.h"
#include "PopOps.h"

namespace pop
{
   namespace
   {
      // Particles born on the same frame age together. The CPU knows how many
      // are alive by keeping, per birth frame, their age (a float accumulated
      // exactly as the kernel accumulates PartAge) and their life spans (the
      // very float values uploaded to the GPU), sorted: the kernel's death test
      // and this one are the same float comparison, so the counts agree.
      struct Cohort
      {
         float age = 0.0f;
         std::vector<float> lives; // ascending; an immortal (life <= 0) is +inf
         size_t dead = 0;
      };

      struct ParticleGpuState : OpState
      {
         bool init = false;
         std::shared_ptr<PopData> parts;
         int alive = 0;
         double carry = 0.0;
         double time = 0.0;
         float nextId = 0.0f;
         uint32_t rng = 0x9e3779b9u;
         std::deque<Cohort> cohorts;
         // A PartDeath attribute from the force chain kills on GPU data the CPU
         // cannot predict: from the first one on, the count comes from the GPU
         // (a 4-byte readback a frame) and the cohorts are dropped.
         bool cohortsValid = true;
         // The CPU engine's generator, drawn in the same order (ParticleState::Rnd).
         float Rnd()
         {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return (float)(rng & 0xffffff) / 16777215.0f;
         }
      };

      KernelIO IO(const char* name, int comps)
      {
         KernelIO io;
         io.name = name;
         io.comps = comps;
         return io;
      }

      KernelUniform UF(const char* name, float v)
      {
         KernelUniform u;
         u.name = name;
         u.type = KernelUniform::kFloat;
         u.v[0] = v;
         return u;
      }

      KernelUniform U3(const char* name, const float v[3])
      {
         KernelUniform u;
         u.name = name;
         u.type = KernelUniform::kVec3;
         u.v[0] = v[0];
         u.v[1] = v[1];
         u.v[2] = v[2];
         return u;
      }

      std::shared_ptr<PopData> EmptyParts()
      {
         auto d = std::make_shared<PopData>();
         d->pointCount = 0;
         d->topo = std::make_shared<PopTopology>();
         return d;
      }

      BufferPtr Owned(unsigned int id, int count, int comps) { return std::make_shared<PopBuffer>(id, count, comps); }

      unsigned int Upload(const void* data, size_t bytes)
      {
         const unsigned int b = AcquireGpuBuffer(bytes);
         glBindBuffer(GL_COPY_WRITE_BUFFER, b);
         glBufferSubData(GL_COPY_WRITE_BUFFER, 0, (GLsizeiptr)bytes, data);
         glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
         return b;
      }

      // The rows of d whose `keep` (a 0 / 1 float per point) is 1, in order;
      // `kept` is how many (known on the CPU, see Cohort), or -1 to read the
      // count back from the GPU (PartDeath; synchronous, 4 bytes).
      std::shared_ptr<PopData> Compact(const PopData& d, unsigned int keep, int kept)
      {
         auto out = EmptyParts();
         const int n = d.pointCount;
         if (kept == 0 || n == 0)
            return out;
         const unsigned int flags = AcquireGpuBuffer((size_t)n * 4), idx = AcquireGpuBuffer((size_t)n * 4),
                            count = AcquireGpuBuffer(16);
         bool ok = gpu::FloatToFlags(keep, n, flags) && gpu::CompactIndices(flags, n, idx, count);
         if (ok && kept < 0)
         {
            uint32_t got = 0;
            glBindBuffer(GL_COPY_READ_BUFFER, count);
            glGetBufferSubData(GL_COPY_READ_BUFFER, 0, 4, &got);
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            kept = (int)std::min<uint32_t>(got, (uint32_t)n);
         }
         out->pointCount = std::max(0, kept);
         for (const PopAttrib& at : d.attribs)
         {
            if (!ok || kept <= 0)
               break;
            const int comps = at.buffer->Comps();
            const unsigned int dst = AcquireGpuBuffer((size_t)kept * (size_t)comps * sizeof(float));
            ok = gpu::GatherRows(at.buffer->Gpu(), comps, idx, count, kept, dst);
            out->Set(at.name, Owned(dst, kept, comps));
         }
         ReleaseGpuBuffer(flags, (size_t)n * 4);
         ReleaseGpuBuffer(idx, (size_t)n * 4);
         ReleaseGpuBuffer(count, 16);
         return ok ? out : nullptr;
      }

      const char* kIntegrate = "float m = max(1e-4, PartMass);\n"
                               "float sp = sqrt(PartVel.x * PartVel.x + PartVel.y * PartVel.y + PartVel.z * PartVel.z);\n"
                               "float dragK = PartDrag * sp;\n"
                               "vec3 tf = vec3(0.0);\n"
                               "if (uTurb > 0.0)\n"
                               "{\n"
                               "   vec3 q = P * uTurbK;\n"
                               "   tf.x = uTurb * (sin(q.y + uTt * 0.9) + sin(q.z * 1.7 + uTt * 1.3)) * 0.5;\n"
                               "   tf.y = uTurb * (sin(q.z + uTt * 1.1) + sin(q.x * 1.3 + uTt * 0.7)) * 0.5;\n"
                               "   tf.z = uTurb * (sin(q.x + uTt * 1.2) + sin(q.y * 1.5 + uTt * 0.8)) * 0.5;\n"
                               "}\n"
                               "vec3 force = PartForce + uG * m - dragK * PartVel + tf * m;\n"
                               "PartVel = (PartVel + force / m * uDt) * (1.0 - uDamp * min(1.0, uDt * 60.0));\n"
                               "P += PartVel * uDt;\n"
                               "PartForce = vec3(0.0);\n"
                               "PartAge += uDt;\n"
                               "PzKeep = (PartLifeSpan > 0.0 && PartAge >= PartLifeSpan) ? 0.0 : 1.0;\n";

      const char* kBirth = "P = P + PzJit;\n"
                           "PartVel = uVel + N * uSpeedN + PzVR;\n"
                           "PartAge = 0.0;\n"
                           "PartId = uFirstId + float(id);\n"
                           "PartMass = uMass;\n"
                           "PartDrag = uDrag;\n"
                           "PartForce = vec3(0.0);\n";
   }

   bool CookParticleGpu(CookCtx& c)
   {
      if (!gpu::Available())
         return false;
      ParticleGpuState& s = c.State<ParticleGpuState>();
      const PopData* src = c.Input(0);
      if (c.reset || !s.init)
      {
         s = ParticleGpuState();
         s.init = true;
         s.rng = (uint32_t)(c.F("seed") * 7919.0f) * 2654435761u + 1u;
         s.parts = EmptyParts();
      }

      // The loop: adopt what the downstream target made of last frame's particles.
      const std::string target = c.S("target");
      if (!target.empty() && c.findTagged)
      {
         const PopData* back = c.findTagged(target);
         if (back != nullptr && back->pointCount == s.alive && back->Has(attr::PartId) && s.alive > 0)
            s.parts = ShallowCopy(*back);
         else if (back == nullptr)
            c.warning = "no Null PointZ tagged '" + target + "'";
      }

      const float dt = c.Bl("play") ? std::min(0.1f, c.env.delta * c.F("timeScale")) : 0.0f;
      float g[3];
      c.V3("gravity", g);
      s.time += dt * c.F("turbSpeed");
      std::string err;

      // Integrate and age every particle; PzKeep marks the living.
      if (s.alive > 0)
      {
         KernelSpec spec;
         spec.reads = { IO(attr::P, 3),         IO(attr::PartVel, 3),      IO(attr::PartForce, 3), IO(attr::PartMass, 1),
                        IO(attr::PartDrag, 1),  IO(attr::PartAge, 1),      IO(attr::PartLifeSpan, 1) };
         spec.writes = { IO(attr::P, 3), IO(attr::PartVel, 3), IO(attr::PartForce, 3), IO(attr::PartAge, 1),
                         IO("PzKeep", 1) };
         spec.body = kIntegrate;
         const bool deathAttr = s.parts->Has(attr::PartDeath);
         if (deathAttr)
         {
            spec.reads.push_back(IO(attr::PartDeath, 1));
            spec.body += "if (PartDeath > 0.5)\n   PzKeep = 0.0;\n";
            s.cohortsValid = false;
         }
         const std::vector<KernelUniform> u = { UF("uDt", dt), U3("uG", g), UF("uDamp", c.F("damp")),
                                                UF("uTurb", c.F("turb")), UF("uTurbK", c.F("turbScale")),
                                                UF("uTt", (float)s.time) };
         std::vector<BufferPtr> out;
         if (!RunKernel(spec, *s.parts, u, c.env, out, err))
         {
            c.error = err;
            return true;
         }
         auto next = ShallowCopy(*s.parts);
         for (size_t w = 0; w + 1 < spec.writes.size(); w++)
            next->Set(spec.writes[w].name, out[w]);

         int alive = 0;
         if (s.cohortsValid)
         {
            for (Cohort& co : s.cohorts)
            {
               co.age += dt;
               while (co.dead < co.lives.size() && co.lives[co.dead] <= co.age)
                  co.dead++;
               alive += (int)(co.lives.size() - co.dead);
            }
            s.cohorts.erase(std::remove_if(s.cohorts.begin(), s.cohorts.end(),
                                           [](const Cohort& co) { return co.dead == co.lives.size(); }),
                            s.cohorts.end());
         }
         else
         {
            s.cohorts.clear();
            alive = -1; // counted on the GPU
         }
         if (alive != s.alive)
         {
            auto compacted = Compact(*next, out.back()->Gpu(), alive);
            if (!compacted)
            {
               c.error = "particle compaction failed";
               return true;
            }
            next = compacted;
         }
         s.parts = next;
         s.alive = next->pointCount;
      }

      // Births: the CPU draws the random numbers (CPU engine order), the GPU
      // gathers the source rows and builds the particles.
      if (src != nullptr && src->pointCount > 0 && c.Bl("play"))
      {
         int births = 0;
         if (c.I("birth") == 0)
         {
            s.carry += (double)c.F("rate") * dt;
            births = (int)s.carry;
            s.carry -= births;
         }
         else
            births = src->pointCount;
         const int maxP = std::max(1, c.I("max"));
         births = std::max(0, std::min(births, maxP - s.alive));
         if (births > 0)
         {
            const int srcCount = src->pointCount;
            const float jitter = c.F("jitter"), velRand = c.F("velRand"), life = c.F("life"), var = c.F("lifeVar");
            std::vector<uint32_t> rows((size_t)births);
            std::vector<float> jit((size_t)births * 3), vr((size_t)births * 3), lives((size_t)births);
            for (int b = 0; b < births; b++)
            {
               rows[(size_t)b] = (uint32_t)(c.I("birth") == 1 ? std::min(b, srcCount - 1)
                                                              : std::min(srcCount - 1, (int)(s.Rnd() * srcCount)));
               float r[7];
               for (int k = 0; k < 7; k++)
                  r[k] = s.Rnd(); // jitter x3, random velocity x3, life
               for (int k = 0; k < 3; k++)
               {
                  jit[(size_t)b * 3 + (size_t)k] = (r[k] * 2.0f - 1.0f) * jitter;
                  vr[(size_t)b * 3 + (size_t)k] = (r[3 + k] * 2.0f - 1.0f) * velRand;
               }
               lives[(size_t)b] = life * (1.0f + (r[6] * 2.0f - 1.0f) * var);
            }

            // Source rows on the GPU (P and N always, every attribute with 'inherit').
            const bool inheritAll = c.Bl("inherit");
            const unsigned int rowsBuf = Upload(rows.data(), rows.size() * 4);
            const uint32_t countValue = (uint32_t)births;
            const unsigned int countBuf = Upload(&countValue, 4);
            auto born = EmptyParts();
            born->pointCount = births;
            bool ok = true;
            for (const PopAttrib& at : src->attribs)
            {
               if (!inheritAll && at.name != attr::P && at.name != attr::N)
                  continue;
               if (at.buffer->Count() < srcCount)
                  continue;
               const int comps = at.buffer->Comps();
               const unsigned int dst = AcquireGpuBuffer((size_t)births * (size_t)comps * sizeof(float));
               ok = ok && gpu::GatherRows(at.buffer->Gpu(), comps, rowsBuf, countBuf, births, dst);
               born->Set(at.name, Owned(dst, births, comps));
            }
            ReleaseGpuBuffer(rowsBuf, rows.size() * 4);
            ReleaseGpuBuffer(countBuf, 4);
            born->Set("PzJit", MakeBuffer(std::move(jit), 3));
            born->Set("PzVR", MakeBuffer(std::move(vr), 3));
            born->Set(attr::PartLifeSpan, MakeBuffer(std::vector<float>(lives), 1));

            KernelSpec spec;
            spec.reads = { IO(attr::P, 3), IO(attr::N, 3), IO("PzJit", 3), IO("PzVR", 3) };
            spec.writes = { IO(attr::P, 3),    IO(attr::PartVel, 3),  IO(attr::PartAge, 1), IO(attr::PartId, 1),
                            IO(attr::PartMass, 1), IO(attr::PartDrag, 1), IO(attr::PartForce, 3) };
            spec.body = kBirth;
            float vel[3];
            c.V3("vel", vel);
            const std::vector<KernelUniform> u = { U3("uVel", vel), UF("uSpeedN", c.F("speedN")),
                                                   UF("uFirstId", s.nextId), UF("uMass", c.F("mass")),
                                                   UF("uDrag", c.F("drag")) };
            std::vector<BufferPtr> out;
            if (!ok || !RunKernel(spec, *born, u, c.env, out, err))
            {
               c.error = ok ? err : "particle birth gather failed";
               return true;
            }
            for (size_t w = 0; w < spec.writes.size(); w++)
               born->Set(spec.writes[w].name, out[w]);
            born->Remove("PzJit");
            born->Remove("PzVR");
            if (!inheritAll && !s.parts->Has(attr::N))
               born->Remove(attr::N); // the CPU engine keeps N only with 'inherit'

            // Living then newborn; an attribute shaped differently in the two
            // (an inherited 3-component Color against a 4-component one) gets
            // its default for the newborn, as before.
            auto merged = gpu::ConcatRows({ s.parts.get(), born.get() }, true);
            merged->topo = std::make_shared<PopTopology>();
            s.parts = merged;
            if (s.cohortsValid)
            {
               Cohort co;
               co.lives = lives;
               for (float& l : co.lives)
                  if (!(l > 0.0f))
                     l = std::numeric_limits<float>::infinity();
               std::sort(co.lives.begin(), co.lives.end());
               s.cohorts.push_back(std::move(co));
            }
            s.nextId += (float)births;
            s.alive += births;
         }
      }

      // Size and colour over life (t = age / life span), as the CPU engine.
      const float sizeBirth = c.F("sizeBirth"), sizeDeath = c.F("sizeDeath"), sizeCurve = c.F("sizeCurve");
      const bool sizeLife = sizeBirth != 1.0f || sizeDeath != 1.0f;
      const bool colorLife = c.Bl("colorLife");
      if ((sizeLife || colorLife) && s.alive > 0)
      {
         KernelSpec spec;
         spec.reads = { IO(attr::PartAge, 1), IO(attr::PartLifeSpan, 1) };
         spec.body = "float t = PartLifeSpan > 0.0 ? clamp(PartAge / PartLifeSpan, 0.0, 1.0) : 0.0;\n";
         if (sizeLife)
         {
            spec.reads.push_back(IO(attr::PScale, 1));
            spec.reads.push_back(IO("PartSize0", 1));
            spec.writes.push_back(IO(attr::PScale, 1));
            spec.writes.push_back(IO("PartSize0", 1));
            spec.body += "if (PartSize0 <= 0.0)\n   PartSize0 = PScale > 0.0 ? PScale : 1.0;\n"
                         "PScale = PartSize0 * (uSB + (uSD - uSB) * pow(t, uSC));\n";
         }
         if (colorLife)
         {
            spec.writes.push_back(IO(attr::Color, 4));
            spec.body += "Color = vec4(uCB + (uCD - uCB) * t, 1.0);\n";
         }
         float cb[3] = { 1, 1, 1 }, cd[3] = { 1, 1, 1 };
         c.V3("colorBirth", cb);
         c.V3("colorDeath", cd);
         const std::vector<KernelUniform> u = { UF("uSB", sizeBirth), UF("uSD", sizeDeath), UF("uSC", sizeCurve),
                                                U3("uCB", cb), U3("uCD", cd) };
         std::vector<BufferPtr> out;
         if (!RunKernel(spec, *s.parts, u, c.env, out, err))
         {
            c.error = err;
            return true;
         }
         auto next = ShallowCopy(*s.parts);
         for (size_t w = 0; w < spec.writes.size(); w++)
            next->Set(spec.writes[w].name, out[w]);
         s.parts = next;
      }

      c.out = ShallowCopy(*s.parts);
      return true;
   }
}
