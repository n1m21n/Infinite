#include "BrainSculptNode.h"

#include "core/bci/BciTypes.h"

#include <algorithm>
#include <cmath>
#include <sstream>

std::array<float, 6> BrainSculptNode::CurrentLatent() const
{
   std::array<float, 6> z{};
   Bci::ReadBrainLatent6(z.data());
   // If an explicit modulator wire is patched into slot 0, blend it onto axis 0 (Arousal)
   // so tests or non-BrainSource modulators can also steer the RBF manifold directly.
   if (input != nullptr)
      z[0] = std::clamp(input->Value01(), 0.0f, 1.0f);
   return z;
}

float BrainSculptNode::PredictRaw(const ParamKey& k, float fallbackPos) const
{
   auto it = mAnchors.find(k);
   if (it == mAnchors.end() || it->second.empty())
      return fallbackPos;

   const auto z = CurrentLatent();
   const float bw = std::max(0.05f, sigma);
   const float invTwoSigSq = 1.0f / (2.0f * bw * bw);
   const float lam = std::max(0.0f, regularization);

   double wSum = lam;
   double ySum = (double)lam * (double)fallbackPos;
   for (const Anchor& a : it->second)
   {
      double d2 = 0.0;
      for (int d = 0; d < 6; ++d)
      {
         const double diff = (double)z[d] - (double)a.z[d];
         d2 += diff * diff;
      }
      const double w = std::exp(-d2 * (double)invTwoSigSq);
      wSum += w;
      ySum += w * (double)a.targetPos01;
   }
   return std::clamp((float)(ySum / std::max(1e-9, wSum)), 0.0f, 1.0f);
}

void BrainSculptNode::Tick(int frameId, double dt)
{
   if (mLastTickFrame == frameId)
      return;
   mLastTickFrame = frameId;

   const float tau = 0.01f + std::clamp(smoothing, 0.0f, 1.0f) * 0.45f;
   const float alpha = dt > 0.0 ? (1.0f - std::exp(-(float)dt / tau)) : 1.0f;

   float avg = 0.0f;
   int n = 0;
   for (const auto& kv : mAnchors)
   {
      const float fallback = mSmoothedPos.count(kv.first) ? mSmoothedPos[kv.first] : 0.5f;
      const float target = PredictRaw(kv.first, fallback);
      float& cur = mSmoothedPos[kv.first];
      if (!mSmoothedPos.count(kv.first))
         cur = target;
      else
         cur += (target - cur) * alpha;
      avg += cur;
      ++n;
   }
   if (n > 0)
      mSummaryValue = std::clamp(avg / (float)n, 0.0f, 1.0f);
   else
      mSummaryValue = CurrentLatent()[0];
}

float BrainSculptNode::ValuePos01For(const ParamKey& k, float curPos)
{
   if (bypassed)
      return input ? std::clamp(input->Value01(), 0.0f, 1.0f) : std::clamp(curPos, 0.0f, 1.0f);
   auto it = mAnchors.find(k);
   if (it == mAnchors.end() || it->second.empty())
      return std::clamp(curPos, 0.0f, 1.0f);

   auto sit = mSmoothedPos.find(k);
   if (sit == mSmoothedPos.end())
   {
      const float p = PredictRaw(k, curPos);
      mSmoothedPos[k] = p;
      return p;
   }
   return std::clamp(sit->second, 0.0f, 1.0f);
}

void BrainSculptNode::OnGrab(const ParamKey& /*k*/)
{
   // Hand holds the control cleanly; CookFrame skips ValuePos01For while grabbed.
}

void BrainSculptNode::OnRelease(const ParamKey& k, float pos, float /*velPerSec*/)
{
   if (bypassed)
      return;   // a bypassed node does not learn
   Anchor a;
   a.z = CurrentLatent();
   a.targetPos01 = std::clamp(pos, 0.0f, 1.0f);

   auto& list = mAnchors[k];
   // Replace near-duplicate latent anchor so repeated adjustments refine rather than bloat
   for (Anchor& existing : list)
   {
      double d2 = 0.0;
      for (int d = 0; d < 6; ++d)
      {
         const double diff = (double)existing.z[d] - (double)a.z[d];
         d2 += diff * diff;
      }
      if (d2 < 0.008)
      {
         existing.targetPos01 = a.targetPos01;
         mSmoothedPos[k] = a.targetPos01;
         return;
      }
   }
   if (list.size() >= 32)
      list.erase(list.begin());
   list.push_back(a);
   mSmoothedPos[k] = a.targetPos01;
}

float BrainSculptNode::Confidence01(const ParamKey& k) const
{
   auto it = mAnchors.find(k);
   if (it == mAnchors.end() || it->second.empty())
      return 0.0f;
   const auto z = CurrentLatent();
   const float bw = std::max(0.05f, sigma);
   const float invTwoSigSq = 1.0f / (2.0f * bw * bw);
   double best = 0.0;
   for (const Anchor& a : it->second)
   {
      double d2 = 0.0;
      for (int d = 0; d < 6; ++d)
      {
         const double diff = (double)z[d] - (double)a.z[d];
         d2 += diff * diff;
      }
      best = std::max(best, std::exp(-d2 * (double)invTwoSigSq));
   }
   return std::clamp((float)best, 0.0f, 1.0f);
}

void BrainSculptNode::ClearAnchors()
{
   mAnchors.clear();
   mSmoothedPos.clear();
}

int BrainSculptNode::TotalAnchorCount() const
{
   int total = 0;
   for (const auto& kv : mAnchors)
      total += (int)kv.second.size();
   return total;
}

void BrainSculptNode::VisitParams(ParamVisitor& v)
{
   v.Float("sigma", sigma);
   v.Float("smoothing", smoothing);
   v.Float("regularization", regularization);

   std::ostringstream out;
   for (const auto& kv : mAnchors)
   {
      for (const Anchor& a : kv.second)
      {
         out << kv.first.uid << "," << kv.first.paramIndex << "," << a.targetPos01;
         for (int d = 0; d < 6; ++d)
            out << "," << a.z[d];
         out << ";";
      }
   }
   std::string serialized = out.str();
   const std::string before = serialized;
   v.Text("anchors", serialized);
   if (serialized != before)
   {
      mAnchors.clear();
      mSmoothedPos.clear();
      std::stringstream ss(serialized);
      std::string item;
      while (std::getline(ss, item, ';'))
      {
         if (item.empty())
            continue;
         std::stringstream ls(item);
         std::string cell;
         std::vector<double> vals;
         while (std::getline(ls, cell, ','))
         {
            try { vals.push_back(std::stod(cell)); }
            catch (...) { vals.clear(); break; }
         }
         if (vals.size() == 9)
         {
            ParamKey pk{ (uint64_t)vals[0], (int)vals[1] };
            Anchor a;
            a.targetPos01 = std::clamp((float)vals[2], 0.0f, 1.0f);
            for (int d = 0; d < 6; ++d)
               a.z[d] = std::clamp((float)vals[3 + d], 0.0f, 1.0f);
            mAnchors[pk].push_back(a);
         }
      }
   }
}
