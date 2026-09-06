#include "GestureDetector.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "Transport.h"

GestureDetector& GestureDetector::Instance()
{
   static GestureDetector instance;
   return instance;
}

void GestureDetector::Evict(double nowSec)
{
   while (!mRecent.empty() && nowSec - mRecent.front().timeSec > windowSeconds)
      mRecent.pop_front();
}

void GestureDetector::NotifyTouch(int nodeIndex, int paramIndex, float value, double nowSec)
{
   if (!enabled)
      return;

   Evict(nowSec);
   mRecent.push_back({ nodeIndex, paramIndex, value, nowSec });
   Detect(nowSec);
}

void GestureDetector::Detect(double nowSec)
{
   const int n = 2 * std::max(1, sensitivity);
   if ((int)mRecent.size() < n)
      return; // not enough history yet - leave any existing pending suggestion alone

   std::vector<TouchEvent> tail(mRecent.end() - n, mRecent.end());

   // Case: every one of the last N touches hit the same widget - a single
   // knob being snapped back and forth (separate click/release cycles, not
   // one continuous drag - see NotifyTouch's call site for why that's the
   // granularity IsItemActivated already gives us).
   bool allSame = true;
   for (size_t i = 1; i < tail.size(); i++)
   {
      if (tail[i].nodeIndex != tail[0].nodeIndex || tail[i].paramIndex != tail[0].paramIndex)
      {
         allSame = false;
         break;
      }
   }
   if (allSame)
   {
      std::vector<Key> collapsedUnused;
      if (DetectSelfOscillation(collapsedUnused, nowSec))
         return;
      // Fell through (values didn't actually cluster into two groups) - no
      // suggestion this call, but don't fall into the multi-key detectors
      // below with a single-key tail; there's nothing there to alternate.
      return;
   }

   // Collapse consecutive repeats of the same key before scanning for
   // alternation - a defensive step against a hypothetical double-fire, not
   // something the normal one-activation-per-press idiom actually produces.
   std::vector<Key> collapsed;
   for (const TouchEvent& e : tail)
   {
      Key k(e.nodeIndex, e.paramIndex);
      if (collapsed.empty() || collapsed.back() != k)
         collapsed.push_back(k);
   }

   std::map<Key, int> distinct;
   for (const Key& k : collapsed)
      distinct[k]++;

   if (distinct.size() == 2)
   {
      if (DetectTwoParamLink(collapsed, nowSec))
         return;
   }
   else if (distinct.size() >= 3 && distinct.size() <= 6)
   {
      if (DetectFanout(collapsed, nowSec))
         return;
   }
}

