// Part of src/main.cpp, included there in order (one translation unit).
// Turbo 0.73: the Circuit Synth body - preset, generator and play mode on
// top, a stereo scope, one knob per part of the circuit, then play, LFO and
// stereo sections.

   const std::vector<std::string>& CircuitPresetNames()
   {
      static const std::vector<std::string> names = []
      {
         std::vector<std::string> v;
         for (int i = 0; i < CircuitDsp::NumPresets(); i++)
            v.push_back(CircuitDsp::GetPreset(i).name);
         return v;
      }();
      return names;
   }

   std::vector<std::string> CircuitProbeNames(const CircuitDsp::Preset& pr)
   {
      std::vector<std::string> v;
      for (int i = 0; i < pr.numProbes; i++)
         v.push_back(pr.probes[i].name);
      return v;
   }

   // The last ~20 ms of L (cyan) and R (amber) over each other: two traces
   // that coincide are mono, two that slide against each other are the
   // twins and probes at work. Also returns the window's L/R correlation.
   float DrawCircuitScope(CircuitSynthNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeTime < 0.0 || now - n->scopeTime > 1.0 / 30.0)
      {
         float buf[CircuitSynthNode::kScopeCapacity * 4];
         const int cap = (int)(sizeof(buf) / sizeof(buf[0]));
         int count = 0, got;
         while (count < cap && (got = n->ReadScope(buf + count, cap - count)) > 0)
            count += got;
         count &= ~1;
         if (count >= 4)
         {
            const int keep = std::min(count, CircuitSynthNode::kScopeCapacity);
            std::copy(buf + count - keep, buf + count, n->scopeCache);
            n->scopeCount = keep;
         }
         n->scopeTime = now;
      }

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);
      const float midY = origin.y + h * 0.5f;
      for (int i = 1; i < 8; i++)
         dl->AddLine(ImVec2(origin.x + w * i / 8.0f, origin.y), ImVec2(origin.x + w * i / 8.0f, br.y), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      float corr = 1.0f;
      const int pairs = n->scopeCount / 2;
      double peak = 0.0;
      for (int i = 0; i < pairs * 2; i++)
         peak = std::max(peak, (double)std::fabs(n->scopeCache[i]));
      if (pairs > 1 && peak > 1e-4)
      {
         double sl = 0.0, sr = 0.0, slr = 0.0;
         for (int i = 0; i < pairs; i++)
         {
            const double l = n->scopeCache[2 * i], r = n->scopeCache[2 * i + 1];
            sl += l * l;
            sr += r * r;
            slr += l * r;
         }
         corr = (float)(slr / std::sqrt(sl * sr + 1e-30));
         const float scale = (float)(h * 0.44 / std::max(0.25, peak)); // small signals still fill the view
         const bool isLight = IsThemeLight();
         for (int ch = 1; ch >= 0; ch--)
         {
            dl->PathClear();
            for (int i = 0; i < pairs; i++)
               dl->PathLineTo(ImVec2(origin.x + w * (float)i / (float)(pairs - 1), midY - n->scopeCache[2 * i + ch] * scale));
            const ImU32 col = ch == 0 ? (isLight ? IM_COL32(0, 130, 200, 240) : IM_COL32(80, 220, 255, 235))
                                      : (isLight ? IM_COL32(210, 120, 10, 220) : IM_COL32(255, 190, 80, 210));
            dl->PathStroke(col, 0, 1.5f);
         }
         char txt[32];
         snprintf(txt, sizeof(txt), "L/R %.2f", corr);
         dl->AddText(ImVec2(br.x - ImGui::CalcTextSize(txt).x - 6.0f, origin.y + 3.0f), ScopeTextCol(), txt);
      }
      else
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "tap / play a note");
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
      return corr;
   }

   // The loaded circuit as a schematic, drawn from the preset's netlist and
   // its grid layout: every part with its symbol and, next to it, the name of
   // the knob that scales it (lit while that knob is under the mouse); A and
   // B mark where the left and right sides listen.
   void DrawCircuitSchematic(CircuitSynthNode* n, const CircuitDsp::Preset& pr, float h, float w)
   {
      using namespace CircuitDsp;
      const ImVec2 o = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(o.x + w, o.y + h);
      dl->AddRectFilled(o, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(o, br, true);
      const bool light = IsThemeLight();
      const ImU32 wireCol = light ? IM_COL32(90, 100, 120, 255) : IM_COL32(120, 132, 156, 255);
      const ImU32 partCol = light ? IM_COL32(40, 48, 64, 255) : IM_COL32(196, 206, 226, 255);
      const ImU32 litCol = light ? IM_COL32(220, 110, 0, 255) : IM_COL32(255, 196, 84, 255);
      const ImU32 labelCol = light ? IM_COL32(60, 90, 160, 255) : IM_COL32(140, 190, 255, 255);
      const ImU32 colA = light ? IM_COL32(0, 130, 200, 255) : IM_COL32(80, 220, 255, 255);
      const ImU32 colB = light ? IM_COL32(210, 120, 10, 255) : IM_COL32(255, 190, 80, 255);

      // Ground parts hang under their node, spread when there are several.
      float groundOffset[kMaxParts] = {};
      int groundNode[kMaxParts];
      int groundCount[kMaxNodes + 1] = {};
      for (int k = 0; k < pr.numParts; k++)
      {
         const PartDef& d = pr.parts[k];
         groundNode[k] = (d.kind != kOpAmp && (d.a == 0) != (d.b == 0)) ? (d.a ? d.a : d.b) : 0;
         if (groundNode[k])
            groundCount[groundNode[k]]++;
      }
      {
         int seen[kMaxNodes + 1] = {};
         for (int k = 0; k < pr.numParts; k++)
            if (const int g = groundNode[k])
               groundOffset[k] = 0.38f * ((float)seen[g]++ - 0.5f * (float)(groundCount[g] - 1));
      }
      // Parallel parts between the same two nodes take detours one above another.
      int dupIndex[kMaxParts] = {};
      for (int k = 0; k < pr.numParts; k++)
      {
         const PartDef& d = pr.parts[k];
         if (groundNode[k] || d.kind == kOpAmp || d.a == 0 || d.b == 0)
            continue;
         for (int j = 0; j < k; j++)
         {
            const PartDef& e = pr.parts[j];
            if (!groundNode[j] && e.kind != kOpAmp && ((e.a == d.a && e.b == d.b) || (e.a == d.b && e.b == d.a)) &&
                e.route == d.route)
               dupIndex[k]++;
         }
      }

      // Grid bounds -> pixels.
      float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
      auto grow = [&](float x, float y)
      {
         minX = std::min(minX, x);
         maxX = std::max(maxX, x);
         minY = std::min(minY, y);
         maxY = std::max(maxY, y);
      };
      for (int i = 1; i <= pr.numNodes; i++)
         grow(pr.nodeX[i], pr.nodeY[i]);
      for (int k = 0; k < pr.numParts; k++)
      {
         const PartDef& d = pr.parts[k];
         if (const int g = groundNode[k])
            grow(pr.nodeX[g] + groundOffset[k], pr.nodeY[g] + 1.0f);
         else if (d.kind == kOpAmp && d.a == 0)
            grow(pr.nodeX[d.c], pr.nodeY[d.c] + 1.0f);
         if (d.route == kRouteOver)
            grow(pr.nodeX[d.a], std::min(pr.nodeY[d.a], pr.nodeY[d.b]) - 0.8f);
         if (d.route == kRouteUnder)
            grow(pr.nodeX[d.a], std::max(pr.nodeY[d.a], pr.nodeY[d.b]) + 0.8f);
         if (dupIndex[k])
            grow(pr.nodeX[d.a], std::min(pr.nodeY[d.a], pr.nodeY[d.b]) - 0.4f * dupIndex[k]);
      }
      // x and y scale apart: a circuit is usually wider than tall, and the
      // panel is too. Room on top for the labels of the top row.
      const float padX = 22.0f, padTop = 20.0f, padBottom = 14.0f;
      const float spanX = std::max(1.0f, maxX - minX), spanY = std::max(1.0f, maxY - minY);
      const float unitX = std::min(90.0f, (w - 2.0f * padX) / spanX);
      const float unitY = std::min(64.0f, (h - padTop - padBottom) / spanY);
      const float unit = std::min(unitX, unitY);
      const float ox = o.x + 0.5f * (w - spanX * unitX) - minX * unitX;
      const float oy = o.y + padTop + 0.5f * (h - padTop - padBottom - spanY * unitY) - minY * unitY;
      auto P = [&](float x, float y) { return ImVec2(ox + x * unitX, oy + y * unitY); };
      const float s = std::clamp(unit * 0.22f, 6.0f, 13.0f); // symbol half length

      auto groundSym = [&](ImVec2 g, ImU32 col)
      {
         for (int i = 0; i < 3; i++)
         {
            const float hw = s * (0.6f - 0.18f * i);
            dl->AddLine(ImVec2(g.x - hw, g.y + i * 3.0f), ImVec2(g.x + hw, g.y + i * 3.0f), col, 1.2f);
         }
      };
      ImFont* font = ImGui::GetFont();
      const float fontSize = ImGui::GetFontSize() * 0.8f;
      auto text = [&](ImVec2 at, ImU32 col, const char* t)
      {
         const ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, t);
         dl->AddText(font, fontSize, ImVec2(at.x - sz.x * 0.5f, at.y - sz.y * 0.5f), col, t);
      };

      // A part's symbol centred at c, along unit direction d (dir: anode to cathode for diodes).
      auto symbol = [&](const PartDef& part, ImVec2 c, ImVec2 d, float dir, ImU32 col, float thick)
      {
         const ImVec2 nrm(-d.y, d.x);
         auto at = [&](float along, float side) { return ImVec2(c.x + d.x * along + nrm.x * side, c.y + d.y * along + nrm.y * side); };
         switch (part.kind)
         {
         case kResistor:
         {
            dl->PathLineTo(at(-s, 0.0f));
            for (int i = 0; i < 6; i++)
               dl->PathLineTo(at(-s + (2.0f * s) * (i + 0.5f) / 6.0f, (i & 1) ? -s * 0.38f : s * 0.38f));
            dl->PathLineTo(at(s, 0.0f));
            dl->PathStroke(col, 0, thick);
            break;
         }
         case kCapacitor:
            dl->AddLine(at(-s, 0.0f), at(-s * 0.22f, 0.0f), col, thick);
            dl->AddLine(at(s * 0.22f, 0.0f), at(s, 0.0f), col, thick);
            dl->AddLine(at(-s * 0.22f, -s * 0.6f), at(-s * 0.22f, s * 0.6f), col, thick + 0.6f);
            dl->AddLine(at(s * 0.22f, -s * 0.6f), at(s * 0.22f, s * 0.6f), col, thick + 0.6f);
            break;
         case kInductor:
         {
            const float r = s / 4.0f;
            for (int i = 0; i < 4; i++)
            {
               const float cx = -s + r * (2 * i + 1);
               for (int t = 0; t <= 8; t++)
               {
                  const float a = 3.14159265f * t / 8.0f;
                  dl->PathLineTo(at(cx - r * std::cos(a), -r * 1.3f * std::sin(a)));
               }
            }
            dl->PathStroke(col, 0, thick);
            break;
         }
         case kDiode:
         {
            const float k = s * 0.4f * dir;
            dl->AddLine(at(-s, 0.0f), at(-k, 0.0f), col, thick);
            dl->AddLine(at(k, 0.0f), at(s, 0.0f), col, thick);
            dl->AddTriangleFilled(at(-k, -s * 0.45f), at(-k, s * 0.45f), at(k, 0.0f), col);
            dl->AddLine(at(k, -s * 0.45f), at(k, s * 0.45f), col, thick + 0.6f);
            break;
         }
         case kNeon:
            dl->AddCircle(c, s * 0.6f, col, 16, thick);
            dl->AddCircleFilled(at(-s * 0.22f, 0.0f), 1.8f, col);
            dl->AddCircleFilled(at(s * 0.22f, 0.0f), 1.8f, col);
            dl->AddLine(at(-s, 0.0f), at(-s * 0.6f, 0.0f), col, thick);
            dl->AddLine(at(s * 0.6f, 0.0f), at(s, 0.0f), col, thick);
            break;
         case kChua:
         case kNegRes:
         {
            const ImVec2 a = at(-s * 0.6f, -s * 0.45f), b = at(s * 0.6f, s * 0.45f);
            dl->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), col, 0.0f, 0, thick);
            dl->AddLine(at(-s, 0.0f), at(-s * 0.6f, 0.0f), col, thick);
            dl->AddLine(at(s * 0.6f, 0.0f), at(s, 0.0f), col, thick);
            text(c, col, part.kind == kChua ? "N" : "-R");
            break;
         }
         case kGenerator:
         {
            dl->AddCircle(c, s * 0.7f, col, 20, thick);
            for (int t = 0; t <= 10; t++)
            {
               const float u = -0.4f + 0.8f * t / 10.0f;
               dl->PathLineTo(ImVec2(c.x + u * s, c.y - std::sin(u * 3.14159265f / 0.4f) * s * 0.22f));
            }
            dl->PathStroke(col, 0, thick);
            dl->AddLine(at(-s, 0.0f), at(-s * 0.7f, 0.0f), col, thick);
            dl->AddLine(at(s * 0.7f, 0.0f), at(s, 0.0f), col, thick);
            break;
         }
         default:
            break;
         }
      };

      // A wire through pts with the part's symbol in the middle of segment seg.
      auto routed = [&](const PartDef& part, const ImVec2* pts, int count, int seg, float dir, ImU32 col, float thick, ImVec2& center)
      {
         for (int i = 0; i + 1 < count; i++)
         {
            const ImVec2 a = pts[i], b = pts[i + 1];
            if (i != seg)
            {
               dl->AddLine(a, b, wireCol, 1.2f);
               continue;
            }
            const float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            const ImVec2 d = len > 1e-3f ? ImVec2((b.x - a.x) / len, (b.y - a.y) / len) : ImVec2(1.0f, 0.0f);
            center = ImVec2(0.5f * (a.x + b.x), 0.5f * (a.y + b.y));
            const float half = std::min(s, len * 0.5f);
            dl->AddLine(a, ImVec2(center.x - d.x * half, center.y - d.y * half), wireCol, 1.2f);
            dl->AddLine(ImVec2(center.x + d.x * half, center.y + d.y * half), b, wireCol, 1.2f);
            symbol(part, center, d, dir, col, thick);
         }
      };

      ImVec2 partCenter[kMaxParts];
      ImVec2 partNormal[kMaxParts];
      for (int k = 0; k < pr.numParts; k++)
      {
         const PartDef& d = pr.parts[k];
         const bool lit = d.knob >= 0 && d.knob == n->hoverKnob;
         const ImU32 col = lit ? litCol : partCol;
         const float thick = lit ? 2.2f : 1.4f;
         partCenter[k] = ImVec2(-1e4f, -1e4f);
         partNormal[k] = ImVec2(1.0f, 0.0f);
         if (d.kind == kOpAmp)
         {
            const ImVec2 out = P(pr.nodeX[d.c], pr.nodeY[d.c]);
            const float hs = std::clamp(unit * 0.3f, 8.0f, 16.0f);
            const ImVec2 t(out.x - unitX * 0.5f, out.y);
            dl->AddTriangle(ImVec2(t.x - hs, t.y - hs), ImVec2(t.x - hs, t.y + hs), ImVec2(t.x + hs, t.y), col, thick);
            dl->AddLine(ImVec2(t.x + hs, t.y), out, wireCol, 1.2f);
            const ImVec2 inMinus(t.x - hs, t.y - hs * 0.5f), inPlus(t.x - hs, t.y + hs * 0.5f);
            text(ImVec2(inMinus.x + 5.0f, inMinus.y), col, "-");
            text(ImVec2(inPlus.x + 5.0f, inPlus.y), col, "+");
            for (int side = 0; side < 2; side++)
            {
               const int node = side == 0 ? d.b : d.a;
               const ImVec2 in = side == 0 ? inMinus : inPlus;
               if (node == 0)
               {
                  const ImVec2 g(in.x - 6.0f, in.y + std::max(8.0f, unitY * 0.3f));
                  dl->AddLine(in, ImVec2(in.x - 6.0f, in.y), wireCol, 1.2f);
                  dl->AddLine(ImVec2(in.x - 6.0f, in.y), g, wireCol, 1.2f);
                  groundSym(g, wireCol);
               }
               else
               {
                  const ImVec2 m = P(pr.nodeX[node], pr.nodeY[node]);
                  dl->AddLine(m, ImVec2(in.x - 6.0f, m.y), wireCol, 1.2f);
                  dl->AddLine(ImVec2(in.x - 6.0f, m.y), ImVec2(in.x - 6.0f, in.y), wireCol, 1.2f);
                  dl->AddLine(ImVec2(in.x - 6.0f, in.y), in, wireCol, 1.2f);
               }
            }
            partCenter[k] = t;
            continue;
         }
         if (const int g = groundNode[k])
         {
            // Node at the top, ground below; a diode or generator keeps its sense.
            const ImVec2 top = P(pr.nodeX[g], pr.nodeY[g]);
            const ImVec2 a = P(pr.nodeX[g] + groundOffset[k], pr.nodeY[g]);
            const ImVec2 b = P(pr.nodeX[g] + groundOffset[k], pr.nodeY[g] + 1.0f);
            dl->AddLine(top, a, wireCol, 1.2f);
            const ImVec2 pts[2] = { a, b };
            routed(d, pts, 2, 0, d.a == g ? 1.0f : -1.0f, col, thick, partCenter[k]);
            groundSym(b, wireCol);
            continue;
         }
         // Between two nodes.
         const float ax = pr.nodeX[d.a], ay = pr.nodeY[d.a], bx = pr.nodeX[d.b], by = pr.nodeY[d.b];
         float g[4][2];
         int count = 2, seg = 0;
         g[0][0] = ax;
         g[0][1] = ay;
         const bool straight = std::fabs(ay - by) < 1e-3f || std::fabs(ax - bx) < 1e-3f;
         if (d.route == kRouteOver || d.route == kRouteUnder || dupIndex[k] > 0)
         {
            // A detour: up (over, or a parallel copy) or down, across, back.
            const float y = d.route == kRouteUnder ? std::max(ay, by) + 0.8f
                          : d.route == kRouteOver ? std::min(ay, by) - 0.8f
                                                  : std::min(ay, by) - 0.4f * dupIndex[k];
            g[1][0] = ax; g[1][1] = y;
            g[2][0] = bx; g[2][1] = y;
            g[3][0] = bx; g[3][1] = by;
            count = 4;
            seg = 1;
         }
         else if (straight)
         {
            g[1][0] = bx;
            g[1][1] = by;
         }
         else
         {
            // L route: the horizontal leg at the upper node's height (or the lower's).
            const bool aUpper = ay < by;
            const bool horizAtA = d.route == kRouteLow ? !aUpper : aUpper;
            g[1][0] = horizAtA ? bx : ax;
            g[1][1] = horizAtA ? ay : by;
            g[2][0] = bx;
            g[2][1] = by;
            count = 3;
            const float horiz = std::fabs(bx - ax), vert = std::fabs(by - ay);
            const int hSeg = horizAtA ? 0 : 1;
            seg = (d.route == kRouteVertical || vert > horiz) ? 1 - hSeg : hSeg;
         }
         ImVec2 pts[4];
         for (int i = 0; i < count; i++)
            pts[i] = P(g[i][0], g[i][1]);
         // The symbol's segment runs from a towards b: a diode points along it.
         const ImVec2 sa = pts[seg], sb = pts[seg + 1];
         routed(d, pts, count, seg, 1.0f, col, thick, partCenter[k]);
         partNormal[k] = std::fabs(sb.y - sa.y) > std::fabs(sb.x - sa.x) ? ImVec2(1.0f, 0.0f)
                       : d.route == kRouteUnder                       ? ImVec2(0.0f, 1.0f)
                                                                      : ImVec2(0.0f, -1.0f);
      }
      // The knob's name next to every part it scales: on one side of the
      // symbol or the other, wherever it prints over no symbol and no other
      // name and stays inside the panel; else left out (a group of parts
      // then shows it once or a few times - hovering the knob still lights
      // every one of them, and a lit name always prints).
      ImVec2 used[kMaxParts * 2][2];
      int numUsed = 0;
      for (int k = 0; k < pr.numParts; k++)
         if (partCenter[k].x > -1e3f)
         {
            const float r = pr.parts[k].kind == kOpAmp ? std::clamp(unit * 0.3f, 8.0f, 16.0f) : s * 0.8f;
            used[numUsed][0] = ImVec2(partCenter[k].x - r, partCenter[k].y - r);
            used[numUsed][1] = ImVec2(partCenter[k].x + r, partCenter[k].y + r);
            numUsed++;
         }
      for (int k = 0; k < pr.numParts; k++)
      {
         const PartDef& d = pr.parts[k];
         if (d.knob < 0 || d.knob >= pr.numKnobs || partCenter[k].x < -1e3f)
            continue;
         const bool lit = d.knob == n->hoverKnob;
         const ImVec2 nrm = groundNode[k] ? ImVec2(1.0f, 0.0f) : partNormal[k];
         const char* label = pr.knobs[d.knob].label;
         const ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
         bool placed = false;
         for (int side = 0; side < 2 && !placed; side++)
         {
            const float sgn = side == 0 ? 1.0f : -1.0f;
            // Beside a vertical symbol the name starts at its edge; above / below a horizontal one it is centred.
            const ImVec2 at(partCenter[k].x + sgn * nrm.x * (s + 4.0f + 0.5f * sz.x), partCenter[k].y + sgn * nrm.y * (s + 6.0f));
            const ImVec2 lo(at.x - 0.5f * sz.x - 2.0f, at.y - 0.5f * sz.y), hi(at.x + 0.5f * sz.x + 2.0f, at.y + 0.5f * sz.y);
            bool clash = lo.x < o.x + 2.0f || hi.x > br.x - 2.0f || lo.y < o.y + 1.0f || hi.y > br.y - 1.0f;
            for (int u = 0; u < numUsed && !clash; u++)
               clash = lo.x < used[u][1].x && hi.x > used[u][0].x && lo.y < used[u][1].y && hi.y > used[u][0].y;
            if (clash && !(lit && side == 1))
               continue;
            if (numUsed < kMaxParts * 2)
            {
               used[numUsed][0] = lo;
               used[numUsed][1] = hi;
               numUsed++;
            }
            text(at, lit ? litCol : labelCol, label);
            placed = true;
         }
      }
      for (int i = 1; i <= pr.numNodes; i++)
         dl->AddCircleFilled(P(pr.nodeX[i], pr.nodeY[i]), 2.0f, wireCol);
      // Probes: where each side listens.
      for (int side = 0; side < 2 && pr.numProbes > 0; side++)
      {
         const int pi = std::clamp(side == 0 ? n->probeA : n->probeB, 0, pr.numProbes - 1);
         const ProbeDef& probe = pr.probes[pi];
         const ImU32 col = side == 0 ? colA : colB;
         const char* letter = side == 0 ? "A" : "B";
         auto mark = [&](ImVec2 at)
         {
            const ImVec2 c(at.x + (side == 0 ? -7.0f : 7.0f), at.y - 9.0f);
            dl->AddCircleFilled(c, 6.0f, col);
            text(c, light ? IM_COL32(255, 255, 255, 255) : IM_COL32(16, 20, 28, 255), letter);
         };
         if (probe.current)
         {
            if (probe.part >= 0 && probe.part < pr.numParts && partCenter[probe.part].x > -1e3f)
               mark(partCenter[probe.part]);
         }
         else
            for (int k = 0; k < 4; k++)
               if (probe.node[k] > 0 && probe.weight[k] != 0.0f)
                  mark(P(pr.nodeX[probe.node[k]], pr.nodeY[probe.node[k]]));
      }
      dl->PopClipRect();
      dl->AddRect(o, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawCircuitSynthBody(GraphNode& gn, CircuitSynthNode* n)
   {
      const CircuitDsp::Preset& pr = CircuitDsp::GetPreset(n->preset);
      const int voices = n->ActiveVoices();
      char stat[128];
      if (n->mode == CircuitDsp::kPoly)
         snprintf(stat, sizeof(stat), "%s  -  poly %d/%d", pr.name, voices, n->MaxVoices());
      else
         snprintf(stat, sizeof(stat), "%s  -  mono%s", pr.name, voices > 0 ? "  -  playing" : "");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: circuit, generator wave, mono / poly, tap / hold.
      {
         static const std::vector<std::string> kWaves = { "saw", "pulse", "sine", "noise", "step" };
         static const std::vector<std::string> kModes = { "mono", "poly" };
         static const std::vector<std::string> kTriggers = { "tap", "hold" };
         const float w = gAudioContentW, x0 = gAudioContentX, y = ImGui::GetCursorScreenPos().y, gap = 5.0f;
         const float waveW = 72.0f, modeW = 64.0f, trigW = 60.0f;
         const float presetW = std::max(90.0f, w - waveW - modeW - trigW - gap * 3.0f);
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("csPreset", CircuitPresetNames(), n->preset, [n](int i) { PushUndoCheckpoint(); n->ApplyPreset(i); }, presetW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + presetW + gap, y));
         AudioBareDropdown("csWave", kWaves, n->wave, [n](int i) { PushUndoCheckpoint(); n->wave = i; }, waveW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + presetW + waveW + gap * 2.0f, y));
         AudioBareDropdown("csMode", kModes, n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; }, modeW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + w - trigW, y));
         AudioBareDropdown("csTrigger", kTriggers, n->trigger, [n](int i) { PushUndoCheckpoint(); n->trigger = i; }, trigW);
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Stereo scope and the TAP button.
      {
         const float w = gAudioContentW, tapW = 64.0f, scopeH = 62.0f;
         const ImVec2 pos = ImGui::GetCursorScreenPos();
         DrawCircuitScope(n, scopeH, w - tapW - 6.0f);
         ImGui::SetCursorScreenPos(ImVec2(pos.x + w - tapW, pos.y));
         if (ModTriggerButton("TAP##circuitTap", ImVec2(tapW, scopeH)))
            n->Tap();
         ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + scopeH));
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // The circuit itself. Each part carries the name of its knob; hovering a
      // knob below lights the parts it scales.
      DrawCircuitSchematic(n, pr, 150.0f, gAudioContentW);
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // One knob per part (or group of parts). The caption is the part's
      // name; the readout shows its value. Always two rows, so switching to
      // a circuit with fewer parts never resizes the node.
      BeginAudioSection("parts");
      int hovered = -1;
      for (int rowStart = 0; rowStart < CircuitSynthNode::kKnobs; rowStart += 4)
      {
         AudioKnobRow row(4);
         for (int k = rowStart; k < rowStart + 4; k++)
         {
            if (k >= pr.numKnobs || pr.knobs[k].part < 0)
            {
               row.index++;
               continue;
            }
            const CircuitDsp::PartDef& part = pr.parts[pr.knobs[k].part];
            char value[48], fmt[96];
            CircuitDsp::FormatPartValue(part, CircuitDsp::KnobScale(part.kind, n->knob[k]), value, sizeof(value));
            int o = 0;
            for (const char* c = value; *c && o < (int)sizeof(fmt) - 2; c++)
            {
               if (*c == '%')
                  fmt[o++] = '%';
               fmt[o++] = *c;
            }
            fmt[o] = 0;
            const ImVec2 cellMin(row.x0 + row.index * row.cellW, row.y0);
            const ImVec2 cellMax(cellMin.x + row.cellW, cellMin.y + row.rowH);
            row.Knob(pr.knobs[k].label, &n->knob[k], -1.0f, 1.0f, fmt, k < 2 ? kKnobLarge : kKnobSmall);
            if (ImGui::IsMouseHoveringRect(cellMin, cellMax))
               hovered = k;
         }
         row.End();
      }
      static const char* sForcedHover = getenv("INFINITE_CIRCUITSYNTHUITEST_HOVER"); // UI fixture: light a knob's parts
      n->hoverKnob = sForcedHover ? std::atoi(sForcedHover) : hovered;
      EndAudioSection();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      BeginAudioSection("play");
      {
         AudioKnobRow row(5);
         row.Knob("pitch", &n->pitch, -24.0f, 24.0f, "%+.1f st");
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s");
         row.Knob("contact", &n->contactMs, 0.05f, 50.0f, "%.2f ms");
         row.Knob("attack", &n->attack, 0.0005f, 2.0f, "%.3f s");
         row.Knob("release", &n->release, 0.01f, 8.0f, "%.2f s");
         row.End();
      }
      AudioToggleButton("C key##circuitCFollow", &n->cFollow, 64.0f);
      ImGui::SameLine();
      AudioToggleButton("L key##circuitLFollow", &n->lFollow, 64.0f);
      EndAudioSection();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      BeginAudioSection("lfo");
      {
         static const std::vector<std::string> kShapes = { "sine", "triangle", "square", "random" };
         static const std::vector<std::string> kTargets = { "tune", "power", "damp", "level", "cutoff" };
         AudioKnobRow row(4);
         row.Knob("rate", &n->lfoRate, 0.05f, 20.0f, "%.2f Hz");
         row.Knob("depth", &n->lfoDepth, 0.0f, 1.0f, "%.2f");
         row.Dropdown("shape", kShapes, n->lfoShape, [n](int i) { PushUndoCheckpoint(); n->lfoShape = i; });
         row.Dropdown("target", kTargets, n->lfoTarget, [n](int i) { PushUndoCheckpoint(); n->lfoTarget = i; });
         row.End();
      }
      EndAudioSection();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // The output filter: the same types as the other synths.
      BeginAudioSection("filter");
      {
         const bool off = !SynthModes::FilterUsesCutoff(n->filterType);
         AudioKnobRow row(3, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row.DropdownKnob("csFilter", SynthModes::FilterTypeList(), n->filterType,
                          [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                          "cutoff", &n->cutoff, 20.0f, 18000.0f, "%.0f Hz", off, kKnobLarge, true);
         if (off)
            ImGui::BeginDisabled();
         row.Knob("reso", &n->reso, 0.0f, 1.0f, "%.2f");
         row.Knob("key", &n->filterKey, 0.0f, 1.0f, "%.2f");
         if (off)
            ImGui::EndDisabled();
         row.End();
      }
      EndAudioSection();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      BeginAudioSection("stereo");
      {
         const std::vector<std::string> probes = CircuitProbeNames(pr);
         AudioKnobRow row(5);
         row.Dropdown("probe A", probes, n->probeA, [n](int i) { PushUndoCheckpoint(); n->probeA = i; });
         row.Dropdown("probe B", probes, n->probeB, [n](int i) { PushUndoCheckpoint(); n->probeB = i; });
         row.Knob("width", &n->width, 0.0f, 1.5f, "%.2f");
         row.Knob("spread", &n->spread, 0.0f, 5.0f, "%.1f %%");
         row.Knob("level", &n->level, 0.0f, 2.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }
