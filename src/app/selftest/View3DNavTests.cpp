// 3D View navigation self-test (INFINITE_VIEW3DNAVTEST): synthetic wheel and key events through ImGui, so the real
// viewport (hover, active id, scroll) is in the loop. Orbit, Shift (no pan), Ctrl zoom, in an empty scene and after an
// object was added and selected.
#include "app/AppShared.h"
#include "nodes/Scene3DNode.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "nodes/EditMeshNode.h"

namespace app
{
bool View3DNavProbe(float out[18]);
void View3DAddNode(GraphNode& gn);
void View3DTestEdit(bool on);
bool View3DEditProbe(std::string* readout, int* axis, bool* plane);

namespace
{
   struct Probe
   {
      float v[18] = {};
      bool ok = false;
   };

   Probe Read()
   {
      Probe p;
      p.ok = View3DNavProbe(p.v);
      return p;
   }

   float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

   void Delta(const Probe& a, const Probe& b, float d[3])
   {
      for (int k = 0; k < 3; k++)
         d[k] = b.v[3 + k] - a.v[3 + k];
   }

   bool sOk = true;

   void Check(const char* label, bool pass)
   {
      printf("  [%s] %s\n", pass ? "pass" : "FAIL", label);
      if (!pass)
         sOk = false;
   }
}

void RunView3DNavTest(int f)
{
   static Probe before;
   ImGuiIO& io = ImGui::GetIO();
   auto turned = [](const Probe& a, const Probe& b) { return std::abs(b.v[0] - a.v[0]) + std::abs(b.v[1] - a.v[1]) > 1e-3f; };
   auto zoomed = [](const Probe& a, const Probe& b) { return std::abs(b.v[2] - a.v[2]) > 1e-3f; };
#ifdef __APPLE__
   const bool plainOrbits = true;
#else
   const bool plainOrbits = false;
#endif
   switch (f)
   {
   case 4:
      NewPatch();
      gView3DOpen = true;
      break;
   case 10:
      before = Read();
      Check("the viewport is drawn", before.ok);
      io.AddMousePosEvent(before.v[12], before.v[13]);
      break;
   case 12:
      io.AddMouseWheelEvent(0.0f, 5.0f);
      break;
   case 15:
   {
      const Probe now = Read();
      Check("the pointer is over the viewport", now.ok && now.v[15] > 0.5f);
      Check(plainOrbits ? "empty scene: plain scroll orbits" : "empty scene: plain wheel zooms",
            plainOrbits ? turned(before, now) : zoomed(before, now));
      break;
   }
   case 16:
      if (GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f))
         View3DAddNode(*cube);
      break;
   case 18:
      before = Read();
      Check("an object was added and selected", before.ok && before.v[17] >= 1.0f);
      io.AddMousePosEvent(before.v[12], before.v[13]);
      break;
   case 19:
      io.AddMouseWheelEvent(0.0f, 5.0f);
      break;
   case 22:
   {
      const Probe now = Read();
      Check("selected: the pointer is still over the viewport", now.ok && now.v[15] > 0.5f);
      Check("selected: nothing else holds the active id", now.v[16] == 0.0f);
      Check("selected: the card around the viewport did not scroll", now.v[14] == before.v[14]);
      Check(plainOrbits ? "selected: plain scroll orbits" : "selected: plain wheel zooms",
            plainOrbits ? turned(before, now) : zoomed(before, now));
      before = now;
      io.AddKeyEvent(ImGuiMod_Shift, true);
      break;
   }
   case 24:
      // macOS hands a Shift swipe up and down over as a horizontal scroll.
      io.AddMouseWheelEvent(4.0f, 0.0f);
      break;
   case 27:
   {
      const Probe now = Read();
      float d[3];
      Delta(before, now, d);
      const float up = std::abs(Dot(d, now.v + 9)), right = std::abs(Dot(d, now.v + 6));
      printf("  shift swipe (OS-swapped): pan along up %.4f, along right %.4f\n", up, right);
      Check("Shift + swipe no longer pans", up + right < 1e-4f);
      before = now;
      break;
   }
   case 28:
      io.AddMouseWheelEvent(0.0f, 4.0f);
      break;
   case 31:
   {
      const Probe now = Read();
      float d[3];
      Delta(before, now, d);
      const float up = std::abs(Dot(d, now.v + 9)), right = std::abs(Dot(d, now.v + 6));
      Check("Shift + vertical scroll no longer pans", up + right < 1e-4f);
      io.AddKeyEvent(ImGuiMod_Shift, false);
      before = now;
      break;
   }
   case 33:
      io.AddKeyEvent(ImGuiMod_Ctrl, true);
      break;
   case 34:
      io.AddMouseWheelEvent(0.0f, 5.0f);
      break;
   case 37:
      Check("Ctrl + scroll zooms", zoomed(before, Read()));
      io.AddKeyEvent(ImGuiMod_Ctrl, false);
      break;
   case 39:
      printf("VIEW3DNAVTEST %s\n", sOk ? "OK" : "FAIL");
      break;
   default:
      break;
   }
}

