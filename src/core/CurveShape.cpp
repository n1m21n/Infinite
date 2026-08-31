#include "CurveShape.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

void CurveShape::Reset()
{
   points.clear();
   points.push_back({ 0.0f, 0.0f });
   points.push_back({ 1.0f, 1.0f });
   version++;
}

int CurveShape::AddPoint(float x, float y)
{
   x = std::min(1.0f, std::max(0.0f, x));
   y = std::min(1.0f, std::max(0.0f, y));

   size_t insertAt = points.size();
   for (size_t i = 0; i < points.size(); i++)
   {
      if (points[i].x > x)
      {
         insertAt = i;
         break;
      }
   }
   points.insert(points.begin() + insertAt, { x, y });
   version++;
   return (int)insertAt;
}

void CurveShape::MovePoint(int index, float x, float y)
{
   if (index < 0 || index >= (int)points.size())
      return;

   y = std::min(1.0f, std::max(0.0f, y));

   if (index == 0)
      x = 0.0f; // endpoints stay pinned to the edges
   else if (index == (int)points.size() - 1)
      x = 1.0f;
   else
   {
      // keep the ordering intact so evaluation stays monotonic in x
      const float lo = points[index - 1].x + 0.002f;
      const float hi = points[index + 1].x - 0.002f;
      x = std::min(hi, std::max(lo, x));
   }

   if (points[index].x == x && points[index].y == y)
      return; // no actual change - don't churn a cache keyed on version

   points[index].x = x;
   points[index].y = y;
   version++;
}

void CurveShape::RemovePoint(int index)
{
   if (index <= 0 || index >= (int)points.size() - 1)
      return; // endpoints are permanent
   points.erase(points.begin() + index);
   version++;
}

float CurveShape::Evaluate(float x) const
{
   if (points.size() < 2)
      return x;

   x = std::min(1.0f, std::max(0.0f, x));
   if (x <= points.front().x)
      return points.front().y;
   if (x >= points.back().x)
      return points.back().y;

   size_t i = 0;
   while (i + 1 < points.size() && points[i + 1].x < x)
      i++;

   const Point& p1 = points[i];
   const Point& p2 = points[i + 1];
   const float span = std::max(1e-5f, p2.x - p1.x);
   const float t = (x - p1.x) / span;

   // Catmull-Rom through the neighbours, so the curve is smooth rather than
   // faceted, with the ends duplicated to avoid overshoot at the edges.
   const Point& p0 = (i > 0) ? points[i - 1] : p1;
   const Point& p3 = (i + 2 < points.size()) ? points[i + 2] : p2;

   const float t2 = t * t;
   const float t3 = t2 * t;
   float y = 0.5f * ((2.0f * p1.y) +
                     (-p0.y + p2.y) * t +
                     (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                     (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
   return std::min(1.0f, std::max(0.0f, y));
}

std::string CurveShape::Encode() const
{
   std::string out;
   for (size_t i = 0; i < points.size(); i++)
   {
      if (i > 0)
         out += ";";
      char buf[48];
      snprintf(buf, sizeof(buf), "%.6g,%.6g", (double)points[i].x, (double)points[i].y);
      out += buf;
   }
   return out;
}

void CurveShape::Decode(const std::string& s)
{
   std::vector<Point> decoded;
   size_t start = 0;
   while (start < s.size())
   {
      size_t sep = s.find(';', start);
      std::string term = s.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
      const size_t comma = term.find(',');
      if (comma != std::string::npos)
      {
         Point p;
         p.x = (float)atof(term.substr(0, comma).c_str());
         p.y = (float)atof(term.substr(comma + 1).c_str());
         decoded.push_back(p);
      }
      if (sep == std::string::npos)
         break;
      start = sep + 1;
   }
   if (decoded.size() < 2)
      return;
   if (decoded.size() == points.size() &&
       std::equal(decoded.begin(), decoded.end(), points.begin(),
                   [](const Point& a, const Point& b) { return a.x == b.x && a.y == b.y; }))
      return; // identical to what's already loaded - don't invalidate a cache for nothing
   points = decoded;
   version++;
}
