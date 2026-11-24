// Generic node parameter bodies, part 1 (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/components/PanelKit.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "plugin/engine/HostAudio.h"
#include "app/AppShared.h"
#include "app/ui/design/DrawParam.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/StepCell.h"
#include "app/ui/design/components/Switch.h"
#include "app/ui/design/components/EmptyState.h"
#include "platform/AppPaths.h"
#include "app/ui/design/UiFmt.h"
#include "app/ui/design/UiScope.h"

namespace app
{
   // R5: a node body's "nothing here yet" message, centred in a quiet block the width of the param column.
   static void EmptyBlock(const char* message, const char* hint)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const ImVec2 sz(kPreviewSize, tok::param_empty_block_y);
      Ui::Claim(sz);
      EmptyState::Draw(p, ImVec2(p.x + sz.x, p.y + sz.y), message, hint);
   }

   // Copies every parameter VisitParams declares, for any node type, by
   // routing through the same save/load format used for patch files: write src
   // to an in-memory param list, then read it back into dst. This used to be a
   // hand-maintained field-by-field switch covering 7 of roughly 50 node types;
   // every type it didn't cover pasted a node whose values silently stayed at
   // spawn defaults. Now paste and patch load share one source of truth, so a
   // node that round-trips through Save also round-trips through Copy.
   void CopyParams(INode* dstNode, INode* srcNode)
   {
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(srcNode, params);
      Patch::LoadParams(dstNode, params);
      ReloadDerivedState(dstNode);

      // A copy/duplicate/paste of a Predictive node must not inherit the source's exact learned
      // model verbatim - VisitParams round-trips the learned blob (profile/model/fitData) like
      // any other param, so without this a "duplicate" opens already showing "Learn Again" with
      // someone else's confidence instead of "press Learn" (bug-blast-radius review, step-10).
      // Only the per-instance learned state resets; each node's own shared cross-session
      // contribution (if any) is untouched - the source instance's material stays in house style.
      if (auto* cn = dynamic_cast<PredictiveColoringNode*>(dstNode))
         cn->ResetProfile();
      else if (auto* nn = dynamic_cast<PredictiveNotesNode*>(dstNode))
         nn->ResetLearnedState();
      else if (auto* mn = dynamic_cast<PredictiveModulatorNode*>(dstNode))
         mn->ResetLearnedState();
      else if (auto* rn = dynamic_cast<PredictiveRhythmNode*>(dstNode))
         rn->ResetLearnedState();
   }


   // ---------------- per-node parameter UI ----------------

   void DrawImageSourceParams(ImageSourceNode* n)
   {
      if (ActionButton::Draw("Choose image...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         // wrap pos is window-relative; passing a bare width put it left of the
         // cursor and wrapped every single character onto its own line
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->LastError().c_str());
         PopTextWrapWidth();
      }
      else if (!n->LoadedPath().empty())
      {
         const std::string file = AppPaths::FsPath(n->LoadedPath()).filename().u8string();
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         PopTextWrapWidth();
      }
   }


   namespace
   {
      using SlideshowParamsRow = params::Row<SlideshowNode>;
      const SlideshowParamsRow kSlideshowParamsRows[] = {
         SlideshowParamsRow::Choice<&SlideshowNode::transition>("transition", &SlideshowNode::TransitionNames),
         SlideshowParamsRow::Slider<&SlideshowNode::holdDuration>("hold", params::Slideshow::hold_duration_lo, params::Slideshow::hold_duration_hi).Fmt("%.2f s"),
         SlideshowParamsRow::Slider<&SlideshowNode::transitionDuration>("transition time", params::Slideshow::transition_duration_lo, params::Slideshow::transition_duration_hi).Fmt("%.2f s"),
         SlideshowParamsRow::Choice<&SlideshowNode::fitMode>("fit", &SlideshowNode::FitModeNames),
         SlideshowParamsRow::Slider<&SlideshowNode::width>("width", params::Slideshow::width_lo, params::Slideshow::width_hi).Fmt("%.0f"),
         SlideshowParamsRow::Slider<&SlideshowNode::height>("height", params::Slideshow::height_lo, params::Slideshow::height_hi).Fmt("%.0f"),
      };
   }

   void DrawSlideshowParams(SlideshowNode* n)
   {
      if (ActionButton::Draw("Choose folder...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->FolderPath().empty())
      {
         if (ActionButton::Draw("Refresh", ImVec2(kPreviewSize, 0)))
            n->ReloadFromFolder();
      }

      Ui::DrawParams(n, kSlideshowParamsRows);

      PushTextWrapWidth(kPreviewSize);
      if (!n->LastError().empty())
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->LastError().c_str());
      if (n->ImageCount() > 0)
      {
         const std::string file = n->CurrentFileName();
         ImGui::TextDisabled("%d/%d  %s", n->CurrentImageNumber(), n->ImageCount(), file.c_str());
      }
      PopTextWrapWidth();
      if (n->ImageCount() == 0)
         EmptyBlock("No images", "Choose a folder containing images");
   }


   void DrawSyphonOutParams(SyphonOutNode* n)
   {
#if !defined(__APPLE__) && !defined(_WIN32)
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("Syphon/Spout texture sharing is unavailable on Linux.");
      PopTextWrapWidth();
      return;
#else
      ImGui::SetNextItemWidth(kPreviewSize);
      if (FieldWell::InputText("##syphon_name", &n->serverNameInput, ImGuiInputTextFlags_EnterReturnsTrue))
      {
         n->SetServerName(n->serverNameInput);
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
         n->SetServerName(n->serverNameInput);
      }

      PushTextWrapWidth(kPreviewSize);
      if (n->PublishedWidth() > 0 && n->PublishedHeight() > 0)
      {
         ImGui::TextColored(tok::RoleV4(tok::col::text_ok), "Broadcasting: %dx%d", n->PublishedWidth(), n->PublishedHeight());
         if (!Platform::SyphonServerCanReportClients())
            ImGui::TextDisabled("Clients: not reported");
         else if (n->HasClients())
            ImGui::TextColored(tok::RoleV4(tok::col::accent_alt_vivid_warm_hi), "Clients: Active");
         else
            ImGui::TextDisabled("Clients: Waiting for app...");
      }
      else
      {
         ImGui::TextDisabled("Connect an image input to publish");
      }
      PopTextWrapWidth();
#endif
   }


   void DrawSyphonInParams(SyphonInNode* n)
   {
#if !defined(__APPLE__) && !defined(_WIN32)
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("Syphon/Spout texture sharing is unavailable on Linux.");
      PopTextWrapWidth();
      return;
#else
      if (ActionButton::Draw("Refresh Servers", ImVec2(kPreviewSize, 0)))
      {
         n->RefreshServers();
      }

      const auto& servers = n->AvailableServers();
      if (servers.empty())
      {
#if defined(_WIN32)
         EmptyBlock("No active Spout senders", "Start a sender, then Refresh");
#else
         EmptyBlock("No active Syphon servers", "Start a server, then Refresh");
#endif
      }
      else
      {
         std::vector<std::string> serverNames;
         serverNames.reserve(servers.size());
         for (const auto& s : servers)
         {
            std::string label = s.appName.empty() ? "App" : s.appName;
            if (!s.serverName.empty())
               label += " (" + s.serverName + ")";
            serverNames.push_back(label);
         }

         int currentIdx = n->SelectedServerIndex();
         if (currentIdx < 0) currentIdx = 0;
         DropdownButton("##syphon_server", serverNames, currentIdx, [n](int idx) {
            n->SelectServer(idx);
         }, kPreviewSize);

         PushTextWrapWidth(kPreviewSize);
         if (n->IsConnected())
         {
            ImGui::TextColored(tok::RoleV4(tok::col::text_ok), "Receiving: %dx%d", n->GetOutputWidth(), n->GetOutputHeight());
         }
         else
         {
            ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "Connecting...");
         }
         PopTextWrapWidth();
      }
#endif
   }


   void DrawNdiOutParams(NdiOutNode* n)
   {
      if (!Ndi::Available())
      {
         EmptyBlock("NDI runtime not found", "Install it from ndi.video");
         return;
      }

      ImGui::SetNextItemWidth(kPreviewSize);
      if (FieldWell::InputText("##ndi_name", &n->sourceNameInput, ImGuiInputTextFlags_EnterReturnsTrue))
         n->SetSourceName(n->sourceNameInput);
      if (ImGui::IsItemDeactivatedAfterEdit())
         n->SetSourceName(n->sourceNameInput);

      PushTextWrapWidth(kPreviewSize);
      if (n->IsPublishing())
      {
         ImGui::TextColored(tok::RoleV4(tok::col::text_ok), "Broadcasting: %dx%d", n->PublishedWidth(), n->PublishedHeight());
         if (n->Connections() > 0)
            ImGui::TextColored(tok::RoleV4(tok::col::accent_alt_vivid_warm_hi), "Receivers: %d", n->Connections());
         else
            ImGui::TextDisabled("Receivers: waiting...");
      }
      else
         ImGui::TextDisabled("Connect an image input to publish");
      PopTextWrapWidth();
   }


   void DrawNdiInParams(NdiInNode* n)
   {
      PushTextWrapWidth(kPreviewSize);
      if (!Ndi::Available())
      {
         ImGui::TextDisabled("NDI runtime not found (install from ndi.video)");
         PopTextWrapWidth();
         return;
      }
      PopTextWrapWidth();

      const std::vector<std::string> sources = n->AvailableSources();
      if (sources.empty())
      {
         EmptyBlock("No NDI sources", "Sources on this network appear here");
         return;
      }

      int currentIdx = 0;
      for (size_t i = 0; i < sources.size(); i++)
         if (sources[i] == n->SourceName())
            currentIdx = (int)i;
      std::vector<std::string> labels;
      labels.reserve(sources.size());
      for (const std::string& s : sources)
         labels.push_back(s);
      DropdownButton("##ndi_source", labels, currentIdx, [n, sources](int idx) {
         n->SelectSource(sources[(size_t)idx]);
      }, kPreviewSize);

      PushTextWrapWidth(kPreviewSize);
      if (n->SourceName().empty())
         ImGui::TextDisabled("Pick a source");
      else if (n->IsReceiving())
         ImGui::TextColored(tok::RoleV4(tok::col::text_ok), "Receiving: %dx%d", n->GetOutputWidth(), n->GetOutputHeight());
      else
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "Waiting for frames...");
      PopTextWrapWidth();
   }


   namespace
   {
      using OscReceiveParamsRow = params::Row<OscReceiveNode>;
      const OscReceiveParamsRow kOscReceiveParamsRows[] = {
         OscReceiveParamsRow::Slider<&OscReceiveNode::low>("low", 0.0f, 1.0f),
         OscReceiveParamsRow::Slider<&OscReceiveNode::high>("high", 0.0f, 1.0f),
      };
   }

   void DrawOscReceiveParams(OscReceiveNode* n)
   {
      Ui::DrawParam(n, OscReceiveParamsRow::SliderInt<&OscReceiveNode::port>("port", params::OscReceive::port_lo, params::OscReceive::port_hi));
      ImGui::SetNextItemWidth(kParamWidth);
      FieldWell::InputText("address", &n->address);
      Ui::DrawParams(n, kOscReceiveParamsRows);
   }


   namespace
   {
      using OscSendParamsRow = params::Row<OscSendNode>;
      const OscSendParamsRow kOscSendParamsRows[] = {
         OscSendParamsRow::Slider<&OscSendNode::epsilon>("epsilon", params::OscSend::epsilon_lo, params::OscSend::epsilon_hi).Fmt("%.4f"),
         OscSendParamsRow::Slider<&OscSendNode::intervalMs>("interval (ms)", params::OscSend::interval_ms_lo, params::OscSend::interval_ms_hi).Fmt("%.0f"),
      };
   }

   void DrawOscSendParams(OscSendNode* n)
   {
      ImGui::SetNextItemWidth(kParamWidth);
      FieldWell::InputText("host", &n->host);
      Ui::DrawParam(n, OscSendParamsRow::SliderInt<&OscSendNode::port>("port", params::OscSend::port_lo, params::OscSend::port_hi));
      ImGui::SetNextItemWidth(kParamWidth);
      FieldWell::InputText("address", &n->address);
      Ui::DrawParams(n, kOscSendParamsRows);
      if (n->LastSent() < 0.0f)
         ImGui::TextDisabled("not sent yet");
      else
         ImGui::TextDisabled("last sent: %s", UiFmt::F3(n->LastSent()).c);
   }


   namespace
   {
      using EnvironmentParamsRow = params::Row<EnvironmentNode>;
      const EnvironmentParamsRow kEnvironmentParamsRows1[] = {
         EnvironmentParamsRow::Slider<&EnvironmentNode::skyElevation>("sun elevation", params::Environment::sky_elevation_lo, params::Environment::sky_elevation_hi).Fmt("%.1f\xC2\xB0"),
         EnvironmentParamsRow::Slider<&EnvironmentNode::skyAzimuth>("sun azimuth", params::Environment::sky_azimuth_lo, params::Environment::sky_azimuth_hi).Fmt("%.1f\xC2\xB0"),
         EnvironmentParamsRow::Slider<&EnvironmentNode::turbidity>("turbidity", params::Environment::turbidity_lo, params::Environment::turbidity_hi),
         EnvironmentParamsRow::Slider<&EnvironmentNode::intensity>("intensity", params::Environment::intensity_lo, params::Environment::intensity_hi),
         EnvironmentParamsRow::Slider<&EnvironmentNode::rotation>("rotation", params::Environment::rotation_lo, params::Environment::rotation_hi).Fmt("%.1f\xC2\xB0"),
      };
      const EnvironmentParamsRow kEnvironmentParamsRows2[] = {
         EnvironmentParamsRow::Slider<&EnvironmentNode::intensity>("intensity", params::Environment::intensity_lo, params::Environment::intensity_hi),
         EnvironmentParamsRow::Slider<&EnvironmentNode::rotation>("rotation", params::Environment::rotation_lo, params::Environment::rotation_hi).Fmt("%.1f\xC2\xB0"),
         // Skewed so half the travel covers 0..5, where most skies' suns are cut; the rest reaches 100.
         EnvironmentParamsRow::Slider<&EnvironmentNode::sunThreshold>("sun threshold", params::Environment::sun_threshold_lo, params::Environment::sun_threshold_hi).Curve([](float p, float lo, float hi) { return SkewTaperMath::PosToValue(p, lo, hi, 5.0f); }, [](float v, float lo, float hi) { return SkewTaperMath::ValueToPos(v, lo, hi, 5.0f); }),
      };
   }

   void DrawEnvironmentParams(EnvironmentNode* n)
   {
      static const std::vector<std::string> kSources = { "Image", "Physical sky" };
      DropdownButton("source", kSources, n->source, [n](int i) { n->source = i; }, kParamWidth);
      if (n->source == 1)
      {
         Ui::DrawParams(n, kEnvironmentParamsRows1);
         return;
      }
      if (ActionButton::Draw("Choose HDRI...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->LastError().c_str());
         PopTextWrapWidth();
      }
      else if (!n->LoadedPath().empty())
      {
         const std::string file = AppPaths::FsPath(n->LoadedPath()).filename().u8string();
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         PopTextWrapWidth();
      }
      else
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("no image: Render 3D uses its procedural sky");
         PopTextWrapWidth();
      }

      Ui::DrawParams(n, kEnvironmentParamsRows2);
   }


   // Not every shape uses corner/sides/inner ratio - e.g. a Circle has no corners and no sides, a Triangle's side
   // count is fixed at 3. Those rows show only for the shapes that use them. Keep in sync with the uShape switch in
   // ShapeNode.cpp's fragment shader. The three take fixed ordinals above the always-drawn ones (0..10), so switching
   // shape never moves rotation / pos / fill and the type-level key join stays true.
   namespace
   {
      using ShapeRow = params::Row<ShapeNode>;
      bool ShapeUsesCorner(const ShapeNode& n) { return n.shapeType == 3 || n.shapeType == 7 || n.shapeType == 9 || n.shapeType == 18; }
      bool ShapeUsesSides(const ShapeNode& n) { return n.shapeType == 5 || n.shapeType == 6 || n.shapeType == 14 || n.shapeType == 19; }
      bool ShapeUsesInner(const ShapeNode& n) { return n.shapeType == 6 || n.shapeType == 14 || n.shapeType == 15 || n.shapeType == 16; }

      const ShapeRow kShapeRows[] = {
         ShapeRow::Choice<&ShapeNode::shapeType>("shape", &ShapeNode::ShapeNames),
         ShapeRow::Slider<&ShapeNode::width>("width", params::Shape::width_lo, params::Shape::width_hi).Fmt("%.0f"),
         ShapeRow::Slider<&ShapeNode::height>("height", params::Shape::height_lo, params::Shape::height_hi).Fmt("%.0f"),
         ShapeRow::Slider<&ShapeNode::sizeX>("size x", params::Shape::size_x_lo, params::Shape::size_x_hi),
         ShapeRow::Slider<&ShapeNode::sizeY>("size y", params::Shape::size_y_lo, params::Shape::size_y_hi),
         ShapeRow::Slider<&ShapeNode::cornerRadius>("corner/thick", params::Shape::corner_radius_lo, params::Shape::corner_radius_hi).At(11).If(ShapeUsesCorner),
         ShapeRow::SliderInt<&ShapeNode::sides>("sides", params::Shape::sides_lo, params::Shape::sides_hi).At(12).If(ShapeUsesSides),
         ShapeRow::Slider<&ShapeNode::innerRatio>("inner ratio", params::Shape::inner_ratio_lo, params::Shape::inner_ratio_hi).At(13).If(ShapeUsesInner),
         ShapeRow::Slider<&ShapeNode::rotation>("rotation", params::Shape::rotation_lo, params::Shape::rotation_hi).Fmt("%.1f\xC2\xB0"),
         ShapeRow::Slider<&ShapeNode::posX>("pos x", 0.0f, 1.0f),
         ShapeRow::Slider<&ShapeNode::posY>("pos y", 0.0f, 1.0f),
         ShapeRow::Colour<&ShapeNode::fillColor>("fill"),
         ShapeRow::Slider<&ShapeNode::fillOpacity>("fill opacity", 0.0f, 1.0f),
         ShapeRow::Slider<&ShapeNode::strokeWidth>("stroke width", params::Shape::stroke_width_lo, params::Shape::stroke_width_hi),
         ShapeRow::Colour<&ShapeNode::strokeColor>("stroke"),
         ShapeRow::Slider<&ShapeNode::feather>("feather", params::Shape::feather_lo, params::Shape::feather_hi),
         ShapeRow::Colour<&ShapeNode::bgColor>("bg"),
         ShapeRow::Slider<&ShapeNode::bgOpacity>("bg opacity", 0.0f, 1.0f),
      };
   }

   void DrawShapeParams(ShapeNode* n)
   {
      Ui::DrawParams(n, kShapeRows);
   }


   namespace
   {
      using FormulaParamsRow = params::Row<FormulaNode>;
      const FormulaParamsRow kFormulaParamsRows1[] = {
         FormulaParamsRow::Slider<&FormulaNode::width>("width", params::Formula::width_lo, params::Formula::width_hi).Fmt("%.0f"),
         FormulaParamsRow::Slider<&FormulaNode::height>("height", params::Formula::height_lo, params::Formula::height_hi).Fmt("%.0f"),
      };
      const FormulaParamsRow kFormulaParamsRows2[] = {
         FormulaParamsRow::Slider<&FormulaNode::knobA>("uA", 0.0f, 1.0f),
         FormulaParamsRow::Slider<&FormulaNode::knobB>("uB", 0.0f, 1.0f),
         FormulaParamsRow::Slider<&FormulaNode::knobC>("uC", 0.0f, 1.0f),
         FormulaParamsRow::Slider<&FormulaNode::knobD>("uD", 0.0f, 1.0f),
      };
   }

   void DrawFormulaParams(FormulaNode* n)
   {
      // The GLSL editor lives in its own window, not inline: ImGui multi-line
      // fields are child windows, and child windows inside the node canvas get
      // clipped away - which is why the box used to render empty.
      //
      // Field build step 17: this dropdown now folds the factory presets and
      // the user's saved .infdev devices into one control (plan §6), rather
      // than a bare preset picker - FormulaNode's device.params stays empty
      // (plan §7), only `formula` round-trips.
      // The Shadertoy node: paste a shader from shadertoy.com as it is (mainImage, its
      // buffers, iChannel pins); its `uniform float x; // lo..hi` lines become knobs.
      // Shadertoy is its own node (ShadertoyNode), so there is no mode picker here.
      if (n->IsShadertoy())
         DropdownButton("buffers", FormulaNode::BufferNames(), n->buffers, [n](int i) { n->buffers = i; n->Apply(); });
      else
         DrawFieldDeviceControls<FormulaNode>(n, "formula", &FormulaNode::PresetNames(),
                                             [](FormulaNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

      if (ActionButton::Draw(n->IsShadertoy() ? "Edit shader..." : "Edit GLSL...", ImVec2(kPreviewSize, 0)))
      {
         gFormulaEditor = n;
         gFormulaEditorOpen = true;
      }

      if (!n->LastError().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->LastError().c_str());
         PopTextWrapWidth();
      }

      Ui::DrawParams(n, kFormulaParamsRows1);
      if (n->IsShadertoy())
      {
         const std::vector<FormulaNode::Knob>& knobs = n->Knobs();
         for (int i = 0; i < (int)knobs.size(); i++)
            ModSlider(knobs[(size_t)i].name.c_str(), &n->stKnob[i], knobs[(size_t)i].lo, knobs[(size_t)i].hi);
      }
      else
      {
         Ui::DrawParams(n, kFormulaParamsRows2);
      }
      Ui::DrawParam(n, FormulaParamsRow::Toggle<&FormulaNode::animate>("animate"));
   }


   namespace
   {
      using TextParamsRow = params::Row<TextNode>;
      const TextParamsRow kTextParamsRows1[] = {
         TextParamsRow::Slider<&TextNode::fontSize>("size", params::Text::font_size_lo, params::Text::font_size_hi),
         TextParamsRow::Colour<&TextNode::color>("colour"),
         TextParamsRow::Slider<&TextNode::tracking>("tracking", params::Text::tracking_lo, params::Text::tracking_hi),
         TextParamsRow::Slider<&TextNode::posX>("pos x", 0.0f, 1.0f),
         TextParamsRow::Slider<&TextNode::posY>("pos y", 0.0f, 1.0f),
      };
      const TextParamsRow kTextParamsRows2[] = {
         TextParamsRow::Slider<&TextNode::scaleX>("scale x", params::Text::scale_x_lo, params::Text::scale_x_hi),
         TextParamsRow::Slider<&TextNode::scaleY>("scale y", params::Text::scale_y_lo, params::Text::scale_y_hi),
         TextParamsRow::Toggle<&TextNode::wordWrap>("word wrap"),
      };
      const TextParamsRow kTextParamsRows3[] = {
         TextParamsRow::Slider<&TextNode::wrapWidth>("box width", params::Text::wrap_width_lo, params::Text::wrap_width_hi),
         TextParamsRow::Slider<&TextNode::wrapHeight>("box height", params::Text::wrap_height_lo, params::Text::wrap_height_hi),
         TextParamsRow::Toggle<&TextNode::fitToBox>("fit text to box"),
      };
      const TextParamsRow kTextParamsRows4[] = {
         TextParamsRow::Colour<&TextNode::outlineColor>("outline colour"),
         TextParamsRow::Toggle<&TextNode::outlineOnly>("outline only"),
      };
      const TextParamsRow kTextParamsRows5[] = {
         TextParamsRow::Slider<&TextNode::width>("width", params::Text::width_lo, params::Text::width_hi).Fmt("%.0f"),
         TextParamsRow::Slider<&TextNode::height>("height", params::Text::height_lo, params::Text::height_hi).Fmt("%.0f"),
      };
   }

   void DrawTextParams(TextNode* n)
   {
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", n->text.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (FieldWell::InputText("##text", buf, sizeof(buf)))
         n->text = buf;

      const std::vector<std::string>& fonts = TextNode::AvailableFonts();
      if (n->fontName.empty())
      {
         n->fontName = fonts.front();
         for (const char* sans : { "Helvetica Neue", "Helvetica", "Arial", "Segoe UI", "Inter" })
         {
            if (std::find(fonts.begin(), fonts.end(), sans) != fonts.end())
            {
               n->fontName = sans;
               break;
            }
         }
      }
      int fontIdx = 0;
      for (int i = 0; i < (int)fonts.size(); i++)
      {
         if (fonts[i] == n->fontName)
            fontIdx = i;
      }
      DropdownButton("font", fonts, fontIdx, [n, &fonts](int i) { n->fontName = fonts[i]; });

      Ui::DrawParams(n, kTextParamsRows1);
      DropdownButton("align", AlignOptions(), n->align, [n](int i) { n->align = i; });
      Ui::DrawParams(n, kTextParamsRows2);
      if (n->wordWrap)
      {
         Ui::DrawParams(n, kTextParamsRows3);
         if (n->fitToBox)
            ImGui::TextDisabled("size is a maximum - fitted to %s pt", UiFmt::F0(n->FittedSize()).c);
         Ui::DrawParam(n, TextParamsRow::Slider<&TextNode::lineSpacing>("line spacing", params::Text::line_spacing_lo, params::Text::line_spacing_hi));
      }
      Ui::DrawParam(n, TextParamsRow::Slider<&TextNode::outlineWidth>("outline", params::Text::outline_width_lo, params::Text::outline_width_hi));
      if (n->outlineWidth > 0.0f)
      {
         Ui::DrawParams(n, kTextParamsRows4);
      }
      Ui::DrawParams(n, kTextParamsRows5);
   }


   namespace
   {
      using VideoParamsRow = params::Row<VideoSourceNode>;
      const VideoParamsRow kVideoParamsRows1[] = {
         VideoParamsRow::Toggle<&VideoSourceNode::loop>("loop"),
         VideoParamsRow::Slider<&VideoSourceNode::speed>("speed", params::VideoSource::speed_lo, params::VideoSource::speed_hi),
      };
      const VideoParamsRow kVideoParamsRows2[] = {
         VideoParamsRow::Toggle<&VideoSourceNode::audioEnabled>("audioEnabled"),
         VideoParamsRow::Slider<&VideoSourceNode::volume>("volume", 0.0f, 1.0f),
      };
   }

   void DrawVideoParams(VideoSourceNode* n)
   {
      if (ActionButton::Draw("Choose video...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      if (!n->LastError().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->LastError().c_str());
         PopTextWrapWidth();
      }
      else if (!n->LoadedPath().empty())
      {
         const std::string file = AppPaths::FsPath(n->LoadedPath()).filename().u8string();
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         PopTextWrapWidth();
         ImGui::TextDisabled("%dx%d  %s / %s", n->GetOutputWidth(), n->GetOutputHeight(),
                             UiFmt::Sec(n->Position()).c, UiFmt::Sec(n->Duration()).c);
      }
      Ui::DrawParams(n, kVideoParamsRows1);
      {
         double dur = n->Duration();
         float sliderMax = (dur > 0.0) ? (float)dur : 1.0f;
         ModSlider("start", &n->trimStart, 0.0f, sliderMax);
         ModSlider("end", &n->trimEnd, 0.0f, sliderMax);
      }

      if (n->HasAudio())
      {
         NodeSeparator();
         Ui::DrawParams(n, kVideoParamsRows2);
      }
      else if (!n->AudioError().empty())
      {
         NodeSeparator();
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("audio: %s", n->AudioError().c_str());
         PopTextWrapWidth();
      }
   }


   namespace
   {
      using VideoInParamsRow = params::Row<VideoInNode>;
      const VideoInParamsRow kVideoInParamsRows[] = {
         VideoInParamsRow::Toggle<&VideoInNode::active>("active"),
         VideoInParamsRow::Toggle<&VideoInNode::mirror>("mirror"),
      };
   }

   void DrawVideoInParams(VideoInNode* n)
   {
      gParamWidthLive = std::max(gParamWidthLive, kPreviewSize);   // dropdowns as wide as the preview above them
      Ui::DrawParams(n, kVideoInParamsRows);

      n->RefreshDevices();
      const auto& devices = n->AvailableDevices();
      if (devices.empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("No camera devices found.");
         PopTextWrapWidth();
      }
      else
      {
         std::vector<std::string> deviceNames;
         deviceNames.reserve(devices.size());
         int currentDevIdx = 0;
         for (size_t i = 0; i < devices.size(); i++)
         {
            std::string name = devices[i].localizedName;
            if (devices[i].isDefault)
               name += " (Default)";
            deviceNames.push_back(name);
            if (n->deviceId == devices[i].uniqueId || (n->deviceId.empty() && devices[i].isDefault))
               currentDevIdx = (int)i;
         }

         DropdownButton("device", deviceNames, currentDevIdx, [n, &devices](int idx) {
            if (idx >= 0 && idx < (int)devices.size())
               n->deviceId = devices[idx].uniqueId;
         });
      }

      Ui::DrawParam(n, VideoInParamsRow::Choice<&VideoInNode::resolution>("res", &VideoInNode::ResolutionNames));

      if (!n->LastError().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->LastError().c_str());
         PopTextWrapWidth();
      }
      else if (n->IsRunning() && n->GetOutputWidth() > 0 && n->GetOutputHeight() > 0)
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("%dx%d", n->GetOutputWidth(), n->GetOutputHeight());
         PopTextWrapWidth();
      }
   }


   namespace
   {
      using FitParamsRow = params::Row<FitNode>;
      const FitParamsRow kFitParamsRows1[] = {
         FitParamsRow::Choice<&FitNode::mode>("mode", &FitNode::ModeNames),
         FitParamsRow::Toggle<&FitNode::matchInput>("match input res"),
      };
      const FitParamsRow kFitParamsRows2[] = {
         FitParamsRow::Slider<&FitNode::width>("width", params::Fit::width_lo, params::Fit::width_hi).Fmt("%.0f"),
         FitParamsRow::Slider<&FitNode::height>("height", params::Fit::height_lo, params::Fit::height_hi).Fmt("%.0f"),
      };
      const FitParamsRow kFitParamsRows3[] = {
         FitParamsRow::Slider<&FitNode::offsetX>("offset X", params::Fit::offset_x_lo, params::Fit::offset_x_hi).Fmt("%.0f px"),
         FitParamsRow::Slider<&FitNode::offsetY>("offset Y", params::Fit::offset_y_lo, params::Fit::offset_y_hi).Fmt("%.0f px"),
         FitParamsRow::Colour<&FitNode::bgColor>("bg"),
         FitParamsRow::Slider<&FitNode::bgOpacity>("bg opacity", 0.0f, 1.0f),
      };
   }

   void DrawFitParams(FitNode* n)
   {
      Ui::DrawParams(n, kFitParamsRows1);
      if (!n->matchInput)
      {
         Ui::DrawParams(n, kFitParamsRows2);
      }
      Ui::DrawParams(n, kFitParamsRows3);
   }


   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   void DrawProjectionHandleOverlay(ProjectionNode* node, ImVec2 origin, ImVec2 imageSize, const char* btnIdSuffix)
   {
      ImGui::SetCursorScreenPos(origin);
      UiInteract::Hit(btnIdSuffix, imageSize);

      static int sDragRow = -1;
      static int sDragCol = -1;
      static ProjectionNode* sDragNode = nullptr;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      ImDrawList* dl = ImGui::GetWindowDrawList();

      auto toScreen = [&](float nx, float ny) -> ImVec2 {
         return ImVec2(origin.x + nx * imageSize.x, origin.y + ny * imageSize.y);
      };

      const bool isCornerPin = (node->mode == (int)ProjectionNode::WarpMode::CornerPin);
      const int gw = isCornerPin ? 2 : std::max(2, std::min(8, node->gridW));
      const int gh = isCornerPin ? 2 : std::max(2, std::min(8, node->gridH));

      // Handle point selection & dragging
      if (ImGui::IsItemActive())
      {
         if (sDragNode != node || sDragRow < 0 || sDragCol < 0)
         {
            float nearestDist = 32.0f;
            int bestR = -1, bestC = -1;
            for (int r = 0; r < gh; ++r)
            {
               for (int c = 0; c < gw; ++c)
               {
                  ImVec2 pt = toScreen(node->points[r][c].x, node->points[r][c].y);
                  float d = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
                  if (d < nearestDist)
                  {
                     nearestDist = d;
                     bestR = r;
                     bestC = c;
                  }
               }
            }
            if (bestR >= 0 && bestC >= 0)
            {
               sDragNode = node;
               sDragRow = bestR;
               sDragCol = bestC;
            }
         }

         if (sDragNode == node && sDragRow >= 0 && sDragCol >= 0)
         {
            float nx = (mouse.x - origin.x) / std::max(1.0f, imageSize.x);
            float ny = (mouse.y - origin.y) / std::max(1.0f, imageSize.y);
            node->points[sDragRow][sDragCol].x = std::max(0.0f, std::min(1.0f, nx));
            node->points[sDragRow][sDragCol].y = std::max(0.0f, std::min(1.0f, ny));
         }
      }
      else if (ImGui::IsItemDeactivated())
      {
         PushUndoCheckpoint();
         sDragNode = nullptr;
         sDragRow = -1;
         sDragCol = -1;
      }

      // Draw connecting wireframe
      for (int r = 0; r < gh; ++r)
      {
         for (int c = 0; c < gw - 1; ++c)
         {
            ImVec2 pA = toScreen(node->points[r][c].x, node->points[r][c].y);
            ImVec2 pB = toScreen(node->points[r][c + 1].x, node->points[r][c + 1].y);
            dl->AddLine(pA, pB, tok::Role(tok::col::role_modulation, tok::param_line_alpha_md), tok::param_projection_handle_overlay_stroke);
         }
      }
      for (int c = 0; c < gw; ++c)
      {
         for (int r = 0; r < gh - 1; ++r)
         {
            ImVec2 pA = toScreen(node->points[r][c].x, node->points[r][c].y);
            ImVec2 pB = toScreen(node->points[r + 1][c].x, node->points[r + 1][c].y);
            dl->AddLine(pA, pB, tok::Role(tok::col::role_modulation, tok::param_line_alpha_md), tok::param_projection_handle_overlay_stroke);
         }
      }

      // Draw corner diagonals in corner pin mode for visual alignment
      if (isCornerPin)
      {
         ImVec2 pTL = toScreen(node->points[0][0].x, node->points[0][0].y);
         ImVec2 pTR = toScreen(node->points[0][1].x, node->points[0][1].y);
         ImVec2 pBR = toScreen(node->points[1][1].x, node->points[1][1].y);
         ImVec2 pBL = toScreen(node->points[1][0].x, node->points[1][0].y);
         dl->AddLine(pTL, pBR, tok::Role(tok::col::role_modulation, tok::param_line_alpha_xs), tok::param_line_stroke_xs);
         dl->AddLine(pTR, pBL, tok::Role(tok::col::role_modulation, tok::param_line_alpha_xs), tok::param_line_stroke_xs);
      }

      // Draw circular pin handles
      for (int r = 0; r < gh; ++r)
      {
         for (int c = 0; c < gw; ++c)
         {
            ImVec2 pt = toScreen(node->points[r][c].x, node->points[r][c].y);
            const bool isDragged = (sDragNode == node && sDragRow == r && sDragCol == c);
            float distToMouse = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
            const bool isHovered = (distToMouse < 18.0f);

            dl->AddCircleFilled(pt, tok::param_projection_handle_overlay_r, tok::Role(tok::col::role_modulation));
            dl->AddCircle(pt, tok::param_ring_r_md, tok::Role(tok::col::surface_deep), 0, tok::param_projection_handle_overlay_stroke);
            if (isDragged || isHovered)
               dl->AddCircle(pt, tok::param_ring_r_lg, tok::Role(tok::col::ink_white, tok::param_ring_alpha), 0, tok::param_ring_stroke_lg);
         }
      }
   }
   // ui-content-end


   // ui-content-begin: node preview: the node's output picture
   void DrawProjectionPreview(ProjectionNode* node)
   {
      const float size = kViewportSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // Checkerboard backdrop
      DrawCheckerboardBackdrop(dl, origin, size);

      if (node->GetOutputTexture() != 0)
      {
         Thumb::Add(dl, (ImTextureID)(intptr_t)node->GetOutputTexture(), origin,
                      ImVec2(origin.x + size, origin.y + size), ImVec2(0, 1), ImVec2(1, 0));
      }

      char btnId[32];
      snprintf(btnId, sizeof(btnId), "##projinlinenode%p", (void*)node);
      DrawProjectionHandleOverlay(node, origin, ImVec2(size, size), btnId);
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size), tok::Role(tok::col::accent_muted_hi), tok::param_projection_preview_radius, 0, tok::param_projection_preview_stroke);

      // Reserve space so the eye toggle and params below aren't drawn over the preview!
      ImGui::SetCursorScreenPos(origin);
      Ui::Claim(ImVec2(size, size));
   }
   // ui-content-end


   void DrawProjectionParams(ProjectionNode* n)
   {
      const float colW = kViewportSize;
      static const std::vector<std::string> kModes = { "Corner Pin (Perspective)", "Mesh Grid" };
      DropdownButton("mode", kModes, n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; }, colW);
      DropdownButton("pattern", ProjectionNode::PatternNames(), n->patternMode,
                     [n](int i) { PushUndoCheckpoint(); n->patternMode = i; }, colW);

      // Through a temporary so the checkpoint is taken before the node
      // changes: ModCheckbox flips its bool in place, and checkpointing after
      // it made Undo restore the already-flipped value.
      bool matchInput = n->matchInput;
      bool matchInputUserChanged = false;
      if (ModCheckbox("match input res", &matchInput, &matchInputUserChanged))
      {
         if (matchInputUserChanged) // a cable flip writes the value but never checkpoints
            PushUndoCheckpoint();
         n->matchInput = matchInput;
      }

      if (!n->matchInput)
      {
         ModSlider("width", &n->width, params::Projection::width_lo, params::Projection::width_hi, "%.0f", colW);
         ModSlider("height", &n->height, params::Projection::height_lo, params::Projection::height_hi, "%.0f", colW);
      }

      if (n->mode == 1) // Mesh Grid
      {
         // No checkpoint here: ModSlider already pushes one on activation,
         // before the drag changes anything. The IsItemDeactivatedAfterEdit
         // checkpoint that used to follow each slider captured the grid AFTER
         // the drag, so the first Undo restored the edited size (a no-op).
         int gw = n->gridW;
         if (ModSliderInt("grid X", &gw, params::param::gw_lo, params::param::gw_hi, params::param::gw_def))
            n->SetGridSize(gw, n->gridH);

         Ui::SameRow(Ui::Space::S4);
         int gh = n->gridH;
         if (ModSliderInt("grid Y", &gh, params::param::gh_lo, params::param::gh_hi, params::param::gh_def))
            n->SetGridSize(n->gridW, gh);
      }

      const float btnW = ActionButton::RowWidth(tok::param_btn_w_row_width_n, kParamWidth);
      ActionButton::RowBegin();
      if (ActionButton::Draw("Reset Corners", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->ResetCorners();
      }
      ActionButton::RowNext();
      if (ActionButton::Draw("Reset All", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->ResetAllPoints();
      }
      ActionButton::RowNext();
      if (ActionButton::Draw("Flip H", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->FlipH();
      }
      ActionButton::RowNext();
      if (ActionButton::Draw("Flip V", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->FlipV();
      }
      ActionButton::RowEnd();
   }


   // Registers a float param without drawing it, at a fixed ordinal, so a
   // binding on a control the current mode hides stays alive (and comes back
   // when the mode does) and the ParamRef.key join still finds the member.
   // Same trick as DrawRateModeControls in NoteBodies.
   void RegisterHiddenParam(float* value, float minV, float maxV, const char* name, int ordinal)
   {
      ParamRef slot;
      slot.nodeIndex = gCurrentNodeIndex;
      slot.paramIndex = ordinal;
      slot.value = value;
      slot.minValue = minV;
      slot.maxValue = maxV;
      slot.name = name;
      Modulation::Instance().RegisterParam(slot);
   }


   // LFO / Random rate: "rate in" picks beats (free float, the legacy
   // control), a MusicTime division (exact, same menu as the note nodes) or
   // Hz. The beats slider keeps ordinal 0 in every mode (hidden when not
   // drawn); Hz takes the fixed ordinal `hzOrdinal`, above the body's count.
   // `names` lists the modes the node offers; the first three are always
   // LFONode's (beats, synced, Hz). MSEG adds Seconds and Frames after them
   // and draws their controls itself.
   void DrawModRateControls(int* rateMode, float* rateBeats, float* rateHz, int hzOrdinal,
                            const std::vector<std::string>& names);

   void DrawModRateControls(int* rateMode, float* rateBeats, float* rateHz, int hzOrdinal)
   {
      DrawModRateControls(rateMode, rateBeats, rateHz, hzOrdinal, LFONode::RateModeNames());
   }

   void DrawModRateControls(int* rateMode, float* rateBeats, float* rateHz, int hzOrdinal,
                            const std::vector<std::string>& names)
   {
      DropdownButton("rate in", names, *rateMode,
                     [rateMode](int i) { PushUndoCheckpoint(); *rateMode = i; });
      if (*rateMode == LFONode::kRateBeats)
      {
         ModSlider("rate", rateBeats, params::param::rate_lo, params::param::rate_hi, "%.2f beats");
      }
      else
      {
         const int beatsOrdinal = gParamCounter++;
         RegisterHiddenParam(rateBeats, 0.05f, 32.0f, "rate", beatsOrdinal);
         if (*rateMode == LFONode::kRateSynced)
         {
            const int div = MusicTime::NearestRateDivision(*rateBeats);
            DropdownButton("division", MusicTime::RateDivisionList(), div, [rateBeats](int i) {
               PushUndoCheckpoint();
               *rateBeats = (float)MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(i, 0, (int)MusicTime::kNumRateDivisions - 1));
            });
         }
         // Dotted cable to whatever replaced the hidden beats slider.
         NoteHiddenParamAnchorAtLastItem(beatsOrdinal);
      }
      if (*rateMode == LFONode::kRateHz)
      {
         FixedParamOrdinal at(hzOrdinal);
         ModSlider("rate (Hz)", rateHz, params::param::rate_hz_lo, params::param::rate_hz_hi, "%.2f Hz", kParamWidth, false, 0.0f,
                   LogTaper::PosToValue, LogTaper::ValueToPos);
      }
      else
      {
         RegisterHiddenParam(rateHz, 0.01f, 40.0f, "rate (Hz)", hzOrdinal);
         NoteHiddenParamAnchorAtLastItem(hzOrdinal);
      }
   }


   // LFO / Random / MSEG: run once per voice when cabled straight into a
   // synth's voice param (audio/VoiceMod.h). Off: one shared value, as ever.
   void DrawPerVoiceCheckbox(bool* voice)
   {
      PushCheckboxStyle();
      ModCheckbox("per voice", voice);
      PopCheckboxStyle();
   }


   namespace
   {
      using LFOParamsRow = params::Row<LFONode>;
      const LFOParamsRow kLFOParamsRows[] = {
         LFOParamsRow::Slider<&LFONode::phase>("phase", 0.0f, 1.0f),
         LFOParamsRow::Slider<&LFONode::low>("low", 0.0f, 1.0f),
         LFOParamsRow::Slider<&LFONode::high>("high", 0.0f, 1.0f),
      };
   }

   void DrawLFOParams(LFONode* n)
   {
      Ui::DrawParam(n, LFOParamsRow::Choice<&LFONode::shape>("shape", &LFONode::ShapeNames));
      DrawModRateControls(&n->rateMode, &n->rateBeats, &n->rateHz, 4);
      Ui::DrawParams(n, kLFOParamsRows);
      // Per voice, each note starts the cycle: phase from does not apply.
      ImGui::BeginDisabled(n->voice);
      Ui::DrawParam(n, LFOParamsRow::Choice<&LFONode::phaseMode>("phase from", &LFONode::PhaseModeNames).Undo());
      ImGui::EndDisabled();
      // Seed only means something for S&H (shape 5) on the new maths.
      if (n->shape == 5 && n->maths >= 1)
      {
         FixedParamOrdinal at(5);
         Ui::DrawParam(n, LFOParamsRow::Slider<&LFONode::seed>("seed", params::LFO::seed_lo, params::LFO::seed_hi).Fmt("%.0f").Step(1.0f));
      }
      else
      {
         RegisterHiddenParam(&n->seed, 0.0f, 200.0f, "seed", 5);
      }
      DrawPerVoiceCheckbox(&n->voice);
   }


   namespace
   {
      using RandomParamsRow = params::Row<RandomNode>;
      const RandomParamsRow kRandomParamsRows[] = {
         RandomParamsRow::Slider<&RandomNode::smooth>("smooth", 0.0f, 1.0f),
         RandomParamsRow::Slider<&RandomNode::low>("low", 0.0f, 1.0f),
         RandomParamsRow::Slider<&RandomNode::high>("high", 0.0f, 1.0f),
         RandomParamsRow::Slider<&RandomNode::seed>("seed", params::Random::seed_lo, params::Random::seed_hi).Fmt("%.0f").Step(1.0f),
      };
   }

   void DrawRandomParams(RandomNode* n)
   {
      DrawModRateControls(&n->rateMode, &n->rateBeats, &n->rateHz, 5);
      Ui::DrawParams(n, kRandomParamsRows);
      DrawPerVoiceCheckbox(&n->voice);
   }


   namespace
   {
      using VoiceSpreadParamsRow = params::Row<VoiceSpreadNode>;
      const VoiceSpreadParamsRow kVoiceSpreadParamsRows[] = {
         VoiceSpreadParamsRow::Choice<&VoiceSpreadNode::mode>("mode", &VoiceSpreadNode::ModeNames).Undo(),
         VoiceSpreadParamsRow::SliderInt<&VoiceSpreadNode::count>("count", 1, ModShapes::kSpreadMaxCount),
         VoiceSpreadParamsRow::Slider<&VoiceSpreadNode::low>("low", 0.0f, 1.0f),
         VoiceSpreadParamsRow::Slider<&VoiceSpreadNode::high>("high", 0.0f, 1.0f),
      };
   }

   void DrawVoiceSpreadParams(VoiceSpreadNode* n)
   {
      Ui::DrawParams(n, kVoiceSpreadParamsRows);
   }


   namespace
   {
      using NoteExpressionParamsRow = params::Row<NoteExpressionNode>;
      const NoteExpressionParamsRow kNoteExpressionParamsRows[] = {
         NoteExpressionParamsRow::Choice<&NoteExpressionNode::source>("source", &NoteExpressionNode::SourceNames).Undo(),
         NoteExpressionParamsRow::Slider<&NoteExpressionNode::low>("low", 0.0f, 1.0f),
         NoteExpressionParamsRow::Slider<&NoteExpressionNode::high>("high", 0.0f, 1.0f),
      };
   }

   void DrawNoteExpressionParams(NoteExpressionNode* n)
   {
      Ui::DrawParams(n, kNoteExpressionParamsRows);
   }


   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   void DrawPatternStepGrid(PatternNode* n)
   {
      const int count = std::clamp(n->EffectiveLength(), 1, PatternNode::kSteps);
      const int curStep = n->CurrentStep();
      const int groupSize = tok::param_group_size;
      const float size = kPreviewSize;
      const float height = tok::param_height;
      const float gap = tok::space_0_5;
      const float cellW = count > 1 ? (size - gap * (float)(count - 1)) / (float)count : size;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImVec2 br(origin.x + size, origin.y + height);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImFont* font = ImGui::GetFont();

      UiInteract::Hit("##patterngrid", ImVec2(size, height));
      const bool hovered = ImGui::IsItemHovered();
      const bool itemActive = ImGui::IsItemActive();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      auto stepAt = [&](float mx) {
         const float stepSpan = cellW + gap;
         if (stepSpan <= 0.0f) return 0;
         int s = (int)((mx - origin.x) / stepSpan);
         return std::clamp(s, 0, count - 1);
      };

      if (ImGui::IsItemActivated())
      {
         PushUndoCheckpoint();
         gPatternGridDrag.node = n;
         gPatternGridDrag.lastStep = -1;
      }

      int hoverStep = -1;
      if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         const int s = stepAt(mouse.x);
         if (s >= 0 && s < PatternNode::kSteps)
         {
            n->steps[s] = (n->steps[s] > 0.05f) ? 0.0f : 1.0f;
            hoverStep = s;
            gPatternGridDrag.lastStep = s;
         }
      }
      else if (itemActive && gPatternGridDrag.node == n)
      {
         const int s = stepAt(mouse.x);
         if (s >= 0 && s < PatternNode::kSteps)
         {
            if (ImGui::GetIO().KeyShift)
            {
               n->steps[s] = std::clamp(n->steps[s] - ImGui::GetIO().MouseDelta.y / height * tok::param_pattern_step_grid_ratio, 0.0f, 1.0f);
            }
            else
            {
               const float t = 1.0f - std::clamp((mouse.y - origin.y) / height, 0.0f, 1.0f);
               const int lo = std::clamp(gPatternGridDrag.lastStep >= 0 ? std::min(gPatternGridDrag.lastStep, s) : s, 0, count - 1);
               const int hi = std::clamp(gPatternGridDrag.lastStep >= 0 ? std::max(gPatternGridDrag.lastStep, s) : s, 0, count - 1);
               for (int i = lo; i <= hi; i++)
                  if (i >= 0 && i < PatternNode::kSteps)
                     n->steps[i] = t;
            }
            gPatternGridDrag.lastStep = s;
            hoverStep = s;
         }
      }
      else if (gPatternGridDrag.node == n)
      {
         gPatternGridDrag.node = nullptr;
         gPatternGridDrag.lastStep = -1;
      }

      if (hovered && MenuParts::RightPressed())
      {
         PushUndoCheckpoint();
         const int s = stepAt(mouse.x);
         if (s >= 0 && s < PatternNode::kSteps)
         {
            n->steps[s] = 0.0f;
            hoverStep = s;
         }
      }
      if (hoverStep < 0 && hovered)
         hoverStep = stepAt(mouse.x);

      const bool isLight = IsThemeLight();

      // ---- 1) Container chassis background ----
      dl->AddRectFilled(origin, br, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), tok::radius_control);

      // ---- 2) Subtle horizontal guide lines (25%, 50%, 75%) ----
      const float y25 = origin.y + height * tok::param_y25_ratio;
      const float y50 = origin.y + height * 0.50f;
      const float y75 = origin.y + height * tok::param_y75_ratio;
      const ImU32 subGuideCol = isLight ? tok::Base(tok::col::surface_hi, tok::param_pattern_step_grid_surface_hi_alpha) : tok::Base(tok::col::surface_lo, tok::param_pattern_step_grid_surface_lo_alpha);
      dl->AddLine(ImVec2(origin.x + tok::space_0_5, y25), ImVec2(br.x - tok::space_0_5, y25), subGuideCol, tok::param_pattern_step_grid_stroke);
      dl->AddLine(ImVec2(origin.x + tok::space_0_5, y50), ImVec2(br.x - tok::space_0_5, y50), subGuideCol, tok::param_pattern_step_grid_stroke);
      dl->AddLine(ImVec2(origin.x + tok::space_0_5, y75), ImVec2(br.x - tok::space_0_5, y75), subGuideCol, tok::param_pattern_step_grid_stroke);

      auto DrawTextCentered = [&](const ImVec2& minP, const ImVec2& maxP, ImU32 col, const char* text, float fSz) {
         const ImVec2 sz = font->CalcTextSizeA(fSz, FLT_MAX, 0.0f, text);
         const float tx = minP.x + std::max(0.0f, (maxP.x - minP.x - sz.x) * 0.5f);
         const float ty = minP.y + std::max(0.0f, (maxP.y - minP.y - sz.y) * 0.5f);
         dl->AddText(font, fSz, ImVec2(tx, ty), col, text);
      };

      // ---- 3) Step columns ----
      for (int i = 0; i < count; i++)
      {
         const float x0 = origin.x + (float)i * (cellW + gap);
         const float x1 = x0 + cellW;
         const bool isCurrent = (i == curStep) && curStep < count;
         const bool isGroupStart = (i % groupSize) == 0;
         const bool isHoveredCol = (hoverStep == i);

         // Track lane background
         const ImU32 gridCol = isLight
            ? (isGroupStart ? tok::Base(tok::col::surface_hi) : tok::Base(tok::col::surface_hi))
            : (isGroupStart ? tok::Role(tok::col::surface_lo) : tok::Role(tok::col::edge_deep));
         dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y), gridCol, tok::radius_cell);

         // Column hover or playhead background wash
         if (isCurrent)
         {
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                              isLight ? tok::Base(tok::col::role_modulation, tok::param_pattern_step_grid_fill_alpha_lg) : tok::Base(tok::col::role_modulation, tok::param_pattern_step_grid_fill_alpha_md), tok::radius_cell);
         }
         else if (isHoveredCol)
         {
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                              isLight ? tok::Base(tok::col::ink_black, tok::param_pattern_step_grid_fill_alpha_xs) : tok::Base(tok::col::ink_white, tok::param_pattern_step_grid_fill_alpha_sm), tok::radius_cell);
         }

         const float v = std::clamp(n->steps[i], 0.0f, 1.0f);
         const float fillTop = br.y - v * height;
         const float fillBottom = br.y;

         // Bar stem fill
         const ImU32 fillCol = isCurrent
            ? (isLight ? tok::Base(tok::col::state_warn, tok::param_pattern_step_grid_state_warn_alpha) : tok::Base(tok::col::role_modulation, tok::param_pattern_step_grid_role_modulation_alpha))
            : (isLight ? tok::Base(tok::col::accent, tok::param_pattern_step_grid_accent_alpha) : tok::Base(tok::col::accent_pale_muted_xlo, tok::param_pattern_step_grid_accent_pale_muted_xlo_alpha));

         if (std::fabs(fillBottom - fillTop) > 1.0f)
         {
            dl->AddRectFilled(ImVec2(x0 + 1.0f, fillTop), ImVec2(x1 - 1.0f, fillBottom), fillCol, tok::radius_cell);
         }

         // Value Cap Pill at the level mark
         const float capH = tok::param_cap_h;
         const float capY = std::clamp(fillTop, origin.y, br.y - capH);

         const ImU32 capCol = isCurrent
            ? (isLight ? tok::Base(tok::col::text_warn) : tok::Base(tok::col::role_modulation))
            : (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_xhi));
         const ImU32 capBorder = isCurrent
            ? (isLight ? tok::Base(tok::col::role_solo) : tok::Base(tok::col::role_solo_alt))
            : (isLight ? tok::Base(tok::col::accent_alt, tok::param_pattern_step_grid_accent_alt_alpha) : tok::Base(tok::col::accent_alt, tok::param_cap_border_alpha));

         dl->AddRectFilled(ImVec2(x0 + 1.0f, capY), ImVec2(x1 - 1.0f, capY + capH), capCol, tok::param_pattern_step_grid_radius);
         dl->AddRect(ImVec2(x0 + tok::space_0_25, capY), ImVec2(x1 - tok::space_0_25, capY + capH), capBorder, tok::param_pattern_step_grid_radius);

         // Display value inside cap if column is wide enough
         if (cellW >= tok::param_pattern_step_grid_limit)
         {
            char valStr[16];
            if (cellW >= tok::param_pattern_step_grid_limit_lg)
               snprintf(valStr, sizeof(valStr), "%s", UiFmt::F2(v).c);
            else
               snprintf(valStr, sizeof(valStr), "%s", UiFmt::F1(v).c);

            const float fontValSz = count <= 8 ? 9.5f : 8.5f;
            const ImU32 txtCol = isCurrent ? tok::Role(tok::col::surface_deep) : tok::Role(tok::col::surface_deep);
            DrawTextCentered(ImVec2(x0 + 1.0f, capY), ImVec2(x1 - 1.0f, capY + capH), txtCol, valStr, fontValSz);
         }

         // Playhead outline
         if (isCurrent)
         {
            StepCell::PlayheadOutline(dl, ImVec2(x0, origin.y), ImVec2(x1, br.y));
         }

         // Step number label below column
         char label[16];
         snprintf(label, sizeof(label), "%d", i + 1);
         const float labelFontSize = count <= 8 ? ImGui::GetFontSize()  // ui-audit-ok: model value: cell count above which the label shrinks to fit
                                                 : std::clamp(cellW * tok::type_fit_frac, tok::type_min, ImGui::GetFontSize());
         const ImVec2 textSize = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
         const ImU32 stepNumCol = isCurrent
            ? (isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation))
            : (isLight
                  ? (isGroupStart ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::edge_mid))
                  : (isGroupStart ? tok::Role(tok::col::surface_mid) : tok::Role(tok::col::edge_mid)));
         dl->AddText(font, labelFontSize, ImVec2(x0 + (cellW - textSize.x) * 0.5f, br.y + tok::space_0_75),
                     stepNumCol, label);
      }

      // Outer border
      dl->AddRect(origin, br, isLight ? tok::Base(tok::col::edge_mid_light_fixed) : tok::Base(tok::col::edge_lo_light), tok::radius_control);

      const float labelRowH = ImGui::GetFontSize() + tok::space_1_25;
      ImGui::SetCursorScreenPos(ImVec2(origin.x, br.y + labelRowH));
      PushTextWrapWidth(size);
      if (hoverStep >= 0)
      {
         ImGui::TextDisabled("step %d: %s", hoverStep + 1, UiFmt::F2(n->steps[hoverStep]).c);
      }
      else
      {
         ImGui::TextDisabled("%s steps, looped", UiFmt::Count((long long)(count)).c);
      }
      PopTextWrapWidth();
   }
   // ui-content-end


   namespace
   {
      using PatternParamsRow = params::Row<PatternNode>;
      const PatternParamsRow kPatternParamsRows[] = {
         PatternParamsRow::SliderInt<&PatternNode::length>("length", 1, PatternNode::kSteps),
         PatternParamsRow::Slider<&PatternNode::stepBeats>("beats / step", params::Pattern::step_beats_lo, params::Pattern::step_beats_hi),
         PatternParamsRow::Toggle<&PatternNode::smoothSteps>("glide between steps"),
         PatternParamsRow::Slider<&PatternNode::low>("low", 0.0f, 1.0f),
         PatternParamsRow::Slider<&PatternNode::high>("high", 0.0f, 1.0f),
      };
   }

   void DrawPatternParams(PatternNode* n)
   {
      DrawPatternStepGrid(n);

      Ui::DrawParams(n, kPatternParamsRows);
   }


   namespace
   {
      using MathParamsRow = params::Row<MathNode>;
      const MathParamsRow kMathParamsRows[] = {
         MathParamsRow::Slider<&MathNode::gain>("gain", params::Math::gain_lo, params::Math::gain_hi),
         MathParamsRow::Slider<&MathNode::offset>("offset", -1.0f, 1.0f),
         MathParamsRow::Toggle<&MathNode::clampOutput>("clamp to 0..1"),
      };
   }

   void DrawMathParams(MathNode* n)
   {
      Ui::DrawParam(n, MathParamsRow::Choice<&MathNode::op>("operation", &MathNode::OpNames));
      if (n->inputA == nullptr)
         Ui::DrawParam(n, MathParamsRow::Slider<&MathNode::constantA>("A (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("A: patched");
      if (n->inputB == nullptr)
         Ui::DrawParam(n, MathParamsRow::Slider<&MathNode::constantB>("B (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("B: patched");
      Ui::DrawParams(n, kMathParamsRows);
   }


   namespace
   {
      using CompareParamsRow = params::Row<CompareNode>;
   }

   void DrawCompareParams(CompareNode* n)
   {
      Ui::DrawParam(n, CompareParamsRow::Choice<&CompareNode::op>("operation", &CompareNode::OpNames));
      if (n->inputA == nullptr)
         Ui::DrawParam(n, CompareParamsRow::Slider<&CompareNode::constantA>("A (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("A: patched");
      if (n->inputB == nullptr)
         Ui::DrawParam(n, CompareParamsRow::Slider<&CompareNode::constantB>("B (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("B: patched");
      if (n->op == 4 || n->op == 5) // == or !=
         Ui::DrawParam(n, CompareParamsRow::Slider<&CompareNode::tolerance>("tolerance", params::Compare::tolerance_lo, params::Compare::tolerance_hi));
   }


   void DrawLogicGateBody(LogicGateNode* n, int nodeIndex)
   {
      const bool isLight = IsThemeLight();
      const float val = n->Value01();
      const float inA = n->LastInputA();
      const float hi = std::clamp(std::max(n->highThresh, n->lowThresh + 0.005f), 0.0f, 1.0f);
      const float lo = std::clamp(std::min(n->lowThresh, hi - 0.005f), 0.0f, 1.0f);

      static std::map<int, std::vector<float>> sGateInHist;
      static std::map<int, double> sGateLastT;
      std::vector<float>& hist = sGateInHist[nodeIndex];
      const double now = ImGui::GetTime();
      double& last = sGateLastT[nodeIndex];
      if (hist.empty() || now - last > tok::motion_s_history_step || now < last)
      {
         hist.assign(1, inA);
         last = now;
      }
      else
      {
         constexpr double kStep = 1.0 / 60.0;
         int steps = 0;
         for (; last + kStep <= now && steps < 4; last += kStep, ++steps)
            hist.push_back(inA);
         if (steps == 4)
            last = now;
      }
      while (hist.size() > tok::param_logic_gate_body_limit)
         hist.erase(hist.begin());

      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float w = kPreviewSize;
      const float h = tok::param_h;
      const float pad = tok::space_1_5;
      const float innerW = w - tok::param_inner_w_ratio * pad;
      const float innerH = h - tok::param_inner_h_ratio * pad;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      Ui::Claim(ImVec2(w, h));
      const ImVec2 br(origin.x + w, origin.y + h);
      AudioViz::Fill(dl, origin, br);

      auto yOf = [&](float v) { return origin.y + pad + innerH * (1.0f - std::clamp(v, 0.0f, 1.0f)); };

      // Schmitt hysteresis thresholds (highThresh & lowThresh)
      const float yHi = yOf(hi);
      const float yLo = yOf(lo);
      dl->AddRectFilled(ImVec2(origin.x + pad, yHi), ImVec2(origin.x + w - pad, yLo),
                        isLight ? tok::Base(tok::col::ink_black, tok::param_logic_gate_fill_alpha_md) : tok::Base(tok::col::ink_white, tok::param_logic_gate_fill_alpha_sm));
      dl->AddLine(ImVec2(origin.x + pad, yHi), ImVec2(origin.x + w - pad, yHi),
                  tok::Role(tok::col::state_ok_dark_vivid_hi), tok::param_logic_gate_body_stroke);
      dl->AddLine(ImVec2(origin.x + pad, yLo), ImVec2(origin.x + w - pad, yLo),
                  tok::Role(tok::col::role_modulation), tok::param_logic_gate_body_stroke);

      // Live input A curve
      const ImU32 curveCol = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::role_modulation);
      for (size_t i = 1; i < hist.size(); ++i)
      {
         const float x0 = origin.x + pad + ((float)(i - 1) / tok::param_logic_gate_body_ratio) * innerW;
         const float x1 = origin.x + pad + ((float)i / tok::param_logic_gate_body_ratio) * innerW;
         dl->AddLine(ImVec2(x0, yOf(hist[i - 1])), ImVec2(x1, yOf(hist[i])), curveCol, tok::param_line_stroke_lg);
      }

      // Hold dwell bar + output state LED in top-right
      const float hold01 = n->HoldProgress01();
      const float barW = tok::param_bar_w;
      const ImVec2 barTl(origin.x + pad, origin.y + pad);
      const ImVec2 barBr(barTl.x + barW, barTl.y + tok::space_1);
      dl->AddRectFilled(barTl, barBr, tok::Role(tok::col::ink_white, tok::param_logic_gate_fill_alpha_lg), tok::param_logic_gate_body_radius);
      if (hold01 > 0.01f)
         dl->AddRectFilled(barTl, ImVec2(barTl.x + barW * hold01, barBr.y), tok::Role(tok::col::state_ok_dark_vivid_hi), tok::param_logic_gate_body_radius);

      const bool high = val > 0.5f;
      const ImVec2 ledPos(origin.x + w - pad - tok::space_1_5, origin.y + pad + tok::space_1_5);
      dl->AddCircleFilled(ledPos, tok::param_logic_gate_body_r, high ? tok::Role(tok::col::state_ok_dark_vivid_hi) : tok::Role(tok::col::ink_white, tok::param_logic_gate_fill_alpha_lg));
      AudioViz::Border(dl, origin, br);
   }


   namespace
   {
      using LogicGateParamsRow = params::Row<LogicGateNode>;
      const LogicGateParamsRow kLogicGateParamsRows1[] = {
         LogicGateParamsRow::Choice<&LogicGateNode::op>("operation", &LogicGateNode::OpNames).Undo(),
         LogicGateParamsRow::Choice<&LogicGateNode::quantize>("quantize", &LogicGateNode::QuantizeNames).Undo(),
      };
      const LogicGateParamsRow kLogicGateParamsRows2[] = {
         LogicGateParamsRow::Slider<&LogicGateNode::highThresh>("high thresh", params::LogicGate::high_thresh_lo, params::LogicGate::high_thresh_hi),
         LogicGateParamsRow::Slider<&LogicGateNode::lowThresh>("low thresh", params::LogicGate::low_thresh_lo, params::LogicGate::low_thresh_hi),
         LogicGateParamsRow::Slider<&LogicGateNode::holdMs>("hold ms", params::LogicGate::hold_ms_lo, params::LogicGate::hold_ms_hi).Fmt("%.0f"),
      };
   }

   void DrawLogicGateParams(LogicGateNode* n)
   {
      Ui::DrawParams(n, kLogicGateParamsRows1);
      if (n->inputA == nullptr)
         Ui::DrawParam(n, LogicGateParamsRow::Slider<&LogicGateNode::constantA>("in A (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in A: patched");
      if (n->inputB == nullptr)
         Ui::DrawParam(n, LogicGateParamsRow::Slider<&LogicGateNode::constantB>("in B (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in B: patched");

      Ui::DrawParams(n, kLogicGateParamsRows2);
      const bool needsPulse = (n->op == LogicGateNode::kOpRisingEdge);
      const bool needsRefrac = (n->op == LogicGateNode::kOpRisingEdge || n->op == LogicGateNode::kOpToggle);
      ImGui::BeginDisabled(!needsPulse && !gParamRegisterOnly);
      Ui::DrawParam(n, LogicGateParamsRow::Slider<&LogicGateNode::pulseMs>("pulse ms", params::LogicGate::pulse_ms_lo, params::LogicGate::pulse_ms_hi).Fmt("%.0f"));
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!needsRefrac && !gParamRegisterOnly);
      Ui::DrawParam(n, LogicGateParamsRow::Slider<&LogicGateNode::refractoryMs>("refractory ms", params::LogicGate::refractory_ms_lo, params::LogicGate::refractory_ms_hi).Fmt("%.0f"));
      ImGui::EndDisabled();
      Ui::DrawParam(n, LogicGateParamsRow::Slider<&LogicGateNode::minPresence>("min presence", params::LogicGate::min_presence_lo, params::LogicGate::min_presence_hi));
   }


   namespace
   {
      using BrainSculptParamsRow = params::Row<BrainSculptNode>;
      const BrainSculptParamsRow kBrainSculptParamsRows[] = {
         BrainSculptParamsRow::Slider<&BrainSculptNode::sigma>("kernel width", params::BrainSculpt::sigma_lo, params::BrainSculpt::sigma_hi),
         BrainSculptParamsRow::Slider<&BrainSculptNode::smoothing>("smoothing", 0.0f, 1.0f),
         BrainSculptParamsRow::Slider<&BrainSculptNode::regularization>("regularization", params::BrainSculpt::regularization_lo, params::BrainSculpt::regularization_hi),
      };
   }

   void DrawBrainSculptParams(BrainSculptNode* n)
   {
      ImGui::TextDisabled("Shift-grab any wired knob to bind state");
      ImGui::TextDisabled("%d anchors across %d params", n->TotalAnchorCount(), n->BoundParamCount());
      if (ActionButton::Draw("Clear Anchors##brainSculptClear", ImVec2(kPreviewSize, 0)))
      {
         PushUndoCheckpoint();
         n->ClearAnchors();
      }
      Ui::DrawParams(n, kBrainSculptParamsRows);
   }


   namespace
   {
      using RangeToRangeParamsRow = params::Row<RangeToRangeNode>;
      const RangeToRangeParamsRow kRangeToRangeParamsRows[] = {
         RangeToRangeParamsRow::Slider<&RangeToRangeNode::inLow>("in low", params::RangeToRange::in_low_lo, params::RangeToRange::in_low_hi),
         RangeToRangeParamsRow::Slider<&RangeToRangeNode::inHigh>("in high", params::RangeToRange::in_high_lo, params::RangeToRange::in_high_hi),
         RangeToRangeParamsRow::Slider<&RangeToRangeNode::outLow>("out low", params::RangeToRange::out_low_lo, params::RangeToRange::out_low_hi),
         RangeToRangeParamsRow::Slider<&RangeToRangeNode::outHigh>("out high", params::RangeToRange::out_high_lo, params::RangeToRange::out_high_hi),
         RangeToRangeParamsRow::Toggle<&RangeToRangeNode::clampOutput>("clamp to out range"),
      };
   }

   void DrawRangeToRangeParams(RangeToRangeNode* n)
   {
      if (n->input == nullptr)
         Ui::DrawParam(n, RangeToRangeParamsRow::Slider<&RangeToRangeNode::constantIn>("in (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in: patched");
      Ui::DrawParams(n, kRangeToRangeParamsRows);
   }


   namespace
   {
      using SmoothParamsRow = params::Row<SmoothNode>;
      const SmoothParamsRow kSmoothParamsRows[] = {
         SmoothParamsRow::Slider<&SmoothNode::frequency>("frequency (Hz)", params::Smooth::frequency_lo, params::Smooth::frequency_hi),
         SmoothParamsRow::Slider<&SmoothNode::damping>("damping", params::Smooth::damping_lo, params::Smooth::damping_hi),
      };
   }

   void DrawSmoothParams(SmoothNode* n)
   {
      if (n->input == nullptr)
         Ui::DrawParam(n, SmoothParamsRow::Slider<&SmoothNode::constantIn>("in (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in: patched");
      // Legacy (maths 0) amount is a per-frame factor; on the new maths it is
      // the same feel at 60 fps turned into a time constant, so fps-free.
      static const std::vector<std::string> kLegacyModes = { "per frame", "time", "spring" };
      static const std::vector<std::string> kModes = { "amount", "time", "spring" };
      DropdownButton("mode", n->maths >= 1 ? kModes : kLegacyModes, n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; });
      if (n->mode == SmoothNode::kTime)
         Ui::DrawParam(n, SmoothParamsRow::Slider<&SmoothNode::timeConstant>("time (s)", params::Smooth::time_constant_lo, params::Smooth::time_constant_hi));
      else if (n->mode == SmoothNode::kSpring)
      {
         Ui::DrawParams(n, kSmoothParamsRows);
      }
      else
         Ui::DrawParam(n, SmoothParamsRow::Slider<&SmoothNode::amount>("amount", params::Smooth::amount_lo, params::Smooth::amount_hi));
   }


   namespace
   {
      using InvertParamsRow = params::Row<InvertNode>;
      const InvertParamsRow kInvertParamsRows[] = {
         InvertParamsRow::Slider<&InvertNode::low>("low", params::Invert::low_lo, params::Invert::low_hi),
         InvertParamsRow::Slider<&InvertNode::high>("high", params::Invert::high_lo, params::Invert::high_hi),
      };
   }

   void DrawInvertParams(InvertNode* n)
   {
      if (n->input == nullptr)
         Ui::DrawParam(n, InvertParamsRow::Slider<&InvertNode::constantIn>("in (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in: patched");
      Ui::DrawParams(n, kInvertParamsRows);
   }


   namespace
   {
      using ModDepthParamsRow = params::Row<ModDepthNode>;
   }

   void DrawModDepthParams(ModDepthNode* n)
   {
      if (n->input == nullptr)
         Ui::DrawParam(n, ModDepthParamsRow::Slider<&ModDepthNode::constantIn>("in (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in: patched");
      Ui::DrawParam(n, ModDepthParamsRow::Slider<&ModDepthNode::depth>("depth", -1.0f, 1.0f));
   }


   namespace
   {
      using NoiseParamsRow = params::Row<NoiseNode>;
      const NoiseParamsRow kNoiseParamsRows1[] = {
         NoiseParamsRow::Choice<&NoiseNode::noiseType>("type", &NoiseNode::TypeNames),
         NoiseParamsRow::Slider<&NoiseNode::width>("width", params::Noise::width_lo, params::Noise::width_hi).Fmt("%.0f"),
         NoiseParamsRow::Slider<&NoiseNode::height>("height", params::Noise::height_lo, params::Noise::height_hi).Fmt("%.0f"),
         NoiseParamsRow::Slider<&NoiseNode::scale>("scale", params::Noise::scale_lo, params::Noise::scale_hi),
      };
      const NoiseParamsRow kNoiseParamsRows2[] = {
         NoiseParamsRow::Slider<&NoiseNode::octaves>("octaves", params::Noise::octaves_lo, params::Noise::octaves_hi).Fmt("%.0f"),
         NoiseParamsRow::Slider<&NoiseNode::lacunarity>("lacunarity", params::Noise::lacunarity_lo, params::Noise::lacunarity_hi),
         NoiseParamsRow::Slider<&NoiseNode::gain>("gain", params::Noise::gain_lo, params::Noise::gain_hi),
      };
      const NoiseParamsRow kNoiseParamsRows3[] = {
         NoiseParamsRow::Slider<&NoiseNode::warp>("warp", params::Noise::warp_lo, params::Noise::warp_hi),
         NoiseParamsRow::Slider<&NoiseNode::speed>("speed", params::Noise::speed_lo, params::Noise::speed_hi),
         NoiseParamsRow::Slider<&NoiseNode::contrast>("contrast", params::Noise::contrast_lo, params::Noise::contrast_hi),
         NoiseParamsRow::Slider<&NoiseNode::brightness>("brightness", params::Noise::brightness_lo, params::Noise::brightness_hi),
         NoiseParamsRow::Slider<&NoiseNode::seed>("seed", params::Noise::seed_lo, params::Noise::seed_hi).Fmt("%.0f").Step(1.0f),
         NoiseParamsRow::Toggle<&NoiseNode::colorNoise>("rgb noise"),
      };
      const NoiseParamsRow kNoiseParamsRows4[] = {
         NoiseParamsRow::Colour<&NoiseNode::lowColor>("low"),
         NoiseParamsRow::Colour<&NoiseNode::highColor>("high"),
      };
   }

   void DrawNoiseParams(NoiseNode* n)
   {
      Ui::DrawParams(n, kNoiseParamsRows1);
      // Octaves, lacunarity and gain only shape the fractal types (and the colour channels of the
      // plain ones, which are fractal); the other types ignore them.
      const int nt = n->noiseType;
      const bool fractal = nt == 1 || nt == 2 || nt == 8 || nt == 9 || (n->colorNoise && nt <= 4);
      ImGui::BeginDisabled(!fractal && !gParamRegisterOnly);
      Ui::DrawParams(n, kNoiseParamsRows2);
      ImGui::EndDisabled();
      Ui::DrawParams(n, kNoiseParamsRows3);
      if (!n->colorNoise)
      {
         Ui::DrawParams(n, kNoiseParamsRows4);
      }
   }


   namespace
   {
      using TextureParamsRow = params::Row<TextureNode>;
      const TextureParamsRow kTextureParamsRows1[] = {
         TextureParamsRow::Choice<&TextureNode::textureType>("type", &TextureNode::TypeNames),
         TextureParamsRow::Slider<&TextureNode::width>("width", params::Texture::width_lo, params::Texture::width_hi).Fmt("%.0f"),
         TextureParamsRow::Slider<&TextureNode::height>("height", params::Texture::height_lo, params::Texture::height_hi).Fmt("%.0f"),
         TextureParamsRow::Slider<&TextureNode::scale>("scale", params::Texture::scale_lo, params::Texture::scale_hi),
         TextureParamsRow::Slider<&TextureNode::seed>("seed", params::Texture::seed_lo, params::Texture::seed_hi).Fmt("%.0f").Step(1.0f),
      };
      const TextureParamsRow kTextureParamsRows2[] = {
         TextureParamsRow::Choice<&TextureNode::voronoiDistance>("distance", &TextureNode::VoronoiDistanceNames),
         TextureParamsRow::Choice<&TextureNode::voronoiFeature>("feature", &TextureNode::VoronoiFeatureNames),
      };
      const TextureParamsRow kTextureParamsRows3[] = {
         TextureParamsRow::Slider<&TextureNode::voronoiRandomness>("randomness", 0.0f, 1.0f),
         TextureParamsRow::Toggle<&TextureNode::voronoiCellColor>("cell color"),
      };
      const TextureParamsRow kTextureParamsRows4[] = {
         TextureParamsRow::Slider<&TextureNode::brickWidth>("brick width", params::Texture::brick_width_lo, params::Texture::brick_width_hi),
         TextureParamsRow::Slider<&TextureNode::brickHeight>("row height", params::Texture::brick_height_lo, params::Texture::brick_height_hi),
         TextureParamsRow::Slider<&TextureNode::brickRowOffset>("row offset", 0.0f, 1.0f),
         TextureParamsRow::Slider<&TextureNode::brickMortarSize>("mortar size", params::Texture::brick_mortar_size_lo, params::Texture::brick_mortar_size_hi),
         TextureParamsRow::Slider<&TextureNode::brickMortarSmooth>("mortar smooth", 0.0f, 1.0f),
         TextureParamsRow::Slider<&TextureNode::brickBias>("bias", params::Texture::brick_bias_lo, params::Texture::brick_bias_hi),
         TextureParamsRow::Colour<&TextureNode::mortarColor>("mortar"),
      };
      const TextureParamsRow kTextureParamsRows5[] = {
         TextureParamsRow::Slider<&TextureNode::magicDepth>("depth", params::Texture::magic_depth_lo, params::Texture::magic_depth_hi).Fmt("%.0f"),
         TextureParamsRow::Slider<&TextureNode::magicDistortion>("distortion", params::Texture::magic_distortion_lo, params::Texture::magic_distortion_hi),
      };
      const TextureParamsRow kTextureParamsRows6[] = {
         TextureParamsRow::Choice<&TextureNode::waveProfile>("profile", &TextureNode::WaveProfileNames),
         TextureParamsRow::Slider<&TextureNode::waveDistortion>("distortion", params::Texture::wave_distortion_lo, params::Texture::wave_distortion_hi),
         TextureParamsRow::Slider<&TextureNode::waveDetail>("detail", params::Texture::wave_detail_lo, params::Texture::wave_detail_hi).Fmt("%.0f"),
         TextureParamsRow::Slider<&TextureNode::waveDetailScale>("detail scale", params::Texture::wave_detail_scale_lo, params::Texture::wave_detail_scale_hi),
         TextureParamsRow::Slider<&TextureNode::wavePhaseOffset>("phase offset", 0.0f, 1.0f),
      };
      const TextureParamsRow kTextureParamsRows7[] = {
         TextureParamsRow::Choice<&TextureNode::musgraveType>("musgrave type", &TextureNode::MusgraveTypeNames),
         TextureParamsRow::Slider<&TextureNode::musgraveDimension>("dimension", params::Texture::musgrave_dimension_lo, params::Texture::musgrave_dimension_hi),
         TextureParamsRow::Slider<&TextureNode::musgraveLacunarity>("lacunarity", params::Texture::musgrave_lacunarity_lo, params::Texture::musgrave_lacunarity_hi),
         TextureParamsRow::Slider<&TextureNode::musgraveOctaves>("octaves", params::Texture::musgrave_octaves_lo, params::Texture::musgrave_octaves_hi).Fmt("%.0f"),
      };
      const TextureParamsRow kTextureParamsRows8[] = {
         TextureParamsRow::Slider<&TextureNode::cloudsDepth>("depth", params::Texture::clouds_depth_lo, params::Texture::clouds_depth_hi).Fmt("%.0f"),
         TextureParamsRow::Toggle<&TextureNode::cloudsHard>("hard"),
      };
      const TextureParamsRow kTextureParamsRows9[] = {
         TextureParamsRow::Choice<&TextureNode::marbleType>("marble type", &TextureNode::MarbleTypeNames),
         TextureParamsRow::Slider<&TextureNode::marbleTurbulence>("turbulence", params::Texture::marble_turbulence_lo, params::Texture::marble_turbulence_hi),
         TextureParamsRow::Slider<&TextureNode::marbleNoiseScale>("noise scale", params::Texture::marble_noise_scale_lo, params::Texture::marble_noise_scale_hi),
         TextureParamsRow::Slider<&TextureNode::marbleNoiseDepth>("noise depth", params::Texture::marble_noise_depth_lo, params::Texture::marble_noise_depth_hi).Fmt("%.0f"),
      };
      const TextureParamsRow kTextureParamsRows10[] = {
         TextureParamsRow::Choice<&TextureNode::woodType>("wood type", &TextureNode::WoodTypeNames),
         TextureParamsRow::Slider<&TextureNode::woodTurbulence>("turbulence", params::Texture::wood_turbulence_lo, params::Texture::wood_turbulence_hi),
         TextureParamsRow::Slider<&TextureNode::woodNoiseScale>("noise scale", params::Texture::wood_noise_scale_lo, params::Texture::wood_noise_scale_hi),
      };
      const TextureParamsRow kTextureParamsRows11[] = {
         TextureParamsRow::Slider<&TextureNode::contrast>("contrast", params::Texture::contrast_lo, params::Texture::contrast_hi),
         TextureParamsRow::Slider<&TextureNode::brightness>("brightness", params::Texture::brightness_lo, params::Texture::brightness_hi),
      };
      const TextureParamsRow kTextureParamsRows12[] = {
         TextureParamsRow::Colour<&TextureNode::lowColor>("low"),
         TextureParamsRow::Colour<&TextureNode::highColor>("high"),
      };
   }

   void DrawTextureParams(TextureNode* n)
   {
      Ui::DrawParams(n, kTextureParamsRows1);

      if (n->textureType == 0) // Voronoi
      {
         Ui::DrawParams(n, kTextureParamsRows2);
         if (n->voronoiDistance == 3)
            Ui::DrawParam(n, TextureParamsRow::Slider<&TextureNode::voronoiMinkowskiExponent>("minkowski exponent", params::Texture::voronoi_minkowski_exponent_lo, params::Texture::voronoi_minkowski_exponent_hi));
         if (n->voronoiFeature == 2)
            Ui::DrawParam(n, TextureParamsRow::Slider<&TextureNode::voronoiSmoothness>("smoothness", params::Texture::voronoi_smoothness_lo, params::Texture::voronoi_smoothness_hi));
         Ui::DrawParams(n, kTextureParamsRows3);
      }
      else if (n->textureType == 1) // Brick
      {
         Ui::DrawParams(n, kTextureParamsRows4);
      }
      else if (n->textureType == 2) // Magic
      {
         Ui::DrawParams(n, kTextureParamsRows5);
      }
      else if (n->textureType == 3) // Wave
      {
         Ui::DrawParam(n, TextureParamsRow::Choice<&TextureNode::waveType>("wave type", &TextureNode::WaveTypeNames));
         if (n->waveType == 0)
            Ui::DrawParam(n, TextureParamsRow::Choice<&TextureNode::waveBandsDirection>("bands direction", &TextureNode::WaveBandsDirectionNames));
         Ui::DrawParams(n, kTextureParamsRows6);
      }
      else if (n->textureType == 4) // Musgrave
      {
         Ui::DrawParams(n, kTextureParamsRows7);
         if (n->musgraveType == 3)
            Ui::DrawParam(n, TextureParamsRow::Slider<&TextureNode::musgraveGain>("gain", params::Texture::musgrave_gain_lo, params::Texture::musgrave_gain_hi));
         if (n->musgraveType >= 2)
            Ui::DrawParam(n, TextureParamsRow::Slider<&TextureNode::musgraveOffset>("offset", params::Texture::musgrave_offset_lo, params::Texture::musgrave_offset_hi));
      }
      else if (n->textureType == 5) // Checker
      {
         // No extra params - scale/seed above already control cell size.
      }
      else if (n->textureType == 6) // Gradient
      {
         Ui::DrawParam(n, TextureParamsRow::Choice<&TextureNode::gradientType>("gradient type", &TextureNode::GradientTypeNames));
      }
      else if (n->textureType == 7) // Clouds
      {
         Ui::DrawParams(n, kTextureParamsRows8);
      }
      else if (n->textureType == 8) // Marble
      {
         Ui::DrawParams(n, kTextureParamsRows9);
      }
      else // Wood
      {
         Ui::DrawParams(n, kTextureParamsRows10);
      }

      Ui::DrawParams(n, kTextureParamsRows11);
      if (n->textureType != 1 && !(n->textureType == 0 && n->voronoiCellColor) && n->textureType != 2)
      {
         Ui::DrawParams(n, kTextureParamsRows12);
      }
   }


   namespace
   {
      using SwitcherParamsRow = params::Row<SwitcherNode>;
      const SwitcherParamsRow kSwitcherParamsRows1[] = {
         SwitcherParamsRow::Choice<&SwitcherNode::unit>("unit", &SwitcherNode::UnitNames),
         SwitcherParamsRow::Slider<&SwitcherNode::interval>("every", params::Switcher::interval_lo, params::Switcher::interval_hi),
      };
      const SwitcherParamsRow kSwitcherParamsRows2[] = {
         SwitcherParamsRow::Slider<&SwitcherNode::crossfade>("crossfade", params::Switcher::crossfade_lo, params::Switcher::crossfade_hi),
         SwitcherParamsRow::Toggle<&SwitcherNode::manual>("manual"),
      };
   }

   void DrawSwitcherParams(SwitcherNode* n)
   {
      ImGui::BeginDisabled(n->manual && !gParamRegisterOnly);   // manual pins the slot; the clock is unused
      Ui::DrawParams(n, kSwitcherParamsRows1);
      ImGui::EndDisabled();
      Ui::DrawParams(n, kSwitcherParamsRows2);
      if (n->manual)
      {
         Ui::DrawParam(n, SwitcherParamsRow::SliderInt<&SwitcherNode::manualSlot>("slot", 0, SwitcherNode::kSlots - 1));
      }
      else
      {
         ImGui::TextDisabled("showing input %c", 'A' + n->ActiveSlot());
      }
   }


   // 2D control surface. Dragging the orb sweeps the mutation weights; the
   // recorded path is drawn behind it so a loop is visible while it plays.
   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   void DrawFxPad(ResynthNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      UiInteract::Hit("##fxpad", ImVec2(size, size));
      const bool active = ImGui::IsItemActive();

      if (active)
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);

      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y),
                     ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f),
                     ScopeGridCol());
      }

      const std::vector<ResynthNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, isLight ? tok::Base(tok::col::accent, tok::param_line_alpha_lg) : tok::Base(tok::col::accent_pale_muted_warm, tok::param_line_alpha_sm), tok::param_fx_pad_stroke);
      }

      // Corner labels: the pad blends between these four named effects, so it is
      // obvious what is being swept rather than four anonymous weights.
      const ImU32 labelCol = isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::text_secondary);
      const char* bl = n->CornerLabel(0);
      const char* brName = n->CornerLabel(1);
      const char* tl = n->CornerLabel(tok::param_tl_corner_label);
      const char* trName = n->CornerLabel(3);
      dl->AddText(ImVec2(origin.x + tok::space_1_25, origin.y + tok::space_1), labelCol, tl);
      ImVec2 trSize = ImGui::CalcTextSize(trName);
      dl->AddText(ImVec2(br.x - trSize.x - tok::space_1_25, origin.y + tok::space_1), labelCol, trName);
      ImVec2 blSize = ImGui::CalcTextSize(bl);
      dl->AddText(ImVec2(origin.x + tok::space_1_25, br.y - blSize.y - tok::space_1), labelCol, bl);
      ImVec2 brSize = ImGui::CalcTextSize(brName);
      dl->AddText(ImVec2(br.x - brSize.x - tok::space_1_25, br.y - brSize.y - tok::space_1), labelCol, brName);

      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? tok::Base(tok::col::text_err)
                     : n->IsPlayingPath()   ? (isLight ? tok::Base(tok::col::state_ok_dark_vivid_lo) : tok::Base(tok::col::state_ok))
                                            : (isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation));
      dl->AddCircleFilled(orb, tok::param_fx_pad_r, orbColor);
      dl->AddCircle(orb, tok::param_fx_pad_r, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), 0, tok::param_ring_stroke_lg);
      AudioViz::Border(dl, origin, br);
   }
   // ui-content-end


   namespace
   {
      using ResynthParamsRow = params::Row<ResynthNode>;
      const ResynthParamsRow kResynthParamsRows[] = {
         ResynthParamsRow::Slider<&ResynthNode::chaos>("chaos", 0.0f, 1.0f),
         ResynthParamsRow::Slider<&ResynthNode::mutation>("mutation", 0.0f, 1.0f),
         ResynthParamsRow::Slider<&ResynthNode::feedback>("feedback", 0.0f, 1.0f),
         ResynthParamsRow::Slider<&ResynthNode::sourcePull>("source pull", 0.0f, 1.0f),
      };
   }

   void DrawResynthParams(ResynthNode* n)
   {
      DrawFxPad(n);

      if (n->IsRecordingPath())
      {
         if (ActionButton::Draw("Stop rec", ImVec2(kPreviewSize * tok::param_resynth_params_ratio, 0), ActionButton::Kind::Record))
            n->StopRecording();
      }
      else
      {
         if (ActionButton::Draw("Rec path", ImVec2(kPreviewSize * tok::param_resynth_params_ratio, 0)))
            n->StartRecording();
      }
      Ui::SameRow();
      if (n->IsPlayingPath())
      {
         if (ActionButton::Draw("Stop", ImVec2(kPreviewSize * tok::param_resynth_params_ratio, 0)))
            n->StopPath();
      }
      else
      {
         if (ActionButton::Draw("Play path", ImVec2(kPreviewSize * tok::param_resynth_params_ratio, 0)))
            n->PlayPath();
      }
      // looping only means something once a path is recorded
      if (n->Path().size() >= tok::param_resynth_params_limit)
      {
         Ui::DrawParam(n, ResynthParamsRow::Toggle<&ResynthNode::loopPath>("loop path"));
         Ui::SameRow();
      }
      if (ActionButton::Draw("clear"))
         n->ClearPath();

      NodeSeparator();
      Ui::DrawParams(n, kResynthParamsRows);

      NodeSeparator();
      ImGui::TextDisabled("generation %d", n->Generation());
      if (ActionButton::Draw("Iterate", ImVec2(kPreviewSize * tok::param_resynth_params_ratio, 0)))
         n->StepOnce();
      Ui::SameRow();
      if (ActionButton::Draw("Reset", ImVec2(kPreviewSize * tok::param_resynth_params_ratio, 0)))
         n->Reset();
      if (ActionButton::Draw("Randomise FX", ImVec2(kPreviewSize, 0)))
         n->Randomise();

      if (PanelKit::FoldHeader("pad corners", true, false))
      {
         static const char* kCornerNames[4] = { "bottom left", "bottom right", "top left", "top right" };
         for (int c = 0; c < ResynthNode::kCorners; c++)
         {
            ImGui::PushID(c);
            char label[32];
            snprintf(label, sizeof(label), "%s##c%d", kCornerNames[c], c);
            DropdownButton(label, ResynthNode::EffectNames(), n->cornerEffect[c],
                           [n, c](int i) { n->cornerEffect[c] = i; });
            ModSlider("amount", &n->cornerAmount[c], 0.0f, 1.0f);
            ImGui::PopID();
         }
      }
      Ui::DrawParam(n, ResynthParamsRow::Toggle<&ResynthNode::autoIterate>("auto iterate"));
      if (n->autoIterate)
         Ui::DrawParam(n, ResynthParamsRow::Slider<&ResynthNode::stepsPerBeat>("steps / beat", params::Resynth::steps_per_beat_lo, params::Resynth::steps_per_beat_hi));
      Ui::DrawParam(n, ResynthParamsRow::Slider<&ResynthNode::seed>("seed", params::Resynth::seed_lo, params::Resynth::seed_hi).Fmt("%.0f").Step(1.0f));
   }


   // ---- macro node bodies -------------------------------------------------
   // Every macro body is drawn the same way: the control sits flush at the
   // top-left of its cell, its label is centred directly underneath, and the
   // cell is reserved afterwards so the node sizes to the control instead of
   // to a 190px image preview that isn't there. MacroBodyEnd is the second
   // half of that contract for the hand-drawn controls; the knob and fader
   // widgets already do it themselves (see KnobFloat's tail).
   void MacroBodyEnd(const ImVec2& origin, float cellW, float contentH, const std::string& caption,
                     std::string* editLabel = nullptr)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const ImU32 capCol = isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::edge_mid_light_fixed);
      const float textH = ImGui::GetTextLineHeight();
      const float capY = origin.y + contentH + tok::space_1;
      bool renaming = false;
      if (editLabel != nullptr)
      {
         // Double-click the caption to rename in place; Enter / click away commits.
         static ImGuiID sRenaming = 0;
         const ImGuiID rid = ImGui::GetID("##macrorename");
         ImGui::SetCursorScreenPos(ImVec2(origin.x, capY));
         UiInteract::Hit("##macrocaptionhit", ImVec2(cellW, textH));
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            sRenaming = rid;
         if (sRenaming == rid)
         {
            renaming = true;
            char buf[64];
            snprintf(buf, sizeof(buf), "%s", editLabel->c_str());
            ImGui::SetCursorScreenPos(ImVec2(origin.x, capY - tok::space_0_5));
            ImGui::SetNextItemWidth(cellW);
            if (ImGui::IsWindowAppearing() || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
               UiInteract::FocusHere();
            if (FieldWell::InputTextWithHint("##macrorenamefield", "name", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
               *editLabel = buf;
               sRenaming = 0;
            }
            else if (!ImGui::IsItemActive() && !ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
               *editLabel = buf;
               sRenaming = 0;
            }
         }
      }
      if (!caption.empty() && !renaming)
      {
         const ImVec2 ts = ImGui::CalcTextSize(caption.c_str());
         if (ts.x <= cellW)
         {
            dl->AddText(ImVec2(origin.x + (cellW - ts.x) * 0.5f, capY), capCol, caption.c_str());
         }
         else
         {
            // Clip rather than let a long user-typed label widen the node.
            dl->PushClipRect(ImVec2(origin.x, capY), ImVec2(origin.x + cellW, capY + textH), true);
            dl->AddText(ImVec2(origin.x, capY), capCol, caption.c_str());
            dl->PopClipRect();
         }
      }
      ImGui::SetCursorScreenPos(origin);
      Ui::Claim(ImVec2(cellW, contentH + tok::space_1 + textH));
   }


   // Same diameter every other audio/modulator knob in the app uses
   // (kKnobStd): KnobFloat's face shading and tick are pixel-offset constants
   // tuned for that size, so scaling the diameter up on its own (108 in an
   // earlier pass) didn't make a bigger knob, it made a flat disc with a thin
   // ring. What changed is the *cell*, not the knob - kMacroCell instead of
   // kPreviewSize.
   void DrawMacroKnobBody(MacroKnobNode* n)
   {
      const std::string caption = n->label.empty() ? std::string("macro") : n->label;
      ModKnob(caption.c_str(), &n->value, 0.0f, 1.0f, "%.3f", kKnobStd, kMacroCell);
   }


   // The name field is the only param most macro nodes have, and it used to be
   // kParamWidth (168px) plus a visible "name" label - together wider than any
   // macro body, so opening params stretched the node and left the dead space
   // around the control that the body metrics had just removed. Sized to the
   // node's own body cell instead, with the word "name" as a placeholder
   // rather than a label taking layout width.
   void MacroNameField(std::string& label, float cellW)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "%s", label.c_str());
      ImGui::SetNextItemWidth(cellW);
      if (FieldWell::InputTextWithHint("##macroname", "name", buf, sizeof(buf)))
         label = buf;
   }


   // Plugin mode only (docs/plans/vst-au/README.md 8.2): which of the host's 32 parameters drives
   // this macro. The value names itself ("slot 7"), so it needs no label beside it.
   void MacroHostSlotField(int& slot, float cellW, const char* id = "##hostslot", const char* prefix = "slot")
   {
      if (!Platform::PluginModeActive())
         return;
      static std::map<std::string, std::vector<std::string>> sItems;
      std::vector<std::string>& names = sItems[prefix];
      if (names.empty())
      {
         names.push_back(std::string("no ") + prefix);
         for (int i = 1; i <= PluginEngine::kSlotCount; i++)
            names.push_back(std::string(prefix) + " " + std::to_string(i));
      }
      std::vector<const char*> items;
      for (const std::string& s : names)
         items.push_back(s.c_str());
      int cur = std::clamp(slot, 0, PluginEngine::kSlotCount);
      if (FormParts::BareCombo(id, &cur, items.data(), (int)items.size(), cellW) && cur != slot)
      {
         PushUndoCheckpoint();
         slot = cur;
      }
   }


   void DrawMacroKnobParams(MacroKnobNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      MacroHostSlotField(n->hostSlot, kMacroCell);
   }


   void DrawMacroSliderBody(MacroSliderNode* n)
   {
      // kFaderH (the Mixer / Gain fader), not kPreviewSize - 12 and not a private
      // height: one fader size app-wide. No level meter: a macro value is a
      // control, not a signal, and the accent fill already shows it.
      const std::string caption = n->label.empty() ? std::string("slider") : n->label;
      ModKnob(caption.c_str(), &n->value, 0.0f, 1.0f, "%.2f", kFaderH, kMacroCell, AudioWidgetStyle::VFader);
   }


   void DrawMacroSliderParams(MacroSliderNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      MacroHostSlotField(n->hostSlot, kMacroCell);
   }


   void DrawMacroBipolarKnobBody(MacroBipolarKnobNode* n)
   {
      const std::string caption = n->label.empty() ? std::string("bipolar") : n->label;
      ModKnob(caption.c_str(), &n->value, -1.0f, 1.0f, "%.2f", kKnobStd, kMacroCell, AudioWidgetStyle::KnobBipolar);
   }


   void DrawMacroBipolarKnobParams(MacroBipolarKnobNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      MacroHostSlotField(n->hostSlot, kMacroCell);
   }


   void DrawMacroToggleBody(MacroToggleNode* n)
   {
      // The shared Switch, enlarged to read as a front-panel control.
      constexpr float kScale = tok::param_scale;
      const float btnW = Switch::kW * kScale, btnH = Switch::kH * kScale;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(origin.x + (kMacroCell - btnW) * 0.5f, origin.y + tok::space_0_5));
      ImGui::PushID(901);
      bool v = n->state;
      if (Switch::Box("##macrotogglebtn", &v, kScale))
      {
         PushUndoCheckpoint();   // snapshot before the flip lands
         n->state = v;
      }
      ImGui::PopID();
      ImGui::SetCursorScreenPos(origin);
      MacroBodyEnd(origin, kMacroCell, btnH + tok::space_1, n->label.empty() ? std::string("toggle") : n->label, &n->label);
   }


   namespace
   {
      using MacroToggleParamsRow = params::Row<MacroToggleNode>;
   }

   void DrawMacroToggleParams(MacroToggleNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      MacroHostSlotField(n->hostSlot, kMacroCell);
      Ui::DrawParam(n, MacroToggleParamsRow::Toggle<&MacroToggleNode::state>("state"));
   }


   void DrawMacroTriggerBody(MacroTriggerNode* n)
   {
      // The shared ActionButton; held or just fired it takes the Primary (accent) look.
      constexpr float kBtnH = tok::param_btn_h;
      const float btnW = tok::param_btn_w;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(origin.x + (kMacroCell - btnW) * 0.5f, origin.y + tok::space_0_5));
      ImGui::PushID(902);
      ActionButton::Draw("bang##macrotriggerbtn", ImVec2(btnW, kBtnH),
                         n->flash > 0.0f ? ActionButton::Kind::Primary : ActionButton::Kind::Plain);
      n->pressed = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
      {
         n->justTriggered = true;
         n->flash = 1.0f;
      }
      ImGui::PopID();

      if (n->pressed)
         n->flash = 1.0f;
      else if (n->flash > 0.0f)
      {
         n->flash -= ImGui::GetIO().DeltaTime * tok::motion_rate_flash_decay;
         if (n->flash < 0.0f) n->flash = 0.0f;
      }
      ImGui::SetCursorScreenPos(origin);
      MacroBodyEnd(origin, kMacroCell, kBtnH + tok::space_1, n->label.empty() ? std::string("bang") : n->label, &n->label);
   }


   void DrawMacroTriggerParams(MacroTriggerNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      MacroHostSlotField(n->hostSlot, kMacroCell);
   }


   void DrawMacroNumBoxBody(MacroNumBoxNode* n)
   {
      const float boxW = kMacroCell - tok::space_2;
      const float boxH = kMacroRowH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 bTL(origin.x + tok::space_1, origin.y);
      const ImVec2 bBR(bTL.x + boxW, bTL.y + boxH);

      (void)bBR;
      // The shared field well (DragFloat frame in the FieldWell style), same as every numeric field.
      ImGui::SetCursorScreenPos(ImVec2(bTL.x, bTL.y + (boxH - ImGui::GetFrameHeight()) * 0.5f));
      // Unbounded: no min/max params, so drag speed scales with the value's
      // own magnitude and the DragFloat gets no clamp (v_min >= v_max).
      float speed = std::max(0.01f, std::abs(n->value) * 0.01f);
      FieldWell::PushStyle(ImGui::GetID("##macronumboxdrag"));
      ImGui::SetNextItemWidth(boxW);
      ImGui::DragFloat("##macronumboxdrag", &n->value, speed, 0.0f, 0.0f,
                       (std::abs(n->value) >= 10.0f) ? UiFmt::Spec::F1 : UiFmt::Spec::F3);
      FieldWell::PopStyle();

      MacroBodyEnd(origin, kMacroCell, boxH, n->label.empty() ? std::string("value") : n->label);
   }


   void DrawMacroNumBoxParams(MacroNumBoxNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      MacroHostSlotField(n->hostSlot, kMacroCell);
      // No min/max: the number box takes any value the user types, and each
      // destination clamps it to its own declared range on the way in (see
      // the MacroNumBox case in the modulation apply loop).
   }


   void DrawMacroRadioSelectorBody(MacroRadioSelectorNode* n)
   {
      const int count = std::clamp(n->count, 2, 8);
      const float gap = tok::space_0_5;
      const float btnH = kMacroRowH;
      const float btnW = (kMacroWideCell - (count - 1) * gap) / (float)count;
      const ImVec2 origin = ImGui::GetCursorScreenPos();

      for (int b = 0; b < count; b++)
      {
         ImGui::SetCursorScreenPos(ImVec2(origin.x + b * (btnW + gap), origin.y));
         ImGui::PushID(b + 700);

         const std::string btnText = std::to_string(b + 1);
         if (ActionButton::Draw(btnText.c_str(), ImVec2(btnW, btnH),
                                b == n->selected ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
         {
            PushUndoCheckpoint();
            n->selected = b;
         }
         ImGui::PopID();
      }

      MacroBodyEnd(origin, kMacroWideCell, btnH, n->label.empty() ? std::string("selector") : n->label);
   }


   namespace
   {
      using MacroRadioSelectorParamsRow = params::Row<MacroRadioSelectorNode>;
   }

   void DrawMacroRadioSelectorParams(MacroRadioSelectorNode* n)
   {
      MacroNameField(n->label, kMacroWideCell);
      MacroHostSlotField(n->hostSlot, kMacroWideCell);
      // Clamped to 8 because the body only ever draws 8 segments - a count of
      // 16 used to leave half the selector unreachable.
      Ui::DrawParam(n, MacroRadioSelectorParamsRow::SliderInt<&MacroRadioSelectorNode::count>("count", params::MacroRadioSelector::count_lo, params::MacroRadioSelector::count_hi));
   }


   void DrawMacroStepGateBody(MacroStepGateNode* n)
   {
      const float gap = tok::space_0_5;
      const float stepH = kMacroRowH;
      const float stepW = (kMacroWideCell - tok::param_macro_step_gate_body_ratio * gap) / tok::param_step_w_ratio;
      const ImVec2 origin = ImGui::GetCursorScreenPos();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const int playStep = n->CurrentStep();

      for (int s = 0; s < 8; s++)
      {
         const float sx = origin.x + s * (stepW + gap);
         const ImVec2 sTL(sx, origin.y);
         const ImVec2 sBR(sx + stepW, origin.y + stepH);

         ImGui::SetCursorScreenPos(sTL);
         ImGui::PushID(s + 800);
         UiInteract::Hit("##stepgatebtn", ImVec2(stepW, stepH));
         const bool hovered = ImGui::IsItemHovered();
         if (ImGui::IsItemClicked())
         {
            PushUndoCheckpoint();
            n->pattern ^= (1 << s);
         }
         ImGui::PopID();

         const bool isOn = (n->pattern & (1 << s)) != 0;
         StepCell::Draw(dl, sTL, sBR, isOn, s == playStep && Transport::Instance().IsPlaying(), hovered);
      }

      MacroBodyEnd(origin, kMacroWideCell, stepH, n->label.empty() ? std::string("step gate") : n->label);
   }


   namespace
   {
      using MacroStepGateParamsRow = params::Row<MacroStepGateNode>;
   }

   void DrawMacroStepGateParams(MacroStepGateNode* n)
   {
      MacroNameField(n->label, kMacroWideCell);
      MacroHostSlotField(n->hostSlot, kMacroWideCell);
      Ui::DrawParam(n, MacroStepGateParamsRow::Slider<&MacroStepGateNode::rateBeats>("rate", params::MacroStepGate::rate_beats_lo, params::MacroStepGate::rate_beats_hi).Fmt("%.2f beats"));
   }


   namespace
   {
      using MidiCCParamsRow = params::Row<MidiCCNode>;
      const MidiCCParamsRow kMidiCCParamsRows[] = {
         MidiCCParamsRow::Slider<&MidiCCNode::low>("low", 0.0f, 1.0f),
         MidiCCParamsRow::Slider<&MidiCCNode::high>("high", 0.0f, 1.0f),
         MidiCCParamsRow::Toggle<&MidiCCNode::invert>("invert"),
      };
   }

   void DrawMidiCCParams(MidiCCNode* n)
   {
      if (n->IsLearning())
      {
         if (ActionButton::Draw("Listening... move a control", ImVec2(kPreviewSize, 0), ActionButton::Kind::Learn))
            n->CancelLearn();
      }
      else if (ActionButton::Draw(n->IsBound() ? "Re-learn" : "MIDI Learn", ImVec2(kPreviewSize, 0)))
      {
         MidiLearnCancelAll();
         n->StartLearn();
      }
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      PopTextWrapWidth();

      if (n->IsBound())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::Text("bound: %s", n->BindingLabel().c_str());
         PopTextWrapWidth();
      }
      ImGui::ProgressBar(n->Value01(), ImVec2(kParamWidth, 0), "");

      Ui::DrawParams(n, kMidiCCParamsRows);
   }


   namespace
   {
      using MidiTriggerParamsRow = params::Row<MidiTriggerNode>;
      const MidiTriggerParamsRow kMidiTriggerParamsRows[] = {
         MidiTriggerParamsRow::Slider<&MidiTriggerNode::hold>("hold", params::MidiTrigger::hold_lo, params::MidiTrigger::hold_hi),
         MidiTriggerParamsRow::Toggle<&MidiTriggerNode::velocitySensitive>("velocity sensitive"),
      };
   }

   void DrawMidiTriggerParams(MidiTriggerNode* n)
   {
      Ui::DrawParam(n, MidiTriggerParamsRow::Choice<&MidiTriggerNode::mode>("device type", &MidiTriggerNode::ModeNames));

      if (n->IsLearning())
      {
         if (ActionButton::Draw("Listening... hit a pad", ImVec2(kPreviewSize, 0), ActionButton::Kind::Learn))
            n->CancelLearn();
      }
      else
      {
         const bool lit = n->Value01() > 0.01f;
         if (ActionButton::Draw(n->IsBound() ? "Re-learn" : "MIDI Learn", ImVec2(kPreviewSize, 0), (lit) ? ActionButton::Kind::Go : ActionButton::Kind::Plain))
         {
            MidiLearnCancelAll();
            n->StartLearn();
         }
      }
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      PopTextWrapWidth();

      if (n->IsBound())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::Text("bound: %s", n->BindingLabel().c_str());
         PopTextWrapWidth();
      }
      ImGui::ProgressBar(n->Value01(), ImVec2(kParamWidth, 0), "");

      if (n->mode == MidiTriggerNode::kKeyboard)
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("value = note number / 127, held until the next key");
         PopTextWrapWidth();
      }
      else
      {
         Ui::DrawParams(n, kMidiTriggerParamsRows);
      }
   }


   void DrawMacroXYBody(MacroXYNode* n)
   {
      const float size = kMacroWideCell;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      UiInteract::Hit("##macroxy", ImVec2(size, size));
      if (ImGui::IsItemActive())
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), ScopeGridCol());
      }
      const std::vector<MacroXYNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, isLight ? tok::Base(tok::col::accent, tok::param_line_alpha_lg) : tok::Base(tok::col::accent_pale_muted_warm, tok::param_line_alpha_sm), tok::param_macro_xy_body_stroke);
      }
      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? tok::Base(tok::col::text_err)
                     : n->IsPlayingPath()   ? (isLight ? tok::Base(tok::col::state_ok_dark_vivid_lo) : tok::Base(tok::col::state_ok))
                                            : (isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation));
      dl->AddCircleFilled(orb, tok::param_macro_xy_body_r, orbColor);
      dl->AddCircle(orb, tok::param_macro_xy_body_r, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), 0, tok::param_ring_stroke_lg);
      AudioViz::Border(dl, origin, br);
   }


   namespace
   {
      using MacroXYParamsRow = params::Row<MacroXYNode>;
      const MacroXYParamsRow kMacroXYParamsRows[] = {
         MacroXYParamsRow::Toggle<&MacroXYNode::loopPath>("loop"),
         MacroXYParamsRow::Slider<&MacroXYNode::speed>("speed", params::MacroXY::speed_lo, params::MacroXY::speed_hi),
      };
   }

   void DrawMacroXYParams(MacroXYNode* n)
   {
      ImGui::TextDisabled("x %s   y %s", UiFmt::F3(n->padX).c, UiFmt::F3(n->padY).c);

      if (n->IsRecordingPath())
      {
         if (ActionButton::Draw("Stop rec", ImVec2(kPreviewSize * tok::param_macro_xy_params_ratio, 0), ActionButton::Kind::Record))
            n->StopRecording();
      }
      else if (ActionButton::Draw("Rec path", ImVec2(kPreviewSize * tok::param_macro_xy_params_ratio, 0)))
      {
         n->StartRecording();
      }
      Ui::SameRow();
      if (n->IsPlayingPath())
      {
         if (ActionButton::Draw("Stop", ImVec2(kPreviewSize * tok::param_macro_xy_params_ratio, 0)))
            n->StopPath();
      }
      else if (ActionButton::Draw("Play path", ImVec2(kPreviewSize * tok::param_macro_xy_params_ratio, 0)))
      {
         n->PlayPath();
      }
      Ui::SameRow();
      if (ActionButton::Draw("Clear", ImVec2(kPreviewSize * tok::param_macro_xy_params_ratio, 0)))
         n->ClearPath();
      Ui::DrawParams(n, kMacroXYParamsRows);
      if (Platform::PluginModeActive())
      {
         MacroHostSlotField(n->hostSlot, kPreviewSize * tok::param_macro_host_slot_field_cell_w, "##hostslotx", "x slot");
         FormParts::Inline();
         MacroHostSlotField(n->hostSlotY, kPreviewSize * tok::param_macro_host_slot_field_cell_w, "##hostsloty", "y slot");
      }
   }
}