// ============================================================ INFINITE_VIEW3DEDITTEST
//
// Edit Mode on a Cube through real key and pointer events. The scene is Y up and drawn Z up, so the Z key must move the
// vertices along engine Y (the vertical), X along X, Shift+Z within the horizontal plane; the op line stays in engine
// order (saved patches replay it) while the on-screen readout speaks the displayed order.
namespace
{
   struct Seq
   {
      int at = 0;
      std::vector<std::pair<int, std::function<void()>>> steps;
      void Do(std::function<void()> fn, int wait = 2)
      {
         steps.push_back({ at, std::move(fn) });
         at += wait;
      }
   };

   struct Capture
   {
      std::string op, readout;
      int axis = -1;
      bool plane = false, running = false;
   };

   EditMeshNode* FindEdit()
   {
      for (GraphNode& gn : gNodes)
         if (auto* e = dynamic_cast<EditMeshNode*>(gn.node.get()))
            return e;
      return nullptr;
   }

   std::string LastLine(const std::string& ops)
   {
      const size_t cut = ops.find_last_of('\n', ops.empty() ? 0 : ops.size() - 2);
      return cut == std::string::npos ? ops : ops.substr(cut + 1);
   }

   bool Nums(const std::string& line, const char* head, float* v, int n)
   {
      if (line.rfind(head, 0) != 0)
         return false;
      const char* p = line.c_str() + strlen(head);
      for (int i = 0; i < n; i++)
      {
         char* end = nullptr;
         v[i] = strtof(p, &end);
         if (end == p)
            return false;
         p = end;
      }
      return true;
   }
}

void RunView3DEditTest(int f)
{
   static Seq seq;
   static int t0 = 0;
   static Capture cap[6];
   static float mid[2] = { 0, 0 };
   ImGuiIO& io = ImGui::GetIO();
   auto tap = [&](ImGuiKey k) { return [k, &io] { io.AddKeyEvent(k, true); }; };
   auto release = [&](ImGuiKey k) { return [k, &io] { io.AddKeyEvent(k, false); }; };
   auto key = [&](ImGuiKey k) { seq.Do(tap(k), 1); seq.Do(release(k), 2); };
   auto at = [&](float dx, float dy) { seq.Do([=, &io] { io.AddMousePosEvent(mid[0] + dx, mid[1] + dy); }, 3); };
   auto grab = [&](int slot)
   {
      seq.Do([slot]
             {
                EditMeshNode* e = FindEdit();
                Capture& c = cap[slot];
                c.op = e != nullptr ? LastLine(e->ops) : "";
                c.running = View3DEditProbe(&c.readout, &c.axis, &c.plane);
             },
             1);
   };

   if (f == 4)
   {
      NewPatch();
      gView3DOpen = true;
      return;
   }
   if (f == 8)
   {
      if (GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f))
         View3DAddNode(*cube);
      return;
   }
   if (f == 12)
   {
      float p[18];
      Check("the viewport is drawn", View3DNavProbe(p));
      mid[0] = p[12];
      mid[1] = p[13];
      View3DTestEdit(true);
      seq = Seq();
      t0 = f + 3;
      seq.Do([&io] { io.AddMousePosEvent(mid[0], mid[1]); }, 3);
      key(ImGuiKey_A);
      // Each case: pointer off the pivot, the op key, the axis key(s), pointer moved, read, Esc.
      struct Case { ImGuiKey op; ImGuiKey axis; bool shift; float tx, ty; };
      const Case cases[] = {
         { ImGuiKey_G, ImGuiKey_Z, false, 120.0f, -50.0f },   // 0: vertical
         { ImGuiKey_G, ImGuiKey_X, false, 120.0f, -50.0f },   // 1: along X
         { ImGuiKey_G, ImGuiKey_Z, true, 120.0f, -50.0f },    // 2: the horizontal plane
         { ImGuiKey_R, ImGuiKey_Z, false, 10.0f, -120.0f },   // 3: about the vertical
         { ImGuiKey_S, ImGuiKey_Z, false, 200.0f, 0.0f },     // 4: scale the vertical
         { ImGuiKey_G, ImGuiKey_Y, false, 120.0f, -50.0f },   // 5: depth
      };
      for (int i = 0; i < 6; i++)
      {
         at(80.0f, 0.0f);
         key(cases[i].op);
         if (cases[i].shift)
            seq.Do([&io] { io.AddKeyEvent(ImGuiMod_Shift, true); }, 2);
         key(cases[i].axis);
         if (cases[i].shift)
            seq.Do([&io] { io.AddKeyEvent(ImGuiMod_Shift, false); }, 2);
         at(cases[i].tx, cases[i].ty);
         grab(i);
         key(ImGuiKey_Escape);
      }
      seq.Do([] {}, 2);
      return;
   }
   if (f < t0 || t0 == 0)
      return;
   const int rel = f - t0;
   for (auto& st : seq.steps)
      if (st.first == rel)
         st.second();
   if (rel == seq.at)
   {
      float v[4];
      auto near0 = [](float x) { return std::abs(x) < 1e-4f; };
      bool parsed = Nums(cap[0].op, "move ", v, 3);
      Check("G then Z moves along the vertical only", parsed && near0(v[0]) && near0(v[2]) && std::abs(v[1]) > 1e-3f);
      const float zShown = v[1];
      parsed = Nums(cap[0].op, "move ", v, 3);
      char want[32];
      snprintf(want, sizeof(want), "Z %.3f", zShown);
      Check("the readout shows the vertical as Z", cap[0].readout.find(want) != std::string::npos);
      Check("the op is still saved in engine order (move x y z)", parsed && cap[0].op.rfind("move ", 0) == 0);
      Check("the Z key locks engine axis 1", cap[0].axis == 1);
      parsed = Nums(cap[1].op, "move ", v, 3);
      Check("G then X moves along X only", parsed && near0(v[1]) && near0(v[2]) && std::abs(v[0]) > 1e-3f);
      parsed = Nums(cap[2].op, "move ", v, 3);
      Check("G then Shift+Z moves in the horizontal plane", parsed && near0(v[1]) && (std::abs(v[0]) > 1e-3f || std::abs(v[2]) > 1e-3f) && cap[2].plane);
      parsed = Nums(cap[3].op, "rotate ", v, 4);
      Check("R then Z turns about the vertical", parsed && near0(v[0]) && near0(v[2]) && std::abs(v[1]) > 0.99f);
      parsed = Nums(cap[4].op, "scale ", v, 3);
      Check("S then Z scales the vertical only", parsed && near0(v[0] - 1.0f) && near0(v[2] - 1.0f) && std::abs(v[1] - 1.0f) > 1e-3f);
      parsed = Nums(cap[5].op, "move ", v, 3);
      Check("G then Y moves in depth (engine Z)", parsed && near0(v[0]) && near0(v[1]) && std::abs(v[2]) > 1e-3f);
      Check("every case was running when read", cap[0].running && cap[1].running && cap[2].running && cap[3].running && cap[4].running && cap[5].running);
      EditMeshNode* e = FindEdit();
      Check("Esc leaves no op behind", e != nullptr && e->ops.find("move ") == std::string::npos);
      printf("VIEW3DEDITTEST %s\n", sOk ? "OK" : "FAIL");
   }
}

