// Turbo: body of every PointZ node, drawn from its pop::PopDef parameter table
// (src/pop/PopOps.h), so new PointZ operators need no UI code of their own.

   namespace
   {
      // "key=v1|v2": visible while that param's value is one of the listed numbers.
      bool PopParamVisible(const pop::PopDef& def, const pop::ParamValues& v, const pop::ParamSpec& p)
      {
         if (p.showIf.empty())
            return true;
         const size_t eq = p.showIf.find('=');
         if (eq == std::string::npos)
            return true;
         const int idx = def.ParamIndex(p.showIf.substr(0, eq));
         if (idx < 0)
            return true;
         const int off = def.FloatOffset(idx);
         const int cur = (off < (int)v.f.size()) ? (int)std::lround(v.f[(size_t)off]) : 0;
         size_t start = eq + 1;
         while (start <= p.showIf.size())
         {
            size_t bar = p.showIf.find('|', start);
            if (bar == std::string::npos)
               bar = p.showIf.size();
            if (std::atoi(p.showIf.substr(start, bar - start).c_str()) == cur)
               return true;
            start = bar + 1;
         }
         return false;
      }
   }

   void DrawPopParams(PopNode* n)
   {
      const pop::PopDef& def = n->Def();
      pop::ParamValues& v = n->Values();
      if (!n->CookError().empty())
         ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1.0f), "%s", n->CookError().substr(0, 400).c_str());
      ImGui::TextDisabled("%d points  %zu faces  %zu lines  %.2f ms", n->PointCount(), n->TriangleCount(), n->StripCount(),
                          n->LastCookMs());
      if (def.everyFrame || !def.animKey.empty())
      {
         if (ModTriggerButton("reset##popReset", ImVec2(kParamWidth, 0)))
            n->RequestReset();
      }
      int off = 0;
      for (size_t i = 0; i < def.params.size(); i++)
      {
         const pop::ParamSpec& p = def.params[i];
         const int here = off;
         off += p.Floats();
         if (!PopParamVisible(def, v, p))
            continue;
         const std::string label = p.label + "##pop_" + p.key;
         switch (p.type)
         {
            case pop::ParamSpec::kFloat:
               ModSlider(label.c_str(), &v.f[(size_t)here], p.lo, p.hi);
               break;
            case pop::ParamSpec::kInt:
            {
               int x = (int)std::lround(v.f[(size_t)here]);
               if (ModSliderInt(label.c_str(), &x, (int)p.lo, (int)p.hi))
                  v.f[(size_t)here] = (float)x;
               break;
            }
            case pop::ParamSpec::kBool:
            {
               bool b = v.f[(size_t)here] != 0.0f;
               ModCheckbox(label.c_str(), &b);
               v.f[(size_t)here] = b ? 1.0f : 0.0f;
               break;
            }
            case pop::ParamSpec::kMenu:
            {
               float* slot = &v.f[(size_t)here];
               DropdownButton(label.c_str(), p.menu, (int)std::lround(*slot), [slot](int k) {
                  PushUndoCheckpoint();
                  *slot = (float)k;
               });
               break;
            }
            case pop::ParamSpec::kColor:
               ColorSwatch(label.c_str(), &v.f[(size_t)here], n);
               break;
            case pop::ParamSpec::kVec3:
            {
               static const char* kAxis[3] = { " x", " y", " z" };
               for (int k = 0; k < 3; k++)
               {
                  const std::string l = p.label + kAxis[k] + "##pop_" + p.key + kAxis[k];
                  ModSlider(l.c_str(), &v.f[(size_t)(here + k)], p.lo, p.hi);
               }
               break;
            }
            case pop::ParamSpec::kText:
            {
               std::string& s = v.s[i];
               // Turbo 0.70: a font (Text PointZ) is picked from the installed families.
               if (p.key == "font")
               {
                  const std::vector<std::string>& families = Platform::AvailableFontFamilies();
                  int current = 0;
                  for (size_t f = 0; f < families.size(); f++)
                     if (families[f] == s)
                        current = (int)f;
                  DropdownButton(p.label.c_str(), families, current,
                                 [&s, &families](int f)
                                 {
                                    if (f >= 0 && f < (int)families.size())
                                    {
                                       PushUndoCheckpoint();
                                       s = families[(size_t)f];
                                    }
                                 });
                  break;
               }
               const bool code = p.key == "code" || p.key == "table";
               ImGui::TextDisabled("%s", p.label.c_str());
               const std::string id = "##popText_" + p.key;
               ImGui::PushItemWidth(code ? kWideNodeWidth : kParamWidth);
               if (code)
                  ImGui::InputTextMultiline(id.c_str(), &s, ImVec2(kWideNodeWidth, ImGui::GetTextLineHeight() * 8.0f));
               else
                  ImGui::InputText(id.c_str(), &s);
               if (ImGui::IsItemActivated())
                  PushUndoCheckpoint();
               ImGui::PopItemWidth();
               break;
            }
         }
      }
   }