bool GestureDetector::DetectSelfOscillation(const std::vector<Key>&, double nowSec)
{
   const Key key(mRecent.back().nodeIndex, mRecent.back().paramIndex);
   const int n = 2 * std::max(1, sensitivity);

   std::vector<float> values;
   std::vector<double> times;
   for (auto it = mRecent.rbegin(); it != mRecent.rend() && (int)values.size() < n; ++it)
   {
      if (it->nodeIndex == key.first && it->paramIndex == key.second)
      {
         values.push_back(it->value);
         times.push_back(it->timeSec);
      }
   }
   if ((int)values.size() < n)
      return false;
   std::reverse(values.begin(), values.end());
   std::reverse(times.begin(), times.end());

   // Simple two-cluster split: sort a copy, split at the single largest gap.
   // Not k-means - the doc explicitly doesn't need this to be principled, just
   // good enough to separate "high" from "low" when someone is snapping a
   // knob between roughly two positions.
   std::vector<float> sorted = values;
   std::sort(sorted.begin(), sorted.end());
   const float total = sorted.back() - sorted.front();
   if (total <= 0.0f)
      return false; // never moved at all - not an oscillation, just repeated clicks

   size_t splitAt = 0;
   float bestGap = -1.0f;
   for (size_t i = 1; i < sorted.size(); i++)
   {
      const float gap = sorted[i] - sorted[i - 1];
      if (gap > bestGap)
      {
         bestGap = gap;
         splitAt = i;
      }
   }
   // Require the split to actually separate two clusters, not just be noise
   // inside one: the gap itself should be a meaningful fraction of the total
   // observed swing.
   if (bestGap < total * 0.25f)
      return false;

   const float threshold = sorted[splitAt - 1] + bestGap * 0.5f;

   float lowSum = 0.0f, highSum = 0.0f;
   int lowCount = 0, highCount = 0;
   int transitions = 0;
   bool lastHigh = values[0] >= threshold;
   for (size_t i = 0; i < values.size(); i++)
   {
      const bool high = values[i] >= threshold;
      if (high) { highSum += values[i]; highCount++; }
      else { lowSum += values[i]; lowCount++; }
      if (i > 0 && high != lastHigh)
         transitions++;
      lastHigh = high;
   }
   if (lowCount == 0 || highCount == 0 || transitions < sensitivity)
      return false;

   double intervalSum = 0.0;
   for (size_t i = 1; i < times.size(); i++)
      intervalSum += times[i] - times[i - 1];
   const double meanIntervalSec = times.size() > 1 ? intervalSum / (double)(times.size() - 1) : 1.0;
   const double secondsPerBeat = 60.0 / std::max(1.0f, Transport::Instance().Tempo());
   const float stepBeats = (float)std::clamp(meanIntervalSec / secondsPerBeat, 0.0625, 32.0);

   mPending = Suggestion();
   mPending.kind = Kind::SelfOscillation;
   mPending.sourceNode = key.first;
   mPending.sourceParam = key.second;
   mPending.clusterLow = lowSum / (float)lowCount;
   mPending.clusterHigh = highSum / (float)highCount;
   mPending.stepBeatsEstimate = stepBeats;
   mPending.expiresAtSec = nowSec + windowSeconds;
   mHasPending = true;
   return true;
}

bool GestureDetector::DetectTwoParamLink(const std::vector<Key>& collapsed, double nowSec)
{
   // Exactly 2 distinct keys and collapsed already has no consecutive
   // repeats, so it necessarily alternates ABAB... - nothing further to
   // verify. Direction is chronological: collapsed.front() is the earliest
   // of the N touches under consideration.
   const Key source = collapsed.front();
   Key dest = source;
   for (const Key& k : collapsed)
   {
      if (k != source)
      {
         dest = k;
         break;
      }
   }
   if (dest == source)
      return false;

   mPending = Suggestion();
   mPending.kind = Kind::TwoParamLink;
   mPending.sourceNode = source.first;
   mPending.sourceParam = source.second;
   mPending.destinations.push_back(dest);
   mPending.expiresAtSec = nowSec + windowSeconds;
   mHasPending = true;
   return true;
}

bool GestureDetector::DetectFanout(const std::vector<Key>& collapsed, double nowSec)
{
   // A "small stable set... in a repeating order" without a full cyclic-order
   // verifier: require every distinct key in the window to have actually
   // recurred (not just been touched once in passing), which is enough to
   // rule out an incidental one-off visit to a third control.
   std::map<Key, int> counts;
   std::vector<Key> order; // first-appearance order, chronological
   for (const Key& k : collapsed)
   {
      if (counts.find(k) == counts.end())
         order.push_back(k);
      counts[k]++;
   }
   for (const auto& [k, count] : counts)
   {
      if (count < 2)
         return false;
   }

   const Key source = order.front();
   mPending = Suggestion();
   mPending.kind = Kind::Fanout;
   mPending.sourceNode = source.first;
   mPending.sourceParam = source.second;
   for (const Key& k : order)
   {
      if (k != source)
         mPending.destinations.push_back(k);
   }
   if (mPending.destinations.empty())
      return false;
   mPending.expiresAtSec = nowSec + windowSeconds;
   mHasPending = true;
   return true;
}

const GestureDetector::Suggestion* GestureDetector::PendingSuggestion(double nowSec)
{
   if (!enabled || !mHasPending)
      return nullptr;
   if (nowSec >= mPending.expiresAtSec)
   {
      mHasPending = false;
      return nullptr;
   }
   return &mPending;
}

void GestureDetector::Dismiss()
{
   mHasPending = false;
}
