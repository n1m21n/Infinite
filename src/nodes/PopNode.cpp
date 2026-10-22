#include "PopNode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "Transport.h"
#include "platform/OpenGLHeaders.h"

namespace
{
   const Mesh kEmptyPopMesh;

   // Bakes a source's placement into converted points, so moving a Geometry
   // node upstream of a PointZ moves the PointZ.
   void BakeModel(pop::PopData& d, const Mat4& m)
   {
      if (m == Mat4::Identity())
         return;
      if (const pop::PopAttrib* a = d.Find(pop::attr::P))
      {
         std::vector<float> P = a->buffer->Cpu();
         const int c = a->buffer->Comps();
         for (int i = 0; i < d.pointCount; i++)
         {
            float* p = &P[(size_t)i * c];
            const float x = p[0], y = c > 1 ? p[1] : 0.0f, z = c > 2 ? p[2] : 0.0f;
            p[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
            if (c > 1) p[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
            if (c > 2) p[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
         }
         d.Set(pop::attr::P, pop::MakeBuffer(std::move(P), c));
      }
      if (const pop::PopAttrib* a = d.Find(pop::attr::N))
      {
         float nm[9];
         m.NormalMatrix(nm);
         std::vector<float> N = a->buffer->Cpu();
         const int c = a->buffer->Comps();
         if (c >= 3)
            for (int i = 0; i < d.pointCount; i++)
            {
               float* n = &N[(size_t)i * c];
               const float x = n[0], y = n[1], z = n[2];
               float ox = nm[0] * x + nm[3] * y + nm[6] * z;
               float oy = nm[1] * x + nm[4] * y + nm[7] * z;
               float oz = nm[2] * x + nm[5] * y + nm[8] * z;
               const float len = std::sqrt(ox * ox + oy * oy + oz * oz);
               if (len > 1e-12f) { ox /= len; oy /= len; oz /= len; }
               n[0] = ox; n[1] = oy; n[2] = oz;
            }
         d.Set(pop::attr::N, pop::MakeBuffer(std::move(N), c));
      }
   }
}

namespace
{
   std::vector<PopNode*>& AllPopNodes()
   {
      static std::vector<PopNode*> all;
      return all;
   }
}

PopNode::PopNode(const pop::PopDef* def) : mDef(def)
{
   mDef->Defaults(mValues);
   AllPopNodes().push_back(this);
}

PopNode::~PopNode()
{
   if (mGpuQ[0][0] != 0)
      glDeleteQueries(6, &mGpuQ[0][0]);
   auto& all = AllPopNodes();
   all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::string& PopNode::Tag() const
{
   static const std::string kNone;
   const int i = mDef->ParamIndex("tag");
   return (i >= 0 && i < (int)mValues.s.size()) ? mValues.s[(size_t)i] : kNone;
}

PopNode* PopNode::FindTagged(const std::string& tag, const PopNode* exclude, int frameId)
{
   if (tag.empty())
      return nullptr;
   for (PopNode* n : AllPopNodes())
      if (n != exclude && n->Tag() == tag)
      {
         // Cooking it here keeps a loop running even when nothing else pulls
         // the target; the cook guard stops the recursion back into the caller,
         // which then reads the caller's previous output (a one-frame loop).
         n->CookIfNeeded(frameId);
         return n;
      }
   return nullptr;
}

INode* PopNode::CreateFor(const std::string& defName)
{
   const pop::PopDef* def = pop::FindDef(defName);
   return def ? new PopNode(def) : nullptr;
}

const char* PopNode::InputLabel(int slot) const
{
   if (slot >= 0 && slot < mDef->inputs)
   {
      if (slot < (int)mDef->inputLabels.size())
         return mDef->inputLabels[(size_t)slot].c_str();
      return mDef->inputs == 1 ? "in" : nullptr;
   }
   const int img = slot - mDef->inputs;
   if (mDef->imageInput && img >= 0 && img < ImageCount())
      return img < (int)mDef->imageLabels.size() ? mDef->imageLabels[(size_t)img].c_str() : mDef->imageLabel.c_str();
   return nullptr;
}

IGeometrySource** PopNode::GeometryInputSlot(int slot)
{
   return (slot >= 0 && slot < mDef->inputs && slot < kMaxInputs) ? &inputs[slot] : nullptr;
}

ImageCable* PopNode::ImageInputSlot(int slot)
{
   const int img = slot - mDef->inputs;
   return (mDef->imageInput && img >= 0 && img < ImageCount()) ? &mImages[img] : nullptr;
}

int PopNode::InputPinCount() const
{
   return mDef->inputs + ImageCount();
}

int PopNode::ImageCount() const
{
   return mDef->imageInput ? std::clamp(mDef->imageCount, 1, 4) : 0;
}

void PopNode::VisitParams(ParamVisitor& v)
{
   int off = 0;
   for (size_t i = 0; i < mDef->params.size(); i++)
   {
      const pop::ParamSpec& p = mDef->params[i];
      const char* key = p.key.c_str();
      switch (p.type)
      {
         case pop::ParamSpec::kFloat:
            v.Float(key, mValues.f[(size_t)off]);
            break;
         case pop::ParamSpec::kInt:
         case pop::ParamSpec::kMenu:
         {
            int x = (int)std::lround(mValues.f[(size_t)off]);
            v.Int(key, x);
            mValues.f[(size_t)off] = (float)x;
            break;
         }
         case pop::ParamSpec::kBool:
         {
            bool b = mValues.f[(size_t)off] != 0.0f;
            v.Bool(key, b);
            mValues.f[(size_t)off] = b ? 1.0f : 0.0f;
            break;
         }
         case pop::ParamSpec::kColor:
            v.Color(key, &mValues.f[(size_t)off]);
            break;
         case pop::ParamSpec::kVec3:
         {
            static const char* kAxis[3] = { "X", "Y", "Z" };
            for (int k = 0; k < 3; k++)
            {
               const std::string name = p.key + kAxis[k];
               v.Float(name.c_str(), mValues.f[(size_t)(off + k)]);
            }
            break;
         }
         case pop::ParamSpec::kText:
            v.Text(key, mValues.s[i]);
            break;
      }
      off += p.Floats();
   }
}

pop::DataPtr PopNode::InputData(int slot, int /*frameId*/)
{
   IGeometrySource* s = inputs[slot];
   if (auto* sn = dynamic_cast<INode*>(s); sn != nullptr && sn->blockOff)
      s = nullptr; // Turbo 0.59: a switched-off Set block reads as nothing
   if (s == nullptr)
   {
      mConverted[slot] = Converted();
      return nullptr;
   }
   if (const pop::PopData* p = s->GetPop())
   {
      mConverted[slot] = Converted();
      // Non-owning: only used during this cook; anything kept is a shallow
      // copy holding the buffers themselves.
      return pop::DataPtr(pop::DataPtr(), p);
   }
   Converted& c = mConverted[slot];
   const Mat4 model = s->GetModelMatrix();
   unsigned long long rev[3] = { s->MeshRevision(), s->PointCloudRevision(), s->CurveStamp() };
   // The placement is part of the key: moving the source re-bakes.
   unsigned long long mh = 1469598103934665603ull;
   for (float f : model.m)
   {
      unsigned int bits;
      std::memcpy(&bits, &f, 4);
      mh = (mh ^ bits) * 1099511628211ull;
   }
   rev[0] ^= mh;
   if (c.source == (const void*)s && c.data && c.rev[0] == rev[0] && c.rev[1] == rev[1] && c.rev[2] == rev[2])
      return c.data;
   std::shared_ptr<pop::PopData> d;
   if (const std::vector<Particle>* cloud = s->GetPointCloud())
      d = pop::FromCloud(*cloud);
   else if (s->GetMesh().HasGeometry())
      d = pop::FromMesh(s->GetMesh());
   else if (const Polyline* line = s->GetCurve())
      d = pop::FromCurve(*line);
   else
      d = std::make_shared<pop::PopData>();
   if (!d->topo)
      d->topo = pop::NewTopo();
   BakeModel(*d, model);
   d->revision = NextMeshRevision();
   c.source = s;
   c.rev[0] = rev[0]; c.rev[1] = rev[1]; c.rev[2] = rev[2];
   c.data = d;
   return d;
}

void PopNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   pop::CookCtx c;
   c.def = mDef;
   c.values = &mValues;
   c.state = &mState;
   c.inCount = std::min(mDef->inputs, kMaxInputs);
   std::vector<unsigned long long> sig((size_t)c.inCount, 0);
   for (int i = 0; i < c.inCount; i++)
   {
      if (auto* node = dynamic_cast<INode*>(inputs[i]))
         node->CookIfNeeded(frameId);
      c.in[i] = InputData(i, frameId);
      sig[(size_t)i] = c.in[i] ? c.in[i]->revision : 0;
   }
   unsigned long long imageRev = 0;
   if (mDef->imageInput)
   {
      for (int k = 0; k < ImageCount(); k++)
      {
         c.images[k] = mImages[k].Pull(frameId);
         imageRev = imageRev * 1099511628211ull + (mImages[k].IsConnected() ? mImages[k].Revision() + 1 : 0);
      }
      c.image = c.images[0];
      c.imageW = mImages[0].Width();
      c.imageH = mImages[0].Height();
   }

   if (bypassed)
   {
      mInstances.reset();
      mWarning.clear();
      mError.clear();
      if (c.in[0])
      {
         if (!mOut || mOut->revision != c.in[0]->revision)
         {
            auto copy = pop::ShallowCopy(*c.in[0]);
            copy->revision = c.in[0]->revision;
            mOut = copy;
         }
      }
      else
         mOut.reset();
      return;
   }

   const bool animated = mDef->everyFrame || (!mDef->animKey.empty() && c.F(mDef->animKey) != 0.0f);
   const bool same = !animated && !mResetRequested && mOut && sig == mLastSigInputs && mValues.f == mLastSigF &&
                     mValues.s == mLastSigS && imageRev == mLastImageRev;
   if (same)
      return;

   const double now = Transport::Instance().Seconds();
   c.env.time = now;
   c.env.beat = Transport::Instance().Beats();
   double dt = (mLastTime >= 0.0) ? now - mLastTime : 1.0 / 60.0;
   if (!(dt > 0.0) || dt > 0.25)
      dt = 1.0 / 60.0; // stopped transport or a hitch: one nominal frame
   mLastTime = now;
   c.env.delta = (float)dt;
   c.env.frame = frameId;
   c.env.seed = (mDef->ParamIndex("seed") >= 0) ? c.F("seed") : 0.0f;
   c.env.imageTex = c.image;
   c.env.lookupData[0] = c.Input(0);
   c.env.lookupData[1] = c.Input(1);
   c.reset = mResetRequested;
   mResetRequested = false;
   c.findTagged = [this, frameId](const std::string& tag) -> const pop::PopData* {
      PopNode* n = FindTagged(tag, this, frameId);
      return n ? n->mOut.get() : nullptr;
   };
   c.findTaggedState = [this, frameId](const std::string& tag) -> pop::OpState* {
      PopNode* n = FindTagged(tag, this, frameId);
      return n ? n->mState.get() : nullptr;
   };

   static const bool gpuTimes = getenv("INFINITE_FRAMEPROF") != nullptr || getenv("INFINITE_PATCHCOMPAT_PLAY") != nullptr;
   if (gpuTimes)
   {
      if (mGpuQ[0][0] == 0)
         glGenQueries(6, &mGpuQ[0][0]);
      // The result from two cooks ago, when the GPU has finished it.
      const int old = (mGpuRing + 1) % 3;
      GLint ready = 0;
      if (mGpuUsed >= 2)
         glGetQueryObjectiv(mGpuQ[old][1], GL_QUERY_RESULT_AVAILABLE, &ready);
      if (ready)
      {
         GLuint64 a = 0, b = 0;
         glGetQueryObjectui64v(mGpuQ[old][0], GL_QUERY_RESULT, &a);
         glGetQueryObjectui64v(mGpuQ[old][1], GL_QUERY_RESULT, &b);
         mTotalGpuMs += (double)(b - a) * 1e-6;
      }
      glQueryCounter(mGpuQ[mGpuRing][0], GL_TIMESTAMP);
   }
   const auto t0 = std::chrono::steady_clock::now();
   const bool ok = mDef->cook ? mDef->cook(c) : false;
   if (gpuTimes)
   {
      glQueryCounter(mGpuQ[mGpuRing][1], GL_TIMESTAMP);
      mGpuRing = (mGpuRing + 1) % 3;
      mGpuUsed++;
   }
   mLastCookMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   mTotalCookMs += mLastCookMs;

   if (!ok || !c.error.empty())
   {
      mError = c.error.empty() ? std::string("cook failed") : c.error;
      auto empty = std::make_shared<pop::PopData>();
      empty->topo = pop::NewTopo();
      empty->revision = NextMeshRevision();
      mOut = empty;
      mInstances.reset();
   }
   else
   {
      mError.clear();
      if (!c.out)
      {
         c.out = std::make_shared<pop::PopData>();
      }
      if (!c.out->topo)
         c.out->topo = pop::NewTopo();
      c.out->revision = NextMeshRevision();
      mOut = c.out;
      if (c.instances)
      {
         if (!c.instances->topo)
            c.instances->topo = pop::NewTopo();
         c.instances->revision = NextMeshRevision();
      }
      mInstances = c.instances;
   }
   mWarning = !mError.empty() ? mError : c.warning;
   mLastSigInputs = sig;
   mLastSigF = mValues.f;
   mLastSigS = mValues.s;
   mLastImageRev = imageRev;
}

const pop::PopData* PopNode::ActiveOut()
{
   return mOut.get();
}

const pop::PopData* PopNode::GetPop()
{
   return mOut.get();
}

bool PopNode::IsCloudOut() const
{
   if (!mOut || mInstances)
      return false;
   const pop::PopTopology& t = mOut->Topo();
   return !t.HasFaces() && !t.HasLines() && mOut->pointCount > 0;
}

bool PopNode::IsCurveOut() const
{
   return mOut && !mInstances && !mOut->Topo().HasFaces() && mOut->Topo().HasLines();
}

const Mesh& PopNode::GetMesh()
{
   const pop::PopData* d = ActiveOut();
   if (d == nullptr)
      return kEmptyPopMesh;
   if (mMeshViewRev != d->revision)
   {
      mMeshView = pop::ToMesh(*d);
      mMeshViewRev = d->revision;
   }
   return mMeshView;
}

unsigned long long PopNode::MeshRevision()
{
   const unsigned long long a = mOut ? mOut->revision : 0;
   const unsigned long long b = mInstances ? mInstances->revision : 0;
   return a ^ (b * 0x9e3779b97f4a7c15ull);
}

const std::vector<Particle>* PopNode::GetPointCloud()
{
   if (!IsCloudOut())
      return nullptr;
   if (mCloudViewRev != mOut->revision)
   {
      mCloudView = pop::ToCloud(*mOut);
      mCloudViewRev = mOut->revision;
   }
   return &mCloudView;
}

unsigned long long PopNode::PointCloudRevision()
{
   return IsCloudOut() ? mOut->revision : 0;
}

const Polyline* PopNode::GetCurve()
{
   if (!IsCurveOut())
      return nullptr;
   if (mCurveViewRev != mOut->revision)
   {
      mCurveView = pop::ToCurve(*mOut);
      mCurveViewRev = mOut->revision;
   }
   return &mCurveView;
}

unsigned long long PopNode::CurveStamp()
{
   return IsCurveOut() ? mOut->revision : 0;
}