// ============================================================ INFINITE_VIEW3DPIVOTTEST
//
// Shift + middle-drag pans the pivot away (Shift + two-finger scroll no longer does); Shift + double-click on empty space glides it back onto the 3D
// cursor (the origin in a new scene) and keeps the distance and the turn.
void RunView3DPivotTest(int f)
{
   static float mid[2] = {}, before[18] = {}, panned[18] = {};
   static bool ok = true;
   ImGuiIO& io = ImGui::GetIO();
   auto check = [&](const char* label, bool pass)
   {
      printf("  [%s] %s\n", pass ? "pass" : "FAIL", label);
      ok = ok && pass;
   };
   auto len3 = [](const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
   switch (f)
   {
   case 4:
      NewPatch();
      gView3DOpen = true;
      break;
   case 10:
   {
      float p[18];
      check("the viewport is drawn", View3DNavProbe(p));
      mid[0] = p[12];
      mid[1] = p[13];
      io.AddMousePosEvent(mid[0], mid[1]);
      break;
   }
   case 12:
      io.AddKeyEvent(ImGuiMod_Shift, true);
      break;
   case 14: io.AddMouseButtonEvent(2, true); break;
   case 15: io.AddMousePosEvent(mid[0] + 60.0f, mid[1] + 30.0f); break;
   case 16: io.AddMousePosEvent(mid[0] + 120.0f, mid[1] + 60.0f); break;
   case 17: io.AddMouseButtonEvent(2, false); break;
   case 19: io.AddMousePosEvent(mid[0], mid[1]); break;
   case 18:
   {
      View3DNavProbe(panned);
      printf("  pivot after drag: %.4f %.4f %.4f  (probe hovered %.0f active %.0f)\n", panned[3], panned[4], panned[5], panned[15], panned[16]);
      check("Shift + middle-drag moved the pivot off the cursor", len3(panned + 3) > 0.01f);
      break;
   }
   case 20: io.AddMouseButtonEvent(0, true); break;
   case 21: io.AddMouseButtonEvent(0, false); break;
   case 22: io.AddMouseButtonEvent(0, true); break;
   case 23: io.AddMouseButtonEvent(0, false); break;
   case 140:   // the glide takes about a third of a second
   {
      float p[18];
      View3DNavProbe(p);
      check("Shift + double-click: the pivot is on the 3D cursor", len3(p + 3) < 1e-3f);
      check("the distance stayed", std::fabs(p[2] - panned[2]) < 1e-3f);
      check("the turn stayed", std::fabs(p[0] - panned[0]) < 1e-3f && std::fabs(p[1] - panned[1]) < 1e-3f);
      io.AddKeyEvent(ImGuiMod_Shift, false);
      printf("VIEW3DPIVOTTEST %s\n", ok ? "OK" : "FAIL");
      break;
   }
   }
   (void)before;
}
} // namespace app
