#pragma once

// Slow-frame attribution for the paced fixtures (B3, B8): Block 2 step 1 of
// docs/plans/perf/README.md. Stage p50s cannot explain a tail - B3's CPU
// stages add up to ~3 ms at p50 while one projector interval in eight is
// doubled. So while a fixture samples, every frame keeps its own stage times,
// the time from the last present to the pacing wait ("work"), the wait
// itself, and when the GPU finished the previous frame's work (a fence
// polled without blocking; GL_TIME_ELAPSED reads null on Apple GL). Frames
// whose projector interval is over 1.5 periods are reported as `slow_frames`
// with the median of each column over the slow frames only, next to the same
// medians over every frame.
//
// Main thread only. Measurement only: nothing here changes what a frame does.

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace Bench
{
   struct FrameTail
   {
      enum Stage
      {
         kModulation,
         kCook,
         kNodeBodies,
         kLinks,
         kCookAll,
         kEditorEnd,
         kImGuiRender,
         kSwap,
         kProjectors,
         kStageCount
      };
      static const char* StageName(int s)
      {
         static const char* kNames[kStageCount] = {
            "modulation", "cook", "node_bodies", "links", "cook_all",
            "editor_end", "imgui_render", "swap", "projectors",
         };
         return kNames[s];
      }

      enum Mark { kMarkFrameStart, kMarkPolled, kMarkNewFrame, kMarkCanvasSwap, kMarkSwapped, kMarkPumped, kMarkWaitStart, kMarkCount };
      static const char* SegmentName(int s)
      {
         // Segment s runs from the checkpoint before it to mark s.
         static const char* kNames[kMarkCount] = {
            "wait_end_to_frame_start", "poll_events", "imgui_new_frame", "frame_body", "canvas_swap", "fence_and_plugin_pump", "projector_render",
         };
         return kNames[s];
      }

      struct Record
      {
         double intervalMs = 0.0;   // projector (window 0) swap to swap
         double loopMs = 0.0;       // loop start to loop start
         double workMs = -1.0;      // last wait return -> this wait start
         double waitMs = -1.0;      // inside PaceProjectorPresent's wait
         double canvasGpuMs = -1.0; // canvas swap fence -> first seen signaled (upper bound)
         double projGpuMs = -1.0;   // last projector swap fence -> first seen signaled
         bool canvasGpuPendingAtWait = false; // canvas fence not yet signaled when the wait began
         std::array<double, kStageCount> stageMs{};
         std::string topNode;       // the most expensive node body this frame
         double topNodeMs = 0.0;
         double nodesMs = 0.0;      // sum of every node body this frame
         std::map<std::string, double> typeMs; // node body time per node type
         int index = 0;             // sampled-frame index, to see periodicity
         bool focused = true;       // canvas was the key window this frame
         // Checkpoints (NowMs) that split workMs with nothing left untimed:
         // previous wait end -> frame start -> after glfwPollEvents -> after
         // ImGui::NewFrame -> before the canvas swap -> after it -> after the
         // plugin editor pump -> wait start (after the projector render pass).
         double prevWaitEndMs = -1.0;
         std::array<double, kMarkCount> markMs{ -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0 };
      };

      bool active = false;
      double lastWaitEndMs = -1.0;
      double periodMs = 1000.0 / 60.0;
      Record cur;
      std::vector<Record> all;

      void BeginFrame(bool on, double period, bool focused)
      {
         active = on;
         if (period > 0.0)
            periodMs = period;
         cur = Record{};
         cur.index = (int)all.size();
         cur.focused = focused;
         cur.prevWaitEndMs = lastWaitEndMs;
      }
      void MarkAt(int m, double nowMs)
      {
         if (active && m >= 0 && m < kMarkCount)
            cur.markMs[(size_t)m] = nowMs;
      }
      static double Segment(const Record& r, int s)
      {
         const double from = s == 0 ? r.prevWaitEndMs : r.markMs[(size_t)(s - 1)];
         const double to = r.markMs[(size_t)s];
         return (from < 0.0 || to < 0.0) ? -1.0 : to - from;
      }
      void AddStage(int s, double ms)
      {
         if (active && s >= 0 && s < kStageCount)
            cur.stageMs[(size_t)s] += ms;
      }
      void AddNode(const std::string& type, double ms)
      {
         if (active)
         {
            cur.nodesMs += ms;
            cur.typeMs[type] += ms;
         }
         if (active && ms > cur.topNodeMs)
         {
            cur.topNodeMs = ms;
            cur.topNode = type;
         }
      }
      // Called once per frame after the last projector swap, with that
      // frame's interval; the fence columns of the previous record are filled
      // in later by the poller, so records are pushed here and patched.
      void EndFrame(double intervalMs)
      {
         if (!active)
            return;
         cur.intervalMs = intervalMs;
         all.push_back(cur);
      }

      static double Median(std::vector<double> v)
      {
         v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return x < 0.0; }), v.end());
         if (v.empty())
            return -1.0;
         std::sort(v.begin(), v.end());
         return v[v.size() / 2];
      }

      nlohmann::json Medians(const std::vector<const Record*>& rs) const
      {
         auto col = [&](auto get) {
            std::vector<double> v;
            v.reserve(rs.size());
            for (const Record* r : rs)
               v.push_back(get(*r));
            const double m = Median(v);
            return m < 0.0 ? nlohmann::json(nullptr) : nlohmann::json(m);
         };
         nlohmann::json j = {
            { "interval_ms", col([](const Record& r) { return r.intervalMs; }) },
            { "loop_ms", col([](const Record& r) { return r.loopMs; }) },
            { "work_ms", col([](const Record& r) { return r.workMs; }) },
            { "wait_ms", col([](const Record& r) { return r.waitMs; }) },
            { "canvas_gpu_ms", col([](const Record& r) { return r.canvasGpuMs; }) },
            { "proj_gpu_ms", col([](const Record& r) { return r.projGpuMs; }) },
            { "nodes_ms", col([](const Record& r) { return r.nodesMs; }) },
         };
         nlohmann::json st = nlohmann::json::object();
         for (int s = 0; s < kStageCount; s++)
            st[StageName(s)] = col([s](const Record& r) { return r.stageMs[(size_t)s]; });
         j["stages_ms"] = st;
         nlohmann::json seg = nlohmann::json::object();
         for (int m = 0; m < kMarkCount; m++)
            seg[SegmentName(m)] = col([m](const Record& r) { return Segment(r, m); });
         j["segments_ms"] = seg;
         int pending = 0;
         for (const Record* r : rs)
            pending += r->canvasGpuPendingAtWait ? 1 : 0;
         j["canvas_gpu_pending_at_wait"] = pending;
         return j;
      }

      nlohmann::json Report() const
      {
         std::vector<const Record*> slow, every;
         int lateArrivals = 0;
         for (const Record& r : all)
         {
            every.push_back(&r);
            if (r.intervalMs > 1.5 * periodMs)
               slow.push_back(&r);
            if (r.workMs > periodMs)
               lateArrivals++;
         }
         nlohmann::json topNodes = nlohmann::json::object(); // slow frames' top node type -> count
         for (const Record* r : slow)
            if (!r->topNode.empty())
               topNodes[r->topNode] = topNodes.value(r->topNode, 0) + 1;
         int focusedFrames = 0, focusedSlow = 0;
         nlohmann::json slowIndex = nlohmann::json::array();
         for (const Record& r : all)
            focusedFrames += r.focused ? 1 : 0;
         for (const Record* r : slow)
         {
            focusedSlow += r->focused ? 1 : 0;
            slowIndex.push_back(r->index);
         }
         // Mean body time per node type on slow vs other frames, largest
         // growth first: says which node types the slow frames spend it in.
         std::map<std::string, std::pair<double, double>> typeSum; // slow, other
         for (const Record& r : all)
         {
            const bool isSlow = r.intervalMs > 1.5 * periodMs;
            for (const auto& [t, ms] : r.typeMs)
               (isSlow ? typeSum[t].first : typeSum[t].second) += ms;
         }
         const double nSlow = std::max<double>(1.0, (double)slow.size());
         const double nOther = std::max<double>(1.0, (double)(all.size() - slow.size()));
         std::vector<std::pair<double, std::string>> growth;
         for (const auto& [t, s] : typeSum)
            growth.push_back({ s.first / nSlow - s.second / nOther, t });
         std::sort(growth.rbegin(), growth.rend());
         nlohmann::json typeGrowth = nlohmann::json::array();
         for (size_t i = 0; i < growth.size() && i < 10; i++)
         {
            const auto& s = typeSum[growth[i].second];
            typeGrowth.push_back({ { "type", growth[i].second },
                                   { "slow_ms", s.first / nSlow }, { "other_ms", s.second / nOther } });
         }
         nlohmann::json firstSlow = nlohmann::json::array();
         for (size_t i = 0; i < slow.size() && i < 12; i++)
         {
            const Record& r = *slow[i];
            nlohmann::json st = nlohmann::json::object();
            for (int s = 0; s < kStageCount; s++)
               st[StageName(s)] = r.stageMs[(size_t)s];
            nlohmann::json seg = nlohmann::json::object();
            for (int m = 0; m < kMarkCount; m++)
               seg[SegmentName(m)] = Segment(r, m);
            firstSlow.push_back({
               { "interval_ms", r.intervalMs }, { "loop_ms", r.loopMs },
               { "work_ms", r.workMs }, { "wait_ms", r.waitMs },
               { "canvas_gpu_ms", r.canvasGpuMs }, { "proj_gpu_ms", r.projGpuMs },
               { "stages_ms", st }, { "segments_ms", seg },
               { "top_node", r.topNode }, { "top_node_ms", r.topNodeMs },
               { "nodes_ms", r.nodesMs }, { "focused", r.focused },
            });
         }
         return {
            { "period_ms", periodMs },
            { "frames", (int)all.size() },
            { "count", (int)slow.size() },
            { "late_arrivals", lateArrivals }, // work > one period: missed before the wait began
            { "slow_median", slow.empty() ? nlohmann::json(nullptr) : Medians(slow) },
            { "all_median", every.empty() ? nlohmann::json(nullptr) : Medians(every) },
            { "slow_top_nodes", topNodes },
            { "focused_frames", focusedFrames }, { "focused_slow", focusedSlow },
            { "slow_index", slowIndex },
            { "node_type_growth", typeGrowth },
            { "first_slow", firstSlow },
         };
      }
   };

   inline FrameTail& Tail()
   {
      static FrameTail sTail;
      return sTail;
   }
}
