   // Turbo 0.60: the few upstream UI helpers that ported node bodies call
   // (upstream's design components and AppShared helpers), with upstream's
   // signatures but built on Turbo's own widgets, so the bodies of nodes taken
   // from upstream (Time Machine, Fluid, Volume, NDI, L-System, Rigid Bodies,
   // FM, Partials, String, ...) can stay as Naman wrote them.

   // The frame every audio visualizer draws inside: scope background, clip,
   // border (upstream components/AudioViz.h).
   namespace AudioViz
   {
      struct Frame
      {
         ImDrawList* dl = nullptr;
         ImVec2 origin;
         ImVec2 br;
      };

      inline Frame Begin(const ImVec2& origin, float w, float h)
      {
         Frame f;
         f.dl = ImGui::GetWindowDrawList();
         f.origin = origin;
         f.br = ImVec2(origin.x + w, origin.y + h);
         f.dl->AddRectFilled(f.origin, f.br, ScopeBgCol(), 4.0f);
         f.dl->PushClipRect(f.origin, f.br, true);
         return f;
      }
      inline Frame Begin(float w, float h) { return Begin(ImGui::GetCursorScreenPos(), w, h); }
      inline void Fill(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx) { dl->AddRectFilled(mn, mx, ScopeBgCol(), 4.0f); }
      inline void Border(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 hotBorder = 0)
      {
         dl->AddRect(mn, mx, hotBorder ? hotBorder : ScopeBorderCol(), 4.0f);
      }
      inline void End(const Frame& f, ImU32 hotBorder = 0)
      {
         f.dl->PopClipRect();
         f.dl->AddRect(f.origin, f.br, hotBorder ? hotBorder : ScopeBorderCol(), 4.0f);
      }
      // A dim caption centred in the frame: "waiting for input", never "broken".
      inline void IdleLabel(const Frame& f, const char* text = "no input")
      {
         const ImVec2 sz = ImGui::CalcTextSize(text);
         const ImVec2 at((f.origin.x + f.br.x - sz.x) * 0.5f, (f.origin.y + f.br.y - sz.y) * 0.5f);
         f.dl->AddText(at, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
      }
   }

   // Upstream's tile button (components/ActionButton.h); a plain button here.
   namespace ActionButton
   {
      inline bool Draw(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f)) { return ImGui::Button(label, size); }
      inline bool Column(const char* label, float width) { return ImGui::Button(label, ImVec2(width, 0.0f)); }
   }

   // Upstream's text field well (components/FieldWell.h).
   namespace FieldWell
   {
      inline bool InputText(const char* label, char* buf, size_t size, ImGuiInputTextFlags flags = 0)
      {
         return ImGui::InputText(label, buf, size, flags);
      }
      inline bool InputText(const char* label, std::string* text, ImGuiInputTextFlags flags = 0)
      {
         return ImGui::InputText(label, text, flags);
      }
   }

   inline void EmptyBlock(const char* message, const char* hint)
   {
      ImGui::TextDisabled("%s", message);
      if (hint != nullptr && hint[0] != '\0')
         ImGui::TextDisabled("%s", hint);
   }

   inline void PushTextWrapWidth(float width) { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width); }
   inline void PopTextWrapWidth() { ImGui::PopTextWrapPos(); }

   // Upstream folds rarely used rows behind "show more" while still
   // registering them (gParamRegisterOnly). Turbo shows them all, so the fold
   // always reports open and the register-only flag stays off.
   bool gParamRegisterOnly = false;
   inline bool BeginAdvancedFold(const char* /*id*/, int /*floatCount*/, int /*colorCount*/, bool& saved)
   {
      saved = gParamRegisterOnly;
      return true;
   }

   // Upstream scopes a discrete control's hash by a loop index; Turbo already
   // counts repeated labels per node, so this is only the scope's shape.
   struct DiscreteSlotScope
   {
      explicit DiscreteSlotScope(int /*slot*/) {}
   };

   // Upstream's AudioSlider takes a position<->value taper pair (frequency
   // fields); Turbo's draws linear, so the pair is accepted and not used yet.
   template <typename ToValue, typename ToPos>
   bool AudioSlider(const char* label, float* v, float lo, float hi, const char* fmt, float width, ToValue, ToPos)
   {
      return AudioSlider(label, v, lo, hi, fmt, width);
   }

   // Defined in PortedParams.inl, called from the audio body dispatcher
   // (AudioBody.inl), which comes first.
   void DrawFMBody(GraphNode& gn, FMNode* n);
   void DrawPartialsBody(GraphNode& gn, PartialsNode* n);
   void DrawStringBody(GraphNode& gn, StringNode* n);
   bool DrawPortedNodeParams(GraphNode& gn);
   void DrawSaturatorBody(GraphNode& gn, AudioEffectNode* n);
   void DrawShapeResonatorBody(GraphNode& gn, AudioEffectNode* n);
   void DrawConvolutionReverbBody(GraphNode& gn, AudioEffectNode* n);


   inline const std::vector<std::string> kAxisXYZ = { "x", "y", "z" };

   // Upstream's ADSR curve plus fields with no section chrome (FM draws one per
   // operator) - Turbo's DrawEnvelopePanel without the section around it.
   void DrawEnvelopeFields(const char* curveId, float* attackMs, float* decayMs, float* sustain,
                           float* releaseMs, float* amount, float amountLo, float amountHi,
                           const char* amountFmt, ImU32 color, const void* /*shape*/)
   {
      const float full = gAudioContentW;
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float fieldsW = std::clamp(full * 0.58f, 230.0f, 300.0f);
      const float curveW = full - fieldsW - gap;
      const float halfW = (fieldsW - gap) * 0.5f;
      const float rowH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
      const float curveH = rowH * 3.0f - ImGui::GetStyle().ItemSpacing.y;

      const ImVec2 top = ImGui::GetCursorScreenPos();
      DrawEditableADSR(curveId, attackMs, decayMs, sustain, releaseMs, 4000.0f, curveH, color, curveW);
      const float fieldsX = top.x + curveW + gap;
      float y = top.y + (amount != nullptr ? 0.0f : rowH * 0.5f);
      if (amount != nullptr)
      {
         ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
         AudioSlider("amount", amount, amountLo, amountHi, amountFmt, fieldsW);
         y += rowH;
      }
      ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
      AudioSlider("attack", attackMs, 0.0f, 4000.0f, "%.0f ms", halfW);
      ImGui::SameLine();
      AudioSlider("decay", decayMs, 0.0f, 4000.0f, "%.0f ms", halfW);
      y += rowH;
      ImGui::SetCursorScreenPos(ImVec2(fieldsX, y));
      AudioSlider("sustain", sustain, 0.0f, 1.0f, "%.3f", halfW);
      ImGui::SameLine();
      AudioSlider("release", releaseMs, 0.0f, 4000.0f, "%.0f ms", halfW);
      ImGui::SetCursorScreenPos(ImVec2(top.x, std::max(top.y + curveH, y + rowH)));
      ImGui::Dummy(ImVec2(full, 0.0f));
   }
